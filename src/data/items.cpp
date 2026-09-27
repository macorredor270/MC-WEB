#include "data/items.h"

#include <array>
#include <map>
#include <unordered_map>
#include <vector>

#include "data/blocks.h"

namespace mcw {
namespace {

struct RawItem {
  int id;
  const char* name;
  const char* displayName;
  int stackSize, maxDurability;
};
constexpr RawItem kItems[] = {
#include "data/generated/items.inc"
};

struct RawVariation {
  int id, meta;
  const char* displayName;
};
constexpr RawVariation kVariations[] = {
#include "data/generated/variations.inc"
};

struct RawHarvest {
  int block, tool;
};
constexpr RawHarvest kHarvest[] = {
#include "data/generated/harvest_tools.inc"
};

constexpr DropEntry kDrops[] = {
#include "data/generated/drops.inc"
};

struct RawToolSpeed {
  const char* material;
  int tool;
  float speed;
};
constexpr RawToolSpeed kToolSpeeds[] = {
#include "data/generated/tool_speeds.inc"
};

const ShapedRecipe kShaped[] = {
#include "data/generated/recipes_shaped.inc"
};
const ShapelessRecipe kShapeless[] = {
#include "data/generated/recipes_shapeless.inc"
};

constexpr Box kBoxes[] = {
#include "data/generated/collision_boxes.inc"
};
struct RawShape {
  int id, start, count;
};
constexpr RawShape kShapes[] = {
#include "data/generated/collision_shapes.inc"
};
struct RawCollisionBlock {
  int block;
  int shapes[16];
};
constexpr RawCollisionBlock kCollisionBlocks[] = {
#include "data/generated/collision_blocks.inc"
};

constexpr Box kFullCube{0, 0, 0, 1, 1, 1};

struct Registry {
  std::array<ItemInfo, 512> items{};
  std::unordered_map<std::string_view, int> byName;
  std::map<std::pair<int, int>, std::string_view> variations;
  std::array<std::vector<int>, 256> harvestTools;
  std::array<std::vector<DropEntry>, 256> drops;
  std::map<std::pair<std::string_view, int>, float> speeds;
  std::array<std::array<std::span<const Box>, 16>, 256> collision{};
  std::array<bool, 256> hasCollisionData{};

  Registry() {
    for (const RawItem& r : kItems) {
      if (r.id < 0 || r.id >= 512) continue;
      items[r.id] = ItemInfo{r.id, r.name, r.displayName, r.stackSize, r.maxDurability, true};
      byName.emplace(r.name, r.id);
    }
    for (const RawVariation& v : kVariations) variations.emplace(std::pair{v.id, v.meta}, v.displayName);
    for (const RawHarvest& h : kHarvest) harvestTools[h.block].push_back(h.tool);
    for (const DropEntry& d : kDrops) drops[d.block].push_back(d);
    for (const RawToolSpeed& t : kToolSpeeds) speeds[{t.material, t.tool}] = t.speed;

    std::map<int, std::span<const Box>> shapes;
    for (const RawShape& s : kShapes) shapes[s.id] = std::span<const Box>(kBoxes + s.start, static_cast<std::size_t>(s.count));
    for (const RawCollisionBlock& b : kCollisionBlocks) {
      if (b.block < 0 || b.block >= 256) continue;
      hasCollisionData[b.block] = true;
      for (int m = 0; m < 16; m++) collision[b.block][m] = shapes[b.shapes[m]];
    }
  }
};

const Registry& reg() {
  static const Registry r;
  return r;
}

}  // namespace

const ItemInfo& itemInfo(int id) {
  static const ItemInfo none{};
  if (id < 0 || id >= 512 || !reg().items[id].exists) return none;
  return reg().items[id];
}

int itemIdByName(std::string_view name) {
  auto it = reg().byName.find(name);
  return it == reg().byName.end() ? -1 : it->second;
}

std::string_view itemDisplayName(int id, int meta) {
  auto it = reg().variations.find({id, meta});
  if (it != reg().variations.end()) return it->second;
  return itemInfo(id).displayName;
}

float toolSpeed(int blockId, int toolId) {
  if (toolId <= 0) return 1.0f;
  const std::string_view mat = blockInfo(blockId).material;
  auto it = reg().speeds.find({mat, toolId});
  return it == reg().speeds.end() ? 1.0f : it->second;
}

bool canHarvest(int blockId, int toolId) {
  if (blockId < 0 || blockId >= 256) return false;
  const auto& tools = reg().harvestTools[blockId];
  if (tools.empty()) return true;
  for (int t : tools)
    if (t == toolId) return true;
  return false;
}

std::span<const DropEntry> dropsOf(int blockId) {
  if (blockId < 0 || blockId >= 256) return {};
  return reg().drops[blockId];
}

std::span<const ShapedRecipe> shapedRecipes() { return kShaped; }
std::span<const ShapelessRecipe> shapelessRecipes() { return kShapeless; }

std::span<const Box> collisionBoxes(int blockId, int meta) {
  if (blockId <= 0 || blockId >= 256) return {};
  const Registry& r = reg();
  if (r.hasCollisionData[blockId]) return r.collision[blockId][meta & 15];
  if (blockInfo(blockId).fullBox) return std::span<const Box>(&kFullCube, 1);
  return {};
}

}  // namespace mcw
