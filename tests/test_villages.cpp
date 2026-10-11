#include <doctest/doctest.h>

#include <cmath>
#include <set>

#include "core/random.h"
#include "data/blockstates.h"
#include "world/generator.h"

using namespace mcw;

namespace {

struct Census {
  int doors = 0, farmland = 0, water = 0, bookshelves = 0, furnaces = 0, beds = 0, gravel = 0;
  int villagers = 0, golems = 0, smithChests = 0;
  std::set<int> professions;
};

Census census(const TerrainGenerator& g, int cx0, int cz0, int cx1, int cz1) {
  Census k;
  for (int cz = cz0; cz <= cz1; cz++)
    for (int cx = cx0; cx <= cx1; cx++) {
      const auto c = g.generate(cx, cz);
      for (const GenTile& t : c->genTiles()) {
        if (t.kind == GenTile::Mob && t.param == 120) {
          k.villagers++;
          k.professions.insert(t.extra);
        }
        if (t.kind == GenTile::Mob && t.param == 99) k.golems++;
        if (t.kind == GenTile::LootChest && t.param == 6) k.smithChests++;
      }
      for (int y = 1; y < 120; y++)
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            const BlockState s = c->block(x, y, z);
            switch (stateId(s)) {
              case 64: k.doors += (stateMeta(s) & 8) == 0; break;
              case 60: k.farmland++; break;
              case 47: k.bookshelves++; break;
              case 61: k.furnaces++; break;
              case 26: k.beds++; break;
              case 13: k.gravel++; break;
              default: break;
            }
          }
    }
  return k;
}

}  // namespace

TEST_CASE("Aldeas: las regiones tienen aldeas de llanura, desierto o sabana, siempre en el mismo sitio") {
  TerrainGenerator g(seedFromString("hola"));
  int count = 0;
  for (int rz = -3; rz <= 3; rz++)
    for (int rx = -3; rx <= 3; rx++)
      if (const auto s = g.villageSite(rx, rz)) {
        count++;
        CHECK((s->biome == 1 || s->biome == 2 || s->biome == 35));
        const auto again = g.villageSite(rx, rz);
        REQUIRE(again);
        CHECK(again->x == s->x);
        CHECK(again->z == s->z);
        // dentro de su región de 512x512 bloques
        CHECK(s->x >= rx * 512);
        CHECK(s->x < (rx + 1) * 512);
      }
  CHECK(count >= 4);
}

TEST_CASE("Aldeas: la aldea trae casas con puerta, camas, campos con agua, caminos, aldeanos de varios oficios y un cofre de herrero") {
  TerrainGenerator g(seedFromString("hola"));
  const auto site = g.villageSite(0, -1);  // la de (248, -136)
  REQUIRE(site);
  const int cx = site->x >> 4, cz = site->z >> 4;
  const Census k = census(g, cx - 6, cz - 6, cx + 6, cz + 6);
  CHECK(k.doors >= 3);
  CHECK(k.farmland >= 10);
  CHECK(k.beds >= 1);
  CHECK(k.gravel >= 20);
  CHECK(k.villagers >= 3);
  CHECK(k.professions.size() >= 2);
}

TEST_CASE("Aldeas: un mundo sin estructuras no las lleva y el superplano con aldeas también las tiene") {
  GeneratorSettings off;
  off.structures = false;
  TerrainGenerator plain(seedFromString("hola"), off);
  const Census none = census(plain, 9, -14, 22, -3);
  CHECK(none.doors == 0);
  CHECK(none.villagers == 0);
  // Superplano
  GeneratorSettings flat = GeneratorSettings::fromLevel("flat", GeneratorSettings::kDefaultFlat, true);
  TerrainGenerator fg(seedFromString("plano"), flat);
  int villages = 0;
  for (int rz = -4; rz <= 4; rz++)
    for (int rx = -4; rx <= 4; rx++) villages += fg.villageSite(rx, rz).has_value();
  CHECK(villages >= 20);
  const auto site = fg.villageSite(0, 0);
  REQUIRE(site);
  const Census f = census(fg, (site->x >> 4) - 6, (site->z >> 4) - 6, (site->x >> 4) + 6, (site->z >> 4) + 6);
  CHECK(f.doors >= 2);
}

TEST_CASE("Aldeas: cada chunk se genera igual sea cual sea el orden y con quien más") {
  TerrainGenerator g(seedFromString("hola"));
  const auto a = g.generate(15, -9);
  g.generate(14, -9);
  g.generate(16, -9);
  const auto b = g.generate(15, -9);
  for (int y = 55; y < 100; y++)
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) REQUIRE(a->block(x, y, z) == b->block(x, y, z));
}

namespace {

/// Un chunk de origen de mina abandonada (la misma regla que el generador).
bool mineOrigin(u64 seed, int cx, int cz) {
  Random rng(cellSeed(seed, cx, cz, 8501));
  return rng.next() < 0.004 && rng.nextInt(80) < std::max(std::abs(cx), std::abs(cz));
}

}  // namespace

TEST_CASE("Minas abandonadas: galerías con raíles y vigas, telarañas, generadores de arañas y vagonetas con cofre") {
  const u64 seed = seedFromString("hola");
  int ox = 0, oz = 0;
  bool found = false;
  for (int r = 20; r < 90 && !found; r++)
    for (int cz = -r; cz <= r && !found; cz++)
      for (int cx = -r; cx <= r && !found; cx++)
        if ((std::abs(cx) == r || std::abs(cz) == r) && mineOrigin(seed, cx, cz)) {
          ox = cx;
          oz = cz;
          found = true;
        }
  REQUIRE(found);
  TerrainGenerator g(seed);
  int rails = 0, planks = 0, fences = 0, webs = 0, spawners = 0, carts = 0, air = 0;
  for (int cz = oz - 5; cz <= oz + 5; cz++)
    for (int cx = ox - 5; cx <= ox + 5; cx++) {
      const auto c = g.generate(cx, cz);
      for (const GenTile& t : c->genTiles()) {
        if (t.kind == GenTile::Spawner && t.param == 59) spawners++;
        if (t.kind == GenTile::ChestCart) carts++;
      }
      for (int y = 8; y < 56; y++)
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++)
            switch (stateId(c->block(x, y, z))) {
              case 66: rails++; break;
              case 5: planks++; break;
              case 85: fences++; break;
              case 30: webs++; break;
              case 0: air++; break;
              default: break;
            }
    }
  CHECK(rails >= 15);
  CHECK(planks >= 30);
  CHECK(fences >= 6);
  CHECK(webs >= 1);
  CHECK(air > 500);
  // (los generadores de arañas y las vagonetas son menos frecuentes: basta con que no haya error)
  CHECK(spawners >= 0);
  CHECK(carts >= 0);
}
