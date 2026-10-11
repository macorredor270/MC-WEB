#pragma once
// Ayudas para construir estructuras dentro de un chunk: se dibujan en coordenadas del mundo y solo se escribe lo que cae
// en el chunk, así cada chunk puede repetir la misma estructura sin depender de sus vecinos.
#include <algorithm>

#include "world/chunk.h"

namespace mcw {

struct ChunkBuilder {
  Chunk& c;
  int bx, bz;
  explicit ChunkBuilder(Chunk& chunk) : c(chunk), bx(chunk.pos().x * 16), bz(chunk.pos().z * 16) {}

  bool inside(int x, int z) const { return x >= bx && x < bx + 16 && z >= bz && z < bz + 16; }
  void set(int x, int y, int z, BlockState s) {
    if (inside(x, z) && y > 0 && y < kChunkHeight) c.setBlock(x - bx, y, z - bz, s);
  }
  BlockState get(int x, int y, int z) const { return inside(x, z) && y >= 0 && y < kChunkHeight ? c.block(x - bx, y, z - bz) : BlockState{0}; }
  /// ¿La caja toca este chunk?
  bool touches(int x0, int z0, int x1, int z1) const { return x1 >= bx && x0 < bx + 16 && z1 >= bz && z0 < bz + 16; }
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, BlockState s) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    x0 = std::max(x0, bx); x1 = std::min(x1, bx + 15);
    z0 = std::max(z0, bz); z1 = std::min(z1, bz + 15);
    y0 = std::max(y0, 1); y1 = std::min(y1, kChunkHeight - 1);
    for (int z = z0; z <= z1; z++)
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) c.setBlock(x - bx, y, z - bz, s);
  }
  /// Como `fill`, pero solo donde ahora hay aire o algo que se pueda sustituir (líquidos incluidos).
  void fillAir(int x0, int y0, int z0, int x1, int y1, int z1, BlockState s) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    x0 = std::max(x0, bx); x1 = std::min(x1, bx + 15);
    z0 = std::max(z0, bz); z1 = std::min(z1, bz + 15);
    y0 = std::max(y0, 1); y1 = std::min(y1, kChunkHeight - 1);
    for (int z = z0; z <= z1; z++)
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          const int id = stateId(c.block(x - bx, y, z - bz));
          if (id == 0 || (id >= 8 && id <= 11)) c.setBlock(x - bx, y, z - bz, s);
        }
  }
  void tile(int x, int y, int z, GenTile::Kind kind, int param) {
    if (!inside(x, z) || y <= 0 || y >= kChunkHeight) return;
    GenTile t;
    t.x = static_cast<i8>(x - bx);
    t.z = static_cast<i8>(z - bz);
    t.y = static_cast<u8>(y);
    t.kind = kind;
    t.param = static_cast<u8>(param);
    c.addGenTile(t);
  }
};

/// Sistema de coordenadas de una pieza: u hacia delante, v a un lado, desde un punto del mundo; `dir` 0 +x, 1 +z, 2 -x, 3 -z.
struct Frame {
  int x = 0, z = 0, dir = 0;
  void at(int u, int v, int& wx, int& wz) const {
    switch (dir & 3) {
      case 0: wx = x + u; wz = z + v; break;
      case 1: wx = x - v; wz = z + u; break;
      case 2: wx = x - u; wz = z - v; break;
      default: wx = x + v; wz = z - u; break;
    }
  }
  void fill(ChunkBuilder& b, int u0, int u1, int v0, int v1, int y0, int y1, BlockState s) const {
    int ax, az, bx2, bz2;
    at(u0, v0, ax, az);
    at(u1, v1, bx2, bz2);
    b.fill(ax, y0, az, bx2, y1, bz2, s);
  }
  void set(ChunkBuilder& b, int u, int v, int y, BlockState s) const {
    int wx, wz;
    at(u, v, wx, wz);
    b.set(wx, y, wz, s);
  }
  /// Orientación (meta de cofre/escalera: 2 norte, 3 sur, 4 oeste, 5 este) de la dirección local (du, dv).
  int facing(int du, int dv) const {
    int ax, az, bx2, bz2;
    at(0, 0, ax, az);
    at(du, dv, bx2, bz2);
    const int dx = bx2 - ax, dz = bz2 - az;
    return dx > 0 ? 5 : dx < 0 ? 4 : dz > 0 ? 3 : 2;
  }
};

}  // namespace mcw
