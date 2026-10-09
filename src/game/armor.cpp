#include "game/armor.h"

#include "data/items.h"

namespace mcw {

std::optional<ArmorInfo> armorInfo(int id) {
  if (id < ItemId::leather_helmet || id > ItemId::golden_boots) return std::nullopt;
  // Los ids van de cuatro en cuatro: casco, pechera, pantalones, botas; y por materiales:
  // cuero (298), malla (302), hierro (306), diamante (310) y oro (314).
  static const ArmorMaterial materials[5] = {ArmorMaterial::Leather, ArmorMaterial::Chain, ArmorMaterial::Iron, ArmorMaterial::Diamond,
                                             ArmorMaterial::Gold};
  //                          casco, pechera, pantalones, botas
  static const int defense[5][4] = {{1, 3, 2, 1}, {2, 5, 4, 1}, {2, 6, 5, 2}, {3, 8, 6, 3}, {2, 5, 3, 1}};
  static const int enchantability[5] = {15, 12, 9, 10, 25};
  const int index = id - ItemId::leather_helmet;
  const int m = index / 4, part = index % 4;
  static const ArmorPiece pieces[4] = {ArmorPiece::Helmet, ArmorPiece::Chestplate, ArmorPiece::Leggings, ArmorPiece::Boots};
  return ArmorInfo{pieces[part], materials[m], defense[m][part], enchantability[m]};
}

const char* armorMaterialTexture(ArmorMaterial m) {
  switch (m) {
    case ArmorMaterial::Leather: return "leather";
    case ArmorMaterial::Chain: return "chainmail";
    case ArmorMaterial::Iron: return "iron";
    case ArmorMaterial::Gold: return "gold";
    case ArmorMaterial::Diamond: return "diamond";
  }
  return "leather";
}

}  // namespace mcw
