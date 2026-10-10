#include <doctest/doctest.h>

#include "data/items.h"
#include "flat_world.h"
#include "game/anvil.h"
#include "game/enchantments.h"
#include "game/menu.h"
#include "game/player.h"
#include "game/session.h"

using namespace mcw;

namespace {

ItemStack withRepairCost(ItemStack s, int cost) {
  ItemExtra e = s.copyExtra();
  e.repairCost = cost;
  s.setExtra(std::move(e));
  return s;
}

ItemStack book(std::initializer_list<std::pair<int, int>> stored) {
  ItemStack b(ItemId::enchanted_book);
  ItemExtra e;
  for (const auto& [id, level] : stored) e.stored.emplace_back(static_cast<i16>(id), static_cast<i16>(level));
  b.setExtra(e);
  return b;
}

int repairCostOf(const ItemStack& s) { return s.extra ? s.extra->repairCost : 0; }

}  // namespace

TEST_CASE("Yunque: solo renombrar cuesta 1 nivel y no encarece el objeto") {
  const ItemStack sword(ItemId::iron_sword);
  const AnvilResult r = anvilCompute(sword, ItemStack(), "Mi espada", false);
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.cost == 1);
  CHECK(r.output.extra->name == "Mi espada");
  CHECK(repairCostOf(r.output) == 0);
  // Con el nombre de siempre (o vacío) y sin nada más, no hay resultado
  CHECK(anvilCompute(sword, ItemStack(), "", false).output.empty());
  CHECK(anvilCompute(sword, ItemStack(), anvilDisplayName(sword), false).output.empty());
  // Quitar el nombre puesto también cuesta 1
  const AnvilResult back = anvilCompute(r.output, ItemStack(), "", false);
  REQUIRE_FALSE(back.output.empty());
  CHECK(back.cost == 1);
  CHECK((back.output.extra == nullptr || back.output.extra->name.empty()));
  // Y sin nada a la izquierda no hay nada
  CHECK(anvilCompute(ItemStack(), ItemStack(ItemId::iron_ingot), "x", false).output.empty());
}

TEST_CASE("Yunque: reparar con material devuelve un cuarto por unidad y gasta lo justo") {
  const int max = itemInfo(ItemId::iron_pickaxe).maxDurability;
  ItemStack pick(ItemId::iron_pickaxe, 1, 100);  // 100 de desgaste
  AnvilResult r = anvilCompute(pick, ItemStack(ItemId::iron_ingot, 1), "", false);
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.output.meta == 100 - max / 4);  // un lingote quita un cuarto de la durabilidad total
  CHECK(r.cost == 1);
  CHECK(r.materialUsed == 1);
  CHECK(repairCostOf(r.output) == 1);  // (0 * 2 + 1)
  // Con varios solo se gastan los que hacen falta: aquí dos (el segundo remata lo que queda)
  r = anvilCompute(pick, ItemStack(ItemId::iron_ingot, 5), "", false);
  CHECK(r.output.meta == 0);
  CHECK(r.materialUsed == 2);
  CHECK(r.cost == 2);
  // Muy estropeada (le quedan 5 usos): hacen falta cuatro lingotes para dejarla nueva
  pick.meta = static_cast<i16>(max - 5);
  r = anvilCompute(pick, ItemStack(ItemId::iron_ingot, 64), "", false);
  CHECK(r.output.meta == 0);
  CHECK(r.materialUsed == 4);
  CHECK(r.cost == 4);
  // Sin daño no hay nada que reparar
  CHECK(anvilCompute(ItemStack(ItemId::iron_pickaxe), ItemStack(ItemId::iron_ingot, 5), "", false).output.empty());
  // El material equivocado no sirve
  CHECK(anvilCompute(pick, ItemStack(ItemId::diamond), "", false).output.empty());
  // Los tablones reparan la madera, el diamante el diamante, el cuero el cuero
  CHECK_FALSE(anvilCompute(ItemStack(ItemId::wooden_sword, 1, 10), ItemStack(5, 1), "", false).output.empty());
  CHECK_FALSE(anvilCompute(ItemStack(ItemId::diamond_chestplate, 1, 10), ItemStack(ItemId::diamond), "", false).output.empty());
  CHECK_FALSE(anvilCompute(ItemStack(ItemId::leather_boots, 1, 5), ItemStack(ItemId::leather), "", false).output.empty());
  CHECK_FALSE(anvilCompute(ItemStack(ItemId::chainmail_helmet, 1, 5), ItemStack(ItemId::iron_ingot), "", false).output.empty());
  CHECK(anvilCompute(ItemStack(ItemId::bow, 1, 5), ItemStack(ItemId::stick), "", false).output.empty());  // el arco no se repara con material
}

