#include "net/server.h"
#include "net/server_remote.h"

#include <algorithm>
#include <format>
#include <cmath>
#include <nlohmann/json.hpp>

#include "core/fs.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/zip.h"
#include "data/items.h"
#include "game/armor.h"
#include "game/enchanting.h"
#include "game/rules.h"
#include "save/anvil.h"
#include "save/nbt.h"
#include "world/world.h"

namespace mcw::net {

int mobNetType(MobType t) { return mobEntityId(t); }

int mobTypeFromNet(int netType) {
  if (netType == 57) return static_cast<int>(MobType::PigZombie);
  if (const auto t = mobFromEntityId(netType)) return static_cast<int>(*t);
  return -1;
}

Server::Server(GameSession& session, Config config) : session_(session), config_(std::move(config)) {
  if (!config_.hostName.empty()) hostUuid_ = offlineUuid(config_.hostName);
  session_.setTileListener([this](const glm::ivec3& p) { tileChanged(p); });
}

Server::~Server() {
  session_.setTileListener({});
  stop();
}

bool Server::start(int port, std::string* error) {
  listener_ = listenTcp(port, error);
  if (listener_) log::info("servidor escuchando en el puerto {}", listener_->port());
  return listener_ != nullptr;
}

void Server::stop() {
  saveAll();
  for (auto& r : remotes_) kick(*r, config_.hostName.empty() ? "El servidor se ha cerrado" : "El anfitrión ha cerrado la partida");
  session_.setOtherPlayers({});  // (sus Player dejan de existir)
  remotes_.clear();
  listener_.reset();
}

bool Server::loadPlayer(Remote& r) {
  if (config_.playerDataDir.empty()) return false;
  const auto data = fs::readFile(config_.playerDataDir / (r.uuid + ".dat"));
  if (!data) return false;
  const auto root = nbt::read(*data);
  if (!root) return false;
  save::playerFromNbt(*root, r.player);
  return true;
}

void Server::savePlayer(const Remote& r) {
  if (config_.playerDataDir.empty() || !r.joined || r.uuid.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(config_.playerDataDir, ec);
  const std::vector<u8> raw = nbt::write(save::playerToNbt(r.player, session_.spawn(), false));
  const std::vector<u8> gz = gzipCompress(raw.data(), raw.size());
  if (!fs::writeFile(config_.playerDataDir / (r.uuid + ".dat"), gz.data(), gz.size()))
    log::warn("no se pudo guardar al jugador {}", r.name);
}

void Server::saveAll() {
  for (const auto& r : remotes_) savePlayer(*r);
}

void Server::setHostSkin(std::vector<u8> png, bool slim, u8 parts) {
  hostSkin_ = png.empty() ? nullptr : std::make_shared<const std::vector<u8>>(std::move(png));
  hostSkinSlim_ = slim;
  hostParts_ = parts;
}

void Server::sendSkin(Remote& to, const std::string& uuid, const std::vector<u8>& png, bool slim) {
  SkinMessage m;
  m.uuid = uuidFromString(uuid);
  m.slim = slim;
  m.png = png;
  BufferWriter w;
  w.string(kSkinChannel).bytes(encodeSkinMessage(m, true));
  send(to, 0x3F, w);
}

void Server::handleSkin(Remote& r, std::span<const u8> data, double now) {
  // Una skin cada segundo como mucho y solo si es un PNG de 64x64 (o 64x32) razonable
  if (now - r.lastSkin < 1.0) return;
  auto m = decodeSkinMessage(data, false);
  if (!m) {
    log::warn("{} ha mandado una skin que no vale", r.name);
    return;
  }
  r.lastSkin = now;
  r.skin = std::make_shared<const std::vector<u8>>(std::move(m->png));
  r.skinSlim = m->slim;
  r.skinVersion++;
  for (auto& o : remotes_)
    if (o->joined && o.get() != &r) sendSkin(*o, r.uuid, *r.skin, r.skinSlim);
}

bool Server::kickPlayer(const std::string& name, const std::string& reason) {
  for (auto& r : remotes_)
    if (r->joined && !r->closed && r->name == name) {
      kick(*r, reason);
      return true;
    }
  return false;
}

int Server::playerCount() const {
  int n = config_.hostName.empty() ? 0 : 1;
  for (const auto& r : remotes_) n += r->joined;
  return n;
}

std::vector<ChunkPos> Server::wantedChunkCenters() const {
  std::vector<ChunkPos> out;
  for (const auto& r : remotes_)
    if (r->joined)
      out.push_back({static_cast<int>(std::floor(r->player.pos.x)) >> 4, static_cast<int>(std::floor(r->player.pos.z)) >> 4});
  return out;
}

std::vector<Server::PlayerView> Server::players() const {
  std::vector<PlayerView> out;
  for (const auto& r : remotes_)
    if (r->joined)
      out.push_back({r->eid, r->name, r->uuid, r->player.pos, r->prevPos, r->player.yaw, r->player.pitch, r->sneaking,
                     r->swingTicks > 0, r->player.inventory.selected(), r->skin, r->skinSlim, r->skinVersion, r->skinParts,
                     r->player.inventory.armorIds(), r->player.mounted()});
  return out;
}

void Server::send(Remote& r, i32 id, const BufferWriter& w) {
  if (r.closed || !r.t) return;
  r.t->send(r.codec.encode(id, w.data()));
}

void Server::sendAll(i32 id, const BufferWriter& w, const Remote* except) {
  for (auto& r : remotes_)
    if (r->joined && r.get() != except) send(*r, id, w);
}

void Server::kick(Remote& r, const std::string& reason) {
  if (r.closed) return;
  log::info("expulsado {}: {}", r.name.empty() ? std::string("(sin nombre)") : r.name, reason);
  BufferWriter w;
  w.string(textToChat(reason));
  send(r, r.state == State::Play ? 0x40 : 0x00, w);
  r.t->close();
  r.closed = true;
}

void Server::broadcastChat(const std::string& text) {
  BufferWriter w;
  w.string(textToChat(text)).i8(0);
  sendAll(0x02, w);
}

void Server::blockChanged(const glm::ivec3& p, BlockState s) {
  if (p.y < 0 || p.y >= kChunkHeight) return;
  BufferWriter w;
  writePosition(w, p);
  w.varInt(s);
  for (auto& r : remotes_)
    if (r->joined && r->chunks.count({p.x >> 4, p.z >> 4})) send(*r, 0x23, w);
}

void Server::sendTile(Remote& r, const glm::ivec3& p) {
  const TileEntities& te = session_.tiles();
  const TilePos k{p.x, p.y, p.z};
  BufferWriter w;
  writePosition(w, p);
  if (const auto sg = te.signs.find(k); sg != te.signs.end()) {
    for (const std::string& line : sg->second.lines) w.string(signLineToJson(line));
    send(r, 0x33, w);  // Update Sign
  } else if (const auto bn = te.banners.find(k); bn != te.banners.end()) {
    w.u8(6).bytes(nbt::write(save::bannerToNbt(p.x, p.y, p.z, bn->second)));
    send(r, 0x35, w);  // Update Block Entity (6 = estandarte)
  } else if (const auto sk = te.skulls.find(k); sk != te.skulls.end()) {
    w.u8(4).bytes(nbt::write(save::skullToNbt(p.x, p.y, p.z, sk->second)));
    send(r, 0x35, w);  // (4 = cabeza)
  }
}

void Server::tileChanged(const glm::ivec3& p) {
  for (auto& r : remotes_)
    if (r->joined && r->chunks.count({p.x >> 4, p.z >> 4})) sendTile(*r, p);
}

void Server::playerListAdd(Remote& to, i32, const std::string& uuid, const std::string& name, int mode) {
  BufferWriter w;
  w.varInt(0).varInt(1);
  const auto u = uuidFromString(uuid);
  w.bytes(u);
  w.string(name).varInt(0).varInt(mode).varInt(0).boolean(false);
  send(to, 0x38, w);
}

void Server::tick(double now, double worldTime) {
  worldTime_ = worldTime;
  if (listener_)
    for (auto& t : listener_->accept()) {
      auto r = std::make_unique<Remote>();
      r->t = std::move(t);
      r->lastReply = now;
      remotes_.push_back(std::move(r));
    }
  for (auto& rp : remotes_) {
    Remote& r = *rp;
    if (r.closed) continue;
    const auto in = r.t->poll();
    if (!in.empty()) r.codec.feed(in);
    try {
      for (int guard = 0; guard < 512 && !r.closed; guard++) {
        auto p = r.codec.next();
        if (!p) break;
        handle(r, *p, now);
      }
    } catch (const DecodeError& e) {
      kick(r, std::string("Paquete no válido: ") + e.what());
    }
    if (r.t->status() == Transport::Status::Closed) r.closed = true;
    if (!r.joined || r.closed) {
      if (!r.joined && now - r.lastReply > 30) r.closed = true;
      continue;
    }
    // Keep-alive cada 10 s; sin respuesta en 30 s, fuera
    if (now - r.lastKeepAlive > 10) {
      r.lastKeepAlive = now;
      BufferWriter w;
      w.varInt(++r.keepAliveId);
      send(r, 0x00, w);
    }
    if (now - r.lastReply > 30) {
      kick(r, "Tiempo de espera agotado");
      continue;
    }
    if (r.swingTicks > 0) r.swingTicks--;
    tickUse(r);
    // Vida y hambre del invitado (regeneración, hambre, ahogo) y avisarle cuando cambian
    r.player.tickStatus(session_.access().world());
    if (r.player.dead && !r.deathHandled) {  // (también si ha muerto por un golpe en el tick de la partida)
      r.deathHandled = true;
      guestDied(r);
    }
    // Empujón de un golpe (criaturas, explosiones): se lo manda como velocidad y el cliente se mueve
    if (glm::length(r.player.motion) > 1e-3) {
      const glm::dvec3 v = glm::clamp(r.player.motion, glm::dvec3(-3.9), glm::dvec3(3.9)) * 8000.0;
      BufferWriter vw;
      vw.varInt(r.eid).i16(static_cast<i16>(v.x)).i16(static_cast<i16>(v.y)).i16(static_cast<i16>(v.z));
      send(r, 0x12, vw);
      r.player.motion = glm::dvec3(0);
    }
    if (r.player.health != r.sentHealth || r.player.food != r.sentFood) {
      r.sentHealth = r.player.health;
      r.sentFood = r.player.food;
      BufferWriter hw;
      hw.f32(r.player.health).varInt(r.player.food).f32(r.player.saturation);
      send(r, 0x06, hw);
    }
    if (r.player.xpLevel != r.sentXpLevel || r.player.xpTotal != r.sentXpTotal || r.player.xpProgress != r.sentXpProgress) {
      r.sentXpLevel = r.player.xpLevel;
      r.sentXpTotal = r.player.xpTotal;
      r.sentXpProgress = r.player.xpProgress;
      BufferWriter xw;
      xw.f32(r.player.xpProgress).varInt(r.player.xpLevel).varInt(r.player.xpTotal);
      send(r, 0x1F, xw);
    }
    // Efectos de poción: los que empiezan, cambian o acaban (Entity Effect / Remove Entity Effect); el tiempo lo cuenta el cliente
    {
      for (const ActiveEffect& e : r.player.effects.list) {
        auto it = std::find_if(r.sentEffects.begin(), r.sentEffects.end(), [&](const ActiveEffect& s) { return s.id == e.id; });
        if (it == r.sentEffects.end() || it->amp != e.amp || e.ticks > it->ticks) {
          BufferWriter ew;
          ew.varInt(r.eid).i8(static_cast<i8>(e.id)).i8(static_cast<i8>(e.amp)).varInt(e.ticks).u8(0);
          send(r, 0x1D, ew);
          if (it == r.sentEffects.end()) r.sentEffects.push_back(e);
          else *it = e;
        } else {
          it->ticks = e.ticks;
        }
      }
      for (std::size_t i = 0; i < r.sentEffects.size();) {
        if (r.player.effects.has(r.sentEffects[i].id)) {
          i++;
          continue;
        }
        BufferWriter ew;
        ew.varInt(r.eid).i8(static_cast<i8>(r.sentEffects[i].id));
        send(r, 0x1E, ew);
        r.sentEffects.erase(r.sentEffects.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
    if (r.window && r.windowCart && !session_.cartById(r.windowCart)) {  // la vagoneta cuyo cofre tenía abierto ya no está
      std::vector<ItemStack> dropped;
      r.window->close(dropped);
      for (const ItemStack& s : dropped) session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), s, {0, 0.1, 0});
      r.window.reset();
      r.windowCart = 0;
      BufferWriter cw;
      cw.u8(static_cast<u8>(r.windowId));
      send(r, 0x2E, cw);
      sendInventory(r);
    }
    sendChunks(r, 6);
    trackEntities(r);
    trackMounts(r);
    pickUpItems(r);
    syncWindow(r);
    r.prevPos = r.player.pos;
  }
  // Hora cada segundo
  if (now - lastTime_ > 1.0) {
    lastTime_ = now;
    BufferWriter w;
    w.i64(static_cast<i64>(worldTime_)).i64(static_cast<i64>(worldTime_));
    sendAll(0x03, w);
  }
  // Quitar a los que se han ido (avisando a los demás)
  for (auto& rp : remotes_) {
    if (!rp->closed || !rp->joined) continue;
    savePlayer(*rp);
    rp->joined = false;
    log::info("{} ha salido ({})", rp->name, rp->t->error().empty() ? std::string("desconectado") : rp->t->error());
    BufferWriter list;
    list.varInt(4).varInt(1);
    list.bytes(uuidFromString(rp->uuid));
    sendAll(0x38, list);
    BufferWriter destroy;
    destroy.varInt(1).varInt(rp->eid);
    sendAll(0x13, destroy);
    broadcastChat("\xC2\xA7" "e" + rp->name + " ha salido de la partida");
    chatForHost_.push_back(rp->name + " ha salido de la partida");
  }
  std::erase_if(remotes_, [](const auto& r) { return r->closed; });
  // La partida ve a los invitados (las criaturas los persiguen, el mundo se mueve a su alrededor)
  std::vector<Player*> guests;
  for (auto& r : remotes_)
    if (r->joined) guests.push_back(&r->player);
  session_.setOtherPlayers(std::move(guests));
}

void Server::guestDied(Remote& r) {
  // Suelta lo que llevaba (salvo con "conservar inventario"), como en el juego
  if (!session_.rules().keepInventory) {
    Random rng(static_cast<u64>(r.eid) * 7919u + static_cast<u64>(worldTime_));
    for (int i = 0; i < PlayerInventory::kSize; i++) {
      ItemStack& s = r.player.inventory.slot(i);
      if (s.empty()) continue;
      session_.dropItem(r.player.pos + glm::dvec3(0, 1, 0), s, {rng.nextFloat() * 0.4 - 0.2, 0.3, rng.nextFloat() * 0.4 - 0.2});
      s.clear();
    }
    sendInventory(r);
    if (const int lose = r.player.xpDroppedOnDeath(); lose > 0) session_.spawnXp(r.player.pos + glm::dvec3(0, 0.5, 0), lose);
    r.player.resetXp();
  }
  const std::string msg = r.name + " ha muerto";
  broadcastChat(msg);
  chatForHost_.push_back(msg);
}

void Server::handle(Remote& r, const Packet& p, double now) {
  BufferReader in(p.data);
  switch (r.state) {
    case State::Handshake:
      if (p.id == 0x00) handleHandshake(r, in);
      break;
    case State::Status: handleStatus(r, p); break;
    case State::Login:
      if (p.id == 0x00) handleLogin(r, in, now);
      break;
    case State::Play:
      r.lastReply = std::max(r.lastReply, p.id == 0x00 ? now : r.lastReply);
      handlePlay(r, p, now);
      break;
  }
}

void Server::handleHandshake(Remote& r, BufferReader& in) {
  r.protocol = in.varInt();
  in.string(255);
  in.u16();
  const i32 next = in.varInt();
  r.state = next == 1 ? State::Status : State::Login;
}

void Server::handleStatus(Remote& r, const Packet& p) {
  if (p.id == 0x00) {
    nlohmann::json j;
    j["version"] = {{"name", kVersionName}, {"protocol", kProtocolVersion}};
    nlohmann::json sample = nlohmann::json::array();
    if (!config_.hostName.empty()) sample.push_back({{"name", config_.hostName}, {"id", hostUuid_}});
    for (const auto& o : remotes_)
      if (o->joined) sample.push_back({{"name", o->name}, {"id", o->uuid}});
    j["players"] = {{"max", config_.maxPlayers}, {"online", playerCount()}, {"sample", sample}};
    j["description"] = {{"text", config_.motd}};
    BufferWriter w;
    w.string(j.dump());
    send(r, 0x00, w);
  } else if (p.id == 0x01) {
    send(r, 0x01, BufferWriter().bytes(p.data));
    r.closed = true;  // (se cierra después de enviar)
    r.t->poll();
    r.t->close();
  }
}

void Server::handleLogin(Remote& r, BufferReader& in, double now) {
  std::string name = in.string(16);
  if (r.protocol != kProtocolVersion) {
    kick(r, "Versión incompatible: esta partida es de Minecraft 1.8");
    return;
  }
  bool valid = name.size() >= 1 && name.size() <= 16;
  for (char c : name) valid &= std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  if (!valid) {
    kick(r, "Nombre de jugador no válido");
    return;
  }
  if (playerCount() >= config_.maxPlayers) {
    kick(r, "La partida está llena");
    return;
  }
  for (const auto& o : remotes_)
    if (o->joined && o->name == name) {
      kick(r, "Ya hay un jugador con ese nombre");
      return;
    }
  if (!config_.hostName.empty() && name == config_.hostName) {
    kick(r, "Ese nombre es el del anfitrión");
    return;
  }
  if (config_.checkLogin) {
    r.name = name;  // (para el log de la expulsión)
    if (const std::string why = config_.checkLogin(name); !why.empty()) {
      kick(r, why);
      return;
    }
  }
  r.name = name;
  r.uuid = offlineUuid(name);
  // Compresión (como los servidores de 1.8) y Login Success
  BufferWriter comp;
  comp.varInt(256);
  send(r, 0x03, comp);
  r.codec.setCompression(256);
  BufferWriter ok;
  ok.string(r.uuid).string(r.name);
  send(r, 0x02, ok);
  r.state = State::Play;
  join(r, now);
}

void Server::join(Remote& r, double now) {
  r.eid = nextEid_++;
  r.joined = true;
  r.lastReply = r.lastKeepAlive = now;
  r.player.mode = config_.guestMode == 1 ? GameMode::Creative : GameMode::Survival;
  r.player.pos = r.player.prevPos = r.prevPos = session_.spawn();
  // Si ya había jugado en este mundo, vuelve con su inventario y donde lo dejó
  if (loadPlayer(r)) {
    if (r.player.dead) r.player.respawn(session_.spawn());
    r.player.prevPos = r.prevPos = r.player.pos;
  }
  r.invMenu = std::make_unique<Menu>(MenuKind::Inventory, r.player);

  BufferWriter jg;
  const int mode = r.player.creative() ? 1 : 0;
  jg.i32(r.eid).u8(static_cast<u8>(mode)).i8(0).u8(static_cast<u8>(config_.difficulty)).u8(static_cast<u8>(config_.maxPlayers))
      .string(config_.levelType).boolean(false);
  send(r, 0x01, jg);
  BufferWriter brand;
  BufferWriter bd;
  bd.string("mc-web");
  brand.string("MC|Brand").bytes(bd.data());
  send(r, 0x3F, brand);
  BufferWriter diff;
  diff.u8(static_cast<u8>(config_.difficulty));
  send(r, 0x41, diff);
  BufferWriter sp;
  writePosition(sp, glm::ivec3(glm::floor(session_.spawn())));
  send(r, 0x05, sp);
  BufferWriter ab;
  ab.i8(mode == 1 ? 0x0D : 0).f32(0.05f).f32(0.1f);
  send(r, 0x39, ab);
  BufferWriter pos;
  pos.f64(r.player.pos.x).f64(r.player.pos.y).f64(r.player.pos.z).f32(0).f32(0).i8(0);
  send(r, 0x08, pos);
  BufferWriter time;
  time.i64(static_cast<i64>(worldTime_)).i64(static_cast<i64>(worldTime_));
  send(r, 0x03, time);
  sendInventory(r);
  // Lista de jugadores: el anfitrión, los que ya están y el nuevo (a todos)
  if (!config_.hostName.empty()) playerListAdd(r, kHostEid, hostUuid_, config_.hostName, 0);
  for (auto& o : remotes_) {
    if (!o->joined) continue;
    playerListAdd(r, o->eid, o->uuid, o->name, o->player.creative() ? 1 : 0);
    if (o.get() != &r) playerListAdd(*o, r.eid, r.uuid, r.name, mode);
  }
  // Las skins que ya hay: la del anfitrión y las de los demás (la suya la manda él al entrar)
  if (hostSkin_ && !config_.hostName.empty()) sendSkin(r, hostUuid_, *hostSkin_, hostSkinSlim_);
  for (auto& o : remotes_)
    if (o->joined && o.get() != &r && o->skin) sendSkin(r, o->uuid, *o->skin, o->skinSlim);
  broadcastChat("\xC2\xA7" "e" + r.name + " se ha unido a la partida");
  chatForHost_.push_back(r.name + " se ha unido a la partida");
  log::info("{} se ha unido ({})", r.name, r.uuid);
}

void Server::sendChunks(Remote& r, int budget) {
  const World& world = session_.access().world();
  const int pcx = static_cast<int>(std::floor(r.player.pos.x)) >> 4, pcz = static_cast<int>(std::floor(r.player.pos.z)) >> 4;
  const int vd = r.viewDistance > 0 ? std::min(r.viewDistance, config_.viewDistance) : config_.viewDistance;
  // Descargar los que quedan lejos
  for (auto it = r.chunks.begin(); it != r.chunks.end();) {
    if (std::abs(it->first - pcx) > vd + 1 || std::abs(it->second - pcz) > vd + 1) {
      BufferWriter w;
      w.i32(it->first).i32(it->second).boolean(true).u16(0).varInt(0);
      send(r, 0x21, w);
      it = r.chunks.erase(it);
    } else {
      ++it;
    }
  }
  // Mandar los que faltan, de cerca a lejos
  int sent = 0;
  for (int d = 0; d <= vd && sent < budget; d++)
    for (int dz = -d; dz <= d && sent < budget; dz++)
      for (int dx = -d; dx <= d && sent < budget; dx++) {
        if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
        const int cx = pcx + dx, cz = pcz + dz;
        if (r.chunks.count({cx, cz})) continue;
        const Chunk* c = world.chunk(cx, cz);
        if (!c) continue;
        u16 mask = 0;
        const std::vector<u8> data = encodeChunkColumn(*c, true, mask);
        BufferWriter w;
        w.i32(cx).i32(cz).boolean(true).u16(mask).varInt(static_cast<i32>(data.size())).bytes(data);
        send(r, 0x21, w);
        r.chunks.insert({cx, cz});
        sent++;
        // Y lo que llevan encima los carteles, estandartes y cabezas de la columna
        const TileEntities& te = session_.tiles();
        auto inColumn = [&](const TilePos& k) { return (std::get<0>(k) >> 4) == cx && (std::get<2>(k) >> 4) == cz; };
        for (const auto& [k, v] : te.signs) if (inColumn(k)) sendTile(r, {std::get<0>(k), std::get<1>(k), std::get<2>(k)});
        for (const auto& [k, v] : te.banners) if (inColumn(k)) sendTile(r, {std::get<0>(k), std::get<1>(k), std::get<2>(k)});
        for (const auto& [k, v] : te.skulls) if (inColumn(k)) sendTile(r, {std::get<0>(k), std::get<1>(k), std::get<2>(k)});
      }
}

namespace {

std::array<i32, 5> packedPose(const glm::dvec3& p, float yaw, float pitch) {
  return {toFixed(p.x), toFixed(p.y), toFixed(p.z), toAngle(yawToMc(yaw)), toAngle(pitchToMc(pitch))};
}

}  // namespace

void Server::trackEntities(Remote& r) {
  struct Seen {
    i32 eid;
    int kind;  // 0 jugador, 1 criatura, 2 objeto, 3 orbe de experiencia, 4 flecha, 5 vagoneta
    glm::dvec3 pos;
    float yaw, pitch, head;
    std::string uuid;
    const Mob* mob = nullptr;
    const ItemEntity* item = nullptr;
    ItemStack held;
    bool sneaking = false;
    u8 parts = 0x7F;  // capas de la skin visibles (jugadores)
    std::array<ItemStack, 4> armor{};
    int xp = 0;  // orbes de experiencia: puntos
    glm::dvec3 motion{0};  // flechas: hacia dónde van (4)
    const Minecart* cart = nullptr;  // vagonetas (5)
    bool large = false;  // bolas de fuego (6): grande (ghast) o pequeña (blaze)
  };
  std::vector<Seen> visible;
  auto near = [&](const glm::dvec3& p, double range) { return glm::length(glm::dvec2(p.x - r.player.pos.x, p.z - r.player.pos.z)) < range; };
  if (!config_.hostName.empty() && near(session_.player().pos, 96)) {
    const Player& h = session_.player();
    visible.push_back({kHostEid, 0, h.pos, h.yaw, h.pitch, h.yaw, hostUuid_, nullptr, nullptr, h.inventory.selected(), h.sneaking, hostParts_,
                       {h.inventory.armor(0), h.inventory.armor(1), h.inventory.armor(2), h.inventory.armor(3)}});
  }
  for (const auto& o : remotes_)
    if (o->joined && o.get() != &r && near(o->player.pos, 96)) {
      const PlayerInventory& inv = o->player.inventory;
      visible.push_back({o->eid, 0, o->player.pos, o->player.yaw, o->player.pitch, o->player.yaw, o->uuid, nullptr, nullptr,
                         inv.selected(), o->sneaking, o->skinParts, {inv.armor(0), inv.armor(1), inv.armor(2), inv.armor(3)}});
    }
  for (const Mob& m : session_.mobs())
    if (!m.dying() && near(m.pos, 80)) visible.push_back({mobEid(m.id), 1, m.pos, m.yaw, m.pitch, m.headYaw, "", &m});
  for (const ItemEntity& e : session_.items())
    if (!e.stack.empty() && near(e.pos, 48)) visible.push_back({itemEid(e.id), 2, e.pos, 0, 0, 0, "", nullptr, &e});
  for (const XpOrb& o : session_.orbs())
    if (o.value > 0 && near(o.pos, 48)) {
      Seen s{orbEid(o.id), 3, o.pos, 0, 0, 0, ""};
      s.xp = o.value;
      visible.push_back(s);
    }
  for (const Arrow& a : session_.arrows())
    if (near(a.pos, 64)) {
      Seen s{arrowEid(a.id), 4, a.pos, a.yaw, a.pitch, 0, ""};
      s.motion = a.motion;
      visible.push_back(s);
    }

  for (const Fireball& f : session_.fireballs())
    if (near(f.pos, 80)) {
      Seen s{static_cast<i32>(0xD00000 + f.id), 6, f.pos, 0, 0, 0, ""};
      s.motion = f.motion;
      s.large = f.large;
      visible.push_back(s);
    }
  for (const Minecart& c : session_.carts())
    if (!c.dead && near(c.pos, 80)) {
      Seen s{cartEid(c.id), 5, c.pos, cartYawToNet(c.yaw), 0, 0, ""};
      s.cart = &c;
      visible.push_back(s);
    }

  // Quitar las que ya no se ven
  std::set<i32> now;
  for (const Seen& s : visible) now.insert(s.eid);
  std::vector<i32> gone;
  for (i32 id : r.tracked)
    if (!now.count(id)) gone.push_back(id);
  if (!gone.empty()) {
    BufferWriter w;
    w.varInt(static_cast<i32>(gone.size()));
    for (i32 id : gone) {
      w.varInt(id);
      r.tracked.erase(id);
      r.lastSent.erase(id);
      r.mobFlags.erase(id);
      r.equipSent.erase(id);
      r.cartSent.erase(id);
      r.attached.erase(id);
    }
    send(r, 0x13, w);
  }
  for (const Seen& s : visible) {
    const auto pose = packedPose(s.pos, s.yaw, s.pitch);
    if (!r.tracked.count(s.eid)) {
      r.tracked.insert(s.eid);
      r.lastSent[s.eid] = pose;
      if (s.kind == 0) {
        BufferWriter w;
        w.varInt(s.eid).bytes(uuidFromString(s.uuid)).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(static_cast<u8>(pose[3]))
            .u8(static_cast<u8>(pose[4])).i16(static_cast<i16>(s.held.empty() ? 0 : s.held.id));
        Metadata m;
        m.byte(0, s.sneaking ? 0x02 : 0);
        m.floatV(6, 20.0f);
        m.byte(10, static_cast<i8>(s.parts));
        writeMetadata(w, m);
        send(r, 0x0C, w);
      } else if (s.kind == 1) {
        BufferWriter w;
        w.varInt(s.eid).u8(static_cast<u8>(mobNetType(s.mob->type))).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(static_cast<u8>(pose[3]))
            .u8(static_cast<u8>(pose[4])).u8(toAngle(yawToMc(s.head))).i16(0).i16(0).i16(0);
        Metadata m;
        m.byte(0, 0);
        m.shortV(1, 300);
        m.floatV(6, s.mob->health);
        if (s.mob->type == MobType::Sheep) m.byte(16, static_cast<i8>((s.mob->woolColor & 15) | (s.mob->sheared ? 0x10 : 0)));
        if (s.mob->type == MobType::Pig) {  // metadato 16: con silla
          m.byte(16, s.mob->saddled ? 1 : 0);
          r.mobFlags[s.eid] = static_cast<u8>(s.mob->saddled ? 4 : 0);
        }
        if (s.mob->isSlimeLike()) m.byte(16, static_cast<i8>(s.mob->size));
        if (s.mob->type == MobType::WitherSkeleton) m.byte(13, 1);
        if (isBreedable(s.mob->type)) {
          m.byte(12, static_cast<i8>(s.mob->baby() ? -1 : 0));  // la edad de 1.8: -1 = cría
          r.mobFlags[s.eid] = static_cast<u8>((s.mob->baby() ? 1 : 0) | (s.mob->inLove > 0 ? 2 : 0) | (r.mobFlags[s.eid] & 4));
        }
        writeMetadata(w, m);
        send(r, 0x0F, w);
      } else if (s.kind == 4) {  // flecha (Spawn Object de tipo 60) y hacia dónde va
        BufferWriter w;
        w.varInt(s.eid).i8(60).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(static_cast<u8>(pose[4])).u8(static_cast<u8>(pose[3])).i32(0);
        send(r, 0x0E, w);
        const glm::dvec3 v = glm::clamp(s.motion, glm::dvec3(-3.9), glm::dvec3(3.9)) * 8000.0;
        BufferWriter vw;
        vw.varInt(s.eid).i16(static_cast<i16>(v.x)).i16(static_cast<i16>(v.y)).i16(static_cast<i16>(v.z));
        send(r, 0x12, vw);
      } else if (s.kind == 6) {  // bola de fuego (Spawn Object de tipo 63 grande o 64 pequeña) con su velocidad
        BufferWriter w;
        w.varInt(s.eid).i8(s.large ? 63 : 64).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(0).u8(0).i32(1);
        const glm::dvec3 v = glm::clamp(s.motion, glm::dvec3(-3.9), glm::dvec3(3.9)) * 8000.0;
        w.i16(static_cast<i16>(v.x)).i16(static_cast<i16>(v.y)).i16(static_cast<i16>(v.z));
        send(r, 0x0E, w);
      } else if (s.kind == 5) {  // vagoneta (Spawn Object de tipo 10; las de cofre, horno y dinamita llevan velocidad)
        const int data = cartNetData(s.cart->type);
        BufferWriter w;
        w.varInt(s.eid).i8(10).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(static_cast<u8>(pose[4])).u8(static_cast<u8>(pose[3])).i32(data);
        if (data > 0) w.i16(0).i16(0).i16(0);
        send(r, 0x0E, w);
        const u32 flags = (s.cart->fuel > 0 ? 1u : 0u) | (s.cart->fuse >= 0 ? 2u : 0u);
        r.cartSent[s.eid] = {s.cart->hits, flags};
        if (flags & 1) {
          BufferWriter mw;
          mw.varInt(s.eid);
          Metadata m;
          m.byte(16, 1);
          writeMetadata(mw, m);
          send(r, 0x1C, mw);
        }
      } else if (s.kind == 3) {  // orbe de experiencia (Spawn Experience Orb)
        BufferWriter w;
        w.varInt(s.eid).i32(pose[0]).i32(pose[1]).i32(pose[2]).i16(static_cast<i16>(std::min(s.xp, 32767)));
        send(r, 0x11, w);
      } else {
        BufferWriter w;
        w.varInt(s.eid).i8(2).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(0).u8(0).i32(1).i16(0).i16(0).i16(0);
        send(r, 0x0E, w);
        BufferWriter mw;
        mw.varInt(s.eid);
        Metadata m;
        Metadata::Entry e;
        e.index = 10;
        e.type = 5;
        e.item = s.item->stack;
        m.entries.push_back(e);
        writeMetadata(mw, m);
        send(r, 0x1C, mw);
      }
      continue;
    }
    // Jugadores: lo que llevan en la mano y puesto (paquete Entity Equipment) al aparecer y al cambiar
    if (s.kind == 0) {
      const std::array<ItemStack, 5> now = {s.held, s.armor[0], s.armor[1], s.armor[2], s.armor[3]};
      auto& before = r.equipSent[s.eid];  // (al aparecer, vacío: se manda todo lo que lleve)
      for (int slot = 0; slot < 5; slot++) {
        const std::size_t k = static_cast<std::size_t>(slot);
        if (now[k] == before[k]) continue;
        BufferWriter ew;
        ew.varInt(s.eid).i16(static_cast<i16>(slot));
        writeSlot(ew, now[k]);
        send(r, 0x04, ew);
        before[k] = now[k];
      }
    }
    // Vagonetas: el bamboleo de un golpe (metadatos 17 a 19), el horno encendido (16) y la dinamita encendida (estado 10)
    if (s.kind == 5) {
      auto& sent = r.cartSent[s.eid];
      const Minecart& c = *s.cart;
      if (c.hits != sent[0]) {
        sent[0] = c.hits;
        BufferWriter mw;
        mw.varInt(s.eid);
        Metadata m;
        m.intV(17, 10);
        m.intV(18, c.shakeDir);
        m.floatV(19, c.damage);
        writeMetadata(mw, m);
        send(r, 0x1C, mw);
      }
      const u32 flags = (c.fuel > 0 ? 1u : 0u) | (c.fuse >= 0 ? 2u : 0u);
      if ((flags ^ sent[1]) & 1) {
        BufferWriter mw;
        mw.varInt(s.eid);
        Metadata m;
        m.byte(16, static_cast<i8>(flags & 1));
        writeMetadata(mw, m);
        send(r, 0x1C, mw);
      }
      if ((flags & 2) && !(sent[1] & 2)) {
        BufferWriter sw;
        sw.i32(s.eid).i8(10);
        send(r, 0x1A, sw);
      }
      sent[1] = flags;
    }
    // Cerdos: les ponen la silla (metadato 16)
    if (s.mob && s.mob->type == MobType::Pig) {
      u8& before = r.mobFlags[s.eid];
      const u8 saddled = s.mob->saddled ? 4 : 0;
      if ((before & 4) != saddled) {
        BufferWriter mw;
        mw.varInt(s.eid);
        Metadata m;
        m.byte(16, saddled ? 1 : 0);
        writeMetadata(mw, m);
        send(r, 0x1C, mw);
        before = static_cast<u8>((before & ~4) | saddled);
      }
    }
    // Animales: crecen (cambia la edad) y entran en modo amor o crían (corazones: estado 18 de la entidad)
    if (s.mob && isBreedable(s.mob->type)) {
      const u8 now = static_cast<u8>((s.mob->baby() ? 1 : 0) | (s.mob->inLove > 0 ? 2 : 0));
      u8& before = r.mobFlags[s.eid];
      const u8 keep = before & 4;
      before &= 3;
      if ((now ^ before) & 1) {
        BufferWriter mw;
        mw.varInt(s.eid);
        Metadata m;
        m.byte(12, static_cast<i8>(s.mob->baby() ? -1 : 0));
        writeMetadata(mw, m);
        send(r, 0x1C, mw);
      }
      // (al entrar en modo amor, y al tener la cría: deja de estarlo y empieza la espera)
      if (((now & 2) && !(before & 2)) || (!(now & 2) && (before & 2) && s.mob->growth > 0)) {
        BufferWriter sw;
        sw.i32(s.eid).i8(18);
        send(r, 0x1A, sw);
      }
      before = static_cast<u8>(now | keep);
    }
    // Se ha movido o girado: teletransporte (sencillo y siempre exacto) y la cabeza
    auto& last = r.lastSent[s.eid];
    if (pose != last) {
      BufferWriter w;
      w.varInt(s.eid).i32(pose[0]).i32(pose[1]).i32(pose[2]).u8(static_cast<u8>(pose[3])).u8(static_cast<u8>(pose[4])).boolean(true);
      send(r, 0x18, w);
      BufferWriter hl;
      hl.varInt(s.eid).u8(toAngle(yawToMc(s.head)));
      send(r, 0x19, hl);
      last = pose;
    }
  }
}

void Server::trackMounts(Remote& r) {
  auto sendFor = [&](i32 riderEid, const Player& p) {
    if (riderEid != r.eid && !r.tracked.count(riderEid)) return;
    i32 vehicle = -1;
    if (p.mount == Player::Mount::Cart && session_.cartById(p.mountId)) vehicle = cartEid(p.mountId);
    if (p.mount == Player::Mount::Mob && session_.mobById(p.mountId)) vehicle = mobEid(p.mountId);
    if (vehicle != -1 && !r.tracked.count(vehicle)) return;  // todavía no ha visto la vagoneta
    auto it = r.attached.find(riderEid);
    if ((it == r.attached.end() ? -1 : it->second) == vehicle) return;
    r.attached[riderEid] = vehicle;
    BufferWriter w;
    w.i32(riderEid).i32(vehicle).boolean(false);
    send(r, 0x1B, w);
  };
  if (!config_.hostName.empty() && session_.localPlayerActive()) sendFor(kHostEid, session_.player());
  for (const auto& o : remotes_)
    if (o->joined) sendFor(o->eid, o->player);
}

void Server::pickUpItems(Remote& r) {
  if (r.player.dead) return;
  for (const ItemEntity& e : session_.items()) {
    if (e.stack.empty() || e.age < e.pickupDelay) continue;
    const glm::dvec3 d = e.pos - r.player.pos;
    if (std::abs(d.x) > 1.3 || std::abs(d.z) > 1.3 || d.y < -0.5 || d.y > 2.3) continue;
    if (r.player.inventory.roomFor(e.stack) < e.stack.count) continue;
    const i32 eid = itemEid(e.id);
    const u32 id = e.id;
    auto taken = session_.takeItem(id);
    if (!taken) continue;
    r.player.inventory.add(*taken);
    BufferWriter w;
    w.varInt(eid).varInt(r.eid);
    for (auto& o : remotes_)
      if (o->joined && o->tracked.count(eid)) send(*o, 0x0D, w);
    sendInventory(r);
    break;  // la lista de objetos ha cambiado
  }
}

namespace {

/// Casilla del protocolo -> casilla de nuestro menú. Las ventanas se numeran igual que en 1.8 (en el
/// inventario: 0 resultado, 1-4 rejilla, 5-8 armadura, 9-44 inventario y barra).
int menuSlotFromNet(MenuKind, int s) { return s; }

int netSlotCount(const Menu& menu) {
  switch (menu.kind()) {
    case MenuKind::Inventory: return 45;
    case MenuKind::Crafting: return 46;
    case MenuKind::Furnace: return 39;
    case MenuKind::Enchant: return 38;
    case MenuKind::Anvil: return 39;
    case MenuKind::Chest: case MenuKind::Hopper: case MenuKind::Dispenser: case MenuKind::Dropper: case MenuKind::Brewing: return menu.containerSize() + 36;
    default: return 0;
  }
}

}  // namespace

void Server::sendInventory(Remote& r) {
  BufferWriter w;
  w.u8(0).i16(45);
  for (int s = 0; s < 45; s++) {
    const int m = menuSlotFromNet(MenuKind::Inventory, s);
    writeSlot(w, m >= 0 && m < static_cast<int>(r.invMenu->slots().size()) ? *r.invMenu->slots()[m].stack : ItemStack());
  }
  send(r, 0x30, w);
  BufferWriter cur;
  cur.i8(-1).i16(-1);
  writeSlot(cur, r.player.cursor);
  send(r, 0x2F, cur);
}

namespace {

/// Las casillas de una ventana, numeradas como en el protocolo.
std::vector<ItemStack> windowSlots(const Menu& menu) {
  std::vector<ItemStack> out;
  const int n = netSlotCount(menu);
  for (int s = 0; s < n; s++) {
    const int m = menuSlotFromNet(menu.kind(), s);
    out.push_back(m >= 0 && m < static_cast<int>(menu.slots().size()) ? *menu.slots()[static_cast<std::size_t>(m)].stack : ItemStack());
  }
  return out;
}

}  // namespace

void Server::sendWindow(Remote& r) {
  if (!r.window) return;
  r.windowSent = windowSlots(*r.window);
  BufferWriter w;
  w.u8(static_cast<u8>(r.windowId)).i16(static_cast<i16>(r.windowSent.size()));
  for (const ItemStack& s : r.windowSent) writeSlot(w, s);
  send(r, 0x30, w);
  BufferWriter cur;
  cur.i8(-1).i16(-1);
  writeSlot(cur, r.player.cursor);
  send(r, 0x2F, cur);
  r.sentFurnace.fill(-1);  // (las propiedades del horno se mandan de nuevo)
  r.sentAnvilCost = -1;    // (y el coste del yunque)
}

void Server::syncWindow(Remote& r) {
  if (!r.window) return;
  const std::vector<ItemStack> now = windowSlots(*r.window);
  if (now.size() != r.windowSent.size()) return;  // (la ventana se acaba de abrir: ya se manda entera)
  for (std::size_t s = 0; s < now.size(); s++) {
    if (now[s] == r.windowSent[s]) continue;
    r.windowSent[s] = now[s];
    BufferWriter w;
    w.i8(static_cast<i8>(r.windowId)).i16(static_cast<i16>(s));
    writeSlot(w, now[s]);
    send(r, 0x2F, w);
  }
  if (const FurnaceState* f = r.window->furnace(); f && r.window->kind() == MenuKind::Furnace) {
    const std::array<int, 4> props = {f->burnTime, f->burnTotal, f->cookTime, FurnaceState::kCookTicks};
    for (std::size_t p = 0; p < props.size(); p++) {
      if (props[p] == r.sentFurnace[p]) continue;
      r.sentFurnace[p] = props[p];
      BufferWriter w;
      w.u8(static_cast<u8>(r.windowId)).i16(static_cast<i16>(p)).i16(static_cast<i16>(props[p]));
      send(r, 0x31, w);
    }
  }
  if (r.window->kind() == MenuKind::Brewing && r.window->brewTime() != r.sentBrew) {  // propiedad 0 del atril: el tiempo que falta
    r.sentBrew = r.window->brewTime();
    BufferWriter w;
    w.u8(static_cast<u8>(r.windowId)).i16(0).i16(static_cast<i16>(r.sentBrew));
    send(r, 0x31, w);
  }
  if (r.window->kind() == MenuKind::Anvil && r.window->anvilCost() != r.sentAnvilCost) {  // propiedad 0 del yunque: lo que cuesta
    r.sentAnvilCost = r.window->anvilCost();
    BufferWriter w;
    w.u8(static_cast<u8>(r.windowId)).i16(0).i16(static_cast<i16>(r.sentAnvilCost));
    send(r, 0x31, w);
  }
  // Se ha sacado algo del yunque: puede desgastarse y, al final, romperse (y entonces se le cierra la ventana)
  if (r.window->kind() == MenuKind::Anvil) {
    bool broke = false;
    for (int n = r.window->takeAnvilUses(); n > 0 && !broke; n--) broke = session_.wearAnvil(r.windowPos, r.player.creative());
    if (broke) {
      std::vector<ItemStack> dropped;
      r.window->close(dropped);
      for (const ItemStack& s : dropped) session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), s, {0, 0.1, 0});
      r.window.reset();
      BufferWriter w;
      w.u8(static_cast<u8>(r.windowId));
      send(r, 0x2E, w);
      sendInventory(r);
    }
  }
}

void Server::sendEnchantProps(Remote& r, bool all) {
  if (!r.window || r.window->kind() != MenuKind::Enchant) return;
  const auto& offers = r.window->offers();
  std::array<int, 10> now{};
  for (int i = 0; i < 3; i++) {
    now[static_cast<std::size_t>(i)] = offers[static_cast<std::size_t>(i)].cost;
    now[static_cast<std::size_t>(4 + i)] = offers[static_cast<std::size_t>(i)].clueEnchant;
    now[static_cast<std::size_t>(7 + i)] = offers[static_cast<std::size_t>(i)].clueLevel;
  }
  now[3] = r.player.xpSeed & -16;
  for (int prop = 0; prop < 10; prop++) {
    if (!all && now[static_cast<std::size_t>(prop)] == r.sentEnchant[static_cast<std::size_t>(prop)]) continue;
    r.sentEnchant[static_cast<std::size_t>(prop)] = now[static_cast<std::size_t>(prop)];
    BufferWriter w;
    w.u8(static_cast<u8>(r.windowId)).i16(static_cast<i16>(prop)).i16(static_cast<i16>(now[static_cast<std::size_t>(prop)]));
    send(r, 0x31, w);
  }
}

void Server::digBlock(Remote& r, int status, const glm::ivec3& pos, int face) {
  (void)face;
  if (status == 5) {  // soltar el botón de usar (acaba de tensar el arco, o deja de comer)
    releaseUse(r);
    return;
  }
  World& world = session_.access().world();
  if (glm::length(glm::dvec3(pos) + 0.5 - r.player.pos) > 8) return;
  const bool creative = r.player.creative();
  const BlockState before = world.block(pos.x, pos.y, pos.z);
  if ((creative && status == 0) || (!creative && status == 2)) {
    if (before == 0 || blockInfo(stateId(before)).hardness < 0) {
      blockChanged(pos, before);
      return;
    }
    session_.breakBlockAt(pos, !creative);
  } else if (status == 3 || status == 4) {
    // Soltar la pila (3) o uno (4) de la mano
    ItemStack& held = r.player.inventory.selected();
    if (held.empty()) return;
    ItemStack out = held;
    out.count = static_cast<i16>(status == 3 ? held.count : 1);
    held.count = static_cast<i16>(held.count - out.count);
    if (held.count <= 0) held.clear();
    const glm::dvec3 dir(-std::sin(r.player.yaw) * 0.3, 0.1, -std::cos(r.player.yaw) * 0.3);
    session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), out, dir);
    sendInventory(r);
  }
}

