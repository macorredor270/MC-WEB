#include "client/terrain.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "assets/textures.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "client/worker_pool.h"
#include "core/jobs.h"
#include "core/log.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <webgl/webgl1_ext.h>
#endif

namespace mcw {
namespace {
constexpr GLsizeiptr kQuadBytes = 4 * sizeof(ChunkVertex);
constexpr int kTableWidth = 256;  // la tabla de secciones es una textura de 256 x 256 huecos
}

Terrain::Terrain(JobSystem& jobs, u64 seed, MesherContext ctx)
    : jobs_(jobs), generator_(std::make_shared<TerrainGenerator>(seed)), ctx_(ctx) {}

Terrain::~Terrain() {
  *alive_ = false;
  for (const Page& p : pages_) {
    if (p.vao) glDeleteVertexArrays(1, &p.vao);
    if (p.vbo) glDeleteBuffers(1, &p.vbo);
  }
  if (sectionTex_) glDeleteTextures(1, &sectionTex_);
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (texArray_) glDeleteTextures(1, &texArray_);
  if (program_) glDeleteProgram(program_);
}

void Terrain::initGL(const BlockTextures& textures) {
  program_ = gl::makeProgram(shaders::kChunkVS, shaders::kChunkFS, "chunk");
  uViewProj_ = glGetUniformLocation(program_, "uViewProj");
  uSections_ = glGetUniformLocation(program_, "uSections");
  uCamBlock_ = glGetUniformLocation(program_, "uCamBlock");
  uCamFrac_ = glGetUniformLocation(program_, "uCamFrac");
  uFogColor_ = glGetUniformLocation(program_, "uFogColor");
  uFog_ = glGetUniformLocation(program_, "uFog");
  uAlphaCutoff_ = glGetUniformLocation(program_, "uAlphaCutoff");
  uBlocks_ = glGetUniformLocation(program_, "uBlocks");
  uLightmap_ = glGetUniformLocation(program_, "uLightmap");

  // Índices compartidos: cada quad son 2 triángulos (0,1,2) (0,2,3)
  std::vector<u32> idx(static_cast<std::size_t>(kPageQuads) * 6);
  for (u32 q = 0; q < kPageQuads; q++) {
    const u32 b = q * 4;
    u32* o = &idx[q * 6];
    o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
  }
  glGenBuffers(1, &ebo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(u32)), idx.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  // Tabla con el origen de cada sección (un entero por coordenada), que leen los vértices por su hueco
  sectionTable_.assign(static_cast<std::size_t>(kTableWidth) * kTableWidth * 4, 0);
  glGenTextures(1, &sectionTex_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, sectionTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32I, kTableWidth, kTableWidth, 0, GL_RGBA_INTEGER, GL_INT, sectionTable_.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  sections_.resize(kMaxSlots);
  freeSlots_.reserve(kMaxSlots);
  for (int i = kMaxSlots - 1; i >= 0; i--) freeSlots_.push_back(static_cast<u16>(i));  // se reparten de 0 hacia arriba

  // Varios tramos de golpe: en escritorio es de OpenGL; en el navegador, la extensión WEBGL_multi_draw (que no
  // tienen todos); sin ella se dibuja tramo a tramo, pero sin cambiar ni uniformes ni VAO
#ifdef __EMSCRIPTEN__
  multiDraw_ = emscripten_webgl_enable_WEBGL_multi_draw(emscripten_webgl_get_current_context());
#else
  multiDraw_ = true;
#endif
  log::info("terreno: páginas de {} quads, dibujo por tramos {}", kPageQuads, multiDraw_ ? "con multi-draw" : "uno a uno");

  // Texture array con todas las capas y sus mipmaps
  tileSize_ = textures.tileSize();
  mipLevels_ = textures.mipLevels();
  glGenTextures(1, &texArray_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  for (int level = 0; level < mipLevels_; level++) {
    const int s = std::max(1, tileSize_ >> level);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA8, s, s, textures.layerCount(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  std::vector<int> all(static_cast<std::size_t>(textures.layerCount()));
  for (int i = 0; i < textures.layerCount(); i++) all[i] = i;
  refreshTextureLayers(textures, all);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mipLevels_ - 1);
  gl::checkErrors("Terrain::initGL");
}

void Terrain::refreshTextureLayers(const BlockTextures& textures, const std::vector<int>& layers) {
  if (!texArray_) return;
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int layer : layers) {
    const auto& mips = textures.mips(layer);
    for (int level = 0; level < mipLevels_ && level < static_cast<int>(mips.size()); level++) {
      const Image& img = mips[level];
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, img.width, img.height, 1, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    }
  }
}

void Terrain::setMeshFlags(u8 flags) {
  if (flags == meshFlags_) return;
  meshFlags_ = flags;
  // Las mallas viejas se siguen viendo hasta que llegan las nuevas (sin huecos)
  for (auto& [pos, col] : columns_) col.dirty = 0xFFFF;
}

void Terrain::setMipmaps(bool on) {
  if (on == mipmaps_ || !texArray_) return;
  mipmaps_ = on;
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, on ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
}

void Terrain::rebuildOffsets(int radius) {
  offsets_.clear();
  for (int z = -radius; z <= radius; z++)
    for (int x = -radius; x <= radius; x++)
      if (x * x + z * z <= radius * radius + radius) offsets_.push_back({x, z});
  std::sort(offsets_.begin(), offsets_.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
    return a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y;
  });
  offsetsRadius_ = radius;
}

bool Terrain::neighborhoodLoaded(int cx, int cz) const {
  for (int dz = -1; dz <= 1; dz++)
    for (int dx = -1; dx <= 1; dx++)
      if (!world_.chunk(cx + dx, cz + dz)) return false;
  return true;
}

void Terrain::onChunkGenerated(std::unique_ptr<Chunk> chunk, bool fresh) {
  inFlightGen_--;
  const ChunkPos pos = chunk->pos();
  auto it = columns_.find(pos);
  if (it == columns_.end()) return;  // se descargó mientras se generaba
  it->second.generating = false;
  ChunkSet modified;
  world_.insert(std::move(chunk), modified);
  if (fresh) {
    newChunks_.push_back(pos);  // los guardados ya traen sus criaturas
    unsaved_.insert(pos);       // un chunk nuevo se guarda aunque no se toque, como en el juego
  }
  for (const ChunkPos& p : modified)
    if (auto c = columns_.find(p); c != columns_.end()) c->second.dirty = 0xFFFF;
}

void Terrain::receiveChunk(std::unique_ptr<Chunk> c, bool groundUp, u16 mask) {
  const ChunkPos p = c->pos();
  Chunk* existing = world_.chunk(p.x, p.z);
  if (!groundUp) {
    // Solo algunas secciones: copiarlas en la columna que ya hay
    if (!existing) return;
    for (int i = 0; i < kSectionCount; i++) {
      if (!(mask & (1 << i)) || !c->section(i)) continue;
      existing->ensureSection(i) = *c->section(i);
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
          for (int dy = -1; dy <= 1; dy++) markDirty(p.x + dx, i + dy, p.z + dz);
    }
    existing->recomputeHeightMap();
    return;
  }
  if (existing) dropChunk(p);
  columns_[p].generating = true;
  inFlightGen_++;
  onChunkGenerated(std::move(c), false);
  // Las vecinas ya cargadas tienen que volver a mallar su borde
  for (int dz = -1; dz <= 1; dz++)
    for (int dx = -1; dx <= 1; dx++)
      if (auto it = columns_.find({p.x + dx, p.z + dz}); it != columns_.end()) it->second.dirty = 0xFFFF;
}

void Terrain::dropChunk(ChunkPos p) {
  if (!columns_.count(p)) return;
  columns_.erase(p);
  world_.remove(p);
  deleteColumn(p);
  eraseStagedColumn(p);
  for (int sy = 0; sy < kSectionCount; sy++) meshVersion_.erase({p.x, sy, p.z});
}

void Terrain::onMeshBuilt(MeshOutput out, u32 version) {
  inFlightMesh_--;
  const glm::ivec3 key{out.sx, out.sy, out.sz};
  auto m = meshing_.find(key);
  if (m != meshing_.end() && --m->second <= 0) meshing_.erase(m);
  if (!columns_.count({out.sx, out.sz})) return;  // columna descargada
  if (meshVersion_[key] != version) return;        // ya hay una malla más nueva
  stageMesh(std::move(out));
}

void Terrain::onMeshFailed(const glm::ivec3& key) {
  inFlightMesh_--;
  auto m = meshing_.find(key);
  if (m != meshing_.end() && --m->second <= 0) meshing_.erase(m);
  markDirty(key.x, key.y, key.z);  // se volverá a pedir
}

void Terrain::submitGenerate(ChunkPos p, bool allowRemote) {
  inFlightGen_++;
  auto alive = alive_;
  if (allowRemote && pool_ &&
      pool_->generate(
          p, [alive, this](std::unique_ptr<Chunk> c) { if (*alive) onChunkGenerated(std::move(c)); },
          [alive, this, p] {
            if (!*alive) return;
            inFlightGen_--;
            submitGenerate(p, false);  // el worker falló: se genera aquí
          }))
    return;
  auto gen = generator_;
  JobSystem* jobs = &jobs_;
  jobs_.submit([gen, alive, jobs, this, p] {
    auto chunk = gen->generate(p.x, p.z);
    auto holder = std::make_shared<std::unique_ptr<Chunk>>(std::move(chunk));
    jobs->postToMain([alive, this, holder] {
      if (*alive) onChunkGenerated(std::move(*holder));
    });
  });
}

void Terrain::submitMesh(const glm::ivec3& key, std::shared_ptr<MeshInput> input, u32 version) {
  meshing_[key]++;
  inFlightMesh_++;
  auto alive = alive_;
  if (pool_ && pool_->mesh(
                   *input, [alive, this, version](MeshOutput out) { if (*alive) onMeshBuilt(std::move(out), version); },
                   [alive, this, key] { if (*alive) onMeshFailed(key); }))
    return;
  JobSystem* jobs = &jobs_;
  const MesherContext ctx = ctx_;
  jobs_.submit([input, ctx, alive, jobs, this, version] {
    auto out = std::make_shared<MeshOutput>(buildMesh(*input, ctx));
    jobs->postToMain([alive, this, out, version] {
      if (*alive) onMeshBuilt(std::move(*out), version);
    });
  });
}

void Terrain::stageMesh(MeshOutput out) {
  const glm::ivec3 key{out.sx, out.sy, out.sz};
  staged_[key] = std::move(out);
}

void Terrain::eraseStagedColumn(ChunkPos pos) {
  for (int sy = 0; sy < kSectionCount; sy++) staged_.erase({pos.x, sy, pos.z});
}

void Terrain::deleteSection(const glm::ivec3& key) {
  // Una sección sin nada que dibujar: fuera su malla, la que esperaba y su sitio en la GPU
  if (key.y < 0 || key.y >= kSectionCount) return;
  staged_.erase(key);
  if (auto it = slotOf_.find(key); it != slotOf_.end()) releaseSection(it->second);
}

void Terrain::flushUploads(double budgetMs) {
  if (staged_.empty()) return;
  const u64 start = SDL_GetTicksNS();
  // De cerca a lejos: lo que tienes delante aparece antes
  std::vector<std::pair<int, glm::ivec3>> order;
  order.reserve(staged_.size());
  for (const auto& [key, m] : staged_) {
    const int dx = key.x - center_.x, dz = key.z - center_.z;
    order.push_back({dx * dx + dz * dz, key});
  }
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  // Además del tiempo, un tope de bytes por frame: si se manda demasiado de golpe, el navegador se
  // queda esperando a que la GPU lo consuma (y ese frame se atasca).
  const std::size_t maxBytes = budgetMs >= 1e8 ? SIZE_MAX : (budgetMs >= 10.0 ? (8u << 20) : (3u << 19));
  std::size_t bytes = 0;
  for (const auto& [d, key] : order) {
    auto it = staged_.find(key);
    if (columns_.count({key.x, key.z})) bytes += uploadSection(it->second);
    staged_.erase(it);
    if ((SDL_GetTicksNS() - start) / 1e6 >= budgetMs || bytes >= maxBytes) break;
  }
}

void Terrain::setupVao(GLuint vao, GLuint vbo) const {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  const GLsizei stride = sizeof(ChunkVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_UNSIGNED_SHORT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, layer)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, u)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, r)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 2, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, blockLight)));
  glEnableVertexAttribArray(5);
  glVertexAttribIPointer(5, 1, GL_UNSIGNED_SHORT, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, slot)));
  glBindVertexArray(0);
}

