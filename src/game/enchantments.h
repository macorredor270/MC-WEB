#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Los 25 encantamientos de 1.8 (con sus ids del juego).
namespace Ench {
inline constexpr int Protection = 0, FireProtection = 1, FeatherFalling = 2, BlastProtection = 3, ProjectileProtection = 4,
                     Respiration = 5, AquaAffinity = 6, Thorns = 7, DepthStrider = 8, Sharpness = 16, Smite = 17,
                     BaneOfArthropods = 18, Knockback = 19, FireAspect = 20, Looting = 21, Efficiency = 32, SilkTouch = 33,
                     Unbreaking = 34, Fortune = 35, Power = 48, Punch = 49, Flame = 50, Infinity = 51, LuckOfTheSea = 61, Lure = 62;
}

/// A qué objetos se puede encantar (EnumEnchantmentType de 1.8).
enum class EnchantTarget : u8 { Armor, ArmorFeet, ArmorHead, ArmorTorso, Weapon, Digger, Bow, FishingRod, Breakable };

struct EnchantInfo {
  int id;
  const char* key;      // "protection", "fire_protection"... (nombre en inglés, para comandos)
  const char* nameEs;   // nombre en español (traducción propia)
  int maxLevel;
  int weight;           // probabilidad relativa en la mesa (10 común, 5 poco, 2 rara, 1 muy rara)
  EnchantTarget target;
  int minBase, minPerLevel, range;  // energía mínima: minBase + (nivel - 1) * minPerLevel; la máxima, mínima + range
  int minEnchantability(int level) const { return minBase + (level - 1) * minPerLevel; }
  int maxEnchantability(int level) const { return minEnchantability(level) + range; }
};

/// Todos los encantamientos, por id creciente.
std::vector<EnchantInfo> const& allEnchantments();
const EnchantInfo* enchantInfo(int id);
/// Por su nombre en inglés ("sharpness") o su número; nullptr si no existe.
const EnchantInfo* enchantByName(std::string_view nameOrId);
/// "Filo III": nombre en español con el nivel en números romanos (sin el nivel si es el único posible).
std::string enchantDisplayName(int id, int level);
std::string romanNumeral(int n);

/// ¿Puede ese objeto llevar el encantamiento? Con `table` solo vale lo que sale de la mesa; sin él (yunque,
/// comandos) también las hachas con Filo, la armadura con Espinas, las tijeras, etc. Los libros valen siempre.
bool canEnchant(const EnchantInfo& e, int itemId, bool table = false);
/// ¿Pueden ir juntos en el mismo objeto? (las protecciones entre sí salvo Caída de pluma, las de daño entre
/// sí, Toque de seda y Fortuna...).
bool enchantsCompatible(int a, int b);
/// Encantabilidad del objeto (material): 0 si no se puede encantar en la mesa.
int itemEnchantability(int itemId);

}  // namespace mcw
