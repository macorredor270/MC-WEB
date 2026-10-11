// Terreno del Nether y del End (y el Fortín del Nether). Como el mundo de arriba, cada chunk se genera sin mirar a los vecinos.
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/random.h"
#include "data/biomes.h"
#include "world/gen_util.h"
#include "world/generator.h"
#include "world/light.h"

namespace mcw {
namespace {

constexpr u32 kSaltNetherDeco = 7101, kSaltFortress = 7102, kSaltBedrockNether = 7103, kSaltEnd = 7201;
const BlockState kAir = 0, kNetherrack = makeState(B::netherrack), kBedrock = makeState(B::bedrock), kLava = makeState(B::lava),
                 kSoulSand = makeState(B::soul_sand), kGravel = makeState(B::gravel), kGlowstone = makeState(B::glowstone),
                 kBrick = makeState(B::nether_brick), kFence = makeState(113), kEndStone = makeState(B::end_stone),
                 kObsidian = makeState(B::obsidian);

}  // namespace

// --- Nether -------------------------------------------------------------------------------------------

std::unique_ptr<Chunk> TerrainGenerator::generateNether(int cx, int cz) const {
  auto chunk = std::make_unique<Chunk>(cx, cz);
  Chunk& c = *chunk;
  const int bx = cx * 16, bz = cz * 16;
  // Densidad en una rejilla de 4 bloques (cada punto una vez) y trilineal en medio: sólido donde es > 0
  constexpr int kStep = 4, kN = 5, kNy = 33;
  std::vector<float> grid(static_cast<std::size_t>(kN * kN * kNy));
  auto gi = [&](int ix, int iy, int iz) { return (static_cast<std::size_t>(iy) * kN + iz) * kN + ix; };
  for (int iy = 0; iy < kNy; iy++) {
    const double y = iy * kStep;
    const double t = std::min(1.3, std::abs(y - 66.0) / 58.0);
    const double edge = -0.12 + 1.5 * t * t * t;
    for (int iz = 0; iz < kN; iz++)
      for (int ix = 0; ix < kN; ix++) {
        const double wx = bx + ix * kStep, wz = bz + iz * kStep;
        const double n = 0.7 * cave1_.noise3(wx / 56.0, y / 28.0, wz / 56.0) + 0.45 * cave2_.noise3(wx / 22.0, y / 12.0, wz / 22.0);
        grid[gi(ix, iy, iz)] = static_cast<float>(n + edge);
      }
  }
  auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
  for (int lz = 0; lz < 16; lz++)
    for (int lx = 0; lx < 16; lx++) {
      c.setBiome(lx, lz, Biome::hell);
      const int ix = lx / kStep, iz = lz / kStep;
      const float fx = (lx % kStep) / float(kStep), fz = (lz % kStep) / float(kStep);
      Random rng(cellSeed(seed_, bx + lx, bz + lz, kSaltBedrockNether));
      for (int y = 0; y < 128; y++) {
        const int iy = y / kStep;
        const float fy = (y % kStep) / float(kStep);
        const float c00 = lerp(grid[gi(ix, iy, iz)], grid[gi(ix + 1, iy, iz)], fx);
        const float c10 = lerp(grid[gi(ix, iy + 1, iz)], grid[gi(ix + 1, iy + 1, iz)], fx);
        const float c01 = lerp(grid[gi(ix, iy, iz + 1)], grid[gi(ix + 1, iy, iz + 1)], fx);
        const float c11 = lerp(grid[gi(ix, iy + 1, iz + 1)], grid[gi(ix + 1, iy + 1, iz + 1)], fx);
        const float d = lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
        BlockState s = d > 0 ? kNetherrack : (y <= 32 ? kLava : kAir);
        // Roca madre arriba y abajo, con la capa de arriba irregular
        const int fromTop = 127 - y;
        if (y == 0 || fromTop == 0 || (y <= 4 && rng.nextInt(5) >= y) || (fromTop <= 4 && rng.nextInt(5) >= fromTop)) s = kBedrock;
        if (s != kAir) c.setBlock(lx, y, lz, s);
      }
    }
  // Arena de almas y grava en el suelo: manchas de ruido
  for (int lz = 0; lz < 16; lz++)
    for (int lx = 0; lx < 16; lx++) {
      const double p = surface_.noise2((bx + lx) / 9.0, (bz + lz) / 9.0);
      const double q = surface_.noise2((bx + lx) / 9.0 + 100.0, (bz + lz) / 9.0 + 100.0);
      for (int y = 124; y > 5; y--) {
        if (c.block(lx, y, lz) != kNetherrack || c.block(lx, y + 1, lz) != kAir) continue;
        if (y >= 28 && y <= 40 && p > 0.12) {
          c.setBlock(lx, y, lz, kSoulSand);
          if (p > 0.3 && c.block(lx, y - 1, lz) == kNetherrack) c.setBlock(lx, y - 1, lz, kSoulSand);
        } else if (q < -0.18 && y > 20) {
          c.setBlock(lx, y, lz, kGravel);
        }
      }
    }
  Random rng(cellSeed(seed_, cx, cz, kSaltNetherDeco));
  // Cuarzo
  for (int v = 0; v < 16; v++) {
    int x = rng.nextInt(16), y = 10 + rng.nextInt(108), z = rng.nextInt(16);
    for (int i = 0; i < 14; i++) {
      if (x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < 127 && c.block(x, y, z) == kNetherrack) c.setBlock(x, y, z, makeState(153));
      switch (rng.nextInt(6)) {
        case 0: x++; break;
        case 1: x--; break;
        case 2: y++; break;
        case 3: y--; break;
        case 4: z++; break;
        default: z--; break;
      }
    }
  }
  // Cúmulos de piedra luminosa colgando del techo
  for (int t = 0; t < 14; t++) {
    const int x = rng.nextInt(16), z = rng.nextInt(16);
    int y = 122;
    while (y > 8 && !(c.block(x, y, z) == kAir && c.block(x, y + 1, z) == kNetherrack)) y--;
    if (y <= 8) continue;
    int px = x, py = y, pz = z;
    for (int i = 0, n = 6 + rng.nextInt(8); i < n; i++) {
      if (px >= 0 && px < 16 && pz >= 0 && pz < 16 && c.block(px, py, pz) == kAir) c.setBlock(px, py, pz, kGlowstone);
      px += rng.nextInt(3) - 1;
      pz += rng.nextInt(3) - 1;
      py -= rng.nextInt(2);
    }
  }
  // Fuego, setas y chorros de lava en las paredes
  for (int t = 0; t < 14; t++) {
    const int x = rng.nextInt(16), z = rng.nextInt(16);
    for (int y = 120; y > 33; y--)
      if (c.block(x, y, z) == kNetherrack && c.block(x, y + 1, z) == kAir) {
        const int roll = rng.nextInt(8);
        c.setBlock(x, y + 1, z, roll < 5 ? makeState(51) : roll < 6 ? makeState(B::brown_mushroom) : makeState(B::red_mushroom));
        break;
      }
  }
  for (int t = 0; t < 18; t++) {
    const int x = 1 + rng.nextInt(14), z = 1 + rng.nextInt(14), y = 8 + rng.nextInt(110);
    if (c.block(x, y, z) != kNetherrack) continue;
    int rock = 0, air = 0;
    constexpr int kD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& d : kD) {
      const BlockState n = c.block(x + d[0], y + d[1], z + d[2]);
      if (n == kNetherrack) rock++;
      else if (n == kAir) air++;
    }
    if (rock == 5 && air == 1) {
      c.setBlock(x, y, z, makeState(B::flowing_lava, 0));
      GenTile gt;
      gt.x = static_cast<i8>(x); gt.z = static_cast<i8>(z); gt.y = static_cast<u8>(y); gt.kind = GenTile::FluidTick;
      c.addGenTile(gt);
    }
  }
  if (settings_.structures) placeFortress(c);
  light::computeInitial(c);
  return chunk;
}

