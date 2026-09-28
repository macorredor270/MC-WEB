#include "game/rules.h"

#include <cmath>

#include "data/blocks.h"
#include "data/blockstates.h"
#include "world/world.h"

namespace mcw {
namespace {

bool isOpaqueAt(const World& w, int x, int y, int z) { return blockInfo(stateId(w.block(x, y, z))).opaqueCube; }

/// ¿El id de bloque también es un ítem con ese mismo id?
bool isBlockItemPlaceable(int id) { return itemInfo(id).exists; }

bool isSoil(int id) { return id == B::grass || id == B::dirt || id == 60 /* farmland */; }

/// Metadata que conserva el objeto al soltarse (troncos pierden el eje, plantas dobles la mitad...).
int droppedMeta(int id, int meta) {
  switch (id) {
    case B::log: case B::log2: case B::leaves: case B::leaves2: return meta & 3;
    case B::sapling: return meta & 7;
    case B::double_plant: return meta & 7;
    case B::torch: case B::snow_layer: case B::reeds: case B::cactus: case B::grass: case B::mycelium: return 0;
    case B::dirt: return meta == 1 ? 1 : 0;
    default: return meta;
  }
}

}  // namespace

float digProgressPerTick(BlockState s, const ItemStack& tool, bool onGround, bool headInWater) {
  const int id = stateId(s);
  const float hardness = blockInfo(id).hardness;
  if (hardness < 0) return 0;
  if (hardness == 0) return 1.0f;
  const int toolId = tool.empty() ? 0 : tool.id;
  float speed = toolSpeed(id, toolId);
  if (headInWater) speed /= 5.0f;
  if (!onGround) speed /= 5.0f;
  return speed / hardness / (canHarvest(id, toolId) ? 30.0f : 100.0f);
}

std::vector<ItemStack> blockDrops(BlockState s, const ItemStack& tool, Random& rng) {
  const int id = stateId(s), meta = stateMeta(s);
  const int toolId = tool.empty() ? 0 : tool.id;
  std::vector<ItemStack> out;
  if (!canHarvest(id, toolId)) return out;
  const bool shears = toolId == ItemId::shears;

  switch (id) {
    case B::stone: out.emplace_back(meta == 0 ? B::cobblestone : B::stone, 1, meta == 0 ? 0 : meta); return out;
    case B::leaves: case B::leaves2: {
      if (shears) { out.emplace_back(id, 1, meta & 3); return out; }
      const int type = meta & 3;
      if (rng.nextInt(20) == 0) out.emplace_back(B::sapling, 1, id == B::leaves2 ? type + 4 : type);
      if ((id == B::leaves && type == 0) || (id == B::leaves2 && type == 1))
        if (rng.nextInt(200) == 0) out.emplace_back(ItemId::apple, 1, 0);
      return out;
    }
    case B::tallgrass:
      if (shears) out.emplace_back(B::tallgrass, 1, meta);
      else if (rng.nextInt(8) == 0) out.emplace_back(ItemId::wheat_seeds, 1, 0);
      return out;
    case B::double_plant: {
      if (meta & 8) return out;  // la mitad de arriba no suelta nada
      const int type = meta & 7;
      if (type == 2 || type == 3) {
        if (rng.nextInt(8) == 0) out.emplace_back(ItemId::wheat_seeds, 1, 0);
      } else {
        out.emplace_back(B::double_plant, 1, type);
      }
      return out;
    }
    case B::gravel: out.emplace_back(rng.nextInt(10) == 0 ? ItemId::flint : B::gravel, 1, 0); return out;
    case B::snow_layer: out.emplace_back(ItemId::snowball, (meta & 7) + 1, 0); return out;
    case B::snow: out.emplace_back(ItemId::snowball, 4, 0); return out;
    case B::deadbush: if (shears) out.emplace_back(B::deadbush, 1, 0); else if (rng.nextInt(2)) out.emplace_back(ItemId::stick, 1, 0); return out;
    case B::web: out.emplace_back(ItemId::string, 1, 0); return out;
    case 59:  // trigo: maduro da trigo y semillas
      if (meta >= 7) {
        out.emplace_back(ItemId::wheat, 1, 0);
        if (const int n = rng.nextInt(4)) out.emplace_back(ItemId::wheat_seeds, n, 0);
      } else {
        out.emplace_back(ItemId::wheat_seeds, 1, 0);
      }
      return out;
    case 141: case 142:
      out.emplace_back(id == 141 ? ItemId::carrot : ItemId::potato, meta >= 7 ? 1 + rng.nextInt(4) : 1, 0);
      if (id == 142 && meta >= 7 && rng.nextInt(50) == 0) out.emplace_back(ItemId::poisonous_potato, 1, 0);
      return out;
    case 115: out.emplace_back(ItemId::nether_wart, meta >= 3 ? 2 + rng.nextInt(3) : 1, 0); return out;
    case 127: out.emplace_back(ItemId::dye, (meta >> 2) >= 2 ? 3 : 1, 3); return out;
    case 104: case 105: return out;
    case 43: case 125: case 181: {  // losa doble: dos losas
      const ItemStack one = pickItem(s);
      out.emplace_back(one.id, 2, one.meta);
      return out;
    }
    case 92: case 51: case 90: case 34: case 36: case 119: return out;
    case 60: out.emplace_back(B::dirt, 1, 0); return out;
    default: break;
  }

  for (const DropEntry& d : dropsOf(id)) {
    int count;
    if (d.maxCount > d.minCount) count = static_cast<int>(d.minCount) + rng.nextInt(static_cast<int>(d.maxCount - d.minCount) + 1);
    else count = static_cast<int>(std::round(d.minCount));
    if (count <= 0) continue;
    const int dropMeta = d.meta >= 0 ? d.meta : (d.id == id ? droppedMeta(id, meta) : 0);
    if (d.id == id && !isBlockItemPlaceable(id)) {
      // Bloques que no son un ítem (puertas, camas, carteles...): sueltan su ítem
      const ItemStack item = pickItem(s);
      if (!item.empty()) out.emplace_back(item.id, count, item.meta);
      continue;
    }
    out.emplace_back(d.id, count, dropMeta);
  }
  return out;
}

bool isReplaceable(BlockState s) {
  const int id = stateId(s);
  return id == B::air || id == B::tallgrass || id == B::deadbush || isFluid(id) || id == B::vine || id == 51 /* fuego */ ||
         (id == B::snow_layer && (stateMeta(s) & 7) == 0);
}

bool canStay(const World& w, int x, int y, int z, BlockState s) {
  const int id = stateId(s), meta = stateMeta(s);
  const int below = stateId(w.block(x, y - 1, z));
  switch (id) {
    case B::tallgrass: case B::yellow_flower: case B::red_flower: case B::sapling: return isSoil(below);
    case B::deadbush: return below == B::sand || below == B::hardened_clay || below == B::stained_hardened_clay || below == B::dirt;
    case B::double_plant:
      if (meta & 8) return below == B::double_plant;
      return isSoil(below) && stateId(w.block(x, y + 1, z)) == B::double_plant;
    case B::brown_mushroom: case B::red_mushroom: return blockInfo(below).opaqueCube;
    case B::reeds: {
      if (below == B::reeds) return true;
      if (below != B::grass && below != B::dirt && below != B::sand) return false;
      for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
        if (isWater(stateId(w.block(x + dx, y - 1, z + dz)))) return true;
      return false;
    }
    case B::cactus: {
      if (below != B::cactus && below != B::sand) return false;
      for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
        if (blockInfo(stateId(w.block(x + dx, y, z + dz))).fullBox) return false;
      return true;
    }
    case B::snow_layer: return blockInfo(below).opaqueCube || below == B::leaves || below == B::leaves2;
    case B::waterlily: return isWater(below);
    case 59: case 141: case 142: case 104: case 105: return below == 60;  // cultivos sobre tierra de cultivo
    case 115: return below == B::soul_sand;
    case 55: case 93: case 94: case 149: case 150: case 27: case 28: case 66: case 157:
      return blockInfo(below).opaqueCube || below == B::glowstone || below == 89;
    case 70: case 72: case 147: case 148:
      return blockInfo(below).opaqueCube || below == 85 || (below >= 188 && below <= 192) || below == 113;
    case 92: case 171: case 63: case 176: case 140: return below != B::air && !isFluid(below);
    case 64: case 71: case 193: case 194: case 195: case 196: case 197:
      if (meta & 8) return below == id;
      return blockInfo(below).opaqueCube && stateId(w.block(x, y + 1, z)) == id;
    case 26: {  // cama: cada mitad necesita la otra
      static const int dx[4] = {0, -1, 0, 1}, dz[4] = {1, 0, -1, 0};
      const int h = meta & 3, sign = (meta & 8) ? -1 : 1;
      return stateId(w.block(x + dx[h] * sign, y, z + dz[h] * sign)) == 26;
    }
    case 127: {  // cacao: pegado a un tronco de jungla
      const glm::ivec3 o = supportOffset(s);
      const BlockState log = w.block(x + o.x, y + o.y, z + o.z);
      return stateId(log) == B::log && (stateMeta(log) & 3) == 3;
    }
    default: {
      const glm::ivec3 o = supportOffset(s);
      if (o != glm::ivec3(0)) return isOpaqueAt(w, x + o.x, y + o.y, z + o.z);
      return true;
    }
  }
}

std::optional<FoodValue> foodValue(const ItemStack& s) {
  switch (s.id) {
    case ItemId::apple: return FoodValue{4, 0.3f};
    case ItemId::bread: return FoodValue{5, 0.6f};
    case ItemId::porkchop: return FoodValue{3, 0.3f};
    case ItemId::cooked_porkchop: return FoodValue{8, 0.8f};
    case ItemId::golden_apple: return FoodValue{4, 1.2f};
    case ItemId::cookie: return FoodValue{2, 0.1f};
    case ItemId::melon: return FoodValue{2, 0.3f};
    case ItemId::beef: return FoodValue{3, 0.3f};
    case ItemId::cooked_beef: return FoodValue{8, 0.8f};
    case ItemId::chicken: return FoodValue{2, 0.3f};
    case ItemId::cooked_chicken: return FoodValue{6, 0.6f};
    case ItemId::rotten_flesh: return FoodValue{4, 0.1f};
    case ItemId::carrot: return FoodValue{3, 0.6f};
    case ItemId::potato: return FoodValue{1, 0.3f};
    case ItemId::baked_potato: return FoodValue{5, 0.6f};
    case ItemId::mushroom_stew: return FoodValue{6, 0.6f};
    case ItemId::mutton: return FoodValue{2, 0.3f};
    case ItemId::cooked_mutton: return FoodValue{6, 0.8f};
    case ItemId::spider_eye: return FoodValue{2, 0.8f};
    default: return std::nullopt;
  }
}

std::optional<ItemStack> smeltingResult(const ItemStack& s) {
  switch (s.id) {
    case B::iron_ore: return ItemStack(ItemId::iron_ingot);
    case B::gold_ore: return ItemStack(ItemId::gold_ingot);
    case B::sand: return ItemStack(B::glass);
    case B::cobblestone: return ItemStack(B::stone);
    case B::log: case B::log2: return ItemStack(ItemId::coal, 1, 1);
    case B::cactus: return ItemStack(ItemId::dye, 1, 2);
    case B::clay: return ItemStack(B::hardened_clay);
    case B::diamond_ore: return ItemStack(ItemId::diamond);
    case B::coal_ore: return ItemStack(ItemId::coal);
    case B::lapis_ore: return ItemStack(ItemId::dye, 1, 4);
    case B::redstone_ore: return ItemStack(ItemId::redstone);
    case B::emerald_ore: return ItemStack(ItemId::emerald);
    case B::stonebrick: return s.meta == 0 ? std::optional(ItemStack(B::stonebrick, 1, 2)) : std::nullopt;
    case B::sponge: return s.meta == 1 ? std::optional(ItemStack(B::sponge, 1, 0)) : std::nullopt;
    case ItemId::clay_ball: return ItemStack(ItemId::brick);
    case ItemId::porkchop: return ItemStack(ItemId::cooked_porkchop);
    case ItemId::beef: return ItemStack(ItemId::cooked_beef);
    case ItemId::chicken: return ItemStack(ItemId::cooked_chicken);
    case ItemId::mutton: return ItemStack(ItemId::cooked_mutton);
    case ItemId::potato: return ItemStack(ItemId::baked_potato);
    default: return std::nullopt;
  }
}

int fuelTicks(const ItemStack& s) {
  switch (s.id) {
    case ItemId::coal: return 1600;
    case B::coal_block: return 16000;
    case B::log: case B::log2: case B::planks: case B::crafting_table: case B::bookshelf: return 300;
    case B::sapling: case ItemId::stick: return 100;
    case ItemId::wooden_sword: case ItemId::wooden_shovel: case ItemId::wooden_pickaxe: case ItemId::wooden_axe:
    case ItemId::wooden_hoe: return 200;
    case ItemId::lava_bucket: return 20000;
    case ItemId::blaze_rod: return 2400;
    default: return 0;
  }
}

const std::vector<ItemStack>& creativeItems() {
  static const std::vector<ItemStack> items = [] {
    std::vector<ItemStack> v;
    // Bloques: cada variante que es un ítem en 1.8
    auto variants = [](int id) -> std::vector<int> {
      auto upTo = [](int n) { std::vector<int> r; for (int i = 0; i < n; i++) r.push_back(i); return r; };
      switch (id) {
        case B::stone: return upTo(7);
        case B::dirt: return upTo(3);
        case B::planks: case B::sapling: case 126: return upTo(6);
        case B::sand: case B::log2: case B::leaves2: case B::sponge: case 139: return upTo(2);
        case B::log: case B::leaves: case B::stonebrick: return upTo(4);
        case B::sandstone: case B::quartz_block: case 168: case 179: case 145: return upTo(3);
        case B::tallgrass: return {1, 2};
        case B::wool: case B::stained_hardened_clay: case B::stained_glass: case 160: case 171: return upTo(16);
        case B::red_flower: return upTo(9);
        case B::double_plant: case 97: return upTo(6);
        case 44: return {0, 1, 3, 4, 5, 6, 7};
        default: return {0};
      }
    };
    for (int id = 1; id < 256; id++) {
      if (!itemInfo(id).exists || isFluid(id)) continue;
      for (int m : variants(id)) {
        ItemStack s(id, 1, m);
        if (isPlaceableItem(s)) v.push_back(s);
      }
    }
    // Objetos (con sus variantes)
    for (int id = 256; id < 512; id++) {
      if (!itemInfo(id).exists) continue;
      int metas = 1;
      if (id == ItemId::dye) metas = 16;
      else if (id == ItemId::coal || id == ItemId::golden_apple || id == ItemId::cooked_fish) metas = 2;
      else if (id == ItemId::fish) metas = 4;
      else if (id == ItemId::skull) metas = 5;
      for (int m = 0; m < metas; m++) v.emplace_back(id, 1, m);
    }
    return v;
  }();
  return items;
}

}  // namespace mcw
