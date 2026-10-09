#include <doctest/doctest.h>

#include "game/enchanting.h"
#include "game/enchantments.h"
#include "game/item_stack.h"
#include "game/menu.h"
#include "game/player.h"
#include "net/protocol.h"
#include "save/anvil.h"
#include "world/light.h"
#include "world/world.h"

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

namespace {

/// Un mundo vacío (aire) de 3x3 chunks.
struct EmptyWorld {
  World w;
  EmptyWorld() {
    ChunkSet mod;
    for (int cz = -1; cz <= 1; cz++)
      for (int cx = -1; cx <= 1; cx++) {
        auto c = std::make_unique<Chunk>(cx, cz);
        c->setBlock(0, 0, 0, makeState(B::bedrock));
        light::computeInitial(*c);
        w.insert(std::move(c), mod);
      }
  }
  void set(int x, int y, int z, int id) {
    ChunkSet mod;
    w.setBlock(x, y, z, makeState(id), mod);
  }
};

}  // namespace

TEST_CASE("Mesa de encantamientos: estanterías del anillo a dos bloques, con el hueco libre") {
  EmptyWorld e;
  e.set(0, 64, 0, 116);  // la mesa
  CHECK(countBookshelves(e.w, 0, 64, 0) == 0);
  // El anillo completo: 16 posiciones a dos alturas (las ofertas solo cuentan hasta 15)
  for (int dz = -2; dz <= 2; dz++)
    for (int dx = -2; dx <= 2; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
      e.set(dx, 64, dz, B::bookshelf);
      e.set(dx, 65, dz, B::bookshelf);
    }
  CHECK(countBookshelves(e.w, 0, 64, 0) == 32);
  // Una piedra en el hueco de al lado tapa las dos estanterías de enfrente
  e.set(1, 64, 0, B::stone);
  CHECK(countBookshelves(e.w, 0, 64, 0) == 30);
  // Y en una esquina interior, las tres posiciones que dependen de ese hueco (a dos alturas)
  e.set(1, 65, 1, B::stone);
  CHECK(countBookshelves(e.w, 0, 64, 0) == 24);
}

TEST_CASE("Mesa de encantamientos: costes según las estanterías y la semilla, y pistas de la lista") {
  const ItemStack pick(ItemId::diamond_pickaxe);
  // Con 15 estanterías la tercera opción cuesta siempre 30; la primera de 2 a 10 y la segunda de 6 a 21
  for (int seed = 1; seed <= 200; seed++) {
    const auto o = enchantOffers(pick, 15, seed * 7919);
    CHECK(o[2].cost == 30);
    CHECK(o[0].cost >= 2);
    CHECK(o[0].cost <= 10);
    CHECK(o[1].cost >= 6);
    CHECK(o[1].cost <= 21);
    for (const EnchantOffer& offer : o) {
      REQUIRE(offer.clueEnchant >= 0);
      // La pista es uno de los encantamientos que saldrán, y valen para un pico
      const auto list = enchantList(pick, static_cast<int>(&offer - &o[0]), offer.cost, seed * 7919);
      bool inList = false;
      for (const auto& [id, level] : list) inList |= id == offer.clueEnchant && level == offer.clueLevel;
      CHECK(inList);
      for (const auto& [id, level] : list) {
        CHECK(canEnchant(*enchantInfo(id), pick.id, true));
        CHECK(level >= 1);
        CHECK(level <= enchantInfo(id)->maxLevel);
      }
    }
  }
  // Sin estanterías: costes bajos y la tercera opción casi nunca llega
  for (int seed = 1; seed <= 200; seed++) {
    const auto o = enchantOffers(pick, 0, seed * 104729);
    CHECK(o[0].cost <= 2);
    CHECK(o[1].cost <= 6);
    CHECK(o[2].cost <= 8);
  }
  // Lo mismo con la misma semilla; con otra, a veces otra cosa
  CHECK(enchantOffers(pick, 7, 12345)[1].cost == enchantOffers(pick, 7, 12345)[1].cost);
  int differs = 0;
  for (int seed = 1; seed <= 50; seed++) differs += enchantOffers(pick, 10, seed)[0].cost != enchantOffers(pick, 10, seed + 1000)[0].cost;
  CHECK(differs > 5);
  // Lo que no se puede encantar no tiene opciones: una manzana, unas tijeras, algo ya encantado
  CHECK(enchantOffers(ItemStack(ItemId::apple), 15, 1)[0].cost == 0);
  CHECK(enchantOffers(ItemStack(ItemId::shears), 15, 1)[0].cost == 0);
  ItemStack done = pick;
  done.addEnchant(Ench::Efficiency, 1);
  CHECK(enchantOffers(done, 15, 1)[0].cost == 0);
  // Un libro sí; el oro encanta más fácil que la piedra (los niveles altos salen antes)
  CHECK(enchantOffers(ItemStack(ItemId::book), 15, 1)[2].cost == 30);
  // Las listas no repiten ni mezclan incompatibles
  for (int seed = 1; seed <= 300; seed++) {
    const auto list = enchantList(ItemStack(ItemId::diamond_chestplate), 2, 30, seed);
    REQUIRE_FALSE(list.empty());
    for (std::size_t i = 0; i < list.size(); i++)
      for (std::size_t j = i + 1; j < list.size(); j++) CHECK(enchantsCompatible(list[i].first, list[j].first));
  }
  // Un libro con varios encantamientos pierde uno al azar
  int books = 0;
  for (int seed = 1; seed <= 300; seed++) {
    const auto full = enchantList(ItemStack(ItemId::diamond_sword), 2, 30, seed);
    const auto book = enchantList(ItemStack(ItemId::book), 2, 30, seed);
    if (full.size() > 2) {
      CHECK(book.size() >= 1);
      books++;
    }
  }
  CHECK(books > 0);
}

