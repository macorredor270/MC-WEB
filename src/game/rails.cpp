#include "game/rails.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "data/blockstates.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw::rails {
namespace {

int railIdAt(const World& w, const glm::ivec3& p) {
  if (p.y < 0 || p.y >= 256) return -1;
  const int id = stateId(w.block(p.x, p.y, p.z));
  return isRail(id) ? id : -1;
}

int railShapeAt(const World& w, const glm::ivec3& p) {
  const BlockState s = w.block(p.x, p.y, p.z);
  return shapeOf(stateId(s), stateMeta(s));
}

/// La celda a la que da cada extremo de un raíl con esa forma (en una cuesta, el extremo alto da a una celda más arriba).
glm::ivec3 endCell(const glm::ivec3& pos, int shape, int which) {
  const Ends& e = endsOf(shape);
  const glm::ivec3& v = which == 0 ? e.a : e.b;
  return pos + glm::ivec3(v.x, v.y + (ascending(shape) ? 1 : 0), v.z);
}

/// ¿El raíl `n` tiene un extremo que da a la celda `to`?
bool pointsTo(const World& w, const glm::ivec3& n, const glm::ivec3& to) {
  const int shape = railShapeAt(w, n);
  return endCell(n, shape, 0) == to || endCell(n, shape, 1) == to;
}

/// El raíl que continúa por la celda del extremo `cell` de `from` (en la misma altura, o una más arriba o abajo): su posición, o nada.
bool neighborAcross(const World& w, const glm::ivec3& from, const glm::ivec3& cell, glm::ivec3& out) {
  for (int dy : {0, 1, -1}) {
    const glm::ivec3 c = cell + glm::ivec3(0, dy, 0);
    if (railIdAt(w, c) >= 0 && c != from) {
      out = c;
      return true;
    }
  }
  return false;
}

/// Cuántos de los dos extremos de `n` están de verdad unidos a otro raíl (que a su vez da a `n`).
int realConnections(const World& w, const glm::ivec3& n) {
  const int shape = railShapeAt(w, n);
  int count = 0;
  for (int which = 0; which < 2; which++) {
    glm::ivec3 other;
    if (neighborAcross(w, n, endCell(n, shape, which), other) && pointsTo(w, other, n)) count++;
  }
  return count;
}

/// ¿Se deja unir `n` al raíl de `us` (que está justo al lado)?
bool accepts(const World& w, const glm::ivec3& n, const glm::ivec3& us) {
  if (pointsTo(w, n, us)) return true;
  const int id = railIdAt(w, n);
  const int links = realConnections(w, n);
  if (straightOnly(id)) return links == 0;  // los especiales solo giran si están solos
  return links < 2;
}

struct Candidate {
  int dir = 0;
  glm::ivec3 pos{0};
  bool pointing = false;
};

/// 0 si el raíl corre de norte a sur (también en cuesta), 1 de este a oeste.
int axisOf(int shape) {
  switch (shape) {
    case 0: case 4: case 5: return 0;
    default: return 1;
  }
}

/// Sigue la fila de raíles encendidos por el extremo `end` de `pos` hasta encontrar uno con potencia propia (a lo sumo 8 más allá).
bool walkChain(const World& w, const glm::ivec3& pos, int id, int end, int distance, const PowerFn& direct) {
  if (distance >= 8) return false;
  const int shape = railShapeAt(w, pos);
  glm::ivec3 next;
  if (!neighborAcross(w, pos, endCell(pos, shape, end), next)) return false;
  const BlockState s = w.block(next.x, next.y, next.z);
  if (stateId(s) != id) return false;
  const int nshape = shapeOf(id, stateMeta(s));
  if (axisOf(nshape) != axisOf(shape) || !active(id, stateMeta(s))) return false;
  if (direct(next)) return true;
  // Sigue por el extremo de `next` que no da a donde venimos
  const glm::ivec3 d0 = endCell(next, nshape, 0) - pos, d1 = endCell(next, nshape, 1) - pos;
  const int far = d0.x * d0.x + d0.y * d0.y + d0.z * d0.z >= d1.x * d1.x + d1.y * d1.y + d1.z * d1.z ? 0 : 1;
  return walkChain(w, next, id, far, distance + 1, direct);
}

}  // namespace

bool poweredByChain(const World& world, const glm::ivec3& pos, const PowerFn& direct) {
  const int id = railIdAt(world, pos);
  if (id != kPowered && id != kActivator) return false;
  return walkChain(world, pos, id, 0, 0, direct) || walkChain(world, pos, id, 1, 0, direct);
}

