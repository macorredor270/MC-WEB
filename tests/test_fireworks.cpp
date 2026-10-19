#include <doctest/doctest.h>

#include "data/items.h"
#include "game/crafting.h"
#include "game/firework.h"
#include "save/anvil.h"

using namespace mcw;

namespace {
ItemStack dye(int meta) { return ItemStack(ItemId::dye, 1, meta); }
}  // namespace

TEST_CASE("Fuegos artificiales: los colores son los de los tintes de 1.8") {
  CHECK(firework::dyeColor(1) == 0xB3312C);   // rojo
  CHECK(firework::dyeColor(15) == 0xF0F0F0);  // blanco (polvo de hueso)
  CHECK(firework::dyeColor(4) == 0x253192);   // lapislázuli
}

TEST_CASE("Fuegos artificiales: una estrella con pólvora, tintes, forma, estela y centelleo") {
  std::array<ItemStack, 9> g;
  g[0] = ItemStack(ItemId::gunpowder, 1);
  g[1] = dye(1);
  g[2] = dye(4);
  g[3] = ItemStack(ItemId::fire_charge, 1);       // bola grande
  g[4] = ItemStack(ItemId::diamond, 1);           // estela
  g[5] = ItemStack(ItemId::glowstone_dust, 1);    // centelleo
  const auto star = matchRecipe(g, 3);
  REQUIRE(star);
  CHECK(star->id == ItemId::firework_charge);
  REQUIRE(star->extra);
  REQUIRE(star->extra->explosions.size() == 1);
  const FireworkExplosion& x = star->extra->explosions[0];
  CHECK(x.type == 1);
  CHECK(x.trail);
  CHECK(x.flicker);
  CHECK(x.colors == std::vector<i32>{0xB3312C, 0x253192});
  CHECK(x.fade.empty());
  // Las otras formas
  for (auto [item, type] : {std::pair{ItemId::gold_nugget, 2}, {ItemId::feather, 4}}) {
    std::array<ItemStack, 4> h{ItemStack(ItemId::gunpowder, 1), dye(2), ItemStack(item, 1), ItemStack()};
    const auto s = matchRecipe(h, 2);
    REQUIRE(s);
    CHECK(s->extra->explosions[0].type == type);
  }
  std::array<ItemStack, 4> head{ItemStack(ItemId::gunpowder, 1), dye(2), ItemStack(397, 1, 4), ItemStack()};  // cabeza de creeper
  const auto creeper = matchRecipe(head, 2);
  REQUIRE(creeper);
  CHECK(creeper->extra->explosions[0].type == 3);
}

TEST_CASE("Fuegos artificiales: sin tinte, con dos formas, con dos pólvoras o con algo ajeno no hay estrella") {
  std::array<ItemStack, 4> noDye{ItemStack(ItemId::gunpowder, 1), ItemStack(), ItemStack(), ItemStack()};
  CHECK_FALSE(firework::craft(noDye));
  std::array<ItemStack, 9> twoShapes;
  twoShapes[0] = ItemStack(ItemId::gunpowder, 1);
  twoShapes[1] = dye(1);
  twoShapes[2] = ItemStack(ItemId::feather, 1);
  twoShapes[3] = ItemStack(ItemId::gold_nugget, 1);
  CHECK_FALSE(firework::craft(twoShapes));
  std::array<ItemStack, 9> foreign;
  foreign[0] = ItemStack(ItemId::gunpowder, 1);
  foreign[1] = dye(1);
  foreign[2] = ItemStack(ItemId::stick, 1);
  CHECK_FALSE(firework::craft(foreign));
}

TEST_CASE("Fuegos artificiales: desvanecer una estrella le pone los colores a los que se apaga") {
  std::array<ItemStack, 4> g{ItemStack(ItemId::gunpowder, 1), dye(1), ItemStack(), ItemStack()};
  const auto star = firework::craft(g);
  REQUIRE(star);
  std::array<ItemStack, 4> f{*star, dye(11), dye(10), ItemStack()};
  const auto faded = firework::craft(f);
  REQUIRE(faded);
  CHECK(faded->id == ItemId::firework_charge);
  CHECK(faded->extra->explosions[0].colors == std::vector<i32>{0xB3312C});
  CHECK(faded->extra->explosions[0].fade == std::vector<i32>{0xDECF2A, 0x41CD34});
}

