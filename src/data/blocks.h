#pragma once
#include <array>
#include <string_view>

#include "core/types.h"

namespace mcw {

/// Estado de bloque global de 1.8: id << 4 | metadata (el mismo formato que el protocolo).
using BlockState = u16;
constexpr BlockState makeState(int id, int meta = 0) { return static_cast<BlockState>((id << 4) | (meta & 15)); }
constexpr int stateId(BlockState s) { return s >> 4; }
constexpr int stateMeta(BlockState s) { return s & 15; }

enum class RenderLayer : u8 { Solid, Cutout, CutoutMipped, Translucent };
enum class TintType : u8 { None, Grass, Foliage, Birch, Spruce, Constant };

struct BlockInfo {
  int id = -1;
  std::string_view name;
  std::string_view displayName;
  float hardness = 0;      // -1 = irrompible
  float resistance = 0;
  int stackSize = 64;
  bool diggable = false;
  bool fullBox = false;    // la caja de colisión es el bloque entero
  bool transparent = true;
  int emitLight = 0;
  int opacity = 0;         // cuánto reduce la luz al atravesarlo (0..15)
  std::string_view material;  // material de minecraft-data ("rock", "wood", "dirt"...): decide la herramienta

  // Propiedades de render/lógica derivadas (ver blocks.cpp)
  bool opaqueCube = false;  // ocupa el bloque entero y no deja ver a través: oculta caras vecinas y hace AO
  bool selfCull = false;    // oculta caras contra el mismo bloque (cristal, hielo)
  bool fluid = false;
  RenderLayer layer = RenderLayer::Solid;
  TintType tint = TintType::None;
  u32 tintColor = 0xFFFFFF;
  bool exists = false;
};

namespace detail {
/// Tabla de 256 punteros (los ids que no existen apuntan al aire). Se rellena al arrancar.
extern const BlockInfo* const* blockTable;
const BlockInfo& blockInfoSlow(int id);
}  // namespace detail

/// Datos de un id de bloque. Va en línea: el mallador y la luz la llaman por cada bloque.
inline const BlockInfo& blockInfo(int id) {
  if (static_cast<unsigned>(id) < 256 && detail::blockTable) [[likely]]
    return *detail::blockTable[id];
  return detail::blockInfoSlow(id);
}
const BlockInfo& blockInfo(BlockState s) = delete;  // evitar confundir estado con id
/// Tinte de un estado concreto (las hojas de abedul y abeto tienen color fijo).
TintType tintTypeOf(BlockState s);
int blockIdByName(std::string_view name);  // -1 si no existe

/// Ids de bloques de 1.8 usados directamente por el motor.
namespace B {
inline constexpr int air = 0, stone = 1, grass = 2, dirt = 3, cobblestone = 4, planks = 5, sapling = 6, bedrock = 7,
                     flowing_water = 8, water = 9, flowing_lava = 10, lava = 11, sand = 12, gravel = 13, gold_ore = 14,
                     iron_ore = 15, coal_ore = 16, log = 17, leaves = 18, sponge = 19, glass = 20, lapis_ore = 21,
                     lapis_block = 22, sandstone = 24, web = 30, tallgrass = 31, deadbush = 32, wool = 35,
                     yellow_flower = 37, red_flower = 38, brown_mushroom = 39, red_mushroom = 40, gold_block = 41,
                     iron_block = 42, brick_block = 45, tnt = 46, bookshelf = 47, mossy_cobblestone = 48, obsidian = 49,
                     torch = 50, diamond_ore = 56, diamond_block = 57, crafting_table = 58, furnace = 61, lit_furnace = 62, redstone_ore = 73,
                     lit_redstone_ore = 74, snow_layer = 78, ice = 79, snow = 80, cactus = 81, clay = 82, reeds = 83,
                     pumpkin = 86, netherrack = 87, soul_sand = 88, glowstone = 89, stained_glass = 95, stonebrick = 98,
                     melon_block = 103, vine = 106, mycelium = 110, waterlily = 111, nether_brick = 112,
                     end_stone = 121, emerald_ore = 129, emerald_block = 133, quartz_block = 155,
                     stained_hardened_clay = 159, stained_glass_pane = 160, leaves2 = 161, log2 = 162,
                     slime = 165, sea_lantern = 169, hardened_clay = 172, coal_block = 173, packed_ice = 174,
                     double_plant = 175;
}  // namespace B

inline bool isFluid(int id) { return id >= B::flowing_water && id <= B::lava; }
inline bool isWater(int id) { return id == B::flowing_water || id == B::water; }
inline bool isLava(int id) { return id == B::flowing_lava || id == B::lava; }

}  // namespace mcw
