#pragma once
#include <array>
#include <utility>
#include <vector>

#include "core/types.h"
#include "game/item_stack.h"

namespace mcw {

class World;

/// Una de las tres opciones de la mesa de encantamientos.
struct EnchantOffer {
  int cost = 0;          // niveles que pide (0 = no hay opción)
  int clueEnchant = -1;  // uno de los encantamientos que traerá (la pista), -1 si ninguno
  int clueLevel = 0;
};

/// Estanterías que rodean una mesa (hasta 15): las del anillo a 2 bloques de distancia, a su altura o una
/// más arriba, siempre que el hueco entre la mesa y ellas esté libre (aire a la altura de la mesa y encima).
int countBookshelves(const World& world, int tx, int ty, int tz);

/// Las tres opciones para ese objeto con tantas estanterías y la semilla del jugador (como en 1.8: el coste de
/// cada una sale de las estanterías y de la semilla, y la pista de la lista de encantamientos de esa opción).
std::array<EnchantOffer, 3> enchantOffers(const ItemStack& item, int bookshelves, i32 xpSeed);

/// Los encantamientos (id, nivel) que da la opción `slot` de coste `cost`: los mismos de la pista. En un
/// libro se quita uno al azar si salen varios.
std::vector<std::pair<int, int>> enchantList(const ItemStack& item, int slot, int cost, i32 xpSeed);

/// Aplica la lista a un objeto: un libro se vuelve libro encantado (con StoredEnchantments) y lo demás
/// recibe los encantamientos.
ItemStack applyEnchants(ItemStack item, const std::vector<std::pair<int, int>>& list);

}  // namespace mcw
