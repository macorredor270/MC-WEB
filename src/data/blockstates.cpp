// Relación entre estados de bloque de 1.8 (id + metadata) y los blockstates de los resource
// packs. Es el formato público de los resource packs de 1.8; los valores de metadata están
// documentados en minecraft.wiki (sección "Data values" de cada bloque). Los tests comprueban
// cada entrada contra los assets de un 1.8.8.jar si se indica MCWEB_ASSETS_DIR.
#include "data/blockstates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>

namespace mcw {
namespace {

using Mapper = std::function<std::optional<BlockstateRef>(int meta, int ext)>;

const std::array<const char*, 16> kColors = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                             "pink",  "gray",   "silver",  "cyan",       "purple", "blue",
                                             "brown", "green",  "red",     "black"};
const std::array<const char*, 6> kWood = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak"};
const std::array<const char*, 4> kAxis = {"axis=y", "axis=x", "axis=z", "axis=none"};
/// Dirección horizontal por índice (la de 1.8: sur, oeste, norte, este).
const std::array<const char*, 4> kHorizontal = {"south", "west", "north", "east"};
/// Dirección por índice de 6 (abajo, arriba, norte, sur, oeste, este).
const std::array<const char*, 6> kFacing6 = {"down", "up", "north", "south", "west", "east"};
const char* b(bool v) { return v ? "true" : "false"; }

Mapper normal(std::string file) {
  return [file](int, int) { return BlockstateRef{file, "normal"}; };
}

Mapper byMeta(std::vector<std::string> files, std::string variant = "normal") {
  return [files, variant](int m, int) -> std::optional<BlockstateRef> {
    if (m < 0 || m >= static_cast<int>(files.size())) return std::nullopt;
    return BlockstateRef{files[m], variant};
  };
}

/// Una propiedad entera guardada en toda la metadata (edad de cultivos, potencia...).
Mapper intProp(std::string file, std::string prop, int max) {
  return [file, prop, max](int m, int) -> std::optional<BlockstateRef> {
    if (m > max) return std::nullopt;
    return BlockstateRef{file, prop + "=" + std::to_string(m)};
  };
}

std::vector<std::string> woodNames(const char* suffix, int count = 6) {
  std::vector<std::string> v;
  for (int i = 0; i < count; i++) v.push_back(std::string(kWood[i]) + suffix);
  return v;
}

// --- Bloques que dependen de los vecinos ---

enum class Kind { None, Fence, Pane, Wall, Stairs, Door, Wire, Vine, Gate, Stem, Tripwire };

bool isWoodFence(int id) { return id == 85 || (id >= 188 && id <= 192); }
bool isFenceGate(int id) { return id == 107 || (id >= 183 && id <= 187); }
bool isPane(int id) { return id == 101 || id == 102 || id == 160; }
bool isStairs(int id) {
  switch (id) {
    case 53: case 67: case 108: case 109: case 114: case 128: case 134: case 135: case 136: case 156: case 163:
    case 164: case 180: return true;
    default: return false;
  }
}
bool isDoor(int id) { return id == 64 || id == 71 || (id >= 193 && id <= 197); }

Kind kindOf(int id) {
  if (isWoodFence(id) || id == 113) return Kind::Fence;
  if (isPane(id)) return Kind::Pane;
  if (id == 139) return Kind::Wall;
  if (isStairs(id)) return Kind::Stairs;
  if (isDoor(id)) return Kind::Door;
  if (id == 55) return Kind::Wire;
  if (id == 106) return Kind::Vine;
  if (isFenceGate(id)) return Kind::Gate;
  if (id == 104 || id == 105) return Kind::Stem;
  if (id == 132) return Kind::Tripwire;
  return Kind::None;
}

// Direcciones horizontales en el orden de los bits de conexión: norte, este, sur, oeste.
constexpr int kDX[4] = {0, 1, 0, -1};
constexpr int kDZ[4] = {-1, 0, 1, 0};

/// Bloque "normal" al que se pegan vallas, muros y paneles (cubo opaco, sin calabazas ni sandías).
bool solidForConnect(int id) {
  if (id == 86 || id == 91 || id == 103) return false;
  return blockInfo(id).opaqueCube;
}

std::string fourSides(int bits) {
  return std::string("east=") + b(bits & 2) + ",north=" + b(bits & 1) + ",south=" + b(bits & 4) + ",west=" + b(bits & 8);
}

/// Forma de las escaleras (índice de kStairShape) según sus vecinos, como en 1.8.
const std::array<const char*, 5> kStairShape = {"straight", "inner_left", "inner_right", "outer_left", "outer_right"};

/// Dirección (0 norte, 1 este, 2 sur, 3 oeste) de unas escaleras con su metadata.
int stairDir(int meta) {
  static const int d[4] = {1, 3, 2, 0};  // 0 este, 1 oeste, 2 sur, 3 norte
  return d[meta & 3];
}

const char* dirName(int d) {
  static const char* n[4] = {"north", "east", "south", "west"};
  return n[d & 3];
}

}  // namespace

