#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>

#include "game/creative_tabs.h"
#include "game/enchantments.h"
#include "game/menu.h"
#include "game/player.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

using namespace mcw;

namespace {

constexpr CreativeTab kItemTabs[] = {CreativeTab::Blocks, CreativeTab::Decoration, CreativeTab::Redstone, CreativeTab::Transport,
                                     CreativeTab::Misc,   CreativeTab::Food,       CreativeTab::Tools,    CreativeTab::Combat,
                                     CreativeTab::Brewing, CreativeTab::Materials};

/// Clave de una pila para contarla (id, variante y encantamientos que guarda).
std::string key(const ItemStack& s) {
  std::string k = std::to_string(s.id) + ":" + std::to_string(s.meta);
  if (s.extra)
    for (const auto& [id, level] : s.extra->stored) k += "/" + std::to_string(id) + "." + std::to_string(level);
  return k;
}

Player creativePlayer() {
  Player p;
  p.mode = GameMode::Creative;
  return p;
}

/// Casilla del menú con ese ítem de la rejilla (primera con ese id y variante).
int slotWith(const Menu& m, int id, int meta = 0) {
  for (int i = 0; i < static_cast<int>(m.slots().size()); i++) {
    const MenuSlot& s = m.slots()[static_cast<std::size_t>(i)];
    if (s.role == SlotRole::Source && s.stack->id == id && s.stack->meta == meta) return i;
  }
  return -1;
}
int slotOfRole(const Menu& m, SlotRole role, int nth = 0) {
  for (int i = 0; i < static_cast<int>(m.slots().size()); i++)
    if (m.slots()[static_cast<std::size_t>(i)].role == role && nth-- == 0) return i;
  return -1;
}
/// Casilla del menú que enseña la casilla `index` del inventario del jugador.
int hotbarSlot(const Menu& m, int index) {
  for (int i = 0; i < static_cast<int>(m.slots().size()); i++)
    if (m.slots()[static_cast<std::size_t>(i)].inventoryIndex == index && m.slots()[static_cast<std::size_t>(i)].role == SlotRole::Storage) return i;
  return -1;
}

}  // namespace

TEST_CASE("Creativo: cada objeto está en una pestaña y solo en una") {
  std::map<std::string, int> count;
  for (const CreativeTab t : kItemTabs)
    for (const ItemStack& s : creativeTabItems(t))
      if (!s.empty()) count[key(s)]++;
  CHECK(creativeUnclassified().empty());  // (si falla, un objeto nuevo aún no tiene pestaña: ponerlo en creative_tabs.cpp)
  for (const ItemStack& s : creativeItems()) CHECK_MESSAGE(count[key(s)] == 1, "objeto sin pestaña o repetido: " << itemInfo(s.id).name << ":" << s.meta);
  std::size_t total = 0;
  for (const auto& [k, n] : count) total += static_cast<std::size_t>(n);
  CHECK(total == creativeItems().size());
}

TEST_CASE("Creativo: las listas acaban en un objeto, no tienen filas vacías y se repiten igual") {
  for (const CreativeTab t : kItemTabs) {
    const auto& list = creativeTabItems(t);
    REQUIRE_FALSE(list.empty());
    CHECK_FALSE(list.back().empty());
    for (std::size_t row = 0; row * kCreativeColumns < list.size(); row++) {
      bool any = false;
      for (std::size_t c = 0; c < kCreativeColumns && row * kCreativeColumns + c < list.size(); c++) any |= !list[row * kCreativeColumns + c].empty();
      CHECK_MESSAGE(any, "fila vacía en la pestaña " << creativeTabName(t));
    }
    CHECK(&creativeTabItems(t) == &list);  // (siempre la misma lista)
  }
  CHECK(creativeTabItems(CreativeTab::Inventory).empty());
}

