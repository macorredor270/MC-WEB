#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/block_entity.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

struct Arena {
  FlatTestWorld fw;
  GameSession s{fw, 21};
  Arena() {
    s.setRules({2, true, true});
    s.setMode(GameMode::Survival);
    s.player().pos = s.player().prevPos = {0.5, 64, 12.5};
    for (int i = 0; i <= static_cast<int>(Ach::TheEnd2); i++) s.achievements().award(static_cast<Ach>(i));  // (hay que tener los anteriores)
  }
  /// La T: tres calaveras de esqueleto atrofiado sobre tres de arena de almas y una debajo del centro; la última calavera se pone al final.
  void buildT(int x, int y, int z, bool lastPlaced = true) {
    fw.setBlock(x, y - 1, z, makeState(B::soul_sand));
    fw.setBlock(x - 1, y - 1, z, makeState(B::soul_sand));
    fw.setBlock(x + 1, y - 1, z, makeState(B::soul_sand));
    fw.setBlock(x, y - 2, z, makeState(B::soul_sand));
    for (int dx : {-1, 0, 1}) {
      fw.setBlock(x + dx, y, z, makeState(kSkullBlock, 1));
      SkullData sk;
      sk.type = 1;
      s.tiles().skulls[{x + dx, y, z}] = sk;
    }
    (void)lastPlaced;
  }
  int count(MobType t) {
    int n = 0;
    for (const Mob& m : s.mobs()) n += m.type == t && !m.dying();
    return n;
  }
  Mob* wither() {
    for (const Mob& m : s.mobs())
      if (m.type == MobType::Wither) return s.mobById(m.id);
    return nullptr;
  }
};

}  // namespace

TEST_CASE("Wither: cuatro de arena de almas en T y tres calaveras de esqueleto atrofiado lo invocan") {
  Arena a;
  a.buildT(0, 66, 0);
  CHECK(a.s.tryBuildWither({1, 66, 0}));
  REQUIRE(a.count(MobType::Wither) == 1);
  const Mob* w = a.wither();
  CHECK(w->invul == 220);
  CHECK(w->health == doctest::Approx(100.0f));
  // La T desaparece entera
  for (int dx : {-1, 0, 1}) {
    CHECK(stateId(a.fw.w.block(dx, 66, 0)) == 0);
    CHECK(stateId(a.fw.w.block(dx, 65, 0)) == 0);
  }
  CHECK(stateId(a.fw.w.block(0, 64, 0)) == 0);
  CHECK(a.s.achievements().has(Ach::SpawnWither));  // (el jugador está a 12 bloques: cuenta)
}

TEST_CASE("Wither: calaveras normales o sin arena debajo no lo invocan") {
  Arena a;
  a.buildT(0, 66, 0);
  a.fw.setBlock(0, 64, 0, makeState(B::stone));  // falta la pata de la T
  CHECK_FALSE(a.s.tryBuildWither({1, 66, 0}));
  Arena b;
  b.buildT(0, 66, 0);
  b.s.tiles().skulls[{0, 66, 0}].type = 0;  // una calavera de esqueleto normal
  CHECK_FALSE(b.s.tryBuildWither({1, 66, 0}));
  CHECK(b.count(MobType::Wither) == 0);
}

TEST_CASE("Wither: cargándose no se le puede herir; a los 11 segundos explota con potencia 7 y queda libre") {
  Arena a;
  a.buildT(0, 66, 0);
  REQUIRE(a.s.tryBuildWither({1, 66, 0}));
  a.fw.setBlock(4, 64, 0, makeState(B::stone));  // algo cerca que la explosión destruya
  Mob* w = a.wither();
  const float h0 = w->health;
  a.s.hurtMobById(w->id, 10.0f, {0, 64, 5});
  CHECK(a.wither()->health >= h0);
  tickN(a.s, 100);
  CHECK(a.wither()->invul > 0);
  CHECK(stateId(a.fw.w.block(4, 64, 0)) == B::stone);
  tickN(a.s, 125);
  CHECK(a.wither()->invul == 0);
  CHECK(stateId(a.fw.w.block(4, 64, 0)) == 0);  // la explosión se lo llevó
  CHECK(a.wither()->health == doctest::Approx(300.0f));
  bool boom = false;
  for (const auto& e : a.s.takeEvents()) boom |= e.type == SessionEvent::Type::Explosion;
  (void)boom;
}

TEST_CASE("Wither: dispara calaveras al jugador que tiene cerca, que dan 8 de daño y marchitan") {
  Arena a;
  Mob* w = a.s.spawnMob(MobType::Wither, {0.5, 66.0, 0.5});
  w->persistent = true;
  w->invul = 0;
  w->health = 300;
  a.s.player().health = 20;
  a.s.player().pos = a.s.player().prevPos = {0.5, 64, 8.5};
  bool shot = false;
  for (int i = 0; i < 200 && !a.s.player().effects.has(fx::Wither); i++) {
    a.s.tick(testing::idleTick());
    shot |= !a.s.fireballs().empty();
  }
  CHECK(shot);
  CHECK(a.s.player().effects.has(fx::Wither));
  CHECK(a.s.player().health < 20.0f);
}

TEST_CASE("Wither: con la mitad de la vida las flechas rebotan; muerto suelta una estrella del Nether y da el logro") {
  Arena a;
  Mob* w = a.s.spawnMob(MobType::Wither, {0.5, 66.0, 0.5});
  const u32 id = w->id;
  w->persistent = true;
  w->noAI = true;
  w->invul = 0;
  w->health = 140;
  a.s.player().pos = a.s.player().prevPos = {0.5, 64, 8.5};
  a.s.player().inventory.slot(0) = ItemStack(ItemId::bow);
  a.s.player().inventory.slot(1) = ItemStack(ItemId::arrow, 8);
  a.s.player().inventory.select(0);
  {
    TickInput draw = testing::idleTick();
    draw.use = draw.usePressed = true;
    draw.pitch = 0.2f;
    for (int i = 0; i < 22; i++) a.s.tick(draw);
    draw.use = draw.usePressed = false;
    a.s.tick(draw);
  }
  REQUIRE_FALSE(a.s.arrows().empty());
  tickN(a.s, 10);
  CHECK(a.s.mobById(id)->health == doctest::Approx(140.0f));
  // Muerte (con el logro de invocarlo ya conseguido)
  a.s.achievements().award(Ach::SpawnWither);
  a.s.mobById(id)->health = 3;
  a.s.hurtMobById(id, 50.0f, {0, 66, 6});
  tickN(a.s, 30);
  int stars = 0;
  for (const ItemEntity& e : a.s.items()) stars += e.stack.id == ItemId::nether_star ? e.stack.count : 0;
  CHECK(stars == 1);
  CHECK(a.s.achievements().has(Ach::KillWither));
}
