#pragma once
#include <array>
#include <cstddef>
#include <functional>
#include <memory>

#include "core/types.h"
#include "data/blocks.h"

namespace mcw {

inline constexpr int kChunkHeight = 256;
inline constexpr int kSectionCount = 16;

struct ChunkPos {
  int x = 0, z = 0;
  bool operator==(const ChunkPos&) const = default;
};

struct ChunkPosHash {
  std::size_t operator()(const ChunkPos& p) const noexcept {
    return std::hash<u64>()((static_cast<u64>(static_cast<u32>(p.x)) << 32) | static_cast<u32>(p.z));
  }
};

/// Sección de 16x16x16. Índice (y << 8) | (z << 4) | x, el mismo orden que el protocolo de 1.8.
struct Section {
  std::array<BlockState, 4096> blocks{};
  std::array<u8, 4096> light{};  // luz de cielo << 4 | luz de bloque
  int nonAir = 0;

  Section() { light.fill(0xF0); }
  static constexpr int index(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
};

/// Columna de 16x256x16. Las secciones que faltan son aire con luz de cielo 15.
class Chunk {
 public:
  Chunk(int cx, int cz) : pos_{cx, cz} { biomes_.fill(1); heightMap_.fill(0); }

  ChunkPos pos() const { return pos_; }

  BlockState block(int x, int y, int z) const {
    const Section* s = sections_[y >> 4].get();
    return s ? s->blocks[Section::index(x, y & 15, z)] : BlockState{0};
  }
  void setBlock(int x, int y, int z, BlockState state);

  int skyLight(int x, int y, int z) const {
    const Section* s = sections_[y >> 4].get();
    return s ? s->light[Section::index(x, y & 15, z)] >> 4 : 15;
  }
  int blockLight(int x, int y, int z) const {
    const Section* s = sections_[y >> 4].get();
    return s ? s->light[Section::index(x, y & 15, z)] & 15 : 0;
  }
  u8 packedLight(int x, int y, int z) const {
    const Section* s = sections_[y >> 4].get();
    return s ? s->light[Section::index(x, y & 15, z)] : u8{0xF0};
  }
  void setSkyLight(int x, int y, int z, int v);
  void setBlockLight(int x, int y, int z, int v);

  int biome(int x, int z) const { return biomes_[(z << 4) | x]; }
  void setBiome(int x, int z, int b) { biomes_[(z << 4) | x] = static_cast<u8>(b); }
  const std::array<u8, 256>& biomes() const { return biomes_; }

  /// Primera altura (desde arriba) por encima de la cual la luz del cielo llega sin atenuar.
  int height(int x, int z) const { return heightMap_[(z << 4) | x]; }
  void recomputeHeightMap();

  const Section* section(int i) const { return sections_[i].get(); }
  Section* section(int i) { return sections_[i].get(); }
  Section& ensureSection(int i);
  /// Índice de la sección más alta que existe, o -1 si la columna está vacía.
  int topSection() const;

 private:
  ChunkPos pos_;
  std::array<std::unique_ptr<Section>, kSectionCount> sections_{};
  std::array<u8, 256> biomes_{};
  std::array<u16, 256> heightMap_{};
};

}  // namespace mcw