TEST_CASE("Yunque: juntar dos objetos iguales suma lo que les queda y un 12 % más") {
  const int max = itemInfo(ItemId::iron_pickaxe).maxDurability;
  const ItemStack a(ItemId::iron_pickaxe, 1, max - 50), b(ItemId::iron_pickaxe, 1, max - 50);
  const AnvilResult r = anvilCompute(a, b, "", false);
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.output.meta == max - (50 + 50 + max * 12 / 100));
  CHECK(r.cost == 2);
  CHECK(repairCostOf(r.output) == 1);
  // Dos objetos distintos no se juntan
  CHECK(anvilCompute(a, ItemStack(ItemId::iron_sword, 1, 10), "", false).output.empty());
  // Ni lo que no tiene durabilidad
  CHECK(anvilCompute(ItemStack(ItemId::stick), ItemStack(ItemId::stick), "", false).output.empty());
}

TEST_CASE("Yunque: un libro encantado pasa sus encantamientos y el precio depende de lo raros que son") {
  const ItemStack sword(ItemId::iron_sword);
  AnvilResult r = anvilCompute(sword, book({{Ench::Sharpness, 3}}), "", false);  // peso 10: 1 por nivel (libro: mitad, mínimo 1)
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.output.enchantLevel(Ench::Sharpness) == 3);
  CHECK(r.cost == 3);
  CHECK(repairCostOf(r.output) == 1);
  r = anvilCompute(sword, book({{Ench::FireAspect, 2}}), "", false);  // peso 2: 4, mitad 2 por nivel
  CHECK(r.output.enchantLevel(Ench::FireAspect) == 2);
  CHECK(r.cost == 4);
  r = anvilCompute(sword, book({{Ench::Knockback, 2}}), "", false);  // peso 5: 2, mitad 1 por nivel
  CHECK(r.cost == 2);
  // Un encantamiento que no sirve para ese objeto no se aplica
  r = anvilCompute(sword, book({{Ench::Efficiency, 3}}), "", false);
  CHECK(r.output.empty());
  // Dos libros del mismo nivel suben uno; el máximo no se pasa
  r = anvilCompute(book({{Ench::Sharpness, 2}}), book({{Ench::Sharpness, 2}}), "", false);
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.output.id == ItemId::enchanted_book);
  CHECK(anvilEnchants(r.output).front().second == 3);
  r = anvilCompute(book({{Ench::Sharpness, 5}}), book({{Ench::Sharpness, 5}}), "", false);
  CHECK(anvilEnchants(r.output).front().second == 5);
  // De niveles distintos se queda el mayor
  r = anvilCompute(book({{Ench::Sharpness, 2}}), book({{Ench::Sharpness, 4}}), "", false);
  CHECK(anvilEnchants(r.output).front().second == 4);
  // Un objeto ya encantado conserva lo que llevaba
  ItemStack enchanted = sword;
  enchanted.addEnchant(Ench::Unbreaking, 2);
  r = anvilCompute(enchanted, book({{Ench::Sharpness, 1}}), "", false);
  CHECK(r.output.enchantLevel(Ench::Unbreaking) == 2);
  CHECK(r.output.enchantLevel(Ench::Sharpness) == 1);
}

TEST_CASE("Yunque: los encantamientos incompatibles cuestan 1 y no se aplican") {
  ItemStack sword(ItemId::iron_sword);
  sword.addEnchant(Ench::Sharpness, 2);
  const AnvilResult r = anvilCompute(sword, book({{Ench::Smite, 3}}), "", false);
  REQUIRE_FALSE(r.output.empty());  // (como en 1.8: se paga ese nivel y solo sube la penitencia)
  CHECK(r.cost == 1);
  CHECK(r.output.enchantLevel(Ench::Smite) == 0);
  CHECK(r.output.enchantLevel(Ench::Sharpness) == 2);
}

