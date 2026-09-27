#include "game/crafting.h"

#include <vector>

namespace mcw {
namespace {

bool matches(const Ingredient& ing, const ItemStack& s) {
  if (ing.id == 0) return s.empty();
  if (s.empty() || s.id != ing.id) return false;
  return ing.meta < 0 || ing.meta == s.meta;
}

bool tryShaped(const ShapedRecipe& r, std::span<const ItemStack> grid, int size, int ox, int oy, bool mirror) {
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      const int rx = x - ox, ry = y - oy;
      Ingredient ing{};
      if (rx >= 0 && ry >= 0 && rx < r.width && ry < r.height) ing = r.cells[ry * r.width + (mirror ? r.width - 1 - rx : rx)];
      if (!matches(ing, grid[y * size + x])) return false;
    }
  return true;
}

}  // namespace

std::optional<ItemStack> matchRecipe(std::span<const ItemStack> grid, int size) {
  bool any = false;
  for (const ItemStack& s : grid) any |= !s.empty();
  if (!any) return std::nullopt;

  for (const ShapedRecipe& r : shapedRecipes()) {
    if (r.width > size || r.height > size) continue;
    for (int oy = 0; oy + r.height <= size; oy++)
      for (int ox = 0; ox + r.width <= size; ox++)
        for (bool mirror : {false, true})
          if (tryShaped(r, grid, size, ox, oy, mirror)) return ItemStack(r.id, r.count, r.meta);
  }

  std::vector<const ItemStack*> items;
  for (const ItemStack& s : grid)
    if (!s.empty()) items.push_back(&s);
  for (const ShapelessRecipe& r : shapelessRecipes()) {
    if (r.n != static_cast<int>(items.size())) continue;
    std::vector<bool> used(items.size(), false);
    bool ok = true;
    for (int i = 0; i < r.n && ok; i++) {
      ok = false;
      for (std::size_t j = 0; j < items.size(); j++)
        if (!used[j] && matches(r.cells[i], *items[j])) { used[j] = true; ok = true; break; }
    }
    if (ok) return ItemStack(r.id, r.count, r.meta);
  }
  return std::nullopt;
}

void consumeIngredients(std::span<ItemStack> grid) {
  for (ItemStack& s : grid) {
    if (s.empty()) continue;
    s.count = static_cast<i16>(s.count - 1);
    if (s.count <= 0) s.clear();
  }
}

}  // namespace mcw
