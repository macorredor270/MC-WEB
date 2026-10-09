#include "game/menu.h"

#include "game/armor.h"
#include "game/crafting.h"
#include "game/player.h"
#include "game/rules.h"

namespace mcw {

bool FurnaceState::tick() {
  const bool wasBurning = burning();
  if (burnTime > 0) burnTime--;
  const auto result = input.empty() ? std::nullopt : smeltingResult(input);
  const bool canSmelt = result && (output.empty() || (output.stacksWith(*result) && output.count + result->count <= output.maxStack()));
  if (burnTime == 0 && canSmelt && !fuel.empty() && fuelTicks(fuel) > 0) {
    burnTotal = burnTime = fuelTicks(fuel);
    if (--fuel.count <= 0) fuel.clear();
  }
  if (burning() && canSmelt) {
    if (++cookTime >= kCookTicks) {
      cookTime = 0;
      if (output.empty()) output = *result;
      else output.count = static_cast<i16>(output.count + result->count);
      if (--input.count <= 0) input.clear();
    }
  } else {
    cookTime = 0;
  }
  return wasBurning != burning();
}

Menu::Menu(MenuKind kind, Player& player, FurnaceState* furnace, ItemStack* chest)
    : kind_(kind), player_(player), furnace_(furnace), chest_(chest) {
  build();
}

void Menu::addPlayerSlots(int invY, int hotbarY) {
  playerStart_ = static_cast<int>(slots_.size());
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 9; c++) {
      const int idx = 9 + r * 9 + c;
      slots_.push_back({8 + c * 18, invY + r * 18, SlotRole::Storage, &player_.inventory.slot(idx), idx});
    }
  for (int c = 0; c < 9; c++) slots_.push_back({8 + c * 18, hotbarY, SlotRole::Storage, &player_.inventory.slot(c), c});
}