void Server::useOnBlock(Remote& r, const glm::ivec3& pos, int face, const glm::vec3& cursor) {
  World& world = session_.access().world();
  if (face < 0) {
    useInAir(r);
    return;
  }
  if (face > 5) return;
  if (glm::length(glm::dvec3(pos) + 0.5 - r.player.pos) > 8) return;
  RayHit hit;
  hit.block = pos;
  hit.face = face;
  hit.point = glm::dvec3(pos) + glm::dvec3(cursor);
  hit.distance = glm::length(hit.point - r.player.eyePos());
  // Con las mismas reglas que el anfitrión (azada, polvo de hueso, puertas, colocar...)
  GameSession::UseResult res;
  session_.actAs(r.player, r.act, [&] { res = session_.useHeldOnBlock(hit); });
  auto openMenu = [&](std::unique_ptr<Menu> m, const char* type, const std::string& title, int slots) {
    r.window = std::move(m);
    r.windowCart = 0;
    r.windowId = r.windowId % 100 + 1;
    BufferWriter w;
    w.u8(static_cast<u8>(r.windowId)).string(type).string(textToChat(title)).u8(static_cast<u8>(slots));
    send(r, 0x2D, w);
    sendWindow(r);
  };
  auto openWindow = [&](MenuKind kind, const char* type, const char* title, int slots, FurnaceState* f, ItemStack* chest) {
    openMenu(std::make_unique<Menu>(kind, r.player, f, chest), type, title, slots);
  };
  using Kind = GameSession::UseResult::Kind;
  switch (res.kind) {
    case Kind::Crafting: openWindow(MenuKind::Crafting, "minecraft:crafting_table", "Crafting", 0, nullptr, nullptr); return;
    case Kind::Chest:
      // (el cofre de ender es del jugador: su sitio es el de este invitado, no el de la partida)
      if (res.ender) {
        openWindow(MenuKind::Chest, "minecraft:chest", "Cofre de ender", 27, nullptr, r.player.enderItems.data());
      } else if (!res.container.empty()) {
        const char* type = res.menuKind == MenuKind::Brewing ? "minecraft:brewing_stand" : res.menuKind == MenuKind::Hopper ? "minecraft:hopper" : res.menuKind == MenuKind::Dispenser ? "minecraft:dispenser"
                         : res.menuKind == MenuKind::Dropper ? "minecraft:dropper" : res.container.size() > 27 ? "minecraft:container" : "minecraft:chest";
        auto m = std::make_unique<Menu>(res.menuKind, r.player, res.container);
        m->setTitle(res.title);
        if (res.brewTime) m->setBrewTime(res.brewTime);
        r.sentBrew = -1;
        openMenu(std::move(m), type, res.title, static_cast<int>(res.container.size()));
      }
      return;
    case Kind::Furnace: openWindow(MenuKind::Furnace, "minecraft:furnace", "Horno", 3, res.furnace, nullptr); return;
    case Kind::Enchant:
      r.window = std::make_unique<Menu>(MenuKind::Enchant, r.player, nullptr, nullptr, res.bookshelves);
      r.windowId = r.windowId % 100 + 1;
      {
        BufferWriter w;
        w.u8(static_cast<u8>(r.windowId)).string("minecraft:enchanting_table").string(textToChat("Encantar")).u8(0);
        send(r, 0x2D, w);
      }
      sendWindow(r);
      sendEnchantProps(r, true);
      return;
    case Kind::Anvil:
      r.window = std::make_unique<Menu>(MenuKind::Anvil, r.player);
      r.windowPos = res.pos;
      r.sentAnvilCost = -1;
      r.windowId = r.windowId % 100 + 1;
      {
        BufferWriter w;
        w.u8(static_cast<u8>(r.windowId)).string("minecraft:anvil").string(textToChat("Reparar y nombrar")).u8(0);
        send(r, 0x2D, w);
      }
      sendWindow(r);
      return;
    case Kind::Sign: {
      // Un cartel recién puesto: el cliente abre el editor y manda el texto al cerrarlo
      r.signPending = res.pos;
      BufferWriter w;
      writePosition(w, res.pos);
      send(r, 0x36, w);
      break;
    }
    case Kind::Used: break;
    case Kind::Nothing: {
      // No ha pasado nada: el cliente ya lo había dibujado, hay que corregirlo
      const glm::ivec3 n = pos + glm::ivec3(kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]);
      blockChanged(n, world.block(n.x, n.y, n.z));
      blockChanged(pos, world.block(pos.x, pos.y, pos.z));
      break;
    }
  }
  sendInventory(r);  // (se gasta lo que se coloca, la azada se desgasta...)
}

