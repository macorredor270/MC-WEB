#pragma once
#include <array>
#include <cstddef>
#include <type_traits>
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
  u16 slot;                 // sección a la que pertenece en la GPU (de ahí sale su posición): lo pone Terrain al subirla
};
static_assert(sizeof(ChunkVertex) == 20);

/// Opciones de calidad del mallado (Ajustes > Gráficos).
enum MeshFlags : u8 {
  kMeshSmoothLight = 1,  // luz suave y oclusión ambiental
  kMeshFancyLeaves = 2,  // hojas transparentes (si no: opacas y sin caras interiores)
  kMeshDefault = kMeshSmoothLight | kMeshFancyLeaves,
};

/// Datos de una sección con un borde de 1 bloque de sus vecinas (18x18x18).
struct MeshInput {
  static constexpr int P = 18;
  u8 flags = kMeshDefault;
  static constexpr int idx(int x, int y, int z) { return (y * P + z) * P + x; }
  int sx = 0, sy = 0, sz = 0;
  std::array<BlockState, P * P * P> blocks{};
  std::array<u8, P * P * P> light{};  // cielo << 4 | bloque
  std::array<u8, P * P> biomes{};     // z * P + x
};

struct MeshOutput {
  int sx = 0, sy = 0, sz = 0;
  u16 visibility = 0x7FFF;               // qué caras de la sección se ven entre sí (client/visibility.h), aunque no haya malla
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

/// Reparte los quads de `in` entre los que no necesitan descartar fragmentos (`solid`) y los que sí (`cutout`: su
/// textura tiene huecos). `cutoutLayer[capa]` es 1 si la capa los necesita. Las hojas rápidas (alfa del vértice 0: el
/// shader las pinta opacas) van a `solid`.
void splitCutout(const std::vector<ChunkVertex>& in, const std::vector<u8>& cutoutLayer, std::vector<ChunkVertex>& solid,
                 std::vector<ChunkVertex>& cutout);

/// Qué caras de la sección se ven entre sí a través de los bloques que no tapan la vista (relleno por regiones
/// de los 16x16x16 bloques de dentro): 15 bits, uno por par de caras (`visPairBit`).
u16 computeVisibility(const MeshInput& in);

/// Mallas a bytes y vuelta, para recibirlas de los Web Workers (mismo build a los dos lados).
std::vector<u8> encodeMeshOutput(const MeshOutput& out);
bool decodeMeshOutput(const u8* data, std::size_t size, MeshOutput& out);
static_assert(std::is_trivially_copyable_v<MeshInput>, "MeshInput se copia tal cual a los workers");

}  // namespace mcw
