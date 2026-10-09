#include "net/protocol.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>

#include "core/png.h"
#include "core/zip.h"
#include "save/anvil.h"
#include "save/nbt.h"

namespace mcw::net {

std::vector<u8> PacketCodec::encode(i32 id, std::span<const u8> payload) const {
  BufferWriter body;
  body.varInt(id).bytes(payload);
  BufferWriter out;
  if (threshold_ < 0) {
    out.varInt(static_cast<i32>(body.size())).bytes(body.data());
    return out.take();
  }
  if (static_cast<int>(body.size()) < threshold_) {
    BufferWriter inner;
    inner.varInt(0).bytes(body.data());
    out.varInt(static_cast<i32>(inner.size())).bytes(inner.data());
    return out.take();
  }
  const std::vector<u8> z = zlibCompress(body.data().data(), body.size());
  BufferWriter inner;
  inner.varInt(static_cast<i32>(body.size())).bytes(z);
  out.varInt(static_cast<i32>(inner.size())).bytes(inner.data());
  return out.take();
}

std::optional<Packet> PacketCodec::next() {
  // Longitud (VarInt) al principio de lo que queda
  const std::span<const u8> avail(in_.data() + pos_, in_.size() - pos_);
  i32 len = 0;
  int shift = 0;
  std::size_t n = 0;
  for (;; n++) {
    if (n >= avail.size()) return std::nullopt;  // falta la longitud entera
    if (n >= 5) throw DecodeError("longitud de paquete no válida");
    const u8 b = avail[n];
    len |= (b & 0x7F) << shift;
    shift += 7;
    if (!(b & 0x80)) { n++; break; }
  }
  if (len < 0 || len > 2 * 1024 * 1024) throw DecodeError("paquete demasiado grande");
  if (avail.size() < n + static_cast<std::size_t>(len)) return std::nullopt;
  std::span<const u8> frame = avail.subspan(n, static_cast<std::size_t>(len));
  pos_ += n + static_cast<std::size_t>(len);
  if (pos_ > 1 << 20) {  // compactar el búfer de vez en cuando
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(pos_));
    pos_ = 0;
  }
  std::vector<u8> body;
  if (threshold_ >= 0) {
    BufferReader r(frame);
    const i32 dataLen = r.varInt();
    const auto rest = r.bytes(r.remaining());
    if (dataLen == 0) {
      body.assign(rest.begin(), rest.end());
    } else {
      if (dataLen < 0 || dataLen > 8 * 1024 * 1024) throw DecodeError("tamaño descomprimido no válido");
      auto d = zlibDecompress(rest.data(), rest.size(), static_cast<std::size_t>(dataLen));
      if (!d) throw DecodeError("zlib no válido");
      body = std::move(*d);
    }
  } else {
    body.assign(frame.begin(), frame.end());
  }
  BufferReader r(body);
  Packet p;
  p.id = r.varInt();
  const auto payload = r.bytes(r.remaining());
  p.data.assign(payload.begin(), payload.end());
  return p;
}

void writePosition(BufferWriter& w, const glm::ivec3& p) {
  const u64 v = ((static_cast<u64>(p.x) & 0x3FFFFFF) << 38) | ((static_cast<u64>(p.y) & 0xFFF) << 26) | (static_cast<u64>(p.z) & 0x3FFFFFF);
  w.i64(static_cast<i64>(v));
}

glm::ivec3 readPosition(BufferReader& r) {
  const i64 v = r.i64();
  // Desplazamientos aritméticos: conservan el signo de x y z
  const i32 x = static_cast<i32>(v >> 38);
  i32 y = static_cast<i32>((v >> 26) & 0xFFF);
  const i32 z = static_cast<i32>(static_cast<i64>(static_cast<u64>(v) << 38) >> 38);
  if (y >= 2048) y -= 4096;
  return {x, y, z};
}

