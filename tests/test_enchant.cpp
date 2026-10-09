#include <doctest/doctest.h>

#include "game/enchantments.h"
#include "game/item_stack.h"
#include "net/protocol.h"
#include "save/anvil.h"

using namespace mcw;

namespace {

ItemStack enchantedSword() {
  ItemStack s(ItemId::diamond_sword);
  ItemExtra e;
  e.ench = {{Ench::Sharpness, 3}, {Ench::Looting, 2}};
  e.name = "Mi espada";
  e.lore = {"Línea 1", "Línea 2"};
  e.repairCost = 3;
  s.setExtra(std::move(e));
  return s;
}

}  // namespace

TEST_CASE("Objetos con etiquetas: igualdad, apilado y copia barata") {
  ItemStack a(ItemId::enchanted_book), b(ItemId::enchanted_book);
  CHECK(a == b);
  ItemExtra e;
  e.stored = {{Ench::Protection, 2}};
  a.setExtra(e);
  CHECK_FALSE(a == b);
  CHECK_FALSE(a.stacksWith(b));  // con y sin etiqueta no se juntan
  b.setExtra(e);
  CHECK(a == b);                 // mismas etiquetas aunque sean punteros distintos
  CHECK(a.extra.get() != b.extra.get());  // (con .get(): MSVC no sabe imprimir un shared_ptr en un CHECK)
  ItemStack tool(ItemId::wooden_pickaxe);
  tool.addEnchant(Ench::Efficiency, 2);
  CHECK(tool.hasEnchants());
  CHECK(tool.enchantLevel(Ench::Efficiency) == 2);
  CHECK(tool.enchantLevel(Ench::Unbreaking) == 0);
  tool.addEnchant(Ench::Efficiency, 4);  // sube el nivel, no repite
  CHECK(tool.extra->ench.size() == 1);
  CHECK(tool.enchantLevel(Ench::Efficiency) == 4);
  // Copiar una pila no copia las etiquetas
  const ItemStack copy = tool;
  CHECK(copy.extra.get() == tool.extra.get());
  // Una etiqueta vacía no cuenta
  ItemStack plain(ItemId::apple);
  plain.setExtra(ItemExtra{});
  CHECK(plain.extra.get() == nullptr);
  CHECK(plain == ItemStack(ItemId::apple));
}

TEST_CASE("Etiquetas de objeto: NBT de 1.8 y paquete de ranura de ida y vuelta") {
  const ItemStack sword = enchantedSword();
  // NBT: tag { ench:[{id,lvl}], display:{Name,Lore}, RepairCost }
  const nbt::Value tag = save::stackToNbt(sword, 3);
  const nbt::Value* t = tag.getCompound("tag");
  REQUIRE(t);
  REQUIRE(t->getList("ench"));
  CHECK(t->getList("ench")->items().size() == 2);
  CHECK(t->getList("ench")->items()[0].getInt("id") == Ench::Sharpness);
  CHECK(t->getList("ench")->items()[0].getInt("lvl") == 3);
  CHECK(t->getCompound("display")->getString("Name") == "Mi espada");
  CHECK(t->getInt("RepairCost") == 3);
  const ItemStack back = save::stackFromNbt(tag);
  CHECK(back == sword);
  CHECK(back.extra->lore.size() == 2);

  // Un cuero teñido y un libro con encantamientos
  ItemStack boots(ItemId::leather_boots);
  ItemExtra be;
  be.color = 0x336699;
  boots.setExtra(be);
  CHECK(save::stackFromNbt(save::stackToNbt(boots)).extra->color == 0x336699);
  ItemStack book(ItemId::enchanted_book);
  ItemExtra ke;
  ke.stored = {{Ench::Fortune, 3}};
  book.setExtra(ke);
  CHECK(save::stackFromNbt(save::stackToNbt(book)) == book);

  // Sin etiquetas el NBT no lleva "tag"
  CHECK_FALSE(save::stackToNbt(ItemStack(ItemId::apple, 3)).has("tag"));

  // Paquete de ranura de 1.8: id, cantidad, daño y NBT
  BufferWriter w;
  net::writeSlot(w, sword);
  net::writeSlot(w, ItemStack(ItemId::apple, 3));
  net::writeSlot(w, book);
  BufferReader r(w.data());
  CHECK(net::readSlot(r) == sword);
  CHECK(net::readSlot(r) == ItemStack(ItemId::apple, 3));
  CHECK(net::readSlot(r) == book);
  CHECK(r.remaining() == 0);
}

