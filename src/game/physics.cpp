#include "game/physics.h"

#include <cmath>

#include "data/items.h"
#include "world/world.h"

namespace mcw {

void collectBlockBoxes(const World& world, const AABB& region, std::vector<AABB>& out) {
  out.clear();
  const int x0 = static_cast<int>(std::floor(region.min.x)), x1 = static_cast<int>(std::floor(region.max.x));
  const int y0 = static_cast<int>(std::floor(region.min.y)) - 1, y1 = static_cast<int>(std::floor(region.max.y));
  const int z0 = static_cast<int>(std::floor(region.min.z)), z1 = static_cast<int>(std::floor(region.max.z));
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      const bool loaded = world.chunkAt(x, z) != nullptr;
      for (int y = y0; y <= y1; y++) {
        if (!loaded) {  // chunk sin cargar: pared sólida para no caer al vacío
          out.push_back({{double(x), double(y), double(z)}, {x + 1.0, y + 1.0, z + 1.0}});
          continue;
        }
        const BlockState s = world.block(x, y, z);
        if (s == 0) continue;
        for (const Box& b : collisionBoxes(stateId(s), stateMeta(s)))
          out.push_back({{x + b.x0, y + b.y0, z + b.z0}, {x + b.x1, y + b.y1, z + b.z1}});
      }
    }
}

double clipAxis(const AABB& box, const std::vector<AABB>& obstacles, int axis, double delta) {
  const int a = (axis + 1) % 3, b = (axis + 2) % 3;
  for (const AABB& o : obstacles) {
    if (box.max[a] <= o.min[a] || box.min[a] >= o.max[a] || box.max[b] <= o.min[b] || box.min[b] >= o.max[b]) continue;
    if (delta > 0 && box.max[axis] <= o.min[axis]) delta = std::min(delta, o.min[axis] - box.max[axis]);
    else if (delta < 0 && box.min[axis] >= o.max[axis]) delta = std::max(delta, o.max[axis] - box.min[axis]);
  }
  return delta;
}

namespace {

/// Movimiento por ejes sin escalón. Devuelve el desplazamiento real.
glm::dvec3 slide(const std::vector<AABB>& obs, AABB& box, const glm::dvec3& want) {
  glm::dvec3 d = want;
  d.y = clipAxis(box, obs, 1, d.y);
  box = box.offset({0, d.y, 0});
  d.x = clipAxis(box, obs, 0, d.x);
  box = box.offset({d.x, 0, 0});
  d.z = clipAxis(box, obs, 2, d.z);
  box = box.offset({0, 0, d.z});
  return d;
}

}  // namespace

MoveResult moveBox(const World& world, AABB& box, glm::dvec3& motion, double stepHeight, bool wasOnGround) {
  std::vector<AABB> obs;
  collectBlockBoxes(world, box.sweep(motion).expand({0, stepHeight, 0}), obs);

  const glm::dvec3 want = motion;
  AABB moved = box;
  glm::dvec3 d = slide(obs, moved, want);

  // Escalón: si choca en horizontal estando en el suelo, probar subiendo hasta stepHeight
  const bool blockedH = (d.x != want.x) || (d.z != want.z);
  if (stepHeight > 0 && blockedH && (wasOnGround || (want.y < 0 && d.y != want.y))) {
    AABB up = box;
    const double lift = clipAxis(up, obs, 1, stepHeight);
    up = up.offset({0, lift, 0});
    glm::dvec3 d2{0, lift, 0};
    const double dx = clipAxis(up, obs, 0, want.x);
    up = up.offset({dx, 0, 0});
    const double dz = clipAxis(up, obs, 2, want.z);
    up = up.offset({0, 0, dz});
    const double down = clipAxis(up, obs, 1, -lift + std::min(0.0, want.y));
    up = up.offset({0, down, 0});
    d2 += glm::dvec3(dx, down, dz);
    if (dx * dx + dz * dz > d.x * d.x + d.z * d.z + 1e-9) {
      moved = up;
      d = d2;
    }
  }

  MoveResult r;
  r.collidedX = d.x != want.x;
  r.collidedY = d.y != want.y;
  r.collidedZ = d.z != want.z;
  r.onGround = r.collidedY && want.y < 0;
  box = moved;
  motion = {r.collidedX ? 0.0 : motion.x, r.collidedY ? 0.0 : motion.y, r.collidedZ ? 0.0 : motion.z};
  return r;
}

