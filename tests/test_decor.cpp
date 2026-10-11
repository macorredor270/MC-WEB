#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/decor.h"
#include "game/session.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void put(FlatTestWorld& f, int x, int y, int z, int id, int meta = 0) { f.setBlock(x, y, z, makeState(id, meta)); }

TickInput aimAt(const GameSession& s, const glm::dvec3& target) {
  TickInput in = testing::idleTick();
  in.aimDir = glm::normalize(target - s.player().eyePos());
  return in;
}

void rightClick(GameSession& s, const glm::dvec3& target) {
  TickInput in = aimAt(s, target);
  in.use = in.usePressed = true;
  s.tick(in);
  in.use = in.usePressed = false;
  s.tick(in);
}

void leftClick(GameSession& s, const glm::dvec3& target) {
  TickInput in = aimAt(s, target);
  in.attack = in.attackPressed = true;
  s.tick(in);
  in.attack = in.attackPressed = false;
  s.tick(in);
}

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

/// Una pared de piedra en z=3 de x0 a x1 y de y0 a y1.
void wall(FlatTestWorld& f, int x0, int x1, int y0, int y1) {
  for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++) put(f, x, y, 3, B::stone);
}

/// Lo que hay de ese objeto en el suelo y en el inventario (lo suelto se recoge al estar cerca).
int itemsOf(const GameSession& s, int id) {
  int n = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (s.player().inventory.slot(i).id == id) n += s.player().inventory.slot(i).count;
  for (const ItemEntity& e : s.items())
    if (e.stack.id == id) n += e.stack.count;
  return n;
}

}  // namespace

TEST_CASE("Marco: se cuelga de la pared, guarda un objeto, gira y suelta lo suyo al golpearlo") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {5.5, 64, 6.0};
  wall(fw, 3, 7, 64, 66);
  p.inventory.selected() = ItemStack(ItemId::item_frame, 2);
  tickN(s, 1);
  rightClick(s, {5.5, 64.5, 3.99});  // la cara sur del bloque (5, 64, 3)
  REQUIRE(s.decors().size() == 1);
  const Decor& f = s.decors()[0];
  CHECK(f.kind == DecorKind::ItemFrame);
  CHECK(f.dir == 0);  // mira al sur
  CHECK(f.cell == glm::ivec3(5, 64, 4));
  CHECK(p.inventory.selected().count == 1);
  // Con un objeto en la mano, un clic derecho sobre el marco lo deja dentro
  p.inventory.selected() = ItemStack(ItemId::diamond, 3);
  rightClick(s, s.decors()[0].center());
  CHECK(s.decors()[0].item.id == ItemId::diamond);
  CHECK(p.inventory.selected().count == 2);
  // Otro clic lo gira un paso (45º) y ocho vuelven al principio
  rightClick(s, s.decors()[0].center());
  CHECK(s.decors()[0].rotation == 1);
  for (int i = 0; i < 7; i++) rightClick(s, s.decors()[0].center());
  CHECK(s.decors()[0].rotation == 0);
  // Un golpe saca el objeto y el marco se queda; el siguiente rompe el marco
  leftClick(s, s.decors()[0].center());
  REQUIRE(s.decors().size() == 1);
  CHECK(s.decors()[0].item.empty());
  CHECK(itemsOf(s, ItemId::diamond) == 3);
  leftClick(s, s.decors()[0].center());
  CHECK(s.decors().empty());
  CHECK(itemsOf(s, ItemId::item_frame) == 1);  // (el otro se sustituyó por diamantes)
}

TEST_CASE("Marco: sin pared se cae, y por arriba no se cuelga") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {5.5, 64, 6.0};
  wall(fw, 5, 5, 64, 64);
  p.inventory.selected() = ItemStack(ItemId::item_frame, 5);
  tickN(s, 1);
  rightClick(s, {5.5, 64.99, 3.5});  // la cara de arriba del bloque
  CHECK(s.decors().empty());
  rightClick(s, {5.5, 64.5, 3.99});
  REQUIRE(s.decors().size() == 1);
  put(fw, 5, 64, 3, 0);  // se quita la pared
  tickN(s, 25);
  CHECK(s.decors().empty());
  CHECK(itemsOf(s, ItemId::item_frame) == 5);
}

TEST_CASE("Cuadro: elige el lienzo más grande que cabe") {
  {
    FlatTestWorld fw;
    GameSession s(fw, 7);
    Player& p = s.player();
    p.pos = p.prevPos = {4.5, 64, 6.0};
    wall(fw, 2, 5, 64, 67);  // 4x4
    p.inventory.selected() = ItemStack(ItemId::painting, 1);
    tickN(s, 1);
    rightClick(s, {5.5, 64.5, 3.99});  // se hace clic en la casilla de abajo a la derecha
    REQUIRE(s.decors().size() == 1);
    const PaintingMotive& m = paintingMotives()[s.decors()[0].motive];
    CHECK(m.w * m.h == 16);  // Pointer, Pigscene o BurningSkull
  }
  {
    FlatTestWorld fw;
    GameSession s(fw, 7);
    Player& p = s.player();
    p.pos = p.prevPos = {4.5, 64, 6.0};
    wall(fw, 5, 5, 64, 64);  // un solo bloque
    p.inventory.selected() = ItemStack(ItemId::painting, 1);
    tickN(s, 1);
    rightClick(s, {5.5, 64.5, 3.99});
    REQUIRE(s.decors().size() == 1);
    const PaintingMotive& m = paintingMotives()[s.decors()[0].motive];
    CHECK(m.w == 1);
    CHECK(m.h == 1);
    // Un golpe lo rompe y suelta el cuadro
    leftClick(s, s.decors()[0].center());
    CHECK(s.decors().empty());
    CHECK(itemsOf(s, ItemId::painting) == 1);
  }
}

