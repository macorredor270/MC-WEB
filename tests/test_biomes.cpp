#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <tuple>
#include <optional>
#include <set>
#include <string>

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
    CAPTURE(std::string(biomeInfo(id).name));
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

TEST_CASE("Templo de la jungla: se encuentra en la jungla y tiene adoquín musgoso y tres cofres de botín") {
  // Busca una semilla y región con templo
  TerrainGenerator* found = nullptr;
  std::optional<TerrainGenerator::TempleSite> site;
  std::vector<std::unique_ptr<TerrainGenerator>> gens;
  for (u64 seed = 1; seed <= 40 && !site; seed++) {
    gens.push_back(std::make_unique<TerrainGenerator>(seed));
    for (int rz = -3; rz <= 3 && !site; rz++)
      for (int rx = -3; rx <= 3 && !site; rx++)
        if ((site = gens.back()->jungleTempleSite(rx, rz))) found = gens.back().get();
  }
  REQUIRE(site);
  CHECK(baseBiome(found->column(site->x, site->z).biome) == Biome::jungle);
  CHECK(site->dir >= 0);
  CHECK(site->dir < 4);
  int moss = 0, cobble = 0, chests = 0;
  const int ccx = site->x >> 4, ccz = site->z >> 4;
  for (int cz = ccz - 2; cz <= ccz + 2; cz++)
    for (int cx = ccx - 2; cx <= ccx + 2; cx++) {
      const auto chunk = found->generate(cx, cz);
      for (const GenTile& t : chunk->genTiles())
        if (t.kind == GenTile::LootChest && t.param == 7) chests++;
      for (int y = 40; y < 120; y++)
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            const int id = stateId(chunk->block(x, y, z));
            moss += id == B::mossy_cobblestone;
            cobble += id == B::cobblestone;
          }
    }
  CHECK(moss > 60);
  CHECK(cobble > 150);
  CHECK(chests >= 3);
}

TEST_CASE("Monumento oceánico: se encuentra en un océano profundo y lleva prismarina, oro, linternas, esponjas y sus guardianes") {
  std::vector<std::unique_ptr<TerrainGenerator>> gens;
  TerrainGenerator* found = nullptr;
  std::optional<TerrainGenerator::TempleSite> site;
  int sites = 0;
  for (u64 seed = 1; seed <= 40; seed++) {
    gens.push_back(std::make_unique<TerrainGenerator>(seed));
    for (int rz = -2; rz <= 2; rz++)
      for (int rx = -2; rx <= 2; rx++)
        if (const auto s = gens.back()->monumentSite(rx, rz)) {
          sites++;
          if (!site) {
            site = s;
            found = gens.back().get();
          }
        }
  }
  CHECK(sites >= 5);  // no es imposible dar con uno
  REQUIRE(site);
  for (int du : {-24, 0, 24})
    for (int dv : {-24, 0, 24}) {
      const ColumnInfo ci = found->column(site->x + du, site->z + dv);
      CHECK((ci.biome == Biome::ocean || ci.biome == Biome::deep_ocean));
      CHECK(ci.height <= 50);
    }
  int prismarine = 0, gold = 0, lanterns = 0, sponges = 0, guardians = 0, elders = 0;
  const int ccx = site->x >> 4, ccz = site->z >> 4;
  for (int cz = ccz - 2; cz <= ccz + 2; cz++)
    for (int cx = ccx - 2; cx <= ccx + 2; cx++) {
      const auto chunk = found->generate(cx, cz);
      for (const GenTile& t : chunk->genTiles())
        if (t.kind == GenTile::Mob && t.param == 68) (t.extra == 1 ? elders : guardians)++;
      for (int y = 20; y < 70; y++)
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            const BlockState s = chunk->block(x, y, z);
            const int id = stateId(s);
            prismarine += id == 168;
            gold += id == B::gold_block;
            lanterns += id == B::sea_lantern;
            sponges += id == B::sponge && stateMeta(s) == 1;
          }
    }
  CHECK(prismarine > 20000);
  CHECK(gold == 8);       // el tesoro: ocho bloques
  CHECK(lanterns > 40);
  CHECK(sponges > 100);
  CHECK(elders == 3);     // anciano arriba y uno a cada lado de la sala central
  CHECK(guardians == 10);
}

