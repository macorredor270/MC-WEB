#include "game/anvil.h"
#include "game/effects.h"

#include <algorithm>

#include "data/blocks.h"
#include "data/items.h"
#include "game/armor.h"
#include "game/enchantments.h"

namespace mcw {
namespace {

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

void setEnchants(ItemStack& s, const std::vector<std::pair<int, int>>& list) {
  ItemExtra e = s.copyExtra();
  auto& dst = s.id == ItemId::enchanted_book ? e.stored : e.ench;
  dst.clear();
  for (const auto& [id, level] : list) dst.emplace_back(static_cast<i16>(id), static_cast<i16>(level));
  s.setExtra(std::move(e));
}

int repairCostOf(const ItemStack& s) { return s.extra ? s.extra->repairCost : 0; }

/// Cuánto encarece cada encantamiento según lo raro que es (peso 10 → 1, 5 → 2, 2 → 4, 1 → 8).
int rarityMultiplier(int weight) { return weight >= 10 ? 1 : weight >= 5 ? 2 : weight >= 2 ? 4 : 8; }

}  // namespace

std::string anvilDisplayName(const ItemStack& s) {
  if (s.extra && !s.extra->name.empty()) return s.extra->name;
  return itemName(s.id, s.meta);
}

bool anvilRepairsWith(int itemId, int materialId) {
  const ItemInfo& info = itemInfo(itemId);
  if (!info.exists || info.maxDurability <= 0) return false;
  const std::string_view n = info.name;
  if (startsWith(n, "wooden_")) return materialId == B::planks;
  if (startsWith(n, "stone_")) return materialId == B::cobblestone;
  if (startsWith(n, "iron_") || startsWith(n, "chainmail_")) return materialId == ItemId::iron_ingot;
  if (startsWith(n, "golden_")) return materialId == ItemId::gold_ingot;
  if (startsWith(n, "diamond_")) return materialId == ItemId::diamond;
  if (startsWith(n, "leather_")) return materialId == ItemId::leather;
  return false;
}

std::vector<std::pair<int, int>> anvilEnchants(const ItemStack& s) {
  std::vector<std::pair<int, int>> out;
  if (!s.extra) return out;
  for (const auto& [id, level] : s.id == ItemId::enchanted_book ? s.extra->stored : s.extra->ench) out.emplace_back(id, level);
  return out;
}

AnvilResult anvilCompute(const ItemStack& left, const ItemStack& right, const std::string& newName, bool creative) {
  AnvilResult none;
  if (left.empty()) return none;
  ItemStack out = left;
  int extra = 0, renameCost = 0;
  int baseCost = repairCostOf(left) + (right.empty() ? 0 : repairCostOf(right));
  int materialUsed = 0;
  std::vector<std::pair<int, int>> map = anvilEnchants(left);
  const int maxDur = itemInfo(left.id).maxDurability;

  if (!right.empty()) {
    const bool rightBook = right.id == ItemId::enchanted_book && right.extra && !right.extra->stored.empty();
    if (maxDur > 0 && anvilRepairsWith(left.id, right.id)) {
      // Reparar con material: cada unidad devuelve un cuarto de la durabilidad (lo que falte, si es menos)
      int step = std::min<int>(out.meta, maxDur / 4);
      if (step <= 0) return none;
      int used = 0;
      for (; step > 0 && used < right.count; used++) {
        out.meta = static_cast<i16>(out.meta - step);
        extra += 1;
        step = std::min<int>(out.meta, maxDur / 4);
      }
      materialUsed = used;
    } else {
      if (!rightBook && (out.id != right.id || maxDur <= 0)) return none;
      if (maxDur > 0 && !rightBook) {
        // Dos iguales: se suman las durabilidades que quedan y un 12 % de propina
        const int leftLeft = maxDur - left.meta, rightLeft = maxDur - right.meta;
        const int bonus = rightLeft + maxDur * 12 / 100;
        const int newDamage = std::max(0, maxDur - (leftLeft + bonus));
        if (newDamage < out.meta) {
          out.meta = static_cast<i16>(newDamage);
          extra += 2;
        }
      }
      for (const auto& [id, rightLevel] : anvilEnchants(right)) {
        const EnchantInfo* e = enchantInfo(id);
        if (!e) continue;
        int level = rightLevel;
        int current = 0;
        for (const auto& [mid, mlevel] : map)
          if (mid == id) current = mlevel;
        level = current == rightLevel ? rightLevel + 1 : std::max(rightLevel, current);
        // Sirve si el objeto la admite (un libro admite todas; en creativo, cualquiera) y no choca con las que ya lleva
        bool apply = creative || left.id == ItemId::enchanted_book || canEnchant(*e, left.id, false);
        for (const auto& [mid, mlevel] : map) {
          (void)mlevel;
          if (mid != id && !enchantsCompatible(id, mid)) {
            apply = false;
            extra += 1;  // (cada choque encarece aunque no se aplique)
          }
        }
        if (!apply) continue;
        level = std::min(level, e->maxLevel);
        bool found = false;
        for (auto& [mid, mlevel] : map)
          if (mid == id) {
            mlevel = level;
            found = true;
          }
        if (!found) map.emplace_back(id, level);
        int mult = rarityMultiplier(e->weight);
        if (rightBook) mult = std::max(1, mult / 2);
        extra += mult * level;
      }
    }
  }
  // Renombrar
  const bool blank = newName.empty() || std::all_of(newName.begin(), newName.end(), [](char c) { return c == ' '; });
  if (blank) {
    if (left.extra && !left.extra->name.empty()) {
      renameCost = 1;
      extra += 1;
      ItemExtra e = out.copyExtra();
      e.name.clear();
      out.setExtra(std::move(e));
    }
  } else if (newName != anvilDisplayName(left)) {
    renameCost = 1;
    extra += 1;
    ItemExtra e = out.copyExtra();
    e.name = newName;
    out.setExtra(std::move(e));
  }
  int cost = baseCost + extra;
  if (extra <= 0) return none;
  if (renameCost == extra && renameCost > 0 && cost >= 40) cost = 39;  // solo renombrar nunca llega a "demasiado caro"
  if (cost >= 40 && !creative) {
    AnvilResult r;
    r.cost = cost;
    r.tooExpensive = true;
    return r;
  }
  // Cada uso (menos el de solo renombrar) duplica la penitencia del objeto y le suma 1
  int repair = repairCostOf(out);
  if (!right.empty() && repair < repairCostOf(right)) repair = repairCostOf(right);
  if (renameCost != extra || renameCost == 0) repair = repair * 2 + 1;
  ItemExtra e = out.copyExtra();
  e.repairCost = repair;
  out.setExtra(std::move(e));
  if (!map.empty() || !anvilEnchants(out).empty()) setEnchants(out, map);
  AnvilResult r;
  r.output = out;
  r.cost = cost;
  r.materialUsed = materialUsed;
  return r;
}

int anvilNextDamage(int currentDamage) { return currentDamage >= 2 ? -1 : currentDamage + 1; }

}  // namespace mcw
