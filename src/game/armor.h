#pragma once
#include <optional>

#include "core/types.h"

namespace mcw {

/// Pieza de armadura (el índice es el de 1.8: 0 botas, 1 pantalones, 2 pechera, 3 casco).
enum class ArmorPiece : u8 { Boots, Leggings, Chestplate, Helmet };
enum class ArmorMaterial : u8 { Leather, Chain, Iron, Gold, Diamond };

struct ArmorInfo {
  ArmorPiece piece;
  ArmorMaterial material;
  int defense;         // puntos de armadura (cada dos son un icono entero)
  int enchantability;  // para la mesa de encantamientos
};

/// Datos de un objeto de armadura (nada si el objeto no lo es).
std::optional<ArmorInfo> armorInfo(int itemId);
inline bool isArmor(int itemId) { return armorInfo(itemId).has_value(); }
/// Nombre del material en las texturas del modelo (`models/armor/<nombre>_layer_1.png`).
const char* armorMaterialTexture(ArmorMaterial m);
/// Color de cuero por defecto (RGB).
constexpr u32 kLeatherColor = 0xA06540;

}  // namespace mcw
