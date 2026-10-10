// Agua y lava que fluyen (como en 1.8) y los cubos. El nivel está en el metadato: 0 fuente, 1 a 7 cada vez más baja, +8 si cae
// (hay líquido encima). El bloque en reposo es el "estático" (9, 11) y se pone a fluir (8, 10) cuando algo cambia a su lado.
#include <algorithm>

#include "core/face.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

bool isLiquidId(int id) { return id >= B::flowing_water && id <= B::lava; }

/// Nivel del líquido de `like` en una celda (-1 si no es del mismo material: agua o lava).
int levelOf(const World& w, int x, int y, int z, int likeId) {
  const BlockState s = w.block(x, y, z);
  const int id = stateId(s);
  if (!isLiquidId(id) || isWater(id) != isWater(likeId)) return -1;
  return stateMeta(s);
}

/// ¿Corta el paso a un líquido? Puertas, carteles, escalera de mano, caña y todo lo que tiene cuerpo (menos alfombras, nieve fina...).
bool blocksFluid(BlockState s) {
  const int id = stateId(s);
  if (id == 0 || isLiquidId(id)) return false;
  switch (id) {
    case 64: case 71: case 193: case 194: case 195: case 196: case 197:  // puertas
    case 63: case 68: case 65: case B::reeds: return true;
    case 171: case B::snow_layer: case B::waterlily: return false;
    default: break;
  }
  return !collisionBoxes(id, stateMeta(s)).empty();
}

constexpr int kDX[4] = {-1, 1, 0, 0}, kDZ[4] = {0, 0, -1, 1};

}  // namespace

bool GameSession::fluidCanFlowInto(const glm::ivec3& p, bool water) {
  const World& w = access_.world();
  if (p.y < 0 || p.y >= kChunkHeight) return false;
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s);
  // Ni el agua entra en la lava ni un líquido en otro igual (esos se unen por niveles); la lava sí puede caer sobre agua (da piedra)
  if (isLiquidId(id) && (isWater(id) == water || (water && isLava(id)))) return false;
  return !blocksFluid(s);
}

int GameSession::fluidFlowCost(const glm::ivec3& p, int depth, int from, bool water) {
  const World& w = access_.world();
  int cost = 1000;
  for (int d = 0; d < 4; d++) {
    if (d == from) continue;
    const glm::ivec3 n = p + glm::ivec3(kDX[d], 0, kDZ[d]);
    const BlockState ns = w.block(n.x, n.y, n.z);
    if (blocksFluid(ns)) continue;
    const int nid = stateId(ns);
    if (isLiquidId(nid) && isWater(nid) == water && stateMeta(ns) == 0) continue;  // una fuente de lo mismo
    if (!blocksFluid(w.block(n.x, n.y - 1, n.z))) return depth;
    if (depth < 4) cost = std::min(cost, fluidFlowCost(n, depth + 1, d ^ 1, water));
  }
  return cost;
}

void GameSession::fluidTryFlow(const glm::ivec3& p, bool water, int level) {
  if (!fluidCanFlowInto(p, water)) return;
  World& w = access_.world();
  const BlockState old = w.block(p.x, p.y, p.z);
  if (old != 0 && !isLiquidId(stateId(old))) breakBlock(p, false);  // lo que había (hierba, antorchas...) se rompe y suelta
  setWorldBlock(p.x, p.y, p.z, makeState(water ? B::flowing_water : B::flowing_lava, level));
  schedule(p, water ? 5 : 30, TickKind::Fluid);
}