TEST_CASE("Yunque: 40 niveles o más es demasiado caro (salvo en creativo) y renombrar nunca pasa de 39") {
  const ItemStack worn = withRepairCost(ItemStack(ItemId::iron_sword), 63);
  AnvilResult r = anvilCompute(worn, book({{Ench::Sharpness, 3}}), "", false);
  CHECK(r.output.empty());
  CHECK(r.tooExpensive);
  CHECK(r.cost >= 40);
  r = anvilCompute(worn, book({{Ench::Sharpness, 3}}), "", true);
  CHECK_FALSE(r.output.empty());  // en creativo vale
  // Solo renombrar: aunque la penitencia pase de 40, se queda en 39
  r = anvilCompute(worn, ItemStack(), "Reliquia", false);
  REQUIRE_FALSE(r.output.empty());
  CHECK(r.cost == 39);
  CHECK(repairCostOf(r.output) == 63);  // y no la encarece
  // Cada uso duplica la penitencia: 0, 1, 3, 7, 15, 31, 63
  ItemStack s(ItemId::iron_pickaxe, 1, 100);
  int expected = 1;
  for (int i = 0; i < 5; i++) {
    r = anvilCompute(s, ItemStack(ItemId::iron_ingot), "", true);
    REQUIRE_FALSE(r.output.empty());
    CHECK(repairCostOf(r.output) == expected);
    expected = expected * 2 + 1;
    s = r.output;
    s.meta = 100;  // (se vuelve a estropear para repetir)
  }
}

TEST_CASE("Yunque: el desgaste del bloque pasa de intacto a algo dañado, muy dañado y roto") {
  CHECK(anvilNextDamage(0) == 1);
  CHECK(anvilNextDamage(1) == 2);
  CHECK(anvilNextDamage(2) == -1);
}

TEST_CASE("Yunque (ventana): renombrar con clics, sin niveles no se puede y con ellos se gastan") {
  Player p;
  p.xpLevel = 0;
  Menu m(MenuKind::Anvil, p);
  p.cursor = ItemStack(ItemId::iron_sword);
  m.click(0, 0, false);  // la espada a la casilla de la izquierda
  REQUIRE(m.anvilSlot(0).id == ItemId::iron_sword);
  CHECK(m.anvilName() == anvilDisplayName(ItemStack(ItemId::iron_sword)));  // el campo se rellena con su nombre
  CHECK(m.anvilOutput().empty());                                              // y sin cambios no hay resultado
  m.setAnvilName("Mi espada");
  REQUIRE_FALSE(m.anvilOutput().empty());
  CHECK(m.anvilCost() == 1);
  CHECK_FALSE(m.anvilCanTake());  // no tiene niveles
  m.click(2, 0, false);
  CHECK(p.cursor.empty());
  CHECK(m.anvilSlot(0).id == ItemId::iron_sword);  // todo sigue donde estaba
  p.xpLevel = 3;
  CHECK(m.anvilCanTake());
  m.click(2, 0, false);
  REQUIRE_FALSE(p.cursor.empty());
  CHECK(p.cursor.extra->name == "Mi espada");
  CHECK(p.xpLevel == 2);
  CHECK(m.anvilSlot(0).empty());
  CHECK(m.anvilOutput().empty());
  CHECK(m.takeAnvilUses() == 1);
  CHECK(m.takeAnvilUses() == 0);
  // Con la mano ocupada no se puede llevarse el resultado
  p.cursor = ItemStack(ItemId::iron_sword);
  m.click(0, 0, false);
  m.setAnvilName("Otra");
  p.cursor = ItemStack(B::stone, 3);
  m.click(2, 0, false);
  CHECK(p.xpLevel == 2);
  CHECK(m.anvilSlot(0).id == ItemId::iron_sword);
}

TEST_CASE("Yunque (ventana): reparar gasta solo el material necesario y cerrar devuelve lo que hubiera dentro") {
  Player p;
  p.xpLevel = 10;
  Menu m(MenuKind::Anvil, p);
  m.anvilSlot(0) = ItemStack(ItemId::iron_pickaxe, 1, 100);
  m.anvilSlot(1) = ItemStack(ItemId::iron_ingot, 5);
  m.click(0, 0, false);  // (un clic cualquiera sobre la casilla recalcula)
  m.click(0, 0, false);
  REQUIRE_FALSE(m.anvilOutput().empty());
  CHECK(m.anvilCost() == 2);
  m.click(2, 0, false);  // se lleva la reparada
  CHECK(p.cursor.id == ItemId::iron_pickaxe);
  CHECK(p.cursor.meta == 0);
  CHECK(p.xpLevel == 8);
  CHECK(m.anvilSlot(1).count == 3);  // gastó 2 lingotes de 5
  // Cerrar devuelve el material al inventario
  std::vector<ItemStack> dropped;
  m.close(dropped);
  int ingots = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (p.inventory.slot(i).id == ItemId::iron_ingot) ingots += p.inventory.slot(i).count;
  CHECK(ingots == 3);
  CHECK(m.anvilSlot(1).empty());
  CHECK(dropped.empty());
}

