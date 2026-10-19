#pragma once
// Fuegos artificiales (1.8): las recetas especiales (estrella de fuegos, cohete) y los colores de los tintes. Comportamiento según minecraft.wiki.
#include <optional>
#include <span>

#include "game/item_stack.h"

namespace mcw::firework {

/// RGB del color de fuegos artificiales de un tinte (meta del objeto tinte: 0 negro ... 15 blanco).
i32 dyeColor(int dyeMeta);
/// Si la rejilla es una receta de fuegos artificiales, su resultado: estrella (pólvora, tintes y, si se quiere, una forma, estela o centelleo),
/// desvanecer una estrella (estrella y tintes) o cohete (papel, de 1 a 3 pólvoras y hasta 7 estrellas).
std::optional<ItemStack> craft(std::span<const ItemStack> grid);

}  // namespace mcw::firework