u32 redstoneWireColor(int power) {
  const float f = power / 15.0f;
  const float r = power == 0 ? 0.3f : f * 0.6f + 0.4f;
  const float g = std::max(0.0f, f * f * 0.7f - 0.5f);
  const float bl = std::max(0.0f, f * f * 0.6f - 0.7f);
  return (u32(r * 255) << 16) | (u32(g * 255) << 8) | u32(bl * 255);
}

u32 stemColor(int age) {
  return (u32(std::min(255, age * 32)) << 16) | (u32(255 - age * 8) << 8) | u32(age * 4);
}

namespace {

std::optional<BlockstateRef> extendedRef(int id, int meta, int ext) {
  switch (kindOf(id)) {
    case Kind::Fence: {
      static const std::map<int, std::string> f = {{85, "fence"},         {188, "spruce_fence"},  {189, "birch_fence"},
                                                   {190, "jungle_fence"}, {191, "dark_oak_fence"}, {192, "acacia_fence"},
                                                   {113, "nether_brick_fence"}};
      return BlockstateRef{f.at(id), fourSides(ext)};
    }
    case Kind::Pane: {
      std::string file = id == 101 ? "iron_bars" : (id == 102 ? "glass_pane" : std::string(kColors[meta]) + "_stained_glass_pane");
      return BlockstateRef{file, fourSides(ext)};
    }
    case Kind::Wall: {
      if (meta > 1) return std::nullopt;
      return BlockstateRef{meta ? "mossy_cobblestone_wall" : "cobblestone_wall",
                           std::string("east=") + b(ext & 2) + ",north=" + b(ext & 1) + ",south=" + b(ext & 4) +
                               ",up=" + b(ext & 16) + ",west=" + b(ext & 8)};
    }
    case Kind::Stairs: {
      static const std::map<int, std::string> f = {
          {53, "oak_stairs"},          {67, "stone_stairs"},    {108, "brick_stairs"},  {109, "stone_brick_stairs"},
          {114, "nether_brick_stairs"}, {128, "sandstone_stairs"}, {134, "spruce_stairs"}, {135, "birch_stairs"},
          {136, "jungle_stairs"},      {156, "quartz_stairs"},  {163, "acacia_stairs"}, {164, "dark_oak_stairs"},
          {180, "red_sandstone_stairs"}};
      if (meta >= 8 || ext > 4) return std::nullopt;
      return BlockstateRef{f.at(id), std::string("facing=") + dirName(stairDir(meta)) + ",half=" +
                                         ((meta & 4) ? "top" : "bottom") + ",shape=" + kStairShape[ext]};
    }
    case Kind::Door: {
      static const std::map<int, std::string> f = {{64, "wooden_door"},  {71, "iron_door"},    {193, "spruce_door"},
                                                   {194, "birch_door"},  {195, "jungle_door"}, {196, "acacia_door"},
                                                   {197, "dark_oak_door"}};
      static const char* facing[4] = {"east", "south", "west", "north"};
      return BlockstateRef{f.at(id), std::string("facing=") + facing[ext & 3] + ",half=" + ((ext & 16) ? "upper" : "lower") +
                                         ",hinge=" + ((ext & 8) ? "right" : "left") + ",open=" + b(ext & 4)};
    }
    case Kind::Wire: {
      static const char* v[3] = {"none", "side", "up"};
      const int n = ext % 3, e = (ext / 3) % 3, s = (ext / 9) % 3, w = (ext / 27) % 3;
      return BlockstateRef{"redstone_wire", std::string("east=") + v[e] + ",north=" + v[n] + ",south=" + v[s] + ",west=" + v[w]};
    }
    case Kind::Vine:
      // Metadata: 1 sur, 2 oeste, 4 norte, 8 este
      return BlockstateRef{"vine", std::string("east=") + b(meta & 8) + ",north=" + b(meta & 4) + ",south=" + b(meta & 1) +
                                       ",up=" + b(ext & 1) + ",west=" + b(meta & 2)};
    case Kind::Gate: {
      static const std::map<int, std::string> f = {{107, "fence_gate"},        {183, "spruce_fence_gate"},
                                                   {184, "birch_fence_gate"},  {185, "jungle_fence_gate"},
                                                   {186, "dark_oak_fence_gate"}, {187, "acacia_fence_gate"}};
      return BlockstateRef{f.at(id), std::string("facing=") + kHorizontal[meta & 3] + ",in_wall=" + b(ext & 1) +
                                         ",open=" + b(meta & 4)};
    }
    case Kind::Stem: {
      if (meta > 7) return std::nullopt;
      static const char* dirs[5] = {"up", "north", "east", "south", "west"};
      return BlockstateRef{id == 104 ? "pumpkin_stem" : "melon_stem", "age=" + std::to_string(meta) + ",facing=" + dirs[ext]};
    }
    case Kind::Tripwire:
      return BlockstateRef{"tripwire", std::string("attached=") + b(meta & 4) + ",east=" + b(ext & 2) + ",north=" + b(ext & 1) +
                                           ",south=" + b(ext & 4) + ",suspended=" + b(meta & 2) + ",west=" + b(ext & 8)};
    case Kind::None: break;
  }
  return std::nullopt;
}

const std::map<int, Mapper>& mappers() {
  static const std::map<int, Mapper> m = [] {
    std::map<int, Mapper> t;
    t[B::stone] = byMeta({"stone", "granite", "smooth_granite", "diorite", "smooth_diorite", "andesite", "smooth_andesite"});
    // Bit 3 = estado virtual "con nieve encima" (en el mundo la hierba siempre es meta 0; el mallador
    // lo activa si hay nieve encima, como hace 1.8 al calcular el estado real).
    t[B::grass] = [](int m, int) { return BlockstateRef{"grass", (m & 8) ? "snowy=true" : "snowy=false"}; };
    t[B::dirt] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 7) == 2) return BlockstateRef{"podzol", (m & 8) ? "snowy=true" : "snowy=false"};
      if (m > 1) return std::nullopt;
      return BlockstateRef{m ? "coarse_dirt" : "dirt", "normal"};
    };
    t[B::cobblestone] = normal("cobblestone");
    t[B::planks] = byMeta(woodNames("_planks"));
    t[B::sapling] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 7) >= 6) return std::nullopt;
      return BlockstateRef{std::string(kWood[m & 7]) + "_sapling", "stage=" + std::to_string(m >> 3)};
    };
    t[B::bedrock] = normal("bedrock");
    t[B::sand] = byMeta({"sand", "red_sand"});
    t[B::gravel] = normal("gravel");
    t[B::gold_ore] = normal("gold_ore");
    t[B::iron_ore] = normal("iron_ore");
    t[B::coal_ore] = normal("coal_ore");
    t[B::log] = [](int m, int) { return BlockstateRef{std::string(kWood[m & 3]) + "_log", kAxis[m >> 2]}; };
    t[B::log2] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 3) >= 2) return std::nullopt;
      return BlockstateRef{std::string(kWood[4 + (m & 3)]) + "_log", kAxis[m >> 2]};
    };
    t[B::leaves] = [](int m, int) { return BlockstateRef{std::string(kWood[m & 3]) + "_leaves", "normal"}; };
    t[B::leaves2] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 3) >= 2) return std::nullopt;
      return BlockstateRef{std::string(kWood[4 + (m & 3)]) + "_leaves", "normal"};
    };
    t[B::sponge] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m > 1) return std::nullopt;
      return BlockstateRef{"sponge", m == 1 ? "wet=true" : "wet=false"};
    };
    t[B::glass] = normal("glass");
    t[B::lapis_ore] = normal("lapis_ore");
    t[B::lapis_block] = normal("lapis_block");
    // Dispensador y soltador: 0 abajo, 1 arriba, 2 norte, 3 sur, 4 oeste, 5 este (bit 3 = activado, no se dibuja)
    for (auto [id, file] : {std::pair{23, "dispenser"}, std::pair{158, "dropper"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        if ((m & 7) > 5) return std::nullopt;
        return BlockstateRef{file, std::string("facing=") + kFacing6[m & 7]};
      };
    t[B::sandstone] = byMeta({"sandstone", "chiseled_sandstone", "smooth_sandstone"});
    t[25] = normal("noteblock");
    // Cama: dirección horizontal en bits 0-1, bit 3 = cabecera (bit 2 ocupada, no se dibuja)
    t[26] = [](int m, int) {
      return BlockstateRef{"bed", std::string("facing=") + kHorizontal[m & 3] + ",part=" + ((m & 8) ? "head" : "foot")};
    };
    // Raíles: bits 0-2 forma, bit 3 activado
    static const char* kRailShape[10] = {"north_south", "east_west", "ascending_east", "ascending_west", "ascending_north",
                                         "ascending_south", "south_east", "south_west", "north_west", "north_east"};
    for (auto [id, file] : {std::pair{27, "golden_rail"}, std::pair{28, "detector_rail"}, std::pair{157, "activator_rail"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        if ((m & 7) > 5) return std::nullopt;
        return BlockstateRef{file, std::string("powered=") + b(m & 8) + ",shape=" + kRailShape[m & 7]};
      };
    t[66] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m > 9) return std::nullopt;
      return BlockstateRef{"rail", std::string("shape=") + kRailShape[m]};
    };
    // Pistones: bits 0-2 dirección, bit 3 extendido
    for (auto [id, file] : {std::pair{29, "sticky_piston"}, std::pair{33, "piston"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        if ((m & 7) > 5) return std::nullopt;
        return BlockstateRef{file, std::string("extended=") + b(m & 8) + ",facing=" + kFacing6[m & 7]};
      };
    t[34] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 7) > 5) return std::nullopt;
      return BlockstateRef{"piston_head", std::string("facing=") + kFacing6[m & 7] + ",short=false,type=" + ((m & 8) ? "sticky" : "normal")};
    };
    t[B::web] = normal("web");
    t[B::tallgrass] = byMeta({"dead_bush", "tall_grass", "fern"});
    t[B::deadbush] = normal("dead_bush");
    t[B::wool] = [](int m, int) { return BlockstateRef{std::string(kColors[m]) + "_wool", "normal"}; };
    t[B::yellow_flower] = normal("dandelion");
    t[B::red_flower] = byMeta({"poppy", "blue_orchid", "allium", "houstonia", "red_tulip", "orange_tulip",
                               "white_tulip", "pink_tulip", "oxeye_daisy"});
    t[B::brown_mushroom] = normal("brown_mushroom");
    t[B::red_mushroom] = normal("red_mushroom");
    t[B::gold_block] = normal("gold_block");
    t[B::iron_block] = normal("iron_block");
    // Losas: bits 0-2 tipo, bit 3 mitad de arriba (en las dobles: "liso", variante "all")
    static const std::vector<std::string> kSlab1 = {"stone", "sandstone", "wood_old", "cobblestone", "brick", "stone_brick",
                                                    "nether_brick", "quartz"};
    t[43] = [](int m, int) {
      const std::string type = kSlab1[m & 7];
      const bool seamless = (m & 8) && (type == "stone" || type == "sandstone" || type == "quartz");
      return BlockstateRef{"double_" + type + "_slab", seamless ? "all" : "normal"};
    };
    t[44] = [](int m, int) { return BlockstateRef{kSlab1[m & 7] + "_slab", (m & 8) ? "half=top" : "half=bottom"}; };
    t[181] = [](int m, int) { return BlockstateRef{"double_red_sandstone_slab", (m & 8) ? "all" : "normal"}; };
    t[182] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m & 7) return std::nullopt;
      return BlockstateRef{"red_sandstone_slab", (m & 8) ? "half=top" : "half=bottom"};
    };
    t[125] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 7) > 5) return std::nullopt;
      return BlockstateRef{std::string(kWood[m & 7]) + "_double_slab", "normal"};
    };
    t[126] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m & 7) > 5) return std::nullopt;
      return BlockstateRef{std::string(kWood[m & 7]) + "_slab", (m & 8) ? "half=top" : "half=bottom"};
    };
    t[B::brick_block] = normal("brick_block");
    t[B::tnt] = normal("tnt");
    t[B::bookshelf] = normal("bookshelf");
    t[B::mossy_cobblestone] = normal("mossy_cobblestone");
    t[B::obsidian] = normal("obsidian");
    // Antorchas: 1 este, 2 oeste, 3 sur, 4 norte, 5 suelo
    for (auto [id, file] : {std::pair{50, "torch"}, std::pair{75, "unlit_redstone_torch"}, std::pair{76, "redstone_torch"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        static const char* f[] = {nullptr, "facing=east", "facing=west", "facing=south", "facing=north", "facing=up"};
        if (m < 1 || m > 5) return std::nullopt;
        return BlockstateRef{file, f[m]};
      };
    t[51] = [](int, int) { return BlockstateRef{"fire", "alt=false,east=false,flip=false,north=false,south=false,upper=0,west=false"}; };
    t[52] = normal("mob_spawner");
    for (auto [id, file] : {std::pair{54, "chest"}, std::pair{146, "trapped_chest"}, std::pair{130, "ender_chest"}})
      t[id] = normal(file);
    t[B::diamond_ore] = normal("diamond_ore");
    t[B::diamond_block] = normal("diamond_block");
    t[B::crafting_table] = normal("crafting_table");
    t[59] = intProp("wheat", "age", 7);
    t[60] = intProp("farmland", "moisture", 7);
    for (int id : {B::furnace, B::lit_furnace}) {
      const std::string file = id == B::furnace ? "furnace" : "lit_furnace";
      t[id] = [file](int m, int) {
        static const char* f[] = {"facing=north", "facing=north", "facing=north", "facing=south", "facing=west", "facing=east"};
        return BlockstateRef{file, f[m < 6 ? m : 2]};
      };
    }
    // Carteles y estandartes se dibujan aparte (como en 1.8): su modelo solo trae la partícula
    for (auto [id, file] : {std::pair{63, "standing_sign"}, std::pair{68, "wall_sign"}, std::pair{176, "standing_banner"},
                            std::pair{177, "wall_banner"}, std::pair{144, "skull"}})
      t[id] = normal(file);
    // Escalera de mano, ganchos de cable trampa: 2 norte, 3 sur, 4 oeste, 5 este
    t[65] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m < 2 || m > 5) return std::nullopt;
      return BlockstateRef{"ladder", std::string("facing=") + kFacing6[m]};
    };
    // Palanca: 0 techo (x), 1 este, 2 oeste, 3 sur, 4 norte, 5 suelo (z), 6 suelo (x), 7 techo (z); bit 3 activada
    t[69] = [](int m, int) {
      static const char* f[8] = {"down_x", "east", "west", "south", "north", "up_z", "up_x", "down_z"};
      return BlockstateRef{"lever", std::string("facing=") + f[m & 7] + ",powered=" + b(m & 8)};
    };
    for (auto [id, file] : {std::pair{70, "stone_pressure_plate"}, std::pair{72, "wooden_pressure_plate"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        if (m > 1) return std::nullopt;
        return BlockstateRef{file, std::string("powered=") + b(m & 1)};
      };
    t[147] = intProp("light_weighted_pressure_plate", "power", 15);
    t[148] = intProp("heavy_weighted_pressure_plate", "power", 15);
    t[B::redstone_ore] = normal("redstone_ore");
    t[B::lit_redstone_ore] = normal("lit_redstone_ore");
    // Botones: 0 techo, 1 este, 2 oeste, 3 sur, 4 norte, 5 suelo; bit 3 pulsado
    for (auto [id, file] : {std::pair{77, "stone_button"}, std::pair{143, "wooden_button"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        if ((m & 7) > 5) return std::nullopt;
        return BlockstateRef{file, std::string("facing=") + kFacing6[m & 7] + ",powered=" + b(m & 8)};
      };
    t[B::snow_layer] = [](int m, int) { return BlockstateRef{"snow_layer", "layers=" + std::to_string((m & 7) + 1)}; };
    t[B::ice] = normal("ice");
    t[B::snow] = normal("snow");
    t[B::cactus] = normal("cactus");
    t[B::clay] = normal("clay");
    t[B::reeds] = normal("reeds");
    t[84] = normal("jukebox");
    // Calabazas: dirección horizontal
    for (auto [id, file] : {std::pair{86, "pumpkin"}, std::pair{91, "lit_pumpkin"}})
      t[id] = [file = std::string(file)](int m, int) { return BlockstateRef{file, std::string("facing=") + kHorizontal[m & 3]}; };
    t[B::netherrack] = normal("netherrack");
    t[B::soul_sand] = normal("soul_sand");
    t[B::glowstone] = normal("glowstone");
    t[90] = [](int m, int) { return BlockstateRef{"portal", m == 2 ? "axis=z" : "axis=x"}; };
    t[92] = intProp("cake", "bites", 6);
    // Repetidores: bits 0-1 dirección, bits 2-3 retardo - 1
    for (auto [id, file] : {std::pair{93, "unpowered_repeater"}, std::pair{94, "powered_repeater"}})
      t[id] = [file = std::string(file)](int m, int) {
        return BlockstateRef{file, "delay=" + std::to_string((m >> 2) + 1) + ",facing=" + kHorizontal[m & 3] + ",locked=false"};
      };
    t[B::stained_glass] = [](int m, int) { return BlockstateRef{std::string(kColors[m]) + "_stained_glass", "normal"}; };
    // Trampillas: bits 0-1 (0 norte, 1 sur, 2 oeste, 3 este), bit 2 abierta, bit 3 arriba
    for (auto [id, file] : {std::pair{96, "trapdoor"}, std::pair{167, "iron_trapdoor"}})
      t[id] = [file = std::string(file)](int m, int) {
        static const char* f[4] = {"north", "south", "west", "east"};
        return BlockstateRef{file, std::string("facing=") + f[m & 3] + ",half=" + ((m & 8) ? "top" : "bottom") + ",open=" + b(m & 4)};
      };
    t[97] = byMeta({"stone_monster_egg", "cobblestone_monster_egg", "stone_brick_monster_egg", "mossy_brick_monster_egg",
                    "cracked_brick_monster_egg", "chiseled_brick_monster_egg"});
    t[B::stonebrick] = byMeta({"stonebrick", "mossy_stonebrick", "cracked_stonebrick", "chiseled_stonebrick"});
    // Setas gigantes: qué caras tienen sombrero
    for (auto [id, file] : {std::pair{99, "brown_mushroom_block"}, std::pair{100, "red_mushroom_block"}})
      t[id] = [file = std::string(file)](int m, int) -> std::optional<BlockstateRef> {
        static const char* v[16] = {"all_inside", "north_west", "north", "north_east", "west", "center", "east", "south_west",
                                    "south",      "south_east", "stem",  nullptr,      nullptr, nullptr, "all_outside", "all_stem"};
        if (!v[m]) return std::nullopt;
        return BlockstateRef{file, std::string("variant=") + v[m]};
      };
    t[B::melon_block] = normal("melon_block");
    t[B::mycelium] = [](int m, int) { return BlockstateRef{"mycelium", (m & 8) ? "snowy=true" : "snowy=false"}; };
    t[B::waterlily] = normal("waterlily");
    t[B::nether_brick] = normal("nether_brick");
    t[115] = intProp("nether_wart", "age", 3);
    t[116] = normal("enchanting_table");
    t[117] = [](int m, int) {
      return BlockstateRef{"brewing_stand", std::string("has_bottle_0=") + b(m & 1) + ",has_bottle_1=" + b(m & 2) +
                                                ",has_bottle_2=" + b(m & 4)};
    };
    t[118] = intProp("cauldron", "level", 3);
    t[120] = [](int m, int) {
      return BlockstateRef{"end_portal_frame", std::string("eye=") + b(m & 4) + ",facing=" + kHorizontal[m & 3]};
    };
    t[B::end_stone] = normal("end_stone");
    t[122] = normal("dragon_egg");
    t[123] = normal("redstone_lamp");
    t[124] = normal("lit_redstone_lamp");
    // Cacao: bits 0-1 dirección, bits 2-3 edad
    t[127] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m >> 2) > 2) return std::nullopt;
      return BlockstateRef{"cocoa", "age=" + std::to_string(m >> 2) + ",facing=" + kHorizontal[m & 3]};
    };
    t[B::emerald_ore] = normal("emerald_ore");
    t[131] = [](int m, int) {
      return BlockstateRef{"tripwire_hook", std::string("attached=") + b(m & 4) + ",facing=" + kHorizontal[m & 3] +
                                                ",powered=" + b(m & 8)};
    };
    t[B::emerald_block] = normal("emerald_block");
    t[137] = normal("command_block");
    t[138] = normal("beacon");
    t[140] = [](int, int) { return BlockstateRef{"flower_pot", "contents=empty"}; };
    t[141] = intProp("carrots", "age", 7);
    t[142] = intProp("potatoes", "age", 7);
    // Yunque: bits 0-1 dirección, bits 2-3 daño
    t[145] = [](int m, int) -> std::optional<BlockstateRef> {
      if ((m >> 2) > 2) return std::nullopt;
      return BlockstateRef{"anvil", "damage=" + std::to_string(m >> 2) + ",facing=" + kHorizontal[m & 3]};
    };
    // Comparadores: bits 0-1 dirección, bit 2 resta, bit 3 activado
    for (auto [id, file] : {std::pair{149, "unpowered_comparator"}, std::pair{150, "powered_comparator"}})
      t[id] = [file = std::string(file)](int m, int) {
        return BlockstateRef{file, std::string("facing=") + kHorizontal[m & 3] + ",mode=" + ((m & 4) ? "subtract" : "compare") +
                                       ",powered=" + b(m & 8)};
      };
    t[151] = intProp("daylight_detector", "power", 15);
    t[178] = intProp("daylight_detector_inverted", "power", 15);
    t[152] = normal("redstone_block");
    t[153] = normal("quartz_ore");
    // Tolva: 0 abajo, 2-5 horizontal
    t[154] = [](int m, int) -> std::optional<BlockstateRef> {
      const int f = m & 7;
      if (f == 1 || f > 5) return std::nullopt;
      return BlockstateRef{"hopper", std::string("facing=") + kFacing6[f]};
    };
    t[B::quartz_block] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m == 0) return BlockstateRef{"quartz_block", "normal"};
      if (m == 1) return BlockstateRef{"chiseled_quartz_block", "normal"};
      if (m == 2) return BlockstateRef{"quartz_column", "axis=y"};
      if (m == 3) return BlockstateRef{"quartz_column", "axis=x"};
      if (m == 4) return BlockstateRef{"quartz_column", "axis=z"};
      return std::nullopt;
    };
    t[B::stained_hardened_clay] = [](int m, int) {
      return BlockstateRef{std::string(kColors[m]) + "_stained_hardened_clay", "normal"};
    };
    t[166] = normal("barrier");
    t[168] = byMeta({"prismarine", "prismarine_bricks", "dark_prismarine"});
    t[B::slime] = normal("slime");
    t[B::sea_lantern] = normal("sea_lantern");
    t[170] = [](int m, int) -> std::optional<BlockstateRef> {
      if (m & 3) return std::nullopt;
      return BlockstateRef{"hay_block", kAxis[m >> 2]};
    };
    t[171] = [](int m, int) { return BlockstateRef{std::string(kColors[m]) + "_carpet", "normal"}; };
    t[B::hardened_clay] = normal("hardened_clay");
    t[B::coal_block] = normal("coal_block");
    t[B::packed_ice] = normal("packed_ice");
    t[B::double_plant] = [](int m, int) -> std::optional<BlockstateRef> {
      static const char* lower[] = {"sunflower", "syringa", "double_grass", "double_fern", "double_rose", "paeonia"};
      // La mitad superior (bit 3) no guarda el tipo en el mundo: el mallador mira la mitad de abajo
      // y usa el estado virtual 8 + tipo, que es el que se mapea aquí.
      const int type = m & 7;
      if (type >= 6) return std::nullopt;
      return BlockstateRef{lower[type], (m & 8) ? "half=upper" : "half=lower"};
    };
    t[179] = byMeta({"red_sandstone", "chiseled_red_sandstone", "smooth_red_sandstone"});

    // Los que dependen de los vecinos: por defecto, sin conexiones
    for (int id = 0; id < 256; id++) {
      if (kindOf(id) == Kind::None) continue;
      t[id] = [id](int m, int ext) { return extendedRef(id, m, ext); };
    }
    return t;
  }();
  return m;
}