TEST_CASE("Yunque (ventana): en creativo no cuesta niveles, y mayús lleva objetos del inventario a las casillas") {
  Player p;
  p.mode = GameMode::Creative;
  Menu m(MenuKind::Anvil, p);
  p.inventory.slot(0) = ItemStack(ItemId::iron_sword);
  p.inventory.slot(1) = ItemStack(ItemId::enchanted_book);
  {
    ItemExtra e;
    e.stored = {{Ench::Sharpness, 5}};
    ItemStack b = p.inventory.slot(1);
    b.setExtra(e);
    p.inventory.slot(1) = b;
  }
  // La casilla del inventario 0 es la 3 + 27 de la barra (primero van las 3 del yunque y luego el inventario)
  m.click(3 + 27 + 0, 0, true);
  m.click(3 + 27 + 1, 0, true);
  CHECK(m.anvilSlot(0).id == ItemId::iron_sword);
  CHECK(m.anvilSlot(1).id == ItemId::enchanted_book);
  REQUIRE_FALSE(m.anvilOutput().empty());
  CHECK(m.anvilOutput().enchantLevel(Ench::Sharpness) == 5);
  const int levels = p.xpLevel;
  m.click(2, 0, true);  // con mayús, directo al inventario
  CHECK(p.xpLevel == levels);  // (creativo: gratis)
  bool found = false;
  for (int i = 0; i < PlayerInventory::kSize; i++) found |= p.inventory.slot(i).id == ItemId::iron_sword && p.inventory.slot(i).enchantLevel(Ench::Sharpness) == 5;
  CHECK(found);
  CHECK(m.anvilSlot(0).empty());
  CHECK(m.anvilSlot(1).empty());
}

TEST_CASE("Yunque (partida): se abre al usarlo, se desgasta de vez en cuando y acaba rompiéndose") {
  testing::FlatTestWorld fw;
  GameSession s(fw, 5);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  fw.setBlock(0, 64, -2, makeState(145, 0));  // un yunque a dos bloques, mirando al norte
  s.tick(testing::idleTick());
  TickInput use = testing::idleTick();
  use.aimDir = glm::normalize(glm::dvec3(0.5, 64.5, -1.5) - p.eyePos());
  use.use = use.usePressed = true;
  s.tick(use);
  use.use = use.usePressed = false;
  s.tick(use);
  REQUIRE(s.menu() != nullptr);
  CHECK(s.menu()->kind() == MenuKind::Anvil);
  // Cada resultado sacado puede desgastarlo (12 %): tras muchos usos pasa por los tres estados y se rompe
  int maxDamage = 0;
  bool broke = false;
  for (int i = 0; i < 4000 && !broke; i++) {
    if (!s.menu()) break;
    s.menu()->anvilSlot(0) = ItemStack(ItemId::iron_sword);
    s.menu()->setAnvilName("x" + std::to_string(i % 7));  // (cualquier cambio vale)
    s.menu()->anvilSlot(0).clear();
    broke = s.wearAnvil({0, 64, -2}, false);
    const BlockState st = fw.w.block(0, 64, -2);
    if (stateId(st) == 145) maxDamage = std::max(maxDamage, stateMeta(st) >> 2);
  }
  CHECK(broke);
  CHECK(maxDamage == 2);
  CHECK(stateId(fw.w.block(0, 64, -2)) == B::air);
  // En creativo no se desgasta nunca
  fw.setBlock(0, 64, -2, makeState(145, 0));
  for (int i = 0; i < 500; i++) CHECK_FALSE(s.wearAnvil({0, 64, -2}, true));
  CHECK((stateMeta(fw.w.block(0, 64, -2)) >> 2) == 0);
}