TEST_CASE("Creativo: los grupos empiezan en fila nueva (herramientas y armaduras)") {
  const auto& tools = creativeTabItems(CreativeTab::Tools);
  // Picos, hachas, palas y azadas: una fila cada tipo
  CHECK(tools[0].id == ItemId::wooden_pickaxe);
  CHECK(tools[4].id == ItemId::diamond_pickaxe);
  CHECK(tools[kCreativeColumns].id == ItemId::wooden_axe);
  CHECK(tools[2 * kCreativeColumns].id == ItemId::wooden_shovel);
  CHECK(tools[3 * kCreativeColumns].id == ItemId::wooden_hoe);
  const auto& combat = creativeTabItems(CreativeTab::Combat);
  CHECK(combat[0].id == ItemId::wooden_sword);
  CHECK(combat[kCreativeColumns].id == ItemId::leather_helmet);
  CHECK(combat[kCreativeColumns + 3].id == ItemId::leather_boots);
  CHECK(combat[2 * kCreativeColumns].id == ItemId::chainmail_helmet);
}

TEST_CASE("Creativo: los libros encantados van a la pestaña de lo que encantan") {
  auto has = [](CreativeTab t, int enchant) {
    for (const ItemStack& s : creativeTabItems(t))
      if (s.id == ItemId::enchanted_book && s.extra && !s.extra->stored.empty() && s.extra->stored.front().first == enchant) return true;
    return false;
  };
  CHECK(has(CreativeTab::Combat, Ench::Sharpness));
  CHECK(has(CreativeTab::Combat, Ench::Protection));
  CHECK(has(CreativeTab::Combat, Ench::Power));
  CHECK(has(CreativeTab::Tools, Ench::Efficiency));
  CHECK(has(CreativeTab::Tools, Ench::Unbreaking));
  CHECK(has(CreativeTab::Tools, Ench::LuckOfTheSea));
  CHECK_FALSE(has(CreativeTab::Tools, Ench::Sharpness));
  // Un libro por cada encantamiento y nivel: 25 encantamientos
  int books = 0;
  for (const CreativeTab t : kItemTabs)
    for (const ItemStack& s : creativeTabItems(t)) books += s.id == ItemId::enchanted_book;
  int expected = 0;
  for (const EnchantInfo& e : allEnchantments()) expected += e.maxLevel;
  CHECK(books == expected);
}

TEST_CASE("Creativo: la búsqueda lo trae todo, sin huecos y en el orden de las pestañas") {
  const auto& all = creativeTabItems(CreativeTab::Search);
  CHECK(all.size() == creativeItems().size());
  for (const ItemStack& s : all) CHECK_FALSE(s.empty());
  CHECK(creativeSearch("").size() == all.size());
  CHECK(creativeSearch("   ").size() == all.size());
  CHECK(all.front().id == creativeTabItems(CreativeTab::Blocks).front().id);
}

TEST_CASE("Creativo: buscar por nombre, sin mayúsculas ni tildes, en español e inglés") {
  auto ids = [](const std::vector<ItemStack>& v) {
    std::vector<int> r;
    for (const ItemStack& s : v) r.push_back(s.id);
    return r;
  };
  const auto picos = creativeSearch("pico");
  REQUIRE(picos.size() == 5);
  CHECK(ids(picos) == std::vector<int>{ItemId::wooden_pickaxe, ItemId::stone_pickaxe, ItemId::iron_pickaxe, ItemId::golden_pickaxe, ItemId::diamond_pickaxe});
  CHECK(creativeSearch("PICO").size() == 5);
  const auto diamante = creativeSearch("pico diam");  // todas las palabras, aunque a medias
  REQUIRE(diamante.size() == 1);
  CHECK(diamante[0].id == ItemId::diamond_pickaxe);
  CHECK(creativeSearch("diamante pico").size() == 1);  // en cualquier orden
  // Sin tildes: "Lapislázuli" se encuentra con "lapislazuli"; el inglés también vale
  CHECK_FALSE(creativeSearch("lapislazuli").empty());
  CHECK_FALSE(creativeSearch("LAPISLÁZULI").empty());
  const auto english = creativeSearch("diamond pickaxe");
  REQUIRE(english.size() == 1);
  CHECK(english[0].id == ItemId::diamond_pickaxe);
  // El nombre interno ("golden_apple") también
  CHECK_FALSE(creativeSearch("golden apple").empty());
  CHECK(creativeSearch("xyzzy").empty());
  CHECK(creativeSearch("pico xyzzy").empty());
  // Los libros se encuentran por lo que encantan, en español (con o sin tilde) y en inglés
  CHECK(creativeSearch("filo").size() == 5);
  CHECK(creativeSearch("sharpness").size() == 5);
  CHECK(creativeSearch("irrompibilidad").size() == 3);
  CHECK(creativeSearch("proteccion").size() > 4);
  for (const ItemStack& b : creativeSearch("filo")) CHECK(b.id == ItemId::enchanted_book);
}