TEST_CASE("Encantamientos: los 25 de 1.8, sus nombres, niveles y rangos") {
  CHECK(allEnchantments().size() == 25);
  CHECK(enchantInfo(Ench::Sharpness)->maxLevel == 5);
  CHECK(enchantInfo(Ench::Sharpness)->weight == 10);
  CHECK(enchantInfo(99) == nullptr);
  CHECK(enchantDisplayName(Ench::Sharpness, 3) == "Filo III");
  CHECK(enchantDisplayName(Ench::SilkTouch, 1) == "Toque de seda");  // sin nivel si solo hay uno
  CHECK(enchantDisplayName(Ench::Protection, 4) == "Protección IV");
  CHECK(romanNumeral(9) == "IX");
  CHECK(enchantByName("sharpness")->id == Ench::Sharpness);
  CHECK(enchantByName("minecraft:fortune")->id == Ench::Fortune);
  CHECK(enchantByName("32")->id == Ench::Efficiency);
  CHECK(enchantByName("nada") == nullptr);
  // Energía mínima y máxima de cada nivel (mesa de encantamientos)
  const EnchantInfo& sharp = *enchantInfo(Ench::Sharpness);
  CHECK(sharp.minEnchantability(1) == 1);
  CHECK(sharp.maxEnchantability(1) == 21);
  CHECK(sharp.minEnchantability(5) == 45);
  CHECK(sharp.maxEnchantability(5) == 65);
  CHECK(enchantInfo(Ench::SilkTouch)->minEnchantability(1) == 15);
  CHECK(enchantInfo(Ench::SilkTouch)->maxEnchantability(1) == 65);
  CHECK(enchantInfo(Ench::AquaAffinity)->maxEnchantability(1) == 41);
  CHECK(enchantInfo(Ench::Respiration)->minEnchantability(3) == 30);
  CHECK(enchantInfo(Ench::Thorns)->minEnchantability(3) == 50);
  CHECK(enchantInfo(Ench::Power)->maxEnchantability(5) == 56);
  // Los ids son únicos y todos tienen nombre
  for (const EnchantInfo& e : allEnchantments()) {
    CHECK(enchantInfo(e.id) == &e);
    CHECK(std::string(e.nameEs).size() > 3);
  }
}

TEST_CASE("Encantamientos: qué objetos los admiten y cuáles son compatibles") {
  const EnchantInfo& sharp = *enchantInfo(Ench::Sharpness);
  CHECK(canEnchant(sharp, ItemId::diamond_sword, true));
  CHECK_FALSE(canEnchant(sharp, ItemId::diamond_pickaxe, true));
  CHECK_FALSE(canEnchant(sharp, ItemId::iron_axe, true));  // el hacha lo admite con yunque, no en la mesa
  CHECK(canEnchant(sharp, ItemId::iron_axe, false));
  CHECK(canEnchant(*enchantInfo(Ench::Efficiency), ItemId::iron_shovel, true));
  CHECK_FALSE(canEnchant(*enchantInfo(Ench::Efficiency), ItemId::iron_hoe, true));
  CHECK(canEnchant(*enchantInfo(Ench::Efficiency), ItemId::shears, false));
  CHECK(canEnchant(*enchantInfo(Ench::Unbreaking), ItemId::iron_hoe, true));  // cualquier cosa con desgaste
  CHECK(canEnchant(*enchantInfo(Ench::Unbreaking), ItemId::leather_boots, true));
  CHECK_FALSE(canEnchant(*enchantInfo(Ench::Unbreaking), ItemId::apple, true));
  CHECK(canEnchant(*enchantInfo(Ench::FeatherFalling), ItemId::iron_boots, true));
  CHECK_FALSE(canEnchant(*enchantInfo(Ench::FeatherFalling), ItemId::iron_helmet, true));
  CHECK(canEnchant(*enchantInfo(Ench::Respiration), ItemId::diamond_helmet, true));
  CHECK(canEnchant(*enchantInfo(Ench::Thorns), ItemId::iron_chestplate, true));
  CHECK_FALSE(canEnchant(*enchantInfo(Ench::Thorns), ItemId::iron_helmet, true));
  CHECK(canEnchant(*enchantInfo(Ench::Thorns), ItemId::iron_helmet, false));
  CHECK(canEnchant(*enchantInfo(Ench::Power), ItemId::bow, true));
  CHECK(canEnchant(*enchantInfo(Ench::Lure), ItemId::fishing_rod, true));
  CHECK(canEnchant(*enchantInfo(Ench::Looting), ItemId::book, true));  // los libros, cualquiera
  // Compatibilidad
  CHECK_FALSE(enchantsCompatible(Ench::Sharpness, Ench::Smite));
  CHECK_FALSE(enchantsCompatible(Ench::Protection, Ench::BlastProtection));
  CHECK(enchantsCompatible(Ench::Protection, Ench::FeatherFalling));
  CHECK(enchantsCompatible(Ench::FireProtection, Ench::FeatherFalling));
  CHECK_FALSE(enchantsCompatible(Ench::SilkTouch, Ench::Fortune));
  CHECK(enchantsCompatible(Ench::Sharpness, Ench::Looting));
  CHECK(enchantsCompatible(Ench::Efficiency, Ench::Unbreaking));
  // Encantabilidad del material
  CHECK(itemEnchantability(ItemId::golden_sword) == 22);
  CHECK(itemEnchantability(ItemId::stone_pickaxe) == 5);
  CHECK(itemEnchantability(ItemId::iron_boots) == 9);
  CHECK(itemEnchantability(ItemId::golden_helmet) == 25);
  CHECK(itemEnchantability(ItemId::diamond_chestplate) == 10);
  CHECK(itemEnchantability(ItemId::book) == 1);
  CHECK(itemEnchantability(ItemId::apple) == 0);
  CHECK(itemEnchantability(ItemId::shears) == 0);
}