// --- Fortín del Nether ---------------------------------------------------------------------------------

namespace {

enum class FKind : u8 { Bridge, Corridor, Hub, Junction, WartRoom, Throne };
struct FPiece {
  FKind kind;
  Frame f;
  int len = 0;
  int fy = 50;
};

constexpr int kRegion = 192;  // bloques entre fortines

void layoutFortress(u64 seed, int rx, int rz, std::vector<FPiece>& out) {
  Random rng(cellSeed(seed, rx, rz, kSaltFortress));
  if (!rng.chance(0.7)) return;
  const int ox = rx * kRegion + 40 + rng.nextInt(112), oz = rz * kRegion + 40 + rng.nextInt(112);
  const int fy = 50 + rng.nextInt(5);
  out.push_back({FKind::Hub, Frame{ox, oz, 0}, 13, fy});
  for (int d = 0; d < 4; d++) {
    Frame f{ox, oz, d};
    int x, z;
    f.at(7, 0, x, z);
    f = Frame{x, z, d};
    const int n = 2 + rng.nextInt(3);
    for (int s = 0; s < n; s++) {
      const bool bridge = rng.chance(0.55);
      const int len = rng.chance(0.5) ? 15 : 20;
      out.push_back({bridge ? FKind::Bridge : FKind::Corridor, f, len, fy});
      f.at(len, 0, x, z);
      f = Frame{x, z, f.dir};
      if (s < n - 1 && rng.chance(0.45)) {
        out.push_back({FKind::Junction, f, 5, fy});
        int cxw, czw;
        f.at(2, 0, cxw, czw);
        const int nd = (f.dir + (rng.chance(0.5) ? 1 : 3)) & 3;
        Frame g{cxw, czw, nd};
        g.at(3, 0, x, z);
        f = Frame{x, z, nd};
      }
    }
    const double r = rng.next();
    if (r < 0.45) out.push_back({FKind::WartRoom, f, 13, fy});
    else if (r < 0.85) out.push_back({FKind::Throne, f, 9, fy});
  }
}

}  // namespace