void Terrain::createPage() {
  Page p;
  glGenBuffers(1, &p.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kPageQuads) * kQuadBytes, nullptr, GL_DYNAMIC_DRAW);
  glGenVertexArrays(1, &p.vao);
  setupVao(p.vao, p.vbo);
  pages_.push_back(p);
  runs_.emplace_back();
}

int Terrain::acquireSlot(const glm::ivec3& key) {
  if (freeSlots_.empty()) {
    if (!slotsWarned_) log::warn("terreno: no caben más secciones con malla ({}): las más lejanas no se ven", kMaxSlots);
    slotsWarned_ = true;
    return -1;
  }
  const u16 slot = freeSlots_.back();
  freeSlots_.pop_back();
  GpuSection& g = sections_[slot];
  g = GpuSection{};
  g.key = key;
  g.activeIndex = static_cast<int>(active_.size());
  active_.push_back(slot);
  slotOf_[key] = slot;
  setTableRow(slot, key, 1);
  return slot;
}

void Terrain::releaseSection(u16 slot) {
  visibleValid_ = false;
  GpuSection& g = sections_[slot];
  for (auto& a : g.alloc) {
    quads_.release(a);
    a = {};
  }
  // Sale de la lista de activos cambiándose por el último
  const int idx = g.activeIndex;
  const u16 last = active_.back();
  active_[static_cast<std::size_t>(idx)] = last;
  sections_[last].activeIndex = idx;
  active_.pop_back();
  slotOf_.erase(g.key);
  setTableRow(slot, g.key, 0);
  g = GpuSection{};
  freeSlots_.push_back(slot);
}

