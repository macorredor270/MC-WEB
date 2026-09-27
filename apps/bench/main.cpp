// Mide lo que cuesta cada paso de la carga del mundo, para saber qué conviene repartir entre núcleos.
// Uso: mcweb-bench [radio] [ruta del jar]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "assets/cc0_pack.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/mesher.h"
#include "world/generator.h"
#include "world/world.h"

using namespace mcw;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

int main(int argc, char** argv) {
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