void TerrainGenerator::placeFortress(Chunk& c) const {
  ChunkBuilder b(c);
  const int rx0 = static_cast<int>(std::floor((b.bx - 150) / double(kRegion))), rx1 = static_cast<int>(std::floor((b.bx + 165) / double(kRegion)));
  const int rz0 = static_cast<int>(std::floor((b.bz - 150) / double(kRegion))), rz1 = static_cast<int>(std::floor((b.bz + 165) / double(kRegion)));
  std::vector<FPiece> pieces;
  for (int rz = rz0; rz <= rz1; rz++)
    for (int rx = rx0; rx <= rx1; rx++) layoutFortress(seed_, rx, rz, pieces);
  if (pieces.empty()) return;

  auto bounds = [&](const FPiece& p, int& x0, int& z0, int& x1, int& z1) {
    int ax, az, bx2, bz2;
    const int r = p.kind == FKind::Hub ? 8 : 8;
    p.f.at(-r, -r, ax, az);
    p.f.at(p.len + r, r, bx2, bz2);
    if (p.kind == FKind::Hub) {
      ax = p.f.x - 9; az = p.f.z - 9; bx2 = p.f.x + 9; bz2 = p.f.z + 9;
    }
    x0 = std::min(ax, bx2); x1 = std::max(ax, bx2); z0 = std::min(az, bz2); z1 = std::max(az, bz2);
  };
  std::vector<const FPiece*> near;
  for (const FPiece& p : pieces) {
    int x0, z0, x1, z1;
    bounds(p, x0, z0, x1, z1);
    if (b.touches(x0, z0, x1, z1)) near.push_back(&p);
  }
  if (near.empty()) return;

  const auto pillar = [&](const Frame& f, int u, int v, int fromY) {
    int wx, wz;
    f.at(u, v, wx, wz);
    if (!b.inside(wx, wz)) return;
    for (int y = fromY; y >= 31; y--) {
      const int id = stateId(b.get(wx, y, wz));
      if (id != 0 && !isFluid(id)) break;
      b.set(wx, y, wz, kBrick);
    }
  };

  // Pasada 0: cáscaras y suelos; 1: interiores vacíos; 2: detalles
  for (int pass = 0; pass < 3; pass++) {
    for (const FPiece* pp : near) {
      const FPiece& p = *pp;
      const Frame& f = p.f;
      const int fy = p.fy;
      Random prng(cellSeed(seed_, f.x, f.z, kSaltFortress + static_cast<u32>(p.kind) + static_cast<u32>(p.len)));
      switch (p.kind) {
        case FKind::Bridge:
          if (pass == 0) {
            f.fill(b, 0, p.len - 1, -2, 2, fy - 1, fy, kBrick);
            f.fill(b, 0, p.len - 1, -2, -2, fy + 1, fy + 1, kFence);
            f.fill(b, 0, p.len - 1, 2, 2, fy + 1, fy + 1, kFence);
            for (int u = 1; u < p.len; u += 6) {
              pillar(f, u, -2, fy - 2);
              pillar(f, u, 2, fy - 2);
            }
          }
          break;
        case FKind::Corridor:
          if (pass == 0) {
            f.fill(b, 0, p.len - 1, -2, 2, fy - 1, fy + 5, kBrick);
          } else if (pass == 1) {
            f.fill(b, 0, p.len - 1, -1, 1, fy + 1, fy + 4, kAir);
          } else {
            for (int u = 2; u < p.len; u += 4) {  // ventanas con barandilla
              f.fill(b, u, u, -2, -2, fy + 2, fy + 3, kFence);
              f.fill(b, u, u, 2, 2, fy + 2, fy + 3, kFence);
            }
            if (p.len >= 15 && prng.chance(0.5)) {
              int wx, wz;
              f.at(p.len / 2, 1, wx, wz);
              b.set(wx, fy + 1, wz, makeState(54, f.facing(0, -1)));
              b.tile(wx, fy + 1, wz, GenTile::LootChest, 2);
            }
          }
          break;
        case FKind::Hub: {
          // f está en el centro; el suelo de 13x13 va de -6 a 6
          if (pass == 0) {
            f.fill(b, -6, 6, -6, 6, fy - 1, fy, kBrick);
            for (int s = -6; s <= 6; s++) {
              if (std::abs(s) <= 1) continue;
              f.fill(b, s, s, -6, -6, fy + 1, fy + 1, kFence);
              f.fill(b, s, s, 6, 6, fy + 1, fy + 1, kFence);
              f.fill(b, -6, -6, s, s, fy + 1, fy + 1, kFence);
              f.fill(b, 6, 6, s, s, fy + 1, fy + 1, kFence);
            }
            for (int su : {-6, 6})
              for (int sv : {-6, 6}) f.fill(b, su, su, sv, sv, fy + 1, fy + 5, kBrick);
            for (int su : {-5, 0, 5})
              for (int sv : {-5, 0, 5}) pillar(f, su, sv, fy - 2);
          }
          break;
        }
        case FKind::Junction:
          if (pass == 0) {
            f.fill(b, 0, 4, -2, 2, fy - 1, fy, kBrick);
            for (int su : {0, 4})
              for (int sv : {-2, 2}) f.fill(b, su, su, sv, sv, fy + 1, fy + 3, kBrick);
            pillar(f, 2, 0, fy - 2);
          }
          break;
        case FKind::WartRoom:
          if (pass == 0) {
            f.fill(b, 0, 12, -6, 6, fy - 1, fy + 8, kBrick);
          } else if (pass == 1) {
            f.fill(b, 1, 11, -5, 5, fy + 1, fy + 7, kAir);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, kAir);  // la puerta
          } else {
            for (int u = 3; u <= 9; u += 2)
              for (int v = -3; v <= 3; v++) {
                f.set(b, u, v, fy, kSoulSand);
                f.set(b, u, v, fy + 1, makeState(115, prng.nextInt(4)));
              }
            for (int su : {1, 11})
              for (int sv : {-5, 5}) f.fill(b, su, su, sv, sv, fy + 1, fy + 6, kBrick);
          }
          break;
        case FKind::Throne:
          if (pass == 0) {
            f.fill(b, 0, 8, -4, 4, fy - 1, fy + 6, kBrick);
          } else if (pass == 1) {
            f.fill(b, 1, 7, -3, 3, fy + 1, fy + 5, kAir);
            f.fill(b, 0, 0, -1, 1, fy + 1, fy + 3, kAir);
          } else {
            for (int du = -1; du <= 1; du++)
              for (int dv = -1; dv <= 1; dv++) {
                if (!du && !dv) continue;
                f.fill(b, 5 + du, 5 + du, dv, dv, fy + 1, fy + 2, kFence);
              }
            f.set(b, 5, 0, fy + 1, makeState(52));
            int wx, wz;
            f.at(5, 0, wx, wz);
            b.tile(wx, fy + 1, wz, GenTile::Spawner, 61);  // blaze
            f.at(7, 2, wx, wz);
            b.set(wx, fy + 1, wz, makeState(54, f.facing(0, -1)));
            b.tile(wx, fy + 1, wz, GenTile::LootChest, 2);
          }
          break;
      }
    }
  }
}