void Terrain::setTableRow(u16 slot, const glm::ivec3& key, int used) {
  i32* t = &sectionTable_[static_cast<std::size_t>(slot) * 4];
  t[0] = key.x;
  t[1] = key.y;
  t[2] = key.z;
  t[3] = used;
  tableLo_ = std::min(tableLo_, static_cast<int>(slot));
  tableHi_ = std::max(tableHi_, static_cast<int>(slot));
}

void Terrain::flushTable() {
  if (tableHi_ < tableLo_) return;
  const int row0 = tableLo_ / kTableWidth, row1 = tableHi_ / kTableWidth;
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, sectionTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row0, kTableWidth, row1 - row0 + 1, GL_RGBA_INTEGER, GL_INT,
                  &sectionTable_[static_cast<std::size_t>(row0) * kTableWidth * 4]);
  glActiveTexture(GL_TEXTURE0);
  tableLo_ = 0x7FFFFFFF;
  tableHi_ = -1;
}

std::size_t Terrain::uploadSection(MeshOutput& m) {
  const glm::ivec3 key{m.sx, m.sy, m.sz};
  std::vector<ChunkVertex>* lists[kPasses] = {&m.opaque, nullptr, &m.translucent};  // (los recortes llegan con el mallado nuevo)
  u32 quads[kPasses] = {};
  bool any = false;
  for (int p = 0; p < kPasses; p++) {
    quads[p] = lists[p] ? static_cast<u32>(lists[p]->size() / 4) : 0;
    any = any || quads[p] > 0;
  }
  auto it = slotOf_.find(key);
  if (!any) {
    if (it != slotOf_.end()) releaseSection(it->second);
    return 0;
  }
  visibleValid_ = false;
  const int slot = it != slotOf_.end() ? it->second : acquireSlot(key);
  if (slot < 0) return 0;
  GpuSection& g = sections_[static_cast<std::size_t>(slot)];
  std::size_t bytes = 0;
  for (int p = 0; p < kPasses; p++) {
    QuadAllocator::Alloc& a = g.alloc[p];
    const u32 n = quads[p];
    if (n == 0) {
      quads_.release(a);
      a = {};
      continue;
    }
    if (!(a.valid() && a.count == n)) {
      // Otro tamaño: se suelta el sitio de antes (así el nuevo puede aprovecharlo) y se reserva uno nuevo
      quads_.release(a);
      u32 fresh = QuadAllocator::kNone;
      a = quads_.allocate(n, &fresh);
      if (fresh != QuadAllocator::kNone) createPage();
    }
    for (ChunkVertex& v : *lists[p]) v.slot = static_cast<u16>(slot);
    glBindBuffer(GL_ARRAY_BUFFER, pages_[a.page].vbo);
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(a.first) * kQuadBytes, static_cast<GLsizeiptr>(n) * kQuadBytes,
                    lists[p]->data());
    bytes += static_cast<std::size_t>(n) * kQuadBytes;
  }
  return bytes;
}

