#include "client/terrain.h"

#include <algorithm>
#include <cmath>

#include "assets/textures.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "core/jobs.h"
#include "core/log.h"

namespace mcw {
namespace {
constexpr int kMaxQuads = 98304;  // tope de quads por malla de sección
}

Terrain::Terrain(JobSystem& jobs, u64 seed, MesherContext ctx)
    : jobs_(jobs), generator_(std::make_shared<TerrainGenerator>(seed)), ctx_(ctx) {}

Terrain::~Terrain() {
  *alive_ = false;
  for (auto& [key, g] : gpu_) {
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

void Terrain::onChunkGenerated(std::unique_ptr<Chunk> chunk) {
  inFlightGen_--;
  const ChunkPos pos = chunk->pos();
  auto it = columns_.find(pos);
  if (it == columns_.end()) return;  // se descargó mientras se generaba
  it->second.generating = false;
  ChunkSet modified;
  world_.insert(std::move(chunk), modified);
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
  uploadMesh(out);
}

void Terrain::uploadMesh(const MeshOutput& out) {
  const glm::ivec3 key{out.sx, out.sy, out.sz};
  if (out.opaque.empty() && out.translucent.empty()) {
    deleteSection(key);
    return;
  }
  GpuSection& g = gpu_[key];
  if (!g.vao[0]) {
    glGenVertexArrays(2, g.vao);
    glGenBuffers(2, g.vbo);
    for (int pass = 0; pass < 2; pass++) {
      glBindVertexArray(g.vao[pass]);
      glBindBuffer(GL_ARRAY_BUFFER, g.vbo[pass]);
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
    }
    glBindVertexArray(0);
  }
  g.bytes = 0;
  const std::vector<ChunkVertex>* data[2] = {&out.opaque, &out.translucent};
  for (int pass = 0; pass < 2; pass++) {
    const auto& v = *data[pass];
    const std::size_t quads = std::min<std::size_t>(v.size() / 4, kMaxQuads);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo[pass]);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quads * 4 * sizeof(ChunkVertex)), v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
    g.count[pass] = static_cast<GLsizei>(quads * 6);
    g.bytes += quads * 4 * sizeof(ChunkVertex);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Terrain::deleteSection(const glm::ivec3& key) {
  auto it = gpu_.find(key);
  if (it == gpu_.end()) return;
  glDeleteVertexArrays(2, it->second.vao);
  glDeleteBuffers(2, it->second.vbo);
  gpu_.erase(it);
}

void Terrain::update(const glm::dvec3& cameraPos, int renderDistance) {
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
    columns_.erase(p);
    world_.remove(p);
    for (int sy = 0; sy < kSectionCount; sy++) {
      deleteSection({p.x, sy, p.z});
      meshVersion_.erase({p.x, sy, p.z});
    }
  }

  // 2) Pedir generación de lo que falta, de cerca a lejos
  const int maxGen = std::max(2, jobs_.threadCount() * 2);
  for (const glm::ivec2& o : offsets_) {
    if (inFlightGen_ >= maxGen) break;
    const ChunkPos p{center_.x + o.x, center_.z + o.y};
    auto [it, inserted] = columns_.try_emplace(p);
    if (!inserted) continue;
    it->second.generating = true;
    inFlightGen_++;
    auto gen = generator_;
    auto alive = alive_;
    JobSystem* jobs = &jobs_;
    jobs_.submit([gen, alive, jobs, this, p] {
      auto chunk = gen->generate(p.x, p.z);
      auto holder = std::make_shared<std::unique_ptr<Chunk>>(std::move(chunk));
      jobs->postToMain([alive, this, holder] {
        if (*alive) onChunkGenerated(std::move(*holder));
      });
    });
  }

  // 3) Mallar secciones sucias cuyas vecinas ya están cargadas
  const int maxMesh = std::max(4, jobs_.threadCount() * 4);
  for (const glm::ivec2& o : offsets_) {
    if (inFlightMesh_ >= maxMesh) break;
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
      meshing_[{p.x, sy, p.z}]++;
      inFlightMesh_++;
      const u32 version = ++meshVersion_[{p.x, sy, p.z}];
      auto alive = alive_;
      JobSystem* jobs = &jobs_;
      const MesherContext ctx = ctx_;
      jobs_.submit([input, ctx, alive, jobs, this, version] {
        auto out = std::make_shared<MeshOutput>(buildMesh(*input, ctx));
        jobs->postToMain([alive, this, out, version] {
          if (*alive) onMeshBuilt(std::move(*out), version);
        });
      });
    }
  }
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
  struct Item { float dist; const GpuSection* g; glm::vec3 off; };
  std::vector<Item> items;
  items.reserve(gpu_.size());
  const float maxDist = (renderDistance_ + 1) * 16.0f;
  for (const auto& [key, g] : gpu_) {
    if (g.count[pass] == 0) continue;
    const glm::vec3 off(static_cast<float>(key.x * 16 - cam.pos.x), static_cast<float>(key.y * 16 - cam.pos.y),
                        static_cast<float>(key.z * 16 - cam.pos.z));
    const glm::vec3 c = off + 8.0f;
    if (std::hypot(c.x, c.z) > maxDist + 12) continue;
    bool visible = true;
    for (const glm::vec4& p : planes) {
      const glm::vec3 pos(p.x >= 0 ? off.x + 16 : off.x, p.y >= 0 ? off.y + 16 : off.y, p.z >= 0 ? off.z + 16 : off.z);
      if (p.x * pos.x + p.y * pos.y + p.z * pos.z + p.w < 0) { visible = false; break; }
    }
    if (visible) items.push_back({glm::dot(c, c), &g, off});
  }
  std::sort(items.begin(), items.end(), [pass](const Item& a, const Item& b) { return pass == 0 ? a.dist < b.dist : a.dist > b.dist; });
  for (const Item& it : items) {
    glUniform3f(uOffset_, it.off.x, it.off.y, it.off.z);
    glBindVertexArray(it.g->vao[pass]);
    glDrawElements(GL_TRIANGLES, it.g->count[pass], GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
  if (pass == 0) drawnSections_ = static_cast<int>(items.size());
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
  s.sections = static_cast<int>(gpu_.size());
  s.drawnSections = drawnSections_;
  s.pendingGen = inFlightGen_;
  s.pendingMesh = inFlightMesh_;
  for (const auto& [k, g] : gpu_) s.gpuBytes += g.bytes;
  return s;
}

bool Terrain::settled() const {
  if (inFlightGen_ > 0 || inFlightMesh_ > 0) return false;
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
    ++meshVersion_[k];
    if (!fillMeshInput(world_, k.x, k.y, k.z, input)) {
      deleteSection(k);
    } else {
      uploadMesh(buildMesh(input, ctx_));
    }
    auto it = columns_.find({k.x, k.z});
    if (it != columns_.end()) it->second.dirty = static_cast<u16>(it->second.dirty & ~(1u << k.y));
  }
}

bool Terrain::isReady(int x, int z) const {
  const int cx = x >> 4, cz = z >> 4;
  if (!neighborhoodLoaded(cx, cz)) return false;
  auto it = columns_.find({cx, cz});
  return it != columns_.end() && it->second.dirty == 0 && !meshing_.count({cx, 4, cz});
}

}  // namespace mcw