/// Bits por defecto de un bloque que depende de los vecinos (cómo se ve suelto o como icono).
int defaultExt(int id, int meta) {
  switch (kindOf(id)) {
    case Kind::Wall: return 16;  // poste
    case Kind::Door: return (meta & 8) ? 16 : (meta & 7);
    case Kind::Wire: return 0;
    default: return 0;
  }
}

}  // namespace

bool dependsOnNeighbors(int id) {
  static const auto table = [] {
    std::array<bool, 256> t{};
    for (int i = 0; i < 256; i++) t[i] = kindOf(i) != Kind::None;
    return t;
  }();
  return static_cast<unsigned>(id) < 256 && table[id];
}

bool extReplacesMeta(int id) {
  const Kind k = kindOf(id);
  return k == Kind::Door || k == Kind::Wire;
}

int extCount(int id) {
  switch (kindOf(id)) {
    case Kind::Fence: case Kind::Pane: case Kind::Tripwire: return 16;
    case Kind::Wall: return 32;
    case Kind::Stairs: return 5;
    case Kind::Door: return 32;
    case Kind::Wire: return 81;
    case Kind::Vine: case Kind::Gate: return 2;
    case Kind::Stem: return 5;
    case Kind::None: break;
  }
  return 0;
}

int neighborBits(BlockState s, const void* ctx, NeighborFn at) {
  const int id = stateId(s), meta = stateMeta(s);
  auto nb = [&](int dx, int dy, int dz) { return at(ctx, dx, dy, dz); };
  auto nid = [&](int dx, int dy, int dz) { return stateId(at(ctx, dx, dy, dz)); };
  switch (kindOf(id)) {
    case Kind::Fence: {
      int bits = 0;
      for (int d = 0; d < 4; d++) {
        const int n = nid(kDX[d], 0, kDZ[d]);
        const bool same = id == 113 ? n == 113 : isWoodFence(n);
        if (same || isFenceGate(n) || solidForConnect(n)) bits |= 1 << d;
      }
      return bits;
    }
    case Kind::Pane: {
      int bits = 0;
      for (int d = 0; d < 4; d++) {
        const int n = nid(kDX[d], 0, kDZ[d]);
        if (isPane(n) || n == B::glass || n == B::stained_glass || solidForConnect(n)) bits |= 1 << d;
      }
      return bits;
    }
    case Kind::Wall: {
      int bits = 0;
      for (int d = 0; d < 4; d++) {
        const int n = nid(kDX[d], 0, kDZ[d]);
        if (n == 139 || isFenceGate(n) || solidForConnect(n)) bits |= 1 << d;
      }
      const bool straightNS = bits == (1 | 4), straightEW = bits == (2 | 8);
      const bool up = !(straightNS || straightEW) || nid(0, 1, 0) != B::air;
      return bits | (up ? 16 : 0);
    }
    case Kind::Stairs: {
      const int dir = stairDir(meta);
      const bool top = meta & 4;
      auto stairAt = [&](int d, int& outDir) {
        const BlockState n = nb(kDX[d], 0, kDZ[d]);
        if (!isStairs(stateId(n)) || ((stateMeta(n) & 4) != 0) != top) return false;
        outDir = stairDir(stateMeta(n));
        return true;
      };
      const int cw = (dir + 1) & 3, ccw = (dir + 3) & 3;
      int other;
      if (stairAt(dir, other) && (other == cw || other == ccw)) return other == cw ? 4 : 3;  // exterior
      if (stairAt((dir + 2) & 3, other) && (other == cw || other == ccw)) return other == cw ? 2 : 1;  // interior
      return 0;
    }
    case Kind::Door: {
      const bool upper = meta & 8;
      const BlockState other = nb(0, upper ? -1 : 1, 0);
      const bool pair = stateId(other) == id;
      const int lower = upper ? (pair ? stateMeta(other) : 0) : meta;
      const int top = upper ? meta : (pair ? stateMeta(other) : 8);
      return (lower & 3) | (lower & 4) | ((top & 1) << 3) | (upper ? 16 : 0);
    }
    case Kind::Wire: {
      auto connects = [&](BlockState n, int d) {
        const int i = stateId(n);
        switch (i) {
          case 55: case 75: case 76: case 152: case 69: case 77: case 143: case 70: case 72: case 147: case 148: case 151:
          case 178: case 149: case 150: case 28: case 146: case 131: return true;
          case 93: case 94: {  // repetidor: solo por delante o por detrás
            const int f = stateMeta(n) & 3;  // 0 sur, 1 oeste, 2 norte, 3 este
            const bool alongZ = f == 0 || f == 2;
            return alongZ == (d == 0 || d == 2);
          }
          default: return false;
        }
      };
      const bool aboveOpaque = blockInfo(nid(0, 1, 0)).opaqueCube;
      int v[4] = {0, 0, 0, 0};
      for (int d = 0; d < 4; d++) {
        const BlockState side = nb(kDX[d], 0, kDZ[d]);
        const bool sideOpaque = blockInfo(stateId(side)).opaqueCube;
        if (!aboveOpaque && sideOpaque && stateId(nb(kDX[d], 1, kDZ[d])) == 55) v[d] = 2;
        else if (connects(side, d)) v[d] = 1;
        else if (!sideOpaque && stateId(nb(kDX[d], -1, kDZ[d])) == 55) v[d] = 1;
      }
      return v[0] + 3 * v[1] + 9 * v[2] + 27 * v[3];
    }
    case Kind::Vine: return blockInfo(nid(0, 1, 0)).opaqueCube ? 1 : 0;
    case Kind::Gate: {
      const int f = meta & 3;  // 0 sur, 1 oeste, 2 norte, 3 este
      const bool alongX = f == 0 || f == 2;  // la puerta ocupa el eje X: mira los lados este y oeste
      const int a = alongX ? nid(1, 0, 0) : nid(0, 0, 1), c = alongX ? nid(-1, 0, 0) : nid(0, 0, -1);
      return (a == 139 || c == 139) ? 1 : 0;
    }
    case Kind::Stem: {
      const int fruit = id == 104 ? 86 : 103;
      for (int d = 0; d < 4; d++)
        if (nid(kDX[d], 0, kDZ[d]) == fruit) return d + 1;
      return 0;
    }
    case Kind::Tripwire: {
      int bits = 0;
      for (int d = 0; d < 4; d++) {
        const int n = nid(kDX[d], 0, kDZ[d]);
        if (n == 132 || n == 131) bits |= 1 << d;
      }
      return bits;
    }
    case Kind::None: break;
  }
  return 0;
}