void Menu::build() {
  slots_.clear();
  switch (kind_) {
    case MenuKind::Inventory:
      texture_ = "gui/container/inventory.png";
      gridSize_ = 2;
      slots_.push_back({154, 28, SlotRole::CraftResult, &result_, -1});
      for (int r = 0; r < 2; r++)
        for (int c = 0; c < 2; c++) slots_.push_back({98 + c * 18, 18 + r * 18, SlotRole::Craft, &grid_[r * 2 + c], -1});
      // Armadura: casco, pechera, pantalones y botas (casillas 5 a 8, como en el protocolo de 1.8)
      for (int i = 0; i < 4; i++) slots_.push_back({8, 8 + i * 18, SlotRole::Armor, &player_.inventory.armor(3 - i), -1, 3 - i});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Crafting:
      texture_ = "gui/container/crafting_table.png";
      gridSize_ = 3;
      slots_.push_back({124, 35, SlotRole::CraftResult, &result_, -1});
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) slots_.push_back({30 + c * 18, 17 + r * 18, SlotRole::Craft, &grid_[r * 3 + c], -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Furnace:
      texture_ = "gui/container/furnace.png";
      slots_.push_back({56, 17, SlotRole::FurnaceInput, &furnace_->input, -1});
      slots_.push_back({56, 53, SlotRole::FurnaceFuel, &furnace_->fuel, -1});
      slots_.push_back({116, 35, SlotRole::FurnaceOutput, &furnace_->output, -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Chest:
      // Como en 1.8: la ventana de 6 filas recortada a 3 (el cliente dibuja las dos partes)
      texture_ = "gui/container/generic_54.png";
      height_ = 3 * 18 + 17 + 96;
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 9; c++) slots_.push_back({8 + c * 18, 18 + r * 18, SlotRole::Storage, &chest_[r * 9 + c], -1});
      addPlayerSlots(85, 143);
      break;
    case MenuKind::Creative:
      texture_ = "gui/container/generic_54.png";
      height_ = 222;
      for (int r = 0; r < 6; r++)
        for (int c = 0; c < 9; c++) slots_.push_back({8 + c * 18, 18 + r * 18, SlotRole::Source, &creativeView_[r * 9 + c], -1});
      addPlayerSlots(140, 198);
      refreshCreative();
      break;
  }
}

int Menu::maxScroll() const {
  const int rows = (static_cast<int>(creativeItems().size()) + 8) / 9;
  return std::max(0, rows - 6);
}

void Menu::scroll(int rows) {
  if (kind_ != MenuKind::Creative) return;
  scroll_ = std::clamp(scroll_ + rows, 0, maxScroll());
  refreshCreative();
}

void Menu::refreshCreative() {
  const auto& all = creativeItems();
  for (int i = 0; i < 54; i++) {
    const int idx = scroll_ * 9 + i;
    creativeView_[i] = idx < static_cast<int>(all.size()) ? all[idx] : ItemStack();
  }
}

void Menu::updateResult() {
  if (gridSize_ == 0) return;
  const auto r = matchRecipe(std::span<const ItemStack>(grid_.data(), static_cast<std::size_t>(gridSize_ * gridSize_)), gridSize_);
  result_ = r.value_or(ItemStack());
}

ItemStack Menu::moveInto(ItemStack s, int from, int to, bool reverse) {
  for (int pass = 0; pass < 2 && !s.empty(); pass++) {
    for (int k = 0; k < to - from && !s.empty(); k++) {
      MenuSlot& slot = slots_[reverse ? to - 1 - k : from + k];
      ItemStack& dst = *slot.stack;
      if (pass == 0 && dst.stacksWith(s)) {
        const int n = std::min<int>(s.count, dst.maxStack() - dst.count);
        dst.count = static_cast<i16>(dst.count + n);
        s.count = static_cast<i16>(s.count - n);
      } else if (pass == 1 && dst.empty()) {
        dst = s;
        s.clear();
      }
    }
  }
  if (s.count <= 0) s.clear();
  return s;
}

void Menu::takeResult(bool shift) {
  if (result_.empty()) return;
  if (shift) {
    // Fabricar todo lo posible y guardarlo en el inventario
    for (int guard = 0; guard < 64 && !result_.empty(); guard++) {
      if (player_.inventory.roomFor(result_) < result_.count) break;
      player_.crafted.push_back(result_);
      player_.inventory.add(result_);
      consumeIngredients(std::span<ItemStack>(grid_.data(), static_cast<std::size_t>(gridSize_ * gridSize_)));
      updateResult();
    }
    return;
  }
  ItemStack& cur = player_.cursor;
  if (cur.empty()) cur = result_;
  else if (cur.stacksWith(result_) && cur.count + result_.count <= cur.maxStack()) cur.count = static_cast<i16>(cur.count + result_.count);
  else return;
  player_.crafted.push_back(result_);
  consumeIngredients(std::span<ItemStack>(grid_.data(), static_cast<std::size_t>(gridSize_ * gridSize_)));
  updateResult();
}

void Menu::click(int index, int button, bool shift) {
  if (index < 0 || index >= static_cast<int>(slots_.size())) return;
  MenuSlot& slot = slots_[index];
  ItemStack& s = *slot.stack;
  ItemStack& cur = player_.cursor;

  switch (slot.role) {
    case SlotRole::CraftResult: takeResult(shift); return;
    case SlotRole::Source:
      if (!cur.empty()) { cur.clear(); return; }  // soltar sobre la lista lo destruye
      if (s.empty()) return;
      if (shift) {
        ItemStack full = s;
        full.count = static_cast<i16>(full.maxStack());
        player_.inventory.add(full);
      } else {
        cur = s;
        cur.count = static_cast<i16>(button == 0 ? cur.maxStack() : 1);
      }
      return;
    case SlotRole::FurnaceOutput:
      if (s.empty()) return;
      player_.smelted.push_back(s);
      if (shift) s = player_.inventory.add(s);
      else if (cur.empty()) { cur = s; s.clear(); }
      else if (cur.stacksWith(s) && cur.count + s.count <= cur.maxStack()) { cur.count = static_cast<i16>(cur.count + s.count); s.clear(); }
      return;
    default: break;
  }

  if (shift) {
    if (s.empty()) return;
    if (slot.inventoryIndex >= 0) {
      const int invPos = index - playerStart_;  // 0..26 = parte principal, 27..35 = barra rápida
      ItemStack rest = s;
      if (kind_ == MenuKind::Inventory) {  // una pieza de armadura va a su casilla si está libre
        if (const auto info = armorInfo(rest.id)) {
          ItemStack& dst = *slots_[5 + (3 - static_cast<int>(info->piece))].stack;
          if (dst.empty()) {
            dst = rest;
            dst.count = 1;
            rest.clear();
          }
        }
      }
      if (rest.empty()) {
        s = rest;
        updateResult();
        return;
      }
      if (kind_ == MenuKind::Chest) rest = moveInto(rest, 0, 27, false);
      else if (kind_ == MenuKind::Furnace && smeltingResult(rest)) rest = moveInto(rest, 0, 1, false);
      else if (kind_ == MenuKind::Furnace && fuelTicks(rest) > 0) rest = moveInto(rest, 1, 2, false);
      if (!rest.empty()) {
        if (invPos < 27) rest = moveInto(rest, playerStart_ + 27, playerStart_ + 36, false);
        else rest = moveInto(rest, playerStart_, playerStart_ + 27, false);
      }
      s = rest;
    } else {
      s = moveInto(s, playerStart_, playerStart_ + 36, true);
    }
    updateResult();
    return;
  }

  if (slot.role == SlotRole::Armor && !cur.empty()) {
    // Solo cabe la pieza que va ahí (una sola)
    const auto info = armorInfo(cur.id);
    if (!info || static_cast<int>(info->piece) != slot.armorIndex) return;
    if (!s.empty()) {
      std::swap(cur, s);
    } else {
      s = cur;
      s.count = 1;
      if (--cur.count <= 0) cur.clear();
    }
    return;
  }
  if (button == 0) {
    if (cur.empty()) {
      std::swap(cur, s);
    } else if (s.empty()) {
      std::swap(cur, s);
    } else if (s.stacksWith(cur)) {
      const int n = std::min<int>(cur.count, s.maxStack() - s.count);
      s.count = static_cast<i16>(s.count + n);
      cur.count = static_cast<i16>(cur.count - n);
      if (cur.count <= 0) cur.clear();
    } else {
      std::swap(cur, s);
    }
  } else {
    if (cur.empty()) {
      if (s.empty()) return;
      const int half = (s.count + 1) / 2;
      cur = s;
      cur.count = static_cast<i16>(half);
      s.count = static_cast<i16>(s.count - half);
      if (s.count <= 0) s.clear();
    } else if (s.empty() || (s.stacksWith(cur) && s.count < s.maxStack())) {
      if (s.empty()) {
        s = cur;
        s.count = 1;
      } else {
        s.count = static_cast<i16>(s.count + 1);
      }
      if (--cur.count <= 0) cur.clear();
    } else if (!s.stacksWith(cur)) {
      std::swap(cur, s);
    }
  }
  if (slot.role == SlotRole::Craft) updateResult();
}

void Menu::clickOutside(int button, std::vector<ItemStack>& dropped) {
  ItemStack& cur = player_.cursor;
  if (cur.empty()) return;
  if (button == 0) {
    dropped.push_back(cur);
    cur.clear();
  } else {
    ItemStack one = cur;
    one.count = 1;
    dropped.push_back(one);
    if (--cur.count <= 0) cur.clear();
  }
}

void Menu::close(std::vector<ItemStack>& dropped) {
  for (int i = 0; i < gridSize_ * gridSize_; i++) {
    if (grid_[i].empty()) continue;
    const ItemStack rest = player_.inventory.add(grid_[i]);
    if (!rest.empty()) dropped.push_back(rest);
    grid_[i].clear();
  }
  result_.clear();
  if (!player_.cursor.empty()) {
    const ItemStack rest = player_.inventory.add(player_.cursor);
    if (!rest.empty()) dropped.push_back(rest);
    player_.cursor.clear();
  }
}

}  // namespace mcw
