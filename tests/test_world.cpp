#include <doctest/doctest.h>

#include "world/generator.h"
#include "world/light.h"
#include "world/world.h"

#include <set>

using namespace mcw;

namespace {
// Mundo plano: piedra hasta y=63 (altura 64)
std::unique_ptr<Chunk> flatChunk(int cx, int cz) {
  auto c = std::make_unique<Chunk>(cx, cz);
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 64; y++) c->setBlock(x, y, z, makeState(B::stone));
  light::computeInitial(*c);
  return c;
}
}  // namespace

TEST_CASE("Luz inicial en terreno plano") {
  auto c = flatChunk(0, 0);
  CHECK(c->height(5, 5) == 64);
  CHECK(c->skyLight(5, 64, 5) == 15);
  CHECK(c->skyLight(5, 200, 5) == 15);
  CHECK(c->skyLight(5, 63, 5) == 0);
  CHECK(c->blockLight(5, 64, 5) == 0);
}

TEST_CASE("La luz de cielo entra bajo un techo y se atenúa") {
  World w;
  ChunkSet mod;
  for (int cz = -1; cz <= 1; cz++)
    for (int cx = -1; cx <= 1; cx++) w.insert(flatChunk(cx, cz), mod);
  // Techo de 5x5 a y=68 sobre (0..4, 0..4): debajo, en el centro, la luz llega por los lados.
  for (int z = 0; z < 5; z++)
    for (int x = 0; x < 5; x++) w.setBlock(x, 68, z, makeState(B::stone), mod);
  CHECK(w.skyLight(2, 67, 2) < 15);
  CHECK(w.skyLight(2, 67, 2) >= 12);
  CHECK(w.skyLight(2, 69, 2) == 15);
  CHECK(w.skyLight(-1, 64, 2) == 15);
  // Quitar el techo devuelve el 15
  for (int z = 0; z < 5; z++)
    for (int x = 0; x < 5; x++) w.setBlock(x, 68, z, 0, mod);
  CHECK(w.skyLight(2, 67, 2) == 15);
}

TEST_CASE("Antorcha: luz de bloque que baja 1 por bloque y se apaga al quitarla") {
  World w;
  ChunkSet mod;
  w.insert(flatChunk(0, 0), mod);
  w.insert(flatChunk(1, 0), mod);
  w.setBlock(15, 64, 8, makeState(B::torch, 5), mod);
  CHECK(w.blockLight(15, 64, 8) == 14);
  CHECK(w.blockLight(16, 64, 8) == 13);  // cruza al chunk vecino
  CHECK(w.blockLight(20, 64, 8) == 9);
  CHECK(mod.count({1, 0}) == 1);
  w.setBlock(15, 64, 8, 0, mod);
  CHECK(w.blockLight(15, 64, 8) == 0);
  CHECK(w.blockLight(20, 64, 8) == 0);
}

TEST_CASE("Coser luz entre chunks: una cueva abierta al lado de un chunk nuevo") {
  World w;
  ChunkSet mod;
  auto a = flatChunk(0, 0);
  // Hueco en el borde este de A que da a un pozo abierto en B
  for (int y = 60; y < 64; y++) a->setBlock(15, y, 8, 0);
  light::computeInitial(*a);
  auto b = flatChunk(1, 0);
  for (int y = 60; y < 64; y++) b->setBlock(0, y, 8, 0);
  light::computeInitial(*b);
  w.insert(std::move(a), mod);
  w.insert(std::move(b), mod);
  CHECK(w.skyLight(15, 60, 8) == 15);
  CHECK(w.skyLight(16, 60, 8) == 15);
}

TEST_CASE("El generador es determinista y los árboles cruzan bordes igual") {
  TerrainGenerator g1(12345), g2(12345), g3(999);
  auto a = g1.generate(3, -2);
  auto b = g2.generate(3, -2);
  auto c = g3.generate(3, -2);
  int diff = 0, diffOther = 0;
  for (int y = 0; y < 256; y++)
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) {
        diff += a->block(x, y, z) != b->block(x, y, z);
        diffOther += a->block(x, y, z) != c->block(x, y, z);
      }
  CHECK(diff == 0);
  CHECK(diffOther > 0);
  // Bedrock en y=0 y el agua no pasa del nivel del mar
  CHECK(stateId(a->block(0, 0, 0)) == B::bedrock);
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) CHECK(!isWater(stateId(a->block(x, TerrainGenerator::kSeaLevel, z))));
}

TEST_CASE("El generador produce varios biomas y árboles") {
  TerrainGenerator g(42);
  std::set<int> biomes;
  int logs = 0;
  for (int cz = -8; cz <= 8; cz += 4)
    for (int cx = -8; cx <= 8; cx += 4) {
      auto c = g.generate(cx * 8, cz * 8);
      for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
          biomes.insert(c->biome(x, z));
          for (int y = 60; y < 120; y++) logs += stateId(c->block(x, y, z)) == B::log;
        }
    }
  CHECK(biomes.size() >= 4);
  CHECK(logs > 0);
}
