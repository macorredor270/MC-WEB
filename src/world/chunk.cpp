#include "world/chunk.h"

namespace mcw {

Section& Chunk::ensureSection(int i) {
  if (!sections_[i]) sections_[i] = std::make_unique<Section>();
  return *sections_[i];
}

void Chunk::setBlock(int x, int y, int z, BlockState state) {
  if (y < 0 || y >= kChunkHeight) return;
  Section* s = sections_[y >> 4].get();
  if (!s) {
    if (state == 0) return;
    s = &ensureSection(y >> 4);
  }
  BlockState& slot = s->blocks[Section::index(x, y & 15, z)];
  if (slot == 0 && state != 0) s->nonAir++;
  else if (slot != 0 && state == 0) s->nonAir--;
  slot = state;
}

void Chunk::setSkyLight(int x, int y, int z, int v) {
  Section* s = sections_[y >> 4].get();
  if (!s) {
    if (v == 15) return;
    s = &ensureSection(y >> 4);
  }
  u8& l = s->light[Section::index(x, y & 15, z)];
  l = static_cast<u8>((v << 4) | (l & 15));
}

void Chunk::setBlockLight(int x, int y, int z, int v) {
  Section* s = sections_[y >> 4].get();
  if (!s) {
    if (v == 0) return;
    s = &ensureSection(y >> 4);
  }
  u8& l = s->light[Section::index(x, y & 15, z)];
  l = static_cast<u8>((l & 0xF0) | v);
}

void Chunk::recomputeHeightMap() {
  const int top = topSection();
  for (int z = 0; z < 16; z++) {
    for (int x = 0; x < 16; x++) {
      int h = 0;
      for (int y = (top + 1) * 16 - 1; y >= 0; y--) {
        if (blockInfo(stateId(block(x, y, z))).opacity > 0) { h = y + 1; break; }
      }
      heightMap_[(z << 4) | x] = static_cast<u16>(h);
    }
  }
}

int Chunk::topSection() const {
  for (int i = kSectionCount - 1; i >= 0; i--)
    if (sections_[i] && sections_[i]->nonAir > 0) return i;
  return -1;
}

}  // namespace mcw