TEST_CASE("Creativo: normalizar el texto de búsqueda") {
  CHECK(normalizeSearchText("ÁÉÍÓÚ áéíóú ÜÑñ") == "aeiou aeiou unn");
  CHECK(normalizeSearchText("Piedra_Luminosa") == "piedra luminosa");
  CHECK(normalizeSearchText("Año 2") == "ano 2");
  CHECK(normalizeSearchText("") == "");
}

TEST_CASE("Creativo: el menú muestra 9x5 objetos y la barra rápida") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  CHECK(m.width() == 195);
  CHECK(m.height() == 136);
  CHECK(m.creativeTab() == CreativeTab::Blocks);
  CHECK(m.texture() == "gui/container/creative_inventory/tab_items.png");
  int sources = 0, storage = 0;
  for (const MenuSlot& s : m.slots()) {
    sources += s.role == SlotRole::Source;
    storage += s.role == SlotRole::Storage;
  }
  CHECK(sources == 45);
  CHECK(storage == 9);  // solo la barra rápida
  // Los objetos de la pestaña, en orden
  const auto& list = creativeTabItems(CreativeTab::Blocks);
  for (int i = 0; i < Menu::kCreativeVisible; i++) {
    const MenuSlot& s = m.slots()[static_cast<std::size_t>(i)];
    CHECK(s.role == SlotRole::Source);
    CHECK(*s.stack == list[static_cast<std::size_t>(i)]);
  }
  CHECK(m.listSize() == static_cast<int>(list.size()));
}

TEST_CASE("Creativo: cambiar de pestaña, desplazar y los topes de la lista") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  CHECK(m.scrollRow() == 0);
  m.scroll(-3);
  CHECK(m.scrollRow() == 0);  // no se pasa por arriba
  m.scroll(2);
  CHECK(m.scrollRow() == 2);
  CHECK(*m.slots()[0].stack == creativeTabItems(CreativeTab::Blocks)[2 * kCreativeColumns]);
  m.scroll(1000);
  CHECK(m.scrollRow() == m.maxScroll());  // ni por abajo
  CHECK(m.maxScroll() == (m.listSize() + kCreativeColumns - 1) / kCreativeColumns - Menu::kCreativeRows);
  m.setScrollRow(3);
  CHECK(m.scrollRow() == 3);
  m.setScrollRow(-9);
  CHECK(m.scrollRow() == 0);
  // Al cambiar de pestaña la lista vuelve arriba del todo
  m.setScrollRow(4);
  m.setCreativeTab(CreativeTab::Food);
  CHECK(m.creativeTab() == CreativeTab::Food);
  CHECK(m.scrollRow() == 0);
  CHECK(*m.slots()[0].stack == creativeTabItems(CreativeTab::Food)[0]);
  // Una pestaña corta no se desplaza
  m.setCreativeTab(CreativeTab::Transport);
  CHECK(m.maxScroll() == 0);
  m.scroll(5);
  CHECK(m.scrollRow() == 0);
  // Fuera del creativo no hay nada que desplazar
  Menu inv(MenuKind::Inventory, p);
  inv.scroll(3);
  CHECK(inv.scrollRow() == 0);
  CHECK(inv.maxScroll() == 0);
}

