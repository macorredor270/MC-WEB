#pragma once
#include <optional>
#include <span>

#include "game/item_stack.h"

namespace mcw {

/// Busca la receta que encaja con una rejilla de `size` x `size` (2 en el inventario, 3 en la mesa).
/// Las recetas con forma pueden estar desplazadas y reflejadas en horizontal, como en el juego.
std::optional<ItemStack> matchRecipe(std::span<const ItemStack> grid, int size);

/// Gasta un ítem de cada casilla ocupada (tras recoger el resultado).
void consumeIngredients(std::span<ItemStack> grid);

}  // namespace mcw