void Terrain::deleteColumn(ChunkPos pos) {
  for (int sy = 0; sy < kSectionCount; sy++)
    if (auto it = slotOf_.find({pos.x, sy, pos.z}); it != slotOf_.end()) releaseSection(it->second);
}

void Terrain::update(const glm::dvec3& cameraPos, int renderDistance, double uploadBudgetMs) {
  const u64 start = SDL_GetTicksNS();
  auto elapsedMs = [start] { return (SDL_GetTicksNS() - start) / 1e6; };
  renderDistance_ = renderDistance;
  // +2: las columnas del borde (también en diagonal) necesitan a sus 8 vecinas generadas para mallarse
  const int genRadius = renderDistance + 2;
  if (offsetsRadius_ != genRadius) rebuildOffsets(genRadius);
  center_ = {static_cast<int>(std::floor(cameraPos.x / 16.0)), static_cast<int>(std::floor(cameraPos.z / 16.0))};

  const bool remote = pool_ && pool_->readyCount() > 0;
  const int maxGen = remote ? pool_->readyCount() * 16 : std::max(2, jobs_.threadCount() * 8);
  if (!remote_) {  // en un servidor los chunks vienen y van por la red
  // 1) Descargar lo que queda lejos
  const int unloadR = genRadius + 1;
  std::vector<ChunkPos> toRemove;
  for (const auto& [pos, col] : columns_) {
    const int dx = pos.x - center_.x, dz = pos.z - center_.z;
    if (dx * dx + dz * dz <= unloadR * unloadR + unloadR) continue;
    bool nearExtra = false;
    for (const ChunkPos& c : extraCenters_)
      nearExtra |= std::abs(pos.x - c.x) <= extraRadius_ + 2 && std::abs(pos.z - c.z) <= extraRadius_ + 2;
    if (!nearExtra) toRemove.push_back(pos);
  }
  for (const ChunkPos& p : toRemove) {
    if (storage_.save && !columns_[p].generating)
      if (const Chunk* c = world_.chunk(p.x, p.z)) storage_.save(*c, true);
    unloaded_.push_back(p);
    unsaved_.erase(p);
    columns_.erase(p);
    world_.remove(p);
    deleteColumn(p);
    eraseStagedColumn(p);
    for (int sy = 0; sy < kSectionCount; sy++) meshVersion_.erase({p.x, sy, p.z});
  }

  // 2) Pedir generación de lo que falta, de cerca a lejos. Con Web Workers listos, todo va a
  //    ellos (y se deja sitio para mallar); si no, al JobSystem (hilos, o el hilo principal en web).
  for (const glm::ivec2& o : offsets_) {
    if (inFlightGen_ >= maxGen || (remote && pool_->capacity() <= 0)) break;
    const ChunkPos p{center_.x + o.x, center_.z + o.y};
    auto [it, inserted] = columns_.try_emplace(p);
    if (!inserted) continue;
    it->second.generating = true;
    // Si está guardado, se lee (en el hilo principal, con cuidado de no pasarse del presupuesto)
    if (storage_.load) {
      if (auto saved = storage_.load(p)) {
        inFlightGen_++;
        onChunkGenerated(std::move(saved), false);
        if (elapsedMs() > uploadBudgetMs * 0.4) break;
        continue;
      }
    }
    submitGenerate(p, remote);
  }
  // Alrededor de los jugadores invitados (solo generar/cargar, sin mallar si están lejos)
  for (const ChunkPos& c : extraCenters_)
    for (const glm::ivec2& o : offsets_) {
      if (inFlightGen_ >= maxGen || (remote && pool_->capacity() <= 0)) break;
      if (std::abs(o.x) > extraRadius_ + 1 || std::abs(o.y) > extraRadius_ + 1) continue;
      const ChunkPos p{c.x + o.x, c.z + o.y};
      auto [it, inserted] = columns_.try_emplace(p);
      if (!inserted) continue;
      it->second.generating = true;
      if (storage_.load)
        if (auto saved = storage_.load(p)) {
          inFlightGen_++;
          onChunkGenerated(std::move(saved), false);
          continue;
        }
      submitGenerate(p, remote);
    }

  }
  // 3) Mallar secciones sucias cuyas vecinas ya están cargadas
  const int maxMesh = remote ? inFlightMesh_ + pool_->capacity() : std::max(4, jobs_.threadCount() * 24);
  int submitted = 0;
  for (const glm::ivec2& o : offsets_) {
    if (inFlightMesh_ >= maxMesh) break;
    // Preparar y mandar trabajos también cuesta (copiar el vecindario): sin pasarse del presupuesto
    if (++submitted % 8 == 0 && elapsedMs() > uploadBudgetMs * 0.5) break;
    if (o.x * o.x + o.y * o.y > renderDistance * renderDistance + renderDistance) continue;
    const ChunkPos p{center_.x + o.x, center_.z + o.y};
    auto it = columns_.find(p);
    if (it == columns_.end() || it->second.dirty == 0 || !neighborhoodLoaded(p.x, p.z)) continue;
    Column& col = it->second;
    for (int sy = 0; sy < kSectionCount && inFlightMesh_ < maxMesh; sy++) {
      if (!(col.dirty & (1u << sy))) continue;
      // Una sola malla en vuelo por sección: así los resultados no llegan desordenados.
      if (meshing_.count({p.x, sy, p.z})) continue;
      col.dirty = static_cast<u16>(col.dirty & ~(1u << sy));
      auto input = std::make_shared<MeshInput>();
      if (!fillMeshInput(world_, p.x, sy, p.z, *input)) {
        deleteSection({p.x, sy, p.z});
        continue;
      }
      input->flags = meshFlags_;
      const u32 version = ++meshVersion_[{p.x, sy, p.z}];
      submitMesh({p.x, sy, p.z}, std::move(input), version);
    }
  }

  // 4) Subir a la GPU las mallas que han llegado (una reconstrucción por columna y frame)
  flushUploads(std::max(0.3, uploadBudgetMs - elapsedMs()));
}

