#pragma once
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "client/gl.h"
#include "client/mesher.h"
#include "game/session.h"
#include "world/generator.h"
#include "world/world.h"

namespace mcw {

class JobSystem;
class BlockTextures;
class WorkerPool;
struct Camera;

struct TerrainStats {
  int chunks = 0, sections = 0, drawnSections = 0, drawCalls = 0, drawnQuads = 0;
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

  /// Guardado de chunks: `load` da el chunk guardado (o nullptr para generarlo); `save` lo guarda
  /// (al descargarlo o en los guardados periódicos). Se llaman en el hilo principal.
  struct Storage {
    std::function<std::unique_ptr<Chunk>(ChunkPos)> load;
    std::function<void(const Chunk&, bool unloading)> save;
  };
  void setStorage(Storage s) { storage_ = std::move(s); }
  /// Jugando en un servidor: no se genera ni se descarga nada por distancia; los chunks llegan
  /// con receiveChunk y se van con dropChunk.
  void setRemote(bool r) { remote_ = r; }
  bool remote() const { return remote_; }
  void receiveChunk(std::unique_ptr<Chunk> c, bool groundUp, u16 mask);
  void dropChunk(ChunkPos p);
  /// Otros sitios donde hay que tener chunks cargados (jugadores invitados), con su radio.
  void setExtraCenters(std::vector<ChunkPos> centers, int radius) {
    extraCenters_ = std::move(centers);
    extraRadius_ = radius;
  }
  /// Empieza otro mundo: lo borra todo (menos texturas y shaders) y usa otro generador.
  void reset(u64 seed, GeneratorSettings settings);
  /// Guarda todos los chunks cargados que tengan cambios sin guardar (al salir del mundo).
  void saveAll();
  /// Guardado automático repartido: guarda chunks pendientes hasta gastar `budgetMs`.
  void saveSome(double budgetMs);
  /// Hay que volver a guardar este chunk (criaturas u objetos que se han movido, por ejemplo).
  void markUnsaved(ChunkPos p) { if (world_.chunk(p.x, p.z)) unsaved_.insert(p); }
  std::size_t unsavedCount() const { return unsaved_.size(); }
  /// Vacía el mundo sin guardar nada (volver al menú).
  void clear();

  void initGL(const BlockTextures& textures);
  /// Web Workers para generar y mallar (build web sin hilos). Sin ellos se usa el JobSystem.
  void setWorkerPool(WorkerPool* pool) { pool_ = pool; }
  /// Carga/descarga y malla alrededor de la cámara; sube a la GPU los resultados listos, de cerca a
  /// lejos, hasta gastar `uploadBudgetMs` (lo que no quepa se sube en los frames siguientes).
  void update(const glm::dvec3& cameraPos, int renderDistance, double uploadBudgetMs = 1e9);
  void drawOpaque(const Camera& cam, GLuint lightmap, const FogParams& fog);
  void drawTranslucent(const Camera& cam, GLuint lightmap, const FogParams& fog);
  void refreshTextureLayers(const BlockTextures& textures, const std::vector<int>& layers);
  /// Calidad del mallado (luz suave, hojas). Si cambia, se vuelve a mallar todo poco a poco.
  void setMeshFlags(u8 flags);
  u8 meshFlags() const { return meshFlags_; }
  /// Mipmaps del texture array (texturas lejanas suavizadas) sí o no.
  void setMipmaps(bool on);

  World& world() override { return world_; }
  /// Cambia un bloque: actualiza la luz y vuelve a mallar al momento lo que se ve afectado.
  void setBlock(int x, int y, int z, BlockState s) override;
  /// ¿La columna de (x, z) está generada y mallada con sus vecinas? (para empezar a jugar)
  bool isReady(int x, int z) const;
  GLuint textureArray() const { return texArray_; }
  const TerrainGenerator& generator() const { return *generator_; }
  TerrainStats stats() const;
  /// Chunks que han llegado desde la última llamada (para poner animales).
  std::vector<ChunkPos> takeNewChunks() { return std::exchange(newChunks_, {}); }
  /// Chunks que acaban de salir de memoria (para guardar y quitar sus criaturas).
  std::vector<ChunkPos> takeUnloaded() { return std::exchange(unloaded_, {}); }
  /// true cuando todo lo que está dentro de la distancia de render ya está generado y mallado.
  bool settled() const;

