#include <doctest/doctest.h>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}
int idAt(FlatTestWorld& f, int x, int y, int z) { return stateId(f.w.block(x, y, z)); }

void useOn(GameSession& s, const glm::ivec3& block, int face, const ItemStack& item) {
  s.player().inventory.slot(0) = item;
  s.player().inventory.select(0);
  RayHit hit;
  hit.block = block;
  hit.face = face;
  hit.point = glm::dvec3(block) + glm::dvec3(0.5);
  s.useHeldOnBlock(hit);
}

}  // namespace

TEST_CASE("El mechero enciende fuego encima de un bloque y se desgasta; sin nada que arda, el fuego se apaga solo") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.player().pos = s.player().prevPos = {8.5, kY, 8.5};
  useOn(s, {2, kY - 1, 2}, Face::Up, ItemStack(ItemId::flint_and_steel));
  CHECK(idAt(fw, 2, kY, 2) == 51);
  CHECK(s.player().inventory.slot(0).meta == 1);  // desgaste de 1 uso
  tickN(s, 600);
  CHECK(idAt(fw, 2, kY, 2) == B::air);  // sobre hierba no hay nada que arda: se apaga
}

TEST_CASE("El fuego se extiende a la madera de al lado y la quema") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.player().pos = s.player().prevPos = {12.5, kY, 12.5};
  for (int x = 3; x <= 5; x++)
    for (int y = kY; y <= kY + 2; y++) fw.setBlock(x, y, 3, makeState(B::planks));
  useOn(s, {2, kY - 1, 3}, Face::Up, ItemStack(ItemId::flint_and_steel));
  REQUIRE(idAt(fw, 2, kY, 3) == 51);
  tickN(s, 3000);
  int planks = 0;
  for (int x = 3; x <= 5; x++)
    for (int y = kY; y <= kY + 2; y++) planks += idAt(fw, x, y, 3) == B::planks;
  CHECK(planks < 9);  // algo se ha quemado
}

TEST_CASE("El fuego sobre roca del Nether no se apaga") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  fw.setBlock(2, kY - 1, 2, makeState(B::netherrack));
  s.player().pos = s.player().prevPos = {12.5, kY, 12.5};
  useOn(s, {2, kY - 1, 2}, Face::Up, ItemStack(ItemId::flint_and_steel));
  tickN(s, 3000);
  CHECK(idAt(fw, 2, kY, 2) == 51);
}

TEST_CASE("El mechero enciende la dinamita, que explota a los 4 segundos") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.player().pos = s.player().prevPos = {12.5, kY, 12.5};
  s.placeBlock({3, kY, 3}, makeState(B::tnt));
  s.player().inventory.slot(0) = ItemStack(ItemId::flint_and_steel);
  s.player().inventory.select(0);
  RayHit hit;
  hit.block = {3, kY, 3};
  hit.face = Face::Up;
  hit.point = {3.5, kY + 1.0, 3.5};
  s.useHeldOnBlock(hit);
  CHECK(idAt(fw, 3, kY, 3) == B::air);
  bool exploded = false;
  for (int i = 0; i < 120 && !exploded; i++) {
    s.tick(testing::idleTick());
    for (const SessionEvent& e : s.takeEvents()) exploded |= e.type == SessionEvent::Type::Explosion;
  }
  CHECK(exploded);
}

TEST_CASE("La lava prende la madera que tiene cerca encima") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.player().pos = s.player().prevPos = {12.5, kY, 12.5};
  s.placeBlock({3, kY, 3}, makeState(B::lava, 0));
  fw.setBlock(4, kY + 1, 3, makeState(B::planks));
  tickN(s, 400);  // la lava se asienta (pasa a su forma en reposo)
  REQUIRE(idAt(fw, 3, kY, 3) == B::lava);
  bool fire = false;
  for (int i = 0; i < 4000 && !fire; i++) {
    s.growthTickAt({3, kY, 3});
    for (int dy = 1; dy <= 2; dy++)
      for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) fire |= idAt(fw, 3 + dx, kY + dy, 3 + dz) == 51;
  }
  CHECK(fire);
}

TEST_CASE("Un fuego en el aire sin nada que lo sujete desaparece al avisarle los vecinos") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.placeBlock({5, kY + 3, 5}, makeState(51, 0));
  CHECK(idAt(fw, 5, kY + 3, 5) == B::air);
}
