// Multijugador en el cliente: abrir la partida en la LAN (servidor integrado) y dibujar a los
// demás jugadores.
#include <cmath>
#include <format>

#include "client/audio.h"
#include "client/entity_renderer.h"
#include "client/particles.h"
#include "client/game.h"
#include "client/hud.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "core/log.h"

namespace mcw {

void Game::openToLan() {
  if (server_) {
    message_ = "La partida ya está abierta";
    messageDetail_ = lanAddressText();
    openScreen(Screen::Message);
    return;
  }
  net::Server::Config cfg;
  cfg.hostName = settings_.playerName;
  cfg.motd = settings_.playerName + " - " + (level_.name.empty() ? std::string("Mundo") : level_.name);
  cfg.guestMode = session_->player().creative() ? 1 : 0;
  cfg.difficulty = level_.difficulty;
  cfg.viewDistance = std::clamp(settings_.renderDistance, 3, 8);
  if (save_) cfg.playerDataDir = save_->dir() / "playerdata";  // los invitados se guardan con el mundo
  auto server = std::make_unique<net::Server>(*session_, cfg);
  server->setHostSkin(localSkinPng_, localSkin_.slim, static_cast<u8>(settings_.skinParts));
  std::string err;
  // El puerto de siempre de Minecraft; si está ocupado, cualquiera libre
  if (!server->start(25565, &err) && !server->start(0, &err)) {
    message_ = "No se pudo abrir la partida";
    messageDetail_ = err;
    openScreen(Screen::Message);
    return;
  }
  server_ = std::move(server);
  net::Server* srv = server_.get();
  session_->setBlockListener([srv](const glm::ivec3& p, BlockState s) { srv->blockChanged(p, s); });
  lanBroadcaster_ = std::make_unique<net::LanBroadcaster>(cfg.motd, server_->port());
  chatMessage("Partida abierta en la LAN: " + lanAddressText(), 0xFFFF55);
  message_ = "Partida abierta en la LAN";
  messageDetail_ = lanAddressText();
  openScreen(Screen::Message);
}

std::string Game::lanAddressText() const {
  if (!server_) return "";
  const auto ips = net::localAddresses();
  std::string out;
  for (std::size_t i = 0; i < ips.size() && i < 2; i++) out += (i ? " o " : "") + ips[i] + ":" + std::to_string(server_->port());
  if (out.empty()) out = "localhost:" + std::to_string(server_->port());
  return out;
}

void Game::applyPeerSkin(const std::string& uuid, std::span<const u8> png, bool slim) {
  if (!entityRenderer_ || png.size() > net::kMaxSkinBytes) return;
  const auto img = decodePng(png);
  if (!img || !validSkinSize(img->width, img->height)) return;
  if (const auto prepared = prepareSkin(*img, slim)) {
    entityRenderer_->setSkin(uuid, *prepared);
    peerSkins_.insert(uuid);
  }
}

void Game::dropPeerSkins() {
  if (entityRenderer_)
    for (const std::string& k : peerSkins_) entityRenderer_->removeSkin(k);
  peerSkins_.clear();
}

void Game::stopNet() {
  dropPeerSkins();
  if (session_) session_->setBlockListener({});
  if (server_) server_->stop();
  server_.reset();
  lanBroadcaster_.reset();
  others_.clear();
  if (terrain_) terrain_->setExtraCenters({}, 0);
}

void Game::tickNet() {
  if (net_ && inWorld_) {
    Player& p = session_->player();
    if (netPositioned_ && spawned_) net_->sendPosition(p.pos, p.yaw, p.pitch, p.onGround);
    if (p.inventory.selectedIndex() != netSelected_) {
      netSelected_ = p.inventory.selectedIndex();
      net_->sendHeldItem(netSelected_);
    }
    if (p.sneaking != netSneaking_) {
      netSneaking_ = p.sneaking;
      net_->sendEntityAction(p.sneaking ? 0 : 1);
    }
    if (p.sprinting != netSprinting_) {
      netSprinting_ = p.sprinting;
      net_->sendEntityAction(p.sprinting ? 3 : 4);
    }
    // Ventana del servidor cerrada por el jugador
    if (netWindow_ != 0 && (!session_->menu() || session_->menu()->kind() == MenuKind::Inventory)) {
      net_->sendCloseWindow(netWindow_);
      netWindow_ = 0;
    }
    // Criaturas, objetos y jugadores: se acercan a donde dice el servidor
    for (auto& [eid, ne] : netEntities_) {
      if (ne.kind == 1) {
        if (Mob* m = session_->mobById(ne.localId)) {
          m->prevPos = m->pos;
          m->prevYaw = m->yaw;
          m->prevHeadYaw = m->headYaw;
          m->prevLimbAmount = m->limbAmount;
          m->pos += (ne.target - m->pos) * 0.5;
          m->yaw = ne.yaw;
          m->headYaw = ne.head;
          m->pitch = ne.pitch;
          const float speed = static_cast<float>(glm::length(glm::dvec2(m->pos.x - m->prevPos.x, m->pos.z - m->prevPos.z)));
          m->limbAmount += (std::min(1.0f, speed * 4.0f) - m->limbAmount) * 0.4f;
          m->limbSwing += m->limbAmount;
          if (m->hurtTime > 0) m->hurtTime--;
          if (m->deathTime > 0 && m->deathTime < 20) m->deathTime++;
          m->age++;
        }
      } else if (ne.kind == 2) {
        if (ItemEntity* it = session_->itemById(ne.localId)) {
          it->prevPos = it->pos;
          it->pos += (ne.target - it->pos) * 0.5;
          it->age++;
        }
      } else if (auto o = others_.find(eid); o != others_.end()) {
        OtherPlayer& op = o->second;
        op.prevPos = op.pos;
        op.prevYaw = op.yaw;
        op.pos += (ne.target - op.pos) * 0.5;
        op.yaw = ne.head != 0 ? ne.head : ne.yaw;
        op.pitch = ne.pitch;
        const float speed = static_cast<float>(glm::length(glm::dvec2(op.pos.x - op.prevPos.x, op.pos.z - op.prevPos.z)));
        op.prevLimbAmount = op.limbAmount;
        op.limbAmount += (std::min(1.0f, speed * 4.0f) - op.limbAmount) * 0.4f;
        op.limbSwing += op.limbAmount;
        if (op.swing > 0) op.swing = std::max(0.0f, op.swing - 0.17f);
      }
    }
  }
  if (!server_) return;
  server_->tick(runTime_, worldTime_);
  if (lanBroadcaster_) lanBroadcaster_->tick(runTime_);
  for (const std::string& line : server_->takeChat()) chatMessage(line);
  terrain_->setExtraCenters(server_->wantedChunkCenters(), server_->config().viewDistance);
  // Jugadores invitados para dibujarlos (con la animación de andar)
  std::map<i32, OtherPlayer> next;
  for (const auto& v : server_->players()) {
    OtherPlayer o;
    if (auto it = others_.find(v.eid); it != others_.end()) o = it->second;
    o.eid = v.eid;
    o.name = v.name;
    o.uuid = v.uuid;
    o.parts = v.skinParts;
    if (v.skinVersion != o.skinVersion) {  // su skin ha llegado o ha cambiado
      o.skinVersion = v.skinVersion;
      if (v.skin) applyPeerSkin(v.uuid, *v.skin, v.skinSlim);
    }
    o.prevPos = o.pos == glm::dvec3(0) ? v.pos : o.pos;
    o.pos = v.pos;
    o.prevYaw = o.yaw;
    o.yaw = v.yaw;
    o.pitch = v.pitch;
    o.sneaking = v.sneaking;
    const float speed = static_cast<float>(glm::length(glm::dvec2(o.pos.x - o.prevPos.x, o.pos.z - o.prevPos.z)));
    o.prevLimbAmount = o.limbAmount;
    o.limbAmount += (std::min(1.0f, speed * 4.0f) - o.limbAmount) * 0.4f;
    o.limbSwing += o.limbAmount;
    o.swing = v.swinging ? std::max(o.swing - 0.2f, 0.0f) : 0.0f;
    if (v.swinging && o.swing <= 0) o.swing = 1.0f;
    next[v.eid] = o;
  }
  others_ = std::move(next);
  // Los que se han ido se llevan su skin
  std::set<std::string> here;
  for (const auto& [eid, o] : others_) here.insert(o.uuid);
  for (auto it = peerSkins_.begin(); it != peerSkins_.end();)
    if (!here.count(*it)) {
      if (entityRenderer_) entityRenderer_->removeSkin(*it);
      it = peerSkins_.erase(it);
    } else {
      ++it;
    }
}

void Game::drawOtherPlayers(const Camera& view, float partial, const FogParams& fog, const std::function<glm::vec3(const glm::dvec3&)>& light) {
  for (const auto& [eid, o] : others_) {
    PlayerPose pp;
    pp.pos = o.prevPos + (o.pos - o.prevPos) * static_cast<double>(partial);
    pp.bodyYaw = o.prevYaw + (o.yaw - o.prevYaw) * partial;
    pp.headYaw = pp.bodyYaw;
    pp.pitch = o.pitch;
    pp.limbAmount = o.prevLimbAmount + (o.limbAmount - o.prevLimbAmount) * partial;
    pp.limbSwing = o.limbSwing - o.limbAmount * (1.0f - partial);
    pp.attack = o.swing > 0 ? 1.0f - o.swing : 0.0f;
    pp.sneaking = o.sneaking;
    // Su skin si la ha mandado; si no, Steve o Alex según su UUID, como en 1.8
    pp.skin = {o.uuid, defaultSkinSlim(o.uuid), o.parts};
    entityRenderer_->drawPlayer(pp, view, light(pp.pos + glm::dvec3(0, 1, 0)), fog);
  }
}

void Game::drawNameTags(float partial) {
  // Nombre encima de cada jugador (proyectado a la pantalla)
  for (const auto& [eid, o] : others_) {
    const glm::dvec3 p = o.prevPos + (o.pos - o.prevPos) * static_cast<double>(partial) + glm::dvec3(0, o.sneaking ? 2.0 : 2.3, 0);
    const glm::vec3 rel(p - cam_.pos);
    if (glm::length(rel) > 64) continue;
    const glm::vec4 clip = cam_.viewProj * glm::vec4(rel, 1.0f);
    if (clip.w <= 0.1f) continue;
    const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    if (std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f) continue;
    const float x = (ndc.x * 0.5f + 0.5f) * ui_->guiWidth(), y = (0.5f - ndc.y * 0.5f) * ui_->guiHeight();
    const std::string name = asciiText(o.name);
    const float w = static_cast<float>(ui_->textWidth(name));
    ui_->rect(x - w / 2 - 1, y - 1, w + 2, 9, 0x40000000);
    ui_->text(x - w / 2, y, name, o.sneaking ? 0x808080 : 0xFFFFFF, false);
  }
}

}  // namespace mcw

