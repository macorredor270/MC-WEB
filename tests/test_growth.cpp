#include <doctest/doctest.h>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;

int idAt(FlatTestWorld& f, int x, int y, int z) { return stateId(f.w.block(x, y, z)); }

int countBlocks(FlatTestWorld& f, int id, int x0, int x1, int y0, int y1, int z0, int z1) {
  int n = 0;
  for (int y = y0; y <= y1; y++)
    for (int z = z0; z <= z1; z++)
      for (int x = x0; x <= x1; x++) n += idAt(f, x, y, z) == id;
  return n;
}

}  // namespace

TEST_CASE("Los brotes crecen en árboles de cada tipo (el de roble oscuro y la jungla gigante piden 2x2)") {
  for (int type = 0; type < 6; type++) {
    FlatTestWorld fw;
    GameSession s(fw, 7);
    const bool needs4 = type == 5;
    fw.setBlock(4, kY, 4, makeState(B::sapling, type));
    if (needs4 || type == 3) {
      fw.setBlock(5, kY, 4, makeState(B::sapling, type));
      fw.setBlock(4, kY, 5, makeState(B::sapling, type));
      fw.setBlock(5, kY, 5, makeState(B::sapling, type));
    }
    for (int i = 0; i < 4000 && idAt(fw, 4, kY, 4) == B::sapling; i++) s.growthTickAt({4, kY, 4});
    const int logId = type >= 4 ? B::log2 : B::log, leafId = type >= 4 ? B::leaves2 : B::leaves;
    CAPTURE(type);
    CHECK(idAt(fw, 4, kY, 4) != B::sapling);
    CHECK(countBlocks(fw, logId, 0, 12, kY, kY + 30, 0, 12) >= 4);
    CHECK(countBlocks(fw, leafId, 0, 12, kY, kY + 30, 0, 12) >= 10);
    CHECK(idAt(fw, 4, kY - 1, 4) == B::dirt);  // bajo el tronco la hierba se vuelve tierra
  }
}

TEST_CASE("Un brote de roble oscuro solo no crece") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  fw.setBlock(4, kY, 4, makeState(B::sapling, 5 | 8));
  for (int i = 0; i < 300; i++) s.growthTickAt({4, kY, 4});
  CHECK(idAt(fw, 4, kY, 4) == B::sapling);
}

TEST_CASE("Un brote con algo encima no crece") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  fw.setBlock(4, kY, 4, makeState(B::sapling, 0 | 8));
  fw.setBlock(4, kY + 2, 4, makeState(B::stone));
  for (int i = 0; i < 400; i++) s.growthTickAt({4, kY, 4});
  CHECK(idAt(fw, 4, kY, 4) == B::sapling);
}

TEST_CASE("Las hojas sin tronco cerca se secan; con un tronco a 4 hojas o menos se quedan") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  // Una fila de hojas: 1..5 hojas desde el tronco
  fw.setBlock(2, kY, 2, makeState(B::log, 0));
  for (int i = 1; i <= 6; i++) fw.setBlock(2 + i, kY, 2, makeState(B::leaves, 8));
  for (int i = 1; i <= 6; i++) s.growthTickAt({2 + i, kY, 2});
  for (int i = 1; i <= 4; i++) {
    CAPTURE(i);
    CHECK(idAt(fw, 2 + i, kY, 2) == B::leaves);
  }
  CHECK(idAt(fw, 7, kY, 2) == B::air);  // a 5 hojas del tronco se seca
  // Una hoja pegada a un tronco: tras comprobarla se quita la marca
  fw.setBlock(8, kY, 8, makeState(B::log, 0));
  fw.setBlock(9, kY, 8, makeState(B::leaves, 8));
  s.growthTickAt({9, kY, 8});
  CHECK(stateMeta(fw.w.block(9, kY, 8)) == 0);
  // Las puestas a mano (bit 4) no se secan
  fw.setBlock(10, kY, 10, makeState(B::leaves, 4 | 8));
  s.growthTickAt({10, kY, 10});
  CHECK(idAt(fw, 10, kY, 10) == B::leaves);
}

TEST_CASE("Romper el último tronco marca las hojas de al lado y acaban secándose en cadena") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  fw.setBlock(2, kY, 2, makeState(B::log, 0));
  fw.setBlock(2, kY + 1, 2, makeState(B::leaves, 0));
  fw.setBlock(2, kY + 2, 2, makeState(B::leaves, 0));
  s.breakBlockAt({2, kY, 2}, false);
  CHECK(stateMeta(fw.w.block(2, kY + 1, 2)) == 8);  // la de al lado ya tiene la marca
  s.growthTickAt({2, kY + 1, 2});
  CHECK(idAt(fw, 2, kY + 1, 2) == B::air);
  s.growthTickAt({2, kY + 2, 2});  // la siguiente se marcó al secarse la primera
  CHECK(idAt(fw, 2, kY + 2, 2) == B::air);
}

TEST_CASE("La hierba se extiende a la tierra de al lado y se pierde bajo un bloque") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  for (int x = 3; x <= 5; x++) fw.setBlock(x, kY - 1, 2, makeState(B::dirt));
  fw.setBlock(2, kY - 1, 2, makeState(B::grass));
  for (int i = 0; i < 3000; i++) s.growthTickAt({2, kY - 1, 2});
  CHECK(countBlocks(fw, B::grass, 3, 5, kY - 1, kY - 1, 2, 2) >= 1);
  fw.setBlock(8, kY - 1, 8, makeState(B::grass));
  fw.setBlock(8, kY, 8, makeState(B::stone));
  s.growthTickAt({8, kY - 1, 8});
  CHECK(idAt(fw, 8, kY - 1, 8) == B::dirt);
}

TEST_CASE("El hielo cerca de una antorcha se derrite y da agua") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.placeBlock({6, kY, 6}, makeState(B::ice));
  s.placeBlock({7, kY, 6}, makeState(B::torch, 5));
  s.growthTickAt({6, kY, 6});
  CHECK(isWater(idAt(fw, 6, kY, 6)));
  s.placeBlock({12, kY, 12}, makeState(B::ice));  // lejos de toda luz de bloque: sigue helado
  s.growthTickAt({12, kY, 12});
  CHECK(idAt(fw, 12, kY, 12) == B::ice);
}
