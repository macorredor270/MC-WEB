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

namespace mcw {
namespace {
constexpr int kMaxQuads = 196608;  // tope de quads por columna (índices compartidos)
constexpr GLsizeiptr kQuadBytes = 4 * sizeof(ChunkVertex);
}

Terrain::Terrain(JobSystem& jobs, u64 seed, MesherContext ctx)
    : jobs_(jobs), generator_(std::make_shared<TerrainGenerator>(seed)), ctx_(ctx) {}

Terrain::~Terrain() {
  *alive_ = false;
  for (auto& [pos, g] : gpu_) {
    glDeleteVertexArrays(2, g.vao);
    glDeleteBuffers(2, g.vbo);
  }
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (texArray_) glDeleteTextures(1, &texArray_);
  if (program_) glDeleteProgram(program_);
}

void Terrain::initGL(const BlockTextures& textures) {
  program_ = gl::makeProgram(shaders::kChunkVS, shaders::kChunkFS, "chunk");
  uViewProj_ = glGetUniformLocation(program_, "uViewProj");
  uOffset_ = glGetUniformLocation(program_, "uOffset");
  uFogColor_ = glGetUniformLocation(program_, "uFogColor");
  uFog_ = glGetUniformLocation(program_, "uFog");
  uAlphaCutoff_ = glGetUniformLocation(program_, "uAlphaCutoff");
  uBlocks_ = glGetUniformLocation(program_, "uBlocks");
  uLightmap_ = glGetUniformLocation(program_, "uLightmap");

  // Índices compartidos: cada quad son 2 triángulos (0,1,2) (0,2,3)
  std::vector<u32> idx(static_cast<std::size_t>(kMaxQuads) * 6);
  for (u32 q = 0; q < static_cast<u32>(kMaxQuads); q++) {
    const u32 b = q * 4;
    u32* o = &idx[q * 6];
    o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
  }
  glGenBuffers(1, &ebo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(u32)), idx.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

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
  const ChunkPos pos{out.sx, out.sz};
  const int sy = out.sy;
  staged_[pos].meshes[sy] = std::move(out);
}

void Terrain::deleteSection(const glm::ivec3& key) {
  // Una malla vacía: al subirla, la sección deja de ocupar sitio en el buffer de la columna
  if (key.y < 0 || key.y >= kSectionCount) return;
  auto it = gpu_.find({key.x, key.z});
  auto st = staged_.find({key.x, key.z});
  const bool hasGpu = it != gpu_.end() && (it->second.quads[0][key.y] || it->second.quads[1][key.y]);
  if (!hasGpu && (st == staged_.end() || !st->second.meshes.count(key.y))) return;
  MeshOutput empty;
  empty.sx = key.x;
  empty.sy = key.y;
  empty.sz = key.z;
  stageMesh(std::move(empty));
}

void Terrain::flushUploads(double budgetMs) {
  if (staged_.empty()) return;
  const u64 start = SDL_GetTicksNS();
  // De cerca a lejos: lo que tienes delante aparece antes. Una columna con secciones aún en camino
  // espera a tenerlas todas (así se reconstruye una vez y no cinco), salvo que lleve mucho esperando.
  std::vector<std::pair<int, ChunkPos>> order;
  order.reserve(staged_.size());
  for (auto& [pos, st] : staged_) {
    st.frames++;
    if (st.frames < 30 && budgetMs < 1e8) {
      bool pending = false;
      for (int sy = 0; sy < kSectionCount && !pending; sy++) pending = meshing_.count({pos.x, sy, pos.z}) > 0;
      if (pending) continue;
    }
    const int dx = pos.x - center_.x, dz = pos.z - center_.z;
    order.push_back({dx * dx + dz * dz, pos});
  }
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  // Además del tiempo, un tope de bytes por frame: si se manda demasiado de golpe, el navegador se
  // queda esperando a que la GPU lo consuma (y ese frame se atasca).
  const std::size_t maxBytes = budgetMs >= 1e8 ? SIZE_MAX : (budgetMs >= 10.0 ? (8u << 20) : (3u << 19));
  std::size_t bytes = 0;
  for (const auto& [d, pos] : order) {
    auto it = staged_.find(pos);
    if (columns_.count(pos)) bytes += rebuildColumn(pos, it->second.meshes);
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
  glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, sectionY)));
  glBindVertexArray(0);
}

