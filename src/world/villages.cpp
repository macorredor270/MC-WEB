// Aldeas: un pozo en el centro, caminos y edificios con sus aldeanos, a la manera de 1.8 (comportamiento observable de
// minecraft.wiki; los edificios y la disposición son propios). Cada chunk repite el mismo plan a partir de la semilla y solo
// dibuja lo que cae en él.
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/random.h"
#include "data/biomes.h"
#include "world/gen_util.h"
#include "world/generator.h"

namespace mcw {
namespace {

constexpr u32 kSaltVillage = 8301;
constexpr int kRegionChunks = 32;

enum class VKind : u8 { Well, SmallHouse, Hut, Butcher, Library, Church, Smithy, FarmA, FarmB, Lamp };

struct VPiece {
  VKind kind;
  Frame f;   // origen en la esquina del lado del camino; +u entra al edificio y +v sigue el camino
  int w = 1, l = 1;  // ancho (v) y fondo (u)
  int base = 64;     // y del suelo del edificio (el bloque sobre el que se camina es base + 1)
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // caja ocupada en el mundo
};

struct VRoad {
  int x0, z0, x1, z1;
};

struct VPlan {
  int cx = 0, cz = 0, biome = 1;
  std::vector<VPiece> pieces;
  std::vector<VRoad> roads;
  int golemX = 0, golemZ = 0, golemY = 0;
};

struct Palette {
  bool sand = false;       // desierto: todo de arenisca
  int logId = 17, logMeta = 0;
  BlockState planks = makeState(B::planks, 0);
  int stairs = 53, fence = 85, door = 64;
  BlockState cobble = makeState(B::cobblestone);
  BlockState path = makeState(B::gravel);
  BlockState log(int axis) const {  // eje 0 vertical, 1 este-oeste, 2 norte-sur
    if (sand) return makeState(B::sandstone, 2);
    return makeState(logId, logMeta | (axis == 1 ? 4 : axis == 2 ? 8 : 0));
  }
};

Palette paletteFor(int biome) {
  Palette p;
  if (biome == Biome::desert) {
    p.sand = true;
    p.planks = makeState(B::sandstone, 0);
    p.stairs = 128;
    p.cobble = makeState(B::sandstone, 0);
    p.path = makeState(B::sandstone, 0);
  } else if (biome == Biome::savanna) {
    p.logId = B::log2;
    p.planks = makeState(B::planks, 4);
    p.stairs = 163;
    p.fence = 192;
    p.door = 196;
  }
  return p;
}

/// Un bloque de escalera que sube hacia la dirección local (du, dv).
BlockState stairAsc(const Palette& pal, const Frame& f, int du, int dv) {
  const int facing = f.facing(du, dv);  // 2 norte, 3 sur, 4 oeste, 5 este
  const int meta = facing == 5 ? 0 : facing == 4 ? 1 : facing == 3 ? 2 : 3;
  return makeState(pal.stairs, meta);
}

/// El marco de una pieza cuyo origen está en (ox, oz), con +u hacia (ux, uz) y +v hacia (vx, vz) (vectores unitarios del mundo).
Frame frameFor(int ox, int oz, int ux, int uz, int vx, int vz) {
  for (int d = 0; d < 4; d++) {
    Frame f{ox, oz, d};
    int ax, az, bx, bz;
    f.at(1, 0, ax, az);
    f.at(0, 1, bx, bz);
    if (ax - ox == ux && az - oz == uz && bx - ox == vx && bz - oz == vz) return f;
  }
  return Frame{ox, oz, 0};
}

struct Draw {
  ChunkBuilder& b;
  const Frame& f;
  int base;
  const Palette& pal;
  void put(int u, int v, int y, BlockState s) const { f.set(b, u, v, base + y, s); }
  void box(int u0, int u1, int v0, int v1, int y0, int y1, BlockState s) const { f.fill(b, u0, u1, v0, v1, base + y0, base + y1, s); }
  void ring(int u0, int u1, int v0, int v1, int y0, int y1, BlockState s) const {
    box(u0, u1, v0, v0, y0, y1, s);
    box(u0, u1, v1, v1, y0, y1, s);
    box(u0, u0, v0, v1, y0, y1, s);
    box(u1, u1, v0, v1, y0, y1, s);
  }
  /// Puerta en la pared de entrada (u = 0), de fuera hacia dentro.
  void door(int v) const {
    const int facing = f.facing(1, 0);
    const int h = facing == 3 ? 0 : facing == 4 ? 1 : facing == 2 ? 2 : 3;
    static const int kDoor[4] = {1, 2, 3, 0};
    put(0, v, 1, makeState(pal.door, kDoor[h]));
    put(0, v, 2, makeState(pal.door, 8));
  }
  void torch(int u, int v, int y, int outU, int outV) const { put(u, v, y, makeState(B::torch, torchMetaFor(f.facing(outU, outV)))); }
  static int torchMetaFor(int facing) { return facing == 5 ? 1 : facing == 4 ? 2 : facing == 3 ? 3 : 4; }
  void chest(int u, int v, int y, int faceU, int faceV, int table) const {
    int wx, wz;
    f.at(u, v, wx, wz);
    b.set(wx, base + y, wz, makeState(54, f.facing(faceU, faceV)));
    b.tile(wx, base + y, wz, GenTile::LootChest, table);
  }
  void villager(int u, int v, int profession) const {
    int wx, wz;
    f.at(u, v, wx, wz);
    b.tile(wx, base + 1, wz, GenTile::Mob, 120, profession);
  }
  void bed(int u, int v, int headU, int headV) const {
    const int facing = f.facing(headU, headV);
    const int h = facing == 3 ? 0 : facing == 4 ? 1 : facing == 2 ? 2 : 3;
    put(u, v, 1, makeState(26, h));
    put(u + headU, v + headV, 1, makeState(26, 8 | h));
  }
  /// Tejado a dos aguas sobre una caja de `l` de fondo y `w` de ancho (w impar), con el alero a la altura `y`.
  void gable(int l, int w, int y, int overU = 0, int stairId = -1, BlockState fill = 0) const {
    const int layers = (w + 1) / 2;
    const Palette roofPal = [&] {
      Palette r = pal;
      if (stairId >= 0) {
        r.stairs = stairId;
        r.planks = fill;
      }
      return r;
    }();
    const Palette& pal = roofPal;
    for (int k = 0; k < layers; k++) {
      const int v0 = k, v1 = w - 1 - k;
      if (v0 > v1) break;
      if (v0 == v1) {
        box(-overU, l - 1 + overU, v0, v0, y + k, y + k, pal.planks);
      } else {
        box(-overU, l - 1 + overU, v0, v0, y + k, y + k, stairAsc(pal, f, 0, 1));
        box(-overU, l - 1 + overU, v1, v1, y + k, y + k, stairAsc(pal, f, 0, -1));
        if (v1 - v0 >= 2) box(-overU, l - 1 + overU, v0 + 1, v1 - 1, y + k, y + k, pal.planks);
      }
    }
  }
  /// Paredes de tablones con los troncos en las esquinas, suelo de tablones y el interior despejado.
  void shell(int l, int w, int wallH) const {
    box(0, l - 1, 0, w - 1, 0, 0, pal.cobble);
    box(1, l - 2, 1, w - 2, 0, 0, pal.planks);
    box(0, l - 1, 0, w - 1, 1, wallH, 0);
    ring(0, l - 1, 0, w - 1, 1, wallH, pal.planks);
    for (int u : {0, l - 1})
      for (int v : {0, w - 1}) box(u, u, v, v, 0, wallH, pal.log(0));
    box(0, l - 1, 0, w - 1, wallH + 1, wallH + 1, pal.planks);  // techo plano bajo el tejado
  }
};

}  // namespace

std::optional<VillageSite> TerrainGenerator::villageSite(int rx, int rz) const {
  // La región y el chunk de la prueba son los de 1.8; el bioma se mira en la celda de 4x4 de (2, 2) del chunk (y, como en 1.8, en ese bloque)
  const legacy::StructPos a = legacy::villageAttempt(seed_, rx, rz);
  const int sx = a.x + 8, sz = a.z + 8;
  auto villageBiome = [](int b) { return b == Biome::plains || b == Biome::desert || b == Biome::savanna; };
  if (settings_.type != WorldType::Flat) {
    std::vector<int> cell;
    terrain_->layers().area4((a.x >> 4) * 4 + 2, (a.z >> 4) * 4 + 2, 1, 1, cell);
    if (!villageBiome(cell[0]) || !villageBiome(biomeAt(a.x + 2, a.z + 2))) return std::nullopt;
  }
  const int biome = biomeAt(sx, sz);
  if (!villageBiome(biome)) return std::nullopt;
  const ColumnInfo center = column(sx, sz);
  if (settings_.type != WorldType::Flat && (center.river || center.height <= kSeaLevel + 1)) return std::nullopt;
  return VillageSite{sx, sz, center.biome};
}

void TerrainGenerator::placeVillages(Chunk& c, const ColumnInfo* cols) const {
  ChunkBuilder b(c);
  const int bx0 = b.bx, bz0 = b.bz;
  // Regiones de 32x32 chunks que pueden tener una aldea que llegue a este chunk (hasta unos 90 bloques del centro)
  const int reach = 96;
  const int rx0 = static_cast<int>(std::floor((bx0 - reach) / (16.0 * kRegionChunks))), rx1 = static_cast<int>(std::floor((bx0 + 15 + reach) / (16.0 * kRegionChunks)));
  const int rz0 = static_cast<int>(std::floor((bz0 - reach) / (16.0 * kRegionChunks))), rz1 = static_cast<int>(std::floor((bz0 + 15 + reach) / (16.0 * kRegionChunks)));
  auto groundAt = [&](int x, int z) -> int {
    if (b.inside(x, z)) return cols[(z - bz0) * 16 + (x - bx0)].height - 1;
    return column(x, z).height - 1;
  };
  for (int rz = rz0; rz <= rz1; rz++)
    for (int rx = rx0; rx <= rx1; rx++) {
      Random rng(cellSeed(seed_, rx, rz, kSaltVillage));  // (la forma de la aldea; el sitio lo fija 1.8)
      const legacy::StructPos attempt = legacy::villageAttempt(seed_, rx, rz);
      const int sx = attempt.x + 8, sz = attempt.z + 8;
      if (!b.touches(sx - reach, sz - reach, sx + reach, sz + reach)) continue;
      const auto site = villageSite(rx, rz);
      if (!site) continue;
      const ColumnInfo center = column(sx, sz);
      const Palette pal = paletteFor(center.biome);
      VPlan plan;
      plan.cx = sx;
      plan.cz = sz;
      plan.biome = center.biome;

      std::vector<VRoad> occupied;
      auto overlaps = [&](int x0, int z0, int x1, int z1) {
        for (const VRoad& o : occupied)
          if (x1 + 1 >= o.x0 && x0 - 1 <= o.x1 && z1 + 1 >= o.z0 && z0 - 1 <= o.z1) return true;
        return false;
      };
      // El pozo (6x6) en el centro
      {
        VPiece well;
        well.kind = VKind::Well;
        well.w = well.l = 6;
        well.f = frameFor(sx - 3, sz - 3, 1, 0, 0, 1);
        well.base = center.height - 1;
        well.x0 = sx - 3; well.z0 = sz - 3; well.x1 = sx + 2; well.z1 = sz + 2;
        plan.pieces.push_back(well);
        occupied.push_back({well.x0, well.z0, well.x1, well.z1});
        plan.golemX = sx;
        plan.golemZ = sz + 4;
        plan.golemY = well.base + 1;
      }
      int libraries = 0, churches = 0, smithies = 0;
      static const int kDirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
      for (int arm = 0; arm < 4; arm++) {
        if (rng.nextInt(8) == 0 && arm > 1) continue;
        const int ux = kDirs[arm][0], uz = kDirs[arm][1];
        const int vx = -uz, vz = ux;  // a un lado
        const int length = 22 + rng.nextInt(24);
        // El camino (3 de ancho) desde el pozo hacia fuera
        VRoad road{std::min(sx + ux * 4, sx + ux * length) - (uz != 0 ? 1 : 0), std::min(sz + uz * 4, sz + uz * length) - (ux != 0 ? 1 : 0),
                   std::max(sx + ux * 4, sx + ux * length) + (uz != 0 ? 1 : 0), std::max(sz + uz * 4, sz + uz * length) + (ux != 0 ? 1 : 0)};
        plan.roads.push_back(road);
        occupied.push_back(road);
        // Edificios a los dos lados
        for (int side = -1; side <= 1; side += 2) {
          int cursor = 6 + rng.nextInt(3);
          while (cursor < length - 5) {
            // Elección del edificio
            const int roll = rng.nextInt(20);
            VKind kind = VKind::SmallHouse;
            int w = 5, l = 6;
            if (roll < 5) { kind = VKind::SmallHouse; w = 5; l = 6; }
            else if (roll < 8) { kind = VKind::Hut; w = 5; l = 4; }
            else if (roll < 10) { kind = VKind::Butcher; w = 7; l = 5; }
            else if (roll < 13) { kind = VKind::FarmA; w = 13; l = 4; }
            else if (roll < 15) { kind = VKind::FarmB; w = 7; l = 4; }
            else if (roll == 15 && libraries < 1) { kind = VKind::Library; w = 9; l = 7; }
            else if (roll == 16 && churches < 1) { kind = VKind::Church; w = 5; l = 9; }
            else if (roll >= 17 && roll < 19 && smithies < 2) { kind = VKind::Smithy; w = 9; l = 7; }
            else if (roll == 19) { kind = VKind::Lamp; w = 1; l = 1; }
            // Huella en el mundo: l hacia fuera del camino (a partir de 3 del eje) y w a lo largo
            const int along0 = cursor, along1 = cursor + w - 1;
            const int out0 = 3, out1 = 3 + l - 1;
            auto world = [&](int along, int out, int& wx, int& wz) {
              wx = sx + ux * along + vx * side * out;
              wz = sz + uz * along + vz * side * out;
            };
            int ax, az, bx2, bz2;
            world(along0, out0, ax, az);
            world(along1, out1, bx2, bz2);
            const int x0 = std::min(ax, bx2), x1 = std::max(ax, bx2), z0 = std::min(az, bz2), z1 = std::max(az, bz2);
            if (overlaps(x0, z0, x1, z1)) {
              cursor += 3;
              continue;
            }
            // Terreno: sin agua y poco desnivel
            int lo = 1 << 20, hi = -1;
            bool wet = false;
            for (int a : {along0, (along0 + along1) / 2, along1})
              for (int o : {out0, (out0 + out1) / 2, out1}) {
                int wx, wz;
                world(a, o, wx, wz);
                const ColumnInfo ci = column(wx, wz);
                if (settings_.type != WorldType::Flat && ci.height <= kSeaLevel) wet = true;
                lo = std::min(lo, ci.height);
                hi = std::max(hi, ci.height);
              }
            if (wet || hi - lo > 4) {
              cursor += 4;
              continue;
            }
            VPiece p;
            p.kind = kind;
            p.w = w;
            p.l = l;
            // Origen: el rincón del lado del camino; +u hacia dentro (alejándose del camino), +v a lo largo
            int ox, oz;
            world(side > 0 ? along1 : along0, out0, ox, oz);
            const int dvx = side > 0 ? -ux : ux, dvz = side > 0 ? -uz : uz;
            p.f = frameFor(ox, oz, vx * side, vz * side, dvx, dvz);
            int dx, dz;
            p.f.at(0, w / 2, dx, dz);
            p.base = groundAt(dx, dz);
            p.x0 = x0; p.z0 = z0; p.x1 = x1; p.z1 = z1;
            plan.pieces.push_back(p);
            occupied.push_back({x0, z0, x1, z1});
            if (kind == VKind::Library) libraries++;
            if (kind == VKind::Church) churches++;
            if (kind == VKind::Smithy) smithies++;
            cursor += w + 1 + rng.nextInt(3);
          }
        }
      }

      // --- Dibujar lo que cae en este chunk -----------------------------------------------------------------------
      // Caminos
      for (const VRoad& r : plan.roads) {
        if (!b.touches(r.x0, r.z0, r.x1, r.z1)) continue;
        for (int z = std::max(r.z0, bz0); z <= std::min(r.z1, bz0 + 15); z++)
          for (int x = std::max(r.x0, bx0); x <= std::min(r.x1, bx0 + 15); x++) {
            const int gy = groundAt(x, z);
            if (settings_.type != WorldType::Flat && cols[(z - bz0) * 16 + (x - bx0)].height <= kSeaLevel) continue;  // el agua se deja
            const u32 h = hash3(x, 0, z, 0xA5);
            b.set(x, gy, z, h % 5 == 0 ? pal.cobble : pal.path);
            for (int y = gy + 1; y <= gy + 3; y++) {
              const int id = stateId(b.get(x, y, z));
              if (id != 0) b.set(x, y, z, 0);
            }
          }
      }
      // Edificios
      for (const VPiece& p : plan.pieces) {
        if (!b.touches(p.x0 - 2, p.z0 - 2, p.x1 + 2, p.z1 + 2)) continue;
        Draw d{b, p.f, p.base, pal};
        Random prng(cellSeed(seed_, p.x0, p.z0, kSaltVillage + 1));
        // Preparar el suelo: cimientos hasta el nivel y todo lo de encima despejado (árboles incluidos)
        const int u0 = -1, u1 = p.l, v0 = -1, v1 = p.w;
        const bool farm = p.kind == VKind::FarmA || p.kind == VKind::FarmB;
        for (int u = u0; u <= u1; u++)
          for (int v = v0; v <= v1; v++) {
            int wx, wz;
            p.f.at(u, v, wx, wz);
            if (!b.inside(wx, wz)) continue;
            const int gy = groundAt(wx, wz);
            for (int y = p.base + 1; y <= std::max(gy, p.base + 12); y++) {
              const int id = stateId(b.get(wx, y, wz));
              if (id != 0 && !isFluid(id)) b.set(wx, y, wz, 0);
            }
            for (int y = gy + 1; y <= p.base; y++) b.set(wx, y, wz, farm ? makeState(B::dirt) : pal.cobble);
            if (gy > p.base) b.set(wx, p.base, wz, farm ? makeState(B::dirt) : pal.cobble);
          }
        switch (p.kind) {
          case VKind::Well: {
            d.box(0, 5, 0, 5, -4, -4, pal.cobble);
            d.box(0, 5, 0, 5, -3, 1, pal.cobble);
            d.box(1, 4, 1, 4, -3, 0, makeState(B::water));
            d.box(1, 4, 1, 4, 1, 1, 0);
            for (int u : {0, 5})
              for (int v : {0, 5}) d.box(u, u, v, v, 2, 4, makeState(pal.fence));
            d.box(0, 5, 0, 5, 5, 5, pal.planks);
            break;
          }
          case VKind::SmallHouse: case VKind::Hut: {
            const int wallH = p.kind == VKind::Hut ? 3 : 3;
            d.shell(p.l, p.w, wallH);
            // Ventanas y puerta
            d.put(2, 0, 2, makeState(102));
            d.put(2, p.w - 1, 2, makeState(102));
            d.put(p.l - 1, p.w / 2, 2, makeState(102));
            d.door(p.w / 2);
            if (p.kind == VKind::Hut) {
              d.box(-1, p.l, -1, p.w, wallH + 1, wallH + 1, pal.planks);  // techo plano con alero
              d.ring(0, p.l - 1, 0, p.w - 1, wallH + 1, wallH + 1, pal.log(2));
              d.chest(p.l - 2, 1, 1, 0, 1, 1);
              d.put(p.l - 2, p.w - 2, 1, makeState(B::crafting_table));
              d.torch(1, 1, 2, 0, 1);
              d.villager(2, 2, 0);
            } else {
              d.gable(p.l, p.w, wallH + 1);
              d.bed(p.l - 2, 1, 0, 1);
              d.put(p.l - 2, p.w - 2, 1, makeState(B::crafting_table));
              d.torch(1, 1, 3, 0, 1);
              d.torch(p.l - 2, p.w - 2, 3, 0, -1);
              d.put(1, p.w - 2, 1, makeState(pal.fence));
              d.put(1, p.w - 2, 2, makeState(72));
              d.villager(2, 2, 0);
              if (prng.nextInt(2) == 0) d.villager(3, 2, 0);
            }
            break;
          }
          case VKind::Butcher: {
            d.shell(p.l, p.w, 3);
            d.gable(p.l, p.w, 4);
            d.put(2, 0, 2, makeState(102));
            d.put(2, p.w - 1, 2, makeState(102));
            d.door(p.w / 2);
            d.put(p.l - 2, 1, 1, makeState(B::furnace, 3));
            d.put(p.l - 2, 2, 1, makeState(B::furnace, 3));
            d.box(1, p.l - 3, p.w - 2, p.w - 2, 1, 1, pal.planks);  // mostrador
            d.chest(p.l - 2, p.w - 2, 1, 0, -1, 1);
            d.torch(1, 1, 3, 0, 1);
            d.villager(2, 2, 4);
            break;
          }
          case VKind::Library: {
            d.shell(p.l, p.w, 4);
            d.gable(p.l, p.w, 5, 0);
            d.put(2, 0, 2, makeState(102));
            d.put(2, p.w - 1, 2, makeState(102));
            d.put(4, 0, 2, makeState(102));
            d.put(4, p.w - 1, 2, makeState(102));
            d.door(p.w / 2);
            for (int v = 1; v < p.w - 1; v++) d.box(p.l - 2, p.l - 2, v, v, 1, 3, makeState(B::bookshelf));
            d.box(1, 4, 1, 1, 1, 2, makeState(B::bookshelf));
            d.box(1, 4, p.w - 2, p.w - 2, 1, 2, makeState(B::bookshelf));
            d.put(2, 4, 1, makeState(B::crafting_table));
            d.put(2, 5, 1, makeState(pal.fence));
            d.put(2, 5, 2, makeState(72));
            d.torch(1, 1, 3, 0, 1);
            d.torch(1, p.w - 2, 3, 0, -1);
            d.villager(3, 3, 1);
            break;
          }
          case VKind::Church: {
            // Nave de 5x5 con tejado a dos aguas y una torre de piedra detrás
            d.box(0, 4, 0, 4, 0, 0, pal.cobble);
            d.box(1, 3, 1, 3, 0, 0, pal.planks);
            d.box(0, 4, 0, 4, 1, 5, 0);
            d.ring(0, 4, 0, 4, 1, 4, pal.cobble);
            for (int u : {0, 4})
              for (int v : {0, 4}) d.box(u, u, v, v, 0, 4, pal.log(0));
            d.put(2, 0, 3, makeState(102));
            d.put(2, 4, 3, makeState(102));
            d.door(2);
            d.gable(5, 5, 5, 0);
            // La torre
            d.box(5, 8, 0, 4, 0, 0, pal.cobble);
            d.box(5, 8, 0, 4, 1, 8, 0);
            d.ring(5, 8, 0, 4, 1, 8, pal.cobble);
            d.box(5, 8, 0, 4, 9, 9, pal.cobble);
            d.ring(5, 8, 0, 4, 10, 10, pal.cobble);
            d.box(6, 8, 1, 3, 1, 1, pal.planks);
            d.box(5, 5, 1, 3, 1, 4, 0);  // paso de la nave a la torre
            d.put(6, 0, 4, makeState(102));
            d.put(6, 4, 4, makeState(102));
            d.put(7, 0, 7, makeState(102));
            d.put(7, 4, 7, makeState(102));
            d.torch(1, 1, 3, 0, 1);
            d.torch(1, 3, 3, 0, -1);
            d.villager(2, 2, 2);
            break;
          }
          case VKind::Smithy: {
            d.shell(p.l, p.w, 4);
            d.ring(0, p.l - 1, 0, p.w - 1, 1, 4, pal.cobble);
            d.gable(p.l, p.w, 5, 0, pal.sand ? 128 : 67, pal.sand ? makeState(B::sandstone) : makeState(B::cobblestone));
            for (int u : {0, p.l - 1})
              for (int v : {0, p.w - 1}) d.box(u, u, v, v, 0, 4, pal.log(0));
            d.put(2, 0, 2, makeState(102));
            d.put(2, p.w - 1, 2, makeState(102));
            d.door(p.w / 2);
            // Fragua: lava tras un murete, hornos y el cofre del herrero
            d.box(1, 2, p.w - 4, p.w - 2, 0, 0, makeState(B::lava));
            d.box(1, 2, p.w - 4, p.w - 2, 1, 1, makeState(B::cobblestone, 0));
            d.box(1, 2, p.w - 4, p.w - 2, 1, 1, makeState(101));
            d.put(p.l - 2, 1, 1, makeState(B::furnace, 3));
            d.put(p.l - 2, 2, 1, makeState(B::furnace, 3));
            d.chest(p.l - 2, p.w - 2, 1, 0, -1, 6);
            d.put(3, 3, 1, makeState(B::crafting_table));
            d.torch(1, 1, 3, 0, 1);
            d.torch(p.l - 2, p.w - 2, 3, 0, -1);
            d.villager(3, 2, 3);
            break;
          }
          case VKind::FarmA: case VKind::FarmB: {
            // Surcos de cultivos con un canal de agua en medio; los dos tipos se reparten el terreno
            for (int v = 0; v < p.w; v++) {
              d.put(0, v, 0, makeState(60, 7));
              d.put(1, v, 0, makeState(B::water));
              d.put(2, v, 0, makeState(60, 7));
              d.put(3, v, 0, makeState(60, 7));
              static const int kCrops[3] = {59, 141, 142};
              const int crop = p.kind == VKind::FarmA ? (v < p.w / 2 ? kCrops[0] : kCrops[1]) : kCrops[2];
              for (int u : {0, 2, 3}) d.put(u, v, 1, makeState(crop, 3 + static_cast<int>(hash3(v, u, p.x0, 0x2F) % 5)));
            }
            d.put(1, -1, 0, makeState(B::dirt));
            d.put(1, p.w, 0, makeState(B::dirt));
            d.villager(1, p.w / 2, 0);
            break;
          }
          case VKind::Lamp: {
            d.put(0, 0, 0, pal.cobble);
            d.box(0, 0, 0, 0, 1, 3, makeState(pal.fence));
            d.put(0, 0, 4, makeState(B::torch, 5));
            break;
          }
        }
      }
      // Un gólem de hierro que protege la aldea
      if (b.inside(plan.golemX, plan.golemZ) && rng.nextInt(3) != 0) b.tile(plan.golemX, plan.golemY, plan.golemZ, GenTile::Mob, 99, 0);
    }
}

}  // namespace mcw
