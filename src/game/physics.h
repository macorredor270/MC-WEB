#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <vector>

#include "core/face.h"
#include "data/blocks.h"

namespace mcw {

class World;

struct AABB {
  glm::dvec3 min{0}, max{0};

  static AABB centered(const glm::dvec3& feet, double width, double height) {
    return {{feet.x - width / 2, feet.y, feet.z - width / 2}, {feet.x + width / 2, feet.y + height, feet.z + width / 2}};
  }
  AABB offset(const glm::dvec3& d) const { return {min + d, max + d}; }
  AABB expand(const glm::dvec3& d) const { return {min - d, max + d}; }
  /// Caja que cubre el recorrido de un movimiento.
  AABB sweep(const glm::dvec3& d) const {
    AABB r = *this;
    for (int i = 0; i < 3; i++) (d[i] < 0 ? r.min[i] : r.max[i]) += d[i];
    return r;
  }
  bool intersects(const AABB& o) const {
    return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
  }
  glm::dvec3 center() const { return (min + max) * 0.5; }
};

/// Cajas de colisión de los bloques que tocan una región (en coordenadas de mundo).
void collectBlockBoxes(const World& world, const AABB& region, std::vector<AABB>& out);

/// Distancia máxima que se puede mover `box` en el eje `axis` antes de chocar con `obstacles`.
double clipAxis(const AABB& box, const std::vector<AABB>& obstacles, int axis, double delta);

struct MoveResult {
  bool collidedX = false, collidedY = false, collidedZ = false;
  bool onGround = false;
};

/// Mueve una caja con colisiones por ejes (Y, X, Z) y escalón automático (`stepHeight`) si
/// estaba en el suelo. `motion` se recorta a lo que realmente se ha movido.
MoveResult moveBox(const World& world, AABB& box, glm::dvec3& motion, double stepHeight, bool wasOnGround);

/// Resultado de apuntar a un bloque.
struct RayHit {
  glm::ivec3 block{0};
  int face = Face::Up;  // cara golpeada
  glm::dvec3 point{0};
  double distance = 0;
};

/// Cajas de selección (las del contorno negro) de un bloque, en 0..1.
void selectionBoxes(BlockState s, BlockState below, std::vector<AABB>& out);

/// Lanza un rayo contra los bloques (ignora aire y fluidos). DDA + test contra la caja de cada bloque.
std::optional<RayHit> raycastBlocks(const World& world, const glm::dvec3& origin, const glm::dvec3& dir, double maxDist);

}  // namespace mcw