void writeSlot(BufferWriter& w, const ItemStack& s) {
  if (s.empty()) {
    w.i16(-1);
    return;
  }
  w.i16(static_cast<i16>(s.id)).i8(static_cast<i8>(s.count)).i16(s.meta);
  if (s.extra) w.bytes(nbt::write(save::itemTagToNbt(*s.extra)));  // encantamientos, nombre...
  else w.u8(0);                                                    // sin NBT
}

ItemStack readSlot(BufferReader& r) {
  const i16 id = r.i16();
  if (id < 0) return {};
  const i8 count = r.i8();
  const i16 damage = r.i16();
  ItemStack s(id, count, damage);
  if (const auto tag = nbt::readFrom(r)) s.extra = save::itemTagFromNbt(*tag);  // encantamientos, nombre...
  return s;
}

void writeMetadata(BufferWriter& w, const Metadata& m) {
  for (const auto& e : m.entries) {
    w.u8(static_cast<u8>((e.type << 5) | (e.index & 31)));
    switch (e.type) {
      case 0: w.i8(static_cast<i8>(e.i)); break;
      case 1: w.i16(static_cast<i16>(e.i)); break;
      case 2: w.i32(static_cast<i32>(e.i)); break;
      case 3: w.f32(e.f); break;
      case 4: w.string(e.s); break;
      case 5: writeSlot(w, e.item); break;
      default: break;
    }
  }
  w.u8(0x7F);
}

Metadata readMetadata(BufferReader& r) {
  Metadata m;
  for (int guard = 0; guard < 256; guard++) {
    const u8 key = r.u8();
    if (key == 0x7F) return m;
    Metadata::Entry e;
    e.index = key & 31;
    e.type = key >> 5;
    switch (e.type) {
      case 0: e.i = r.i8(); break;
      case 1: e.i = r.i16(); break;
      case 2: e.i = r.i32(); break;
      case 3: e.f = r.f32(); break;
      case 4: e.s = r.string(); break;
      case 5: e.item = readSlot(r); break;
      case 6: r.i32(); r.i32(); r.i32(); break;
      case 7: r.f32(); r.f32(); r.f32(); break;
      default: throw DecodeError("metadatos no válidos");
    }
    m.entries.push_back(std::move(e));
  }
  throw DecodeError("demasiados metadatos");
}

