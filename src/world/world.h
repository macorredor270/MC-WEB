#pragma once
#include <memory>
#include <unordered_map>

#include "world/chunk.h"
#include "world/light.h"

namespace mcw {

/// Conjunto de chunks cargados con acceso por coordenadas de mundo.
class World {
 public:
  World() : light_(*this) {}

  Chunk* chunk(int cx, int cz) {
    auto it = chunks_.find({cx, cz});
    return it == chunks_.end() ? nullptr : it->second.get();
  }
  const Chunk* chunk(int cx, int cz) const {
    auto it = chunks_.find({cx, cz});
    return it == chunks_.end() ? nullptr : it->second.get();
  }
  Chunk* chunkAt(int x, int z) { return chunk(x >> 4, z >> 4); }
  const Chunk* chunkAt(int x, int z) const { return chunk(x >> 4, z >> 4); }

  BlockState block(int x, int y, int z) const {
    if (y < 0 || y >= kChunkHeight) return 0;
    const Chunk* c = chunkAt(x, z);
    return c ? c->block(x & 15, y, z & 15) : BlockState{0};
  }
  int skyLight(int x, int y, int z) const {
    if (y >= kChunkHeight) return 15;
    if (y < 0) return 0;
    const Chunk* c = chunkAt(x, z);
    return c ? c->skyLight(x & 15, y, z & 15) : 15;
  }
  int blockLight(int x, int y, int z) const {
    if (y < 0 || y >= kChunkHeight) return 0;
    const Chunk* c = chunkAt(x, z);
    return c ? c->blockLight(x & 15, y, z & 15) : 0;
  }
  int biome(int x, int z) const {
    const Chunk* c = chunkAt(x, z);
    return c ? c->biome(x & 15, z & 15) : 1;
  }

  /// Inserta un chunk ya generado (con su luz inicial) y cose la luz con los vecinos.
  /// `modified` recibe los chunks cuya luz ha cambiado (incluido el nuevo).
  void insert(std::unique_ptr<Chunk> c, ChunkSet& modified);
  std::unique_ptr<Chunk> remove(ChunkPos pos);

  /// Cambia un bloque y actualiza la luz. Devuelve los chunks afectados.
  void setBlock(int x, int y, int z, BlockState s, ChunkSet& modified);

  std::size_t size() const { return chunks_.size(); }
  const std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>& chunks() const { return chunks_; }

 private:
  std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> chunks_;
  LightEngine light_;
};

}  // namespace mcw
