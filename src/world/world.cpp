#include "world/world.h"

namespace mcw {

void World::insert(std::unique_ptr<Chunk> c, ChunkSet& modified) {
  const ChunkPos pos = c->pos();
  chunks_[pos] = std::move(c);
  modified.insert(pos);
  light_.stitch(pos, modified);
}

std::unique_ptr<Chunk> World::remove(ChunkPos pos) {
  auto it = chunks_.find(pos);
  if (it == chunks_.end()) return nullptr;
  auto c = std::move(it->second);
  chunks_.erase(it);
  return c;
}

void World::setBlock(int x, int y, int z, BlockState s, ChunkSet& modified) {
  Chunk* c = chunkAt(x, z);
  if (!c || y < 0 || y >= kChunkHeight) return;
  c->setBlock(x & 15, y, z & 15, s);
  c->recomputeHeightMap();
  light_.blockChanged(x, y, z, modified);
}

}  // namespace mcw