 private:
  struct Column {
    u16 dirty = 0xFFFF;  // secciones que hay que volver a mallar
    bool generating = false;
  };
  /// Una columna de 16 secciones en un solo buffer por pasada (sólido y translúcido): las secciones
  /// van seguidas, de abajo arriba, y se dibuja el tramo visible con una sola llamada.
  struct GpuColumn {
    GLuint vao[2] = {0, 0}, vbo[2] = {0, 0};
    std::array<u32, kSectionCount> first[2]{}, quads[2]{};
    u32 total[2] = {0, 0};
  };
  struct SectionKeyHash {
    std::size_t operator()(const glm::ivec3& k) const noexcept {
      return std::hash<u64>()((u64(u32(k.x)) << 40) ^ (u64(u32(k.z)) << 8) ^ u64(u32(k.y)));
    }
  };

  void rebuildOffsets(int radius);
  void onChunkGenerated(std::unique_ptr<Chunk> chunk, bool fresh = true);
  void onMeshBuilt(MeshOutput out, u32 version);
  void onMeshFailed(const glm::ivec3& key);
  void submitGenerate(ChunkPos p, bool allowRemote);
  void submitMesh(const glm::ivec3& key, std::shared_ptr<MeshInput> input, u32 version);
  /// Guarda una malla nueva de sección; se sube a la GPU (junto con las demás de su columna) en
  /// flushUploads(), una vez por frame como mucho por columna.
  void stageMesh(MeshOutput out);
  void flushUploads(double budgetMs = 1e9);
  /// Devuelve los bytes nuevos que se han mandado a la GPU.
  std::size_t rebuildColumn(ChunkPos pos, std::map<int, MeshOutput>& updates);
  void setupVao(GLuint vao, GLuint vbo) const;
  void deleteColumn(ChunkPos pos);
  void markDirty(int sx, int sy, int sz);
  void deleteSection(const glm::ivec3& key);
  void draw(const Camera& cam, GLuint lightmap, const FogParams& fog, int pass);
  bool neighborhoodLoaded(int cx, int cz) const;

  JobSystem& jobs_;
  WorkerPool* pool_ = nullptr;
  std::shared_ptr<const TerrainGenerator> generator_;
  MesherContext ctx_;
  World world_;
  std::unordered_map<ChunkPos, Column, ChunkPosHash> columns_;
  std::unordered_map<ChunkPos, GpuColumn, ChunkPosHash> gpu_;
  struct Staged {
    std::map<int, MeshOutput> meshes;
    int frames = 0;  // frames que lleva esperando
  };
  std::unordered_map<ChunkPos, Staged, ChunkPosHash> staged_;
  std::unordered_map<glm::ivec3, int, SectionKeyHash> meshing_;  // secciones con malla en cola
  std::unordered_map<glm::ivec3, u32, SectionKeyHash> meshVersion_;  // última malla pedida de cada sección
  std::vector<glm::ivec2> offsets_;
  std::vector<ChunkPos> newChunks_, unloaded_;
  std::vector<ChunkPos> extraCenters_;
  int extraRadius_ = 0;
  bool remote_ = false;
  Storage storage_;
  std::unordered_set<ChunkPos, ChunkPosHash> unsaved_;
  int offsetsRadius_ = -1;
  int inFlightGen_ = 0, inFlightMesh_ = 0;
  int renderDistance_ = 8;
  u8 meshFlags_ = kMeshDefault;
  bool mipmaps_ = true;
  ChunkPos center_{};
  mutable int drawnSections_ = 0, drawCalls_ = 0, drawnQuads_ = 0;

  GLuint program_ = 0, ebo_ = 0, texArray_ = 0;
  GLint uViewProj_ = -1, uOffset_ = -1, uFogColor_ = -1, uFog_ = -1, uAlphaCutoff_ = -1, uBlocks_ = -1, uLightmap_ = -1;
  int tileSize_ = 16, mipLevels_ = 1;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mcw
