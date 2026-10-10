// Lo que crece, se extiende o se deshace con el tiempo: brotes, hojas, hierba, setas, hielo y nieve.
#include <algorithm>
#include <deque>

#include "data/blockstates.h"
#include "game/mob.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/tree_grow.h"
#include "world/world.h"

namespace mcw {
namespace {

bool isLeafId(int id) { return id == B::leaves || id == B::leaves2; }
bool isLogId(int id) { return id == B::log || id == B::log2; }

}  // namespace

int GameSession::lightAtTick(int x, int y, int z) const {
  const World& w = access_.world();
  return effectiveLight(w.skyLight(x, y, z), w.blockLight(x, y, z), skyDarkness(worldTime_));
}

/// ¿Está esta hoja a 4 hojas o menos de un tronco? (si no, se seca)
bool GameSession::leafConnected(const glm::ivec3& p) const {
  const World& w = access_.world();
  struct Node {
    glm::ivec3 pos;
    int depth;
  };
  std::deque<Node> queue{{p, 1}};
  std::vector<glm::ivec3> seen{p};
  while (!queue.empty()) {
    const Node n = queue.front();
    queue.pop_front();
    for (const auto& d : kFaceNormals) {
      const glm::ivec3 q = n.pos + glm::ivec3(d[0], d[1], d[2]);
      const int id = stateId(w.block(q.x, q.y, q.z));
      if (isLogId(id)) return true;
      if (isLeafId(id) && n.depth < 4 && std::find(seen.begin(), seen.end(), q) == seen.end()) {
        seen.push_back(q);
        queue.push_back({q, n.depth + 1});
      }
    }
  }
  return false;
}

/// Al romper un tronco o una hoja, las hojas de alrededor (en 3x3x3) pasan a comprobar si se secan.
void GameSession::flagLeavesAround(const glm::ivec3& p) {
  World& w = access_.world();
  for (int dy = -1; dy <= 1; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        const BlockState s = w.block(p.x + dx, p.y + dy, p.z + dz);
        if (isLeafId(stateId(s)) && !(stateMeta(s) & 12)) setWorldBlock(p.x + dx, p.y + dy, p.z + dz, makeState(stateId(s), stateMeta(s) | 8));
      }
}

/// Hace crecer el brote de (x, y, z). `force`: con harina de hueso ya no espera a la fase 2. Devuelve true si ha cambiado algo.
bool GameSession::growSapling(const glm::ivec3& p, bool force) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  if (stateId(s) != B::sapling) return false;
  if (!force && !(stateMeta(s) & 8)) {
    setWorldBlock(p.x, p.y, p.z, makeState(B::sapling, stateMeta(s) | 8));  // la primera vez solo pasa de fase
    return true;
  }
  const int type = stateMeta(s) & 7;
  // Los de jungla y roble oscuro buscan otros tres brotes iguales formando un cuadrado de 2x2 (los de roble oscuro lo necesitan)
  glm::ivec3 corner = p;
  bool big = false;
  if (type == 3 || type == 5) {
    for (int ox = -1; ox <= 0 && !big; ox++)
      for (int oz = -1; oz <= 0 && !big; oz++) {
        bool all = true;
        for (int dx = 0; dx <= 1; dx++)
          for (int dz = 0; dz <= 1; dz++) {
            const BlockState o = w.block(p.x + ox + dx, p.y, p.z + oz + dz);
            all &= stateId(o) == B::sapling && (stateMeta(o) & 7) == type;
          }
        if (all) {
          big = true;
          corner = {p.x + ox, p.y, p.z + oz};
        }
      }
    if (type == 5 && !big) return false;
  }
  // El suelo tiene que ser tierra o hierba
  auto soil = [&](int x, int z) {
    const int below = stateId(w.block(x, p.y - 1, z));
    return below == B::grass || below == B::dirt || below == 60;
  };
  if (!soil(corner.x, corner.z)) return false;
  const int sx = big ? 2 : 1;
  std::vector<std::pair<glm::ivec3, BlockState>> saplings;
  for (int dx = 0; dx < sx; dx++)
    for (int dz = 0; dz < sx; dz++) saplings.push_back({{corner.x + dx, p.y, corner.z + dz}, w.block(corner.x + dx, p.y, corner.z + dz)});
  for (const auto& [pos, st] : saplings) setWorldBlock(pos.x, pos.y, pos.z, 0);
  auto get = [&](int x, int y, int z) { return w.block(x, y, z); };
  auto put = [&](int x, int y, int z, BlockState st, bool) { setWorldBlock(x, y, z, st); };
  if (!growTree(get, put, type, corner.x, p.y, corner.z, tickRng_, big)) {
    for (const auto& [pos, st] : saplings) setWorldBlock(pos.x, pos.y, pos.z, st);  // no cabe: se queda como estaba
    return false;
  }
  // Bajo el tronco la hierba se vuelve tierra
  for (int dx = 0; dx < sx; dx++)
    for (int dz = 0; dz < sx; dz++)
      if (stateId(w.block(corner.x + dx, p.y - 1, corner.z + dz)) == B::grass) setWorldBlock(corner.x + dx, p.y - 1, corner.z + dz, makeState(B::dirt));
  for (int dx = -2; dx <= sx + 1; dx++)
    for (int dz = -2; dz <= sx + 1; dz++) neighborUpdates({corner.x + dx, p.y, corner.z + dz});
  return true;
}