void Terrain::buildVisible(const Camera& cam) {
  // Lo mismo para el pase sólido y el translúcido de un frame: solo se recalcula si la cámara o las mallas cambian
  if (visibleValid_ && visiblePos_ == cam.pos && visibleViewProj_ == cam.viewProj) return;
  visibleValid_ = true;
  visiblePos_ = cam.pos;
  visibleViewProj_ = cam.viewProj;
  visible_.clear();
  // La cámara se parte en bloque entero y fracción: las secciones se colocan restando enteros y no se pierde
  // precisión aunque se esté muy lejos del origen del mundo
  camBlock_ = glm::ivec3(static_cast<int>(std::floor(cam.pos.x)), static_cast<int>(std::floor(cam.pos.y)),
                         static_cast<int>(std::floor(cam.pos.z)));
  camFrac_ = glm::vec3(static_cast<float>(cam.pos.x - camBlock_.x), static_cast<float>(cam.pos.y - camBlock_.y),
                       static_cast<float>(cam.pos.z - camBlock_.z));

  // Frustum en coordenadas relativas a la cámara
  const glm::mat4& m = cam.viewProj;
  glm::vec4 planes[6];
  for (int i = 0; i < 3; i++) {
    planes[i * 2] = glm::vec4(m[0][3] + m[0][i], m[1][3] + m[1][i], m[2][3] + m[2][i], m[3][3] + m[3][i]);
    planes[i * 2 + 1] = glm::vec4(m[0][3] - m[0][i], m[1][3] - m[1][i], m[2][3] - m[2][i], m[3][3] - m[3][i]);
  }
  auto boxVisible = [&](const glm::vec3& lo, const glm::vec3& hi) {
    for (const glm::vec4& p : planes) {
      const glm::vec3 v(p.x >= 0 ? hi.x : lo.x, p.y >= 0 ? hi.y : lo.y, p.z >= 0 ? hi.z : lo.z);
      if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
    }
    return true;
  };
  const float maxDist = (renderDistance_ + 1) * 16.0f + 12.0f;
  visible_.reserve(active_.size());
  for (const u16 slot : active_) {
    const GpuSection& g = sections_[slot];
    const glm::vec3 off(static_cast<float>(g.key.x * 16 - camBlock_.x) - camFrac_.x,
                        static_cast<float>(g.key.y * 16 - camBlock_.y) - camFrac_.y,
                        static_cast<float>(g.key.z * 16 - camBlock_.z) - camFrac_.z);
    if (std::hypot(off.x + 8.0f, off.z + 8.0f) > maxDist) continue;
    if (!boxVisible(off, off + 16.0f)) continue;
    const glm::vec3 c = off + 8.0f;
    visible_.push_back({slot, glm::dot(c, c)});
  }
  // De cerca a lejos: los sólidos así tapan pronto lo que tienen detrás; los translúcidos se recorren al revés
  std::sort(visible_.begin(), visible_.end(), [](const Visible& a, const Visible& b) { return a.dist2 < b.dist2; });
}