void Server::useInAir(Remote& r) {
  ItemStack& held = r.player.inventory.selected();
  if (held.empty()) return;
  if (held.id == ItemId::bucket || held.id == ItemId::water_bucket || held.id == ItemId::lava_bucket) {  // cubos
    bool ok = false;
    session_.actAs(r.player, r.act, [&] { ok = session_.useBucket(); });
    if (ok) sendInventory(r);
    return;
  }
  if (isArmor(held.id)) {  // una pieza de armadura se pone
    bool ok = false;
    session_.actAs(r.player, r.act, [&] { ok = session_.wearHeldArmor(); });
    if (ok) sendInventory(r);
    return;
  }
  if (!r.player.creative() && foodValue(held) && r.player.food < 20) {  // comer: 32 ticks (se cancela soltando el botón)
    r.use = Remote::Use::Eat;
    r.useTicks = 0;
    r.useSlot = r.player.inventory.selectedIndex();
    return;
  }
  if (held.id == ItemId::bow) {
    int arrows = 0;
    session_.actAs(r.player, r.act, [&] { arrows = session_.arrowCount(); });
    if (r.player.creative() || arrows > 0) {
      r.use = Remote::Use::Bow;
      r.useTicks = 0;
      r.useSlot = r.player.inventory.selectedIndex();
    }
  }
}