TEST_CASE("Fuegos artificiales: el cohete lleva el vuelo y las explosiones de sus estrellas (hasta siete) y salen tres") {
  std::array<ItemStack, 4> g{ItemStack(ItemId::gunpowder, 1), dye(1), ItemStack(), ItemStack()};
  const auto star = firework::craft(g);
  REQUIRE(star);
  std::array<ItemStack, 9> r;
  r[0] = ItemStack(ItemId::paper, 1);
  r[1] = ItemStack(ItemId::gunpowder, 1);
  r[2] = ItemStack(ItemId::gunpowder, 1);
  r[3] = *star;
  r[4] = *star;
  const auto rocket = matchRecipe(r, 3);
  REQUIRE(rocket);
  CHECK(rocket->id == ItemId::fireworks);
  CHECK(rocket->count == 3);
  REQUIRE(rocket->extra);
  CHECK(rocket->extra->flight == 2);
  CHECK(rocket->extra->explosions.size() == 2);
  // Sin estrellas también vale; con cuatro pólvoras no; con ocho estrellas no
  std::array<ItemStack, 4> plain{ItemStack(ItemId::paper, 1), ItemStack(ItemId::gunpowder, 1), ItemStack(), ItemStack()};
  const auto bare = matchRecipe(plain, 2);
  REQUIRE(bare);
  CHECK(bare->id == ItemId::fireworks);
  CHECK(bare->extra->flight == 1);
  CHECK(bare->extra->explosions.empty());
  std::array<ItemStack, 9> many;
  many[0] = ItemStack(ItemId::paper, 1);
  many[1] = ItemStack(ItemId::gunpowder, 1);
  for (int i = 2; i < 9; i++) many[static_cast<std::size_t>(i)] = *star;  // 7 estrellas: bien
  CHECK(firework::craft(many));
}

TEST_CASE("Fuegos artificiales: las etiquetas de estrella y cohete hacen ida y vuelta por NBT") {
  ItemExtra e;
  e.flight = 3;
  FireworkExplosion a, b;
  a.type = 2;
  a.flicker = true;
  a.colors = {0xB3312C, 0x253192};
  a.fade = {0xF0F0F0};
  b.type = 4;
  b.trail = true;
  b.colors = {0x41CD34};
  e.explosions = {a, b};
  const auto tag = save::itemTagToNbt(e);
  const auto back = save::itemTagFromNbt(tag);
  REQUIRE(back);
  CHECK(*back == e);
  // Una estrella lleva su "Explosion"
  ItemExtra star;
  star.explosions = {a};
  const auto starBack = save::itemTagFromNbt(save::itemTagToNbt(star));
  REQUIRE(starBack);
  CHECK(starBack->flight == 0);
  CHECK(starBack->explosions == star.explosions);
}

#include "flat_world.h"
#include "game/session.h"

using testing::FlatTestWorld;

namespace {

ItemStack rocketWith(int flight, int explosions) {
  ItemExtra e;
  e.flight = flight;
  for (int i = 0; i < explosions; i++) {
    FireworkExplosion x;
    x.type = static_cast<u8>(i % 5);
    x.colors = {0xB3312C};
    e.explosions.push_back(x);
  }
  ItemStack s(ItemId::fireworks, 3, 0);
  s.extra = std::make_shared<const ItemExtra>(std::move(e));
  return s;
}

RayHit groundHit() {
  RayHit h;
  h.block = {0, 63, 0};
  h.face = Face::Up;
  h.point = {0.5, 64.0, 0.5};
  h.distance = 2;
  return h;
}

struct Range {
  FlatTestWorld fw;
  GameSession s{fw, 5};
  Range() {
    s.setRules({2, true, true});
    s.setMode(GameMode::Survival);
    s.player().pos = s.player().prevPos = {30.5, 64, 30.5};  // lejos del estallido
  }
};

}  // namespace

