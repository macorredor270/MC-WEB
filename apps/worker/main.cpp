// Módulo de los Web Workers del build web sin hilos: genera chunks y construye mallas fuera del
// hilo principal. No tiene gráficos ni SDL; el JavaScript de web/worker-pre.js le pasa los
// trabajos y devuelve los resultados a la página (ver src/client/worker_pool.cpp).
#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>
#include <memory>

#include "assets/cc0_pack.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/mesher.h"
#include "core/log.h"
#include "world/chunk_codec.h"
#include "world/generator.h"

using namespace mcw;

namespace {

struct State {
  std::unique_ptr<TerrainGenerator> generator;
  PackStack packs;
  BlockTextures textures;
  BlockModels models;
  Colormaps colors;
};
State* g = nullptr;

/// Copia los bytes a memoria de malloc: el JavaScript los lee y luego llama a free().
u8* toHeap(const std::vector<u8>& bytes, int* outLen) {
  u8* p = static_cast<u8*>(std::malloc(bytes.empty() ? 1 : bytes.size()));
  if (!bytes.empty()) std::memcpy(p, bytes.data(), bytes.size());
  *outLen = static_cast<int>(bytes.size());
  return p;
}

}  // namespace

extern "C" {

/// Prepara el generador y los modelos de bloque. Devuelve el número de capas de textura que han
/// salido al hornear los modelos: la página lo compara con el suyo para comprobar que mallamos igual.
EMSCRIPTEN_KEEPALIVE int mcw_worker_init(unsigned seedLo, unsigned seedHi, const u8* bundle, int bundleLen) {
  delete g;
  g = new State();
  const u64 seed = (static_cast<u64>(seedHi) << 32) | seedLo;
  g->generator = std::make_unique<TerrainGenerator>(seed);
  auto cc0 = makeCC0Pack();
  g->packs.pushBottom(cc0);
  if (bundle && bundleLen > 0) {
    if (auto pack = unbundlePack(bundle, static_cast<std::size_t>(bundleLen), "pack")) g->packs.pushTop(pack);
    else log::warn("worker: paquete de modelos no válido");
  }
  g->models.bake(g->packs, g->textures);
  g->colors.load(g->packs);
  return g->textures.layerCount();
}

/// Cambia de mundo (semilla y tipo de generador) sin volver a hornear los modelos.
EMSCRIPTEN_KEEPALIVE void mcw_worker_world(unsigned seedLo, unsigned seedHi, const char* name, const char* options, int structures) {
  if (!g) return;
  const u64 seed = (static_cast<u64>(seedHi) << 32) | seedLo;
  g->generator = std::make_unique<TerrainGenerator>(seed, GeneratorSettings::fromLevel(name, options, structures != 0));
}

EMSCRIPTEN_KEEPALIVE u8* mcw_worker_generate(int cx, int cz, int* outLen) {
  auto chunk = g->generator->generate(cx, cz);
  return toHeap(encodeChunk(*chunk), outLen);
}

EMSCRIPTEN_KEEPALIVE u8* mcw_worker_mesh(const u8* input, int len, int* outLen) {
  if (len != static_cast<int>(sizeof(MeshInput))) {
    *outLen = 0;
    return static_cast<u8*>(std::malloc(1));
  }
  auto in = std::make_unique<MeshInput>();
  std::memcpy(in.get(), input, sizeof(MeshInput));
  const MeshOutput out = buildMesh(*in, MesherContext{&g->models, &g->colors});
  return toHeap(encodeMeshOutput(out), outLen);
}

}  // extern "C"
