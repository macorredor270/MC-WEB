#include "net/server.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "core/hash.h"
#include "core/log.h"
#include "data/items.h"
#include "game/rules.h"
#include "world/world.h"

namespace mcw::net {

int mobNetType(MobType t) {
  switch (t) {
    case MobType::Pig: return 90;
    case MobType::Cow: return 92;
    case MobType::Sheep: return 91;
    case MobType::Chicken: return 93;
    case MobType::Zombie: return 54;
    case MobType::Skeleton: return 51;
    case MobType::Creeper: return 50;
    case MobType::Spider: return 52;
    default: return 90;
  }
}

int mobTypeFromNet(int netType) {
  switch (netType) {
    case 90: return static_cast<int>(MobType::Pig);
    case 92: return static_cast<int>(MobType::Cow);
    case 91: return static_cast<int>(MobType::Sheep);
    case 93: return static_cast<int>(MobType::Chicken);
    case 54: case 57: return static_cast<int>(MobType::Zombie);
    case 51: return static_cast<int>(MobType::Skeleton);
    case 50: return static_cast<int>(MobType::Creeper);
    case 52: case 59: return static_cast<int>(MobType::Spider);
    default: return -1;
  }
}

struct Server::Remote {
  std::unique_ptr<Transport> t;
  PacketCodec codec;
  State state = State::Handshake;
  int protocol = 0;
  std::string name, uuid;
  i32 eid = 0;
  Player player;
  bool joined = false, closed = false;
  std::set<std::pair<int, int>> chunks;
  std::set<i32> tracked;
  std::map<i32, std::array<i32, 5>> lastSent;  // x, y, z, yaw, pitch (en unidades del protocolo)
  double lastKeepAlive = 0, lastReply = 0;
  i32 keepAliveId = 0;
  std::unique_ptr<Menu> invMenu;  // ventana 0 (inventario)
  std::unique_ptr<Menu> window;   // mesa, cofre u horno abiertos
  int windowId = 0;
  bool sneaking = false, sprinting = false;
  int swingTicks = 0;
  int viewDistance = -1;  // la que pide el cliente (Client Settings); -1 = la del servidor
  glm::dvec3 prevPos{0};
};

Server::Server(GameSession& session, Config config) : session_(session), config_(std::move(config)) {
  if (!config_.hostName.empty()) hostUuid_ = offlineUuid(config_.hostName);
}

Server::~Server() { stop(); }

bool Server::start(int port, std::string* error) {
  listener_ = listenTcp(port, error);
  if (listener_) log::info("servidor escuchando en el puerto {}", listener_->port());
  return listener_ != nullptr;
}

void Server::stop() {
  for (auto& r : remotes_) kick(*r, "El anfitrión ha cerrado la partida");
  remotes_.clear();
  listener_.reset();
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
                     r->swingTicks > 0, r->player.inventory.selected()});
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
    sendChunks(r, 6);
    trackEntities(r);
    pickUpItems(r);
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
  if (name == config_.hostName) {
    kick(r, "Ese nombre es el del anfitrión");
    return;
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
  r.invMenu = std::make_unique<Menu>(MenuKind::Inventory, r.player);

  BufferWriter jg;
  jg.i32(r.eid).u8(static_cast<u8>(config_.guestMode)).i8(0).u8(static_cast<u8>(config_.difficulty)).u8(static_cast<u8>(config_.maxPlayers))
      .string("default").boolean(false);
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
  ab.i8(config_.guestMode == 1 ? 0x0D : 0).f32(0.05f).f32(0.1f);
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
    playerListAdd(r, o->eid, o->uuid, o->name, config_.guestMode);
    if (o.get() != &r) playerListAdd(*o, r.eid, r.uuid, r.name, config_.guestMode);
  }
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
    int kind;  // 0 jugador, 1 criatura, 2 objeto
    glm::dvec3 pos;
    float yaw, pitch, head;
    std::string uuid;
    const Mob* mob = nullptr;
    const ItemEntity* item = nullptr;
    ItemStack held;
    bool sneaking = false;
  };
  std::vector<Seen> visible;
  auto near = [&](const glm::dvec3& p, double range) { return glm::length(glm::dvec2(p.x - r.player.pos.x, p.z - r.player.pos.z)) < range; };
  if (!config_.hostName.empty() && near(session_.player().pos, 96)) {
    const Player& h = session_.player();
    visible.push_back({kHostEid, 0, h.pos, h.yaw, h.pitch, h.yaw, hostUuid_, nullptr, nullptr, h.inventory.selected(), h.sneaking});
  }
  for (const auto& o : remotes_)
    if (o->joined && o.get() != &r && near(o->player.pos, 96))
      visible.push_back({o->eid, 0, o->player.pos, o->player.yaw, o->player.pitch, o->player.yaw, o->uuid, nullptr, nullptr,
                         o->player.inventory.selected(), o->sneaking});
  for (const Mob& m : session_.mobs())
    if (!m.dying() && near(m.pos, 80)) visible.push_back({mobEid(m.id), 1, m.pos, m.yaw, m.pitch, m.headYaw, "", &m});
  for (const ItemEntity& e : session_.items())
    if (!e.stack.empty() && near(e.pos, 48)) visible.push_back({itemEid(e.id), 2, e.pos, 0, 0, 0, "", nullptr, &e});

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
        m.byte(10, 0x7F);
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
        writeMetadata(w, m);
        send(r, 0x0F, w);
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