void Server::releaseUse(Remote& r) {
  const Remote::Use what = r.use;
  r.use = Remote::Use::None;
  if (what == Remote::Use::Bow && r.player.inventory.selectedIndex() == r.useSlot) {
    session_.actAs(r.player, r.act, [&] { session_.releaseBow(r.useTicks); });
    sendInventory(r);
  }
}

void Server::tickUse(Remote& r) {
  if (r.use == Remote::Use::None) return;
  if (r.player.dead || r.player.inventory.selectedIndex() != r.useSlot) {  // cambiar de casilla o morir lo cancela
    r.use = Remote::Use::None;
    return;
  }
  r.useTicks = std::min(r.useTicks + 1, 72000);
  if (r.use == Remote::Use::Eat && r.useTicks >= 32) {
    r.use = Remote::Use::None;
    bool ate = false;
    session_.actAs(r.player, r.act, [&] { ate = session_.finishEating(); });
    if (ate) {
      BufferWriter w;
      w.i32(r.eid).i8(9);  // "ha terminado de usar el objeto": el cliente deja de comer
      send(r, 0x1A, w);
      sendInventory(r);
    }
  }
}

void Server::useEntity(Remote& r, i32 target, bool attack) {
  const glm::dvec3 eye = r.player.eyePos();
  if (target >= 0x10000 && target < 0x400000) {  // una criatura
    Mob* m = session_.mobById(static_cast<u32>(target - 0x10000));
    if (!m || glm::length(m->pos + glm::dvec3(0, m->info().height * 0.5, 0) - eye) > 6.0) return;
    session_.actAs(r.player, r.act, [&] {
      if (attack) session_.punchMob(*m);
      else session_.useHeldOnMob(*m);
    });
    sendInventory(r);
    return;
  }
  if (target >= 0x1000000) {  // una vagoneta: golpearla, montarla, abrir su cofre, echarle carbón
    Minecart* c = session_.cartById(static_cast<u32>(target - 0x1000000));
    if (!c || glm::length(c->pos + glm::dvec3(0, 0.35, 0) - eye) > 6.0) return;
    GameSession::UseResult res;
    session_.actAs(r.player, r.act, [&] {
      if (attack) session_.punchCart(*c);
      else res = session_.useHeldOnCart(*c);
    });
    if (res.kind == GameSession::UseResult::Kind::Chest && res.chest) {
      r.window = std::make_unique<Menu>(MenuKind::Chest, r.player, nullptr, res.chest);
      r.windowCart = res.cart;
      r.windowId = r.windowId % 100 + 1;
      BufferWriter w;
      w.u8(static_cast<u8>(r.windowId)).string("minecraft:chest").string(textToChat("Vagoneta con cofre")).u8(27);
      send(r, 0x2D, w);
      sendWindow(r);
      return;
    }
    sendInventory(r);
    return;
  }
  if (!attack) return;
  // Otro jugador (el anfitrión o un invitado)
  Player* victim = nullptr;
  if (target == kHostEid && session_.localPlayerActive()) victim = &session_.player();
  for (auto& o : remotes_)
    if (o->joined && o->eid == target && o.get() != &r) victim = &o->player;
  if (!victim || glm::length(victim->pos + glm::dvec3(0, 0.9, 0) - eye) > 6.0) return;
  if (session_.attackPlayer(r.player, *victim)) {
    BufferWriter w;
    w.i32(target).i8(2);  // "ha recibido un golpe": se ve el destello y suena
    sendAll(0x1A, w);
    sendInventory(r);
  }
}