TEST_CASE("Cohete: se lanza desde un bloque, sube acelerando, estalla al acabar el vuelo y se gasta uno") {
  Range r;
  r.s.player().inventory.slot(0) = rocketWith(2, 1);
  const auto res = r.s.useHeldOnBlock(groundHit());
  CHECK(res.kind == GameSession::UseResult::Kind::Used);
  CHECK(r.s.player().inventory.slot(0).count == 2);
  REQUIRE(r.s.rockets().size() == 1);
  const double y0 = r.s.rockets()[0].pos.y;
  r.s.tick(testing::idleTick());
  CHECK(r.s.rockets()[0].pos.y > y0);
  double prevDy = 0;
  for (int i = 0; i < 6; i++) {
    const double before = r.s.rockets()[0].pos.y;
    r.s.tick(testing::idleTick());
    const double dy = r.s.rockets()[0].pos.y - before;
    CHECK(dy > prevDy);  // acelera
    prevDy = dy;
  }
  std::vector<FireworkBurst> bursts;
  for (int i = 0; i < 80 && bursts.empty(); i++) {
    r.s.tick(testing::idleTick());
    bursts = r.s.takeBursts();
  }
  REQUIRE(bursts.size() == 1);
  CHECK(bursts[0].pos.y > 66);
  CHECK(bursts[0].extra->explosions.size() == 1);
  for (int i = 0; i < 4; i++) r.s.tick(testing::idleTick());
  CHECK(r.s.rockets().empty());  // se quita tras estallar
}

TEST_CASE("Cohete: el vuelo dura más con más pólvora (10 ticks por cada una, más algo de azar)") {
  int shortest[2] = {1000, 1000}, longest[2] = {0, 0};
  for (int flight : {1, 3}) {
    for (u64 seed = 1; seed <= 20; seed++) {
      FlatTestWorld fw;
      GameSession s(fw, seed);
      s.setRules({2, true, true});
      s.player().pos = s.player().prevPos = {30.5, 64, 30.5};
      s.launchRocket({0.5, 64, 0.5}, rocketWith(flight, 1).extra);
      int ticks = 0;
      while (!s.rockets().empty() && !s.rockets()[0].exploded && ticks < 200) {
        s.tick(testing::idleTick());
        ticks++;
      }
      const int k = flight == 1 ? 0 : 1;
      shortest[k] = std::min(shortest[k], ticks);
      longest[k] = std::max(longest[k], ticks);
    }
  }
  CHECK(shortest[0] >= 10);
  CHECK(longest[0] <= 22);
  CHECK(shortest[1] >= 30);
  CHECK(longest[1] <= 42);
}

TEST_CASE("Cohete: con estrellas hiere con 5 más 2 por explosión a lo que tenga a menos de 5 bloques; sin estrellas no hace nada") {
  for (const int explosions : {0, 2}) {
    Range r;
    // Murciélagos quietos en una columna a varias alturas (alguno quedará cerca del estallido) y otro lejos
    std::vector<u32> column;
    for (int y : {65, 68, 71, 74, 77, 80}) {
      Mob* b = r.s.spawnMob(MobType::Bat, {0.5, static_cast<double>(y), 0.5});
      REQUIRE(b);
      b->noAI = true;
      b->persistent = true;
      b->health = 100;
      column.push_back(b->id);
    }
    Mob* far = r.s.spawnMob(MobType::Bat, {20.5, 70, 0.5});
    REQUIRE(far);
    far->noAI = true;
    far->persistent = true;
    far->health = 100;
    const u32 farId = far->id;
    r.s.launchRocket({0.5, 64.0, 0.5}, rocketWith(2, explosions).extra);
    for (int i = 0; i < 60; i++) r.s.tick(testing::idleTick());
    int hurt = 0;
    float lowest = 100;
    for (u32 id : column)
      if (const Mob* m = r.s.mobById(id)) {
        if (m->health < 100) hurt++;
        lowest = std::min(lowest, m->health);
      }
    CHECK(r.s.mobById(farId)->health == 100);  // el lejano no
    if (explosions == 0) {
      CHECK(hurt == 0);
    } else {
      CHECK(hurt >= 1);
      CHECK(lowest == doctest::Approx(100.0f - 9.0f));  // 5 + 2 * 2
    }
  }
}

TEST_CASE("Cohete: contra un techo estalla enseguida") {
  Range r;
  for (int z = -2; z <= 2; z++)
    for (int x = -2; x <= 2; x++) r.fw.setBlock(x, 67, z, makeState(B::stone));
  r.s.launchRocket({0.5, 64.0, 0.5}, rocketWith(3, 1).extra);
  int ticks = 0;
  while (!r.s.rockets().empty() && !r.s.rockets()[0].exploded && ticks < 100) {
    r.s.tick(testing::idleTick());
    ticks++;
  }
  CHECK(ticks < 15);  // (con el vuelo 3 tardaría más de 30)
}