/// Harina de hueso sobre hierba: salen hierba alta y flores alrededor.
void GameSession::growGrassPatch(const glm::ivec3& p) {
  World& w = access_.world();
  const glm::ivec3 above = p + glm::ivec3(0, 1, 0);
  for (int i = 0; i < 128; i++) {
    glm::ivec3 q = above;
    bool ok = true;
    for (int j = 0; j < i / 16 && ok; j++) {
      q += glm::ivec3(tickRng_.nextInt(3) - 1, (tickRng_.nextInt(3) - 1) * tickRng_.nextInt(3) / 2, tickRng_.nextInt(3) - 1);
      ok = stateId(w.block(q.x, q.y - 1, q.z)) == B::grass && !blockInfo(stateId(w.block(q.x, q.y, q.z))).opaqueCube;
    }
    if (!ok || w.block(q.x, q.y, q.z) != 0) continue;
    if (tickRng_.nextInt(8) == 0) setWorldBlock(q.x, q.y, q.z, tickRng_.nextInt(2) ? makeState(B::yellow_flower) : makeState(B::red_flower, tickRng_.nextInt(9)));
    else setWorldBlock(q.x, q.y, q.z, makeState(B::tallgrass, 1));
  }
}

void GameSession::growthTickAt(const glm::ivec3& p) { growthTick(p.x, p.y, p.z, access_.world().block(p.x, p.y, p.z)); }

void GameSession::growthTick(int x, int y, int z, BlockState s) {
  World& w = access_.world();
  const int id = stateId(s), meta = stateMeta(s);
  switch (id) {
    case B::lava: lavaIgnite({x, y, z}); break;
    case B::sapling:
      if (lightAtTick(x, y + 1, z) >= 9 && tickRng_.nextInt(7) == 0) growSapling({x, y, z}, false);
      break;
    case B::leaves: case B::leaves2:
      if ((meta & 8) && !(meta & 4)) {
        // Hojas con la marca de comprobar: se secan si ya no hay un tronco a 4 hojas
        for (int dz = -5; dz <= 5; dz += 5)
          for (int dx = -5; dx <= 5; dx += 5)
            if (!w.chunk((x + dx) >> 4, (z + dz) >> 4)) return;
        if (leafConnected({x, y, z})) {
          setWorldBlock(x, y, z, makeState(id, meta & ~8));
        } else {
          const std::vector<ItemStack> dropped = blockDrops(s, ItemStack(), tickRng_);
          for (const ItemStack& d : dropped)
            spawnItem(glm::dvec3(x + 0.5, y + 0.5, z + 0.5), d, {tickRng_.nextFloat() * 0.2 - 0.1, 0.2, tickRng_.nextFloat() * 0.2 - 0.1}, 10);
          setWorldBlock(x, y, z, 0);
          events_.push_back({SessionEvent::Type::BlockBroken, {x, y, z}, s});
          neighborUpdates({x, y, z});
          flagLeavesAround({x, y, z});
        }
      }
      break;
    case B::grass: case B::mycelium: {
      // Sin luz (algo opaco encima) se vuelve tierra; con luz se extiende a la tierra de alrededor
      const int upId = stateId(w.block(x, y + 1, z));
      if (lightAtTick(x, y + 1, z) < 4 && blockInfo(upId).opaqueCube) {
        setWorldBlock(x, y, z, makeState(B::dirt));
      } else if (lightAtTick(x, y + 1, z) >= 9) {
        for (int i = 0; i < 4; i++) {
          const int nx = x + tickRng_.nextInt(3) - 1, ny = y + tickRng_.nextInt(5) - 3, nz = z + tickRng_.nextInt(3) - 1;
          const BlockState n = w.block(nx, ny, nz);
          if (stateId(n) == B::dirt && stateMeta(n) == 0 && lightAtTick(nx, ny + 1, nz) >= 4 &&
              !blockInfo(stateId(w.block(nx, ny + 1, nz))).opaqueCube && !isFluid(stateId(w.block(nx, ny + 1, nz))))
            setWorldBlock(nx, ny, nz, makeState(id));
        }
      }
      break;
    }
    case B::brown_mushroom: case B::red_mushroom: {
      if (tickRng_.nextInt(25) != 0) break;
      int count = 5;
      for (int dx = -4; dx <= 4 && count > 0; dx++)
        for (int dz = -4; dz <= 4 && count > 0; dz++)
          for (int dy = -1; dy <= 1; dy++)
            if (stateId(w.block(x + dx, y + dy, z + dz)) == id && --count <= 0) break;
      if (count <= 0) break;
      glm::ivec3 q(x + tickRng_.nextInt(3) - 1, y + tickRng_.nextInt(2) - tickRng_.nextInt(2), z + tickRng_.nextInt(3) - 1);
      for (int i = 0; i < 4; i++) {
        const glm::ivec3 cand = q + glm::ivec3(tickRng_.nextInt(3) - 1, tickRng_.nextInt(2) - tickRng_.nextInt(2), tickRng_.nextInt(3) - 1);
        if (w.block(cand.x, cand.y, cand.z) == 0 && canStay(w, cand.x, cand.y, cand.z, s)) q = cand;
      }
      if (w.block(q.x, q.y, q.z) == 0 && canStay(w, q.x, q.y, q.z, s)) setWorldBlock(q.x, q.y, q.z, s);
      break;
    }
    case B::ice: case B::snow: case B::snow_layer:
      // Se derrite con luz de bloque (antorchas, lava...): el hielo da agua
      if (w.blockLight(x, y, z) > (id == B::ice ? 8 : 11)) {
        if (id == B::ice) {
          setWorldBlock(x, y, z, makeState(B::water));
        } else {
          setWorldBlock(x, y, z, 0);
        }
        neighborUpdates({x, y, z});
      }
      break;
    default: break;
  }
}

}  // namespace mcw