// ---------------------------------------------------------------------------
// Jugar en un servidor (MC-WEB o 1.8 en modo offline)
// ---------------------------------------------------------------------------
namespace mcw {
namespace {

/// Casilla del protocolo (ventana 0) -> casilla de nuestro inventario (-1 si no hay).
int invSlotFromNet(int s) {
  if (s >= 9 && s <= 35) return s;
  if (s >= 36 && s <= 44) return s - 36;
  return -1;
}
int netSlotFromInv(int i) { return i < 9 ? 36 + i : i; }

/// Quita los códigos de color (§x) del chat.
std::string stripFormatting(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
      i += 2;
      continue;
    }
    out += s[i];
  }
  return out;
}

}  // namespace

void Game::connectToServer(const std::string& address) {
  std::string host;
  int port = 25565;
  net::splitAddress(address, host, port);
  if (host.empty()) return;
  std::unique_ptr<net::Transport> t = net::tcpAvailable() ? net::connectTcp(host, port) : net::connectViaProxy(settings_.proxyUrl, host, port);
  netAddress_ = address;
  net_ = std::make_unique<net::Client>(std::move(t), host, port, settings_.playerName);
  net_->setViewDistance(settings_.renderDistance);
  net_->setSkinParts(static_cast<u8>(settings_.skinParts));
  net_->setSkin(localSkinPng_, localSkin_.slim);
  message_ = "Conectando con el servidor...";
  messageDetail_ = address;
  openScreen(Screen::Message);
}

