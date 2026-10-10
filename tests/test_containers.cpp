#include <doctest/doctest.h>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;
constexpr int kChest = 54, kHopper = 154, kDispenser = 23, kDropper = 158, kJukebox = 84, kNoteBlock = 25;

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

int count(const ItemStack* items, int n, int id) {
  int c = 0;
  for (int i = 0; i < n; i++)
    if (items[i].id == id) c += items[i].count;
  return c;
}

GameSession::UseResult useAt(GameSession& s, const glm::ivec3& p) {
  RayHit hit;
  hit.block = p;
  hit.face = 1;
  hit.point = glm::dvec3(p) + glm::dvec3(0.5, 1.0, 0.5);
  s.player().pos = s.player().prevPos = glm::dvec3(p.x + 0.5, kY, p.z + 3.5);
  return s.useHeldOnBlock(hit);
}

}  // namespace

TEST_CASE("Dos cofres iguales y juntos forman uno doble de 54 casillas, el de menor coordenada primero") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(kChest, 2));
  CHECK(useAt(s, {0, kY, 0}).container.size() == 27);
  s.placeBlock({1, kY, 0}, makeState(kChest, 2));
  const auto big = useAt(s, {1, kY, 0});
  CHECK(big.kind == GameSession::UseResult::Kind::Chest);
  REQUIRE(big.container.size() == 54);
  CHECK(big.title == "Cofre grande");
  s.chestItems({0, kY, 0})[0] = ItemStack(ItemId::diamond, 3);
  s.chestItems({1, kY, 0})[0] = ItemStack(ItemId::apple, 2);
  const auto again = useAt(s, {0, kY, 0});  // desde cualquiera de los dos sale lo mismo
  REQUIRE(again.container.size() == 54);
  CHECK(again.container[0]->id == ItemId::diamond);
  CHECK(again.container[27]->id == ItemId::apple);
  // mirando distinto no se unen
  s.placeBlock({5, kY, 0}, makeState(kChest, 2));
  s.placeBlock({6, kY, 0}, makeState(kChest, 4));
  CHECK(useAt(s, {5, kY, 0}).container.size() == 27);
}

TEST_CASE("Un menú de contenedor grande numera las casillas como el protocolo: las suyas y luego las 36 del jugador") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  std::vector<ItemStack> store(54);
  std::vector<ItemStack*> slots;
  for (auto& it : store) slots.push_back(&it);
  Menu m(MenuKind::Chest, s.player(), slots);
  CHECK(m.containerSize() == 54);
  CHECK(m.slots().size() == 54 + 36);
  Menu hopper(MenuKind::Hopper, s.player(), std::vector<ItemStack*>{&store[0], &store[1], &store[2], &store[3], &store[4]});
  CHECK(hopper.slots().size() == 5 + 36);
  Menu disp(MenuKind::Dispenser, s.player(), std::vector<ItemStack*>(slots.begin(), slots.begin() + 9));
  CHECK(disp.slots().size() == 9 + 36);
  // con mayús, lo del inventario va al contenedor
  s.player().inventory.slot(9) = ItemStack(ItemId::coal, 5);
  m.click(54, 0, true);
  CHECK(count(store.data(), 54, ItemId::coal) == 5);
}

TEST_CASE("La tolva pasa objetos al cofre al que apunta y los coge del de arriba") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(kChest, 2));
  s.placeBlock({0, kY + 1, 0}, makeState(kHopper, 0));  // apunta hacia abajo
  s.placeBlock({0, kY + 2, 0}, makeState(kChest, 2));
  s.chestItems({0, kY + 2, 0})[0] = ItemStack(ItemId::coal, 10);
  tickN(s, 120);
  CHECK(count(s.chestItems({0, kY, 0}), 27, ItemId::coal) == 10);
  CHECK(count(s.chestItems({0, kY + 2, 0}), 27, ItemId::coal) == 0);
}