namespace {

const std::map<std::string, char>& colorCodes() {
  static const std::map<std::string, char> c = {
      {"black", '0'},     {"dark_blue", '1'}, {"dark_green", '2'}, {"dark_aqua", '3'}, {"dark_red", '4'},
      {"dark_purple", '5'}, {"gold", '6'},    {"gray", '7'},       {"dark_gray", '8'}, {"blue", '9'},
      {"green", 'a'},     {"aqua", 'b'},      {"red", 'c'},        {"light_purple", 'd'}, {"yellow", 'e'},
      {"white", 'f'},
  };
  return c;
}

/// Textos de 1.8 más habituales en el chat de los servidores (traducción propia).
std::string translateKey(const std::string& key) {
  static const std::map<std::string, std::string> t = {
      {"chat.type.text", "<%s> %s"},
      {"chat.type.announcement", "[%s] %s"},
      {"chat.type.emote", "* %s %s"},
      {"multiplayer.player.joined", "%s se ha unido a la partida"},
      {"multiplayer.player.left", "%s ha salido de la partida"},
      {"commands.generic.notFound", "Comando desconocido"},
      {"death.attack.generic", "%s ha muerto"},
      {"death.fell.accident.generic", "%s se ha caído"},
      {"death.attack.player", "%s ha sido asesinado por %s"},
      {"death.attack.mob", "%s ha sido asesinado por %s"},
      {"death.attack.lava", "%s ha intentado nadar en lava"},
      {"death.attack.drown", "%s se ha ahogado"},
      {"death.attack.fall", "%s se ha estampado contra el suelo"},
      {"death.attack.explosion.player", "%s ha explotado por culpa de %s"},
      {"death.attack.arrow", "%s ha recibido un flechazo de %s"},
      {"disconnect.kicked", "Te han expulsado"},
      {"disconnect.timeout", "Tiempo de espera agotado"},
  };
  auto it = t.find(key);
  return it == t.end() ? key : it->second;
}

void appendChat(const nlohmann::json& j, std::string& out, const std::string& inherited) {
  if (j.is_string()) {
    out += j.get<std::string>();
    return;
  }
  if (j.is_array()) {
    for (const auto& e : j) appendChat(e, out, inherited);
    return;
  }
  if (!j.is_object()) return;
  std::string fmt = inherited;
  if (j.contains("color") && j["color"].is_string()) {
    auto it = colorCodes().find(j["color"].get<std::string>());
    if (it != colorCodes().end()) fmt = std::string("\xC2\xA7") + it->second;
  }
  if (!fmt.empty()) out += fmt;
  if (j.contains("text") && j["text"].is_string()) out += j["text"].get<std::string>();
  if (j.contains("translate") && j["translate"].is_string()) {
    std::string pattern = translateKey(j["translate"].get<std::string>());
    std::vector<std::string> args;
    if (j.contains("with") && j["with"].is_array())
      for (const auto& a : j["with"]) {
        std::string s;
        appendChat(a, s, fmt);
        args.push_back(s);
      }
    std::size_t argi = 0;
    for (std::size_t i = 0; i < pattern.size(); i++) {
      if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 's') {
        if (argi < args.size()) out += args[argi++];
        i++;
      } else if (pattern[i] == '%' && i + 3 < pattern.size() && pattern[i + 2] == '$' && pattern[i + 3] == 's') {
        const std::size_t k = static_cast<std::size_t>(pattern[i + 1] - '1');
        if (k < args.size()) out += args[k];
        i += 3;
      } else {
        out += pattern[i];
      }
    }
  }
  if (j.contains("extra")) appendChat(j["extra"], out, fmt);
}

}  // namespace

std::string chatToText(std::string_view json) {
  const auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded()) return std::string(json);
  std::string out;
  appendChat(j, out, "");
  return out;
}

std::string textToChat(std::string_view text) { return nlohmann::json{{"text", std::string(text)}}.dump(); }

std::vector<u8> encodeChunkColumn(const Chunk& c, bool skyLight, u16& mask) {
  mask = 0;
  for (int i = 0; i < kSectionCount; i++)
    if (const Section* s = c.section(i); s && s->nonAir > 0) mask |= static_cast<u16>(1 << i);
  std::vector<u8> out;
  const int count = std::popcount(static_cast<unsigned>(mask));
  out.reserve(static_cast<std::size_t>(count) * (8192 + 2048 + (skyLight ? 2048 : 0)) + 256);
  for (int i = 0; i < kSectionCount; i++) {
    if (!(mask & (1 << i))) continue;
    const Section* s = c.section(i);
    for (int k = 0; k < 4096; k++) {
      out.push_back(static_cast<u8>(s->blocks[k] & 0xFF));
      out.push_back(static_cast<u8>(s->blocks[k] >> 8));
    }
  }
  for (int i = 0; i < kSectionCount; i++) {
    if (!(mask & (1 << i))) continue;
    const Section* s = c.section(i);
    for (int k = 0; k < 4096; k += 2) out.push_back(static_cast<u8>((s->light[k] & 15) | ((s->light[k + 1] & 15) << 4)));
  }
  if (skyLight)
    for (int i = 0; i < kSectionCount; i++) {
      if (!(mask & (1 << i))) continue;
      const Section* s = c.section(i);
      for (int k = 0; k < 4096; k += 2) out.push_back(static_cast<u8>((s->light[k] >> 4) | (s->light[k + 1] & 0xF0)));
    }
  out.insert(out.end(), c.biomes().begin(), c.biomes().end());
  return out;
}

