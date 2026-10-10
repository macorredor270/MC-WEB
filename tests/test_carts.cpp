#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/rails.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;  // el suelo es y=63: los raíles van en y=64

void put(FlatTestWorld& f, int x, int y, int z, int id, int meta = 0) { f.setBlock(x, y, z, makeState(id, meta)); }

/// Una fila de raíles este-oeste desde x0 hasta x1 (incluidos) en z=0.
void line(FlatTestWorld& f, int x0, int x1, int id = rails::kRail, int meta = 1) {
  for (int x = x0; x <= x1; x++) put(f, x, kY, 0, id, meta);
}

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

void tickN(GameSession& s, int n, const TickInput& in = testing::idleTick()) {
  for (int i = 0; i < n; i++) s.tick(in);
}

glm::dvec3 above(const Minecart& c) { return c.pos + glm::dvec3(0, 0.35, 0); }

int countOf(const Player& p, int id) {
  int n = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (p.inventory.slot(i).id == id) n += p.inventory.slot(i).count;
  return n;
}

}  // namespace

TEST_CASE("Vagoneta (partida): el objeto sobre un raíl la coloca y se gasta uno") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, kY, 2.5};
  line(fw, 0, 3);
  p.inventory.selected() = ItemStack(ItemId::minecart, 2);
  tickN(s, 1);
  rightClick(s, {0.5, kY + 0.06, 0.5});
  REQUIRE(s.carts().size() == 1);
  CHECK(p.inventory.selected().count == 1);
  const Minecart& c = s.carts()[0];
  CHECK(c.type == CartType::Normal);
  CHECK(c.pos.x == doctest::Approx(0.5));
  CHECK(c.pos.y == doctest::Approx(kY + 0.0625));
  // Sobre la hierba no se pone
  rightClick(s, {1.5, kY, 4.5});
  CHECK(s.carts().size() == 1);
  // Cada tipo de vagoneta pone el suyo; en creativo no se gasta
  s.setMode(GameMode::Creative);
  p.inventory.selected() = ItemStack(ItemId::tnt_minecart, 1);
  rightClick(s, {2.5, kY + 0.06, 0.5});
  REQUIRE(s.carts().size() == 2);
  CHECK(s.carts()[1].type == CartType::Tnt);
  CHECK(p.inventory.selected().count == 1);
}

TEST_CASE("Raíles (partida): al colocar uno junto a otro se unen") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, kY, 3.5};
  put(fw, 0, kY, 0, rails::kRail, 0);
  p.inventory.selected() = ItemStack(rails::kRail, 4);
  tickN(s, 1);
  rightClick(s, {1.5, kY, 0.5});  // la hierba de al lado, por arriba
  REQUIRE(stateId(fw.w.block(1, kY, 0)) == rails::kRail);
  CHECK(stateMeta(fw.w.block(1, kY, 0)) == 1);  // este-oeste
  CHECK(stateMeta(fw.w.block(0, kY, 0)) == 1);  // y el primero se ha girado para encajar
}

TEST_CASE("Vagoneta (partida): se monta, la empuja el que va dentro y al agacharse se baja") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {3.5, kY, 2.5};
  line(fw, 0, 40);
  const u32 id = s.spawnCart(CartType::Normal, {3, kY, 0});
  REQUIRE(id != 0);
  tickN(s, 1);
  rightClick(s, above(*s.cartById(id)));
  REQUIRE(p.mounted());
  CHECK(p.mount == Player::Mount::Cart);
  // Empujando hacia delante (mirando al este) arranca y se aleja
  TickInput go = testing::idleTick();
  go.yaw = -1.5707963f;
  go.move.forward = 1;
  tickN(s, 40, go);
  const Minecart* c = s.cartById(id);
  REQUIRE(c);
  CHECK(c->pos.x > 8.0);
  CHECK(p.pos.x == doctest::Approx(c->pos.x));
  CHECK(p.pos.y == doctest::Approx(c->pos.y - 0.35));
  // Agacharse la deja
  TickInput down = go;
  down.move.forward = 0;
  down.move.sneak = true;
  s.tick(down);
  CHECK_FALSE(p.mounted());
  CHECK(p.pos.y >= c->pos.y + 0.6);
}