TEST_CASE("La tolva con potencia de redstone deja de mover objetos") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(kChest, 2));
  s.placeBlock({0, kY + 1, 0}, makeState(kHopper, 0));
  s.placeBlock({1, kY + 1, 0}, makeState(152));  // bloque de redstone
  s.placeBlock({0, kY + 2, 0}, makeState(kChest, 2));
  s.chestItems({0, kY + 2, 0})[0] = ItemStack(ItemId::coal, 10);
  tickN(s, 80);
  CHECK(count(s.chestItems({0, kY, 0}), 27, ItemId::coal) == 0);
}

TEST_CASE("El soltador saca un objeto con un pulso de redstone") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(kDropper, 5));  // mira al este
  s.chestItems({0, kY, 0})[4] = ItemStack(ItemId::stick, 3);
  s.placeBlock({0, kY, 1}, makeState(152));
  tickN(s, 20);
  CHECK(count(s.chestItems({0, kY, 0}), 27, ItemId::stick) == 2);
  int dropped = 0;
  for (const auto& e : s.items()) dropped += e.stack.id == ItemId::stick ? e.stack.count : 0;
  CHECK(dropped == 1);
}

TEST_CASE("El soltador pasa el objeto al cofre que tiene delante") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.placeBlock({0, kY, 0}, makeState(kDropper, 5));
  s.placeBlock({1, kY, 0}, makeState(kChest, 2));
  s.chestItems({0, kY, 0})[0] = ItemStack(ItemId::stick, 2);
  s.placeBlock({0, kY, 1}, makeState(152));
  tickN(s, 20);
  CHECK(count(s.chestItems({1, kY, 0}), 27, ItemId::stick) == 1);
}

TEST_CASE("Tocadiscos: guardar un disco y sacarlo; bloque musical: el instrumento sale del bloque de debajo") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.player().inventory.slot(0) = ItemStack(ItemId::record_13);
  s.player().inventory.select(0);
  s.placeBlock({0, kY, 0}, makeState(kJukebox, 0));
  s.takeEvents();
  useAt(s, {0, kY, 0});
  CHECK(stateMeta(fw.w.block(0, kY, 0)) == 1);
  CHECK(s.chestItems({0, kY, 0})[0].id == ItemId::record_13);
  CHECK(s.player().inventory.slot(0).empty());
  bool started = false;
  for (const auto& e : s.takeEvents()) started |= e.type == SessionEvent::Type::RecordStart;
  CHECK(started);
  useAt(s, {0, kY, 0});
  CHECK(stateMeta(fw.w.block(0, kY, 0)) == 0);
  CHECK(s.chestItems({0, kY, 0})[0].empty());
  bool dropped = false;
  for (const auto& e : s.items()) dropped |= e.stack.id == ItemId::record_13;
  CHECK(dropped);

  // Bloque musical: encima de piedra suena el bombo (1) y encima de la hierba, el piano (0)
  s.placeBlock({4, kY, 0}, makeState(kNoteBlock, 0));
  s.takeEvents();
  s.player().inventory.slot(0).clear();
  useAt(s, {4, kY, 0});
  int value = -1;
  for (const auto& e : s.takeEvents())
    if (e.type == SessionEvent::Type::NotePlay) value = e.value;
  REQUIRE(value >= 0);
  CHECK((value >> 5) == 0);
  CHECK((value & 31) == 1);  // un clic sube la nota
}

TEST_CASE("Los contenedores se guardan: el bloque musical y la tolva vacía con sus datos") {
  const nbt::Value tag = save::noteToNbt(3, 70, -2, 17, true);
  const auto back = save::noteFromNbt(tag);
  REQUIRE(back);
  CHECK(std::get<0>(*back) == glm::ivec3(3, 70, -2));
  CHECK(std::get<1>(*back) == 17);
  CHECK(std::get<2>(*back));
  ChestState c;
  c.items[2] = ItemStack(ItemId::arrow, 12);
  const auto h = save::chestFromNbt(save::chestToNbt(1, 2, 3, c, "Hopper"));
  REQUIRE(h);
  CHECK(h->second.items[2] == ItemStack(ItemId::arrow, 12));
  CHECK(save::chestToNbt(1, 2, 3, c, "Hopper").getString("id") == "Hopper");
}
