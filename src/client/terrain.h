#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

#include "client/gl.h"
#include "client/mesher.h"
#include "game/session.h"
#include "world/generator.h"
#include "world/world.h"

namespace mcw {

class JobSystem;
class BlockTextures;
struct Camera;

struct TerrainStats {
  int chunks = 0, sections = 0, drawnSections = 0;
  int pendingGen = 0, pendingMesh = 0;
  std::size_t gpuBytes = 0;
};

struct FogParams {
  glm::vec3 color{1};
  float start = 100, end = 128;
};

/// Mundo local de F1: genera chunks en hilos, les pone luz, los malla en hilos y los dibuja.
/// (En F2 la generación pasa al servidor integrado y aquí solo queda recibir chunks y mallar.)
class Terrain : public WorldAccess {
 public:
  Terrain(JobSystem& jobs, u64 seed, MesherContext ctx);
  ~Terrain();

  void initGL(const BlockTextures& textures);
  /// Carga/descarga y malla alrededor de la cámara; sube a la GPU los resultados listos.
  void update(const glm::dvec3& cameraPos, int renderDistance);
  void drawOpaque(const Camera& cam, GLuint lightmap, const FogParams& fog);
  void drawTranslucent(const Camera& cam, GLuint lightmap, const FogParams& fog);
  void refreshTextureLayers(const BlockTextures& textures, const std::vector<int>& layers);

  World& world() override { return world_; }
  /// Cambia un bloque: actualiza la luz y vuelve a mallar al momento lo que se ve afectado.
  void setBlock(int x, int y, int z, BlockState s) override;
  /// ¿La columna de (x, z) está generada y mallada con sus vecinas? (para empezar a jugar)
  bool isReady(int x, int z) const;
  GLuint textureArray() const { return texArray_; }
  const TerrainGenerator& generator() const { return *generator_; }
  TerrainStats stats() const;
  /// true cuando todo lo que está dentro de la distancia de render ya está generado y mallado.
  bool settled() const;

 private:
  struct Column {
    u16 dirty = 0xFFFF;  // secciones que hay que volver a mallar
    bool generating = false;
  };
  struct GpuSection {
    GLuint vao[2] = {0, 0}, vbo[2] = {0, 0};
    GLsizei count[2] = {0, 0};
    std::size_t bytes = 0;
  };
  struct SectionKeyHash {
    std::size_t operator()(const glm::ivec3& k) const noexcept {
      return std::hash<u64>()((u64(u32(k.x)) << 40) ^ (u64(u32(k.z)) << 8) ^ u64(u32(k.y)));
    }
  };

  void rebuildOffsets(int radius);
  void onChunkGenerated(std::unique_ptr<Chunk> chunk);
  void onMeshBuilt(MeshOutput out, u32 version);
  void uploadMesh(const MeshOutput& out);
  void markDirty(int sx, int sy, int sz);
  void deleteSection(const glm::ivec3& key);
  void draw(const Camera& cam, GLuint lightmap, const FogParams& fog, int pass);
  bool neighborhoodLoaded(int cx, int cz) const;

  JobSystem& jobs_;
  std::shared_ptr<const TerrainGenerator> generator_;
  MesherContext ctx_;
  World world_;
  std::unordered_map<ChunkPos, Column, ChunkPosHash> columns_;
  std::unordered_map<glm::ivec3, GpuSection, SectionKeyHash> gpu_;
  std::unordered_map<glm::ivec3, int, SectionKeyHash> meshing_;  // secciones con malla en cola
  std::unordered_map<glm::ivec3, u32, SectionKeyHash> meshVersion_;  // última malla pedida de cada sección
  std::vector<glm::ivec2> offsets_;
  int offsetsRadius_ = -1;
  int inFlightGen_ = 0, inFlightMesh_ = 0;
  int renderDistance_ = 8;
  ChunkPos center_{};
  mutable int drawnSections_ = 0;

  GLuint program_ = 0, ebo_ = 0, texArray_ = 0;
  GLint uViewProj_ = -1, uOffset_ = -1, uFogColor_ = -1, uFog_ = -1, uAlphaCutoff_ = -1, uBlocks_ = -1, uLightmap_ = -1;
  int tileSize_ = 16, mipLevels_ = 1;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mcw