TEST_CASE("Creativo: coger de la lista, dejar en la barra, destruir y mayúsculas") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  const int stone = slotWith(m, B::stone, 0);
  REQUIRE(stone >= 0);
  m.click(stone, 0, false);  // clic izquierdo: una pila entera
  CHECK(p.cursor.id == B::stone);
  CHECK(p.cursor.count == 64);
  const int hot0 = hotbarSlot(m, 0);
  REQUIRE(hot0 >= 0);
  m.click(hot0, 0, false);  // se deja en la barra rápida
  CHECK(p.cursor.empty());
  CHECK(p.inventory.slot(0).id == B::stone);
  CHECK(p.inventory.slot(0).count == 64);
  // Clic derecho: uno solo
  m.click(slotWith(m, B::cobblestone), 1, false);
  CHECK(p.cursor.count == 1);
  m.click(slotWith(m, B::cobblestone), 0, false);  // dejarlo sobre la lista lo destruye
  CHECK(p.cursor.empty());
  // Mayúsculas: la pila entera va al inventario sin pasar por el cursor
  m.click(slotWith(m, B::dirt, 0), 0, true);
  CHECK(p.cursor.empty());
  CHECK(p.inventory.slot(1).id == B::dirt);
  CHECK(p.inventory.slot(1).count == 64);
  // Las herramientas no se apilan
  m.setCreativeTab(CreativeTab::Tools);
  m.click(slotWith(m, ItemId::diamond_pickaxe), 0, false);
  CHECK(p.cursor.id == ItemId::diamond_pickaxe);
  CHECK(p.cursor.count == 1);
}

TEST_CASE("Creativo: en las pestañas de objetos, mayúsculas manda la barra rápida al resto del inventario") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  p.inventory.slot(2) = ItemStack(B::cobblestone, 40);
  m.click(hotbarSlot(m, 2), 0, true);
  CHECK(p.inventory.slot(2).empty());
  CHECK(p.inventory.slot(9).id == B::cobblestone);
  CHECK(p.inventory.slot(9).count == 40);
  // Se junta con lo que ya hay, y si no cabe no se pierde
  p.inventory.slot(3) = ItemStack(B::cobblestone, 40);
  m.click(hotbarSlot(m, 3), 0, true);
  CHECK(p.inventory.slot(9).count == 64);
  CHECK(p.inventory.slot(10).count == 16);
  for (int i = 9; i < 36; i++) p.inventory.slot(i) = ItemStack(B::dirt, 64);  // inventario lleno
  p.inventory.slot(4) = ItemStack(B::stone, 5);
  m.click(hotbarSlot(m, 4), 0, true);
  CHECK(p.inventory.slot(4).count == 5);
}

TEST_CASE("Creativo: la tecla numérica sobre un objeto lo pone en la barra rápida") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  p.inventory.slot(3) = ItemStack(B::dirt, 5);
  m.hotkey(slotWith(m, B::cobblestone), 3);  // (casilla 4 de la barra: sustituye lo que hubiera)
  CHECK(p.inventory.slot(3).id == B::cobblestone);
  CHECK(p.inventory.slot(3).count == 64);
  m.setCreativeTab(CreativeTab::Tools);
  m.hotkey(slotWith(m, ItemId::iron_sword) >= 0 ? slotWith(m, ItemId::iron_sword) : slotWith(m, ItemId::iron_axe), 0);
  CHECK(p.inventory.slot(0).count == 1);  // (las herramientas no se apilan)
  m.hotkey(-1, 0);     // (fuera de las casillas: nada)
  m.hotkey(0, 9);      // (casilla de barra que no existe)
  m.hotkey(0, -1);
  // Sobre una casilla del inventario, intercambia con la de la barra
  m.setCreativeTab(CreativeTab::Inventory);
  p.inventory.slot(9) = ItemStack(B::sand, 7);
  p.inventory.slot(1) = ItemStack(B::gravel, 2);
  m.hotkey(hotbarSlot(m, 9), 1);
  CHECK(p.inventory.slot(1).id == B::sand);
  CHECK(p.inventory.slot(1).count == 7);
  CHECK(p.inventory.slot(9).id == B::gravel);
  m.hotkey(hotbarSlot(m, 1), 1);  // consigo misma: sigue igual
  CHECK(p.inventory.slot(1).id == B::sand);
  // Fuera del creativo no hace nada
  Menu inv(MenuKind::Inventory, p);
  inv.hotkey(5, 1);
  CHECK(p.inventory.slot(1).id == B::sand);
}