TEST_CASE("Vagoneta (partida): los golpes se acumulan y a la quinta con el puño se rompe y suelta su objeto") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {3.5, kY, 2.5};
  line(fw, 0, 10);
  const u32 id = s.spawnCart(CartType::Normal, {3, kY, 0});
  tickN(s, 1);
  const glm::dvec3 target = above(*s.cartById(id));
  TickInput hit = aimAt(s, target);
  hit.attack = hit.attackPressed = true;
  s.tick(hit);
  REQUIRE(s.cartById(id));
  CHECK(s.cartById(id)->damage > 5.0f);
  for (int i = 0; i < 4; i++) s.tick(hit);
  CHECK(s.cartById(id) == nullptr);
  bool dropped = false;
  for (const ItemEntity& e : s.items())
    if (e.stack.id == ItemId::minecart) dropped = true;
  CHECK(dropped);
}

TEST_CASE("Vagoneta (partida): una espada la rompe de un golpe y en creativo no suelta nada") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {3.5, kY, 2.5};
  line(fw, 0, 10);
  p.inventory.selected() = ItemStack(ItemId::iron_sword);
  const u32 id = s.spawnCart(CartType::Normal, {3, kY, 0});
  tickN(s, 1);
  leftClick(s, above(*s.cartById(id)));
  CHECK(s.cartById(id) == nullptr);
  CHECK(s.items().size() == 1);
  // En creativo
  FlatTestWorld fc;
  GameSession c(fc, 7);
  c.setMode(GameMode::Creative);
  c.player().pos = c.player().prevPos = {3.5, kY, 2.5};
  line(fc, 0, 10);
  const u32 cid = c.spawnCart(CartType::Normal, {3, kY, 0});
  tickN(c, 1);
  leftClick(c, above(*c.cartById(cid)));
  CHECK(c.cartById(cid) == nullptr);
  CHECK(c.items().empty());
}

TEST_CASE("Vagoneta con cofre: se abre, guarda cosas y al romperla suelta el cofre, la vagoneta y lo guardado") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {3.5, kY, 2.5};
  line(fw, 0, 10);
  const u32 id = s.spawnCart(CartType::Chest, {3, kY, 0});
  REQUIRE(s.cartItems(id) != nullptr);
  s.cartItems(id)[0] = ItemStack(ItemId::diamond, 5);
  s.cartItems(id)[26] = ItemStack(B::cobblestone, 64);
  tickN(s, 1);
  rightClick(s, above(*s.cartById(id)));
  REQUIRE(s.menu() != nullptr);
  CHECK(s.menu()->kind() == MenuKind::Chest);
  CHECK(s.menu()->slots()[0].stack->id == ItemId::diamond);
  // Se saca un diamante con el clic y se cierra
  s.menu()->click(0, 0, true);
  s.closeMenu();
  CHECK(s.cartItems(id)[0].empty());
  CHECK(countOf(p, ItemId::diamond) == 5);
  // Con una espada se rompe del todo
  p.inventory.selected() = ItemStack(ItemId::diamond_sword);
  leftClick(s, above(*s.cartById(id)));
  CHECK(s.cartById(id) == nullptr);
  int minecarts = 0, chests = 0, cobble = 0;
  for (const ItemEntity& e : s.items()) {
    if (e.stack.id == ItemId::minecart) minecarts += e.stack.count;
    if (e.stack.id == 54) chests += e.stack.count;
    if (e.stack.id == B::cobblestone) cobble += e.stack.count;
  }
  CHECK(minecarts == 1);
  CHECK(chests == 1);
  CHECK(cobble == 64);
}

TEST_CASE("Vagoneta con horno: con carbón se empuja sola, lejos de quien la alimenta") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {2.5, kY, 0.5};  // al oeste de la vagoneta
  line(fw, 0, 40);
  const u32 id = s.spawnCart(CartType::Furnace, {4, kY, 0});
  p.inventory.selected() = ItemStack(ItemId::coal, 3);
  tickN(s, 1);
  rightClick(s, above(*s.cartById(id)));
  CHECK(p.inventory.selected().count == 2);
  CHECK(s.cartById(id)->fuel > 3000);
  tickN(s, 60);
  const Minecart* c = s.cartById(id);
  REQUIRE(c);
  CHECK(c->pos.x > 10.0);
  CHECK(c->motion.x > 0.1);
  // Sin carbón no se mueve
  for (int x = 0; x <= 10; x++) put(fw, x, kY, 6, rails::kRail, 1);
  const u32 other = s.spawnCart(CartType::Furnace, {4, kY, 6});
  tickN(s, 20);
  CHECK(s.cartById(other)->pos.x == doctest::Approx(4.5));
}