int Terrain::submitRuns(u32 page, std::vector<std::pair<u32, u32>>& runs) {
  const std::size_t n = runs.size();
  glBindVertexArray(pages_[page].vao);
  int calls = 0;
  if (multiDraw_ && n > 1) {
    drawCounts_.resize(n);
    drawOffsets_.resize(n);
    for (std::size_t i = 0; i < n; i++) {
      drawCounts_[i] = static_cast<GLsizei>(runs[i].second * 6);
      drawOffsets_[i] = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(runs[i].first) * 6 * sizeof(u32));
    }
#ifdef __EMSCRIPTEN__
    glMultiDrawElementsWEBGL(GL_TRIANGLES, drawCounts_.data(), GL_UNSIGNED_INT, drawOffsets_.data(), static_cast<GLsizei>(n));
#else
    glMultiDrawElements(GL_TRIANGLES, drawCounts_.data(), GL_UNSIGNED_INT, drawOffsets_.data(), static_cast<GLsizei>(n));
#endif
    calls = 1;
  } else {
    for (const auto& r : runs) {
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(r.second * 6), GL_UNSIGNED_INT,
                     reinterpret_cast<void*>(static_cast<std::uintptr_t>(r.first) * 6 * sizeof(u32)));
      calls++;
    }
  }
  runs.clear();
  return calls;
}

