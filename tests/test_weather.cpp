#include <doctest/doctest.h>

#include <cmath>

#include "data/biomes.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"
#include "save/world_save.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void setBiomeAll(FlatTestWorld& f, int biome) {
  for (int cz = -1; cz <= 1; cz++)
    for (int cx = -1; cx <= 1; cx++)
      if (Chunk* c = f.w.chunk(cx, cz))
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) c->setBiome(x, z, biome);
}

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

}  // namespace

TEST_CASE("Tiempo: la lluvia sube y baja poco a poco y se puede fijar") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  CHECK_FALSE(s.weather().raining);
  s.setWeather(true, false, 200);
  tickN(s, 20);
  CHECK(s.weather().raining);
  CHECK(s.weather().rain == doctest::Approx(0.2f).epsilon(0.1));
  tickN(s, 100);
  CHECK(s.weather().rain == doctest::Approx(1.0f));
  CHECK(s.weather().thunder == doctest::Approx(0.0f));
  // Se acaba cuando se cumple el tiempo (y el siguiente cambio ya es al azar)
  tickN(s, 200);
  CHECK_FALSE(s.weather().raining);
  tickN(s, 120);
  CHECK(s.weather().rain == doctest::Approx(0.0f));
  // Tormenta
  s.setWeather(true, true, 4000);
  tickN(s, 120);
  CHECK(s.weather().thunder == doctest::Approx(1.0f));
}

TEST_CASE("Tiempo: llueve o nieva según el bioma y no bajo techo; el desierto no tiene lluvia") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setWeather(true, false, 4000);
  tickN(s, 120);
  CHECK(s.precipitationAt(2, 70, 2) == 1);  // llanura
  setBiomeAll(fw, Biome::ice_plains);
  CHECK(s.precipitationAt(2, 70, 2) == 2);  // nieve
  setBiomeAll(fw, Biome::desert);
  CHECK(s.precipitationAt(2, 70, 2) == 0);
  setBiomeAll(fw, Biome::plains);
  // Con un techo encima no cae nada
  fw.setBlock(2, 70, 2, makeState(B::stone));
  s.tick(testing::idleTick());
  CHECK(s.precipitationAt(2, 66, 2) == 0);
  CHECK(s.precipitationAt(5, 66, 5) == 1);
}

TEST_CASE("Tiempo: la lluvia apaga el fuego al descubierto y llena los calderos") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.player().pos = s.player().prevPos = {8.5, 64, 8.5};
  for (int x = 0; x < 16; x++)
    for (int z = 0; z < 16; z++)
      if ((x + z) % 2 == 0) fw.setBlock(x, 64, z, makeState(118, 0));  // calderos
  s.tick(testing::idleTick());
  s.setWeather(true, false, 20000);
  tickN(s, 120);
  s.strikeLightning(-5.5, -5.5);  // un rayo al descubierto: prende fuego
  CHECK(stateId(fw.w.block(-6, 64, -6)) == 51);
  tickN(s, 100);
  CHECK(stateId(fw.w.block(-6, 64, -6)) != 51);  // y la lluvia lo apaga
  tickN(s, 4000);
  int filled = 0;
  for (int x = 0; x < 16; x++)
    for (int z = 0; z < 16; z++)
      if (stateId(fw.w.block(x, 64, z)) == 118 && stateMeta(fw.w.block(x, 64, z)) > 0) filled++;
  CHECK(filled > 0);
}

TEST_CASE("Tiempo: en un bioma frío se hielan las aguas quietas y la nieve cubre el suelo") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  setBiomeAll(fw, Biome::ice_plains);
  fw.setBlock(4, 63, 4, makeState(B::water, 0));  // un hueco de agua quieta a nivel del suelo
  s.tick(testing::idleTick());
  s.setWeather(true, false, 40000);
  int snow = 0;
  for (int i = 0; i < 4000; i++) s.tick(testing::idleTick());
  for (int x = -16; x < 32; x++)
    for (int z = -16; z < 32; z++)
      if (stateId(fw.w.block(x, 64, z)) == B::snow_layer) snow++;
  CHECK(snow > 50);
  CHECK(stateId(fw.w.block(4, 63, 4)) == B::ice);
}

TEST_CASE("Tiempo: un rayo prende fuego y hiere, y el cerdo que toca se vuelve cerdo zombi") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.player().pos = s.player().prevPos = {12.5, 64, 12.5};
  Mob* pig = s.spawnMob(MobType::Pig, {2.5, 64, 2.5});
  REQUIRE(pig);
  pig->noAI = true;
  s.spawnMob(MobType::Cow, {3.5, 64, 2.5});
  s.tick(testing::idleTick());
  s.strikeLightning(2.5, 2.5);
  bool seen = false;
  for (const SessionEvent& e : s.takeEvents()) seen |= e.type == SessionEvent::Type::Lightning;
  CHECK(seen);
  int pigs = 0, zombies = 0;
  for (const Mob& m : s.mobs()) {
    pigs += m.type == MobType::Pig && !m.dying();
    zombies += m.type == MobType::PigZombie;
  }
  CHECK(pigs == 0);
  CHECK(zombies == 1);
  CHECK(s.takeLightning().size() == 1);
}

TEST_CASE("Tiempo: dormir despeja la tormenta y el estado se guarda en level.dat") {
  LevelInfo info;
  info.raining = true;
  info.thundering = true;
  info.rainTime = 1234;
  info.thunderTime = 567;
  const auto nbt = levelToNbt(info);
  const LevelInfo back = levelFromNbt(nbt);
  CHECK(back.raining);
  CHECK(back.thundering);
  CHECK(back.rainTime == 1234);
  CHECK(back.thunderTime == 567);
}
