#include "game/loot.h"

#include <vector>

#include "data/blocks.h"
#include "data/items.h"
#include "game/enchanting.h"

namespace mcw {
namespace {

struct Entry {
  int id, meta, minCount, maxCount, weight;
};

ItemStack enchantedBook(Random& rng) {
  ItemStack book(ItemId::enchanted_book);
  const auto list = enchantList(book, 0, 20 + rng.nextInt(10), static_cast<i32>(rng.nextU32()));
  return applyEnchants(book, list);
}

}  // namespace

void fillLoot(ChestState& chest, int table, Random& rng) {
  static const Entry kDungeon[] = {
      {ItemId::saddle, 0, 1, 1, 10},       {ItemId::iron_ingot, 0, 1, 4, 10},  {ItemId::bread, 0, 1, 1, 10},
      {ItemId::wheat, 0, 1, 4, 10},        {ItemId::gunpowder, 0, 1, 4, 10},   {ItemId::string, 0, 1, 4, 10},
      {ItemId::bucket, 0, 1, 1, 10},       {ItemId::golden_apple, 0, 1, 1, 1}, {ItemId::redstone, 0, 1, 4, 10},
      {ItemId::record_13, 0, 1, 1, 10},    {ItemId::record_cat, 0, 1, 1, 10},  {ItemId::name_tag, 0, 1, 1, 10},
      {ItemId::golden_horse_armor, 0, 1, 1, 2}, {ItemId::iron_horse_armor, 0, 1, 1, 5}, {ItemId::diamond_horse_armor, 0, 1, 1, 1},
      {-1, 0, 1, 1, 1},  // libro encantado
  };
  static const Entry kFortress[] = {
      {ItemId::diamond, 0, 1, 3, 5},       {ItemId::iron_ingot, 0, 1, 5, 5},   {ItemId::gold_ingot, 0, 1, 3, 15},
      {ItemId::golden_sword, 0, 1, 1, 5},  {ItemId::golden_chestplate, 0, 1, 1, 5}, {ItemId::flint_and_steel, 0, 1, 1, 5},
      {ItemId::nether_wart, 0, 3, 7, 5},   {ItemId::saddle, 0, 1, 1, 10},      {ItemId::golden_horse_armor, 0, 1, 1, 8},
      {ItemId::iron_horse_armor, 0, 1, 1, 5}, {ItemId::diamond_horse_armor, 0, 1, 1, 3}, {B::obsidian, 0, 2, 4, 2},
  };
  static const Entry kCorridor[] = {
      {ItemId::ender_pearl, 0, 1, 1, 10},  {ItemId::iron_ingot, 0, 1, 5, 10},  {ItemId::gold_ingot, 0, 1, 3, 5},
      {ItemId::redstone, 0, 4, 9, 5},      {ItemId::bread, 0, 1, 3, 15},       {ItemId::apple, 0, 1, 3, 15},
      {ItemId::iron_pickaxe, 0, 1, 1, 5},  {ItemId::iron_sword, 0, 1, 1, 5},   {ItemId::iron_chestplate, 0, 1, 1, 5},
      {ItemId::iron_helmet, 0, 1, 1, 5},   {ItemId::iron_leggings, 0, 1, 1, 5}, {ItemId::iron_boots, 0, 1, 1, 5},
      {ItemId::golden_apple, 0, 1, 1, 1},  {ItemId::saddle, 0, 1, 1, 1},       {ItemId::iron_horse_armor, 0, 1, 1, 1},
  };
  static const Entry kLibrary[] = {
      {ItemId::book, 0, 1, 3, 20}, {ItemId::paper, 0, 2, 7, 20}, {ItemId::map, 0, 1, 1, 1}, {ItemId::compass, 0, 1, 1, 1}, {-1, 0, 1, 1, 10},
  };
  static const Entry kCrossing[] = {
      {ItemId::iron_ingot, 0, 1, 5, 10}, {ItemId::gold_ingot, 0, 1, 3, 5}, {ItemId::redstone, 0, 4, 9, 5}, {ItemId::coal, 0, 3, 8, 10},
      {ItemId::bread, 0, 1, 3, 15},      {ItemId::apple, 0, 1, 3, 15},     {ItemId::iron_pickaxe, 0, 1, 1, 1}, {-1, 0, 1, 1, 1},
  };
  const Entry* list = kDungeon;
  std::size_t n = std::size(kDungeon);
  int rolls = 8;
  switch (table) {
    case 2: list = kFortress; n = std::size(kFortress); rolls = 2 + rng.nextInt(5); break;
    case 3: list = kCorridor; n = std::size(kCorridor); rolls = 2 + rng.nextInt(2); break;
    case 4: list = kLibrary; n = std::size(kLibrary); rolls = 1 + rng.nextInt(10); break;
    case 5: list = kCrossing; n = std::size(kCrossing); rolls = 1 + rng.nextInt(5); break;
    default: break;
  }
  int total = 0;
  for (std::size_t i = 0; i < n; i++) total += list[i].weight;
  for (int r = 0; r < rolls; r++) {
    int pick = rng.nextInt(total);
    const Entry* e = list;
    for (std::size_t i = 0; i < n; i++) {
      pick -= list[i].weight;
      if (pick < 0) {
        e = &list[i];
        break;
      }
    }
    ItemStack s = e->id < 0 ? enchantedBook(rng) : ItemStack(e->id, static_cast<i16>(e->minCount + rng.nextInt(e->maxCount - e->minCount + 1)), static_cast<i16>(e->meta));
    chest.items[static_cast<std::size_t>(rng.nextInt(27))] = s;
  }
}

}  // namespace mcw
