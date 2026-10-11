#pragma once
#include <array>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

#include "game/creative_tabs.h"
#include "game/enchanting.h"
#include "game/item_stack.h"

namespace mcw {

class Player;

enum class MenuKind { Inventory, Crafting, Furnace, Creative, Chest, Enchant, Anvil, Hopper, Dispenser, Dropper, Brewing };
enum class SlotRole { Storage, Craft, CraftResult, Source, FurnaceInput, FurnaceFuel, FurnaceOutput, Armor, EnchantItem, EnchantLapis, Trash, AnvilLeft, AnvilRight, AnvilOutput, BrewBottle, BrewIngredient };

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
  int brewTime = 0;  // atril de pociones: ticks que faltan (0 = parado)
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
  /// `chest`: las 27 casillas de un cofre (o del cofre de ender del jugador). `bookshelves`: las estanterías
  /// que rodean la mesa de encantamientos.
  Menu(MenuKind kind, Player& player, FurnaceState* furnace = nullptr, ItemStack* chest = nullptr, int bookshelves = 0);
  /// Un contenedor con las casillas que se le pasen (un cofre doble: 54, cada una de uno de los dos cofres; una tolva: 5; un
  /// dispensador o soltador: 9).
  Menu(MenuKind kind, Player& player, std::vector<ItemStack*> container);
  /// Cuántas casillas propias tiene el contenedor (0 si no es uno).
  int containerSize() const { return static_cast<int>(container_.size()); }
  /// El título de la ventana (si no se pone, el que toca por el tipo).
  const std::string& title() const { return title_; }
  void setTitle(std::string t) { title_ = std::move(t); }
  /// Atril de pociones: de dónde sale el tiempo que falta (para dibujar la burbuja y la flecha).
  void setBrewTime(const int* p) { brewTime_ = p; }
  int brewTime() const { return brewTime_ ? *brewTime_ : 0; }
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

  // --- Mesa de encantamientos ---
  int bookshelves() const { return bookshelves_; }
  /// Las tres opciones que se ven ahora (para el objeto de la casilla y la semilla del jugador).
  const std::array<EnchantOffer, 3>& offers() const { return offers_; }
  /// Casillas de la mesa: 0 = objeto, 1 = lapislázuli.
  ItemStack& enchantSlot(int i) { return enchantSlots_[static_cast<std::size_t>(i)]; }
  const ItemStack& enchantSlot(int i) const { return enchantSlots_[static_cast<std::size_t>(i)]; }
  /// ¿Puede el jugador pagar ahora la opción `button` (hay objeto, lapislázuli y niveles)? En creativo basta con
  /// que haya opción.
  bool canEnchant(int button) const;
  /// Pulsar la opción `button` (0 a 2): gasta niveles y lapislázuli y encanta el objeto. Devuelve si lo ha hecho.
  bool enchant(int button);
  /// Jugando en un servidor, las opciones las manda él: se muestran tal cual y no se recalculan. `seed` es la
  /// semilla con la que dibujar la escritura rúnica (la manda el servidor).
  void setRemoteOffers(const std::array<EnchantOffer, 3>& offers, i32 seed = 0) {
    remoteOffers_ = true;
    offers_ = offers;
    remoteSeed_ = seed;
  }
  /// Semilla de la escritura rúnica: la del jugador, o la que manda el servidor.
  i32 runeSeed() const;
  /// Vuelve a calcular las opciones (después de poner un objeto en la casilla por código).
  void refreshOffers() { updateEnchant(); }

  // --- Yunque ---
  /// Casillas del yunque: 0 = objeto, 1 = lo que se le añade (material, otro igual o un libro encantado).
  ItemStack& anvilSlot(int i) { return anvilSlots_[static_cast<std::size_t>(i)]; }
  const ItemStack& anvilSlot(int i) const { return anvilSlots_[static_cast<std::size_t>(i)]; }
  /// El resultado de ahora (vacío si no se puede). Jugando en un servidor lo manda él (`setRemoteAnvil`).
  ItemStack& anvilOutput() { return anvilOut_; }
  const ItemStack& anvilOutput() const { return anvilOut_; }
  /// Lo que hay escrito en el campo del nombre (al poner un objeto se rellena con su nombre).
  const std::string& anvilName() const { return anvilName_; }
  void setAnvilName(std::string name);
  /// Niveles que cuesta sacar el resultado (0 si no hay), y si es "demasiado caro".
  int anvilCost() const { return anvilCost_; }
  bool anvilTooExpensive() const { return anvilExpensive_; }
  /// ¿Puede el jugador sacar ya el resultado (hay y le alcanzan los niveles, o es creativo)?
  bool anvilCanTake() const;
  /// Cuántas veces se ha sacado un resultado desde la última vez que se preguntó (la partida desgasta el yunque).
  int takeAnvilUses() { return std::exchange(anvilUses_, 0); }
  /// En un servidor: el coste lo manda él y el resultado llega como una casilla más; no se recalcula aquí.
  void setRemoteAnvil(int cost) {
    remoteAnvil_ = true;
    anvilCost_ = cost;
  }