void selectionBoxes(BlockState s, BlockState below, std::vector<AABB>& out) {
  out.clear();
  const int id = stateId(s), meta = stateMeta(s);
  auto box = [&](double x0, double y0, double z0, double x1, double y1, double z1) { out.push_back({{x0, y0, z0}, {x1, y1, z1}}); };
  switch (id) {
    case B::tallgrass: case B::deadbush: case B::sapling: box(0.1, 0, 0.1, 0.9, 0.8, 0.9); return;
    case B::yellow_flower: case B::red_flower: box(0.3, 0, 0.3, 0.7, 0.6, 0.7); return;
    case B::brown_mushroom: case B::red_mushroom: box(0.3, 0, 0.3, 0.7, 0.4, 0.7); return;
    case B::reeds: box(0.125, 0, 0.125, 0.875, 1, 0.875); return;
    case B::double_plant: box(0.1, 0, 0.1, 0.9, 1, 0.9); return;
    case B::waterlily: box(0, 0, 0, 1, 0.015625, 1); return;
    case B::web: box(0, 0, 0, 1, 1, 1); return;
    case B::snow_layer: box(0, 0, 0, 1, ((meta & 7) + 1) / 8.0, 1); return;
    case B::torch:
      switch (meta) {
        case 1: box(0, 0.2, 0.35, 0.3, 0.8, 0.65); return;
        case 2: box(0.7, 0.2, 0.35, 1, 0.8, 0.65); return;
        case 3: box(0.35, 0.2, 0, 0.65, 0.8, 0.3); return;
        case 4: box(0.35, 0.2, 0.7, 0.65, 0.8, 1); return;
        default: box(0.4, 0, 0.4, 0.6, 0.6, 0.6); return;
      }
    default: break;
  }
  (void)below;
  if (isFluid(id) || id == B::air) return;
  const auto boxes = collisionBoxes(id, meta);
  if (boxes.empty()) { box(0, 0, 0, 1, 1, 1); return; }
  for (const Box& b : boxes) box(b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
}

namespace {

/// Intersección rayo-caja (método de los "slabs"). Devuelve t de entrada y la cara.
bool rayBox(const glm::dvec3& o, const glm::dvec3& d, const AABB& b, double& tHit, int& face) {
  double tmin = -1e30, tmax = 1e30;
  int enterFace = -1;
  for (int a = 0; a < 3; a++) {
    if (std::abs(d[a]) < 1e-12) {
      if (o[a] < b.min[a] || o[a] > b.max[a]) return false;
      continue;
    }
    double t1 = (b.min[a] - o[a]) / d[a], t2 = (b.max[a] - o[a]) / d[a];
    // Cara por la que entra: si el rayo va en positivo entra por la cara "menor"
    int f1 = a == 0 ? Face::West : (a == 1 ? Face::Down : Face::North);
    int f2 = a == 0 ? Face::East : (a == 1 ? Face::Up : Face::South);
    if (t1 > t2) { std::swap(t1, t2); std::swap(f1, f2); }
    if (t1 > tmin) { tmin = t1; enterFace = f1; }
    tmax = std::min(tmax, t2);
    if (tmin > tmax) return false;
  }
  if (tmax < 0 || enterFace < 0) return false;
  tHit = tmin;
  face = enterFace;
  return true;
}

}  // namespace

std::optional<RayHit> raycastBlocks(const World& world, const glm::dvec3& origin, const glm::dvec3& dirIn, double maxDist) {
  const glm::dvec3 dir = glm::normalize(dirIn);
  glm::ivec3 cell(static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)), static_cast<int>(std::floor(origin.z)));
  const glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
  glm::dvec3 tMax, tDelta;
  for (int a = 0; a < 3; a++) {
    if (std::abs(dir[a]) < 1e-12) { tMax[a] = 1e30; tDelta[a] = 1e30; continue; }
    const double next = step[a] > 0 ? cell[a] + 1 - origin[a] : origin[a] - cell[a];
    tMax[a] = next / std::abs(dir[a]);
    tDelta[a] = 1.0 / std::abs(dir[a]);
  }
  std::vector<AABB> boxes;
  for (int i = 0; i < 256; i++) {
    const BlockState s = world.block(cell.x, cell.y, cell.z);
    if (s != 0 && !isFluid(stateId(s))) {
      selectionBoxes(s, world.block(cell.x, cell.y - 1, cell.z), boxes);
      std::optional<RayHit> best;
      for (const AABB& b : boxes) {
        const AABB wb = b.offset(glm::dvec3(cell));
        double t;
        int face;
        if (rayBox(origin, dir, wb, t, face) && t <= maxDist && (!best || t < best->distance))
          best = RayHit{cell, face, origin + dir * std::max(0.0, t), std::max(0.0, t)};
      }
      if (best) return best;
    }
    const int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
    if (tMax[a] > maxDist) break;
    cell[a] += step[a];
    tMax[a] += tDelta[a];
  }
  return std::nullopt;
}

}  // namespace mcw