void Server::clickWindow(Remote& r, int window, int slot, int button, int mode, int action, const ItemStack& clicked) {
  Menu* menu = window == 0 ? r.invMenu.get() : (window == r.windowId ? r.window.get() : nullptr);
  if (!menu) return;
  // Lo que el cliente dice que había en la casilla antes del clic tiene que coincidir con lo que había: si no, está
  // desincronizado y se le contesta que no (y se le manda todo de nuevo). Con la tecla numérica (modo 2), tirar (modo 4)
  // y al soltar fuera, el cliente manda una casilla vacía; con mayúsculas, la casilla o nada (si no cabía).
  ItemStack expected;
  if (slot >= 0 && mode != 2 && mode != 4) {
    const int m = menuSlotFromNet(menu->kind(), slot);
    if (m >= 0 && m < static_cast<int>(menu->slots().size())) expected = *menu->slots()[m].stack;
  }
  // En el resultado del yunque basta con que sea el mismo objeto: quien escribe el nombre letra a letra hace clic en cuanto
  // acaba, con lo último que vio (que puede ser el nombre sin la última letra); la partida ya ha cogido el de verdad
  const bool anvilResult = menu->kind() == MenuKind::Anvil && slot == 2 && !expected.empty() && clicked.id == expected.id;
  const bool accepted = clicked == expected || (mode == 1 && clicked.empty()) || anvilResult;
  if (!accepted)
    log::warn("{}: clic en la casilla {} de la ventana {} rechazado: el servidor tiene {}x{}:{} y el cliente dice {}x{}:{}{}", r.name, slot, window, expected.id,
              expected.count, expected.meta, clicked.id, clicked.count, clicked.meta, expected.sameExtra(clicked) ? "" : " (etiquetas distintas)");
  if (!accepted && !expected.sameExtra(clicked)) {
    auto show = [](const ItemStack& s) {
      const ItemExtra e = s.copyExtra();
      return std::format("nombre \"{}\", penitencia {}, {} encantamientos, {} guardados, {} líneas, color {}", e.name, e.repairCost, e.ench.size(), e.stored.size(),
                         e.lore.size(), e.color);
    };
    log::warn("   servidor: {}; cliente: {}", show(expected), show(clicked));
  }
  if (slot == -999) {
    std::vector<ItemStack> dropped;
    menu->clickOutside(button, dropped);
    for (const ItemStack& s : dropped) session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), s, {0, 0.1, 0});
  } else {
    const int m = menuSlotFromNet(menu->kind(), slot);
    if (m >= 0) {
      if (mode == 0 || mode == 1) {
        menu->click(m, button, mode == 1);
      } else if (mode == 2 && button >= 0 && button < 9) {  // tecla 1-9: intercambiar con la barra rápida
        ItemStack& a = *menu->slots()[m].stack;
        std::swap(a, r.player.inventory.slot(button));
      }
    }
  }
  // Confirmar la transacción (Confirm Transaction): los clientes esperan esta respuesta a cada clic
  BufferWriter ct;
  ct.i8(static_cast<i8>(window)).i16(static_cast<i16>(action)).boolean(accepted);
  send(r, 0x32, ct);
  if (menu == r.invMenu.get()) sendInventory(r);
  else {
    sendWindow(r);
    sendEnchantProps(r, false);
    syncWindow(r);  // (propiedades del yunque, y el desgaste si se ha sacado algo)
  }
}