TEST_CASE("Vagoneta con dinamita: el raíl activador con potencia la enciende y a los 4 segundos explota") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {30.5, kY, 20.5};  // lejos de la explosión
  put(fw, 5, kY, 0, rails::kActivator, 1 | 8);
  put(fw, 4, kY, 0, rails::kRail, 1);
  put(fw, 6, kY, 0, rails::kRail, 1);
  const u32 id = s.spawnCart(CartType::Tnt, {5, kY, 0});
  tickN(s, 2);
  const Minecart* c = s.cartById(id);
  REQUIRE(c);
  CHECK(c->fuse > 0);
  tickN(s, 70);
  CHECK(s.cartById(id) != nullptr);  // aún no
  tickN(s, 15);
  CHECK(s.cartById(id) == nullptr);
  CHECK(fw.w.block(5, kY - 1, 0) == 0);  // el suelo de debajo ha saltado
  // Una vagoneta con dinamita a la que se golpea parada no explota y suelta la dinamita
  FlatTestWorld f2;
  GameSession t(f2, 7);
  t.player().pos = t.player().prevPos = {5.5, kY, 2.5};
  line(f2, 0, 10);
  t.player().inventory.selected() = ItemStack(ItemId::iron_sword);
  const u32 id2 = t.spawnCart(CartType::Tnt, {5, kY, 0});
  tickN(t, 1);
  leftClick(t, above(*t.cartById(id2)));
  CHECK(t.cartById(id2) == nullptr);
  bool tnt = false;
  for (const ItemEntity& e : t.items())
    if (e.stack.id == B::tnt) tnt = true;
  CHECK(tnt);
}

TEST_CASE("Vagoneta con dinamita: choca de frente a toda velocidad y explota") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.player().pos = s.player().prevPos = {30.5, kY, 20.5};
  line(fw, 0, 14, rails::kPowered, 1 | 8);
  put(fw, 15, kY, 0, B::stone);
  const u32 id = s.spawnCart(CartType::Tnt, {2, kY, 0});
  tickN(s, 1);
  s.cartById(id)->motion.x = 0.4;
  tickN(s, 80);
  CHECK(s.cartById(id) == nullptr);
  CHECK(fw.w.block(15, kY, 0) == 0);  // el bloque contra el que chocó ha saltado
}

TEST_CASE("Raíl propulsor (partida): la potencia llega a 8 raíles más en fila") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.player().pos = s.player().prevPos = {0.5, kY, 5.5};
  for (int x = 1; x <= 12; x++) put(fw, x, kY, 0, rails::kPowered, 1);
  s.placeBlock({0, kY, 0}, makeState(152));  // bloque de redstone junto al primero
  for (int x = 1; x <= 9; x++) CHECK_MESSAGE((stateMeta(fw.w.block(x, kY, 0)) & 8) != 0, "x = ", x);
  for (int x = 10; x <= 12; x++) CHECK_MESSAGE((stateMeta(fw.w.block(x, kY, 0)) & 8) == 0, "x = ", x);
  // Al quitar el bloque se apagan todos
  s.placeBlock({0, kY, 0}, 0);
  for (int x = 1; x <= 12; x++) CHECK_MESSAGE((stateMeta(fw.w.block(x, kY, 0)) & 8) == 0, "x = ", x);
}