const Ends& endsOf(int shape) {
  static const Ends table[10] = {
      {{0, 0, -1}, {0, 0, 1}},   // 0 norte-sur
      {{-1, 0, 0}, {1, 0, 0}},   // 1 este-oeste
      {{-1, -1, 0}, {1, 0, 0}},  // 2 sube al este
      {{-1, 0, 0}, {1, -1, 0}},  // 3 sube al oeste
      {{0, 0, -1}, {0, -1, 1}},  // 4 sube al norte
      {{0, -1, -1}, {0, 0, 1}},  // 5 sube al sur
      {{0, 0, 1}, {1, 0, 0}},    // 6 sur-este
      {{0, 0, 1}, {-1, 0, 0}},   // 7 sur-oeste
      {{0, 0, -1}, {-1, 0, 0}},  // 8 norte-oeste
      {{0, 0, -1}, {1, 0, 0}},   // 9 norte-este
  };
  return table[std::clamp(shape, 0, 9)];
}

int shapeFor(const World& world, const glm::ivec3& pos) {
  const BlockState s = world.block(pos.x, pos.y, pos.z);
  const int id = stateId(s);
  const int current = shapeOf(id, stateMeta(s));
  std::vector<Candidate> found;
  for (int d = 0; d < 4; d++) {
    const glm::ivec3 base = pos + dirVec(d);
    // Un raíl al lado, uno una celda más arriba (cuesta hacia arriba) o uno más abajo (cuesta hacia abajo)
    for (int dy : {0, 1, -1}) {
      const glm::ivec3 n = base + glm::ivec3(0, dy, 0);
      if (railIdAt(world, n) < 0 || !accepts(world, n, pos)) continue;
      found.push_back({d, n, pointsTo(world, n, pos)});
      break;
    }
  }
  if (found.empty()) return current;
  std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.pointing && !b.pointing; });
  if (found.size() > 2) found.resize(2);
  auto slopeToward = [&](const Candidate& c) { return c.pos.y > pos.y; };
  auto axisShape = [&](const Candidate& c) {
    const bool ns = c.dir == 0 || c.dir == 2;
    if (!slopeToward(c)) return ns ? 0 : 1;
    switch (c.dir) {
      case 0: return 4;  // sube al norte
      case 1: return 2;  // sube al este
      case 2: return 5;  // sube al sur
      default: return 3;
    }
  };
  if (found.size() == 1) return axisShape(found[0]);
  const Candidate &a = found[0], &b = found[1];
  const bool opposite = (a.dir + 2) % 4 == b.dir;
  if (opposite) return slopeToward(b) && !slopeToward(a) ? axisShape(b) : axisShape(a);
  if (straightOnly(id)) return axisShape(a);
  // Curva: se pide por los dos lados que une
  const int lo = std::min(a.dir, b.dir), hi = std::max(a.dir, b.dir);
  if (lo == 1 && hi == 2) return 6;  // este y sur
  if (lo == 2 && hi == 3) return 7;  // sur y oeste
  if (lo == 0 && hi == 3) return 8;  // norte y oeste
  return 9;                          // norte y este
}

bool connect(WorldAccess& access, const glm::ivec3& pos) {
  World& w = access.world();
  bool changed = false;
  auto apply = [&](const glm::ivec3& p) {
    const BlockState s = w.block(p.x, p.y, p.z);
    const int id = stateId(s), meta = stateMeta(s);
    if (!isRail(id)) return false;
    const int want = shapeFor(w, p);
    if (want == shapeOf(id, meta)) return false;
    access.setBlock(p.x, p.y, p.z, makeState(id, withShape(id, meta, want)));
    return true;
  };
  changed |= apply(pos);
  // Los vecinos a los que se une pueden tener que girar para encajar
  for (int d = 0; d < 4; d++)
    for (int dy : {0, 1, -1}) {
      const glm::ivec3 n = pos + dirVec(d) + glm::ivec3(0, dy, 0);
      if (railIdAt(w, n) >= 0) {
        changed |= apply(n);
        break;
      }
    }
  return changed;
}

OnRail pointOnRail(const glm::ivec3& railPos, int shape, double x, double z) {
  const Ends& e = endsOf(shape);
  const double ax = railPos.x + 0.5 + e.a.x * 0.5, az = railPos.z + 0.5 + e.a.z * 0.5;
  const double bx = railPos.x + 0.5 + e.b.x * 0.5, bz = railPos.z + 0.5 + e.b.z * 0.5;
  const double dx = bx - ax, dz = bz - az;
  const double len2 = dx * dx + dz * dz;
  const double t = len2 > 0 ? ((x - ax) * dx + (z - az) * dz) / len2 : 0.0;
  const double lift = ascending(shape) ? 1.0 : 0.0;
  const double ya = railPos.y + lift + e.a.y + 0.0625, yb = railPos.y + lift + e.b.y + 0.0625;
  const double tc = std::clamp(t, 0.0, 1.0);
  return {ax + dx * t, ya + (yb - ya) * tc, az + dz * t};
}

bool railAt(const World& world, double x, double y, double z, glm::ivec3& railPos) {
  const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y)), cz = static_cast<int>(std::floor(z));
  if (railIdAt(world, {cx, cy - 1, cz}) >= 0) {
    railPos = {cx, cy - 1, cz};
    return true;
  }
  if (railIdAt(world, {cx, cy, cz}) >= 0) {
    railPos = {cx, cy, cz};
    return true;
  }
  return false;
}

}  // namespace mcw::rails
