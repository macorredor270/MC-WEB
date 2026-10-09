#pragma once
#include <array>
#include <string>
#include <vector>

#include "game/item_stack.h"

namespace mcw {

class Player;

enum class MenuKind { Inventory, Crafting, Furnace, Creative, Chest };
enum class SlotRole { Storage, Craft, CraftResult, Source, FurnaceInput, FurnaceFuel, FurnaceOutput, Armor };

/// Estado de un horno (su "bloque con datos").
struct FurnaceState {
  ItemStack input, fuel, output;
  int burnTime = 0, burnTotal = 0, cookTime = 0;
  static constexpr int kCookTicks = 200;
  bool burning() const { return burnTime > 0; }
  /// Un tick; devuelve true si ha cambiado si está encendido.
  bool tick();
};

/// Contenido de un cofre (27 casillas).
struct ChestState {
  std::array<ItemStack, 27> items{};
  bool empty() const {
    for (const ItemStack& s : items)
      if (!s.empty()) return false;
    return true;
  }
};

struct MenuSlot {
  int x = 0, y = 0;  // posición del ítem (esquina superior izquierda) dentro de la textura de la ventana
  SlotRole role = SlotRole::Storage;
  ItemStack* stack = nullptr;
  int inventoryIndex = -1;  // índice en el inventario del jugador, si lo es
  int armorIndex = -1;      // casilla de armadura (0 botas .. 3 casco), si lo es
};

/// Ventana de inventario con sus casillas y las reglas de clic del juego.
class Menu {
 public:
  /// `chest`: las 27 casillas de un cofre (o del cofre de ender del jugador).
  Menu(MenuKind kind, Player& player, FurnaceState* furnace = nullptr, ItemStack* chest = nullptr);
  Menu(const Menu&) = delete;  // las casillas apuntan a miembros propios
  Menu& operator=(const Menu&) = delete;

  MenuKind kind() const { return kind_; }
  const std::string& texture() const { return texture_; }
  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<MenuSlot>& slots() const { return slots_; }
  FurnaceState* furnace() const { return furnace_; }

  /// Clic en una casilla: botón 0 = izquierdo, 1 = derecho; `shift` = mover rápido.
  void click(int slot, int button, bool shift);
  /// Clic fuera de la ventana: suelta lo que se lleva en el cursor (todo o uno).
  void clickOutside(int button, std::vector<ItemStack>& dropped);
  /// Al cerrar: lo que hay en la rejilla de crafteo y en el cursor vuelve al inventario (o se suelta).
  void close(std::vector<ItemStack>& dropped);

  /// Modo creativo: desplazar la lista de ítems (en filas).
  void scroll(int rows);
  int scrollRow() const { return scroll_; }
  int maxScroll() const;

 private:
  void build();
  void addPlayerSlots(int invY, int hotbarY);
  void updateResult();
  void refreshCreative();
  /// Mueve una pila a las casillas del rango (juntando primero). Devuelve lo que sobra.
  ItemStack moveInto(ItemStack s, int from, int to, bool reverse);
  void takeResult(bool shift);

  MenuKind kind_;
  Player& player_;
  FurnaceState* furnace_;
  ItemStack* chest_ = nullptr;
  std::string texture_;
  int width_ = 176, height_ = 166;
  int gridSize_ = 0;
  std::array<ItemStack, 9> grid_{};
  ItemStack result_;
  std::vector<MenuSlot> slots_;
  std::array<ItemStack, 54> creativeView_{};
  int scroll_ = 0;
  int playerStart_ = 0;  // primera casilla del inventario del jugador en slots_
};

}  // namespace mcw
