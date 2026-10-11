#include "game/menu.h"

#include "game/anvil.h"
#include "game/armor.h"
#include "game/crafting.h"
#include "game/effects.h"
#include "game/enchantments.h"
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

Menu::Menu(MenuKind kind, Player& player, FurnaceState* furnace, ItemStack* chest, int bookshelves)
    : kind_(kind), player_(player), furnace_(furnace), bookshelves_(bookshelves) {
  const int n = kind == MenuKind::Chest ? 27 : kind == MenuKind::Brewing ? 4 : kind == MenuKind::Hopper ? 5 : (kind == MenuKind::Dispenser || kind == MenuKind::Dropper) ? 9 : 0;
  for (int i = 0; chest && i < n; i++) container_.push_back(chest + i);
  build();
}

Menu::Menu(MenuKind kind, Player& player, std::vector<ItemStack*> container)
    : kind_(kind), player_(player), furnace_(nullptr), container_(std::move(container)) {
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
      armorBase_ = 5;
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
    case MenuKind::Enchant:
      texture_ = "gui/container/enchanting_table.png";
      slots_.push_back({15, 47, SlotRole::EnchantItem, &enchantSlots_[0], -1});
      slots_.push_back({35, 47, SlotRole::EnchantLapis, &enchantSlots_[1], -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Anvil:
      texture_ = "gui/container/anvil.png";
      slots_.push_back({27, 47, SlotRole::AnvilLeft, &anvilSlots_[0], -1});
      slots_.push_back({76, 47, SlotRole::AnvilRight, &anvilSlots_[1], -1});
      slots_.push_back({134, 47, SlotRole::AnvilOutput, &anvilOut_, -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Chest: {
      // Como en 1.8: la ventana de 6 filas recortada a las que haga falta (3 en un cofre, 6 en uno doble)
      texture_ = "gui/container/generic_54.png";
      const int rows = std::max(1, static_cast<int>(container_.size()) / 9);
      height_ = rows * 18 + 17 + 96;
      for (int r = 0; r < rows; r++)
        for (int c = 0; c < 9; c++)
          slots_.push_back({8 + c * 18, 18 + r * 18, SlotRole::Storage, container_.empty() ? &trash_ : container_[static_cast<std::size_t>(r * 9 + c)], -1});
      addPlayerSlots(rows * 18 + 31, rows * 18 + 89);
      break;
    }
    case MenuKind::Hopper:
      texture_ = "gui/container/hopper.png";
      height_ = 133;
      for (int c = 0; c < 5; c++) slots_.push_back({44 + c * 18, 20, SlotRole::Storage, container_.empty() ? &trash_ : container_[static_cast<std::size_t>(c)], -1});
      addPlayerSlots(51, 109);
      break;
    case MenuKind::Brewing:
      texture_ = "gui/container/brewing_stand.png";
      height_ = 166;
      slots_.push_back({56, 46, SlotRole::BrewBottle, container_.empty() ? &trash_ : container_[0], -1});
      slots_.push_back({79, 53, SlotRole::BrewBottle, container_.empty() ? &trash_ : container_[1], -1});
      slots_.push_back({102, 46, SlotRole::BrewBottle, container_.empty() ? &trash_ : container_[2], -1});
      slots_.push_back({79, 17, SlotRole::BrewIngredient, container_.empty() ? &trash_ : container_[3], -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Dispenser: case MenuKind::Dropper:
      texture_ = "gui/container/dispenser.png";
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
          slots_.push_back({62 + c * 18, 17 + r * 18, SlotRole::Storage, container_.empty() ? &trash_ : container_[static_cast<std::size_t>(r * 3 + c)], -1});
      addPlayerSlots(84, 142);
      break;
    case MenuKind::Creative:
      width_ = 195;
      height_ = 136;
      buildCreative();
      break;
  }
}

// --- Modo creativo ---

const std::vector<ItemStack>& Menu::list() const {
  static const std::vector<ItemStack> none;
  if (tab_ == CreativeTab::Search) return searchResult_;
  if (tab_ == CreativeTab::Inventory) return none;
  return creativeTabItems(tab_);
}

void Menu::buildCreative() {
  slots_.clear();
  armorBase_ = -1;
  switch (tab_) {
    case CreativeTab::Search: texture_ = "gui/container/creative_inventory/tab_item_search.png"; break;
    case CreativeTab::Inventory: texture_ = "gui/container/creative_inventory/tab_inventory.png"; break;
    default: texture_ = "gui/container/creative_inventory/tab_items.png"; break;
  }
  if (tab_ == CreativeTab::Inventory) {
    // Inventario de supervivencia: la armadura a los lados del jugador, el inventario, la barra rápida y la papelera
    armorBase_ = 0;
    slots_.push_back({9, 6, SlotRole::Armor, &player_.inventory.armor(3), -1, 3});
    slots_.push_back({9, 33, SlotRole::Armor, &player_.inventory.armor(2), -1, 2});
    slots_.push_back({63, 6, SlotRole::Armor, &player_.inventory.armor(1), -1, 1});
    slots_.push_back({63, 33, SlotRole::Armor, &player_.inventory.armor(0), -1, 0});
    playerStart_ = static_cast<int>(slots_.size());
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 9; c++) {
        const int i = 9 + r * 9 + c;
        slots_.push_back({9 + c * 18, 54 + r * 18, SlotRole::Storage, &player_.inventory.slot(i), i});
      }
    for (int c = 0; c < 9; c++) slots_.push_back({9 + c * 18, 112, SlotRole::Storage, &player_.inventory.slot(c), c});
    slots_.push_back({173, 112, SlotRole::Trash, &trash_, -1});
  } else {
    for (int r = 0; r < kCreativeRows; r++)
      for (int c = 0; c < kCreativeCols; c++)
        slots_.push_back({9 + c * 18, 18 + r * 18, SlotRole::Source, &creativeView_[static_cast<std::size_t>(r * kCreativeCols + c)], -1});
    playerStart_ = static_cast<int>(slots_.size());
    for (int c = 0; c < 9; c++) slots_.push_back({9 + c * 18, 112, SlotRole::Storage, &player_.inventory.slot(c), c});
  }
  refreshCreative();
}

void Menu::setCreativeTab(CreativeTab tab) {
  if (kind_ != MenuKind::Creative) return;
  tab_ = tab;
  scroll_ = 0;
  if (tab == CreativeTab::Search) {
    search_.clear();
    searchResult_ = creativeSearch(search_);
  }
  buildCreative();
}

void Menu::setSearchText(std::string text) {
  search_ = std::move(text);
  if (kind_ != MenuKind::Creative || tab_ != CreativeTab::Search) return;
  searchResult_ = creativeSearch(search_);
  scroll_ = 0;
  refreshCreative();
}

void Menu::typeSearch(std::string_view text) {
  constexpr std::size_t kMaxSearch = 40;
  std::string s = search_;
  for (const char c : text) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) continue;  // saltos de línea y demás caracteres de control
    if (s.size() >= kMaxSearch) break;
    s += c;
  }
  if (s != search_) setSearchText(std::move(s));
}

void Menu::eraseSearchChar() {
  if (search_.empty()) return;
  std::size_t n = search_.size() - 1;
  while (n > 0 && (static_cast<unsigned char>(search_[n]) & 0xC0) == 0x80) n--;  // continuación UTF-8
  setSearchText(search_.substr(0, n));
}

int Menu::maxScroll() const {
  if (kind_ != MenuKind::Creative) return 0;
  const int rows = (listSize() + kCreativeCols - 1) / kCreativeCols;
  return std::max(0, rows - kCreativeRows);
}

void Menu::hotkey(int index, int hotbarIndex) {
  if (kind_ != MenuKind::Creative || index < 0 || index >= static_cast<int>(slots_.size()) || hotbarIndex < 0 || hotbarIndex >= PlayerInventory::kHotbar) return;
  const MenuSlot& slot = slots_[static_cast<std::size_t>(index)];
  ItemStack& hot = player_.inventory.slot(hotbarIndex);
  if (slot.role == SlotRole::Source) {
    if (slot.stack->empty()) return;
    hot = *slot.stack;
    hot.count = static_cast<i16>(hot.maxStack());
  } else if (slot.role == SlotRole::Storage && slot.stack != &hot) {
    std::swap(*slot.stack, hot);
  }
}

void Menu::setScrollRow(int row) {
  if (kind_ != MenuKind::Creative) return;
  scroll_ = std::clamp(row, 0, maxScroll());
  refreshCreative();
}

void Menu::scroll(int rows) { setScrollRow(scroll_ + rows); }

void Menu::refreshCreative() {
  const auto& all = list();
  for (int i = 0; i < kCreativeVisible; i++) {
    const std::size_t idx = static_cast<std::size_t>(scroll_ * kCreativeCols + i);
    creativeView_[static_cast<std::size_t>(i)] = idx < all.size() ? all[idx] : ItemStack();
  }
}

ItemStack Menu::moveToInventory(ItemStack s, int from, int to) {
  for (int pass = 0; pass < 2 && !s.empty(); pass++) {
    for (int i = from; i < to && !s.empty(); i++) {
      ItemStack& dst = player_.inventory.slot(i);
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

void Menu::updateEnchant() {
  if (kind_ != MenuKind::Enchant || remoteOffers_) return;
  offers_ = enchantOffers(enchantSlots_[0], bookshelves_, player_.xpSeed);
}

// --- Yunque ---

void Menu::setAnvilName(std::string name) {
  if (name.size() > 35) name.resize(35);  // (el campo de 1.8 admite 30 y pico; los bytes de más se cortan)
  anvilName_ = std::move(name);
  updateAnvil();
}

bool Menu::anvilCanTake() const {
  if (anvilOut_.empty() || anvilExpensive_) return false;
  return player_.creative() || player_.xpLevel >= anvilCost_;
}

void Menu::updateAnvil() {
  if (kind_ != MenuKind::Anvil) return;
  // Al cambiar el objeto de la izquierda, el campo se rellena con su nombre
  const std::string current = anvilSlots_[0].empty() ? std::string() : anvilDisplayName(anvilSlots_[0]);
  if (current != anvilNameFor_) {
    anvilNameFor_ = current;
    anvilName_ = current;
  }
  if (remoteAnvil_) return;  // en un servidor, lo calcula él
  if (anvilSlots_[0].empty()) {
    anvilOut_.clear();
    anvilCost_ = anvilMaterial_ = 0;
    anvilExpensive_ = false;
    return;
  }
  const AnvilResult r = anvilCompute(anvilSlots_[0], anvilSlots_[1], anvilName_, player_.creative());
  anvilOut_ = r.output;
  anvilCost_ = r.cost;
  anvilMaterial_ = r.materialUsed;
  anvilExpensive_ = r.tooExpensive;
}

void Menu::takeAnvilResult(bool shift) {
  if (!anvilCanTake()) return;
  ItemStack& cur = player_.cursor;
  if (shift) {
    if (player_.inventory.roomFor(anvilOut_) < anvilOut_.count) return;
    player_.inventory.add(anvilOut_);
  } else if (cur.empty()) {
    cur = anvilOut_;
  } else {
    return;  // (hay que tener la mano libre para llevarse el resultado)
  }
  if (!player_.creative()) player_.addXpLevels(-anvilCost_);
  anvilSlots_[0].clear();
  if (anvilMaterial_ > 0 && anvilSlots_[1].count > anvilMaterial_) anvilSlots_[1].count = static_cast<i16>(anvilSlots_[1].count - anvilMaterial_);
  else anvilSlots_[1].clear();
  anvilUses_++;
  anvilOut_.clear();
  anvilNameFor_.clear();
  anvilName_.clear();
  updateAnvil();
}

i32 Menu::runeSeed() const { return remoteOffers_ ? remoteSeed_ : player_.xpSeed; }

bool Menu::canEnchant(int button) const {
  if (kind_ != MenuKind::Enchant || button < 0 || button > 2) return false;
  const ItemStack& item = enchantSlots_[0];
  const ItemStack& lapis = enchantSlots_[1];
  const int need = button + 1;
  const EnchantOffer& o = offers_[static_cast<std::size_t>(button)];
  if (item.empty() || o.cost <= 0) return false;
  if (player_.creative()) return true;
  return !lapis.empty() && lapis.count >= need && player_.xpLevel >= need && player_.xpLevel >= o.cost;
}

bool Menu::enchant(int button) {
  if (!canEnchant(button)) return false;
  ItemStack& item = enchantSlots_[0];
  ItemStack& lapis = enchantSlots_[1];
  const int need = button + 1;
  const bool creative = player_.creative();
  const EnchantOffer& o = offers_[static_cast<std::size_t>(button)];
  const auto list = enchantList(item, button, o.cost, player_.xpSeed);
  if (list.empty()) return false;
  if (!creative) {
    player_.onEnchant(need);
    if ((lapis.count = static_cast<i16>(lapis.count - need)) <= 0) lapis.clear();
  }
  item = applyEnchants(item, list);
  player_.enchanted++;
  updateEnchant();
  return true;
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
    case SlotRole::AnvilOutput: takeAnvilResult(shift); return;
    case SlotRole::Trash:  // la papelera se come lo que se lleve en el cursor
      cur.clear();
      return;
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
    if (kind_ == MenuKind::Creative && tab_ != CreativeTab::Inventory) {
      // En las pestañas de objetos solo se ve la barra rápida: mayús manda la pila al resto del inventario
      if (slot.inventoryIndex >= 0 && slot.inventoryIndex < PlayerInventory::kHotbar) s = moveToInventory(s, PlayerInventory::kHotbar, PlayerInventory::kSize);
      return;
    }
    if (slot.inventoryIndex >= 0) {
      const int invPos = index - playerStart_;  // 0..26 = parte principal, 27..35 = barra rápida
      ItemStack rest = s;
      if (armorBase_ >= 0) {  // una pieza de armadura va a su casilla si está libre
        if (const auto info = armorInfo(rest.id)) {
          ItemStack& dst = *slots_[static_cast<std::size_t>(armorBase_ + (3 - static_cast<int>(info->piece)))].stack;
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
      if (kind_ == MenuKind::Enchant) {  // todo va a las casillas de la mesa (como en 1.8): el lapislázuli a la suya, lo demás al objeto
        if (rest.id == ItemId::dye && rest.meta == 4) {
          rest = moveInto(rest, 1, 2, false);
        } else if (slots_[0].stack->empty()) {  // el objeto: de uno en uno
          *slots_[0].stack = rest;
          slots_[0].stack->count = 1;
          if (--rest.count <= 0) rest.clear();
        }
        s = rest;
        updateEnchant();
        return;
      }
      if (kind_ == MenuKind::Anvil) {  // a la casilla del objeto si está libre; si no, a la de al lado
        if (slots_[0].stack->empty()) {
          *slots_[0].stack = rest;
          rest.clear();
        } else if (slots_[1].stack->empty()) {
          *slots_[1].stack = rest;
          rest.clear();
        }
        s = rest;
        updateAnvil();
        return;
      }
      if (kind_ == MenuKind::Brewing && containerSize() >= 4) {  // botellas a sus casillas (una en cada) y el ingrediente a la suya
        if (rest.id == ItemId::potion || rest.id == ItemId::glass_bottle) {
          for (int i = 0; i < 3 && !rest.empty(); i++)
            if (slots_[static_cast<std::size_t>(i)].stack->empty()) {
              *slots_[static_cast<std::size_t>(i)].stack = rest;
              slots_[static_cast<std::size_t>(i)].stack->count = 1;
              if (--rest.count <= 0) rest.clear();
            }
        } else if (potion::isIngredient(rest.id, rest.meta)) {
          rest = moveInto(rest, 3, 4, false);
        }
      } else if (containerSize() > 0 && (kind_ == MenuKind::Chest || kind_ == MenuKind::Hopper || kind_ == MenuKind::Dispenser || kind_ == MenuKind::Dropper))
        rest = moveInto(rest, 0, containerSize(), false);
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
    updateAnvil();
    return;
  }

  if ((slot.role == SlotRole::EnchantItem || slot.role == SlotRole::EnchantLapis) && !cur.empty()) {
    const bool lapisSlot = slot.role == SlotRole::EnchantLapis;
    if (lapisSlot && !(cur.id == ItemId::dye && cur.meta == 4)) return;  // solo lapislázuli
    const int limit = lapisSlot ? 64 : 1;                                 // y el objeto, de uno en uno
    const int want = button == 0 ? cur.count : 1;
    if (s.empty()) {
      const int n = std::min(want, limit);
      s = cur;
      s.count = static_cast<i16>(n);
      if ((cur.count = static_cast<i16>(cur.count - n)) <= 0) cur.clear();
    } else if (s.stacksWith(cur)) {
      const int n = std::min({want, limit - static_cast<int>(s.count), static_cast<int>(cur.count)});
      s.count = static_cast<i16>(s.count + n);
      if ((cur.count = static_cast<i16>(cur.count - n)) <= 0) cur.clear();
    } else if (cur.count <= limit) {
      std::swap(cur, s);  // (intercambiar el objeto por otro: el que sale pasa al cursor)
    }
    updateEnchant();
    return;
  }
  if ((slot.role == SlotRole::BrewBottle || slot.role == SlotRole::BrewIngredient) && !cur.empty()) {
    const bool bottle = slot.role == SlotRole::BrewBottle;
    if (bottle ? !(cur.id == ItemId::potion || cur.id == ItemId::glass_bottle) : !potion::isIngredient(cur.id, cur.meta)) return;
    const int limit = bottle ? 1 : 64;  // (las botellas, de una en una)
    const int want = button == 0 ? cur.count : 1;
    if (s.empty()) {
      const int n = std::min(want, limit);
      s = cur;
      s.count = static_cast<i16>(n);
      if ((cur.count = static_cast<i16>(cur.count - n)) <= 0) cur.clear();
    } else if (s.stacksWith(cur)) {
      const int n = std::min({want, limit - static_cast<int>(s.count), static_cast<int>(cur.count)});
      if (n > 0) {
        s.count = static_cast<i16>(s.count + n);
        if ((cur.count = static_cast<i16>(cur.count - n)) <= 0) cur.clear();
      }
    } else if (cur.count <= limit) {
      std::swap(cur, s);
    }
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
  if (slot.role == SlotRole::EnchantItem) updateEnchant();
  if (slot.role == SlotRole::AnvilLeft || slot.role == SlotRole::AnvilRight) updateAnvil();
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
  for (ItemStack& e : enchantSlots_) {
    if (e.empty()) continue;
    const ItemStack rest = player_.inventory.add(e);
    if (!rest.empty()) dropped.push_back(rest);
    e.clear();
  }
  for (ItemStack& a : anvilSlots_) {
    if (a.empty()) continue;
    const ItemStack rest = player_.inventory.add(a);
    if (!rest.empty()) dropped.push_back(rest);
    a.clear();
  }
  anvilOut_.clear();
  if (!player_.cursor.empty()) {
    const ItemStack rest = player_.inventory.add(player_.cursor);
    if (!rest.empty()) dropped.push_back(rest);
    player_.cursor.clear();
  }
}

}  // namespace mcw
