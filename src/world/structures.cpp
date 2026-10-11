// Estructuras del mundo: mazmorras y fortines (strongholds) con el portal del End.
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/random.h"
#include "world/gen_util.h"
#include "world/generator.h"

namespace mcw {
namespace {

constexpr u32 kSaltDungeon = 8101, kSaltStronghold = 8201;

BlockState stoneBrick(int x, int y, int z) {
  const u32 h = hash3(x, y, z, 0x5B) % 100;
  return makeState(B::stonebrick, h < 65 ? 0 : h < 82 ? 1 : h < 95 ? 2 : 0);
}

/// Metadato de antorcha pegada a una pared que mira hacia `facing` (2 norte, 3 sur, 4 oeste, 5 este).
int torchMeta(int facing) { return facing == 5 ? 1 : facing == 4 ? 2 : facing == 3 ? 3 : 4; }

/// Metadato del marco del portal que mira hacia la dirección del mundo (dx, dz): 0 sur, 1 oeste, 2 norte, 3 este.
int frameMeta(int dx, int dz) { return dz > 0 ? 0 : dx < 0 ? 1 : dz < 0 ? 2 : 3; }

}  // namespace

std::array<std::pair<int, int>, 3> strongholdPositions(u64 seed) {
  Random rng(seed ^ 0x57A0B1D5ull);
  std::array<std::pair<int, int>, 3> out{};
  double angle = rng.next() * 2.0 * 3.14159265358979;
  for (int i = 0; i < 3; i++) {
    const double dist = 1000.0 + rng.next() * 900.0;
    out[static_cast<std::size_t>(i)] = {static_cast<int>(std::floor(std::cos(angle) * dist / 16.0)) * 16 + 8,
                                        static_cast<int>(std::floor(std::sin(angle) * dist / 16.0)) * 16 + 8};
    angle += 2.0 * 3.14159265358979 / 3.0 + (rng.next() - 0.5) * 0.6;
  }
  return out;
}

// --- Mazmorras ---------------------------------------------------------------------------------------

void TerrainGenerator::placeDungeons(Chunk& c, const ColumnInfo*) const {
  Random rng(cellSeed(seed_, c.pos().x, c.pos().z, kSaltDungeon));
  auto solid = [&](int x, int y, int z) {
    const int id = stateId(c.block(x, y, z));
    return id != B::air && !isFluid(id) && blockInfo(id).opaqueCube;
  };
  ChunkBuilder b(c);
  for (int attempt = 0; attempt < 8; attempt++) {
    const int hx = 2 + rng.nextInt(2), hz = 2 + rng.nextInt(2);
    const int x = 5 + rng.nextInt(6), y = 8 + rng.nextInt(48), z = 5 + rng.nextInt(6);
    // Todo el contorno tiene que ser roca, y la sala debe asomar a alguna cueva por entre 1 y 5 sitios
    int openings = 0;
    bool ok = true;
    for (int dx = -hx - 1; dx <= hx + 1 && ok; dx++)
      for (int dy = -1; dy <= 4 && ok; dy++)
        for (int dz = -hz - 1; dz <= hz + 1 && ok; dz++) {
          const int px = x + dx, py = y + dy, pz = z + dz;
          const bool wall = dx == -hx - 1 || dx == hx + 1 || dz == -hz - 1 || dz == hz + 1;
          if ((dy == -1 || dy == 4) && !solid(px, py, pz)) ok = false;
          else if (wall && dy == 0 && c.block(px, py, pz) == 0 && c.block(px, py + 1, pz) == 0) openings++;
        }
    if (!ok || openings < 1 || openings > 5) continue;
    for (int dx = -hx - 1; dx <= hx + 1; dx++)
      for (int dy = 3; dy >= -1; dy--)
        for (int dz = -hz - 1; dz <= hz + 1; dz++) {
          const bool wall = dx == -hx - 1 || dx == hx + 1 || dz == -hz - 1 || dz == hz + 1;
          const int px = x + dx, py = y + dy, pz = z + dz;
          if (dy == -1) {
            c.setBlock(px, py, pz, rng.nextInt(4) == 0 ? makeState(B::cobblestone) : makeState(B::mossy_cobblestone));
          } else if (wall) {
            // Las paredes son de adoquín, salvo donde hay un hueco al exterior (la sala asoma a una cueva)
            if (solid(px, py, pz)) c.setBlock(px, py, pz, makeState(B::cobblestone));
          } else {
            c.setBlock(px, py, pz, 0);
          }
        }
    // Dos cofres junto a las paredes
    for (int k = 0; k < 2; k++)
      for (int tries = 0; tries < 3; tries++) {
        const int cxp = x + rng.range(-hx, hx), czp = z + rng.range(-hz, hz);
        if (c.block(cxp, y, czp) != 0 || (cxp == x && czp == z)) continue;
        int walls = 0;
        for (const auto& d : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
          if (stateId(c.block(cxp + d.first, y, czp + d.second)) == B::cobblestone) walls++;
        if (walls != 1) continue;
        c.setBlock(cxp, y, czp, makeState(54, 2 + rng.nextInt(4)));
        b.tile(b.bx + cxp, y, b.bz + czp, GenTile::LootChest, 1);
        break;
      }
    const int roll = rng.nextInt(4);
    c.setBlock(x, y, z, makeState(52));
    b.tile(b.bx + x, y, b.bz + z, GenTile::Spawner, roll == 0 ? 51 : roll == 1 ? 52 : 54);
  }
}

// --- Fortines ---------------------------------------------------------------------------------------

namespace {

enum class SKind : u8 { Corridor, Room, Library, Prison, ChestCorridor, Portal };
struct SPiece {
  SKind kind;
  Frame f;
  int len = 5;
  int fy = 28;
};

void layoutStronghold(u64 seed, int idx, int sx, int sz, std::vector<SPiece>& out) {
  Random rng(cellSeed(seed, sx, sz, kSaltStronghold));
  const int fy = 22 + rng.nextInt(14);
  const int dir = rng.nextInt(4);
  out.push_back({SKind::Portal, Frame{sx, sz, dir}, 16, fy});
  std::vector<Frame> doors;
  {
    int x, z;
    out.back().f.at(-1, 0, x, z);
    doors.push_back(Frame{x, z, (dir + 2) & 3});
  }
  for (int n = 0; n < 16 && !doors.empty(); n++) {
    const std::size_t pick = static_cast<std::size_t>(rng.nextInt(static_cast<int>(doors.size())));
    const Frame f = doors[pick];
    doors.erase(doors.begin() + static_cast<std::ptrdiff_t>(pick));
    const double r = rng.next();
    SPiece p{SKind::Corridor, f, 5, fy};
    if (r < 0.45) {
      p.len = rng.chance(0.5) ? 5 : 10;
    } else if (r < 0.62) {
      p.kind = SKind::Room; p.len = 11;
    } else if (r < 0.72) {
      p.kind = SKind::Library; p.len = 13;
    } else if (r < 0.82) {
      p.kind = SKind::Prison; p.len = 9;
    } else {
      p.kind = SKind::ChestCorridor; p.len = 5;
    }
    out.push_back(p);
    int x, z;
    p.f.at(p.len, 0, x, z);
    if (p.kind != SKind::ChestCorridor) doors.push_back(Frame{x, z, p.f.dir});
    if (p.kind == SKind::Room || (p.kind == SKind::Corridor && p.len == 10 && rng.chance(0.5))) {
      const int mid = p.len / 2;
      p.f.at(mid, -(p.kind == SKind::Room ? 6 : 2), x, z);
      doors.push_back(Frame{x, z, (p.f.dir + 3) & 3});
      p.f.at(mid, p.kind == SKind::Room ? 6 : 2, x, z);
      doors.push_back(Frame{x, z, (p.f.dir + 1) & 3});
    }
  }
  (void)idx;
}

}  // namespace

void TerrainGenerator::placeStrongholds(Chunk& c) const {
  ChunkBuilder b(c);
  const auto sites = strongholdPositions(seed_);
  std::vector<SPiece> pieces;
  for (std::size_t i = 0; i < sites.size(); i++) {
    const auto [sx, sz] = sites[i];
    if (!b.touches(sx - 90, sz - 90, sx + 90, sz + 90)) continue;
    layoutStronghold(seed_, static_cast<int>(i), sx, sz, pieces);
  }
  if (pieces.empty()) return;
  const BlockState air = 0;
  for (int pass = 0; pass < 3; pass++) {
    for (const SPiece& p : pieces) {
      const Frame& f = p.f;
      const int fy = p.fy;
      Random prng(cellSeed(seed_, f.x, f.z, kSaltStronghold + static_cast<u32>(p.kind) * 7 + static_cast<u32>(p.len)));
      // Cáscara de ladrillos de piedra (variada) y luego el interior
      auto shell = [&](int u0, int u1, int v0, int v1, int h) {
        int ax, az, bx2, bz2;
        f.at(u0, v0, ax, az);
        f.at(u1, v1, bx2, bz2);
        const int x0 = std::min(ax, bx2), x1 = std::max(ax, bx2), z0 = std::min(az, bz2), z1 = std::max(az, bz2);
        if (!b.touches(x0, z0, x1, z1)) return;
        for (int z = std::max(z0, b.bz); z <= std::min(z1, b.bz + 15); z++)
          for (int x = std::max(x0, b.bx); x <= std::min(x1, b.bx + 15); x++)
            for (int y = fy - 1; y <= fy + h; y++) b.set(x, y, z, stoneBrick(x, y, z));
      };
      switch (p.kind) {
        case SKind::Corridor: case SKind::ChestCorridor:
          if (pass == 0) shell(0, p.len - 1, -2, 2, 5);
          else if (pass == 1) f.fill(b, 0, p.len - 1, -1, 1, fy + 1, fy + 3, air);
          else {
            for (int u = 1; u < p.len; u += 4) {
              int wx, wz;
              f.at(u, 1, wx, wz);
              b.set(wx, fy + 2, wz, makeState(B::torch, torchMeta(f.facing(0, -1))));
            }
            if (p.kind == SKind::ChestCorridor) {
              int wx, wz;
              f.at(p.len - 1, 0, wx, wz);
              b.set(wx, fy + 1, wz, makeState(54, f.facing(-1, 0)));
              b.tile(wx, fy + 1, wz, GenTile::LootChest, 3);
              f.fill(b, p.len, p.len, -1, 1, fy + 1, fy + 3, stoneBrick(f.x, fy, f.z));  // cerrado al fondo
            }
          }
          break;
        case SKind::Room:
          if (pass == 0) shell(0, 10, -6, 6, 6);
          else if (pass == 1) {
            f.fill(b, 1, 9, -5, 5, fy + 1, fy + 5, air);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, air);
            f.fill(b, 10, 10, -1, 1, fy + 1, fy + 3, air);
            f.fill(b, 4, 6, -6, -6, fy + 1, fy + 3, air);
            f.fill(b, 4, 6, 6, 6, fy + 1, fy + 3, air);
          } else {
            for (int su : {3, 7})
              for (int sv : {-2, 2}) {
                f.fill(b, su, su, sv, sv, fy + 1, fy + 5, makeState(B::stonebrick, 3));
                int wx, wz;
                f.at(su, sv, wx, wz);
                b.set(wx, fy + 3, wz, stoneBrick(wx, fy + 3, wz));
              }
            int wx, wz;
            f.at(5, 0, wx, wz);
            b.set(wx, fy + 1, wz, makeState(B::cobblestone));
            b.set(wx, fy + 2, wz, makeState(B::torch, 5));
            if (prng.chance(0.4)) {
              f.at(5, 3, wx, wz);
              b.set(wx, fy + 1, wz, makeState(54, f.facing(0, -1)));
              b.tile(wx, fy + 1, wz, GenTile::LootChest, 5);
            }
          }
          break;
        case SKind::Library:
          if (pass == 0) shell(0, 12, -6, 6, 8);
          else if (pass == 1) {
            f.fill(b, 1, 11, -5, 5, fy + 1, fy + 7, air);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, air);
            f.fill(b, 12, 12, -1, 1, fy + 1, fy + 3, air);
          } else {
            for (int u = 2; u <= 10; u += 2)
              for (int sv : {-5, 5}) f.fill(b, u, u, sv, sv, fy + 1, fy + 3, makeState(B::bookshelf));
            for (int sv : {-4, 4})
              for (int u = 3; u <= 9; u += 2) f.fill(b, u, u, sv, sv, fy + 1, fy + 2, makeState(B::bookshelf));
            int wx, wz;
            f.at(6, 0, wx, wz);
            b.set(wx, fy + 1, wz, makeState(B::crafting_table));
            f.at(10, 3, wx, wz);
            b.set(wx, fy + 1, wz, makeState(54, f.facing(0, -1)));
            b.tile(wx, fy + 1, wz, GenTile::LootChest, 4);
            f.at(4, -3, wx, wz);
            b.set(wx, fy + 1, wz, makeState(B::web));
          }
          break;
        case SKind::Prison:
          if (pass == 0) shell(0, 8, -4, 4, 4);
          else if (pass == 1) {
            f.fill(b, 1, 7, -3, 3, fy + 1, fy + 3, air);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, air);
            f.fill(b, 8, 8, -1, 1, fy + 1, fy + 3, air);
          } else {
            for (int u : {2, 6}) {
              f.fill(b, u, u, -3, 3, fy + 1, fy + 2, makeState(101));
              f.fill(b, u, u, 0, 0, fy + 1, fy + 2, air);
            }
          }
          break;
        case SKind::Portal: {
          if (pass == 0) {
            shell(0, 15, -5, 5, 8);
          } else if (pass == 1) {
            f.fill(b, 1, 14, -4, 4, fy + 1, fy + 7, air);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, air);
            f.fill(b, 6, 12, -3, 3, fy + 1, fy + 1, makeState(B::lava));  // el foso de lava
          } else {
            // Plataforma de 5x5 con el anillo de 12 marcos (3x3 de hueco), y el generador de lepismas debajo
            f.fill(b, 7, 11, -2, 2, fy + 1, fy + 2, makeState(B::stonebrick));
            int wx, wz;
            f.at(9, 0, wx, wz);
            b.set(wx, fy + 1, wz, makeState(52));
            b.tile(wx, fy + 1, wz, GenTile::Spawner, 60);
            for (int u = 7; u <= 11; u++)
              for (int v = -2; v <= 2; v++) {
                const bool edge = u == 7 || u == 11 || v == -2 || v == 2;
                const bool corner = (u == 7 || u == 11) && (v == -2 || v == 2);
                if (!edge || corner) continue;
                int cx2, cz2, mx, mz;
                f.at(u, v, cx2, cz2);
                f.at(9, 0, mx, mz);
                const int meta = frameMeta((mx > cx2) - (mx < cx2), (mz > cz2) - (mz < cz2));
                b.set(cx2, fy + 3, cz2, makeState(120, meta | (prng.nextInt(10) == 0 ? 4 : 0)));
              }
            // Escalón de entrada a la plataforma
            f.fill(b, 5, 6, -1, 1, fy + 1, fy + 1, makeState(B::stonebrick));
            for (int u = 2; u < 14; u += 4) {
              f.at(u, -4, wx, wz);
              b.set(wx, fy + 3, wz, makeState(B::torch, torchMeta(f.facing(0, 1))));
            }
          }
          break;
        }
      }
    }
  }
}

}  // namespace mcw
