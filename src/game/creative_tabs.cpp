#include "game/creative_tabs.h"

#include <array>
#include <cctype>

#include "data/items.h"
#include "game/enchantments.h"
#include "game/rules.h"

namespace mcw {
namespace {

constexpr int idx(CreativeTab t) { return static_cast<int>(t); }

/// Reparte la lista general del creativo entre las pestañas. Cada objeto sale en la primera lista que lo reclama
/// y en ninguna más, así que no puede haber repetidos aunque dos listas pidan lo mismo.
class Filler {
 public:
  explicit Filler(const std::vector<ItemStack>& all) : all_(all), used_(all.size(), false) {}

  /// Todas las variantes de `id` (o solo la `meta` si no es -1), en el orden de la lista general.
  void add(std::vector<ItemStack>& out, int id, int meta = -1) {
    for (std::size_t i = 0; i < all_.size(); i++) {
      const ItemStack& s = all_[i];
      if (used_[i] || s.id != id || (meta >= 0 && s.meta != meta)) continue;
      out.push_back(s);
      used_[i] = true;
    }
  }
  /// Las variantes `from` a `to`, en ese orden (o al revés si `from` > `to`).
  void range(std::vector<ItemStack>& out, int id, int from, int to) {
    const int step = from <= to ? 1 : -1;
    for (int m = from;; m += step) {
      add(out, id, m);
      if (m == to) break;
    }
  }
  /// Cada identificador de una lista, con todas sus variantes.
  void each(std::vector<ItemStack>& out, std::initializer_list<int> ids) {
    for (const int id : ids) add(out, id);
  }
  /// Libros encantados: los que sirven para lo que diga `want` (según a qué se puede encantar lo que guardan).
  template <typename Pred>
  void books(std::vector<ItemStack>& out, Pred want) {
    for (std::size_t i = 0; i < all_.size(); i++) {
      const ItemStack& s = all_[i];
      if (used_[i] || s.id != ItemId::enchanted_book || !s.extra || s.extra->stored.empty()) continue;
      const EnchantInfo* e = enchantInfo(s.extra->stored.front().first);
      if (!e || !want(e->target)) continue;
      out.push_back(s);
      used_[i] = true;
    }
  }
  /// Rellena con huecos hasta el final de la fila (si la lista ya acaba en una fila entera o está vacía, no hace nada).
  static void row(std::vector<ItemStack>& out) {
    while (!out.empty() && out.size() % kCreativeColumns != 0) out.emplace_back();
  }
  /// Lo que nadie ha reclamado.
  std::vector<ItemStack> rest() const {
    std::vector<ItemStack> r;
    for (std::size_t i = 0; i < all_.size(); i++)
      if (!used_[i]) r.push_back(all_[i]);
    return r;
  }