void Game::leaveRemote(const std::string& reason) {
  if (net_) net_->disconnect();
  net_.reset();
  netEntities_.clear();
  netMobEid_.clear();
  others_.clear();
  dropPeerSkins();
  netWindow_ = 0;
  netPositioned_ = false;
  if (inWorld_) {
    terrain_->clear();
    terrain_->setRemote(false);
    session_ = std::make_unique<GameSession>(*terrain_, 0);
    inWorld_ = false;
    spawned_ = false;
  }
  if (!reason.empty()) {
    message_ = "Se ha perdido la conexión";
    messageDetail_ = reason;
    openScreen(Screen::Message);
  } else {
    openScreen(Screen::Multiplayer);
  }
}

void Game::enterRemoteWorld(const net::ClientEvent& e) {
  level_ = LevelInfo{};
  level_.name = netAddress_;
  level_.difficulty = e.c;
  level_.allowCommands = true;
  save_.reset();
  terrain_->clear();
  terrain_->setStorage({});
  terrain_->setRemote(true);
  session_ = std::make_unique<GameSession>(*terrain_, 0);
  particles_ = std::make_unique<ParticleSystem>();
  particles_->initGL(terrain_->textureArray());
  subtitles_.clear();
  netEntities_.clear();
  netMobEid_.clear();
  others_.clear();
  netPositioned_ = false;
  netSelected_ = -1;
  // Lo que hace el jugador se manda al servidor
  auto hooks = std::make_shared<GameSession::RemoteHooks>();
  net::Client* c = net_.get();
  hooks->dig = [c](int status, const glm::ivec3& p, int face) { c->sendDig(status, p, face); c->sendSwing(); };
  hooks->use = [c](const glm::ivec3& p, int face, const glm::vec3& cursor, const ItemStack& held) { c->sendPlace(p, face, held, cursor); };
  hooks->useEntity = [this, c](u32 mobId, bool attack) {
    if (auto it = netMobEid_.find(mobId); it != netMobEid_.end()) {
      c->sendSwing();
      c->sendUseEntity(it->second, attack);
    }
  };
  hooks->drop = [c](bool whole) { c->sendDig(whole ? 3 : 4, {0, 0, 0}, 0); };
  hooks->swing = [c] { c->sendSwing(); };
  session_->setRemote(hooks);
  session_->setMode(e.a == 1 ? GameMode::Creative : GameMode::Survival);
  keepPlayerPos_ = true;
  spawned_ = false;
  settledAt_ = -1;
  loggedLoaded_ = false;
  inWorld_ = true;
  chat_.clear();
  chatMessage("Conectado a " + netAddress_, 0xFFFF55);
  setScreen(Screen::None);
}