TEST_CASE("Creativo: la papelera destruye lo que lleves en el cursor") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  CHECK(slotOfRole(m, SlotRole::Trash) < 0);  // (solo está en el inventario de supervivencia)
  m.setCreativeTab(CreativeTab::Inventory);
  const int trash = slotOfRole(m, SlotRole::Trash);
  REQUIRE(trash >= 0);
  p.cursor = ItemStack(B::diamond_block, 12);
  m.click(trash, 0, false);
  CHECK(p.cursor.empty());
  CHECK(m.slots()[static_cast<std::size_t>(trash)].stack->empty());
  m.click(trash, 1, false);  // sin nada en el cursor no pasa nada
  CHECK(p.cursor.empty());
}

TEST_CASE("Creativo: inventario de supervivencia (armadura, inventario, barra y papelera)") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  m.setCreativeTab(CreativeTab::Inventory);
  CHECK(m.texture() == "gui/container/creative_inventory/tab_inventory.png");
  int armor = 0, storage = 0, source = 0;
  for (const MenuSlot& s : m.slots()) {
    armor += s.role == SlotRole::Armor;
    storage += s.role == SlotRole::Storage;
    source += s.role == SlotRole::Source;
  }
  CHECK(armor == 4);
  CHECK(storage == 36);
  CHECK(source == 0);
  CHECK(m.maxScroll() == 0);
  // La armadura: solo cabe la pieza que va en cada casilla
  const int head = slotOfRole(m, SlotRole::Armor, 0);
  p.cursor = ItemStack(ItemId::iron_chestplate);
  m.click(head, 0, false);
  CHECK(p.inventory.armor(3).empty());  // una pechera no cabe en la casilla del casco
  p.cursor = ItemStack(ItemId::iron_helmet);
  m.click(head, 0, false);
  CHECK(p.inventory.armor(3).id == ItemId::iron_helmet);
  CHECK(p.cursor.empty());
  // Mayúsculas sobre una pieza puesta: va al inventario
  m.click(head, 0, true);
  CHECK(p.inventory.armor(3).empty());
  int where = -1;
  for (int i = 0; i < 36; i++)
    if (p.inventory.slot(i).id == ItemId::iron_helmet) where = i;
  REQUIRE(where >= 0);
  // Mayúsculas sobre una pieza del inventario: se la pone
  m.click(hotbarSlot(m, where), 0, true);
  CHECK(p.inventory.armor(3).id == ItemId::iron_helmet);
  CHECK(p.inventory.slot(where).empty());
}

