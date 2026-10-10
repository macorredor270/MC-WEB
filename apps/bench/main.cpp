// Mide lo que cuesta cada paso de la carga del mundo, para saber qué conviene repartir entre núcleos.
// Uso: mcweb-bench [radio] [ruta del jar]
//      mcweb-bench --find-cave [semilla]   busca una cueva grande cerca del origen (para las pruebas de dibujo)
//      mcweb-bench --find-water [semilla]  busca una orilla con agua delante (para ver el pase translúcido)
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

#include "assets/cc0_pack.h"
#include "assets/item_models.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/rules.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/mesher.h"
#include "world/generator.h"
#include "world/world.h"

using namespace mcw;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

/// Busca una cueva grande y sin cielo cerca del origen: un sitio donde ponerse a medir el dibujo bajo tierra.
static int findCave(u64 seed) {
  constexpr int R = 8;  // chunks alrededor del origen
  TerrainGenerator gen(seed);
  World world;
  for (int z = -R; z <= R; z++)
    for (int x = -R; x <= R; x++) {
      ChunkSet modified;
      world.insert(gen.generate(x, z), modified);
    }
  auto air = [&](int x, int y, int z) { return stateId(world.block(x, y, z)) == 0; };
  auto solid = [&](int x, int y, int z) { return blockInfo(stateId(world.block(x, y, z))).opaqueCube; };
  int bestSize = 0, bx = 0, by = 0, bz = 0;
  for (int y = 14; y <= 50; y += 3)
    for (int z = -80; z <= 80; z += 4)
      for (int x = -80; x <= 80; x += 4) {
        if (!air(x, y, z) || !air(x, y + 1, z) || !solid(x, y - 1, z) || world.skyLight(x, y, z) != 0) continue;
        // Relleno del aire conectado (con tope): si toca el exterior no vale
        constexpr int B = 24, N = 2 * B + 1;
        std::vector<u8> seen(static_cast<std::size_t>(N * N * N), 0);
        std::vector<std::array<int, 3>> stack{{x, y, z}};
        int size = 0;
        bool open = false;
        while (!stack.empty() && size < 8000 && !open) {
          const auto [cx, cy, cz] = stack.back();
          stack.pop_back();
          const int ix = cx - x + B, iy = cy - y + B, iz = cz - z + B;
          if (ix < 0 || iy < 0 || iz < 0 || ix >= N || iy >= N || iz >= N) continue;
          u8& sv = seen[static_cast<std::size_t>((iy * N + iz) * N + ix)];
          if (sv || !air(cx, cy, cz)) continue;
          sv = 1;
          size++;
          if (world.skyLight(cx, cy, cz) > 0) open = true;
          stack.push_back({cx + 1, cy, cz});
          stack.push_back({cx - 1, cy, cz});
          stack.push_back({cx, cy + 1, cz});
          stack.push_back({cx, cy - 1, cz});
          stack.push_back({cx, cy, cz + 1});
          stack.push_back({cx, cy, cz - 1});
        }
        if (!open && size > bestSize) {
          bestSize = size;
          bx = x;
          by = y;
          bz = z;
        }
      }
  if (bestSize == 0) {
    std::puts("no se ha encontrado ninguna cueva");
    return 1;
  }
  std::printf("cueva de %d bloques de aire: --pos %d.5,%d,%d.5\n", bestSize, bx, by, bz);
  return 0;
}

/// Busca una orilla: un sitio en tierra con un lago o el mar a unos pasos, mirando hacia el agua.
static int findWater(u64 seed) {
  constexpr int R = 10;
  TerrainGenerator gen(seed);
  World world;
  for (int z = -R; z <= R; z++)
    for (int x = -R; x <= R; x++) {
      ChunkSet modified;
      world.insert(gen.generate(x, z), modified);
    }
  auto water = [&](int x, int y, int z) {
    const int id = stateId(world.block(x, y, z));
    return id == 8 || id == 9;
  };
  auto top = [&](int x, int z) {  // primer bloque no aire desde arriba
    for (int y = 120; y > 0; y--)
      if (stateId(world.block(x, y, z)) != 0) return y;
    return 0;
  };
  int best = 0, bx = 0, by = 0, bz = 0;
  double byaw = 0;
  for (int z = -150; z <= 150; z += 3)
    for (int x = -150; x <= 150; x += 3) {
      const int y = top(x, z);
      if (y < 60 || water(x, y, z) || stateId(world.block(x, y, z)) == 0) continue;
      // Agua en una fila delante, a 5, 9, 13 y 17 pasos
      static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
      for (const auto& d : dirs) {
        int n = 0;
        for (int k = 5; k <= 25; k += 4) {
          const int wx = x + d[0] * k, wz = z + d[1] * k, wy = top(wx, wz);
          if (water(wx, wy, wz)) n++;
        }
        if (n > best) {
          best = n;
          bx = x;
          by = y + 1;
          bz = z;
          byaw = std::atan2(-static_cast<double>(d[0]), -static_cast<double>(d[1])) * 180.0 / 3.14159265358979;
        }
      }
    }
  if (best == 0) {
    std::puts("no se ha encontrado ninguna orilla");
    return 1;
  }
  std::printf("orilla (%d de 5 tramos con agua): --pos %d.5,%d,%d.5 --yaw %.0f\n", best, bx, by, bz, byaw);
  return 0;
}