 private:
  const std::vector<ItemStack>& all_;
  std::vector<bool> used_;
};

struct Tabs {
  std::array<std::vector<ItemStack>, kCreativeTabCount> list;
  std::vector<ItemStack> unclassified;
};

Tabs buildTabs() {
  using namespace ItemId;
  Tabs t;
  Filler f(creativeItems());
  auto& blocks = t.list[idx(CreativeTab::Blocks)];
  auto& deco = t.list[idx(CreativeTab::Decoration)];
  auto& redstone = t.list[idx(CreativeTab::Redstone)];
  auto& transport = t.list[idx(CreativeTab::Transport)];
  auto& misc = t.list[idx(CreativeTab::Misc)];
  auto& food = t.list[idx(CreativeTab::Food)];
  auto& tools = t.list[idx(CreativeTab::Tools)];
  auto& combat = t.list[idx(CreativeTab::Combat)];
  auto& brewing = t.list[idx(CreativeTab::Brewing)];
  auto& materials = t.list[idx(CreativeTab::Materials)];

  // Cada grupo empieza en una fila nueva (`row` rellena con huecos hasta el final de la fila)
  auto row = [&](std::vector<ItemStack>& out) { f.row(out); };

  // --- Bloques de construcción: de lo más natural a lo más trabajado ---
  f.add(blocks, grass);
  f.range(blocks, dirt, 0, 2);
  f.each(blocks, {farmland, mycelium, snow, ice, packed_ice, clay, sand, gravel, sandstone, red_sandstone});
  row(blocks);
  f.add(blocks, stone);
  f.each(blocks, {cobblestone, mossy_cobblestone, stonebrick, bedrock, obsidian, netherrack, soul_sand, nether_brick, end_stone});
  row(blocks);
  f.each(blocks, {coal_ore, iron_ore, gold_ore, lapis_ore, redstone_ore, diamond_ore, emerald_ore, quartz_ore});
  row(blocks);
  f.each(blocks, {coal_block, iron_block, gold_block, lapis_block, diamond_block, emerald_block, quartz_block});
  row(blocks);
  f.each(blocks, {brick_block, prismarine, sea_lantern, glowstone, hardened_clay, sponge, hay_block, slime});
  row(blocks);
  f.each(blocks, {log, log2, planks});
  row(blocks);
  f.add(blocks, stained_hardened_clay);
  row(blocks);
  f.each(blocks, {glass, stained_glass});
  row(blocks);
  f.add(blocks, wool);
  row(blocks);
  // Escaleras (las de madera en el orden de las maderas, luego las de piedra) y losas
  f.each(blocks, {oak_stairs, spruce_stairs, birch_stairs, jungle_stairs, acacia_stairs, dark_oak_stairs});
  f.each(blocks, {stone_stairs, brick_stairs, stone_brick_stairs, nether_brick_stairs, sandstone_stairs, red_sandstone_stairs,
                  quartz_stairs});
  row(blocks);
  f.each(blocks, {wooden_slab, stone_slab, stone_slab2});

  // --- Decoración ---
  f.each(deco, {sapling, leaves, leaves2});
  row(deco);
  f.each(deco, {tallgrass, deadbush, double_plant, yellow_flower, red_flower, brown_mushroom, red_mushroom, vine, waterlily, cactus});
  row(deco);
  f.each(deco, {pumpkin, lit_pumpkin, melon_block, brown_mushroom_block, red_mushroom_block});
  row(deco);
  f.each(deco, {web, snow_layer, carpet});
  row(deco);
  f.each(deco, {fence, spruce_fence, birch_fence, jungle_fence, acacia_fence, dark_oak_fence, nether_brick_fence, cobblestone_wall,
                iron_bars, glass_pane, ladder});
  row(deco);
  f.add(deco, stained_glass_pane);
  row(deco);
  f.each(deco, {torch, chest, ender_chest, crafting_table, furnace, bookshelf, enchanting_table, anvil, jukebox});
  row(deco);
  f.each(deco, {bed, sign, painting, item_frame, armor_stand, flower_pot, banner});
  row(deco);
  f.add(deco, skull);
  row(deco);
  f.add(deco, monster_egg);

  // --- Redstone ---
  f.each(redstone, {ItemId::redstone, redstone_torch, redstone_block, repeater, comparator, lever, stone_button, wooden_button,
                    tripwire_hook, stone_pressure_plate, wooden_pressure_plate, light_weighted_pressure_plate,
                    heavy_weighted_pressure_plate, daylight_detector});
  row(redstone);
  f.each(redstone, {piston, sticky_piston, dispenser, dropper, hopper, noteblock, redstone_lamp, tnt, trapped_chest, command_block});
  row(redstone);
  f.each(redstone, {wooden_door, spruce_door, birch_door, jungle_door, acacia_door, dark_oak_door, iron_door, trapdoor, iron_trapdoor});
  row(redstone);
  f.each(redstone, {fence_gate, spruce_fence_gate, birch_fence_gate, jungle_fence_gate, acacia_fence_gate, dark_oak_fence_gate});

  // --- Transporte ---
  f.each(transport, {rail, golden_rail, detector_rail, activator_rail});
  row(transport);
  f.each(transport, {minecart, chest_minecart, furnace_minecart, tnt_minecart, hopper_minecart, command_block_minecart});
  row(transport);
  f.each(transport, {boat, saddle, iron_horse_armor, golden_horse_armor, diamond_horse_armor, lead});

  // --- Varios ---
  f.each(misc, {mob_spawner, end_portal_frame, dragon_egg, beacon, barrier, nether_star, spawn_egg});
  row(misc);
  f.each(misc, {experience_bottle, ender_pearl, ender_eye, fire_charge, snowball, egg});
  row(misc);
  f.each(misc, {name_tag, map, filled_map, writable_book, written_book, fireworks, firework_charge});

  // --- Alimentos ---
  f.each(food, {apple, golden_apple, golden_carrot, bread, cookie, cake, pumpkin_pie, mushroom_stew, rabbit_stew});
  row(food);
  f.each(food, {porkchop, cooked_porkchop, beef, cooked_beef, chicken, cooked_chicken, mutton, cooked_mutton, rabbit, cooked_rabbit,
                fish, cooked_fish, rotten_flesh});
  row(food);
  f.each(food, {melon, carrot, potato, baked_potato, poisonous_potato});

  // --- Herramientas: una fila por tipo, en el orden de los materiales ---
  f.each(tools, {wooden_pickaxe, stone_pickaxe, iron_pickaxe, golden_pickaxe, diamond_pickaxe});
  row(tools);
  f.each(tools, {wooden_axe, stone_axe, iron_axe, golden_axe, diamond_axe});
  row(tools);
  f.each(tools, {wooden_shovel, stone_shovel, iron_shovel, golden_shovel, diamond_shovel});
  row(tools);
  f.each(tools, {wooden_hoe, stone_hoe, iron_hoe, golden_hoe, diamond_hoe});
  row(tools);
  f.each(tools, {shears, flint_and_steel, fishing_rod, carrot_on_a_stick, compass, clock});
  row(tools);
  f.each(tools, {bucket, water_bucket, lava_bucket, milk_bucket});
  row(tools);
  f.books(tools, [](EnchantTarget e) { return e == EnchantTarget::Digger || e == EnchantTarget::FishingRod || e == EnchantTarget::Breakable; });

  // --- Combate: espadas, arco y flechas, y cada armadura en su fila (casco, pechera, pantalones, botas) ---
  f.each(combat, {wooden_sword, stone_sword, iron_sword, golden_sword, diamond_sword, bow, arrow});
  row(combat);
  f.each(combat, {leather_helmet, leather_chestplate, leather_leggings, leather_boots});
  row(combat);
  f.each(combat, {chainmail_helmet, chainmail_chestplate, chainmail_leggings, chainmail_boots});
  row(combat);
  f.each(combat, {iron_helmet, iron_chestplate, iron_leggings, iron_boots});
  row(combat);
  f.each(combat, {golden_helmet, golden_chestplate, golden_leggings, golden_boots});
  row(combat);
  f.each(combat, {diamond_helmet, diamond_chestplate, diamond_leggings, diamond_boots});
  row(combat);
  f.books(combat, [](EnchantTarget) { return true; });  // (lo que quede: armadura, armas y arcos)

  // --- Pociones ---
  f.each(brewing, {potion, glass_bottle, brewing_stand, cauldron});
  row(brewing);
  f.each(brewing, {nether_wart, blaze_powder, blaze_rod, ghast_tear, magma_cream, spider_eye, fermented_spider_eye, speckled_melon,
                   rabbit_foot});

  // --- Materiales ---
  f.each(materials, {coal, diamond, emerald, iron_ingot, gold_ingot, gold_nugget, quartz, netherbrick, brick, clay_ball, flint});
  row(materials);
  f.each(materials, {stick, string, feather, gunpowder, leather, rabbit_hide, slime_ball, bone, paper, book, bowl, glowstone_dust,
                     prismarine_shard, prismarine_crystals});
  row(materials);
  f.each(materials, {reeds, sugar, wheat_seeds, wheat, pumpkin_seeds, melon_seeds});
  row(materials);
  f.range(materials, dye, 15, 0);  // los tintes en el orden de la lana (blanco, naranja, magenta...)

  // Lo que nadie ha pedido va al final de "Varios" (y las pruebas avisan)
  t.unclassified = f.rest();
  if (!t.unclassified.empty()) {
    row(misc);
    for (const ItemStack& s : t.unclassified) misc.push_back(s);
  }
  // (los huecos del final no hacen falta)
  for (auto& list : t.list)
    while (!list.empty() && list.back().empty()) list.pop_back();

  // La búsqueda lo trae todo, en el orden de las pestañas
  auto& search = t.list[idx(CreativeTab::Search)];
  for (const CreativeTab tab : {CreativeTab::Blocks, CreativeTab::Decoration, CreativeTab::Redstone, CreativeTab::Transport,
                                CreativeTab::Misc, CreativeTab::Food, CreativeTab::Tools, CreativeTab::Combat, CreativeTab::Brewing,
                                CreativeTab::Materials})
    for (const ItemStack& s : t.list[idx(tab)])
      if (!s.empty()) search.push_back(s);
  return t;
}

const Tabs& tabs() {
  static const Tabs t = buildTabs();
  return t;
}

/// Texto en el que se busca para cada objeto de la pestaña de búsqueda: los nombres (español e inglés), el nombre
/// interno y, en los libros, lo que encantan.
std::string searchBlob(const ItemStack& s) {
  std::string blob = normalizeSearchText(itemDisplayNameEs(s.id, s.meta));
  blob += '\n';
  blob += normalizeSearchText(itemDisplayName(s.id, s.meta));
  blob += '\n';
  blob += normalizeSearchText(itemInfo(s.id).name);
  if (s.extra) {
    for (const auto& list : {&s.extra->stored, &s.extra->ench})
      for (const auto& [id, level] : *list) {
        (void)level;
        if (const EnchantInfo* e = enchantInfo(id)) {
          blob += '\n';
          blob += normalizeSearchText(e->nameEs);
          blob += '\n';
          blob += normalizeSearchText(e->key);
        }
      }
  }
  return blob;
}

const std::vector<std::string>& searchBlobs() {
  static const std::vector<std::string> blobs = [] {
    std::vector<std::string> b;
    for (const ItemStack& s : creativeTabItems(CreativeTab::Search)) b.push_back(searchBlob(s));
    return b;
  }();
  return blobs;
}

}  // namespace

std::string_view creativeTabName(CreativeTab t) {
  switch (t) {
    case CreativeTab::Blocks: return "Bloques de construcción";
    case CreativeTab::Decoration: return "Decoración";
    case CreativeTab::Redstone: return "Redstone";
    case CreativeTab::Transport: return "Transporte";
    case CreativeTab::Misc: return "Varios";
    case CreativeTab::Search: return "Buscar objetos";
    case CreativeTab::Food: return "Alimentos";
    case CreativeTab::Tools: return "Herramientas";
    case CreativeTab::Combat: return "Combate";
    case CreativeTab::Brewing: return "Pociones";
    case CreativeTab::Materials: return "Materiales";
    case CreativeTab::Inventory: return "Inventario";
  }
  return {};
}

ItemStack creativeTabIcon(CreativeTab t) {
  switch (t) {
    case CreativeTab::Blocks: return ItemStack(ItemId::brick_block);
    case CreativeTab::Decoration: return ItemStack(ItemId::red_flower);
    case CreativeTab::Redstone: return ItemStack(ItemId::redstone);
    case CreativeTab::Transport: return ItemStack(ItemId::rail);
    case CreativeTab::Misc: return ItemStack(ItemId::lava_bucket);
    case CreativeTab::Search: return ItemStack(ItemId::compass);
    case CreativeTab::Food: return ItemStack(ItemId::apple);
    case CreativeTab::Tools: return ItemStack(ItemId::iron_axe);
    case CreativeTab::Combat: return ItemStack(ItemId::golden_sword);
    case CreativeTab::Brewing: return ItemStack(ItemId::potion);
    case CreativeTab::Materials: return ItemStack(ItemId::stick);
    case CreativeTab::Inventory: return ItemStack(ItemId::chest);
  }
  return {};
}

const std::vector<ItemStack>& creativeTabItems(CreativeTab t) { return tabs().list[static_cast<std::size_t>(idx(t))]; }

const std::vector<ItemStack>& creativeUnclassified() { return tabs().unclassified; }

std::string normalizeSearchText(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0xC3 && i + 1 < text.size()) {  // letras latinas con tilde (UTF-8 de dos bytes)
      const unsigned char d = static_cast<unsigned char>(text[++i]);
      const unsigned char u = d >= 0xA0 ? static_cast<unsigned char>(d - 0x20) : d;  // a minúscula
      char base = 0;
      if (u >= 0x80 && u <= 0x85) base = 'a';
      else if (u == 0x87) base = 'c';
      else if (u >= 0x88 && u <= 0x8B) base = 'e';
      else if (u >= 0x8C && u <= 0x8F) base = 'i';
      else if (u == 0x91) base = 'n';
      else if ((u >= 0x92 && u <= 0x96) || u == 0x98) base = 'o';
      else if (u >= 0x99 && u <= 0x9C) base = 'u';
      else if (u == 0x9D) base = 'y';
      if (base) out += base;
      continue;
    }
    if (c >= 0x80) continue;  // lo demás que no es ASCII no cuenta
    out += c == '_' ? ' ' : static_cast<char>(std::tolower(c));
  }
  return out;
}

std::vector<ItemStack> creativeSearch(std::string_view query) {
  std::vector<std::string> words;
  {
    const std::string q = normalizeSearchText(query);
    std::size_t i = 0;
    while (i < q.size()) {
      while (i < q.size() && q[i] == ' ') i++;
      const std::size_t start = i;
      while (i < q.size() && q[i] != ' ') i++;
      if (i > start) words.push_back(q.substr(start, i - start));
    }
  }
  const auto& all = creativeTabItems(CreativeTab::Search);
  if (words.empty()) return all;
  const auto& blobs = searchBlobs();
  std::vector<ItemStack> out;
  for (std::size_t i = 0; i < all.size(); i++) {
    bool ok = true;
    for (const std::string& w : words)
      if (blobs[i].find(w) == std::string::npos) {
        ok = false;
        break;
      }
    if (ok) out.push_back(all[i]);
  }
  return out;
}

}  // namespace mcw