std::optional<BlockstateRef> blockstateOf(BlockState state, int ext) {
  const auto& m = mappers();
  auto it = m.find(stateId(state));
  if (it == m.end()) return std::nullopt;
  return it->second(stateMeta(state), ext);
}

std::optional<BlockstateRef> blockstateOf(BlockState state) {
  return blockstateOf(state, defaultExt(stateId(state), stateMeta(state)));
}

std::vector<std::pair<BlockState, BlockstateRef>> allMappedStates() {
  std::vector<std::pair<BlockState, BlockstateRef>> out;
  for (const auto& [id, fn] : mappers())
    for (int meta = 0; meta < 16; meta++)
      if (auto ref = fn(meta, defaultExt(id, meta))) out.emplace_back(makeState(id, meta), *ref);
  return out;
}

std::vector<ExtendedStateRef> allExtendedStates() {
  std::vector<ExtendedStateRef> out;
  for (const auto& [id, fn] : mappers()) {
    if (!dependsOnNeighbors(id)) continue;
    const int metas = extReplacesMeta(id) ? 1 : 16;
    for (int meta = 0; meta < metas; meta++)
      for (int ext = 0; ext < extCount(id); ext++)
        if (auto ref = fn(meta, ext)) out.push_back({makeState(id, meta), ext, *ref});
  }
  return out;
}

}  // namespace mcw