std::size_t Terrain::rebuildColumn(ChunkPos pos, std::map<int, MeshOutput>& updates) {
  GpuColumn& g = gpu_[pos];
  std::size_t uploaded = 0;
  for (int pass = 0; pass < 2; pass++) {
    // Tamaños nuevos: las secciones actualizadas cambian, el resto se conserva
    std::array<u32, kSectionCount> quads = g.quads[pass];
    std::array<const std::vector<ChunkVertex>*, kSectionCount> fresh{};
    bool changed = false;
    for (auto& [sy, m] : updates) {
      const auto& v = pass == 0 ? m.opaque : m.translucent;
      fresh[sy] = &v;
      quads[sy] = static_cast<u32>(v.size() / 4);
      changed = changed || quads[sy] != 0 || g.quads[pass][sy] != 0;
    }
    if (!changed) continue;
    u32 total = 0;
    for (int sy = 0; sy < kSectionCount; sy++) {
      if (total + quads[sy] > static_cast<u32>(kMaxQuads)) quads[sy] = 0;  // no cabe (no pasa en la práctica)
      total += quads[sy];
    }
    if (total == 0) {
      if (g.vbo[pass]) glDeleteBuffers(1, &g.vbo[pass]);
      if (g.vao[pass]) glDeleteVertexArrays(1, &g.vao[pass]);
      g.vbo[pass] = g.vao[pass] = 0;
      g.first[pass].fill(0);
      g.quads[pass].fill(0);
      g.total[pass] = 0;
      continue;
    }
    // Buffer nuevo: lo que no cambia se copia de GPU a GPU, sin pasar por la CPU
    GLuint nb = 0;
    glGenBuffers(1, &nb);
    glBindBuffer(GL_COPY_WRITE_BUFFER, nb);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(total) * kQuadBytes, nullptr, GL_STATIC_DRAW);
    if (g.vbo[pass]) glBindBuffer(GL_COPY_READ_BUFFER, g.vbo[pass]);
    std::array<u32, kSectionCount> first{};
    u32 off = 0;
    for (int sy = 0; sy < kSectionCount; sy++) {
      first[sy] = off;
      const u32 n = quads[sy];
      if (n == 0) continue;
      if (fresh[sy]) {
        glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(off) * kQuadBytes, static_cast<GLsizeiptr>(n) * kQuadBytes,
                        fresh[sy]->data());
        uploaded += static_cast<std::size_t>(n) * kQuadBytes;
      } else {
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(g.first[pass][sy]) * kQuadBytes,
                            static_cast<GLintptr>(off) * kQuadBytes, static_cast<GLsizeiptr>(n) * kQuadBytes);
      }
      off += n;
    }
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    if (g.vbo[pass]) glDeleteBuffers(1, &g.vbo[pass]);
    g.vbo[pass] = nb;
    if (!g.vao[pass]) glGenVertexArrays(1, &g.vao[pass]);
    setupVao(g.vao[pass], nb);
    g.first[pass] = first;
    g.quads[pass] = quads;
    g.total[pass] = total;
  }
  if (g.total[0] == 0 && g.total[1] == 0) deleteColumn(pos);
  return uploaded;
}

void Terrain::deleteColumn(ChunkPos pos) {
  auto it = gpu_.find(pos);
  if (it == gpu_.end()) return;
  glDeleteVertexArrays(2, it->second.vao);
  glDeleteBuffers(2, it->second.vbo);
  gpu_.erase(it);
}

