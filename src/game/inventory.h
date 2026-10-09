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
  /// Armadura puesta: 0 botas, 1 pantalones, 2 pechera, 3 casco (como en 1.8).
  ItemStack& armor(int i) { return armor_[i]; }
  const ItemStack& armor(int i) const { return armor_[i]; }
  /// Puntos de armadura (suma de las piezas puestas, 0..20).
  int armorPoints() const;
  /// Los objetos de armadura puestos (para dibujarlos): 0 botas .. 3 casco; 0 = nada.
  std::array<i16, 4> armorIds() const {
    std::array<i16, 4> ids{};
    for (int i = 0; i < 4; i++) ids[static_cast<std::size_t>(i)] = armor_[static_cast<std::size_t>(i)].empty() ? 0 : armor_[static_cast<std::size_t>(i)].id;
    return ids;
  }
  ItemStack& selected() { return slots_[selected_]; }
  const ItemStack& selected() const { return slots_[selected_]; }
  int selectedIndex() const { return selected_; }
  void select(int i) { selected_ = ((i % kHotbar) + kHotbar) % kHotbar; }

  /// Mete una pila (primero juntando con las que ya hay, luego en huecos vacíos, empezando por
  /// la hotbar). Devuelve lo que no ha cabido.
  ItemStack add(ItemStack stack);
  /// Cuántos ítems de este tipo caben todavía.
  int roomFor(const ItemStack& stack) const;
  void clear() {
    slots_.fill({});
    armor_.fill({});
  }
  bool isEmpty() const;

 private:
  std::array<ItemStack, kSize> slots_{};
  std::array<ItemStack, 4> armor_{};
  int selected_ = 0;
};

}  // namespace mcw
