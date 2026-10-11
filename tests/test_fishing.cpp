#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/enchantments.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

int category(const ItemStack& s) {
  switch (s.id) {
    case ItemId::fish: return 0;
    case ItemId::bow: case ItemId::enchanted_book: case ItemId::name_tag: case ItemId::saddle: return 2;
    default: return s.id == ItemId::fishing_rod && s.hasEnchants() ? 2 : 1;
  }
}

struct Pond {
  FlatTestWorld fw;
  GameSession s{fw, 5};
  Pond() {
    // Un estanque de 7x7 y 3 de hondo delante del jugador
    for (int x = -3; x <= 3; x++)
      for (int z = -9; z <= -3; z++)
        for (int y = 61; y <= 63; y++) fw.setBlock(x, y, z, y == 63 ? makeState(B::water, 0) : makeState(B::water, 0));
    s.setMode(GameMode::Survival);
    s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
    s.player().inventory.selected() = ItemStack(ItemId::fishing_rod);
  }
  void use() {
    TickInput in = testing::idleTick();
    in.use = in.usePressed = true;
    in.pitch = -0.2f;  // un poco hacia abajo
    s.tick(in);
  }
};

}  // namespace

TEST_CASE("Botín de la pesca: 85 % de peces, 10 % de basura y 5 % de tesoros, y la Suerte marina sube los tesoros") {
  Random rng(7);
  int counts[3] = {0, 0, 0}, luckyTreasure = 0;
  const int n = 20000;
  for (int i = 0; i < n; i++) counts[category(fishingLoot(rng, 0, 0))]++;
  for (int i = 0; i < n; i++)
    if (category(fishingLoot(rng, 3, 0)) == 2) luckyTreasure++;
  CHECK(counts[0] > n * 0.83);
  CHECK(counts[0] < n * 0.87);
  CHECK(counts[1] > n * 0.085);
  CHECK(counts[1] < n * 0.115);
  CHECK(counts[2] > n * 0.035);
  CHECK(counts[2] < n * 0.065);
  CHECK(luckyTreasure > counts[2] * 1.5);  // (de 5 % a 8 %)
}

TEST_CASE("Botín de la pesca: los peces son de los cuatro tipos y los tesoros salen encantados") {
  Random rng(3);
  bool seen[4] = {false, false, false, false};
  bool enchantedBow = false, enchantedBook = false;
  for (int i = 0; i < 40000; i++) {
    const ItemStack s = fishingLoot(rng, 0, 0);
    if (s.id == ItemId::fish) seen[s.meta & 3] = true;
    if (s.id == ItemId::bow) enchantedBow |= s.hasEnchants();
    if (s.id == ItemId::enchanted_book) enchantedBook = true;
  }
  for (bool b : seen) CHECK(b);
  CHECK(enchantedBow);
  CHECK(enchantedBook);
}

TEST_CASE("Cebo acorta la espera: de 100 a 600 ticks y 100 menos por nivel") {
  Random rng(1);
  int lo = 100000, hi = 0, loLure = 100000, hiLure = 0;
  for (int i = 0; i < 5000; i++) {
    const int a = fishingWaitTicks(rng, 0), b = fishingWaitTicks(rng, 3);
    lo = std::min(lo, a);
    hi = std::max(hi, a);
    loLure = std::min(loLure, b);
    hiLure = std::max(hiLure, b);
  }
  CHECK(lo >= 100);
  CHECK(hi <= 600);
  CHECK(loLure == 0);
  CHECK(hiLure <= 300);
}

TEST_CASE("Caña: el anzuelo cae al agua, flota y se recoge sin nada si no ha picado") {
  Pond p;
  p.use();
  REQUIRE(p.s.hooks().size() == 1);
  tickN(p.s, 40);
  REQUIRE(p.s.hooks().size() == 1);
  CHECK(p.s.hooks()[0].state == FishingHook::State::Bobbing);
  const std::size_t before = p.s.items().size();
  p.use();  // recoger
  CHECK(p.s.hooks().empty());
  CHECK(p.s.items().size() == before);
}

