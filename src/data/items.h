#pragma once
#include <span>
#include <string>
#include <string_view>

#include "core/types.h"

namespace mcw {

/// Ítem de 1.8 (los bloques también son ítems con id < 256).
struct ItemInfo {
  int id = -1;
  std::string_view name;
  std::string_view displayName;
  int stackSize = 64;
  int maxDurability = 0;  // > 0 en herramientas
  bool exists = false;
};

const ItemInfo& itemInfo(int id);
int itemIdByName(std::string_view name);  // -1 si no existe
/// Nombre visible de una variante (p. ej. carbón / carbón vegetal).
std::string_view itemDisplayName(int id, int meta);
inline bool isBlockItem(int id) { return id > 0 && id < 256; }

/// Ids de ítems por nombre de registro: ItemId::stick, ItemId::wooden_pickaxe...
namespace ItemId {
#include "data/generated/item_ids.inc"
}

// --- Herramientas y cosecha ---------------------------------------------------

/// Multiplicador de velocidad de una herramienta sobre un bloque (1 = a mano).
float toolSpeed(int blockId, int toolId);
/// ¿Se obtiene algo al romper el bloque con esa herramienta (o a mano, toolId = 0)?
bool canHarvest(int blockId, int toolId);

struct DropEntry {
  int block;
  int id;
  int meta;  // -1 = el mismo tipo que el bloque
  float minCount, maxCount;
};
std::span<const DropEntry> dropsOf(int blockId);

// --- Recetas ------------------------------------------------------------------

struct Ingredient {
  i16 id = 0;
  i16 meta = 0;  // -1 = cualquier variante
};
struct ShapedRecipe {
  int id, meta, count, width, height;
  Ingredient cells[9];
};
struct ShapelessRecipe {
  int id, meta, count, n;
  Ingredient cells[9];
};
std::span<const ShapedRecipe> shapedRecipes();
std::span<const ShapelessRecipe> shapelessRecipes();

// --- Colisión -----------------------------------------------------------------

struct Box {
  float x0, y0, z0, x1, y1, z1;
};
/// Cajas de colisión de un estado de bloque en coordenadas locales (0..1).
std::span<const Box> collisionBoxes(int blockId, int meta);

}  // namespace mcw