#include <queue>
#include "world/world.h"

TEST_CASE("Monumento oceánico: desde la entrada se llega por dentro a la torre, a la sala del tesoro, al anillo, a las salas de esponja y a todos los guardianes") {
  TerrainGenerator* found = nullptr;
  std::optional<TerrainGenerator::TempleSite> site;
  std::vector<std::unique_ptr<TerrainGenerator>> gens;
  for (u64 seed = 1; seed <= 40 && !site; seed++) {
    gens.push_back(std::make_unique<TerrainGenerator>(seed));
    for (int rz = -2; rz <= 2 && !site; rz++)
      for (int rx = -2; rx <= 2 && !site; rx++)
        if ((site = gens.back()->monumentSite(rx, rz))) found = gens.back().get();
  }
  REQUIRE(site);
  World world;
  ChunkSet modified;
  std::vector<glm::ivec3> mobCells;
  const int ccx = site->x >> 4, ccz = site->z >> 4;
  for (int cz = ccz - 2; cz <= ccz + 2; cz++)
    for (int cx = ccx - 2; cx <= ccx + 2; cx++) {
      auto chunk = found->generate(cx, cz);
      for (const GenTile& t : chunk->genTiles())
        if (t.kind == GenTile::Mob && t.param == 68) mobCells.push_back({cx * 16 + t.x, t.y, cz * 16 + t.z});
      world.insert(std::move(chunk), modified);
    }
  const int base = 40;
  auto cell = [&](int u, int v, int y) { return glm::ivec3(site->x + u, base + y, site->z + v); };
  auto inside = [&](const glm::ivec3& c) {
    const int u = c.x - site->x, v = c.z - site->z, y = c.y - base;
    return u >= -28 && u <= 27 && v >= -28 && v <= 27 && y >= 1 && y <= 22;  // (dentro de la estructura; fuera todo es mar)
  };
  auto isWater = [&](const glm::ivec3& c) { return stateId(world.block(c.x, c.y, c.z)) == B::water; };
  std::set<std::tuple<int, int, int>> seenCells;
  std::queue<glm::ivec3> q;
  const glm::ivec3 start = cell(-28, 0, 2);  // justo dentro de la entrada
  REQUIRE(isWater(start));
  q.push(start);
  seenCells.insert({start.x, start.y, start.z});
  while (!q.empty()) {
    const glm::ivec3 c = q.front();
    q.pop();
    for (const glm::ivec3& d : {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)}) {
      const glm::ivec3 n = c + d;
      if (!inside(n) || !isWater(n) || seenCells.count({n.x, n.y, n.z})) continue;
      seenCells.insert({n.x, n.y, n.z});
      q.push(n);
    }
  }
  auto reach = [&](const glm::ivec3& c) { return seenCells.count({c.x, c.y, c.z}) == 1; };
  CHECK(reach(cell(0, 0, 19)));     // la torre
  CHECK(reach(cell(6, 6, 14)));     // el tercer cuerpo (su anillo)
  CHECK(reach(cell(14, 14, 8)));    // el segundo
  CHECK(reach(cell(-7, 0, 2)));     // la sala central, a un lado del tesoro
  CHECK(reach(cell(24, 24, 2)));    // el anillo
  CHECK(reach(cell(-21, -17, 2)));  // la boca de una sala de esponja
  CHECK(reach(cell(21, 17, 2)));    // y la de la otra
  REQUIRE(mobCells.size() == 13);
  for (const glm::ivec3& m : mobCells) {
    CAPTURE(m.x - site->x);
    CAPTURE(m.y - base);
    CAPTURE(m.z - site->z);
    CHECK(reach(m));
  }
  // El oro está dentro de prismarina oscura, no al aire libre
  CHECK(stateId(world.block(site->x - 1, base + 3, site->z - 1)) == B::gold_block);
  CHECK(stateId(world.block(site->x - 2, base + 3, site->z - 1)) == 168);
}
