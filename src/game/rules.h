#pragma once
#include <optional>
#include <vector>

#include "core/random.h"
#include "game/item_stack.h"
#include "game/physics.h"

namespace mcw {

class World;

// Reglas del juego descritas en minecraft.wiki (Breaking, Food, Smelting, Fuel...).

/// Cuánto avanza la rotura por tick (se rompe al llegar a 1). 0 = irrompible.
float digProgressPerTick(BlockState s, const ItemStack& tool, bool onGround, bool headInWater);

/// Lo que suelta un bloque roto en supervivencia con esa herramienta.
std::vector<ItemStack> blockDrops(BlockState s, const ItemStack& tool, Random& rng);

/// ¿Se puede sustituir al colocar otro bloque (aire, hierba alta, agua...)?
bool isReplaceable(BlockState s);

/// ¿Este bloque puede seguir en su sitio (plantas sobre tierra, antorchas apoyadas...)?
bool canStay(const World& world, int x, int y, int z, BlockState s);

/// Estado que se coloca con el ítem de la mano al usarlo sobre `hit`, o nullopt si no se puede.
/// `pos` recibe dónde se coloca.
std::optional<BlockState> placementFor(const World& world, const ItemStack& held, const RayHit& hit, float yaw,
                                       glm::ivec3& pos);

/// ¿El ítem es un bloque que sabemos dibujar y colocar?
bool isPlaceableItem(const ItemStack& s);

struct FoodValue {
  int food;
  float saturation;
};
std::optional<FoodValue> foodValue(const ItemStack& s);

/// Horno: resultado de fundir un ítem y ticks que dura un combustible.
std::optional<ItemStack> smeltingResult(const ItemStack& s);
int fuelTicks(const ItemStack& s);

/// Ítems para la pestaña del modo creativo (bloques colocables y herramientas/materiales).
const std::vector<ItemStack>& creativeItems();

}  // namespace mcw
