#pragma once
#include <array>
#include <vector>

#include "core/types.h"
#include "data/blocks.h"

namespace mcw {

class BlockModels;
class Colormaps;
class World;

/// Vértice de chunk (20 bytes). Posición relativa a la sección en 1/256 de bloque.
struct ChunkVertex {
  i16 x, y, z;
  u16 layer;
  u16 u, v;
  u8 r, g, b, a;
  u8 blockLight, skyLight;  // 0..240 (luz * 16, admite medias para la luz suave)
  u8 pad[2];
};
static_assert(sizeof(ChunkVertex) == 20);

/// Datos de una sección con un borde de 1 bloque de sus vecinas (18x18x18).
struct MeshInput {
  static constexpr int P = 18;
  static constexpr int idx(int x, int y, int z) { return (y * P + z) * P + x; }
  int sx = 0, sy = 0, sz = 0;
  std::array<BlockState, P * P * P> blocks{};
  std::array<u8, P * P * P> light{};  // cielo << 4 | bloque
  std::array<u8, P * P> biomes{};     // z * P + x
};

struct MeshOutput {
  int sx = 0, sy = 0, sz = 0;
  std::vector<ChunkVertex> opaque;       // sólido + cutout (se dibujan con alpha test)
  std::vector<ChunkVertex> translucent;  // agua, hielo, cristal tintado
};

struct MesherContext {
  const BlockModels* models = nullptr;
  const Colormaps* colors = nullptr;
};

/// Copia de un World la sección (sx,sy,sz) con su borde. Devuelve false si la sección está vacía.
bool fillMeshInput(const World& world, int sx, int sy, int sz, MeshInput& out);

/// Construye la malla de una sección. Es una función pura: se puede llamar desde cualquier hilo.
MeshOutput buildMesh(const MeshInput& in, const MesherContext& ctx);

}  // namespace mcw
