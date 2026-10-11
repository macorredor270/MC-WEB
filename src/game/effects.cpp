#include "game/effects.h"

#include <algorithm>

#include "data/items.h"

namespace mcw {
namespace fx {

namespace {
struct Info {
  const char* name;
  bool harmful, instant;
  u32 color;
  int icon;
};
// Los iconos siguen el orden de la hoja de efectos de 1.8
constexpr Info kInfo[] = {
    {"", false, false, 0, 0},
    {"Velocidad", false, false, 0x7CAFC6, 0},
    {"Lentitud", true, false, 0x5A6C81, 1},
    {"Prisa", false, false, 0xD9C043, 2},
    {"Fatiga de minería", true, false, 0x4A4217, 3},
    {"Fuerza", false, false, 0x932423, 4},
    {"Curación instantánea", false, true, 0xF82423, 5},
    {"Daño instantáneo", true, true, 0x430A09, 6},
    {"Salto mejorado", false, false, 0x22FF4C, 7},
    {"Náuseas", true, false, 0x551D4A, 8},
    {"Regeneración", false, false, 0xCD5CAB, 9},
    {"Resistencia", false, false, 0x99453A, 10},
    {"Resistencia al fuego", false, false, 0xE49A3A, 11},
    {"Respiración acuática", false, false, 0x2E5299, 12},
    {"Invisibilidad", false, false, 0x7F8392, 13},
    {"Ceguera", true, false, 0x1F1F23, 14},
    {"Visión nocturna", false, false, 0x1F1FA1, 15},
    {"Hambre", true, false, 0x587653, 16},
    {"Debilidad", true, false, 0x484D48, 17},
    {"Veneno", true, false, 0x4E9331, 18},
    {"Wither", true, false, 0x352A27, 19},
    {"Salud mejorada", false, false, 0xF87D23, 20},
    {"Absorción", false, false, 0x2552A5, 21},
    {"Saturación", false, true, 0xF82423, 22},
};
const Info& info(int id) { return kInfo[std::clamp(id, 0, 23)]; }
}  // namespace

const char* name(int id) { return info(id).name; }
bool harmful(int id) { return info(id).harmful; }
bool instant(int id) { return info(id).instant; }
u32 color(int id) { return info(id).color; }
int icon(int id) { return info(id).icon; }

std::string duration(int ticks) {
  if (ticks > 32767) return "**:**";
  const int s = ticks / 20;
  return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
}

}  // namespace fx

namespace potion {
namespace {

struct Base {
  int effect;
  int ticks, ticksExtended, ticksLevel2;  // 0 = no admite
  const char* name;
  bool instant;
};
// Duraciones de 1.8 (en ticks): 3:00, 8:00 y 1:30; la regeneración y el veneno, 0:45, 2:00 y 0:22; la debilidad y la lentitud, 1:30 y 4:00
const Base* baseInfo(int b) {
  static const Base kBases[] = {
      {1, 900, 2400, 450, "regeneración", false},
      {2, 3600, 9600, 1800, "velocidad", false},
      {12 - 9, 3600, 9600, 0, "resistencia al fuego", false},  // 3
      {19, 900, 2400, 440, "veneno", false},                   // 4
      {6, 1, 0, 1, "curación", true},                          // 5
      {16, 3600, 9600, 0, "visión nocturna", false},           // 6
      {0, 0, 0, 0, "", false},                                 // 7
      {18, 1800, 4800, 0, "debilidad", false},                 // 8
      {5, 3600, 9600, 1800, "fuerza", false},                  // 9
      {2 - 0, 1800, 4800, 0, "lentitud", false},               // 10 (efecto 2 = lentitud)
      {8, 3600, 9600, 1800, "salto", false},                   // 11
      {7, 1, 0, 1, "daño", true},                              // 12
      {13, 3600, 9600, 0, "respiración acuática", false},      // 13
      {14, 3600, 9600, 0, "invisibilidad", false},             // 14
  };
  return b >= 1 && b <= 14 && kBases[b - 1].effect != 0 ? &kBases[b - 1] : nullptr;
}
// (la tabla de arriba se indexa por tipo - 1: la posición 0 es el tipo 1)
int effectIdOf(int b) {
  switch (b) {
    case 1: return fx::Regeneration;
    case 2: return fx::Speed;
    case 3: return fx::FireResistance;
    case 4: return fx::Poison;
    case 5: return fx::InstantHealth;
    case 6: return fx::NightVision;
    case 8: return fx::Weakness;
    case 9: return fx::Strength;
    case 10: return fx::Slowness;
    case 11: return fx::JumpBoost;
    case 12: return fx::InstantDamage;
    case 13: return fx::WaterBreathing;
    case 14: return fx::Invisibility;
    default: return 0;
  }
}
}  // namespace

std::vector<ActiveEffect> effectsOf(int meta) {
  std::vector<ActiveEffect> out;
  const int b = baseOf(meta);
  const Base* info = baseInfo(b);
  const int effect = effectIdOf(b);
  if (!info || !effect) return out;
  int amp = 0, ticks = info->ticks;
  if (meta & kLevel2 && info->ticksLevel2 > 0) {
    amp = 1;
    ticks = info->ticksLevel2;
  } else if (meta & kExtended && info->ticksExtended > 0) {
    ticks = info->ticksExtended;
  }
  if ((meta & kSplash) && !info->instant) ticks = ticks * 3 / 4;
  out.push_back({effect, amp, ticks});
  return out;
}

std::string name(int meta) {
  const int b = baseOf(meta);
  const bool splash = (meta & kSplash) != 0;
  std::string prefix = splash ? "Poción arrojadiza" : "Poción";
  if (b == 0) {
    if (meta == 0) return "Botella de agua";
    if (meta & kAwkward) return prefix + " rara";
    if (meta & kLevel2) return prefix + " densa";
    if (meta & kExtended) return prefix + " mundana (ampliada)";
    return prefix + " mundana";
  }
  const Base* info = baseInfo(b);
  if (!info) return prefix;
  std::string s = prefix + " de " + info->name;
  if ((meta & kLevel2) && info->ticksLevel2 > 0) s += " II";
  if ((meta & kExtended) && info->ticksExtended > 0 && !(meta & kLevel2)) s += " (ampliada)";
  return s;
}

u32 color(int meta) {
  const auto e = effectsOf(meta | kDrink);
  if (e.empty()) return (meta & kAwkward) ? 0x385DC6 : meta == 0 ? 0x385DC6 : 0x3C5CA8;
  return fx::color(e[0].id);
}

bool isIngredient(int id, int itemMeta) {
  if (id == ItemId::fish) return itemMeta == 3;  // (el pez globo)
  switch (id) {
    case ItemId::nether_wart: case ItemId::sugar: case ItemId::ghast_tear: case ItemId::spider_eye: case ItemId::fermented_spider_eye:
    case ItemId::blaze_powder: case ItemId::magma_cream: case ItemId::speckled_melon: case ItemId::golden_carrot: case ItemId::rabbit_foot:
    case ItemId::redstone: case ItemId::glowstone_dust: case ItemId::gunpowder: return true;
    default: return false;
  }
}

int brew(int meta, int ing, int ingMeta) {
  if (!isIngredient(ing, ingMeta)) return -1;
  const int flags = meta & (kDrink | kSplash);
  const int low = meta & 0xFF;  // efecto + nivel II + ampliada + rara
  const int b = baseOf(meta);
  if (ing == ItemId::gunpowder) return (meta & kSplash) ? -1 : (meta & ~kDrink) | kSplash;
  // Agua (la botella sin nada)
  if (meta == 0) {
    switch (ing) {
      case ItemId::nether_wart: return kAwkward;
      case ItemId::fermented_spider_eye: return 8;  // debilidad
      case ItemId::glowstone_dust: return kLevel2;  // densa
      case ItemId::redstone: return kExtended;      // mundana ampliada
      case ItemId::sugar: case ItemId::ghast_tear: case ItemId::spider_eye: case ItemId::blaze_powder: case ItemId::magma_cream:
      case ItemId::speckled_melon: case ItemId::golden_carrot: case ItemId::rabbit_foot: return kDrink;  // mundana
      default: return -1;
    }
  }
  // Rara: se hace la poción según el ingrediente
  if (b == 0 && (meta & kAwkward)) {
    int effect = -1;
    switch (ing) {
      case ItemId::golden_carrot: effect = 6; break;
      case ItemId::magma_cream: effect = 3; break;
      case ItemId::ghast_tear: effect = 1; break;
      case ItemId::blaze_powder: effect = 9; break;
      case ItemId::fish: effect = 13; break;  // (pez globo, meta 3: se comprueba fuera)
      case ItemId::sugar: effect = 2; break;
      case ItemId::speckled_melon: effect = 5; break;
      case ItemId::spider_eye: effect = 4; break;
      case ItemId::rabbit_foot: effect = 11; break;
      default: return -1;
    }
    return flags | effect;
  }
  if (b == 0) return -1;
  // Con efecto: más tiempo, más nivel o corrupción
  const Base* info = baseInfo(b);
  if (!info) return -1;
  if (ing == ItemId::redstone) return (!(meta & kExtended) && !(meta & kLevel2) && info->ticksExtended > 0) ? (meta | kExtended) : -1;
  if (ing == ItemId::glowstone_dust) return (!(meta & kLevel2) && !(meta & kExtended) && info->ticksLevel2 > 0) ? (meta | kLevel2) : -1;
  if (ing == ItemId::fermented_spider_eye) {
    int to = -1;
    switch (b) {
      case 2: case 3: case 11: to = 10; break;  // velocidad, resistencia al fuego y salto -> lentitud
      case 6: to = 14; break;                    // visión nocturna -> invisibilidad
      case 5: case 4: to = 12; break;            // curación y veneno -> daño
      case 1: to = 4; break;                     // regeneración -> veneno
      case 9: to = 8; break;                     // fuerza -> debilidad
      default: return -1;
    }
    return flags | (low & ~15 & ~kExtended) | to | (low & kExtended && to != 12 ? 0 : 0);
  }
  return -1;
}

}  // namespace potion

std::string itemName(int id, int meta) {
  if (id == ItemId::potion) return potion::name(meta);
  return std::string(itemDisplayNameEs(id, meta));
}

}  // namespace mcw