// --- El End --------------------------------------------------------------------------------------------

std::unique_ptr<Chunk> TerrainGenerator::generateEnd(int cx, int cz) const {
  auto chunk = std::make_unique<Chunk>(cx, cz);
  Chunk& c = *chunk;
  const int bx = cx * 16, bz = cz * 16;
  for (int lz = 0; lz < 16; lz++)
    for (int lx = 0; lx < 16; lx++) {
      c.setBiome(lx, lz, Biome::the_end);
      const double wx = bx + lx, wz = bz + lz;
      const double d = std::sqrt(wx * wx + wz * wz);
      const double angle = std::atan2(wz, wx);
      // Contorno irregular de la isla: radio entre ~58 y ~74 bloques
      const double radius = 64.0 + 8.0 * surface_.noise2(std::cos(angle) * 2.0, std::sin(angle) * 2.0) + 3.0 * detail_.noise2(wx / 30.0, wz / 30.0);
      if (d >= radius) continue;
      const double k = d / radius;
      const double top = 62.0 + 2.0 * detail_.noise2(wx / 25.0, wz / 25.0) * (1.0 - k) - 3.0 * k * k;
      const double thick = 26.0 * std::sqrt(std::max(0.0, 1.0 - k * k)) + 3.0 * cavern_.noise2(wx / 16.0, wz / 16.0) * (1.0 - k);
      const int yTop = static_cast<int>(std::floor(top)), yBot = std::max(1, static_cast<int>(std::floor(top - thick)));
      for (int y = yBot; y <= yTop; y++) c.setBlock(lx, y, lz, kEndStone);
    }
  // Las torres de obsidiana: diez, en círculo, con un cristal encima (algunas dentro de una jaula)
  ChunkBuilder b(c);
  for (int i = 0; i < 10; i++) {
    const double a = i * 2.0 * 3.14159265358979 / 10.0;
    const int tx = static_cast<int>(std::lround(42.0 * std::cos(a))), tz = static_cast<int>(std::lround(42.0 * std::sin(a)));
    Random rng(cellSeed(seed_, i, 99, kSaltEnd));
    const int radius = 2 + (i * 7 % 4);  // 2..5
    const int height = 76 + (i * 5 % 10) * 3;
    if (!b.touches(tx - radius - 2, tz - radius - 2, tx + radius + 2, tz + radius + 2)) continue;
    for (int dz = -radius; dz <= radius; dz++)
      for (int dx = -radius; dx <= radius; dx++) {
        if (dx * dx + dz * dz > radius * radius + 1) continue;
        for (int y = 40; y <= height; y++) b.set(tx + dx, y, tz + dz, kObsidian);
      }
    b.set(tx, height, tz, kBedrock);
    b.set(tx, height + 1, tz, makeState(51));
    b.tile(tx, height + 1, tz, GenTile::EndCrystal, 0);
    if (rng.chance(0.5) || radius <= 2) {  // jaula de barrotes alrededor del cristal
      for (int dy = 1; dy <= 3; dy++) {
        for (int d = -2; d <= 2; d++) {
          b.set(tx + d, height + dy, tz - 2, makeState(101));
          b.set(tx + d, height + dy, tz + 2, makeState(101));
          b.set(tx - 2, height + dy, tz + d, makeState(101));
          b.set(tx + 2, height + dy, tz + d, makeState(101));
        }
      }
      for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) b.set(tx + dx, height + 4, tz + dz, makeState(101));
    }
  }
  light::computeInitial(c);
  return chunk;
}

}  // namespace mcw
