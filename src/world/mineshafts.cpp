// Minas abandonadas: galerías con raíles y vigas de madera, cruces, escaleras y salas, con telarañas, generadores de arañas
// de cueva y vagonetas con cofre (comportamiento observable de 1.8 según minecraft.wiki; las construcciones son propias). El plan
// de cada mina sale de la semilla y cada chunk dibuja la parte que le toca.
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/random.h"
#include "world/gen_util.h"
#include "world/generator.h"

namespace mcw {
namespace {

constexpr u32 kSaltMine = 8501;
constexpr int kReachChunks = 7;  // hasta dónde llega una mina desde su chunk de origen

enum class MKind : u8 { Room, Corridor, Crossing, Stairs };

struct MPiece {
  MKind kind;
  Frame f;      // +u hacia delante, v a un lado; el origen está en el suelo, al principio de la pieza
  int y = 20;   // y del suelo
  int len = 5;  // fondo (u)
  int w = 3;    // ancho (v), centrado en 0
  int h = 3;
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0, y0 = 0, y1 = 0;
  bool spider = false;
  bool cart = false;
};

void bounds(MPiece& p) {
  int ax, az, bx, bz;
  p.f.at(0, -p.w / 2, ax, az);
  p.f.at(p.len - 1, p.w / 2, bx, bz);
  p.x0 = std::min(ax, bx);
  p.x1 = std::max(ax, bx);
  p.z0 = std::min(az, bz);
  p.z1 = std::max(az, bz);
  p.y0 = p.y - (p.kind == MKind::Stairs ? 5 : 0);
  p.y1 = p.y + p.h + 1;
}

bool intersects(const MPiece& a, const MPiece& b) {
  return a.x1 >= b.x0 && a.x0 <= b.x1 && a.z1 >= b.z0 && a.z0 <= b.z1 && a.y1 >= b.y0 && a.y0 <= b.y1;
}

}  // namespace

void TerrainGenerator::placeMineshafts(Chunk& c, const ColumnInfo*) const {
  ChunkBuilder b(c);
  const int ccx = c.pos().x, ccz = c.pos().z;
  for (int dz = -kReachChunks; dz <= kReachChunks; dz++)
    for (int dx = -kReachChunks; dx <= kReachChunks; dx++) {
      const int scx = ccx + dx, scz = ccz + dz;
      Random rng(cellSeed(seed_, scx, scz, kSaltMine));
      // Como en 1.8: un chunk de cada 250, y más a menudo lejos del origen (hasta 80 chunks)
      if (!(rng.next() < 0.004 && rng.nextInt(80) < std::max(std::abs(scx), std::abs(scz)))) continue;
      const int sx = scx * 16 + 8, sz = scz * 16 + 8;
      if (!b.touches(sx - kReachChunks * 16 - 8, sz - kReachChunks * 16 - 8, sx + kReachChunks * 16 + 8, sz + kReachChunks * 16 + 8)) continue;
      // --- Plan ---
      std::vector<MPiece> pieces;
      const int floorY = 14 + rng.nextInt(34);
      {
        MPiece room;
        room.kind = MKind::Room;
        room.w = 7 + rng.nextInt(5);
        room.len = 7 + rng.nextInt(5);
        room.h = 5 + rng.nextInt(2);
        room.y = floorY;
        room.f = Frame{sx - room.len / 2, sz, 0};
        bounds(room);
        pieces.push_back(room);
      }
      struct Exit {
        Frame f;
        int y;
        int depth;
      };
      std::vector<Exit> exits;
      {
        const MPiece& room = pieces[0];
        for (int side = 0; side < 4; side++) {
          if (rng.nextInt(4) == 0) continue;
          const int off = rng.range(-room.w / 2 + 1, room.w / 2 - 1), offU = rng.range(1, room.len - 2);
          Frame e = room.f;
          int wx, wz;
          switch (side) {
            case 0: room.f.at(room.len, off, wx, wz); e = Frame{wx, wz, room.f.dir}; break;
            case 1: room.f.at(-1, off, wx, wz); e = Frame{wx, wz, (room.f.dir + 2) & 3}; break;
            case 2: room.f.at(offU, room.w / 2 + 1, wx, wz); e = Frame{wx, wz, (room.f.dir + 1) & 3}; break;
            default: room.f.at(offU, -room.w / 2 - 1, wx, wz); e = Frame{wx, wz, (room.f.dir + 3) & 3}; break;
          }
          exits.push_back({e, floorY, 0});
        }
      }
      auto free = [&](const MPiece& cand) {
        if (std::abs((cand.x0 + cand.x1) / 2 - sx) > kReachChunks * 16 || std::abs((cand.z0 + cand.z1) / 2 - sz) > kReachChunks * 16) return false;
        if (cand.y0 < 8 || cand.y1 > 56) return false;
        for (const MPiece& o : pieces)
          if (intersects(cand, o)) return false;
        return true;
      };
      for (std::size_t i = 0; i < exits.size() && pieces.size() < 40; i++) {
        const Exit ex = exits[i];
        if (ex.depth > 8) continue;
        MPiece p;
        p.f = ex.f;
        p.y = ex.y;
        const int roll = rng.nextInt(100);
        if (roll < 62) {
          p.kind = MKind::Corridor;
          p.len = 5 * (2 + rng.nextInt(3));
          p.spider = rng.nextInt(18) == 0;
          p.cart = rng.nextInt(14) == 0;
        } else if (roll < 80) {
          p.kind = MKind::Crossing;
          p.len = 3;
        } else if (roll < 90) {
          p.kind = MKind::Stairs;
          p.len = 5;
        } else {
          p.kind = MKind::Room;
          p.w = 7 + rng.nextInt(3);
          p.len = 7 + rng.nextInt(3);
          p.h = 5;
        }
        // Una sala se centra delante de la salida
        if (p.kind == MKind::Room) {
          int ox, oz;
          p.f.at(0, 0, ox, oz);
          p.f = Frame{ox, oz, ex.f.dir};
        }
        bounds(p);
        if (!free(p)) continue;
        pieces.push_back(p);
        int ex2, ez2;
        p.f.at(p.len, 0, ex2, ez2);
        const int yOut = p.kind == MKind::Stairs ? p.y - 5 : p.y;
        const int d = ex.depth + 1;
        if (p.kind == MKind::Corridor || p.kind == MKind::Stairs) {
          exits.push_back({Frame{ex2, ez2, p.f.dir}, yOut, d});
          if (p.kind == MKind::Corridor && p.len >= 10 && rng.nextInt(3) == 0) {  // un ramal a un lado
            int mx, mz;
            p.f.at(rng.range(2, p.len - 3), 2, mx, mz);
            exits.push_back({Frame{mx, mz, (p.f.dir + 1) & 3}, yOut, d + 1});
          }
        } else if (p.kind == MKind::Crossing) {
          exits.push_back({Frame{ex2, ez2, p.f.dir}, yOut, d});
          int mx, mz;
          p.f.at(1, 2, mx, mz);
          exits.push_back({Frame{mx, mz, (p.f.dir + 1) & 3}, yOut, d});
          p.f.at(1, -2, mx, mz);
          exits.push_back({Frame{mx, mz, (p.f.dir + 3) & 3}, yOut, d});
        } else {
          int mx, mz;
          p.f.at(p.len, 0, mx, mz);
          exits.push_back({Frame{mx, mz, p.f.dir}, yOut, d});
          p.f.at(p.len / 2, p.w / 2 + 1, mx, mz);
          exits.push_back({Frame{mx, mz, (p.f.dir + 1) & 3}, yOut, d + 1});
          p.f.at(p.len / 2, -p.w / 2 - 1, mx, mz);
          exits.push_back({Frame{mx, mz, (p.f.dir + 3) & 3}, yOut, d + 1});
        }
      }
      // --- Dibujo en este chunk ---
      const BlockState planks = makeState(B::planks, 0), fence = makeState(85), web = makeState(B::web);
      auto carve = [&](int x, int y, int z) {
        if (!b.inside(x, z) || y < 2 || y >= 120) return;
        const int id = stateId(b.get(x, y, z));
        if (id == B::bedrock || isFluid(id)) return;
        b.set(x, y, z, 0);
      };
      for (const MPiece& p : pieces) {
        if (!b.touches(p.x0 - 1, p.z0 - 1, p.x1 + 1, p.z1 + 1)) continue;
        Random prng(cellSeed(seed_, p.x0, p.z0, kSaltMine + 1 + static_cast<u32>(p.y)));
        const Frame& f = p.f;
        auto at = [&](int u, int v, int& wx, int& wz) { f.at(u, v, wx, wz); };
        switch (p.kind) {
          case MKind::Corridor: case MKind::Crossing: {
            const bool corridor = p.kind == MKind::Corridor;
            for (int u = 0; u < p.len; u++)
              for (int v = -1; v <= 1; v++) {
                int wx, wz;
                at(u, v, wx, wz);
                for (int y = p.y + 1; y <= p.y + 3; y++) carve(wx, y, wz);
                // El suelo: si hay un hueco debajo se tapa con tablones
                if (b.inside(wx, wz) && stateId(b.get(wx, p.y, wz)) == 0) b.set(wx, p.y, wz, planks);
              }
            if (corridor) {
              const int axisMeta = (f.dir & 1) ? 0 : 1;  // raíl norte-sur (0) si el pasillo va en z; este-oeste (1) si va en x
              for (int u = 0; u < p.len; u++) {
                int wx, wz;
                at(u, 0, wx, wz);
                if (b.inside(wx, wz) && prng.nextInt(10) != 0) b.set(wx, p.y + 1, wz, makeState(66, axisMeta));
              }
              // Vigas cada cinco bloques
              for (int u = 1; u < p.len; u += 5) {
                for (int v : {-1, 1}) {
                  int wx, wz;
                  at(u, v, wx, wz);
                  if (b.inside(wx, wz)) {
                    b.set(wx, p.y + 1, wz, fence);
                    b.set(wx, p.y + 2, wz, fence);
                  }
                }
                for (int v = -1; v <= 1; v++) {
                  int wx, wz;
                  at(u, v, wx, wz);
                  if (b.inside(wx, wz)) b.set(wx, p.y + 3, wz, planks);
                }
                // Con probabilidad, una telaraña colgando
                if (prng.nextInt(3) == 0) {
                  int wx, wz;
                  at(u + 1, prng.range(-1, 1), wx, wz);
                  if (b.inside(wx, wz)) b.set(wx, p.y + 3, wz, web);
                }
              }
              if (p.spider) {
                // Un generador de arañas de cueva en el centro, con telarañas alrededor
                int sx2, sz2;
                at(p.len / 2, 0, sx2, sz2);
                for (int u = p.len / 2 - 2; u <= p.len / 2 + 2; u++)
                  for (int v = -1; v <= 1; v++)
                    for (int y = p.y + 1; y <= p.y + 3; y++) {
                      int wx, wz;
                      at(u, v, wx, wz);
                      if (b.inside(wx, wz) && stateId(b.get(wx, y, wz)) == 0 && prng.nextInt(2) == 0) b.set(wx, y, wz, web);
                    }
                if (b.inside(sx2, sz2)) {
                  b.set(sx2, p.y + 1, sz2, makeState(52));
                  b.tile(sx2, p.y + 1, sz2, GenTile::Spawner, 59);
                }
              }
              if (p.cart) {
                int wx, wz;
                at(2 + prng.nextInt(std::max(1, p.len - 4)), 0, wx, wz);
                if (b.inside(wx, wz)) {
                  b.set(wx, p.y + 1, wz, makeState(66, axisMeta));
                  b.tile(wx, p.y + 1, wz, GenTile::ChestCart, 8);
                }
              }
            } else {
              // Cruce: cuatro pilares de tablones en las esquinas
              for (int u : {0, 2})
                for (int v : {-1, 1}) {
                  int wx, wz;
                  at(u, v, wx, wz);
                  if (b.inside(wx, wz))
                    for (int y = p.y + 1; y <= p.y + 3; y++) b.set(wx, y, wz, planks);
                }
            }
            break;
          }
          case MKind::Room: {
            for (int u = 0; u < p.len; u++)
              for (int v = -p.w / 2; v <= p.w / 2; v++) {
                int wx, wz;
                at(u, v, wx, wz);
                for (int y = p.y + 1; y <= p.y + p.h; y++) carve(wx, y, wz);
                if (b.inside(wx, wz) && stateId(b.get(wx, p.y, wz)) == 0) b.set(wx, p.y, wz, planks);
              }
            // Algunos pilares y telarañas
            for (int k = 0; k < 4; k++) {
              int wx, wz;
              at(1 + prng.nextInt(p.len - 2), prng.range(-p.w / 2 + 1, p.w / 2 - 1), wx, wz);
              if (!b.inside(wx, wz)) continue;
              for (int y = p.y + 1; y <= p.y + p.h; y++) b.set(wx, y, wz, planks);
            }
            for (int k = 0; k < 5; k++) {
              int wx, wz;
              at(prng.nextInt(p.len), prng.range(-p.w / 2, p.w / 2), wx, wz);
              if (b.inside(wx, wz) && stateId(b.get(wx, p.y + p.h, wz)) == 0) b.set(wx, p.y + p.h, wz, web);
            }
            break;
          }
          case MKind::Stairs: {
            for (int u = 0; u < p.len; u++)
              for (int v = -1; v <= 1; v++) {
                int wx, wz;
                at(u, v, wx, wz);
                for (int y = p.y - u + 1; y <= p.y - u + 3; y++) carve(wx, y, wz);
                if (b.inside(wx, wz) && u > 0) {  // el escalón se tapa si hay un vacío debajo
                  if (stateId(b.get(wx, p.y - u, wz)) == 0) b.set(wx, p.y - u, wz, planks);
                }
              }
            break;
          }
        }
      }
    }
}

}  // namespace mcw
