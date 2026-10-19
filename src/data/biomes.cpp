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

int baseBiome(int id) {
  switch (id) {
    case Biome::desert_hills: return Biome::desert;
    case Biome::forest_hills: return Biome::forest;
    case Biome::taiga_hills: return Biome::taiga;
    case Biome::jungle_hills: return Biome::jungle;
    case Biome::birch_forest_hills: return Biome::birch_forest;
    case Biome::cold_taiga_hills: return Biome::cold_taiga;
    case Biome::mega_taiga_hills: return Biome::mega_taiga;
    case Biome::ice_mountains: return Biome::ice_plains;
    case Biome::savanna_plateau: return Biome::savanna;
    case Biome::mesa_plateau_f: case Biome::mesa_plateau: return Biome::mesa;
    case Biome::extreme_hills_plus: case Biome::mountain_edge: case Biome::gravelly_mountains: case Biome::modified_gravelly_mountains:
      return Biome::extreme_hills;
    case Biome::sunflower_plains: return Biome::plains;
    case Biome::desert_lakes: return Biome::desert;
    case Biome::flower_forest: return Biome::forest;
    case Biome::taiga_mountains: return Biome::taiga;
    case Biome::swamp_hills: return Biome::swamp;
    case Biome::modified_jungle: return Biome::jungle;
    case Biome::modified_jungle_edge: return Biome::jungle_edge;
    case Biome::tall_birch_forest: case Biome::tall_birch_hills: return Biome::birch_forest;
    case Biome::dark_forest_hills: return Biome::roofed_forest;
    case Biome::snowy_taiga_mountains: return Biome::cold_taiga;
    case Biome::giant_spruce_taiga: case Biome::giant_spruce_taiga_hills: return Biome::mega_taiga;
    case Biome::shattered_savanna: case Biome::shattered_savanna_plateau: return Biome::savanna;
    case Biome::eroded_badlands: case Biome::modified_wooded_badlands_plateau: case Biome::modified_badlands_plateau: return Biome::mesa;
    default: return id;
  }
}

int explorationIndex(int id) {
  // La lista de 1.8 (BiomeGenBase.explorationBiomesList); el cielo del End y el Nether cuentan
  static constexpr int kList[kExplorationBiomes] = {
      Biome::ocean, Biome::plains, Biome::desert, Biome::extreme_hills, Biome::forest, Biome::taiga, Biome::swamp, Biome::river,
      Biome::hell, Biome::the_end, Biome::frozen_ocean, Biome::frozen_river, Biome::ice_plains, Biome::ice_mountains,
      Biome::mushroom_island, Biome::mushroom_shore, Biome::beach, Biome::desert_hills, Biome::forest_hills, Biome::taiga_hills,
      Biome::jungle, Biome::jungle_hills, Biome::jungle_edge, Biome::deep_ocean, Biome::stone_beach, Biome::cold_beach,
      Biome::birch_forest, Biome::birch_forest_hills, Biome::roofed_forest, Biome::cold_taiga, Biome::cold_taiga_hills,
      Biome::mega_taiga, Biome::mega_taiga_hills, Biome::extreme_hills_plus, Biome::savanna, Biome::savanna_plateau, Biome::mesa,
      Biome::mesa_plateau_f, Biome::mesa_plateau};
  for (int i = 0; i < kExplorationBiomes; i++)
    if (kList[i] == id) return i;
  return -1;
}

std::pair<int, int> colormapCoords(int biomeId) {
  const BiomeInfo& b = biomeInfo(biomeId);
  const float t = std::clamp(b.temperature, 0.0f, 1.0f);
  const float r = std::clamp(b.rainfall, 0.0f, 1.0f) * t;
  return {static_cast<int>((1.0f - t) * 255.0f), static_cast<int>((1.0f - r) * 255.0f)};
}

}  // namespace mcw
