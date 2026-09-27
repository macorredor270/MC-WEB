#pragma once
#include <array>

#include "game/item_stack.h"

namespace mcw {

/// Inventario del jugador: 0..8 es la barra rápida (hotbar), 9..35 el resto.
class PlayerInventory {
 public:
  static constexpr int kSize = 36;
  static constexpr int kHotbar = 9;

  ItemStack& slot(int i) { return slots_[i]; }
  const ItemStack& slot(int i) const { return slots_[i]; }
  ItemStack& selected() { return slots_[selected_]; }
  const ItemStack& selected() const { return slots_[selected_]; }
  int selectedIndex() const { return selected_; }
  void select(int i) { selected_ = ((i % kHotbar) + kHotbar) % kHotbar; }

  /// Mete una pila (primero juntando con las que ya hay, luego en huecos vacíos, empezando por
  /// la hotbar). Devuelve lo que no ha cabido.
  ItemStack add(ItemStack stack);
  /// Cuántos ítems de este tipo caben todavía.
  int roomFor(const ItemStack& stack) const;
  void clear() { slots_.fill({}); }
  bool isEmpty() const;

 private:
  std::array<ItemStack, kSize> slots_{};
  int selected_ = 0;
};

}  // namespace mcw