/// La lava que toca agua se vuelve obsidiana (si es fuente) o adoquín. Devuelve true si se ha convertido.
bool GameSession::fluidMix(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  if (!isLava(stateId(s))) return false;
  bool touchesWater = false;
  for (int d = 0; d < 6 && !touchesWater; d++) {
    if (kFaceNormals[d][1] == -1) continue;  // por debajo no cuenta
    touchesWater = isWater(stateId(w.block(p.x + kFaceNormals[d][0], p.y + kFaceNormals[d][1], p.z + kFaceNormals[d][2])));
  }
  if (!touchesWater) return false;
  const int level = stateMeta(s);
  if (level == 0) setWorldBlock(p.x, p.y, p.z, makeState(B::obsidian));
  else if (level <= 4) setWorldBlock(p.x, p.y, p.z, makeState(B::cobblestone));
  else return false;
  events_.push_back({SessionEvent::Type::Fizz, p, s});
  neighborUpdates(p);
  return true;
}

void GameSession::fluidNeighbor(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s);
  if (!isLiquidId(id)) return;
  if (fluidMix(p)) return;
  // En reposo vuelve a fluir por si algo ha cambiado a su lado; uno que ya fluye (recién puesto) también se pone a funcionar
  if (id == B::water || id == B::lava)
    setWorldBlock(p.x, p.y, p.z, makeState(id == B::water ? B::flowing_water : B::flowing_lava, stateMeta(s)));
  schedule(p, isWater(id) ? 5 : 30, TickKind::Fluid);
}

void GameSession::fluidTick(const glm::ivec3& p) {
  World& w = access_.world();
  BlockState s = w.block(p.x, p.y, p.z);
  int id = stateId(s);
  if (!isLiquidId(id)) return;
  const bool water = isWater(id);
  const int step = water ? 1 : 2;  // la lava (de la superficie) pierde 2 niveles por bloque
  const int tickDelay = water ? 5 : 30;
  int delay = tickDelay;
  const int level = stateMeta(s);
  const auto setStatic = [&](int lv) { setWorldBlock(p.x, p.y, p.z, makeState(water ? B::water : B::lava, lv)); };

  if (level > 0) {
    int lowest = -100, sources = 0;
    for (int d = 0; d < 4; d++) {
      int lv = levelOf(w, p.x + kDX[d], p.y, p.z + kDZ[d], id);
      if (lv < 0) continue;
      if (lv == 0) sources++;
      if (lv >= 8) lv = 0;
      if (lowest < 0 || lv < lowest) lowest = lv;
    }
    int next = lowest + step;
    if (next >= 8 || lowest < 0) next = -1;
    const int above = levelOf(w, p.x, p.y + 1, p.z, id);
    if (above >= 0) next = above >= 8 ? above : above + 8;
    // Dos fuentes al lado y suelo (o una fuente) debajo: el agua se hace fuente (agua infinita)
    if (sources >= 2 && water) {
      const BlockState below = w.block(p.x, p.y - 1, p.z);
      if (blocksFluid(below) || (isWater(stateId(below)) && stateMeta(below) == 0)) next = 0;
    }
    if (!water && level < 8 && next < 8 && next > level && tickRng_.nextInt(4) != 0) delay *= 4;  // la lava se arrastra
    if (next == level) {
      setStatic(level);
    } else if (next < 0) {
      setWorldBlock(p.x, p.y, p.z, 0);
      neighborUpdates(p);
    } else {
      setWorldBlock(p.x, p.y, p.z, makeState(id, next));
      schedule(p, delay, TickKind::Fluid);
      neighborUpdates(p);
    }
  } else {
    setStatic(0);
  }

  s = w.block(p.x, p.y, p.z);
  id = stateId(s);
  if (!isLiquidId(id)) return;
  const int lv = stateMeta(s);
  const glm::ivec3 down = p + glm::ivec3(0, -1, 0);
  if (fluidCanFlowInto(down, water)) {
    if (!water && isWater(stateId(w.block(down.x, down.y, down.z)))) {  // la lava que cae sobre agua la deja en piedra
      setWorldBlock(down.x, down.y, down.z, makeState(B::stone));
      events_.push_back({SessionEvent::Type::Fizz, down, 0});
      neighborUpdates(down);
      return;
    }
    fluidTryFlow(down, water, lv >= 8 ? lv : lv + 8);
  } else if (lv == 0 || blocksFluid(w.block(down.x, down.y, down.z))) {
    // De lado: hacia donde antes haya un desnivel (o por todos los lados si no hay ninguno cerca)
    int cost[4] = {1000, 1000, 1000, 1000};
    for (int d = 0; d < 4; d++) {
      const glm::ivec3 n = p + glm::ivec3(kDX[d], 0, kDZ[d]);
      const BlockState ns = w.block(n.x, n.y, n.z);
      if (blocksFluid(ns)) continue;
      const int nid = stateId(ns);
      if (isLiquidId(nid) && isWater(nid) == water && stateMeta(ns) == 0) continue;
      cost[d] = blocksFluid(w.block(n.x, n.y - 1, n.z)) ? fluidFlowCost(n, 1, d ^ 1, water) : 0;
    }
    const int best = *std::min_element(cost, cost + 4);
    int next = lv + step;
    if (lv >= 8) next = 1;
    if (next >= 8) return;
    for (int d = 0; d < 4; d++)
      if (cost[d] == best) fluidTryFlow(p + glm::ivec3(kDX[d], 0, kDZ[d]), water, next);
  }
}

