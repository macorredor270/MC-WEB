#pragma once
// Raíles: su forma según los vecinos y la geometría por la que andan las vagonetas.
#include <array>
#include <functional>
#include <glm/glm.hpp>

#include "core/types.h"

namespace mcw {

class World;
class WorldAccess;

namespace rails {

inline constexpr int kRail = 66, kPowered = 27, kDetector = 28, kActivator = 157;

inline bool isRail(int id) { return id == kRail || id == kPowered || id == kDetector || id == kActivator; }
/// Los raíles especiales solo van rectos (o en cuesta); el normal también hace curvas.
inline bool straightOnly(int id) { return id == kPowered || id == kDetector || id == kActivator; }

/// Forma: 0 norte-sur, 1 este-oeste, 2 sube al este, 3 sube al oeste, 4 sube al norte, 5 sube al sur y 6 a 9 curvas
/// (6 sur-este, 7 sur-oeste, 8 norte-oeste, 9 norte-este). En los especiales solo los bits bajos (0 a 5); el 8 es "activo".
inline int shapeOf(int id, int meta) { return straightOnly(id) ? (meta & 7) : (meta & 15); }
inline bool active(int id, int meta) { return straightOnly(id) && (meta & 8) != 0; }
inline int withShape(int id, int meta, int shape) { return straightOnly(id) ? ((meta & 8) | (shape & 7)) : (shape & 15); }
inline bool ascending(int shape) { return shape >= 2 && shape <= 5; }

/// Los dos extremos de cada forma, relativos al centro de la celda: (dx, dy, dz) con dy = -1 en el extremo bajo de una cuesta.
struct Ends {
  glm::ivec3 a, b;
};
const Ends& endsOf(int shape);

/// Dirección horizontal 0 norte, 1 este, 2 sur, 3 oeste.
inline glm::ivec3 dirVec(int d) {
  static const glm::ivec3 v[4] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
  return v[d & 3];
}

/// Forma que le corresponde a un raíl en `pos` según los vecinos (y la que ya tiene, que se respeta si encaja).
int shapeFor(const World& world, const glm::ivec3& pos);
/// Recalcula y escribe la forma del raíl de `pos` (y la de los vecinos a los que se conecta). Devuelve si cambió algo.
bool connect(WorldAccess& access, const glm::ivec3& pos);

/// Punto de la vía sobre la línea central del raíl más cercano a (x, z) y su altura (y de la superficie, la de la vagoneta).
struct OnRail {
  double x = 0, y = 0, z = 0;
};
OnRail pointOnRail(const glm::ivec3& railPos, int shape, double x, double z);

/// Los propulsores (y los activadores) se pasan la potencia en fila: uno con potencia propia (`direct`) enciende hasta 8 más
/// seguidos del mismo tipo, siempre que vayan en el mismo eje. Dice si el de `pos` se enciende por alguno de sus lados.
using PowerFn = std::function<bool(const glm::ivec3&)>;
bool poweredByChain(const World& world, const glm::ivec3& pos, const PowerFn& direct);

/// El raíl en el que está una vagoneta en (x, y, z): el de su celda o el de debajo. Devuelve false si no hay.
bool railAt(const World& world, double x, double y, double z, glm::ivec3& railPos);

}  // namespace rails
}  // namespace mcw
