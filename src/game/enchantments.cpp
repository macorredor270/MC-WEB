#include "game/enchantments.h"

#include <algorithm>

#include "data/items.h"
#include "game/armor.h"

namespace mcw {

std::vector<EnchantInfo> const& allEnchantments() {
  using T = EnchantTarget;
  static const std::vector<EnchantInfo> table = {
      //  id  clave                    nombre                        máx peso destino          base +/nivel rango
      {Ench::Protection, "protection", "Protección", 4, 10, T::Armor, 1, 11, 20},
      {Ench::FireProtection, "fire_protection", "Protección contra el fuego", 4, 5, T::Armor, 10, 8, 12},
      {Ench::FeatherFalling, "feather_falling", "Caída de pluma", 4, 5, T::ArmorFeet, 5, 6, 6},
      {Ench::BlastProtection, "blast_protection", "Protección contra explosiones", 4, 2, T::Armor, 5, 8, 12},
      {Ench::ProjectileProtection, "projectile_protection", "Protección contra proyectiles", 4, 5, T::Armor, 3, 6, 15},
      {Ench::Respiration, "respiration", "Respiración", 3, 2, T::ArmorHead, 10, 10, 30},
      {Ench::AquaAffinity, "aqua_affinity", "Afinidad acuática", 1, 2, T::ArmorHead, 1, 0, 40},
      {Ench::Thorns, "thorns", "Espinas", 3, 1, T::ArmorTorso, 10, 20, 50},
      {Ench::DepthStrider, "depth_strider", "Agilidad acuática", 3, 2, T::ArmorFeet, 10, 10, 15},
      {Ench::Sharpness, "sharpness", "Filo", 5, 10, T::Weapon, 1, 11, 20},
      {Ench::Smite, "smite", "Pesadez", 5, 5, T::Weapon, 5, 8, 20},
      {Ench::BaneOfArthropods, "bane_of_arthropods", "Perdición de los artrópodos", 5, 5, T::Weapon, 5, 8, 20},
      {Ench::Knockback, "knockback", "Retroceso", 2, 5, T::Weapon, 5, 20, 50},
      {Ench::FireAspect, "fire_aspect", "Aspecto ígneo", 2, 2, T::Weapon, 10, 20, 50},
      {Ench::Looting, "looting", "Botín", 3, 2, T::Weapon, 15, 9, 50},
      {Ench::Efficiency, "efficiency", "Eficiencia", 5, 10, T::Digger, 1, 10, 50},
      {Ench::SilkTouch, "silk_touch", "Toque de seda", 1, 1, T::Digger, 15, 0, 50},
      {Ench::Unbreaking, "unbreaking", "Irrompibilidad", 3, 5, T::Breakable, 5, 8, 50},
      {Ench::Fortune, "fortune", "Fortuna", 3, 2, T::Digger, 15, 9, 50},
      {Ench::Power, "power", "Poder", 5, 10, T::Bow, 1, 10, 15},
      {Ench::Punch, "punch", "Golpe", 2, 2, T::Bow, 12, 20, 25},
      {Ench::Flame, "flame", "Llama", 1, 2, T::Bow, 20, 0, 30},
      {Ench::Infinity, "infinity", "Infinidad", 1, 1, T::Bow, 20, 0, 30},
      {Ench::LuckOfTheSea, "luck_of_the_sea", "Suerte marina", 3, 2, T::FishingRod, 15, 9, 50},
      {Ench::Lure, "lure", "Atracción", 3, 2, T::FishingRod, 15, 9, 50},
  };
  return table;
}

const EnchantInfo* enchantInfo(int id) {
  for (const EnchantInfo& e : allEnchantments())
    if (e.id == id) return &e;
  return nullptr;
}

const EnchantInfo* enchantByName(std::string_view s) {
  if (s.rfind("minecraft:", 0) == 0) s.remove_prefix(10);
  for (const EnchantInfo& e : allEnchantments())
    if (s == e.key) return &e;
  int id = 0;
  if (!s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    for (char c : s) id = id * 10 + (c - '0');
    return enchantInfo(id);
  }
  return nullptr;
}

std::string romanNumeral(int n) {
  static const char* units[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
  return n >= 0 && n <= 10 ? units[n] : std::to_string(n);
}

std::string enchantDisplayName(int id, int level) {
  const EnchantInfo* e = enchantInfo(id);
  if (!e) return "Encantamiento " + std::to_string(id) + " " + romanNumeral(level);
  return e->maxLevel == 1 ? std::string(e->nameEs) : std::string(e->nameEs) + " " + romanNumeral(level);
}

namespace {

bool isSword(int id) {
  return id == ItemId::wooden_sword || id == ItemId::stone_sword || id == ItemId::iron_sword || id == ItemId::golden_sword ||
         id == ItemId::diamond_sword;
}
bool isAxe(int id) {
  return id == ItemId::wooden_axe || id == ItemId::stone_axe || id == ItemId::iron_axe || id == ItemId::golden_axe || id == ItemId::diamond_axe;
}
bool isDiggerTool(int id) {  // pico, pala y hacha (ItemTool de 1.8; las azadas no)
  return isAxe(id) || id == ItemId::wooden_pickaxe || id == ItemId::stone_pickaxe || id == ItemId::iron_pickaxe ||
         id == ItemId::golden_pickaxe || id == ItemId::diamond_pickaxe || id == ItemId::wooden_shovel || id == ItemId::stone_shovel ||
         id == ItemId::iron_shovel || id == ItemId::golden_shovel || id == ItemId::diamond_shovel;
}

}  // namespace

bool canEnchant(const EnchantInfo& e, int itemId, bool table) {
  if (itemId == ItemId::book || itemId == ItemId::enchanted_book) return true;
  const auto armor = armorInfo(itemId);
  const bool breakable = itemInfo(itemId).maxDurability > 0;
  switch (e.target) {
    case EnchantTarget::Armor: return armor.has_value();
    case EnchantTarget::ArmorFeet: return armor && armor->piece == ArmorPiece::Boots;
    case EnchantTarget::ArmorHead: return armor && armor->piece == ArmorPiece::Helmet;
    case EnchantTarget::ArmorTorso: return armor && (armor->piece == ArmorPiece::Chestplate || !table);  // Espinas: cualquier armadura con yunque
    case EnchantTarget::Weapon: return isSword(itemId) || (!table && e.id == Ench::Sharpness && isAxe(itemId)) ||
                                       (!table && (e.id == Ench::Smite || e.id == Ench::BaneOfArthropods) && isAxe(itemId));
    case EnchantTarget::Digger: return isDiggerTool(itemId) || (!table && itemId == ItemId::shears && e.id != Ench::Fortune);
    case EnchantTarget::Bow: return itemId == ItemId::bow;
    case EnchantTarget::FishingRod: return itemId == ItemId::fishing_rod;
    case EnchantTarget::Breakable: return breakable;
  }
  return false;
}

bool enchantsCompatible(int a, int b) {
  if (a == b) return false;
  auto protection = [](int id) { return id == Ench::Protection || id == Ench::FireProtection || id == Ench::BlastProtection || id == Ench::ProjectileProtection; };
  auto damage = [](int id) { return id == Ench::Sharpness || id == Ench::Smite || id == Ench::BaneOfArthropods; };
  if (protection(a) && protection(b)) return false;  // (Caída de pluma sí va con todas)
  if (damage(a) && damage(b)) return false;
  if ((a == Ench::SilkTouch && b == Ench::Fortune) || (a == Ench::Fortune && b == Ench::SilkTouch)) return false;
  return true;
}

int itemEnchantability(int id) {
  if (id == ItemId::book) return 1;
  if (id == ItemId::bow || id == ItemId::fishing_rod) return 1;
  if (const auto a = armorInfo(id)) return a->enchantability;
  // Herramientas y espadas: según el material (madera 15, piedra 5, hierro 14, diamante 10, oro 22)
  struct Row {
    int ids[5];
    int value;
  };
  static const Row rows[] = {
      {{ItemId::wooden_sword, ItemId::wooden_pickaxe, ItemId::wooden_axe, ItemId::wooden_shovel, ItemId::wooden_hoe}, 15},
      {{ItemId::stone_sword, ItemId::stone_pickaxe, ItemId::stone_axe, ItemId::stone_shovel, ItemId::stone_hoe}, 5},
      {{ItemId::iron_sword, ItemId::iron_pickaxe, ItemId::iron_axe, ItemId::iron_shovel, ItemId::iron_hoe}, 14},
      {{ItemId::diamond_sword, ItemId::diamond_pickaxe, ItemId::diamond_axe, ItemId::diamond_shovel, ItemId::diamond_hoe}, 10},
      {{ItemId::golden_sword, ItemId::golden_pickaxe, ItemId::golden_axe, ItemId::golden_shovel, ItemId::golden_hoe}, 22},
  };
  for (const Row& r : rows)
    for (int x : r.ids)
      if (x == id) return r.value;
  return 0;
}

}  // namespace mcw