namespace {
bool isBucket(int id) { return id == ItemId::bucket || id == ItemId::water_bucket || id == ItemId::lava_bucket; }
}  // namespace

bool GameSession::useBucket() {
  ItemStack& held = player_.inventory.selected();
  if (!isBucket(held.id)) return false;
  World& w = access_.world();
  const glm::dvec3 dir = glm::dvec3(-std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), -std::cos(player_.yaw) * std::cos(player_.pitch));
  const auto hit = raycastBlocks(w, player_.eyePos(), dir, reach(), true);
  if (!hit) return false;
  const glm::ivec3 at = hit->block;
  const BlockState target = w.block(at.x, at.y, at.z);
  const int tid = stateId(target);
  if (held.id == ItemId::bucket) {
    // Vacío: recoge una fuente de agua o de lava
    if (!isLiquidId(tid) || stateMeta(target) != 0) return false;
    setWorldBlock(at.x, at.y, at.z, 0);
    neighborUpdates(at);
    const int full = isWater(tid) ? ItemId::water_bucket : ItemId::lava_bucket;
    if (!player_.creative()) {
      if (held.count <= 1) {
        held = ItemStack(full);
      } else {
        held.count--;
        const ItemStack rest = player_.inventory.add(ItemStack(full));
        if (!rest.empty()) throwItem(rest);
      }
    }
    events_.push_back({SessionEvent::Type::BucketFilled, at, target});
    return true;
  }
  // Lleno: lo vuelca en el hueco que hay delante de la cara apuntada (o en el propio bloque si se puede sustituir)
  const bool water = held.id == ItemId::water_bucket;
  const glm::ivec3 pos = at + glm::ivec3(kFaceNormals[hit->face][0], kFaceNormals[hit->face][1], kFaceNormals[hit->face][2]);
  if (pos.y < 0 || pos.y >= kChunkHeight) return false;
  const BlockState there = w.block(pos.x, pos.y, pos.z);
  if (blocksFluid(there)) return false;  // ahí hay algo sólido
  if (there != 0 && !isLiquidId(stateId(there))) breakBlock(pos, false);  // una planta o antorcha se rompe y suelta
  setWorldBlock(pos.x, pos.y, pos.z, makeState(water ? B::flowing_water : B::flowing_lava, 0));
  schedule(pos, water ? 5 : 30, TickKind::Fluid);
  neighborUpdates(pos);
  events_.push_back({SessionEvent::Type::BucketEmptied, pos, makeState(water ? B::water : B::lava)});
  if (!player_.creative()) held = ItemStack(ItemId::bucket);
  return true;
}

}  // namespace mcw
