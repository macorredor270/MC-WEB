#include "game/rules.h"

#include <cmath>

#include "data/blocks.h"
#include "data/blockstates.h"
#include "world/world.h"

namespace mcw {
namespace {

bool isOpaqueAt(const World& w, int x, int y, int z) { return blockInfo(stateId(w.block(x, y, z))).opaqueCube; }

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
    default: break;
  }

  for (const DropEntry& d : dropsOf(id)) {
    int count;
    if (d.maxCount > d.minCount) count = static_cast<int>(d.minCount) + rng.nextInt(static_cast<int>(d.maxCount - d.minCount) + 1);
    else count = static_cast<int>(std::round(d.minCount));
    if (count <= 0) continue;
    const int dropMeta = d.meta >= 0 ? d.meta : (d.id == id ? droppedMeta(id, meta) : 0);
    out.emplace_back(d.id, count, dropMeta);
  }
  return out;
}

bool isReplaceable(BlockState s) {
  const int id = stateId(s);
  return id == B::air || id == B::tallgrass || id == B::deadbush || isFluid(id) || id == B::vine || id == 51 /* fire */ ||
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
    case B::torch:
      switch (meta) {
        case 1: return isOpaqueAt(w, x - 1, y, z);
        case 2: return isOpaqueAt(w, x + 1, y, z);
        case 3: return isOpaqueAt(w, x, y, z - 1);
        case 4: return isOpaqueAt(w, x, y, z + 1);
        default: return isOpaqueAt(w, x, y - 1, z);
      }
    case B::snow_layer: return blockInfo(below).opaqueCube || below == B::leaves || below == B::leaves2;
    case B::waterlily: return isWater(below);
    default: return true;
  }
}

bool isPlaceableItem(const ItemStack& s) {
  if (s.empty() || !isBlockItem(s.id)) return false;
  if (s.id == B::double_plant) return s.meta < 6;
  if (s.id == B::torch) return true;
  return blockstateOf(makeState(s.id, s.id == B::log || s.id == B::log2 ? s.meta & 3 : s.meta)).has_value();
}

std::optional<BlockState> placementFor(const World& w, const ItemStack& held, const RayHit& hit, float yaw, glm::ivec3& pos) {
  if (!isPlaceableItem(held)) return std::nullopt;
  const BlockState target = w.block(hit.block.x, hit.block.y, hit.block.z);
  pos = hit.block;
  int face = hit.face;
  if (!isReplaceable(target)) {
    pos += glm::ivec3(kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]);
  } else {
    face = Face::Up;  // se sustituye el propio bloque (p. ej. hierba alta): como apoyar en el de abajo
  }
  if (pos.y < 0 || pos.y >= kChunkHeight) return std::nullopt;
  if (!isReplaceable(w.block(pos.x, pos.y, pos.z))) return std::nullopt;

  const int id = held.id;
  int meta = held.meta;
  switch (id) {
    case B::log: case B::log2: {
      const int axis = (face == Face::Up || face == Face::Down) ? 0 : (face == Face::East || face == Face::West ? 4 : 8);
      meta = (held.meta & 3) | axis;
      break;
    }
    case B::torch: {
      static const int kTorchMeta[6] = {-1, 5, 4, 3, 2, 1};  // por cara golpeada: abajo no vale
      meta = kTorchMeta[face];
      if (meta < 0) return std::nullopt;
      break;
    }
    case B::furnace: case B::lit_furnace: {  // horno: mira hacia el jugador
      const double fx = -std::sin(yaw), fz = -std::cos(yaw);
      if (std::abs(fx) > std::abs(fz)) meta = fx > 0 ? 4 : 5;  // mira al oeste / este
      else meta = fz > 0 ? 2 : 3;                              // mira al norte / sur
      break;
    }
    case B::leaves: case B::leaves2: meta = held.meta & 3; break;
    case B::double_plant:
      if (pos.y + 1 >= kChunkHeight || !isReplaceable(w.block(pos.x, pos.y + 1, pos.z))) return std::nullopt;
      meta = held.meta & 7;
      break;
    default: break;
  }
  const BlockState state = makeState(id, meta);
  if (id != B::double_plant && !canStay(w, pos.x, pos.y, pos.z, state)) return std::nullopt;
  if (id == B::double_plant) {
    const int below = stateId(w.block(pos.x, pos.y - 1, pos.z));
    if (!(below == B::grass || below == B::dirt)) return std::nullopt;
  }
  return state;
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
    // Bloques: cada variante de ítem que sepamos dibujar
    for (int id = 1; id < 256; id++) {
      if (isFluid(id)) continue;
      int metas = 1;
      switch (id) {
        case B::stone: metas = 7; break;
        case B::dirt: metas = 3; break;
        case B::planks: case B::sapling: metas = 6; break;
        case B::sand: case B::quartz_block: case B::log2: case B::leaves2: metas = 2; break;
        case B::log: case B::leaves: case B::stonebrick: metas = 4; break;
        case B::sandstone: metas = 3; break;
        case B::tallgrass: metas = 3; break;
        case B::wool: case B::stained_hardened_clay: case B::stained_glass: metas = 16; break;
        case B::red_flower: metas = 9; break;
        case B::double_plant: metas = 6; break;
        case B::sponge: metas = 2; break;
        default: break;
      }
      for (int m = 0; m < metas; m++) {
        ItemStack s(id, 1, m);
        if (id == B::tallgrass && m == 0) continue;  // arbusto muerto: está como bloque propio
        if (isPlaceableItem(s)) v.push_back(s);
      }
    }
    // Herramientas, materiales y comida
    const int tools[] = {ItemId::wooden_sword, ItemId::wooden_shovel, ItemId::wooden_pickaxe, ItemId::wooden_axe,
                         ItemId::stone_sword, ItemId::stone_shovel, ItemId::stone_pickaxe, ItemId::stone_axe,
                         ItemId::iron_sword, ItemId::iron_shovel, ItemId::iron_pickaxe, ItemId::iron_axe,
                         ItemId::golden_sword, ItemId::golden_shovel, ItemId::golden_pickaxe, ItemId::golden_axe,
                         ItemId::diamond_sword, ItemId::diamond_shovel, ItemId::diamond_pickaxe, ItemId::diamond_axe,
                         ItemId::shears, ItemId::stick, ItemId::coal, ItemId::iron_ingot, ItemId::gold_ingot,
                         ItemId::diamond, ItemId::emerald, ItemId::redstone, ItemId::flint, ItemId::clay_ball,
                         ItemId::brick, ItemId::snowball, ItemId::string, ItemId::reeds, ItemId::wheat_seeds,
                         ItemId::apple, ItemId::bread, ItemId::golden_apple, ItemId::cooked_porkchop, ItemId::cooked_beef};
    for (int t : tools) v.emplace_back(t, 1, 0);
    v.emplace_back(ItemId::coal, 1, 1);
    v.emplace_back(ItemId::dye, 1, 4);
    return v;
  }();
  return items;
}

}  // namespace mcw
