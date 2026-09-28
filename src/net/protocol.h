#pragma once
// Protocolo de Minecraft 1.8 (versión 47), según la documentación pública (wiki.vg /
// minecraft.wiki "Protocol", versión 1.8). Implementación propia.
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/buffer.h"
#include "game/item_stack.h"
#include "world/chunk.h"

namespace mcw::net {

inline constexpr int kProtocolVersion = 47;
inline constexpr const char* kVersionName = "1.8.8";

enum class State { Handshake, Status, Login, Play };

struct Packet {
  i32 id = 0;
  std::vector<u8> data;
};

/// Tramas del protocolo: longitud VarInt y, con compresión activada, tamaño sin comprimir + zlib.
class PacketCodec {
 public:
  /// Umbral de compresión (-1 = sin compresión, como antes de "Set Compression").
  void setCompression(int threshold) { threshold_ = threshold; }
  int compression() const { return threshold_; }
  /// Paquete completo listo para enviar (sin cifrar).
  std::vector<u8> encode(i32 id, std::span<const u8> payload) const;
  /// Bytes recibidos (ya descifrados).
  void feed(std::span<const u8> bytes) { in_.insert(in_.end(), bytes.begin(), bytes.end()); }
  /// Siguiente paquete completo. Lanza DecodeError si los datos no son válidos.
  std::optional<Packet> next();
  std::size_t buffered() const { return in_.size() - pos_; }

 private:
  std::vector<u8> in_;
  std::size_t pos_ = 0;
  int threshold_ = -1;
};

// --- Tipos del protocolo ---

/// Posición de bloque empaquetada en 64 bits (x 26, y 12, z 26).
void writePosition(BufferWriter& w, const glm::ivec3& p);
glm::ivec3 readPosition(BufferReader& r);

/// Objeto: id (short, -1 vacío), cantidad, daño y NBT (sin NBT por ahora al escribir).
void writeSlot(BufferWriter& w, const ItemStack& s);
ItemStack readSlot(BufferReader& r);

/// Ángulo en 1/256 de vuelta.
inline u8 toAngle(float degrees) { return static_cast<u8>(static_cast<int>(std::floor(degrees * 256.0f / 360.0f)) & 255); }
inline float fromAngle(u8 a) { return a * 360.0f / 256.0f; }
/// Coordenada de entidad en punto fijo (1/32 de bloque).
inline i32 toFixed(double v) { return static_cast<i32>(std::floor(v * 32.0)); }
inline double fromFixed(i32 v) { return v / 32.0; }

/// Metadatos de entidad (los valores que usamos; el resto se lee y se descarta).
struct Metadata {
  struct Entry {
    u8 index = 0, type = 0;
    i64 i = 0;       // byte, short, int
    float f = 0;     // float
    std::string s;   // string
    ItemStack item;  // slot
  };
  std::vector<Entry> entries;
  void byte(u8 index, i8 v) { entries.push_back({index, 0, v}); }
  void shortV(u8 index, i16 v) { entries.push_back({index, 1, v}); }
  void intV(u8 index, i32 v) { entries.push_back({index, 2, v}); }
  void floatV(u8 index, float v) { Entry e{index, 3}; e.f = v; entries.push_back(e); }
  void string(u8 index, std::string v) { Entry e{index, 4}; e.s = std::move(v); entries.push_back(e); }
  const Entry* find(u8 index) const {
    for (const Entry& e : entries)
      if (e.index == index) return &e;
    return nullptr;
  }
};
void writeMetadata(BufferWriter& w, const Metadata& m);
Metadata readMetadata(BufferReader& r);

/// Chat: de JSON a texto con códigos de color (§) y de texto a JSON.
std::string chatToText(std::string_view json);
std::string textToChat(std::string_view text);

// --- Chunks (formato de red de 1.8) ---

/// Datos de una columna completa (groundUp): bloques como u16 (id << 4 | meta, little endian), luz de
/// bloque, luz de cielo y biomas. `mask` recibe las secciones incluidas.
std::vector<u8> encodeChunkColumn(const Chunk& c, bool skyLight, u16& mask);
/// Rellena `c` con los datos de red. Lanza DecodeError si faltan bytes. Devuelve los bytes leídos.
std::size_t decodeChunkColumn(Chunk& c, std::span<const u8> data, u16 mask, bool skyLight, bool groundUp);

/// UUID en 16 bytes desde "xxxxxxxx-xxxx-...".
std::array<u8, 16> uuidFromString(std::string_view s);

}  // namespace mcw::net