TEST_CASE("Mesa de encantamientos: la ventana gasta niveles y lapislázuli y encanta") {
  Player p;
  p.xpLevel = 3;
  p.xpSeed = 4242;
  p.inventory.slot(0) = ItemStack(ItemId::diamond_sword);
  p.inventory.slot(1) = ItemStack(ItemId::dye, 5, 4);  // lapislázuli
  p.inventory.slot(2) = ItemStack(ItemId::apple, 3);
  Menu m(MenuKind::Enchant, p, nullptr, nullptr, 15);
  REQUIRE(m.slots().size() == 38);
  CHECK(m.slots()[0].role == SlotRole::EnchantItem);
  CHECK(m.slots()[1].role == SlotRole::EnchantLapis);
  // La espada a la casilla del objeto: solo una, y el lapislázuli a la suya (mayús)
  m.click(2 + 27 + 0, 0, true);  // barra, casilla 0 = la espada
  CHECK(m.slots()[0].stack->id == ItemId::diamond_sword);
  m.click(2 + 27 + 1, 0, true);  // el lapislázuli
  CHECK(m.slots()[1].stack->count == 5);
  // Una manzana no entra en la casilla del lapislázuli (ni cursor ni mayús)
  p.cursor = ItemStack(ItemId::apple, 2);
  m.click(1, 0, false);
  CHECK(m.slots()[1].stack->count == 5);
  CHECK(p.cursor.count == 2);
  p.cursor.clear();
  const auto offers = m.offers();
  REQUIRE(offers[0].cost > 0);
  REQUIRE(offers[2].cost == 30);
  // Sin niveles suficientes para la tercera opción (pide 3 niveles y 30 de nivel): no pasa nada
  CHECK_FALSE(m.enchant(2));
  CHECK(p.xpLevel == 3);
  // La primera cuesta 1 nivel y 1 lapislázuli si se tienen los niveles que pide (con 15 estanterías pide de 2 a 10)
  p.xpLevel = offers[0].cost;
  const int seedBefore = p.xpSeed;
  REQUIRE(m.enchant(0));
  CHECK(p.xpLevel == offers[0].cost - 1);
  CHECK(m.slots()[1].stack->count == 4);
  CHECK(m.slots()[0].stack->hasEnchants());
  CHECK(p.xpSeed != seedBefore);  // las ofertas cambian
  CHECK(p.enchanted == 1);
  // Ya encantada no ofrece nada
  CHECK(m.offers()[0].cost == 0);
  CHECK_FALSE(m.enchant(1));
  // Cerrar devuelve el objeto y el lapislázuli al inventario
  std::vector<ItemStack> dropped;
  m.close(dropped);
  CHECK(dropped.empty());
  bool sword = false;
  int lapis = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++) {
    if (p.inventory.slot(i).id == ItemId::diamond_sword) sword = p.inventory.slot(i).hasEnchants();
    if (p.inventory.slot(i).id == ItemId::dye) lapis += p.inventory.slot(i).count;
  }
  CHECK(sword);
  CHECK(lapis == 4);

  // Un libro se vuelve libro encantado con StoredEnchantments; en creativo no gasta nada
  Player c;
  c.mode = GameMode::Creative;
  c.xpSeed = 99;
  Menu cm(MenuKind::Enchant, c, nullptr, nullptr, 10);
  c.cursor = ItemStack(ItemId::book);
  cm.click(0, 0, false);
  REQUIRE(cm.offers()[2].cost > 0);
  REQUIRE(cm.enchant(2));
  CHECK(cm.slots()[0].stack->id == ItemId::enchanted_book);
  CHECK_FALSE(cm.slots()[0].stack->extra->stored.empty());
  CHECK(cm.slots()[0].stack->extra->ench.empty());
  CHECK(c.xpLevel == 0);
}