void Terrain::draw(const Camera& cam, GLuint lightmap, const FogParams& fog, int pass) {
  flushTable();
  buildVisible(cam);
  glUseProgram(program_);
  glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, &cam.viewProj[0][0]);
  glUniform3i(uCamBlock_, camBlock_.x, camBlock_.y, camBlock_.z);
  glUniform3f(uCamFrac_, camFrac_.x, camFrac_.y, camFrac_.z);
  glUniform3f(uFogColor_, fog.color.r, fog.color.g, fog.color.b);
  glUniform2f(uFog_, fog.start, fog.end);
  glUniform1f(uAlphaCutoff_, pass == 2 ? 0.004f : 0.5f);
  glUniform1i(uBlocks_, 0);
  glUniform1i(uLightmap_, 1);
  glUniform1i(uSections_, 2);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, sectionTex_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, lightmap);

  int sections = 0, quads = 0, calls = 0;
  if (pass != 2) {
    // Sólidos: todos los tramos de cada página en una llamada, de cerca a lejos dentro de la página
    for (const Visible& v : visible_) {
      const QuadAllocator::Alloc& a = sections_[v.slot].alloc[pass];
      if (a.count == 0) continue;
      runs_[a.page].push_back({a.first, a.count});
      sections++;
      quads += static_cast<int>(a.count);
    }
    for (u32 page = 0; page < pages_.size(); page++)
      if (!runs_[page].empty()) calls += submitRuns(page, runs_[page]);
  } else {
    // Translúcidos: de lejos a cerca, por bandas de un chunk (dentro de una banda, por página)
    auto band = [](float dist2) { return static_cast<int>(std::sqrt(dist2) / 16.0f); };
    std::size_t i = visible_.size();
    while (i > 0) {
      const int b = band(visible_[i - 1].dist2);
      std::size_t j = i;
      while (j > 0 && band(visible_[j - 1].dist2) == b) j--;
      for (std::size_t k = j; k < i; k++) {
        const QuadAllocator::Alloc& a = sections_[visible_[k].slot].alloc[pass];
        if (a.count == 0) continue;
        runs_[a.page].push_back({a.first, a.count});
        sections++;
        quads += static_cast<int>(a.count);
      }
      for (u32 page = 0; page < pages_.size(); page++)
        if (!runs_[page].empty()) calls += submitRuns(page, runs_[page]);
      i = j;
    }
  }
  glBindVertexArray(0);
  if (pass == 0) {
    drawnSections_ = sections;
    drawCalls_ = calls;
    drawnQuads_ = quads;
  } else {
    drawnSections_ += sections;
    drawCalls_ += calls;
    drawnQuads_ += quads;
  }
}

void Terrain::drawOpaque(const Camera& cam, GLuint lightmap, const FogParams& fog) { draw(cam, lightmap, fog, 0); }

void Terrain::drawTranslucent(const Camera& cam, GLuint lightmap, const FogParams& fog) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  draw(cam, lightmap, fog, 2);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

TerrainStats Terrain::stats() const {
  TerrainStats s;
  s.chunks = static_cast<int>(world_.size());
  s.sections = static_cast<int>(slotOf_.size());
  s.drawnSections = drawnSections_;
  s.drawnQuads = drawnQuads_;
  s.drawCalls = drawCalls_;
  s.pendingGen = inFlightGen_;
  s.pendingMesh = inFlightMesh_;
  s.gpuBytes = static_cast<std::size_t>(quads_.usedQuads()) * kQuadBytes;
  return s;
}

void Terrain::saveAll() { saveSome(-1.0); }

void Terrain::saveSome(double budgetMs) {
  if (!storage_.save) {
    unsaved_.clear();
    return;
  }
  const u64 start = SDL_GetTicksNS();
  for (auto it = unsaved_.begin(); it != unsaved_.end();) {
    if (budgetMs >= 0 && (SDL_GetTicksNS() - start) / 1e6 > budgetMs) break;
    if (const Chunk* c = world_.chunk(it->x, it->z)) storage_.save(*c, false);
    it = unsaved_.erase(it);
  }
}