TEST_CASE("Caña: cuando pica y se recoge a tiempo sale una pieza, experiencia y desgaste") {
  Pond p;
  p.use();
  bool bit = false;
  for (int i = 0; i < 1500 && !bit; i++) {
    p.s.tick(testing::idleTick());
    if (!p.s.hooks().empty() && p.s.hooks()[0].nibble > 0) bit = true;
  }
  REQUIRE(bit);
  const std::size_t items = p.s.items().size(), orbs = p.s.orbs().size();
  p.use();
  CHECK(p.s.hooks().empty());
  CHECK(p.s.items().size() == items + 1);
  CHECK(p.s.orbs().size() > orbs);
  CHECK(p.s.player().inventory.selected().meta == 1);
}

TEST_CASE("Caña: si se deja pasar el picotazo, el pez se escapa y el anzuelo sigue esperando") {
  Pond p;
  p.use();
  bool bit = false;
  for (int i = 0; i < 1500 && !bit; i++) {
    p.s.tick(testing::idleTick());
    bit = !p.s.hooks().empty() && p.s.hooks()[0].nibble > 0;
  }
  REQUIRE(bit);
  tickN(p.s, 45);
  REQUIRE(p.s.hooks().size() == 1);
  CHECK(p.s.hooks()[0].nibble == 0);
  CHECK(p.s.hooks()[0].state == FishingHook::State::Bobbing);
}

TEST_CASE("Caña: guardar la caña o alejarse suelta el anzuelo; en tierra firme se queda en el suelo") {
  Pond p;
  p.use();
  tickN(p.s, 20);
  REQUIRE(p.s.hooks().size() == 1);
  p.s.player().inventory.selected() = ItemStack(ItemId::stick);
  tickN(p.s, 2);
  CHECK(p.s.hooks().empty());
  // Hacia atrás, sin agua
  p.s.player().inventory.selected() = ItemStack(ItemId::fishing_rod);
  TickInput in = testing::idleTick();
  in.use = in.usePressed = true;
  in.yaw = 3.14159f;
  in.pitch = -0.3f;
  p.s.tick(in);
  tickN(p.s, 40);
  REQUIRE(p.s.hooks().size() == 1);
  CHECK(p.s.hooks()[0].state == FishingHook::State::Ground);
  CHECK(p.s.hooks()[0].life >= 1);
  const int wear = p.s.player().inventory.selected().meta;
  p.s.tick(in);  // recoger
  CHECK(p.s.hooks().empty());
  CHECK(p.s.player().inventory.selected().meta == wear + 2);
}

TEST_CASE("Caña: un anzuelo que da a una criatura se engancha y recoger la arrastra hacia el jugador") {
  Pond p;
  Mob* m = p.s.spawnMob(MobType::Cow, {0.5, 64.0, -2.5});
  REQUIRE(m);
  m->noAI = true;
  const u32 id = m->id;
  bool hooked = false;
  for (int k = 0; k <= 10 && !hooked; k++) {
    const double slope = -0.05 * k;
    TickInput in = testing::idleTick();
    in.use = in.usePressed = true;
    in.pitch = static_cast<float>(std::atan(slope));
    p.s.tick(in);
    for (int i = 0; i < 6 && !hooked; i++) {
      p.s.tick(testing::idleTick());
      hooked = !p.s.hooks().empty() && p.s.hooks()[0].state == FishingHook::State::Hooked;
    }
    if (!hooked && !p.s.hooks().empty()) {  // recoger y probar con otra inclinación
      p.s.tick(in);
      tickN(p.s, 6);
    }
  }
  REQUIRE(hooked);
  CHECK(p.s.hooks()[0].hookedMob == id);
  tickN(p.s, 15);  // (que se pare el empujón del golpe)
  double zBefore = 0;
  for (const Mob& x : p.s.mobs())
    if (x.id == id) zBefore = x.pos.z;
  TickInput in = testing::idleTick();
  in.use = in.usePressed = true;
  p.s.tick(in);
  CHECK(p.s.hooks().empty());
  tickN(p.s, 10);
  const Mob* after = nullptr;
  for (const Mob& x : p.s.mobs())
    if (x.id == id) after = &x;
  REQUIRE(after);
  CHECK(after->pos.z > zBefore + 0.3);  // hacia el jugador (que está en +Z)
  CHECK(p.s.player().inventory.selected().meta == 3);
}
