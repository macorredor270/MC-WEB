#include "world/chunk_codec.h"

#include <bit>
#include <cstring>

namespace mcw {
namespace {

constexpr u32 kMagic = 0x4357434D;  // "MCWC"

template <typename T>
void put(std::vector<u8>& out, const T& v) {
  const auto* p = reinterpret_cast<const u8*>(&v);
  out.insert(out.end(), p, p + sizeof(T));
}

struct Reader {
  const u8* p;
  const u8* end;
  template <typename T>
  bool get(T& v) {
    if (static_cast<std::size_t>(end - p) < sizeof(T)) return false;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
  }
};

}  // namespace

std::vector<u8> encodeChunk(const Chunk& c) {
  u16 mask = 0;
  for (int i = 0; i < kSectionCount; i++)
    if (c.section(i)) mask = static_cast<u16>(mask | (1u << i));
  std::vector<u8> out;
  out.reserve(16 + 256 * 3 + static_cast<std::size_t>(std::popcount(mask)) * (4 + sizeof(Section::blocks) + sizeof(Section::light)));
  put(out, kMagic);
  put(out, c.pos().x);
  put(out, c.pos().z);
  put(out, mask);
  put(out, c.biomes());
  put(out, c.heightMap());
  for (int i = 0; i < kSectionCount; i++) {
    const Section* s = c.section(i);
    if (!s) continue;
    put(out, static_cast<i32>(s->nonAir));
    put(out, s->blocks);
    put(out, s->light);
  }
  return out;
}

std::unique_ptr<Chunk> decodeChunk(const u8* data, std::size_t size) {
  Reader r{data, data + size};
  u32 magic = 0;
  int cx = 0, cz = 0;
  u16 mask = 0;
  std::array<u8, 256> biomes{};
  std::array<u16, 256> heights{};
  if (!r.get(magic) || magic != kMagic || !r.get(cx) || !r.get(cz) || !r.get(mask) || !r.get(biomes) || !r.get(heights))
    return nullptr;
  auto c = std::make_unique<Chunk>(cx, cz);
  for (int i = 0; i < 256; i++) c->setBiome(i & 15, i >> 4, biomes[i]);
  c->setHeightMap(heights);
  for (int i = 0; i < kSectionCount; i++) {
    if (!(mask & (1u << i))) continue;
    Section& s = c->ensureSection(i);
    i32 nonAir = 0;
    if (!r.get(nonAir) || !r.get(s.blocks) || !r.get(s.light)) return nullptr;
    s.nonAir = nonAir;
  }
  return c;
}

}  // namespace mcw
