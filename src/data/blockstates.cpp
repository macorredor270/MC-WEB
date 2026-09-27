// Relación entre estados de bloque de 1.8 (id + metadata) y los blockstates de los resource
// packs. Es el formato público de los resource packs de 1.8; los valores de metadata están
// documentados en minecraft.wiki (sección "Data values" de cada bloque). Los tests comprueban
// cada entrada contra los assets de un 1.8.8.jar si se indica MCWEB_ASSETS_DIR.
#include "data/blockstates.h"

#include <array>
#include <functional>
#include <map>

namespace mcw {
namespace {

using Mapper = std::function<std::optional<BlockstateRef>(int meta)>;

const std::array<const char*, 16> kColors = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                             "pink",  "gray",   "silver",  "cyan",       "purple", "blue",
                                             "brown", "green",  "red",     "black"};
const std::array<const char*, 6> kWood = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak"};
const std::array<const char*, 4> kAxis = {"axis=y", "axis=x", "axis=z", "axis=none"};

Mapper normal(std::string file) {
  return [file](int) { return BlockstateRef{file, "normal"}; };
}

Mapper byMeta(std::vector<std::string> files, std::string variant = "normal") {
  return [files, variant](int m) -> std::optional<BlockstateRef> {
    if (m < 0 || m >= static_cast<int>(files.size())) return std::nullopt;
    return BlockstateRef{files[m], variant};
  };
}

std::vector<std::string> woodNames(const char* suffix, int count = 6) {
  std::vector<std::string> v;
  for (int i = 0; i < count; i++) v.push_back(std::string(kWood[i]) + suffix);
  return v;
}

const std::map<int, Mapper>& mappers() {
  static const std::map<int, Mapper> m = [] {
    std::map<int, Mapper> t;
    t[B::stone] = byMeta({"stone", "granite", "smooth_granite", "diorite", "smooth_diorite", "andesite", "smooth_andesite"});
    // Bit 3 = estado virtual "con nieve encima" (en el mundo la hierba siempre es meta 0; el mallador
    // lo activa si hay nieve encima, como hace 1.8 al calcular el estado real).
    t[B::grass] = [](int m) { return BlockstateRef{"grass", (m & 8) ? "snowy=true" : "snowy=false"}; };
    t[B::dirt] = [](int m) -> std::optional<BlockstateRef> {
      if ((m & 7) == 2) return BlockstateRef{"podzol", (m & 8) ? "snowy=true" : "snowy=false"};
      return byMeta({"dirt", "coarse_dirt"})(m);
    };
    t[B::cobblestone] = normal("cobblestone");
    t[B::planks] = byMeta(woodNames("_planks"));
    t[B::sapling] = [](int m) -> std::optional<BlockstateRef> {
      if ((m & 7) >= 6) return std::nullopt;
      return BlockstateRef{std::string(kWood[m & 7]) + "_sapling", "stage=" + std::to_string(m >> 3)};
    };
    t[B::bedrock] = normal("bedrock");
    t[B::sand] = byMeta({"sand", "red_sand"});
    t[B::gravel] = normal("gravel");
    t[B::gold_ore] = normal("gold_ore");
    t[B::iron_ore] = normal("iron_ore");
    t[B::coal_ore] = normal("coal_ore");
    t[B::log] = [](int m) { return BlockstateRef{std::string(kWood[m & 3]) + "_log", kAxis[m >> 2]}; };
    t[B::log2] = [](int m) -> std::optional<BlockstateRef> {
      if ((m & 3) >= 2) return std::nullopt;
      return BlockstateRef{std::string(kWood[4 + (m & 3)]) + "_log", kAxis[m >> 2]};
    };
    t[B::leaves] = [](int m) { return BlockstateRef{std::string(kWood[m & 3]) + "_leaves", "normal"}; };
    t[B::leaves2] = [](int m) -> std::optional<BlockstateRef> {
      if ((m & 3) >= 2) return std::nullopt;
      return BlockstateRef{std::string(kWood[4 + (m & 3)]) + "_leaves", "normal"};
    };
    t[B::sponge] = [](int m) { return BlockstateRef{"sponge", m == 1 ? "wet=true" : "wet=false"}; };
    t[B::glass] = normal("glass");
    t[B::lapis_ore] = normal("lapis_ore");
    t[B::lapis_block] = normal("lapis_block");
    t[B::sandstone] = byMeta({"sandstone", "chiseled_sandstone", "smooth_sandstone"});
    t[B::web] = normal("web");
    t[B::tallgrass] = byMeta({"dead_bush", "tall_grass", "fern"});
    t[B::deadbush] = normal("dead_bush");
    t[B::wool] = [](int m) { return BlockstateRef{std::string(kColors[m]) + "_wool", "normal"}; };
    t[B::yellow_flower] = normal("dandelion");
    t[B::red_flower] = byMeta({"poppy", "blue_orchid", "allium", "houstonia", "red_tulip", "orange_tulip",
                               "white_tulip", "pink_tulip", "oxeye_daisy"});
    t[B::brown_mushroom] = normal("brown_mushroom");
    t[B::red_mushroom] = normal("red_mushroom");
    t[B::gold_block] = normal("gold_block");
    t[B::iron_block] = normal("iron_block");
    t[B::brick_block] = normal("brick_block");
    t[B::tnt] = normal("tnt");
    t[B::bookshelf] = normal("bookshelf");
    t[B::mossy_cobblestone] = normal("mossy_cobblestone");
    t[B::obsidian] = normal("obsidian");
    t[B::torch] = [](int m) -> std::optional<BlockstateRef> {
      static const char* f[] = {nullptr, "facing=east", "facing=west", "facing=south", "facing=north", "facing=up"};
      if (m < 1 || m > 5) return std::nullopt;
      return BlockstateRef{"torch", f[m]};
    };
    t[B::diamond_ore] = normal("diamond_ore");
    t[B::diamond_block] = normal("diamond_block");
    t[B::crafting_table] = normal("crafting_table");
    t[B::redstone_ore] = normal("redstone_ore");
    t[B::lit_redstone_ore] = normal("lit_redstone_ore");
    t[B::snow_layer] = [](int m) { return BlockstateRef{"snow_layer", "layers=" + std::to_string((m & 7) + 1)}; };
    t[B::ice] = normal("ice");
    t[B::snow] = normal("snow");
    t[B::cactus] = normal("cactus");
    t[B::clay] = normal("clay");
    t[B::reeds] = normal("reeds");
    t[B::netherrack] = normal("netherrack");
    t[B::soul_sand] = normal("soul_sand");
    t[B::glowstone] = normal("glowstone");
    t[B::stained_glass] = [](int m) { return BlockstateRef{std::string(kColors[m]) + "_stained_glass", "normal"}; };
    t[B::stonebrick] = byMeta({"stonebrick", "mossy_stonebrick", "cracked_stonebrick", "chiseled_stonebrick"});
    t[B::melon_block] = normal("melon_block");
    t[B::mycelium] = [](int m) { return BlockstateRef{"mycelium", (m & 8) ? "snowy=true" : "snowy=false"}; };
    t[B::waterlily] = normal("waterlily");
    t[B::nether_brick] = normal("nether_brick");
    t[B::end_stone] = normal("end_stone");
    t[B::emerald_ore] = normal("emerald_ore");
    t[B::emerald_block] = normal("emerald_block");
    t[B::quartz_block] = byMeta({"quartz_block", "chiseled_quartz_block"});
    t[B::stained_hardened_clay] = [](int m) {
      return BlockstateRef{std::string(kColors[m]) + "_stained_hardened_clay", "normal"};
    };
    t[B::slime] = normal("slime");
    t[B::sea_lantern] = normal("sea_lantern");
    t[B::hardened_clay] = normal("hardened_clay");
    t[B::coal_block] = normal("coal_block");
    t[B::packed_ice] = normal("packed_ice");
    t[B::double_plant] = [](int m) -> std::optional<BlockstateRef> {
      static const char* lower[] = {"sunflower", "syringa", "double_grass", "double_fern", "double_rose", "paeonia"};
      // La mitad superior (bit 3) no guarda el tipo en el mundo: el mallador mira la mitad de abajo
      // y usa el estado virtual 8 + tipo, que es el que se mapea aquí.
      const int type = m & 7;
      if (type >= 6) return std::nullopt;
      return BlockstateRef{lower[type], (m & 8) ? "half=upper" : "half=lower"};
    };
    return t;
  }();
  return m;
}

}  // namespace

std::optional<BlockstateRef> blockstateOf(BlockState state) {
  const auto& m = mappers();
  auto it = m.find(stateId(state));
  if (it == m.end()) return std::nullopt;
  return it->second(stateMeta(state));
}

std::vector<std::pair<BlockState, BlockstateRef>> allMappedStates() {
  std::vector<std::pair<BlockState, BlockstateRef>> out;
  for (const auto& [id, fn] : mappers())
    for (int meta = 0; meta < 16; meta++)
      if (auto ref = fn(meta)) out.emplace_back(makeState(id, meta), *ref);
  return out;
}

}  // namespace mcw
