#include "data/blocks.h"

#include <array>

#include <string>
#include <unordered_map>

namespace mcw {
namespace {

struct RawBlock {
  int id;
  const char* name;
  const char* displayName;
  float hardness, resistance;
  int stackSize;
  bool diggable, fullBox, transparent;
  int emitLight, filterLight;
  const char* material;
};

constexpr RawBlock kRaw[] = {
#include "data/generated/blocks.inc"
};

struct Registry {
  std::array<BlockInfo, 256> byId{};
  std::unordered_map<std::string, int> byName;

  Registry() {
    for (const RawBlock& r : kRaw) {
      BlockInfo& b = byId[r.id];
      b.id = r.id;
      b.name = r.name;
      b.displayName = r.displayName;
      b.hardness = r.hardness;
      b.resistance = r.resistance;
      b.stackSize = r.stackSize;
      b.diggable = r.diggable;
      b.fullBox = r.fullBox;
      b.transparent = r.transparent;
      b.emitLight = r.emitLight;
      b.opacity = r.filterLight;
      b.material = r.material;
      b.exists = true;
      byName.emplace(r.name, r.id);
    }
    applyOverrides();
  }

  void set(int id, auto&& fn) { fn(byId[id]); }

  void applyOverrides() {
    for (BlockInfo& b : byId) {
      if (!b.exists) continue;
      // Sin caja de colisión (plantas, antorchas, raíles...): no bloquean la luz.
      if (!b.fullBox && !isFluid(b.id)) { b.opacity = 0; b.transparent = true; }
      b.opaqueCube = b.fullBox && !b.transparent;
    }
    // Opacidad según minecraft.wiki ("Opacity"): el agua y el hielo restan 3, hojas y telaraña 1.
    for (int id : {B::flowing_water, B::water, B::ice}) byId[id].opacity = 3;
    for (int id : {B::leaves, B::leaves2, B::web}) byId[id].opacity = 1;
    for (int id : {B::flowing_lava, B::lava}) byId[id].opacity = 15;

    // La cabeza del pistón no es un cubo (minecraft-data la marca como bloque entero)
    byId[34].opaqueCube = false;
    byId[34].transparent = true;
    byId[34].opacity = 0;
    for (int id : {B::flowing_water, B::water, B::flowing_lava, B::lava}) {
      byId[id].fluid = true;
      byId[id].opaqueCube = false;
    }
    // Capas de render de 1.8
    for (int id : {B::leaves, B::leaves2}) byId[id].layer = RenderLayer::CutoutMipped;
    for (int id : {B::glass, B::sapling, B::web, B::tallgrass, B::deadbush, B::yellow_flower, B::red_flower,
                   B::brown_mushroom, B::red_mushroom, B::torch, B::reeds, B::cactus, B::vine, B::waterlily,
                   B::double_plant})
      byId[id].layer = RenderLayer::Cutout;
    for (int id : {B::flowing_water, B::water, B::ice, B::stained_glass, B::stained_glass_pane, B::slime})
      byId[id].layer = RenderLayer::Translucent;
    for (int id : {B::glass, B::ice, B::stained_glass}) byId[id].selfCull = true;

    // Tintes de bioma (colores fijos según minecraft.wiki, "Color")
    for (int id : {B::grass, B::tallgrass, B::double_plant, B::reeds}) byId[id].tint = TintType::Grass;
    for (int id : {B::leaves, B::leaves2, B::vine}) byId[id].tint = TintType::Foliage;
    byId[B::waterlily].tint = TintType::Constant;
    byId[B::waterlily].tintColor = 0x208030;
  }
};

const Registry& registry() {
  static const Registry r;
  return r;
}

}  // namespace

namespace detail {

const BlockInfo& blockInfoSlow(int id) {
  const auto& r = registry();
  if (id < 0 || id >= 256 || !r.byId[id].exists) return r.byId[0];
  return r.byId[id];
}

namespace {
const BlockInfo* const* buildTable() {
  static std::array<const BlockInfo*, 256> t{};
  for (int i = 0; i < 256; i++) t[i] = &blockInfoSlow(i);
  return t.data();
}
}  // namespace

const BlockInfo* const* blockTable = buildTable();

}  // namespace detail

int blockIdByName(std::string_view name) {
  const auto& r = registry();
  auto it = r.byName.find(std::string(name));
  return it == r.byName.end() ? -1 : it->second;
}

}  // namespace mcw

namespace mcw {
TintType tintTypeOf(BlockState s) {
  const int id = stateId(s);
  if (id == B::leaves) {
    const int type = stateMeta(s) & 3;
    if (type == 1) return TintType::Spruce;
    if (type == 2) return TintType::Birch;
  }
  return blockInfo(id).tint;
}
}  // namespace mcw
