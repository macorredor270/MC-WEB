#include <doctest/doctest.h>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/effects.h"
#include "game/session.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

}  // namespace

TEST_CASE("Las pociones se elaboran como en 1.8: agua + verruga = rara, + azúcar = velocidad, + redstone = más larga, + piedra luminosa = nivel II") {
  using namespace potion;
  CHECK(brew(0, ItemId::nether_wart) == kAwkward);
  const int swift = brew(kAwkward, ItemId::sugar);
  CHECK(baseOf(swift) == 2);
  CHECK(brew(swift, ItemId::redstone) == (swift | kExtended));
  CHECK(brew(swift, ItemId::glowstone_dust) == (swift | kLevel2));
  CHECK(brew(swift | kExtended, ItemId::glowstone_dust) == -1);  // no se pueden juntar
  const int splash = brew(swift, ItemId::gunpowder);
  CHECK((splash & kSplash) != 0);
  CHECK(brew(splash, ItemId::gunpowder) == -1);
  CHECK(baseOf(brew(swift, ItemId::fermented_spider_eye)) == 10);   // velocidad -> lentitud
  CHECK(baseOf(brew(brew(kAwkward, ItemId::speckled_melon), ItemId::fermented_spider_eye)) == 12);  // curación -> daño
  CHECK(baseOf(brew(0, ItemId::fermented_spider_eye)) == 8);        // agua + ojo fermentado = debilidad
  CHECK(brew(kAwkward, ItemId::stick) == -1);
  CHECK(brew(brew(kAwkward, ItemId::speckled_melon), ItemId::redstone) == -1);  // lo instantáneo no se alarga
}

TEST_CASE("Las duraciones y niveles de las pociones son los de 1.8") {
  using namespace potion;
  auto fx0 = [](int meta) { return effectsOf(meta)[0]; };
  CHECK(fx0(kDrink | 2).ticks == 3600);
  CHECK(fx0(kDrink | 2 | kExtended).ticks == 9600);
  CHECK(fx0(kDrink | 2 | kLevel2).amp == 1);
  CHECK(fx0(kDrink | 2 | kLevel2).ticks == 1800);
  CHECK(fx0(kDrink | 1).ticks == 900);          // regeneración 0:45
  CHECK(fx0(kSplash | 2).ticks == 2700);        // las arrojadizas, 3/4
  CHECK(effectsOf(kAwkward).empty());
  CHECK(effectsOf(0).empty());
  CHECK(name(kDrink | 5 | kLevel2).find("curación II") != std::string::npos);
  CHECK(name(kSplash | 4).find("arrojadiza") != std::string::npos);
}

TEST_CASE("Beber una poción da su efecto y devuelve el frasco; la leche los quita") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setMode(GameMode::Survival);
  s.player().inventory.slot(0) = ItemStack(ItemId::potion, 1, potion::kDrink | 2);
  s.player().inventory.select(0);
  TickInput in = testing::idleTick();
  in.use = true;
  for (int i = 0; i < 40; i++) s.tick(in);
  CHECK(s.player().effects.has(fx::Speed));
  CHECK(s.player().inventory.slot(0).id == ItemId::glass_bottle);
  s.player().inventory.slot(0) = ItemStack(ItemId::milk_bucket);
  for (int i = 0; i < 40; i++) s.tick(in);
  CHECK_FALSE(s.player().effects.has(fx::Speed));
  CHECK(s.player().inventory.slot(0).id == ItemId::bucket);
}

TEST_CASE("Los efectos curan, envenenan (sin matar), marchitan (matando), absorben y resisten") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY, 0.5};
  Player& p = s.player();
  p.health = 5;
  s.applyEffect(p, fx::Regeneration, 0, 400);
  tickN(s, 120);
  CHECK(p.health > 7.5f);
  p.effects.clear();
  p.health = 4;
  s.applyEffect(p, fx::Poison, 0, 2000);
  tickN(s, 400);
  CHECK(p.health == doctest::Approx(1.0f));  // el veneno nunca mata
  CHECK_FALSE(p.dead);
  p.effects.clear();
  s.applyEffect(p, fx::Wither, 0, 2000);
  tickN(s, 200);
  CHECK(p.dead);
}

TEST_CASE("Absorción, resistencia y resistencia al fuego reducen o evitan el daño; velocidad y salto cambian el movimiento") {
  Player p;
  p.health = 20;
  p.effects.add(fx::Absorption, 0, 100);
  p.absorption = 4;
  p.damage(3.0f, false, DamageKind::Generic);
  CHECK(p.health == doctest::Approx(20.0f));
  CHECK(p.absorption == doctest::Approx(1.0f));
  Player q;
  q.health = 20;
  q.effects.add(fx::Resistance, 1, 100);  // II: -40 %
  q.damage(10.0f, false, DamageKind::Generic);
  CHECK(q.health == doctest::Approx(14.0f));
  Player r;
  r.effects.add(fx::FireResistance, 0, 100);
  CHECK_FALSE(r.damage(4.0f, true, DamageKind::Fire));
  Player f;
  f.effects.add(fx::Speed, 1, 100);
  f.effects.add(fx::Slowness, 0, 100);
  CHECK(f.effectSpeedFactor() == doctest::Approx(1.4f * 0.85f));
}

