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

/// Lo que se coloca al usar un ítem: el bloque y, si ocupa dos, el segundo (mitad de arriba de
/// puertas y plantas dobles, cabecera de la cama).
struct Placement {
  glm::ivec3 pos{0};
  BlockState state = 0;
  bool hasSecond = false;
  glm::ivec3 secondPos{0};
  BlockState secondState = 0;
};

/// Qué se coloca con el ítem de la mano al usarlo sobre `hit` (orientado según la cara golpeada
/// y hacia dónde mira el jugador), o nullopt si no se puede.
std::optional<Placement> placementFor(const World& world, const ItemStack& held, const RayHit& hit, float yaw, float pitch);

/// Bloque que coloca un ítem (puertas, semillas, redstone...) o -1 si no coloca ninguno.
int blockForItem(const ItemStack& s);

/// ¿El ítem coloca un bloque que sabemos dibujar?
bool isPlaceableItem(const ItemStack& s);

/// Ítem que representa un bloque (lo que se coge con el botón central): la puerta da el ítem de
/// puerta, el trigo sus semillas... Vacío si no hay.
ItemStack pickItem(BlockState s);

/// Bloque desde el que se sostiene uno colgado (antorchas, botones, escaleras de mano...), relativo
/// a su posición. (0,0,0) si no cuelga de nada.
glm::ivec3 supportOffset(BlockState s);

/// Dirección horizontal de 1.8 (0 sur, 1 oeste, 2 norte, 3 este) hacia la que mira el jugador.
int horizontalFacing(float yaw);

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
