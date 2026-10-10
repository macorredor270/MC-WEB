#include <doctest/doctest.h>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;  // el suelo es y=63

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

int idAt(FlatTestWorld& f, int x, int y, int z) { return stateId(f.w.block(x, y, z)); }
int metaAt(FlatTestWorld& f, int x, int y, int z) { return stateMeta(f.w.block(x, y, z)); }

/// Con el cubo en la mano mirando hacia abajo al bloque (x, 63, z) desde arriba.
void lookDownAt(GameSession& s, int x, int z) {
  s.player().pos = s.player().prevPos = {x + 0.5, kY, z + 0.5};
  s.player().pitch = -1.5707f;
}

void useBucketAt(GameSession& s, int x, int z) {
  lookDownAt(s, x, z);
  TickInput in = testing::idleTick();
  in.pitch = -1.5707f;
  in.usePressed = in.use = true;
  s.tick(in);
}

}  // namespace

TEST_CASE("El agua de un cubo se extiende siete bloques por un suelo plano, cada vez más baja") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.player().inventory.slot(0) = ItemStack(ItemId::water_bucket);
  s.player().inventory.select(0);
  lookDownAt(s, 0, 0);
  CHECK(s.useBucket());
  CHECK(idAt(fw, 0, kY, 0) == B::flowing_water);
  CHECK(s.player().inventory.selected().id == ItemId::bucket);  // el cubo se queda vacío
  tickN(s, 300);
  CHECK(idAt(fw, 0, kY, 0) == B::water);  // en reposo es el bloque estático
  CHECK(metaAt(fw, 0, kY, 0) == 0);
  for (int d = 1; d <= 7; d++) {
    CHECK(isWater(idAt(fw, d, kY, 0)));
    CHECK(metaAt(fw, d, kY, 0) == d);
    CHECK(isWater(idAt(fw, -d, kY, 0)));
    CHECK(isWater(idAt(fw, 0, kY, d)));
  }
  CHECK(idAt(fw, 8, kY, 0) == B::air);
  CHECK(idAt(fw, 5, kY, 5) == B::air);  // en diagonal no llega tan lejos (7 pasos en total)
}

TEST_CASE("El agua cae por un hueco y se desborda al llegar abajo; sin hueco no baja") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  for (int y = kY - 3; y < kY; y++) fw.setBlock(1, y, 0, 0);  // un pozo de 3 de hondo junto a la fuente
  s.placeBlock({0, kY, 0}, makeState(B::flowing_water, 0));
  tickN(s, 300);
  CHECK(isWater(idAt(fw, 1, kY, 0)));
  CHECK(isWater(idAt(fw, 1, kY - 1, 0)));
  CHECK(isWater(idAt(fw, 1, kY - 2, 0)));
  CHECK(metaAt(fw, 1, kY - 1, 0) >= 8);  // lo que cae lleva el bit de caída
  CHECK(isWater(idAt(fw, 1, kY - 3, 0)));
}

TEST_CASE("Dos fuentes con un hueco entre ellas lo llenan: agua infinita") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(B::water, 0));
  s.placeBlock({2, kY, 0}, makeState(B::water, 0));
  tickN(s, 100);
  CHECK(isWater(idAt(fw, 1, kY, 0)));
  CHECK(metaAt(fw, 1, kY, 0) == 0);
}

TEST_CASE("Quitar la fuente seca el agua que fluía de ella") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(B::flowing_water, 0));
  tickN(s, 200);
  REQUIRE(isWater(idAt(fw, 3, kY, 0)));
  s.placeBlock({0, kY, 0}, 0);
  tickN(s, 400);
  CHECK(idAt(fw, 1, kY, 0) == B::air);
  CHECK(idAt(fw, 3, kY, 0) == B::air);
}

TEST_CASE("Lava y agua: la fuente de lava se vuelve obsidiana, la que fluye adoquín y la que cae sobre agua piedra") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(B::lava, 0));
  s.placeBlock({1, kY, 0}, makeState(B::water, 0));
  tickN(s, 5);
  CHECK(idAt(fw, 0, kY, 0) == B::obsidian);

  s.placeBlock({5, kY, 0}, makeState(B::flowing_lava, 3));
  s.placeBlock({6, kY, 0}, makeState(B::water, 0));
  tickN(s, 5);
  CHECK(idAt(fw, 5, kY, 0) == B::cobblestone);

  // Lava que cae sobre agua: piedra
  fw.setBlock(10, kY - 1, 0, makeState(B::water, 0));
  s.placeBlock({10, kY, 0}, makeState(B::flowing_lava, 0));
  tickN(s, 100);
  CHECK(idAt(fw, 10, kY - 1, 0) == B::stone);
}

TEST_CASE("La lava es más lenta y llega menos lejos que el agua (4 bloques)") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(B::flowing_lava, 0));
  tickN(s, 40);  // el agua ya habría llegado a siete bloques; la lava va a 30 ticks por paso (o más)
  CHECK(idAt(fw, 2, kY, 0) == B::air);
  tickN(s, 1500);
  CHECK(isLava(idAt(fw, 3, kY, 0)));
  CHECK(metaAt(fw, 3, kY, 0) == 6);
  CHECK(idAt(fw, 4, kY, 0) == B::air);  // 0, 2, 4, 6: cuatro bloques de lava fluyendo
}

TEST_CASE("Cubo vacío: recoge una fuente (y deja el cubo lleno), pero no el agua que fluye; en creativo no se gasta") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(B::water, 0));
  s.placeBlock({1, kY, 0}, makeState(B::water, 3));
  s.player().inventory.slot(0) = ItemStack(ItemId::bucket);
  s.player().inventory.select(0);
  lookDownAt(s, 1, 0);
  CHECK(!s.useBucket());  // un tramo que fluye no se recoge
  lookDownAt(s, 0, 0);
  CHECK(s.useBucket());
  CHECK(s.player().inventory.selected().id == ItemId::water_bucket);
  CHECK(idAt(fw, 0, kY, 0) == B::air);

  s.setMode(GameMode::Creative);
  s.placeBlock({1, kY, 0}, 0);
  tickN(s, 300);
  s.placeBlock({0, kY, 0}, makeState(B::lava, 0));
  s.player().inventory.slot(0) = ItemStack(ItemId::bucket);
  lookDownAt(s, 0, 0);
  CHECK(s.useBucket());
  CHECK(s.player().inventory.selected().id == ItemId::bucket);  // en creativo el cubo no se gasta
  CHECK(idAt(fw, 0, kY, 0) == B::air);
}

TEST_CASE("Un cubo de agua no se vuelca en la lava ni sobre un bloque sólido") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.player().inventory.slot(0) = ItemStack(ItemId::water_bucket);
  s.player().inventory.select(0);
  s.placeBlock({0, kY, 0}, makeState(B::stone));
  lookDownAt(s, 0, 0);  // mira al bloque de piedra que acabamos de poner debajo de la vista: se vuelca encima
  s.player().pos = s.player().prevPos = {0.5, kY + 3, 0.5};
  CHECK(s.useBucket());
  CHECK(isWater(idAt(fw, 0, kY + 1, 0)));
}
