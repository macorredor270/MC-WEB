#include <doctest/doctest.h>

#include <map>
#include <set>

#include "data/biomes.h"
#include "world/generator.h"

using namespace mcw;

TEST_CASE("Biomas: el mundo normal da todos los de la lista de exploración (salvo Nether y End)") {
  std::set<int> seen;
  for (u64 seed = 1; seed <= 10; seed++) {
    TerrainGenerator g(seed);
    for (int z = -100; z < 100; z++)
      for (int x = -100; x < 100; x++) seen.insert(g.column(x * 40, z * 40).biome);
  }
  for (int id = 0; id < 256; id++) {
    const int idx = explorationIndex(id);
    if (idx < 0 || id == Biome::hell || id == Biome::the_end) continue;
    CAPTURE(id);
    CAPTURE(biomeInfo(id).name);
    CHECK(seen.count(id) == 1);
  }
}

TEST_CASE("Biomas: la lista de exploración tiene 39 y las colinas se reducen a su bioma de base") {
  int n = 0;
  for (int id = 0; id < 256; id++) n += explorationIndex(id) >= 0;
  CHECK(n == kExplorationBiomes);
  CHECK(baseBiome(Biome::jungle_hills) == Biome::jungle);
  CHECK(baseBiome(Biome::mesa_plateau_f) == Biome::mesa);
  CHECK(baseBiome(Biome::forest_hills) == Biome::forest);
  CHECK(baseBiome(Biome::plains) == Biome::plains);
}