  // --- Modo creativo ---
  /// La lista se ve en una rejilla de 9 columnas y 5 filas; bajo ella, la barra rápida.
  static constexpr int kCreativeCols = kCreativeColumns, kCreativeRows = 5, kCreativeVisible = kCreativeCols * kCreativeRows;
  CreativeTab creativeTab() const { return tab_; }
  /// Cambia de pestaña: la lista vuelve arriba del todo y, en la búsqueda, el campo se vacía.
  void setCreativeTab(CreativeTab tab);
  /// Lo escrito en el campo de la pestaña "Buscar objetos" (se filtra al instante).
  const std::string& searchText() const { return search_; }
  void setSearchText(std::string text);
  /// Añade lo escrito (sin saltos de línea ni caracteres de control, hasta un máximo).
  void typeSearch(std::string_view text);
  /// Borra la última letra (entera, aunque ocupe varios bytes).
  void eraseSearchChar();
  /// Tecla numérica (casilla 0 a 8 de la barra) sobre una casilla: en la lista pone ahí la pila entera; en las casillas
  /// del inventario las intercambia con la de la barra rápida.
  void hotkey(int slot, int hotbarIndex);
  /// Desplaza la lista de objetos (en filas).
  void scroll(int rows);
  void setScrollRow(int row);
  int scrollRow() const { return scroll_; }
  int maxScroll() const;
  /// Cuántos objetos hay en la lista de ahora (ya filtrada si se está buscando).
  int listSize() const { return static_cast<int>(list().size()); }

 private:
  void build();
  void addPlayerSlots(int invY, int hotbarY);
  void updateResult();
  void refreshCreative();
  void buildCreative();
  const std::vector<ItemStack>& list() const;
  /// Mete una pila en las casillas `from` a `to` del inventario del jugador (juntando primero). Devuelve lo que sobra.
  ItemStack moveToInventory(ItemStack s, int from, int to);
  /// Mueve una pila a las casillas del rango (juntando primero). Devuelve lo que sobra.
  ItemStack moveInto(ItemStack s, int from, int to, bool reverse);
  void takeResult(bool shift);
  void updateEnchant();
  void updateAnvil();
  void takeAnvilResult(bool shift);

  MenuKind kind_;
  Player& player_;
  FurnaceState* furnace_;
  std::string title_;
  const int* brewTime_ = nullptr;
  std::vector<ItemStack*> container_;  // las casillas del contenedor (cofre, tolva, dispensador...)
  std::string texture_;
  int width_ = 176, height_ = 166;
  int gridSize_ = 0;
  std::array<ItemStack, 9> grid_{};
  ItemStack result_;
  std::vector<MenuSlot> slots_;
  std::array<ItemStack, kCreativeVisible> creativeView_{};
  CreativeTab tab_ = CreativeTab::Blocks;
  std::string search_;
  std::vector<ItemStack> searchResult_;
  ItemStack trash_;      // la papelera siempre está vacía
  int armorBase_ = -1;   // primera casilla de armadura en slots_ (casco primero), -1 si no hay
  std::array<ItemStack, 2> enchantSlots_{};  // objeto y lapislázuli
  std::array<ItemStack, 2> anvilSlots_{};    // yunque: objeto y lo que se le añade
  ItemStack anvilOut_;
  std::string anvilName_;
  int anvilCost_ = 0, anvilMaterial_ = 0, anvilUses_ = 0;
  bool anvilExpensive_ = false, remoteAnvil_ = false;
  std::string anvilNameFor_;  // el nombre del objeto de la izquierda con el que se rellenó el campo (para saber si cambió)
  std::array<EnchantOffer, 3> offers_{};
  int bookshelves_ = 0;
  bool remoteOffers_ = false;
  i32 remoteSeed_ = 0;
  int scroll_ = 0;
  int playerStart_ = 0;  // primera casilla del inventario del jugador en slots_
};

}  // namespace mcw