TEST_CASE("Creativo: el campo de búsqueda filtra al escribir y se vacía al volver a la pestaña") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  m.setCreativeTab(CreativeTab::Search);
  CHECK(m.texture() == "gui/container/creative_inventory/tab_item_search.png");
  CHECK(m.searchText().empty());
  CHECK(m.listSize() == static_cast<int>(creativeItems().size()));  // sin texto, todo
  m.scroll(6);
  CHECK(m.scrollRow() == 6);
  m.typeSearch("pi");
  m.typeSearch("co");
  CHECK(m.searchText() == "pico");
  CHECK(m.listSize() == 5);
  CHECK(m.scrollRow() == 0);  // al escribir, la lista vuelve arriba
  CHECK(m.slots()[0].stack->id == ItemId::wooden_pickaxe);
  CHECK(m.slots()[5].stack->empty());
  m.eraseSearchChar();
  CHECK(m.searchText() == "pic");
  m.setSearchText("");
  CHECK(m.listSize() == static_cast<int>(creativeItems().size()));
  // Caracteres de control fuera, y un máximo
  m.typeSearch("a\nb\tc\x01");
  CHECK(m.searchText() == "abc");
  m.typeSearch(std::string(100, 'x'));
  CHECK(m.searchText().size() == 40);
  // Borrar una letra con tilde quita los dos bytes
  m.setSearchText("");
  m.typeSearch("ca\xC3\xB1");  // "cañ"
  m.eraseSearchChar();
  CHECK(m.searchText() == "ca");
  m.eraseSearchChar();
  m.eraseSearchChar();
  m.eraseSearchChar();  // (ya vacío: no pasa nada)
  CHECK(m.searchText().empty());
  // Se puede coger lo encontrado
  m.typeSearch("manzana dorada");
  REQUIRE(m.listSize() == 2);
  m.click(0, 0, false);
  CHECK(p.cursor.id == ItemId::golden_apple);
  // Al salir de la búsqueda y volver, el campo está vacío
  m.setCreativeTab(CreativeTab::Blocks);
  m.setCreativeTab(CreativeTab::Search);
  CHECK(m.searchText().empty());
  CHECK(m.listSize() == static_cast<int>(creativeItems().size()));
}

TEST_CASE("Creativo: fuera de la búsqueda lo escrito no cambia la lista") {
  Player p = creativePlayer();
  Menu m(MenuKind::Creative, p);
  const int size = m.listSize();
  m.typeSearch("pico");
  CHECK(m.listSize() == size);  // (en Bloques, el texto no filtra)
}

TEST_CASE("Creativo: la sesión recuerda la última pestaña, pero no la búsqueda") {
  struct Flat : WorldAccess {
    World w;
    World& world() override { return w; }
    void setBlock(int x, int y, int z, BlockState s) override {
      ChunkSet mod;
      w.setBlock(x, y, z, s, mod);
    }
  } fw;
  GameSession s(fw, 1);
  s.setMode(GameMode::Creative);
  s.openInventory();
  REQUIRE(s.menu());
  CHECK(s.menu()->creativeTab() == CreativeTab::Blocks);
  s.menu()->setCreativeTab(CreativeTab::Food);
  s.closeMenu();
  s.openInventory();
  CHECK(s.menu()->creativeTab() == CreativeTab::Food);
  s.menu()->setCreativeTab(CreativeTab::Search);
  s.closeMenu();
  s.openInventory();
  CHECK(s.menu()->creativeTab() == CreativeTab::Food);  // la búsqueda no se recuerda (la tecla E debe poder cerrar)
  // Al cerrar, lo que se lleve en el cursor vuelve al inventario
  s.menu()->click(0, 0, false);
  CHECK_FALSE(s.player().cursor.empty());
  s.closeMenu();
  CHECK(s.player().cursor.empty());
}

TEST_CASE("Creativo: pestañas arriba y abajo, y las de la derecha pegadas al borde") {
  int top = 0, bottom = 0;
  std::set<std::pair<bool, int>> places;
  for (int i = 0; i < kCreativeTabCount; i++) {
    const auto t = static_cast<CreativeTab>(i);
    (creativeTabOnTop(t) ? top : bottom)++;
    places.insert({creativeTabOnTop(t), creativeTabColumn(t)});
    CHECK_FALSE(creativeTabName(t).empty());
    CHECK_FALSE(creativeTabIcon(t).empty());
  }
  CHECK(top == 6);
  CHECK(bottom == 6);
  CHECK(places.size() == 12);  // cada una en su sitio
  CHECK(creativeTabRightmost(CreativeTab::Search));
  CHECK(creativeTabRightmost(CreativeTab::Inventory));
  CHECK_FALSE(creativeTabRightmost(CreativeTab::Misc));
}