void Server::handlePlay(Remote& r, const Packet& p, double now) {
  BufferReader in(p.data);
  auto moved = [&] {
    // Límite sencillo: nada de teletransportes de más de 10 bloques por paquete
    if (glm::length(r.player.pos - r.prevPos) > 10) {
      r.player.pos = r.prevPos;
      BufferWriter w;
      w.f64(r.player.pos.x).f64(r.player.pos.y).f64(r.player.pos.z).f32(yawToMc(r.player.yaw)).f32(pitchToMc(r.player.pitch)).i8(0);
      send(r, 0x08, w);
    }
  };
  switch (p.id) {
    case 0x00: r.lastReply = now; break;
    case 0x01: {
      const std::string msg = in.string(100);
      if (!msg.empty() && msg[0] == '/') {
        // Las órdenes las dan los operadores; en una partida LAN, solo el anfitrión
        const bool op = config_.isOp && config_.isOp(r.name);
        std::vector<std::string> reply;
        if (op) {
          log::info("{} usa la orden {}", r.name, msg);
          reply = runCommand(msg, r.name);
        } else {
          reply.push_back(config_.isOp ? "No tienes permiso para usar esta orden" : "Los comandos solo los puede usar el anfitrión");
        }
        for (const std::string& line : reply) {
          BufferWriter w;
          w.string(textToChat("\xC2\xA7" "7" + line)).i8(0);
          send(r, 0x02, w);
        }
        break;
      }
      const std::string line = "<" + r.name + "> " + msg;
      BufferWriter w;
      w.string(textToChat(line)).i8(0);
      sendAll(0x02, w);
      chatForHost_.push_back(line);
      break;
    }
    case 0x02: {
      const i32 target = in.varInt();
      const i32 type = in.varInt();
      if (type == 2) {  // "interactuar en un punto": lo mismo que interactuar
        in.f32();
        in.f32();
        in.f32();
      }
      useEntity(r, target, type == 1);
      break;
    }
    case 0x03: r.player.onGround = in.boolean(); break;
    case 0x04: {
      const glm::dvec3 at{in.f64(), in.f64(), in.f64()};
      const bool ground = in.boolean();
      if (r.player.mounted()) break;  // montado, lo lleva la montura
      r.player.pos = at;
      r.player.onGround = ground;
      moved();
      break;
    }
    case 0x05:
      r.player.yaw = yawFromMc(in.f32());
      r.player.pitch = pitchFromMc(in.f32());
      r.player.onGround = in.boolean();
      break;
    case 0x06: {
      const glm::dvec3 at{in.f64(), in.f64(), in.f64()};
      r.player.yaw = yawFromMc(in.f32());
      r.player.pitch = pitchFromMc(in.f32());
      const bool ground = in.boolean();
      if (r.player.mounted()) break;
      r.player.pos = at;
      r.player.onGround = ground;
      moved();
      break;
    }
    case 0x0C: {  // montado: hacia dónde empuja y si se quiere bajar (bit 1)
      in.f32();
      const float forward = in.f32();
      const u8 flags = in.u8();
      r.player.moveForward = std::clamp(forward, -1.0f, 1.0f);
      if ((flags & 2) && r.player.mounted()) session_.dismount(r.player);
      break;
    }
    case 0x07: {
      const int status = in.i8();
      const glm::ivec3 pos = readPosition(in);
      digBlock(r, status, pos, in.i8());
      break;
    }
    case 0x08: {
      const glm::ivec3 pos = readPosition(in);
      const int face = in.u8();
      readSlot(in);
      const glm::vec3 cursor(in.i8() / 16.0f, in.i8() / 16.0f, in.i8() / 16.0f);
      useOnBlock(r, pos, face == 255 ? -1 : face, cursor);
      break;
    }
    case 0x12: {  // texto del cartel
      const glm::ivec3 pos = readPosition(in);
      SignText t;
      for (std::string& line : t.lines) line = signLineFromJson(in.string());
      if (r.signPending && *r.signPending == pos && glm::length(glm::dvec3(pos) + 0.5 - r.player.pos) <= 10) session_.setSignText(pos, t);
      r.signPending.reset();
      break;
    }
    case 0x09: r.player.inventory.select(std::clamp<int>(in.i16(), 0, 8)); break;
    case 0x0A: {
      r.swingTicks = 6;
      BufferWriter w;
      w.varInt(r.eid).u8(0);
      sendAll(0x0B, w, &r);
      break;
    }
    case 0x0B: {
      in.varInt();
      const i32 action = in.varInt();
      if (action == 0 || action == 1) r.sneaking = action == 0;
      if (action == 3 || action == 4) r.sprinting = action == 3;
      BufferWriter w;
      w.varInt(r.eid);
      Metadata m;
      m.byte(0, static_cast<i8>((r.sneaking ? 0x02 : 0) | (r.sprinting ? 0x08 : 0)));
      writeMetadata(w, m);
      sendAll(0x1C, w, &r);
      break;
    }
    case 0x11: {  // Enchant Item: ventana y opción (0 a 2)
      const int window = in.u8(), button = in.u8();
      if (r.window && window == r.windowId && r.window->kind() == MenuKind::Enchant && r.window->enchant(button)) {
        sendWindow(r);
        sendEnchantProps(r, false);
      }
      break;
    }
    case 0x0D: {
      const int window = in.u8();
      if (window != 0 && r.window) {
        std::vector<ItemStack> dropped;
        r.window->close(dropped);
        for (const ItemStack& s : dropped) session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), s, {0, 0.1, 0});
        r.window.reset();
      } else if (window == 0 && r.invMenu) {
        std::vector<ItemStack> dropped;
        r.invMenu->close(dropped);
        for (const ItemStack& s : dropped) session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), s, {0, 0.1, 0});
        r.invMenu = std::make_unique<Menu>(MenuKind::Inventory, r.player);
      }
      sendInventory(r);
      break;
    }
    case 0x0E: {
      const int window = in.u8();
      const int slot = in.i16();
      const int button = in.i8();
      const int action = in.i16();
      const int mode = in.i8();
      const ItemStack clicked = readSlot(in);
      clickWindow(r, window, slot, button, mode, action, clicked);
      break;
    }
    case 0x10: {  // modo creativo: poner un objeto en una casilla (o, con -1, tirarlo)
      const int slot = in.i16();
      ItemStack item = readSlot(in);
      if (!r.player.creative()) break;
      // Solo valen objetos que existen y con una cantidad posible (como en el servidor de 1.8)
      const bool valid = item.empty() || (item.id > 0 && itemInfo(item.id).exists && item.count >= 1 && item.count <= 64 && item.meta >= 0);
      if (!valid) break;
      const int m = menuSlotFromNet(MenuKind::Inventory, slot);
      if (m >= 5 && m < static_cast<int>(r.invMenu->slots().size())) {
        *r.invMenu->slots()[m].stack = item;
        // El servidor confirma con la casilla que ha quedado (así el cliente sabe que la ha aceptado)
        BufferWriter w;
        w.i8(0).i16(static_cast<i16>(slot));
        writeSlot(w, item);
        send(r, 0x2F, w);
      } else if (slot < 0 && !item.empty() && now - r.lastCreativeDrop >= 0.05) {
        r.lastCreativeDrop = now;  // (como mucho 20 al segundo)
        const float yaw = r.player.yaw, pitch = r.player.pitch;
        const glm::dvec3 dir(-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
        session_.dropItem(r.player.pos + glm::dvec3(0, 1.3, 0), item, dir * 0.3);
      }
      break;
    }
    case 0x16:  // estado del cliente: 0 = reaparecer tras morir
      if (in.varInt() == 0 && r.player.dead) {
        r.player.respawn(session_.spawn());
        r.sentHealth = -1;
        r.deathHandled = false;
        // El cliente vacía el mundo al reaparecer: hay que volver a mandárselo todo
        r.chunks.clear();
        r.tracked.clear();
        r.lastSent.clear();
        BufferWriter rw;
        rw.i32(0).u8(static_cast<u8>(config_.difficulty)).u8(r.player.creative() ? 1 : 0).string(config_.levelType);
        send(r, 0x07, rw);
        BufferWriter pw;
        pw.f64(r.player.pos.x).f64(r.player.pos.y).f64(r.player.pos.z).f32(0).f32(0).i8(0);
        send(r, 0x08, pw);
        sendInventory(r);
      }
      break;
    case 0x15: {  // ajustes del cliente: idioma, distancia de visión y capas de la skin
      in.string(16);
      r.viewDistance = std::clamp<int>(in.i8(), 2, 32);
      in.i8();       // chat
      in.boolean();  // colores del chat
      const u8 parts = in.u8() & 0x7F;
      if (parts != r.skinParts) {
        r.skinParts = parts;
        if (r.joined) {  // los que lo ven se enteran (metadato 10 del jugador)
          BufferWriter mw;
          mw.varInt(r.eid);
          Metadata m;
          m.byte(10, static_cast<i8>(parts));
          writeMetadata(mw, m);
          for (auto& o : remotes_)
            if (o->joined && o.get() != &r && o->tracked.count(r.eid)) send(*o, 0x1C, mw);
        }
      }
      break;
    }
    case 0x17: {  // mensaje de plugin
      const std::string channel = in.string(64);
      const auto body = in.bytes(in.remaining());
      if (channel == kSkinChannel) handleSkin(r, body, now);
      else if (channel == "MC|ItemName" && r.window && r.window->kind() == MenuKind::Anvil) {
        // El nombre que escribe en el yunque (una cadena con su longitud delante)
        BufferReader br(body);
        std::string name = br.string(64);
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 32; }), name.end());
        r.window->setAnvilName(std::move(name));
        syncWindow(r);  // (ya: el cliente hace clic en el resultado en cuanto lo ve y espera que sea el de este nombre)
      }
      break;
    }
    default: break;  // mensajes de plugins, estado del cliente, etc.
  }
}

}  // namespace mcw::net