TEST_CASE("Raíl detector (partida): lo enciende una vagoneta encima y se apaga poco después de irse") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.player().pos = s.player().prevPos = {0.5, kY, 8.5};
  line(fw, 0, 12);
  put(fw, 6, kY, 0, rails::kDetector, 1);
  put(fw, 6, kY, -1, 123);  // una lámpara de redstone al lado
  const u32 id = s.spawnCart(CartType::Normal, {2, kY, 0});
  tickN(s, 1);
  CHECK(stateId(fw.w.block(6, kY, -1)) == 123);
  s.cartById(id)->motion.x = 0.3;
  bool lit = false;
  for (int i = 0; i < 30; i++) {
    s.tick(testing::idleTick());
    lit |= stateId(fw.w.block(6, kY, -1)) == 124;
  }
  CHECK(lit);
  // Se queda parada lejos y la lámpara acaba apagándose
  tickN(s, 60);
  CHECK(stateId(fw.w.block(6, kY, -1)) == 123);
  CHECK((stateMeta(fw.w.block(6, kY, 0)) & 8) == 0);
}

TEST_CASE("Raíl activador (partida): baja a quien va montado") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {2.5, kY, 2.5};
  line(fw, 0, 12);
  put(fw, 6, kY, 0, rails::kActivator, 1 | 8);
  const u32 id = s.spawnCart(CartType::Normal, {2, kY, 0});
  tickN(s, 1);
  rightClick(s, above(*s.cartById(id)));
  REQUIRE(p.mounted());
  s.cartById(id)->motion.x = 0.3;
  TickInput in = testing::idleTick();
  tickN(s, 25, in);
  CHECK_FALSE(p.mounted());
}

TEST_CASE("Kilómetro de ruta: montado a más de 1000 bloques de donde se subió") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {2.5, kY, 2.5};
  line(fw, 0, 12);
  const u32 id = s.spawnCart(CartType::Normal, {2, kY, 0});
  tickN(s, 1);
  rightClick(s, above(*s.cartById(id)));
  REQUIRE(p.mounted());
  for (Ach a : {Ach::OpenInventory, Ach::MineWood, Ach::BuildWorkBench, Ach::BuildPickaxe, Ach::BuildFurnace, Ach::AcquireIron}) s.award(a);
  CHECK_FALSE(s.achievements().has(Ach::OnARail));
  tickN(s, 3);
  CHECK_FALSE(s.achievements().has(Ach::OnARail));
  p.cartStart = glm::ivec3(2 + 1200, kY, 0);  // como si se hubiera subido muy lejos
  tickN(s, 2);
  CHECK(s.achievements().has(Ach::OnARail));
}

TEST_CASE("Raíl en cuesta: si se quita el bloque del lado alto, se cae") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.player().pos = s.player().prevPos = {0.5, kY, 6.5};
  put(fw, 1, kY, 0, B::stone);
  put(fw, 0, kY, 0, rails::kRail, 2);   // sube al este
  put(fw, 1, kY + 1, 0, rails::kRail, 1);
  s.placeBlock({3, kY, 0}, makeState(B::stone));  // (una actualización cualquiera: todo sigue)
  CHECK(stateId(fw.w.block(0, kY, 0)) == rails::kRail);
  s.placeBlock({1, kY, 0}, 0);  // se quita el bloque de la cima
  CHECK(stateId(fw.w.block(0, kY, 0)) == 0);
  CHECK(stateId(fw.w.block(1, kY + 1, 0)) == 0);
}

TEST_CASE("Vagonetas: se quitan con el chunk y vuelven con su contenido") {
  FlatTestWorld fw;
  GameSession s(fw, 7);
  s.player().pos = s.player().prevPos = {0.5, kY, 6.5};
  line(fw, 0, 8);
  const u32 chest = s.spawnCart(CartType::Chest, {2, kY, 0});
  s.cartItems(chest)[3] = ItemStack(ItemId::gold_ingot, 9);
  const u32 furnace = s.spawnCart(CartType::Furnace, {4, kY, 0});
  s.cartById(furnace)->fuel = 1234;
  auto saved = s.cartsInChunk(0, 0, true);
  CHECK(saved.size() == 2);
  CHECK(s.carts().empty());
  for (auto& [cart, contents] : saved) s.addCart(cart, &contents);
  REQUIRE(s.carts().size() == 2);
  bool foundChest = false, foundFurnace = false;
  for (const Minecart& c : s.carts()) {
    if (c.type == CartType::Chest) foundChest = s.cartItems(c.id)[3].id == ItemId::gold_ingot && s.cartItems(c.id)[3].count == 9;
    if (c.type == CartType::Furnace) foundFurnace = c.fuel == 1234;
  }
  CHECK(foundChest);
  CHECK(foundFurnace);
}