void Game::pollNet() {
  if (!net_) return;
  net_->poll();
  for (const net::ClientEvent& e : net_->takeEvents()) {
    if (!net_) break;
    if (e.type == net::ClientEvent::Type::Joined) {
      enterRemoteWorld(e);
      continue;
    }
    if (e.type == net::ClientEvent::Type::Disconnected) {
      leaveRemote(e.text);
      return;
    }
    if (inWorld_) handleNetEvent(e);
  }
}

void Game::handleNetEvent(const net::ClientEvent& e) {
  using T = net::ClientEvent::Type;
  Player& p = session_->player();
  switch (e.type) {
    case T::Chunk: terrain_->receiveChunk(std::make_unique<Chunk>(std::move(*e.chunk)), e.flag, static_cast<u16>(e.a)); break;
    case T::UnloadChunk: terrain_->dropChunk({e.pos.x, e.pos.z}); break;
    case T::BlockChange:
      for (const auto& [pos, st] : e.blocks) terrain_->setBlock(pos.x, pos.y, pos.z, st);
      break;
    case T::Explosion:
      for (const auto& [pos, st] : e.blocks) terrain_->setBlock(pos.x, pos.y, pos.z, 0);
      particles_->explosion({e.x, e.y, e.z});
      audio_->play(Sfx::Explosion, {e.x, e.y, e.z}, 4.0f, 0.9f);
      break;
    case T::PlayerPosition: {
      glm::dvec3 pos(e.x, e.y, e.z);
      if (e.a & 0x01) pos.x += p.pos.x;
      if (e.a & 0x02) pos.y += p.pos.y;
      if (e.a & 0x04) pos.z += p.pos.z;
      float yaw = e.yaw, pitch = e.pitch;
      if (e.a & 0x08) yaw += p.yaw;
      if (e.a & 0x10) pitch += p.pitch;
      p.pos = p.prevPos = pos;
      p.motion = glm::dvec3(0);
      p.yaw = cam_.yaw = yaw;
      p.pitch = cam_.pitch = pitch;
      netPositioned_ = true;
      net_->sendPosition(p.pos, p.yaw, p.pitch, p.onGround);
      break;
    }
    case T::SpawnPosition: spawn_ = glm::dvec3(e.pos) + glm::dvec3(0.5, 0, 0.5); break;
    case T::Time: worldTime_ = e.y; break;
    case T::Health:
      // Un golpe: el destello rojo y el sonido, como con el daño en local
      if (e.f < p.health && !p.dead && spawned_) {
        hurtFlash_ = 1.0f;
        audio_->playFlat(Sfx::Hurt, e.f <= 0 ? 1.0f : 0.9f, e.f <= 0 ? 0.8f : 1.0f);  // (más grave al morir)
      }
      p.health = e.f;
      p.food = e.a;
      p.saturation = static_cast<float>(e.x);
      if (p.health <= 0 && !p.dead) {
        p.dead = true;
        setScreen(Screen::Death);
      }
      break;
    case T::EntityVelocity:
      // A nosotros: el empujón de un golpe o de una explosión
      if (e.eid == net_->entityId()) p.motion = glm::dvec3(e.x, e.y, e.z);
      break;
    case T::Respawn:
      terrain_->clear();
      terrain_->setRemote(true);
      netEntities_.clear();
      netMobEid_.clear();
      others_.clear();
      session_->setMode(e.a == 1 ? GameMode::Creative : GameMode::Survival);
      p.dead = false;
      p.health = 20;
      spawned_ = false;
      netPositioned_ = false;
      if (screen_ == Screen::Death) setScreen(Screen::None);
      break;
    case T::GameState:
      if (e.a == 3) session_->setMode(static_cast<int>(e.f) == 1 ? GameMode::Creative : GameMode::Survival);
      break;
    case T::Chat: chatMessage(stripFormatting(e.text)); break;
    case T::HeldItem:
      p.inventory.select(e.a);
      netSelected_ = e.a;
      break;
    case T::WindowItems:
      if (e.a == 0) {
        for (int s = 0; s < static_cast<int>(e.items.size()); s++)
          if (const int i = invSlotFromNet(s); i >= 0) p.inventory.slot(i) = e.items[s];
      } else if (e.a == netWindow_) {
        const int own = session_->menu() && session_->menu()->kind() == MenuKind::Chest ? 27
                        : session_->menu() && session_->menu()->kind() == MenuKind::Furnace ? 3 : 10;
        for (int s = 0; s < static_cast<int>(e.items.size()); s++) {
          if (s < own) {
            if (own == 27) netChest_.items[s] = e.items[s];
            else if (own == 3) (s == 0 ? netFurnace_.input : s == 1 ? netFurnace_.fuel : netFurnace_.output) = e.items[s];
            continue;
          }
          const int k = s - own;  // 0..26 principal, 27..35 barra rápida
          if (k < 27) p.inventory.slot(9 + k) = e.items[s];
          else if (k < 36) p.inventory.slot(k - 27) = e.items[s];
        }
      }
      break;
    case T::SetSlot:
      if (e.a == -1 && e.b == -1) {
        p.cursor = e.item;
      } else if (e.a == 0) {
        if (const int i = invSlotFromNet(e.b); i >= 0) p.inventory.slot(i) = e.item;
      } else if (e.a == netWindow_ && session_->menu()) {
        const MenuKind k = session_->menu()->kind();
        const int own = k == MenuKind::Chest ? 27 : k == MenuKind::Furnace ? 3 : 10;
        if (e.b < own) {
          if (own == 27) netChest_.items[e.b] = e.item;
          else if (own == 3) (e.b == 0 ? netFurnace_.input : e.b == 1 ? netFurnace_.fuel : netFurnace_.output) = e.item;
        } else {
          const int s = e.b - own;
          if (s < 27) p.inventory.slot(9 + s) = e.item;
          else if (s < 36) p.inventory.slot(s - 27) = e.item;
        }
      }
      break;
    case T::OpenWindow: {
      netWindow_ = e.a;
      std::unique_ptr<Menu> m;
      if (e.text == "minecraft:chest" || e.text == "minecraft:container") {
        netChest_ = {};
        m = std::make_unique<Menu>(MenuKind::Chest, p, nullptr, netChest_.items.data());
      } else if (e.text == "minecraft:crafting_table") {
        m = std::make_unique<Menu>(MenuKind::Crafting, p);
      } else if (e.text == "minecraft:furnace") {
        netFurnace_ = {};
        m = std::make_unique<Menu>(MenuKind::Furnace, p, &netFurnace_);
      } else {
        // Ventanas que aún no tenemos (encantar, yunque...): cerrarla
        net_->sendCloseWindow(e.a);
        netWindow_ = 0;
        chatMessage("Esta ventana aún no está disponible en MC-WEB", 0xAAAAAA);
        break;
      }
      session_->openMenu(std::move(m));
      setScreen(Screen::Menu);
      break;
    }
    case T::CloseWindow:
      if (session_->menu()) session_->closeMenu();
      netWindow_ = 0;
      break;
    case T::PlayerListAdd: netNames_[e.uuid] = e.text; break;
    case T::PlayerListRemove:
      netNames_.erase(e.uuid);
      if (peerSkins_.erase(e.uuid) && entityRenderer_) entityRenderer_->removeSkin(e.uuid);
      break;
    case T::PlayerSkin: applyPeerSkin(e.uuid, e.data, e.flag); break;
    case T::SpawnPlayer: {
      OtherPlayer o;
      o.eid = e.eid;
      auto it = netNames_.find(e.uuid);
      o.name = it != netNames_.end() ? it->second : "?";
      o.uuid = e.uuid;
      if (const auto* parts = e.meta.find(10)) o.parts = static_cast<u8>(parts->i & 0x7F);
      o.pos = o.prevPos = {e.x, e.y, e.z};
      o.yaw = o.prevYaw = e.yaw;
      o.pitch = e.pitch;
      others_[e.eid] = o;
      NetEntity ne;
      ne.kind = 3;
      ne.target = o.pos;
      ne.yaw = e.yaw;
      ne.pitch = e.pitch;
      netEntities_[e.eid] = ne;
      break;
    }
    case T::SpawnMob: {
      const int type = net::mobTypeFromNet(e.a);
      if (type < 0) break;
      Mob m;
      m.type = static_cast<MobType>(type);
      m.pos = m.prevPos = {e.x, e.y, e.z};
      m.yaw = m.prevYaw = e.yaw;
      m.headYaw = m.prevHeadYaw = e.f;
      m.health = m.info().maxHealth;
      if (const auto* h = e.meta.find(6); h && h->type == 3) m.health = h->f;
      if (m.type == MobType::Sheep)
        if (const auto* w = e.meta.find(16)) {
          m.woolColor = static_cast<u8>(w->i & 15);
          m.sheared = (w->i & 0x10) != 0;
        }
      const u32 id = session_->addMob(m);
      netEntities_[e.eid] = {id, 1, m.pos, e.yaw, e.f, e.pitch};
      netMobEid_[id] = e.eid;
      break;
    }
    case T::SpawnObject:
      if (e.a == 2) {  // objeto en el suelo (su contenido llega en los metadatos)
        ItemEntity it;
        it.stack = ItemStack(B::stone);
        it.pos = it.prevPos = {e.x, e.y, e.z};
        it.pickupDelay = 1 << 30;
        const u32 id = session_->addItem(it);
        netEntities_[e.eid] = {id, 2, it.pos};
      }
      break;
    case T::EntityMetadata: {
      auto it = netEntities_.find(e.eid);
      if (it == netEntities_.end()) break;
      if (it->second.kind == 2) {
        if (const auto* s = e.meta.find(10); s && s->type == 5)
          if (ItemEntity* item = session_->itemById(it->second.localId)) item->stack = s->item;
      } else if (it->second.kind == 1) {
        if (Mob* m = session_->mobById(it->second.localId)) {
          if (const auto* h = e.meta.find(6); h && h->type == 3) m->health = h->f;
          if (const auto* w = e.meta.find(16); w && m->type == MobType::Sheep) {
            m->woolColor = static_cast<u8>(w->i & 15);
            m->sheared = (w->i & 0x10) != 0;
          }
        }
      } else if (auto o = others_.find(e.eid); o != others_.end()) {
        if (const auto* f = e.meta.find(0)) o->second.sneaking = (f->i & 0x02) != 0;
        if (const auto* f = e.meta.find(10)) o->second.parts = static_cast<u8>(f->i & 0x7F);
      }
      break;
    }
    case T::EntityMove: case T::EntityTeleport: case T::EntityLook: case T::EntityHeadLook: {
      auto it = netEntities_.find(e.eid);
      if (it == netEntities_.end()) break;
      NetEntity& ne = it->second;
      if (e.type == T::EntityMove) ne.target += glm::dvec3(e.x, e.y, e.z);
      if (e.type == T::EntityTeleport) ne.target = {e.x, e.y, e.z};
      if (e.type == T::EntityHeadLook) ne.head = e.yaw;
      else if (e.type != T::EntityMove || e.flag) {
        ne.yaw = e.yaw;
        ne.pitch = e.pitch;
      }
      break;
    }
    case T::EntityStatus:
      if (auto it = netEntities_.find(e.eid); it != netEntities_.end() && it->second.kind == 1)
        if (Mob* m = session_->mobById(it->second.localId)) {
          if (e.a == 2) m->hurtTime = 10;
          if (e.a == 3) m->deathTime = 1;
        }
      if (e.eid == net_->entityId() && e.a == 2) hurtFlash_ = 1.0f;
      break;
    case T::EntityAnimation:
      if (auto o = others_.find(e.eid); o != others_.end() && e.a == 0) o->second.swing = 1.0f;
      break;
    case T::CollectItem: case T::DestroyEntities: {
      std::vector<i32> ids = e.type == T::CollectItem ? std::vector<i32>{e.eid} : e.ids;
      for (i32 id : ids) {
        auto it = netEntities_.find(id);
        if (it == netEntities_.end()) continue;
        if (it->second.kind == 1) {
          netMobEid_.erase(it->second.localId);
          session_->removeMob(it->second.localId);
        } else if (it->second.kind == 2) {
          session_->removeItem(it->second.localId);
          if (e.type == T::CollectItem) audio_->playFlat(Sfx::Pop, 0.4f, 1.1f);
        } else {
          others_.erase(id);
        }
        netEntities_.erase(it);
      }
      break;
    }
    default: break;
  }
}

void Game::netMenuClick(int slot, int button, bool shift, const std::array<ItemStack, 36>& before) {
  if (!net_ || !session_->menu()) return;
  Menu* m = session_->menu();
  Player& p = session_->player();
  if (m->kind() == MenuKind::Creative) {
    // Creativo: se mandan las casillas del inventario que han cambiado
    for (int i = 0; i < PlayerInventory::kSize; i++)
      if (!(p.inventory.slot(i) == before[i])) net_->sendCreativeSlot(netSlotFromInv(i), p.inventory.slot(i));
    return;
  }
  int netSlot = slot;
  if (m->kind() == MenuKind::Inventory && slot >= 5) netSlot = slot + 4;  // (1.8 tiene 4 casillas de armadura)
  const int window = m->kind() == MenuKind::Inventory ? 0 : netWindow_;
  net_->sendClickWindow(window, netSlot, button, shift ? 1 : 0, slot >= 0 && slot < static_cast<int>(m->slots().size()) ? *m->slots()[slot].stack : ItemStack());
}

}  // namespace mcw