TEST_CASE("Soporte para armadura: se pone de pie, se viste, se desviste y a la segunda se rompe") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {5.5, 64, 8.5};
  p.inventory.selected() = ItemStack(ItemId::armor_stand, 1);
  tickN(s, 1);
  rightClick(s, {5.5, 63.99, 5.5});
  REQUIRE(s.decors().size() == 1);
  const Decor& st = s.decors()[0];
  CHECK(st.kind == DecorKind::ArmorStand);
  CHECK(st.pos.y == doctest::Approx(64.0));
  CHECK(p.inventory.selected().empty());
  // Se le pone la pechera
  p.inventory.selected() = ItemStack(ItemId::iron_chestplate, 1);
  rightClick(s, st.pos + glm::dvec3(0, 1.2, 0));
  CHECK(s.decors()[0].equip[3].id == ItemId::iron_chestplate);
  CHECK(p.inventory.selected().empty());
  // Con la mano vacía a la altura del pecho se la quita
  rightClick(s, s.decors()[0].pos + glm::dvec3(0, 1.2, 0));
  CHECK(s.decors()[0].equip[3].empty());
  CHECK(p.inventory.selected().id == ItemId::iron_chestplate);
  // Se vuelve a vestir y se rompe: el primer golpe lo hace temblar y el segundo lo rompe
  rightClick(s, s.decors()[0].pos + glm::dvec3(0, 1.2, 0));
  CHECK(s.decors()[0].equip[3].id == ItemId::iron_chestplate);
  leftClick(s, s.decors()[0].pos + glm::dvec3(0, 1.0, 0));
  REQUIRE(s.decors().size() == 1);
  CHECK(s.decors()[0].hurtTime > 0);
  leftClick(s, s.decors()[0].pos + glm::dvec3(0, 1.0, 0));
  CHECK(s.decors().empty());
  CHECK(itemsOf(s, ItemId::armor_stand) == 1);
  CHECK(itemsOf(s, ItemId::iron_chestplate) == 1);
}

TEST_CASE("Soporte para armadura: cae si le quitan el suelo") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {5.5, 64, 8.5};
  put(fw, 5, 64, 5, B::stone);
  p.inventory.selected() = ItemStack(ItemId::armor_stand, 1);
  tickN(s, 1);
  rightClick(s, {5.5, 64.99, 5.5});
  REQUIRE(s.decors().size() == 1);
  CHECK(s.decors()[0].pos.y == doctest::Approx(65.0));
  put(fw, 5, 64, 5, 0);
  tickN(s, 20);
  CHECK(s.decors()[0].pos.y == doctest::Approx(64.0).epsilon(0.02));
}

TEST_CASE("Adornos: guardado en NBT con los ids de 1.8") {
  Decor f;
  f.kind = DecorKind::ItemFrame;
  f.cell = {5, 64, 4};
  f.dir = 2;
  f.item = ItemStack(ItemId::diamond, 1);
  f.rotation = 3;
  const auto fr = save::decorFromNbt(save::decorToNbt(f));
  REQUIRE(fr);
  CHECK(fr->kind == DecorKind::ItemFrame);
  CHECK(fr->cell == f.cell);
  CHECK(fr->dir == 2);
  CHECK(fr->item.id == ItemId::diamond);
  CHECK(fr->rotation == 3);
  CHECK(save::decorToNbt(f).getString("id") == "ItemFrame");

  Decor pt;
  pt.kind = DecorKind::Painting;
  pt.cell = {1, 70, 2};
  pt.dir = 1;
  pt.motive = static_cast<u8>(paintingMotiveByName("Pigscene"));
  const auto pr = save::decorFromNbt(save::decorToNbt(pt));
  REQUIRE(pr);
  CHECK(pr->motive == pt.motive);
  CHECK(save::decorToNbt(pt).getString("Motive") == "Pigscene");

  Decor st;
  st.kind = DecorKind::ArmorStand;
  st.pos = {3.5, 64, -2.5};
  st.yaw = 0.785f;
  st.equip[4] = ItemStack(ItemId::iron_helmet, 1);
  st.arms = true;
  const auto sr = save::decorFromNbt(save::decorToNbt(st));
  REQUIRE(sr);
  CHECK(sr->pos.x == doctest::Approx(3.5));
  CHECK(sr->equip[4].id == ItemId::iron_helmet);
  CHECK(sr->equip[3].empty());
  CHECK(sr->arms);
  CHECK(sr->yaw == doctest::Approx(0.785f).epsilon(0.01));
}