void Terrain::clear() {
  // Los trabajos en vuelo del mundo anterior se descartan al llegar
  *alive_ = false;
  alive_ = std::make_shared<bool>(true);
  while (!active_.empty()) releaseSection(active_.back());
  for (const auto& [pos, col] : columns_) world_.remove(pos);
  columns_.clear();
  staged_.clear();
  meshing_.clear();
  meshVersion_.clear();
  newChunks_.clear();
  unloaded_.clear();
  unsaved_.clear();
  inFlightGen_ = inFlightMesh_ = 0;
}

void Terrain::reset(u64 seed, GeneratorSettings settings) {
  clear();
  generator_ = std::make_shared<TerrainGenerator>(seed, settings);
  if (pool_) pool_->setWorld(seed, settings.generatorName(), settings.flatOptions(), settings.structures);
}

bool Terrain::settled() const {
  if (inFlightGen_ > 0 || inFlightMesh_ > 0 || !staged_.empty()) return false;
  for (const glm::ivec2& o : offsets_) {
    if (o.x * o.x + o.y * o.y > renderDistance_ * renderDistance_ + renderDistance_) continue;
    auto it = columns_.find({center_.x + o.x, center_.z + o.y});
    // En un servidor solo llegan los chunks de su distancia de visión: cuentan los que haya
    if (it == columns_.end()) {
      if (remote_ && o.x * o.x + o.y * o.y > 4) continue;  // (las de alrededor sí hacen falta)
      return false;
    }
    if (it->second.generating) return false;
    // (las del borde de lo recibido no se pueden mallar hasta que lleguen sus vecinas)
    if (it->second.dirty != 0 && !(remote_ && !neighborhoodLoaded(it->first.x, it->first.z))) return false;
  }
  return true;
}

}  // namespace mcw

namespace mcw {

void Terrain::markDirty(int sx, int sy, int sz) {
  if (sy < 0 || sy >= kSectionCount) return;
  auto it = columns_.find({sx, sz});
  if (it != columns_.end()) it->second.dirty = static_cast<u16>(it->second.dirty | (1u << sy));
}

void Terrain::setBlock(int x, int y, int z, BlockState s) {
  if (y < 0 || y >= kChunkHeight || !world_.chunkAt(x, z)) return;
  ChunkSet modified;
  world_.setBlock(x, y, z, s, modified);
  unsaved_.insert({x >> 4, z >> 4});
  for (const ChunkPos& c : modified) unsaved_.insert(c);

  // Secciones cuyo borde incluye este bloque: se vuelven a mallar ya, en este mismo frame
  const int sx = x >> 4, sy = y >> 4, sz = z >> 4;
  const int lx = x & 15, ly = y & 15, lz = z & 15;
  std::vector<glm::ivec3> now;
  for (int dy = -1; dy <= 1; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        if ((dx == -1 && lx != 0) || (dx == 1 && lx != 15)) continue;
        if ((dy == -1 && ly != 0) || (dy == 1 && ly != 15)) continue;
        if ((dz == -1 && lz != 0) || (dz == 1 && lz != 15)) continue;
        now.push_back({sx + dx, sy + dy, sz + dz});
      }
  // La luz puede haber cambiado en otras secciones (hacia abajo si se abre el cielo): en segundo plano
  for (const ChunkPos& c : modified)
    for (int yy = 0; yy <= std::min(kSectionCount - 1, sy + 1); yy++) markDirty(c.x, yy, c.z);

  for (const glm::ivec3& k : now) {
    if (k.y < 0 || k.y >= kSectionCount || !neighborhoodLoaded(k.x, k.z)) {
      markDirty(k.x, k.y, k.z);
      continue;
    }
    MeshInput input;
    input.flags = meshFlags_;
    ++meshVersion_[k];
    if (!fillMeshInput(world_, k.x, k.y, k.z, input)) {
      deleteSection(k);
    } else {
      stageMesh(buildMesh(input, ctx_));
    }
    auto it = columns_.find({k.x, k.z});
    if (it != columns_.end()) it->second.dirty = static_cast<u16>(it->second.dirty & ~(1u << k.y));
  }
  flushUploads();  // el cambio se ve en este mismo frame
}

bool Terrain::isReady(int x, int z) const {
  const int cx = x >> 4, cz = z >> 4;
  if (!neighborhoodLoaded(cx, cz)) return false;
  auto it = columns_.find({cx, cz});
  return it != columns_.end() && it->second.dirty == 0 && !meshing_.count({cx, 4, cz});
}

}  // namespace mcw