std::size_t decodeChunkColumn(Chunk& c, std::span<const u8> data, u16 mask, bool skyLight, bool groundUp) {
  const int count = std::popcount(static_cast<unsigned>(mask));
  const std::size_t need = static_cast<std::size_t>(count) * (8192 + 2048 + (skyLight ? 2048 : 0)) + (groundUp ? 256 : 0);
  if (data.size() < need) throw DecodeError("datos de chunk incompletos");
  std::size_t p = 0;
  for (int i = 0; i < kSectionCount; i++) {
    if (!(mask & (1 << i))) continue;
    Section& s = c.ensureSection(i);
    int nonAir = 0;
    for (int k = 0; k < 4096; k++) {
      const BlockState st = static_cast<BlockState>(data[p] | (data[p + 1] << 8));
      s.blocks[k] = st;
      nonAir += st != 0;
      p += 2;
    }
    s.nonAir = nonAir;
  }
  for (int i = 0; i < kSectionCount; i++) {
    if (!(mask & (1 << i))) continue;
    Section& s = *c.section(i);
    for (int k = 0; k < 4096; k += 2) {
      const u8 b = data[p++];
      s.light[k] = static_cast<u8>((s.light[k] & 0xF0) | (b & 15));
      s.light[k + 1] = static_cast<u8>((s.light[k + 1] & 0xF0) | (b >> 4));
    }
  }
  if (skyLight)
    for (int i = 0; i < kSectionCount; i++) {
      if (!(mask & (1 << i))) continue;
      Section& s = *c.section(i);
      for (int k = 0; k < 4096; k += 2) {
        const u8 b = data[p++];
        s.light[k] = static_cast<u8>((s.light[k] & 15) | ((b & 15) << 4));
        s.light[k + 1] = static_cast<u8>((s.light[k + 1] & 15) | (b & 0xF0));
      }
    }
  if (groundUp) {
    for (int k = 0; k < 256; k++) c.setBiome(k & 15, k >> 4, data[p + k]);
    p += 256;
  }
  c.recomputeHeightMap();
  return p;
}

std::array<u8, 16> uuidFromString(std::string_view s) {
  std::array<u8, 16> out{};
  int n = 0;
  auto hex = [](char ch) { return ch >= 'a' ? ch - 'a' + 10 : (ch >= 'A' ? ch - 'A' + 10 : ch - '0'); };
  for (std::size_t i = 0; i + 1 < s.size() && n < 16;) {
    if (s[i] == '-') { i++; continue; }
    out[n++] = static_cast<u8>((hex(s[i]) << 4) | hex(s[i + 1]));
    i += 2;
  }
  return out;
}

std::vector<u8> encodeSkinMessage(const SkinMessage& m, bool withUuid) {
  BufferWriter w;
  w.u8(1);  // versión
  if (withUuid) w.bytes(m.uuid);
  w.u8(m.slim ? 1 : 0);
  w.bytes(m.png);
  return w.data();
}

std::optional<SkinMessage> decodeSkinMessage(std::span<const u8> data, bool withUuid) {
  try {
    BufferReader r(data);
    if (r.u8() != 1) return std::nullopt;
    SkinMessage m;
    if (withUuid) {
      const auto id = r.bytes(16);
      std::copy(id.begin(), id.end(), m.uuid.begin());
    }
    m.slim = (r.u8() & 1) != 0;
    const auto png = r.bytes(r.remaining());
    if (png.empty() || png.size() > kMaxSkinBytes) return std::nullopt;
    const auto size = pngSize(png);
    if (!size || size->first != 64 || (size->second != 64 && size->second != 32)) return std::nullopt;
    m.png.assign(png.begin(), png.end());
    return m;
  } catch (const DecodeError&) {
    return std::nullopt;
  }
}

}  // namespace mcw::net