/// Casilla del protocolo -> casilla de nuestro menú (-1 si no existe, p. ej. la armadura).
int menuSlotFromNet(MenuKind kind, int s) {
  if (kind == MenuKind::Inventory) {
    if (s >= 0 && s <= 4) return s;
    if (s >= 9 && s <= 44) return s - 4;
    return -1;
  }
  return s;
}

int netSlotCount(MenuKind kind) {
  switch (kind) {
    case MenuKind::Inventory: return 45;
    case MenuKind::Crafting: return 46;
    case MenuKind::Furnace: return 39;
    case MenuKind::Chest: return 63;
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

void Server::sendWindow(Remote& r) {
  if (!r.window) return;
  const int n = netSlotCount(r.window->kind());
  BufferWriter w;
  w.u8(static_cast<u8>(r.windowId)).i16(static_cast<i16>(n));
  for (int s = 0; s < n; s++) {
    const int m = menuSlotFromNet(r.window->kind(), s);
    writeSlot(w, m >= 0 && m < static_cast<int>(r.window->slots().size()) ? *r.window->slots()[m].stack : ItemStack());
  }
  send(r, 0x30, w);
  BufferWriter cur;
  cur.i8(-1).i16(-1);
  writeSlot(cur, r.player.cursor);
  send(r, 0x2F, cur);
}

void Server::digBlock(Remote& r, int status, const glm::ivec3& pos, int face) {
  (void)face;
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
  if (face < 0 || face > 5) return;  // usar en el aire: de momento nada
  if (glm::length(glm::dvec3(pos) + 0.5 - r.player.pos) > 8) return;
  const int id = stateId(world.block(pos.x, pos.y, pos.z));
  auto openWindow = [&](MenuKind kind, const char* type, const char* title, int slots, FurnaceState* f, ItemStack* chest) {
    r.window = std::make_unique<Menu>(kind, r.player, f, chest);
    r.windowId = r.windowId % 100 + 1;
    BufferWriter w;
    w.u8(static_cast<u8>(r.windowId)).string(type).string(textToChat(title)).u8(static_cast<u8>(slots));
    send(r, 0x2D, w);
    sendWindow(r);
  };
  if (!r.sneaking) {
    if (id == B::crafting_table) { openWindow(MenuKind::Crafting, "minecraft:crafting_table", "Crafting", 0, nullptr, nullptr); return; }
    if (id == 54 || id == 146) { openWindow(MenuKind::Chest, "minecraft:chest", "Cofre", 27, nullptr, session_.chestItems(pos)); return; }
    if (id == 130) { openWindow(MenuKind::Chest, "minecraft:chest", "Cofre de ender", 27, nullptr, r.player.enderItems.data()); return; }
    if (id == B::furnace || id == B::lit_furnace) { openWindow(MenuKind::Furnace, "minecraft:furnace", "Horno", 3, session_.furnaceAt(pos), nullptr); return; }
    if (session_.interact(pos)) return;
  }
  ItemStack& held = r.player.inventory.selected();
  RayHit hit;
  hit.block = pos;
  hit.face = face;
  hit.point = glm::dvec3(pos) + glm::dvec3(cursor);
  const auto place = placementFor(world, held, hit, r.player.yaw, r.player.pitch);
  const glm::ivec3 n = pos + glm::ivec3(kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]);
  if (!place) {
    // El cliente lo ha dibujado ya: corregirlo
    blockChanged(n, world.block(n.x, n.y, n.z));
    blockChanged(pos, world.block(pos.x, pos.y, pos.z));
    return;
  }
  session_.placeBlock(place->pos, place->state);
  if (place->hasSecond) session_.placeBlock(place->secondPos, place->secondState);
  if (!r.player.creative() && --held.count <= 0) held.clear();
  BufferWriter w;
  w.i8(0).i16(static_cast<i16>(36 + r.player.inventory.selectedIndex()));
  writeSlot(w, held);
  send(r, 0x2F, w);
}

void Server::clickWindow(Remote& r, int window, int slot, int button, int mode) {
  Menu* menu = window == 0 ? r.invMenu.get() : (window == r.windowId ? r.window.get() : nullptr);
  if (!menu) return;
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
  if (menu == r.invMenu.get()) sendInventory(r);
  else sendWindow(r);
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
        BufferWriter w;
        w.string(textToChat("\xC2\xA7" "7Los comandos solo los puede usar el anfitrión")).i8(0);
        send(r, 0x02, w);
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
      if (type == 1 && target >= 0x10000 && target < 0x400000) {
        const ItemStack& held = r.player.inventory.selected();
        float dmg = 1.0f;
        switch (held.id) {
          case ItemId::wooden_sword: case ItemId::golden_sword: dmg = 5; break;
          case ItemId::stone_sword: dmg = 6; break;
          case ItemId::iron_sword: dmg = 7; break;
          case ItemId::diamond_sword: dmg = 8; break;
          default: break;
        }
        session_.hurtMobById(static_cast<u32>(target - 0x10000), dmg, r.player.pos);
      }
      break;
    }
    case 0x03: r.player.onGround = in.boolean(); break;
    case 0x04:
      r.player.pos = {in.f64(), in.f64(), in.f64()};
      r.player.onGround = in.boolean();
      moved();
      break;
    case 0x05:
      r.player.yaw = yawFromMc(in.f32());
      r.player.pitch = pitchFromMc(in.f32());
      r.player.onGround = in.boolean();
      break;
    case 0x06:
      r.player.pos = {in.f64(), in.f64(), in.f64()};
      r.player.yaw = yawFromMc(in.f32());
      r.player.pitch = pitchFromMc(in.f32());
      r.player.onGround = in.boolean();
      moved();
      break;
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
      in.i16();
      const int mode = in.i8();
      readSlot(in);
      clickWindow(r, window, slot, button, mode);
      break;
    }
    case 0x10: {  // modo creativo: poner un objeto en una casilla
      const int slot = in.i16();
      const ItemStack item = readSlot(in);
      if (!r.player.creative()) break;
      const int m = menuSlotFromNet(MenuKind::Inventory, slot);
      if (m >= 5 && m < static_cast<int>(r.invMenu->slots().size())) *r.invMenu->slots()[m].stack = item;
      break;
    }
    case 0x15: {  // ajustes del cliente: idioma y distancia de visión
      in.string(16);
      r.viewDistance = std::clamp<int>(in.i8(), 2, 32);
      break;
    }
    default: break;  // mensajes de plugins, estado del cliente, etc.
  }
}

}  // namespace mcw::net