/// Revisa que todo bloque y objeto tenga modelo y textura: `mcweb-bench --audit [jar]` (sin jar, solo el pack libre).
static int audit(const char* jar) {
  PackStack packs;
  packs.pushBottom(makeCC0Pack());
  if (jar)
    if (auto p = ZipPack::open(jar)) packs.pushTop(std::shared_ptr<const Pack>(std::move(p)));
  BlockTextures textures;
  BlockModels models;
  Colormaps colors;
  ItemModels items;
  models.bake(packs, textures);
  colors.load(packs);
  items.prepare(packs, textures, models, colors);
  textures.load(packs);
  int bad = 0;
  auto report = [&](const char* kind, const std::string& what) {
    std::printf("%s: %s\n", kind, what.c_str());
    bad++;
  };
  for (const std::string& e : models.errors()) report("modelo", e);
  for (const std::string& m : textures.missing()) report("textura", m);
  // Cada estado de bloque que existe
  for (int id = 1; id < 256; id++) {
    const BlockInfo& bi = blockInfo(id);
    if (!bi.exists || bi.fluid) continue;
    for (int meta = 0; meta < 16; meta++) {
      const BlockState st = makeState(id, meta);
      if (!blockstateOf(st)) continue;  // meta sin sentido para ese bloque
      const VariantList* vl = models.forState(st);
      if (!vl || vl->models.empty()) report("bloque sin modelo", std::string(bi.name) + ":" + std::to_string(meta));
    }
  }
  // Cada objeto que se puede conseguir (el inventario creativo) y todos los objetos con su variante 0
  auto checkIcon = [&](int id, int meta) {
    const ItemIcon& ic = items.icon(id, meta);
    const std::string name = std::string(itemInfo(id).name) + ":" + std::to_string(meta);
    if (ic.kind == ItemIcon::Kind::None) report("objeto sin icono", name);
    else if (ic.kind == ItemIcon::Kind::Flat && ic.layer == BlockTextures::kMissingLayer) report("objeto con textura que falta", name);
    else if (ic.kind == ItemIcon::Kind::Block) {
      const VariantList* vl = models.forState(ic.state);
      if (!vl || vl->models.empty()) report("objeto-bloque sin modelo", name);
    }
  };
  for (const ItemStack& s : creativeItems()) checkIcon(s.id, s.meta);
  for (int id = 1; id < 512; id++)
    if (itemInfo(id).exists) checkIcon(id, 0);
  std::printf("%d problemas\n", bad);
  return bad ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--audit") return audit(argc > 2 ? argv[2] : nullptr);
  if (argc > 1 && std::string_view(argv[1]) == "--find-water") return findWater(argc > 2 ? static_cast<u64>(std::atoll(argv[2])) : 42);
  if (argc > 1 && std::string_view(argv[1]) == "--find-cave") return findCave(argc > 2 ? static_cast<u64>(std::atoll(argv[2])) : 42);
  const int r = argc > 1 ? std::atoi(argv[1]) : 6;
  auto t0 = Clock::now();
  PackStack packs;
  packs.pushBottom(makeCC0Pack());
  if (argc > 2)
    if (auto p = ZipPack::open(argv[2])) packs.pushTop(std::shared_ptr<const Pack>(std::move(p)));
  BlockTextures textures;
  BlockModels models;
  Colormaps colors;
  models.bake(packs, textures);
  colors.load(packs);
  auto t1 = Clock::now();
  std::printf("assets (modelos + colores): %.1f ms\n", ms(t0, t1));

  TerrainGenerator gen(42);
  std::vector<std::unique_ptr<Chunk>> chunks;
  t0 = Clock::now();
  for (int z = -r; z <= r; z++)
    for (int x = -r; x <= r; x++) chunks.push_back(gen.generate(x, z));
  t1 = Clock::now();
  const int n = static_cast<int>(chunks.size());
  std::printf("generar: %d chunks, %.2f ms/chunk\n", n, ms(t0, t1) / n);

  World world;
  t0 = Clock::now();
  for (auto& c : chunks) {
    ChunkSet modified;
    world.insert(std::move(c), modified);
  }
  t1 = Clock::now();
  std::printf("insertar y coser luz: %.2f ms/chunk\n", ms(t0, t1) / n);

  MesherContext ctx{&models, &colors};
  int sections = 0, empty = 0;
  std::size_t verts = 0;
  double fillMs = 0, meshMs = 0;
  MeshInput input;
  for (int z = -r + 1; z < r; z++)
    for (int x = -r + 1; x < r; x++)
      for (int sy = 0; sy < kSectionCount; sy++) {
        auto a = Clock::now();
        const bool ok = fillMeshInput(world, x, sy, z, input);
        auto b = Clock::now();
        fillMs += ms(a, b);
        if (!ok) { empty++; continue; }
        MeshOutput out = buildMesh(input, ctx);
        auto c = Clock::now();
        meshMs += ms(b, c);
        sections++;
        verts += out.opaque.size() + out.translucent.size();
      }
  std::printf("copiar vecindario: %.3f ms/sección (%d vacías)\n", fillMs / (sections + empty), empty);
  std::printf("mallar: %d secciones, %.2f ms/sección, %.0f vértices/sección (%.1f KB)\n", sections, meshMs / sections,
              double(verts) / sections, double(verts) * sizeof(ChunkVertex) / sections / 1024.0);
  return 0;
}
