#pragma once
// Efectos de estado (como en 1.8): los de las pociones, la manzana de oro, el faro... y las pociones mismas.
#include <string>
#include <vector>

#include "core/types.h"

namespace mcw {

struct ActiveEffect {
  int id = 0;   // 1 velocidad ... 23 saturación (los ids de 1.8)
  int amp = 0;  // 0 = nivel I
  int ticks = 0;
  bool operator==(const ActiveEffect&) const = default;
};

namespace fx {
enum : int {
  Speed = 1, Slowness, Haste, MiningFatigue, Strength, InstantHealth, InstantDamage, JumpBoost, Nausea, Regeneration, Resistance,
  FireResistance, WaterBreathing, Invisibility, Blindness, NightVision, Hunger, Weakness, Poison, Wither, HealthBoost, Absorption,
  Saturation, Count
};
/// "Velocidad", "Regeneración"...
const char* name(int id);
/// ¿Es perjudicial (lentitud, veneno...)?
bool harmful(int id);
/// ¿Es instantáneo (curación, daño)?
bool instant(int id);
/// Color de las partículas (RGB).
u32 color(int id);
/// Posición del icono en la hoja de efectos de `gui/container/inventory.png` (8 por fila, 18x18, desde la fila y=198).
int icon(int id);
/// "2:30" para una duración en ticks, "**:**" si es muy larga.
std::string duration(int ticks);
}  // namespace fx

/// Lista de efectos activos de un jugador o criatura.
struct Effects {
  std::vector<ActiveEffect> list;
  /// Nivel (0 = I) del efecto, o -1 si no lo tiene.
  int amp(int id) const {
    for (const ActiveEffect& e : list)
      if (e.id == id) return e.amp;
    return -1;
  }
  bool has(int id) const { return amp(id) >= 0; }
  /// Añade un efecto: si ya lo había, se queda el más fuerte (o el más largo del mismo nivel).
  void add(int id, int amp, int ticks) {
    for (ActiveEffect& e : list)
      if (e.id == id) {
        if (amp > e.amp || (amp == e.amp && ticks > e.ticks)) {
          e.amp = amp;
          e.ticks = ticks;
        }
        return;
      }
    list.push_back({id, amp, ticks});
  }
  void remove(int id) {
    for (std::size_t i = 0; i < list.size(); i++)
      if (list[i].id == id) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
  }
  void clear() { list.clear(); }
};

// --- Pociones ------------------------------------------------------------------------------------------
// El daño del objeto "poción" de 1.8: los 4 bits de abajo = el efecto, 32 = nivel II, 64 = duración ampliada, 8192 = se bebe, 16384 = se lanza.

namespace potion {
constexpr int kDrink = 8192, kSplash = 16384, kLevel2 = 32, kExtended = 64, kAwkward = 16;
/// Los efectos que da una poción (duración ya calculada; las lanzadas valen 3/4 del tiempo).
std::vector<ActiveEffect> effectsOf(int meta);
/// El efecto base (1 regeneración, 2 velocidad, 3 resistencia al fuego, 4 veneno, 5 curación, 6 visión nocturna, 8 debilidad, 9 fuerza,
/// 10 lentitud, 11 salto, 12 daño, 13 respiración, 14 invisibilidad); 0 si no tiene.
inline int baseOf(int meta) { return meta & 15; }
/// Nombre en español ("Poción de regeneración II", "Poción arrojadiza de veneno"...).
std::string name(int meta);
/// Color del líquido (RGB) para la botella.
u32 color(int meta);
/// Lo que sale de echar `ingredient` a una botella de poción `meta` (-1 si no se puede).
int brew(int meta, int ingredient, int ingredientMeta = 0);
/// ¿Sirve de ingrediente en el atril?
bool isIngredient(int itemId, int itemMeta = 0);
/// ¿Es agua (botella de agua sin nada más)?
inline bool isWater(int meta) { return meta == 0; }
}  // namespace potion

/// Nombre en español de un objeto (el de las pociones sale de su efecto).
std::string itemName(int id, int meta);

}  // namespace mcw
