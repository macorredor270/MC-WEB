// Fuego (como en 1.8): arde, se extiende a lo inflamable de alrededor, quema bloques y se apaga solo. También el mechero, la carga
// de fuego y la lava que prende lo que tiene encima.
#include <algorithm>

#include "core/face.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kFire = 51;

}  // namespace

bool GameSession::fireNeighborFlammable(const glm::ivec3& p) const {
  const World& w = access_.world();
  for (const auto& d : kFaceNormals)
    if (fireFlammability(stateId(w.block(p.x + d[0], p.y + d[1], p.z + d[2]))) > 0) return true;
  return false;
}

bool GameSession::fireCanExist(const glm::ivec3& p) const {
  const World& w = access_.world();
  return blockInfo(stateId(w.block(p.x, p.y - 1, p.z))).opaqueCube || fireNeighborFlammable(p);
}

void GameSession::fireIgnite(const glm::ivec3& p, int age) {
  setWorldBlock(p.x, p.y, p.z, makeState(kFire, std::clamp(age, 0, 15)));
  schedule(p, 30 + tickRng_.nextInt(10), TickKind::Fire);
}

/// Prueba si lo que hay en `p` se quema o prende con `chance` (más alto, más difícil).
void GameSession::fireTryBurn(const glm::ivec3& p, int chance, int age) {
  World& w = access_.world();
  const int id = stateId(w.block(p.x, p.y, p.z));
  const int flam = fireFlammability(id);
  if (flam <= 0 || tickRng_.nextInt(chance) >= flam) return;
  if (tickRng_.nextInt(age + 10) < 5) {
    fireIgnite(p, age + tickRng_.nextInt(5) / 4);
  } else {
    setWorldBlock(p.x, p.y, p.z, 0);
  }
  if (id == 46) {  // la dinamita se enciende
    setWorldBlock(p.x, p.y, p.z, 0);
    schedule(p, 80, TickKind::Tnt);
  }
  neighborUpdates(p);
}

void GameSession::fireTick(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  if (stateId(s) != kFire || !rules_.fireTick) return;
  if (!fireCanExist(p)) {
    setWorldBlock(p.x, p.y, p.z, 0);
    return;
  }
  const int belowId = stateId(w.block(p.x, p.y - 1, p.z));
  const bool eternal = belowId == B::netherrack;
  int age = stateMeta(s);
  if (age < 15) {
    age = std::min(15, age + tickRng_.nextInt(3) / 2);
    setWorldBlock(p.x, p.y, p.z, makeState(kFire, age));
  }
  schedule(p, 30 + tickRng_.nextInt(10), TickKind::Fire);
  if (!eternal) {
    if (!fireNeighborFlammable(p)) {
      if (!blockInfo(belowId).opaqueCube || age > 3) setWorldBlock(p.x, p.y, p.z, 0);
      return;
    }
    if (fireFlammability(belowId) <= 0 && age == 15 && tickRng_.nextInt(4) == 0) {
      setWorldBlock(p.x, p.y, p.z, 0);
      return;
    }
  }
  // Quema lo de alrededor
  fireTryBurn(p + glm::ivec3(1, 0, 0), 300, age);
  fireTryBurn(p + glm::ivec3(-1, 0, 0), 300, age);
  fireTryBurn(p + glm::ivec3(0, -1, 0), 250, age);
  fireTryBurn(p + glm::ivec3(0, 1, 0), 250, age);
  fireTryBurn(p + glm::ivec3(0, 0, -1), 300, age);
  fireTryBurn(p + glm::ivec3(0, 0, 1), 300, age);
  // Y salta a huecos de aire que tengan algo que arda cerca (más difícil cuanto más alto)
  for (int dy = -1; dy <= 4; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0 && dz == 0) continue;
        const glm::ivec3 q = p + glm::ivec3(dx, dy, dz);
        if (w.block(q.x, q.y, q.z) != 0) continue;
        int enc = 0;
        for (const auto& d : kFaceNormals) enc = std::max(enc, fireEncouragement(stateId(w.block(q.x + d[0], q.y + d[1], q.z + d[2]))));
        if (enc <= 0) continue;
        const int chance = 100 + (dy > 1 ? (dy - 1) * 100 : 0);
        const int j = (enc + 40 + rules_.difficulty * 7) / (age + 30);
        if (j > 0 && tickRng_.nextInt(chance) <= j) fireIgnite(q, age + tickRng_.nextInt(5) / 4);
      }
}

/// La lava en reposo prende lo que tiene encima (tick aleatorio).
void GameSession::lavaIgnite(const glm::ivec3& p) {
  if (!rules_.fireTick) return;
  World& w = access_.world();
  const int n = tickRng_.nextInt(3);
  if (n > 0) {
    glm::ivec3 q = p;
    for (int i = 0; i < n; i++) {
      q += glm::ivec3(tickRng_.nextInt(3) - 1, 1, tickRng_.nextInt(3) - 1);
      const BlockState s = w.block(q.x, q.y, q.z);
      if (s == 0) {
        if (fireNeighborFlammable(q)) {
          fireIgnite(q, 0);
          return;
        }
      } else if (blockInfo(stateId(s)).fullBox) {
        return;
      }
    }
  } else {
    for (int i = 0; i < 3; i++) {
      const glm::ivec3 q = p + glm::ivec3(tickRng_.nextInt(3) - 1, 0, tickRng_.nextInt(3) - 1);
      if (w.block(q.x, q.y + 1, q.z) == 0 && fireFlammability(stateId(w.block(q.x, q.y, q.z))) > 0) fireIgnite(q + glm::ivec3(0, 1, 0), 0);
    }
  }
}

/// Mechero o carga de fuego sobre un bloque. true si ha encendido algo.
bool GameSession::useFlame(const glm::ivec3& target, int face) {
  World& w = access_.world();
  if (stateId(w.block(target.x, target.y, target.z)) == 46) {  // dinamita: se enciende
    setWorldBlock(target.x, target.y, target.z, 0);
    schedule(target, 80, TickKind::Tnt);
    neighborUpdates(target);
    return true;
  }
  const glm::ivec3 pos = target + glm::ivec3(kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]);
  if (w.block(pos.x, pos.y, pos.z) != 0 || !fireCanExist(pos)) return false;
  fireIgnite(pos, 0);
  return true;
}

}  // namespace mcw