void Terrain::update(const glm::dvec3& cameraPos, int renderDistance, double uploadBudgetMs) {
  const u64 start = SDL_GetTicksNS();
  auto elapsedMs = [start] { return (SDL_GetTicksNS() - start) / 1e6; };
  renderDistance_ = renderDistance;
  // +2: las columnas del borde (también en diagonal) necesitan a sus 8 vecinas generadas para mallarse
  const int genRadius = renderDistance + 2;
  if (offsetsRadius_ != genRadius) rebuildOffsets(genRadius);
  center_ = {static_cast<int>(std::floor(cameraPos.x / 16.0)), static_cast<int>(std::floor(cameraPos.z / 16.0))};

  // 1) Descargar lo que queda lejos
  const int unloadR = genRadius + 1;
  std::vector<ChunkPos> toRemove;
  for (const auto& [pos, col] : columns_) {
    const int dx = pos.x - center_.x, dz = pos.z - center_.z;
    if (dx * dx + dz * dz > unloadR * unloadR + unloadR) toRemove.push_back(pos);
  }
  for (const ChunkPos& p : toRemove) {
    if (storage_.save && !columns_[p].generating)
      if (const Chunk* c = world_.chunk(p.x, p.z)) storage_.save(*c, true);
    unloaded_.push_back(p);
    unsaved_.erase(p);
    columns_.erase(p);
    world_.remove(p);
    deleteColumn(p);
    staged_.erase(p);
    for (int sy = 0; sy < kSectionCount; sy++) meshVersion_.erase({p.x, sy, p.z});
  }

  // 2) Pedir generación de lo que falta, de cerca a lejos. Con Web Workers listos, todo va a
  //    ellos (y se deja sitio para mallar); si no, al JobSystem (hilos, o el hilo principal en web).
  const bool remote = pool_ && pool_->readyCount() > 0;
  const int maxGen = remote ? pool_->readyCount() * 16 : std::max(2, jobs_.threadCount() * 8);
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

void Terrain::draw(const Camera& cam, GLuint lightmap, const FogParams& fog, int pass) {
  glUseProgram(program_);
  glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, &cam.viewProj[0][0]);
  glUniform3f(uFogColor_, fog.color.r, fog.color.g, fog.color.b);
  glUniform2f(uFog_, fog.start, fog.end);
  glUniform1f(uAlphaCutoff_, pass == 0 ? 0.5f : 0.004f);
  glUniform1i(uBlocks_, 0);
  glUniform1i(uLightmap_, 1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, lightmap);

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
  struct Item { float dist; GLuint vao; u32 first, count; glm::vec3 off; };
  std::vector<Item> items;
  items.reserve(gpu_.size());
  int sections = 0;
  const float maxDist = (renderDistance_ + 1) * 16.0f;
  for (const auto& [pos, g] : gpu_) {
    if (g.total[pass] == 0) continue;
    const glm::vec3 off(static_cast<float>(pos.x * 16 - cam.pos.x), static_cast<float>(-cam.pos.y),
                        static_cast<float>(pos.z * 16 - cam.pos.z));
    if (std::hypot(off.x + 8.0f, off.z + 8.0f) > maxDist + 12) continue;
    // Tramo de secciones visibles (de la más baja a la más alta); las de en medio van incluidas
    int lo = -1, hi = -1;
    for (int sy = 0; sy < kSectionCount; sy++) {
      if (g.quads[pass][sy] == 0) continue;
      const glm::vec3 a = off + glm::vec3(0.0f, sy * 16.0f, 0.0f);
      if (!boxVisible(a, a + 16.0f)) continue;
      if (lo < 0) lo = sy;
      hi = sy;
      sections++;
    }
    if (lo < 0) continue;
    const glm::vec3 c = off + glm::vec3(8.0f, (lo + hi + 1) * 8.0f, 8.0f);
    const u32 first = g.first[pass][lo];
    items.push_back({glm::dot(c, c), g.vao[pass], first, g.first[pass][hi] + g.quads[pass][hi] - first, off});
  }
  std::sort(items.begin(), items.end(), [pass](const Item& a, const Item& b) { return pass == 0 ? a.dist < b.dist : a.dist > b.dist; });
  for (const Item& it : items) {
    glUniform3f(uOffset_, it.off.x, it.off.y, it.off.z);
    glBindVertexArray(it.vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(it.count * 6), GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(it.first) * 6 * sizeof(u32)));
  }
  glBindVertexArray(0);
  if (pass == 0) {
    drawnSections_ = sections;
    drawCalls_ = static_cast<int>(items.size());
  } else {
    drawCalls_ += static_cast<int>(items.size());
  }
}

void Terrain::drawOpaque(const Camera& cam, GLuint lightmap, const FogParams& fog) { draw(cam, lightmap, fog, 0); }

void Terrain::drawTranslucent(const Camera& cam, GLuint lightmap, const FogParams& fog) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  draw(cam, lightmap, fog, 1);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

TerrainStats Terrain::stats() const {
  TerrainStats s;
  s.chunks = static_cast<int>(world_.size());
  for (const auto& [pos, g] : gpu_)
    for (int sy = 0; sy < kSectionCount; sy++) s.sections += (g.quads[0][sy] || g.quads[1][sy]) ? 1 : 0;
  s.drawnSections = drawnSections_;
  s.drawCalls = drawCalls_;
  s.pendingGen = inFlightGen_;
  s.pendingMesh = inFlightMesh_;
  for (const auto& [pos, g] : gpu_) s.gpuBytes += static_cast<std::size_t>(g.total[0] + g.total[1]) * kQuadBytes;
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
  for (auto& [pos, g] : gpu_) {
    glDeleteVertexArrays(2, g.vao);
    glDeleteBuffers(2, g.vbo);
  }
  gpu_.clear();
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
    if (it == columns_.end() || it->second.generating || it->second.dirty != 0) return false;
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
