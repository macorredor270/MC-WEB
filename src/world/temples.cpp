// Templos del desierto, cabañas de brujas en los pantanos y pozos del desierto (comportamiento observable de 1.8 según
// minecraft.wiki; las construcciones son propias). Como las aldeas, se reconstruyen en cada chunk a partir de la semilla.
#include <algorithm>
#include <cmath>

#include "core/random.h"
#include "data/biomes.h"
#include "world/gen_util.h"
#include "world/generator.h"

namespace mcw {
namespace {

constexpr u32 kSaltTemple = 8401, kSaltWell = 8402;
constexpr int kTempleRegion = 32;

/// Dibujo en el marco de una estructura: coordenadas locales (u, v) y alturas relativas a `base`.
struct Pen {
  ChunkBuilder& b;
  Frame f;
  int base;
  void put(int u, int v, int y, BlockState s) const { f.set(b, u, v, base + y, s); }
  void box(int u0, int u1, int v0, int v1, int y0, int y1, BlockState s) const { f.fill(b, u0, u1, v0, v1, base + y0, base + y1, s); }
  void torch(int u, int v, int y, int outU, int outV) const {
    const int facing = f.facing(outU, outV);
    put(u, v, y, makeState(B::torch, facing == 5 ? 1 : facing == 4 ? 2 : facing == 3 ? 3 : 4));
  }
  void chest(int u, int v, int y, int faceU, int faceV, int table) const {
    int wx, wz;
    f.at(u, v, wx, wz);
    b.set(wx, base + y, wz, makeState(54, f.facing(faceU, faceV)));
    b.tile(wx, base + y, wz, GenTile::LootChest, table);
  }
};

}  // namespace

void TerrainGenerator::placeTemples(Chunk& c, const ColumnInfo* cols) const {
  ChunkBuilder b(c);
  const int bx0 = b.bx, bz0 = b.bz;
  auto groundAt = [&](int x, int z) -> int {
    if (b.inside(x, z)) return cols[(z - bz0) * 16 + (x - bx0)].height - 1;
    return column(x, z).height - 1;
  };
  const int reach = 20;
  const int rx0 = static_cast<int>(std::floor((bx0 - reach) / (16.0 * kTempleRegion))), rx1 = static_cast<int>(std::floor((bx0 + 15 + reach) / (16.0 * kTempleRegion)));
  const int rz0 = static_cast<int>(std::floor((bz0 - reach) / (16.0 * kTempleRegion))), rz1 = static_cast<int>(std::floor((bz0 + 15 + reach) / (16.0 * kTempleRegion)));
  for (int rz = rz0; rz <= rz1; rz++)
    for (int rx = rx0; rx <= rx1; rx++) {
      Random rng(cellSeed(seed_, rx, rz, kSaltTemple));
      const int sx = (rx * kTempleRegion + rng.nextInt(24)) * 16 + 8, sz = (rz * kTempleRegion + rng.nextInt(24)) * 16 + 8;
      if (!b.touches(sx - reach, sz - reach, sx + reach, sz + reach)) continue;
      const ColumnInfo center = column(sx, sz);
      if (center.river || center.height <= kSeaLevel) continue;
      const int dir = rng.nextInt(4);
      if (center.biome == Biome::desert) {
        // --- Templo del desierto: pirámide escalonada de 21x21 con dos torres delante y una cámara trampa debajo ---
        const Frame f{sx - 10, sz - 10, 0};
        int lo = 1 << 20, hi = 0;
        for (int du : {0, 10, 20})
          for (int dv : {0, 10, 20}) {
            const ColumnInfo ci = column(sx - 10 + du, sz - 10 + dv);
            lo = std::min(lo, ci.height);
            hi = std::max(hi, ci.height);
          }
        const int base = (lo + hi) / 2 - 1;
        Pen p{b, f, base};
        const BlockState sand = makeState(B::sandstone), chis = makeState(B::sandstone, 1), smooth = makeState(B::sandstone, 2);
        // Preparar el terreno: cimientos y todo lo de encima despejado
        for (int z = std::max(sz - 11, bz0); z <= std::min(sz + 11, bz0 + 15); z++)
          for (int x = std::max(sx - 11, bx0); x <= std::min(sx + 11, bx0 + 15); x++) {
            const int gy = groundAt(x, z);
            for (int y = std::min(gy, base) - 0; y <= base; y++) b.set(x, y, z, sand);
            for (int y = base + 1; y <= std::max(gy, base + 18); y++)
              if (stateId(b.get(x, y, z)) != 0) b.set(x, y, z, 0);
          }
        // Pirámide: capas cada vez más pequeñas
        for (int k = 0; k < 10; k++) p.box(k, 20 - k, k, 20 - k, 1 + k, 1 + k, sand);
        // Dos torres delante (en u = 0), más altas que el cuerpo
        for (int v : {0, 16}) {
          p.box(0, 4, v, v + 4, 1, 9, sand);
          p.box(0, 4, v, v + 4, 10, 10, smooth);
          p.box(1, 3, v + 1, v + 3, 10, 10, sand);
          p.box(2, 2, v + 2, v + 2, 11, 11, chis);
        }
        // Sala principal hueca y entrada
        p.box(5, 15, 5, 15, 1, 5, 0);
        p.box(0, 5, 9, 11, 1, 3, 0);
        // Suelo de la sala con dibujo azul y naranja, y zócalo cincelado
        p.box(5, 15, 5, 15, 0, 0, makeState(159, 1));
        for (int i = 5; i <= 15; i++) {
          p.put(i, 10, 0, makeState(159, 11));
          p.put(10, i, 0, makeState(159, 11));
        }
        p.box(5, 15, 5, 5, 1, 1, chis);
        p.box(5, 15, 15, 15, 1, 1, chis);
        p.box(5, 5, 5, 15, 1, 1, chis);
        p.box(15, 15, 5, 15, 1, 1, chis);
        p.put(10, 10, 0, makeState(159, 1));
        // La cámara trampa: pozo en el centro de la sala, cuatro cofres y una placa de presión sobre nueve bloques de dinamita
        p.box(7, 13, 7, 13, -10, -1, sand);
        p.box(8, 12, 8, 12, -8, -5, 0);
        p.box(10, 10, 10, 10, -4, -1, 0);
        p.box(9, 11, 9, 11, -9, -9, makeState(B::tnt));
        p.put(10, 10, -8, makeState(70));  // placa de presión de piedra
        p.chest(8, 10, -8, 1, 0, 7);
        p.chest(12, 10, -8, -1, 0, 7);
        p.chest(10, 8, -8, 0, 1, 7);
        p.chest(10, 12, -8, 0, -1, 7);
        // Antorchas en la sala
        for (int v : {7, 13}) {
          p.torch(5, v, 3, 1, 0);
          p.torch(15, v, 3, -1, 0);
        }
        (void)dir;
      } else if (center.biome == Biome::swamp) {
        // --- Cabaña de bruja: sobre pilotes, 7x9, con caldero y mesa de trabajo ---
        const Frame f = [&] {
          Frame ff{sx - 3, sz - 4, 0};
          return ff;
        }();
        const int base = std::max(center.height, kSeaLevel + 1);
        Pen p{b, f, base};
        const BlockState planks = makeState(B::planks, 0), log = makeState(B::log, 0), spruceStair = makeState(134, 0);
        // Pilotes hasta el fondo y despejar encima
        for (int u : {0, 6})
          for (int v : {0, 8}) {
            int wx, wz;
            f.at(u, v, wx, wz);
            for (int y = base; y > 1; y--) {
              const int id = stateId(b.get(wx, y, wz));
              if (y < base && id != 0 && !isFluid(id)) break;
              b.set(wx, y, wz, log);
            }
          }
        p.box(0, 6, 0, 8, 0, 0, planks);
        p.box(0, 6, 0, 8, 1, 6, 0);
        for (int u = 0; u <= 6; u++)
          for (int v = 0; v <= 8; v++)
            if (u == 0 || u == 6 || v == 0 || v == 8) p.box(u, u, v, v, 1, 3, planks);
        for (int u : {0, 6})
          for (int v : {0, 8}) p.box(u, u, v, v, 1, 3, log);
        p.box(0, 6, 0, 8, 4, 4, planks);
        // Tejado a dos aguas de abeto
        for (int k = 0; k < 3; k++) {
          p.box(k, k, -1, 9, 4 + k, 4 + k, makeState(134, 0));
          p.box(6 - k, 6 - k, -1, 9, 4 + k, 4 + k, makeState(134, 1));
        }
        p.box(3, 3, -1, 9, 7, 7, planks);
        (void)spruceStair;
        p.put(3, 0, 1, makeState(64, 1));
        p.put(3, 0, 2, makeState(64, 8));
        p.put(0, 4, 2, makeState(85));
        p.put(6, 4, 2, makeState(85));
        p.put(1, 7, 1, makeState(B::crafting_table));
        p.put(5, 7, 1, makeState(118, 3));  // caldero con agua
        p.put(1, 1, 1, makeState(140));      // maceta
        int wx, wz;
        f.at(3, 4, wx, wz);
        b.tile(wx, base + 1, wz, GenTile::Mob, 66, 0);
      }
    }
  // --- Pozos del desierto: uno de cada mil chunks de desierto ---
  Random wellRng(cellSeed(seed_, c.pos().x, c.pos().z, kSaltWell));
  if (wellRng.nextInt(1000) == 0) {
    const int wx0 = bx0 + 4 + wellRng.nextInt(8), wz0 = bz0 + 4 + wellRng.nextInt(8);
    const ColumnInfo ci = cols[(wz0 - bz0) * 16 + (wx0 - bx0)];
    if (ci.biome == Biome::desert && ci.height > kSeaLevel) {
      const int base = ci.height - 1;
      const BlockState sand = makeState(B::sandstone);
      for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) {
          b.fill(wx0 + dx, base - 3, wz0 + dz, wx0 + dx, base - 1, wz0 + dz, sand);
          b.set(wx0 + dx, base, wz0 + dz, sand);
          for (int y = base + 1; y <= base + 4; y++) b.set(wx0 + dx, y, wz0 + dz, 0);
        }
      b.fill(wx0 - 1, base, wz0 - 1, wx0 + 1, base, wz0 + 1, makeState(B::water));
      b.fill(wx0 - 1, base - 2, wz0 - 1, wx0 + 1, base - 1, wz0 + 1, makeState(B::water));
      for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++)
          if (std::abs(dx) == 2 || std::abs(dz) == 2) b.set(wx0 + dx, base + 1, wz0 + dz, makeState(44, 1));  // losa de arenisca
      b.fill(wx0 - 1, base + 1, wz0 - 1, wx0 + 1, base + 1, wz0 + 1, makeState(B::sandstone));
      b.fill(wx0 - 1, base + 1, wz0 - 1, wx0 + 1, base + 1, wz0 + 1, 0);
      b.fill(wx0 - 1, base + 4, wz0 - 1, wx0 + 1, base + 4, wz0 + 1, makeState(44, 1));
      for (int dz : {-1, 1})
        for (int dx : {-1, 1}) b.fill(wx0 + dx, base + 1, wz0 + dz, wx0 + dx, base + 3, wz0 + dz, sand);
    }
  }
}

}  // namespace mcw
