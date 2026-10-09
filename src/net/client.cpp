#include "net/client.h"

#include <cmath>
#include <nlohmann/json.hpp>

#include "core/hash.h"
#include "core/log.h"

namespace mcw::net {

Client::Client(std::unique_ptr<Transport> t, std::string host, int port, std::string name)
    : transport_(std::move(t)), host_(std::move(host)), name_(std::move(name)), port_(port) {
  if (!transport_) {
    fail("No se puede conectar desde aquí");
    return;
  }
  // Handshake (estado 2 = login) y Login Start
  BufferWriter hs;
  hs.varInt(kProtocolVersion).string(host_).u16(static_cast<u16>(port_)).varInt(2);
  send(0x00, hs);
  BufferWriter ls;
  ls.string(name_);
  send(0x00, ls);
}

void Client::send(i32 id, const BufferWriter& w) {
  if (!transport_ || closed_) return;
  transport_->send(codec_.encode(id, w.data()));
}

void Client::fail(std::string why) {
  if (closed_) return;
  log::info("desconectado del servidor: {}", why);
  closed_ = true;
  error_ = std::move(why);
  ClientEvent e{ClientEvent::Type::Disconnected};
  e.text = error_;
  events_.push_back(std::move(e));
  if (transport_) transport_->close();
}

void Client::sendSettings() {
  BufferWriter cs;
  cs.string("es_ES").i8(static_cast<i8>(viewDistance_)).i8(0).boolean(true).u8(skinParts_);
  send(0x15, cs);
}

void Client::setSkinParts(u8 parts) {
  skinParts_ = parts & 0x7F;
  if (state_ == State::Play) sendSettings();
}

void Client::setSkin(std::vector<u8> png, bool slim) {
  skinPng_ = std::move(png);
  skinSlim_ = slim;
  if (mcwebServer_) sendSkin();
}

void Client::sendSkin() {
  if (skinPng_.empty() || skinPng_.size() > kMaxSkinBytes) return;
  SkinMessage m;
  m.slim = skinSlim_;
  m.png = skinPng_;
  BufferWriter w;
  w.string(kSkinChannel).bytes(encodeSkinMessage(m, false));
  send(0x17, w);
}

void Client::setViewDistance(int chunks) {
  viewDistance_ = std::clamp(chunks, 2, 32);
  if (state_ == State::Play) sendSettings();
}

void Client::disconnect() {
  if (transport_ && !closed_) transport_->close();
  closed_ = true;
}

void Client::poll() {
  if (closed_ || !transport_) return;
  const std::vector<u8> in = transport_->poll();
  if (!in.empty()) codec_.feed(in);
  try {
    for (int guard = 0; guard < 4096; guard++) {
      auto p = codec_.next();
      if (!p) break;
      handle(*p);
      if (closed_) return;
    }
  } catch (const DecodeError& e) {
    fail(std::string("Datos no válidos del servidor: ") + e.what());
    return;
  }
  if (transport_->status() == Transport::Status::Closed) fail(transport_->error().empty() ? "Desconectado" : transport_->error());
}

void Client::handle(const Packet& p) {
  if (state_ == State::Login) handleLogin(p);
  else handlePlay(p);
}

void Client::handleLogin(const Packet& p) {
  BufferReader r(p.data);
  switch (p.id) {
    case 0x00: fail("Desconectado: " + chatToText(r.string())); break;
    case 0x01:
      fail("Este servidor pide cuenta de Minecraft (online-mode). Solo se puede entrar a servidores en modo offline "
           "o a partidas de MC-WEB.");
      break;
    case 0x02:
      uuid_ = r.string();
      name_ = r.string();
      state_ = State::Play;
      {
        // Ajustes del cliente y marca
        sendSettings();
        BufferWriter brand;
        BufferWriter data;
        data.string("mc-web");
        brand.string("MC|Brand").bytes(data.data());
        send(0x17, brand);
      }
      break;
    case 0x03: codec_.setCompression(r.varInt()); break;
    default: break;
  }
}

void Client::handlePlay(const Packet& p) {
  BufferReader r(p.data);
  auto ev = [&](ClientEvent::Type t) -> ClientEvent& {
    events_.push_back(ClientEvent{t});
    return events_.back();
  };
  switch (p.id) {
    case 0x00: {  // keep alive
      BufferWriter w;
      w.varInt(r.varInt());
      send(0x00, w);
      break;
    }
    case 0x01: {  // join game
      auto& e = ev(ClientEvent::Type::Joined);
      entityId_ = e.eid = r.i32();
      const u8 mode = r.u8();
      e.a = mode & 7;
      e.flag = (mode & 8) != 0;  // extremo
      dimension_ = e.b = r.i8();
      e.c = r.u8();  // dificultad
      r.u8();
      e.text = r.string();  // tipo de mundo
      break;
    }
    case 0x02: {
      auto& e = ev(ClientEvent::Type::Chat);
      e.text = chatToText(r.string());
      e.a = r.i8();
      break;
    }
    case 0x03: {
      auto& e = ev(ClientEvent::Type::Time);
      e.x = static_cast<double>(r.i64());
      const i64 day = r.i64();
      e.y = static_cast<double>(day < 0 ? -day : day);
      e.flag = day < 0;  // hora parada
      break;
    }
    case 0x04: {
      auto& e = ev(ClientEvent::Type::EntityEquipment);
      e.eid = r.varInt();
      e.a = r.i16();
      e.item = readSlot(r);
      break;
    }
    case 0x05: ev(ClientEvent::Type::SpawnPosition).pos = readPosition(r); break;
    case 0x06: {
      auto& e = ev(ClientEvent::Type::Health);
      e.f = r.f32();
      e.a = r.varInt();
      e.x = r.f32();
      break;
    }
    case 0x07: {
      auto& e = ev(ClientEvent::Type::Respawn);
      dimension_ = e.b = r.i32();
      e.c = r.u8();
      e.a = r.u8() & 7;
      e.text = r.string();
      break;
    }
    case 0x08: {  // posición del jugador (bits de "relativo" en a)
      auto& e = ev(ClientEvent::Type::PlayerPosition);
      e.x = r.f64();
      e.y = r.f64();
      e.z = r.f64();
      const float yaw = r.f32(), pitch = r.f32();
      e.a = r.u8();
      e.yaw = (e.a & 0x08) ? -glm::radians(yaw) : yawFromMc(yaw);
      e.pitch = (e.a & 0x10) ? -glm::radians(pitch) : pitchFromMc(pitch);
      break;
    }
    case 0x09: ev(ClientEvent::Type::HeldItem).a = r.i8(); break;
    case 0x0B: {
      auto& e = ev(ClientEvent::Type::EntityAnimation);
      e.eid = r.varInt();
      e.a = r.u8();
      break;
    }
    case 0x0C: {
      auto& e = ev(ClientEvent::Type::SpawnPlayer);
      e.eid = r.varInt();
      const i64 hi = r.i64(), lo = r.i64();
      char buf[40];
      std::snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
      e.uuid = std::string(buf).insert(8, "-").insert(13, "-").insert(18, "-").insert(23, "-");
      e.x = fromFixed(r.i32());
      e.y = fromFixed(r.i32());
      e.z = fromFixed(r.i32());
      e.yaw = yawFromMc(fromAngle(r.u8()));
      e.pitch = pitchFromMc(fromAngle(r.u8()));
      e.a = r.i16();  // objeto en la mano
      e.meta = readMetadata(r);
      break;
    }
    case 0x0D: {
      auto& e = ev(ClientEvent::Type::CollectItem);
      e.eid = r.varInt();
      e.a = r.varInt();
      break;
    }
    case 0x0E: {
      auto& e = ev(ClientEvent::Type::SpawnObject);
      e.eid = r.varInt();
      e.a = r.i8();
      e.x = fromFixed(r.i32());
      e.y = fromFixed(r.i32());
      e.z = fromFixed(r.i32());
      e.pitch = pitchFromMc(fromAngle(r.u8()));
      e.yaw = yawFromMc(fromAngle(r.u8()));
      e.b = r.i32();
      if (e.b != 0) {
        e.pos = {r.i16(), r.i16(), r.i16()};  // velocidad (1/8000 bloque/tick)
      }
      break;
    }
    case 0x0F: {
      auto& e = ev(ClientEvent::Type::SpawnMob);
      e.eid = r.varInt();
      e.a = r.u8();
      e.x = fromFixed(r.i32());
      e.y = fromFixed(r.i32());
      e.z = fromFixed(r.i32());
      e.yaw = yawFromMc(fromAngle(r.u8()));
      e.pitch = pitchFromMc(fromAngle(r.u8()));
      e.f = yawFromMc(fromAngle(r.u8()));  // cabeza
      e.pos = {r.i16(), r.i16(), r.i16()};
      e.meta = readMetadata(r);
      break;
    }
    case 0x11: {  // orbe de experiencia: id, posición y puntos
      auto& e = ev(ClientEvent::Type::SpawnXpOrb);
      e.eid = r.varInt();
      e.x = fromFixed(r.i32());
      e.y = fromFixed(r.i32());
      e.z = fromFixed(r.i32());
      e.a = r.i16();
      break;
    }
    case 0x12: {
      auto& e = ev(ClientEvent::Type::EntityVelocity);
      e.eid = r.varInt();
      e.x = r.i16() / 8000.0;
      e.y = r.i16() / 8000.0;
      e.z = r.i16() / 8000.0;
      break;
    }
    case 0x13: {
      auto& e = ev(ClientEvent::Type::DestroyEntities);
      const i32 n = r.varInt();
      for (i32 i = 0; i < n && i < 4096; i++) e.ids.push_back(r.varInt());
      break;
    }
    case 0x15: case 0x17: {
      auto& e = ev(ClientEvent::Type::EntityMove);
      e.eid = r.varInt();
      e.x = r.i8() / 32.0;
      e.y = r.i8() / 32.0;
      e.z = r.i8() / 32.0;
      e.flag = p.id == 0x17;
      if (e.flag) {
        e.yaw = yawFromMc(fromAngle(r.u8()));
        e.pitch = pitchFromMc(fromAngle(r.u8()));
      }
      e.onGround = r.boolean();
      break;
    }
    case 0x16: {
      auto& e = ev(ClientEvent::Type::EntityLook);
      e.eid = r.varInt();
      e.yaw = yawFromMc(fromAngle(r.u8()));
      e.pitch = pitchFromMc(fromAngle(r.u8()));
      e.onGround = r.boolean();
      break;
    }
    case 0x18: {
      auto& e = ev(ClientEvent::Type::EntityTeleport);
      e.eid = r.varInt();
      e.x = fromFixed(r.i32());
      e.y = fromFixed(r.i32());
      e.z = fromFixed(r.i32());
      e.yaw = yawFromMc(fromAngle(r.u8()));
      e.pitch = pitchFromMc(fromAngle(r.u8()));
      e.onGround = r.boolean();
      break;
    }
    case 0x19: {
      auto& e = ev(ClientEvent::Type::EntityHeadLook);
      e.eid = r.varInt();
      e.yaw = yawFromMc(fromAngle(r.u8()));
      break;
    }
    case 0x1A: {
      auto& e = ev(ClientEvent::Type::EntityStatus);
      e.eid = r.i32();
      e.a = r.i8();
      break;
    }
    case 0x1C: {
      auto& e = ev(ClientEvent::Type::EntityMetadata);
      e.eid = r.varInt();
      e.meta = readMetadata(r);
      break;
    }
    case 0x1F: {
      auto& e = ev(ClientEvent::Type::Experience);
      e.f = r.f32();
      e.a = r.varInt();
      e.b = r.varInt();
      break;
    }
    case 0x21: {  // una columna
      const i32 cx = r.i32(), cz = r.i32();
      const bool groundUp = r.boolean();
      const u16 mask = r.u16();
      const i32 size = r.varInt();
      const auto data = r.bytes(static_cast<std::size_t>(std::max(0, size)));
      if (groundUp && mask == 0) {
        auto& e = ev(ClientEvent::Type::UnloadChunk);
        e.pos = {cx, 0, cz};
        break;
      }
      auto chunk = std::make_shared<Chunk>(cx, cz);
      decodeChunkColumn(*chunk, data, mask, dimension_ == 0, groundUp);
      auto& e = ev(ClientEvent::Type::Chunk);
      e.chunk = std::move(chunk);
      e.flag = groundUp;
      e.a = mask;
      break;
    }
    case 0x22: {
      auto& e = ev(ClientEvent::Type::BlockChange);
      const i32 cx = r.i32(), cz = r.i32();
      const i32 n = r.varInt();
      for (i32 i = 0; i < n && i < 65536; i++) {
        const u8 xz = r.u8(), y = r.u8();
        const i32 id = r.varInt();
        e.blocks.push_back({{cx * 16 + (xz >> 4), y, cz * 16 + (xz & 15)}, static_cast<BlockState>(id)});
      }
      break;
    }
    case 0x23: {
      auto& e = ev(ClientEvent::Type::BlockChange);
      const glm::ivec3 pos = readPosition(r);
      e.blocks.push_back({pos, static_cast<BlockState>(r.varInt())});
      break;
    }
    case 0x26: {  // varias columnas
      const bool sky = r.boolean();
      const i32 n = r.varInt();
      std::vector<std::tuple<i32, i32, u16>> metas;
      for (i32 i = 0; i < n && i < 1024; i++) {
        const i32 cx = r.i32(), cz = r.i32();
        metas.emplace_back(cx, cz, r.u16());
      }
      for (const auto& [cx, cz, mask] : metas) {
        auto chunk = std::make_shared<Chunk>(cx, cz);
        const std::size_t pos = r.position();
        const std::size_t used = decodeChunkColumn(*chunk, std::span<const u8>(p.data).subspan(pos), mask, sky, true);
        r.bytes(used);
        auto& e = ev(ClientEvent::Type::Chunk);
        e.chunk = std::move(chunk);
        e.flag = true;
        e.a = mask;
      }
      break;
    }
    case 0x27: {
      auto& e = ev(ClientEvent::Type::Explosion);
      e.x = r.f32();
      e.y = r.f32();
      e.z = r.f32();
      e.f = r.f32();
      const i32 n = r.i32();
      for (i32 i = 0; i < n && i < 65536; i++) {
        const glm::ivec3 o(r.i8(), r.i8(), r.i8());
        e.blocks.push_back({glm::ivec3(glm::floor(glm::dvec3(e.x, e.y, e.z))) + o, 0});
      }
      break;
    }
    case 0x29: {
      auto& e = ev(ClientEvent::Type::Sound);
      e.text = r.string();
      e.x = r.i32() / 8.0;
      e.y = r.i32() / 8.0;
      e.z = r.i32() / 8.0;
      e.f = r.f32();
      e.a = r.u8();
      break;
    }
    case 0x2B: {
      auto& e = ev(ClientEvent::Type::GameState);
      e.a = r.u8();
      e.f = r.f32();
      break;
    }
    case 0x2D: {
      auto& e = ev(ClientEvent::Type::OpenWindow);
      e.a = r.u8();
      e.text = r.string();
      e.uuid = chatToText(r.string());  // título
      e.b = r.u8();
      break;
    }
    case 0x2E: ev(ClientEvent::Type::CloseWindow).a = r.u8(); break;
    case 0x2F: {
      auto& e = ev(ClientEvent::Type::SetSlot);
      e.a = r.i8();
      e.b = r.i16();
      e.item = readSlot(r);
      break;
    }
    case 0x30: {
      auto& e = ev(ClientEvent::Type::WindowItems);
      e.a = r.u8();
      const i16 n = r.i16();
      for (i16 i = 0; i < n; i++) e.items.push_back(readSlot(r));
      break;
    }
    case 0x32: {  // confirmar transacción: si la rechaza hay que contestar igual
      const i8 window = r.i8();
      const i16 action = r.i16();
      if (!r.boolean()) {
        BufferWriter w;
        w.i8(window).i16(action).boolean(true);
        send(0x0F, w);
      }
      break;
    }
    case 0x38: {
      const i32 action = r.varInt(), n = r.varInt();
      for (i32 i = 0; i < n && i < 1024; i++) {
        const i64 hi = r.i64(), lo = r.i64();
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
        std::string uuid = std::string(buf).insert(8, "-").insert(13, "-").insert(18, "-").insert(23, "-");
        if (action == 0) {
          auto& e = ev(ClientEvent::Type::PlayerListAdd);
          e.uuid = uuid;
          e.text = r.string();
          const i32 props = r.varInt();
          for (i32 k = 0; k < props; k++) {
            const std::string pname = r.string(), value = r.string();
            if (r.boolean()) r.string();
            if (pname == "textures") e.meta.string(0, value);  // skin (base64)
          }
          e.a = r.varInt();  // modo
          e.b = r.varInt();  // ping
          if (r.boolean()) r.string();
        } else if (action == 1 || action == 2) {
          r.varInt();
        } else if (action == 3) {
          if (r.boolean()) r.string();
        } else if (action == 4) {
          ev(ClientEvent::Type::PlayerListRemove).uuid = uuid;
        }
      }
      break;
    }
    case 0x39: {
      auto& e = ev(ClientEvent::Type::Abilities);
      e.a = r.i8();
      e.f = r.f32();
      break;
    }
    case 0x3F: {  // mensaje de plugin
      const std::string channel = r.string(64);
      const auto body = r.bytes(r.remaining());
      if (channel == "MC|Brand") {
        // Un servidor de MC-WEB lo dice: con él sí se habla el canal de skins
        try {
          BufferReader b(body);
          if (b.string(32) == "mc-web" && !mcwebServer_) {
            mcwebServer_ = true;
            sendSkin();
          }
        } catch (const DecodeError&) {
        }
      } else if (channel == kSkinChannel) {
        if (auto m = decodeSkinMessage(body, true)) {
          auto& e = ev(ClientEvent::Type::PlayerSkin);
          e.uuid = uuidToString(m->uuid);
          e.flag = m->slim;
          e.data = std::move(m->png);
        }
      }
      break;
    }
    case 0x40: fail("Desconectado: " + chatToText(r.string())); break;
    case 0x46: codec_.setCompression(r.varInt()); break;
    default: break;  // el resto (mapas, marcadores, títulos...) se ignora de momento
  }
}

void Client::sendPosition(const glm::dvec3& feet, float yaw, float pitch, bool onGround) {
  BufferWriter w;
  w.f64(feet.x).f64(feet.y).f64(feet.z).f32(yawToMc(yaw)).f32(pitchToMc(pitch)).boolean(onGround);
  send(0x06, w);
}

void Client::sendChat(const std::string& text) {
  BufferWriter w;
  w.string(text.substr(0, 100));
  send(0x01, w);
}

void Client::sendDig(int status, const glm::ivec3& pos, int face) {
  static const int kFace[6] = {0, 1, 2, 3, 4, 5};  // nuestras caras coinciden con las de 1.8
  BufferWriter w;
  w.i8(static_cast<i8>(status));
  writePosition(w, pos);
  w.i8(static_cast<i8>(face >= 0 && face < 6 ? kFace[face] : 0));
  send(0x07, w);
}

void Client::sendPlace(const glm::ivec3& pos, int face, const ItemStack& held, const glm::vec3& cursor) {
  BufferWriter w;
  if (face < 0) writePosition(w, {-1, 255, -1});  // usar el objeto (sin bloque)
  else writePosition(w, pos);
  w.i8(static_cast<i8>(face < 0 ? 255 : face));
  writeSlot(w, held);
  w.i8(static_cast<i8>(cursor.x * 16)).i8(static_cast<i8>(cursor.y * 16)).i8(static_cast<i8>(cursor.z * 16));
  send(0x08, w);
}

void Client::sendHeldItem(int slot) {
  BufferWriter w;
  w.i16(static_cast<i16>(slot));
  send(0x09, w);
}

void Client::sendSwing() { send(0x0A, BufferWriter()); }

void Client::sendUseEntity(i32 target, bool attack) {
  BufferWriter w;
  w.varInt(target).varInt(attack ? 1 : 0);
  send(0x02, w);
}

void Client::sendEntityAction(int action) {
  BufferWriter w;
  w.varInt(entityId_).varInt(action).varInt(0);
  send(0x0B, w);
}

void Client::sendClickWindow(int window, int slot, int button, int mode, const ItemStack& clicked) {
  BufferWriter w;
  w.u8(static_cast<u8>(window)).i16(static_cast<i16>(slot)).i8(static_cast<i8>(button)).i16(actionCounter_++).i8(static_cast<i8>(mode));
  writeSlot(w, clicked);
  send(0x0E, w);
}

void Client::sendCloseWindow(int window) {
  BufferWriter w;
  w.u8(static_cast<u8>(window));
  send(0x0D, w);
}

void Client::sendCreativeSlot(int slot, const ItemStack& item) {
  BufferWriter w;
  w.i16(static_cast<i16>(slot));
  writeSlot(w, item);
  send(0x10, w);
}

void Client::sendRespawn() {
  BufferWriter w;
  w.varInt(0);
  send(0x16, w);
}

// --- Estado del servidor (lista de Multijugador) ---

StatusPinger::StatusPinger(std::unique_ptr<Transport> t, std::string host, int port)
    : transport_(std::move(t)), host_(std::move(host)), port_(port) {
  if (!transport_) {
    done_ = true;
    error_ = "No se puede conectar desde aquí";
  }
}

bool StatusPinger::poll(double now) {
  if (done_) return true;
  if (start_ < 0) start_ = now;
  if (now - start_ > 8.0) {
    done_ = true;
    error_ = "El servidor no contesta";
    transport_->close();
    return true;
  }
  if (!sent_) {
    BufferWriter hs;
    hs.varInt(kProtocolVersion).string(host_).u16(static_cast<u16>(port_)).varInt(1);
    transport_->send(codec_.encode(0x00, hs.data()));
    transport_->send(codec_.encode(0x00, {}));
    sent_ = true;
  }
  const auto in = transport_->poll();
  if (!in.empty()) codec_.feed(in);
  try {
    while (auto p = codec_.next()) {
      BufferReader r(p->data);
      if (p->id == 0x00) {
        const auto j = nlohmann::json::parse(r.string(), nullptr, false);
        if (j.is_object()) {
          if (j.contains("description")) status_.motd = chatToText(j["description"].is_string() ? nlohmann::json(j["description"]).dump() : j["description"].dump());
          if (j.contains("version")) {
            status_.version = j["version"].value("name", "");
            status_.protocol = j["version"].value("protocol", 0);
          }
          if (j.contains("players")) {
            status_.online = j["players"].value("online", 0);
            status_.max = j["players"].value("max", 0);
            if (j["players"].contains("sample") && j["players"]["sample"].is_array())
              for (const auto& s : j["players"]["sample"]) status_.sample.push_back(s.value("name", ""));
          }
          if (j.contains("favicon") && j["favicon"].is_string()) {
            const std::string fav = j["favicon"].get<std::string>();
            const auto comma = fav.find(',');
            if (comma != std::string::npos) status_.faviconPng = base64Decode(std::string_view(fav).substr(comma + 1));
          }
        }
        gotInfo_ = true;
        BufferWriter ping;
        ping.i64(static_cast<i64>(now * 1000));
        pingSent_ = now;
        transport_->send(codec_.encode(0x01, ping.data()));
      } else if (p->id == 0x01) {
        status_.pingMs = static_cast<int>((now - pingSent_) * 1000);
        ok_ = done_ = true;
        transport_->close();
        return true;
      }
    }
  } catch (const DecodeError&) {
    done_ = true;
    error_ = "Respuesta no válida";
    return true;
  }
  if (transport_->status() == Transport::Status::Closed) {
    done_ = true;
    ok_ = gotInfo_;
    if (!gotInfo_) error_ = transport_->error().empty() ? "No se pudo conectar" : transport_->error();
  }
  return done_;
}

std::string base64Decode(std::string_view in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int buf = 0, bits = 0;
  for (char c : in) {
    const int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xFF);
    }
  }
  return out;
}

std::string base64Encode(std::string_view in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int buf = 0, bits = 0;
  for (unsigned char c : in) {
    buf = (buf << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out += t[(buf >> bits) & 63];
    }
  }
  if (bits > 0) out += t[(buf << (6 - bits)) & 63];
  while (out.size() % 4) out += '=';
  return out;
}

}  // namespace mcw::net
