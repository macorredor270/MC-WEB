#include "data/biomes.h"

#include <algorithm>
#include <array>

namespace mcw {
namespace {

struct RawBiome {
  int id;
  const char* name;
  const char* displayName;
  float temperature, rainfall;
  u32 color;
};

constexpr RawBiome kRaw[] = {
#include "data/generated/biomes.inc"
};

struct Registry {
  std::array<BiomeInfo, 256> byId{};
  std::array<bool, 256> exists{};
  Registry() {
    for (const RawBiome& r : kRaw) {
      if (r.id < 0 || r.id >= 256) continue;
      byId[r.id] = BiomeInfo{r.id, r.name, r.displayName, r.temperature, r.rainfall, r.color};
      exists[r.id] = true;
    }
  }
};

const Registry& registry() {
  static const Registry r;
  return r;
}

}  // namespace

const BiomeInfo& biomeInfo(int id) {
  const auto& r = registry();
  if (id < 0 || id >= 256 || !r.exists[id]) return r.byId[Biome::plains];
  return r.byId[id];
}

std::pair<int, int> colormapCoords(int biomeId) {
  const BiomeInfo& b = biomeInfo(biomeId);
  const float t = std::clamp(b.temperature, 0.0f, 1.0f);
  const float r = std::clamp(b.rainfall, 0.0f, 1.0f) * t;
  return {static_cast<int>((1.0f - t) * 255.0f), static_cast<int>((1.0f - r) * 255.0f)};
}

}  // namespace mcw
