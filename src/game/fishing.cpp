// Botín de la pesca y tiempos de espera (la parte del anzuelo está en fishing_session.cpp).
#include "game/fishing.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "data/items.h"
#include "game/enchanting.h"

namespace mcw {

namespace {

struct Entry {
  int weight;
  ItemStack stack;
  int kind;  // 0 normal, 1 pierde parte de su vida (hasta el 90 %), 2 encantado (tesoro)
};

const ItemStack& pick(const std::vector<Entry>& list, Random& rng, int* kind) {
  int total = 0;
  for (const Entry& e : list) total += e.weight;
  int r = rng.nextInt(total);
  for (const Entry& e : list) {
    if ((r -= e.weight) < 0) {
      *kind = e.kind;
      return e.stack;
    }
  }
  *kind = list.back().kind;
  return list.back().stack;
}

}  // namespace

int fishingWaitTicks(Random& rng, int lure) {
  const int lo = std::max(0, 100 - 100 * lure), hi = std::max(lo, 600 - 100 * lure);
  return rng.range(lo, hi);
}

ItemStack fishingLoot(Random& rng, int luck, int lure) {
  static const std::vector<Entry> fish = {
      {60, ItemStack(ItemId::fish, 1, 0), 0}, {25, ItemStack(ItemId::fish, 1, 1), 0},
      {2, ItemStack(ItemId::fish, 1, 2), 0},  {13, ItemStack(ItemId::fish, 1, 3), 0},
  };
  static const std::vector<Entry> junk = {
      {10, ItemStack(ItemId::bowl), 0},          {2, ItemStack(ItemId::fishing_rod), 1},
      {10, ItemStack(ItemId::leather), 0},       {10, ItemStack(ItemId::leather_boots), 1},
      {10, ItemStack(ItemId::rotten_flesh), 0},  {5, ItemStack(ItemId::stick), 0},
      {5, ItemStack(ItemId::string), 0},         {10, ItemStack(ItemId::potion, 1, 0), 0},
      {10, ItemStack(ItemId::bone), 0},          {1, ItemStack(ItemId::dye, 1, 0), 0},
      {10, ItemStack(ItemId::tripwire_hook), 0}, {17, ItemStack(ItemId::waterlily), 0},
  };
  static const std::vector<Entry> treasure = {
      {1, ItemStack(ItemId::bow), 2}, {1, ItemStack(ItemId::book), 2}, {1, ItemStack(ItemId::fishing_rod), 2},
      {1, ItemStack(ItemId::name_tag), 0}, {1, ItemStack(ItemId::saddle), 0},
  };
  // Las probabilidades de 1.8: basura 10 % y tesoro 5 %, que cambian con Suerte marina y Cebo
  const float junkChance = std::max(0.0f, 0.1f - 0.025f * static_cast<float>(luck) - 0.01f * static_cast<float>(lure));
  const float treasureChance = std::max(0.0f, 0.05f + 0.01f * static_cast<float>(luck) - 0.01f * static_cast<float>(lure));
  float f = rng.nextFloat();
  const std::vector<Entry>* list = &fish;
  if (f < junkChance) {
    list = &junk;
  } else {
    f -= junkChance;
    if (f < treasureChance) list = &treasure;
  }
  int kind = 0;
  ItemStack s = pick(*list, rng, &kind);
  if (kind == 1) {
    const int max = itemInfo(s.id).maxDurability;
    s.meta = static_cast<i16>(static_cast<float>(max) * rng.nextFloat() * 0.9f);  // gastado hasta un 90 %
  } else if (kind == 2) {
    // Tesoro encantado: como una mesa a nivel 22..30 (el libro, con un único encantamiento)
    const int cost = rng.range(22, 30);
    const i32 seed = static_cast<i32>(rng.nextU32());
    s = applyEnchants(s, enchantList(s, 0, cost, seed));
    if (s.id != ItemId::enchanted_book && s.isTool()) s.meta = static_cast<i16>(static_cast<float>(itemInfo(s.id).maxDurability) * rng.nextFloat() * 0.25f);
  }
  return s;
}

}  // namespace mcw
