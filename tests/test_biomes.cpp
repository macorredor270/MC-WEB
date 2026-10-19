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

#include "flat_world.h"
#include "game/session.h"

TEST_CASE("Aventurero: se concede al pisar los 39 biomas, cuando ya se tiene El fin") {
  testing::FlatTestWorld fw;
  GameSession s(fw, 5);
  s.setRules({2, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  auto standIn = [&](int biome) {
    Chunk* c = fw.w.chunk(0, 0);
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) c->setBiome(x, z, biome);
    s.tick(testing::idleTick());
  };
  int visited = 0;
  for (int id = 0; id < 256; id++)
    if (explorationIndex(id) >= 0) {
      standIn(id);
      visited++;
      CHECK(s.achievements().exploredBiomes() == visited);
      standIn(id);  // volver a pisarlo no suma
      CHECK(s.achievements().exploredBiomes() == visited);
      if (visited < kExplorationBiomes) CHECK_FALSE(s.achievements().has(Ach::ExploreAllBiomes));
    }
  CHECK(visited == kExplorationBiomes);
  // Con los 39 pisados pero sin El fin, el logro espera
  CHECK_FALSE(s.achievements().has(Ach::ExploreAllBiomes));
  for (int a = 0; a < static_cast<int>(Ach::ExploreAllBiomes); a++) s.achievements().award(static_cast<Ach>(a));
  for (int i = 0; i < 120; i++) s.tick(testing::idleTick());
  CHECK(s.achievements().has(Ach::ExploreAllBiomes));
  // Un bioma que no es de la lista (la llanura de pinchos de hielo) no cuenta
  CHECK_FALSE(s.achievements().markBiome(Biome::ice_spikes));
}