TEST_CASE("Una poción arrojadiza afecta a las criaturas cercanas según la distancia; lo instantáneo cura o daña (y al revés a los no muertos)") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.player().pos = s.player().prevPos = {20.5, kY, 20.5};
  Mob* pig = s.spawnMob(MobType::Pig, {0.5, kY, 0.5});
  REQUIRE(pig);
  const u32 pid = pig->id;
  Mob* zombie = s.spawnMob(MobType::Zombie, {2.5, kY, 0.5});
  REQUIRE(zombie);
  const u32 zid = zombie->id;
  s.mobById(pid)->health = 4;
  s.mobById(zid)->health = 4;
  s.splashPotion({1.5, kY + 0.5, 0.5}, potion::kSplash | 5);  // curación
  CHECK(s.mobById(pid)->health > 4.0f);
  CHECK(s.mobById(zid)->health < 4.0f);  // los muertos vivientes se hieren
  s.splashPotion({0.5, kY + 0.5, 0.5}, potion::kSplash | 4);  // veneno
  CHECK(s.mobById(pid)->effects.has(fx::Poison));
  // Lejos no llega
  Mob* far = s.spawnMob(MobType::Cow, {8.5, kY, 8.5});
  REQUIRE(far);
  const u32 fid = far->id;
  s.splashPotion({0.5, kY + 0.5, 0.5}, potion::kSplash | 4);
  CHECK_FALSE(s.mobById(fid)->effects.has(fx::Poison));
}

TEST_CASE("El atril de pociones tarda 20 s y transforma las tres botellas; sin ingrediente no hace nada") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  const glm::ivec3 at{2, kY, 2};
  s.placeBlock(at, makeState(117));
  ItemStack* items = s.chestItems(at);
  for (int i = 0; i < 3; i++) items[i] = ItemStack(ItemId::potion, 1, 0);
  tickN(s, 100);
  CHECK(items[0].meta == 0);
  items[3] = ItemStack(ItemId::nether_wart, 2);
  tickN(s, 399);
  CHECK(items[0].meta == 0);
  tickN(s, 3);
  for (int i = 0; i < 3; i++) CHECK(items[i].meta == potion::kAwkward);
  CHECK(items[3].count == 1);
  CHECK(stateMeta(fw.w.block(at.x, at.y, at.z)) == 7);  // las tres botellas se ven en el bloque
  // Otra tanda: azúcar sobre una poción rara
  items[3] = ItemStack(ItemId::sugar, 1);
  tickN(s, 410);
  CHECK(potion::baseOf(items[0].meta) == 2);
  CHECK(items[3].empty());
}

TEST_CASE("El caldero se llena con un cubo o un frasco de agua y se vacía con un frasco o un cubo") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setMode(GameMode::Survival);
  const glm::ivec3 at{3, kY, 3};
  s.placeBlock(at, makeState(118, 0));
  RayHit hit;
  hit.block = at;
  hit.face = 1;
  hit.point = glm::dvec3(at) + glm::dvec3(0.5, 1.0, 0.5);
  s.player().pos = s.player().prevPos = {3.5, kY, 6.5};
  s.player().inventory.slot(0) = ItemStack(ItemId::water_bucket);
  s.player().inventory.select(0);
  s.useHeldOnBlock(hit);
  CHECK(stateMeta(fw.w.block(at.x, at.y, at.z)) == 3);
  CHECK(s.player().inventory.slot(0).id == ItemId::bucket);
  s.player().inventory.slot(0) = ItemStack(ItemId::glass_bottle, 2);
  s.useHeldOnBlock(hit);
  CHECK(stateMeta(fw.w.block(at.x, at.y, at.z)) == 2);
  CHECK(s.player().inventory.slot(0).count == 1);
  bool water = false;
  for (int i = 0; i < PlayerInventory::kSize; i++) water |= s.player().inventory.slot(i).id == ItemId::potion && s.player().inventory.slot(i).meta == 0;
  CHECK(water);
}

TEST_CASE("La manzana de oro da regeneración y absorción, y la encantada, mucho más; el ojo de araña envenena") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setMode(GameMode::Survival);
  s.player().food = 20;
  s.player().inventory.slot(0) = ItemStack(ItemId::golden_apple, 2, 1);
  s.player().inventory.select(0);
  TickInput in = testing::idleTick();
  in.use = true;
  for (int i = 0; i < 40; i++) s.tick(in);
  CHECK(s.player().effects.amp(fx::Regeneration) == 4);
  CHECK(s.player().effects.has(fx::FireResistance));
  CHECK(s.player().absorption == doctest::Approx(16.0f));
}

TEST_CASE("El atril se guarda con su tiempo y sus botellas") {
  ChestState c;
  c.items[0] = ItemStack(ItemId::potion, 1, 16);
  c.items[3] = ItemStack(ItemId::sugar, 4);
  c.brewTime = 123;
  const auto back = save::chestFromNbt(save::chestToNbt(1, 2, 3, c, "Cauldron"));
  REQUIRE(back);
  CHECK(back->second.brewTime == 123);
  CHECK(back->second.items[0].meta == 16);
  CHECK(back->second.items[3].id == ItemId::sugar);
}
