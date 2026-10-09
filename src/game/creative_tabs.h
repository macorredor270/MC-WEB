#pragma once
// Las pestañas del inventario del modo creativo: qué objetos hay en cada una, en qué orden, y la búsqueda por texto.
#include <string>
#include <string_view>
#include <vector>

#include "game/item_stack.h"

namespace mcw {

/// Pestañas, en el orden en que se dibujan: las seis primeras arriba (la búsqueda a la derecha del todo) y las seis
/// siguientes abajo (a la derecha del todo, el inventario de supervivencia).
enum class CreativeTab : u8 {
  Blocks, Decoration, Redstone, Transport, Misc, Search,
  Food, Tools, Combat, Brewing, Materials, Inventory,
};
inline constexpr int kCreativeTabCount = 12;
/// Columnas de la rejilla de objetos.
inline constexpr int kCreativeColumns = 9;

inline constexpr bool creativeTabOnTop(CreativeTab t) { return static_cast<int>(t) < 6; }
inline constexpr int creativeTabColumn(CreativeTab t) { return static_cast<int>(t) % 6; }
/// Las dos de la derecha (búsqueda e inventario) van pegadas al borde derecho de la ventana.
inline constexpr bool creativeTabRightmost(CreativeTab t) { return creativeTabColumn(t) == 5; }

/// Nombre visible (español) de la pestaña.
std::string_view creativeTabName(CreativeTab t);
/// Objeto que se dibuja en la pestaña.
ItemStack creativeTabIcon(CreativeTab t);

/// Objetos de la pestaña, ya en su orden. Entre los grupos (herramientas, colores...) hay casillas vacías para que
/// cada grupo empiece en una fila nueva. La búsqueda trae todos los objetos de las demás pestañas (sin huecos, en el
/// orden de pestañas) y el inventario de supervivencia ninguno (allí se ve el inventario del jugador).
const std::vector<ItemStack>& creativeTabItems(CreativeTab t);

/// Los objetos cuyo nombre (en español o en inglés) contiene todas las palabras de `query`, sin tener en cuenta
/// mayúsculas ni tildes. Los libros encantados se encuentran también por el nombre de lo que encantan. Con
/// `query` vacío salen todos.
std::vector<ItemStack> creativeSearch(std::string_view query);

/// Minúsculas y sin tildes (la ñ cuenta como n): así se comparan los nombres con lo que se escribe.
std::string normalizeSearchText(std::string_view text);

/// Objetos del creativo que ninguna lista reclama y que se han añadido a "Varios" al final. Debe estar vacío:
/// cuando el juego gane objetos nuevos, hay que ponerlos en su pestaña.
const std::vector<ItemStack>& creativeUnclassified();

}  // namespace mcw
