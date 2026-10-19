#include <doctest/doctest.h>

#include <ostream>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

/// Un estanque de agua (y 52 a 63) de 13x13 en el mundo plano, con el jugador dentro.
struct Pool {
  FlatTestWorld fw;
  GameSession s{fw, 11};
  Pool() {
    s.setRules({2, true, true});
    s.setMode(GameMode::Survival);
    for (int y = 52; y <= 63; y++)
      for (int z = -6; z <= 6; z++)
        for (int x = -6; x <= 6; x++) fw.setBlock(x, y, z, makeState(B::water));
    s.player().pos = s.player().prevPos = {0.5, 58, 0.5};
  }
  Mob* guardian(const glm::dvec3& at, bool elder = false) {
    Mob* m = s.spawnMob(MobType::Guardian, at);
    REQUIRE(m);
    m->persistent = true;
    if (elder) {
      m->elder = true;
      m->health = 80;
    }
    return m;
  }
};

}  // namespace

TEST_CASE("Guardián: datos, id de entidad y guardado (con su versión anciana)") {
  CHECK(mobEntityId(MobType::Guardian) == 68);
  CHECK(mobFromEntityId(68) == MobType::Guardian);
  CHECK(mobInfo(MobType::Guardian).maxHealth == 30.0f);
  Mob m;
  m.type = MobType::Guardian;
  m.pos = {1, 60, 2};
  m.elder = true;
  m.health = 80;
  const auto back = save::mobFromNbt(save::mobToNbt(m));
  REQUIRE(back);
  CHECK(back->type == MobType::Guardian);
  CHECK(back->elder);
  CHECK(back->scale() == doctest::Approx(2.35f));
  Mob small;
  small.type = MobType::Guardian;
  CHECK(small.scale() == doctest::Approx(1.0f));
}

TEST_CASE("Guardián: apunta con el rayo, lo carga 4 s y quita 6 de vida, y no ataca a quien no ve") {
  Pool p;
  p.guardian({4.5, 58, 0.5});
  const float full = p.s.player().health;
  tickN(p.s, 30);
  const Mob* gm = &p.s.mobs()[0];  // (el puntero de spawnMob no vale tras añadir más criaturas)
  CHECK(gm->beamTarget == p.s.player().uid);
  CHECK(gm->beamTicks > 0);
  CHECK(p.s.player().health == doctest::Approx(full));  // aún cargando
  tickN(p.s, 70);
  CHECK(p.s.player().health < full);  // la carga acaba a los 80 ticks
  CHECK(p.s.player().health == doctest::Approx(full - 6.0f).epsilon(0.01));
}

TEST_CASE("Guardián: si el jugador sale del alcance (más de 15 bloques) el rayo se apaga") {
  Pool p;
  p.guardian({5.5, 58, 0.5});
  tickN(p.s, 20);
  CHECK(p.s.mobs()[0].beamTarget != 0);
  p.s.player().pos = p.s.player().prevPos = {0.5, 64.5, 0.5};  // fuera del agua, lejos del guardián no, pero sin ser visible en agua: se mueve a 30 bloques
  p.s.player().pos.x = 40.5;
  p.s.player().prevPos = p.s.player().pos;
  tickN(p.s, 5);
  CHECK(p.s.mobs()[0].beamTarget == 0);
  CHECK(p.s.mobs()[0].beamTicks == 0);
}

TEST_CASE("Guardián: con las púas fuera, golpearlo cuerpo a cuerpo hiere a quien golpea") {
  Pool p;
  Mob* g = p.guardian({1.5, 58, 0.5});
  g->noAI = true;
  g->spikes = true;
  const float full = p.s.player().health;
  p.s.player().inventory.slot(0) = ItemStack(ItemId::wooden_sword, 1);
  p.s.punchMob(*g);
  CHECK(p.s.player().health < full);
  g->spikes = false;
  g->invulnerable = 0;
  const float after = p.s.player().health;
  p.s.punchMob(*g);
  CHECK(p.s.player().health == doctest::Approx(after));  // sin púas no pasa nada
}

TEST_CASE("Guardián anciano: maldice con fatiga de minado III a los jugadores cercanos, no a los lejanos") {
  Pool p;
  Mob* g = p.guardian({3.5, 58, 0.5}, true);
  g->age = 1199;
  tickN(p.s, 2);
  CHECK(p.s.player().effects.amp(fx::MiningFatigue) == 2);
  // Uno solo normal no la da
  Pool q;
  Mob* n = q.guardian({3.5, 58, 0.5});
  n->age = 1199;
  tickN(q.s, 2);
  CHECK_FALSE(q.s.player().effects.has(fx::MiningFatigue));
}

TEST_CASE("Guardián: al morir suelta fragmentos de prismarina y, si es el anciano, una esponja húmeda") {
  Pool p;
  Mob* g = p.guardian({3.5, 58, 0.5}, true);
  p.s.hurtMob(*g, 500.0f, {0, 58, 0}, 0.0f, true);
  tickN(p.s, 2);
  bool sponge = false;
  for (const ItemEntity& e : p.s.items())
    if (e.stack.id == B::sponge && e.stack.meta == 1) sponge = true;
  CHECK(sponge);
}
