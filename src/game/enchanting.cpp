#include "game/enchanting.h"

#include <algorithm>
#include <cmath>

#include "core/random.h"
#include "data/blocks.h"
#include "game/enchantments.h"
#include "world/world.h"

namespace mcw {
namespace {

bool airAt(const World& w, int x, int y, int z) { return stateId(w.block(x, y, z)) == B::air; }

int calcCost(Random& rng, int slot, int power, int enchantability) {
  if (enchantability <= 0) return 0;
  power = std::min(power, 15);
  const int j = rng.nextInt(8) + 1 + (power >> 1) + rng.nextInt(power + 1);
  if (slot == 0) return std::max(j / 3, 1);
  if (slot == 1) return j * 2 / 3 + 1;
  return std::max(j, power * 2);
}

struct Candidate {
  int id, level;
};

/// El que sale al azar entre los candidatos según su peso (probabilidad relativa).
Candidate pickWeighted(Random& rng, const std::vector<Candidate>& pool) {
  int total = 0;
  for (const Candidate& c : pool) total += enchantInfo(c.id)->weight;
  int r = rng.nextInt(total);
  for (const Candidate& c : pool) {
    r -= enchantInfo(c.id)->weight;
    if (r < 0) return c;
  }
  return pool.back();
}

std::vector<std::pair<int, int>> buildList(Random& rng, const ItemStack& item, int cost) {
  int e = itemEnchantability(item.id);
  if (e <= 0) return {};
  e /= 2;
  e = 1 + rng.nextInt((e >> 1) + 1) + rng.nextInt((e >> 1) + 1);
  const int base = e + cost;
  const float f = (rng.nextFloat() + rng.nextFloat() - 1.0f) * 0.15f;
  const int k = std::max(1, static_cast<int>(static_cast<float>(base) * (1.0f + f) + 0.5f));
  // Candidatos: cada encantamiento que admite el objeto, con el nivel más alto cuyo rango contiene a k
  std::vector<Candidate> pool;
  for (const EnchantInfo& en : allEnchantments()) {
    if (!canEnchant(en, item.id, true)) continue;
    for (int level = en.maxLevel; level >= 1; level--)
      if (k >= en.minEnchantability(level) && k <= en.maxEnchantability(level)) {
        pool.push_back({en.id, level});
        break;
      }
  }
  if (pool.empty()) return {};
  std::vector<std::pair<int, int>> list;
  const Candidate first = pickWeighted(rng, pool);
  list.emplace_back(first.id, first.level);
  // Con probabilidad (k+1)/50, k/2... se añade otro compatible con los que ya hay
  for (int l = k; rng.nextInt(50) <= l; l >>= 1) {
    std::erase_if(pool, [&](const Candidate& c) {
      for (const auto& [id, lvl] : list)
        if (!enchantsCompatible(c.id, id)) return true;
      return false;
    });
    if (pool.empty()) break;
    const Candidate next = pickWeighted(rng, pool);
    list.emplace_back(next.id, next.level);
  }
  return list;
}

}  // namespace

int countBookshelves(const World& w, int tx, int ty, int tz) {
  int count = 0;
  for (int dz = -1; dz <= 1; dz++)
    for (int dx = -1; dx <= 1; dx++) {
      if ((dx == 0 && dz == 0) || !airAt(w, tx + dx, ty, tz + dz) || !airAt(w, tx + dx, ty + 1, tz + dz)) continue;
      auto shelf = [&](int x, int y, int z) { count += stateId(w.block(x, y, z)) == B::bookshelf ? 1 : 0; };
      for (int dy = 0; dy <= 1; dy++) {
        shelf(tx + dx * 2, ty + dy, tz + dz * 2);
        if (dx != 0 && dz != 0) {  // las esquinas también cuentan los dos huecos intermedios del lado
          shelf(tx + dx * 2, ty + dy, tz + dz);
          shelf(tx + dx, ty + dy, tz + dz * 2);
        }
      }
    }
  return count;
}

std::array<EnchantOffer, 3> enchantOffers(const ItemStack& item, int bookshelves, i32 xpSeed) {
  std::array<EnchantOffer, 3> offers{};
  if (item.empty() || item.hasEnchants() || (item.extra && !item.extra->stored.empty())) return offers;  // ya encantado
  const int ench = itemEnchantability(item.id);
  if (ench <= 0) return offers;
  Random rng(static_cast<u64>(static_cast<u32>(xpSeed)) * 0x9E3779B97F4A7C15ull + 0x1234);
  for (int slot = 0; slot < 3; slot++) {
    offers[static_cast<std::size_t>(slot)].cost = calcCost(rng, slot, bookshelves, ench);
    if (offers[static_cast<std::size_t>(slot)].cost < slot + 1) offers[static_cast<std::size_t>(slot)].cost = 0;
  }
  for (int slot = 0; slot < 3; slot++) {
    EnchantOffer& o = offers[static_cast<std::size_t>(slot)];
    if (o.cost <= 0) continue;
    const auto list = enchantList(item, slot, o.cost, xpSeed);
    if (list.empty()) continue;
    const auto& pick = list[static_cast<std::size_t>(rng.nextInt(static_cast<int>(list.size())))];
    o.clueEnchant = pick.first;
    o.clueLevel = pick.second;
  }
  return offers;
}

std::vector<std::pair<int, int>> enchantList(const ItemStack& item, int slot, int cost, i32 xpSeed) {
  Random rng(static_cast<u64>(static_cast<u32>(xpSeed) + static_cast<u32>(slot)) * 0xD1B54A32D192ED03ull + 0x77);
  auto list = buildList(rng, item, cost);
  if (item.id == ItemId::book && list.size() > 1) list.erase(list.begin() + rng.nextInt(static_cast<int>(list.size())));
  return list;
}

ItemStack applyEnchants(ItemStack item, const std::vector<std::pair<int, int>>& list) {
  ItemExtra extra = item.copyExtra();
  if (item.id == ItemId::book) {
    item.id = ItemId::enchanted_book;
    for (const auto& [id, level] : list) extra.stored.emplace_back(static_cast<i16>(id), static_cast<i16>(level));
  } else {
    for (const auto& [id, level] : list) extra.ench.emplace_back(static_cast<i16>(id), static_cast<i16>(level));
  }
  item.setExtra(std::move(extra));
  return item;
}

}  // namespace mcw
