#include "world/tree_grow.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace mcw {
namespace {

bool replaceableByTree(BlockState s) {
  const int id = stateId(s);
  return id == B::air || id == B::leaves || id == B::leaves2 || id == B::sapling || id == B::tallgrass || id == B::vine ||
         id == B::snow_layer || id == B::deadbush;
}

struct Cell {
  int x, y, z;
  BlockState s;
  bool log;
};

}  // namespace

bool growTree(const std::function<BlockState(int, int, int)>& get, const std::function<void(int, int, int, BlockState, bool)>& put,
              int type, int x, int y, int z, Random& rng, bool big) {
  type = std::clamp(type, 0, 5);
  const bool second = type >= 4;  // acacia y roble oscuro usan los bloques de la segunda lista
  const int meta = second ? type - 4 : type;
  const BlockState logS = makeState(second ? B::log2 : B::log, meta), leafS = makeState(second ? B::leaves2 : B::leaves, meta);
  std::vector<Cell> logs, leaves;
  auto log = [&](int lx, int ly, int lz) { logs.push_back({lx, ly, lz, logS, true}); };
  auto leaf = [&](int lx, int ly, int lz) { leaves.push_back({lx, ly, lz, leafS, false}); };
  // Una capa redonda de hojas centrada en (cx, cz) (`ox`, `oz` desplazan el centro: troncos de 2x2)
  auto disc = [&](double cx, double cz, int ly, double r) {
    const int reach = static_cast<int>(r) + 1;
    for (int dz = -reach; dz <= reach + 1; dz++)
      for (int dx = -reach; dx <= reach + 1; dx++) {
        const double ex = (x + dx) - cx, ez = (z + dz) - cz;
        if (ex * ex + ez * ez <= r * r + 0.25) leaf(x + dx, ly, z + dz);
      }
  };

  if (type == 4) {  // acacia: tronco recto que se dobla en diagonal y copa plana
    const int h = 5 + rng.nextInt(2);
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const auto& d = dirs[rng.nextInt(4)];
    const int bendAt = h - 3;
    int bx = x, bz = z, top = y;
    for (int i = 0; i < h; i++) {
      if (i >= bendAt) {
        bx += d[0];
        bz += d[1];
      }
      log(bx, y + i, bz);
      top = y + i;
    }
    for (int dz = -3; dz <= 3; dz++)
      for (int dx = -3; dx <= 3; dx++) {
        const int r2 = dx * dx + dz * dz;
        if (r2 <= 8) leaf(bx + dx, top, bz + dz);
        if (r2 <= 3) leaf(bx + dx, top + 1, bz + dz);
      }
  } else if (type == 5) {  // roble oscuro: tronco de 2x2 y copa ancha
    const int h = 6 + rng.nextInt(3);
    for (int i = 0; i < h; i++)
      for (int dz = 0; dz <= 1; dz++)
        for (int dx = 0; dx <= 1; dx++) log(x + dx, y + i, z + dz);
    const int top = y + h - 1;
    disc(x + 0.5, z + 0.5, top + 1, 1.5);
    disc(x + 0.5, z + 0.5, top, 3.2);
    disc(x + 0.5, z + 0.5, top - 1, 3.2);
    disc(x + 0.5, z + 0.5, top - 2, 2.2);
  } else if (type == 3 && big) {  // jungla gigante
    const int h = 14 + rng.nextInt(10);
    for (int i = 0; i < h; i++)
      for (int dz = 0; dz <= 1; dz++)
        for (int dx = 0; dx <= 1; dx++) log(x + dx, y + i, z + dz);
    const int top = y + h - 1;
    disc(x + 0.5, z + 0.5, top + 1, 2.3);
    disc(x + 0.5, z + 0.5, top, 4.3);
    disc(x + 0.5, z + 0.5, top - 1, 4.3);
    disc(x + 0.5, z + 0.5, top - 3, 3.0);
  } else if (type == 1) {  // abeto: tronco alto y copa de capas cónicas
    const int h = 6 + rng.nextInt(4);
    const int top = y + h - 1;
    for (int i = 0; i < h; i++) log(x, y + i, z);
    leaf(x, top + 1, z);
    for (int ly = top; ly >= y + 2; ly--) {
      const int layer = top - ly;
      const int radius = layer == 0 ? 1 : (layer % 2 == 1 ? std::min(1 + layer / 3, 3) : std::max(1, std::min(layer / 3, 2)));
      for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
          if (std::abs(dx) == radius && std::abs(dz) == radius && radius > 0) continue;
          leaf(x + dx, ly, z + dz);
        }
    }
  } else {  // roble, abedul y jungla pequeña
    const int h = type == 3 ? 4 + rng.nextInt(7) : (type == 2 ? 5 : 4) + rng.nextInt(3);
    const int top = y + h - 1;
    for (int i = 0; i < h; i++) log(x, y + i, z);
    for (int ly = top - 2; ly <= top + 1; ly++) {
      const int radius = ly >= top ? 1 : 2;
      for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
          const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
          if (corner && (ly == top + 1 || rng.nextInt(2) == 0)) continue;
          leaf(x + dx, ly, z + dz);
        }
    }
  }

  // Hace falta sitio para el tronco (y que no llegue al techo del mundo); las hojas solo van donde cabe
  for (const Cell& c : logs)
    if (c.y >= 255 || !replaceableByTree(get(c.x, c.y, c.z))) {
      // el propio brote cuenta como hueco
      const bool sapling = c.x >= x && c.x <= x + 1 && c.z >= z && c.z <= z + 1 && c.y == y;
      if (!sapling) return false;
    }
  for (const Cell& c : leaves)
    if (c.y < 255 && replaceableByTree(get(c.x, c.y, c.z))) put(c.x, c.y, c.z, c.s, false);
  for (const Cell& c : logs) put(c.x, c.y, c.z, c.s, true);
  return true;
}

}  // namespace mcw
