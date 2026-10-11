#include "client/item_renderer.h"

#include <SDL3/SDL_timer.h>

#include <cmath>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

#include "assets/item_models.h"
#include "assets/models.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "core/face.h"
#include "game/effects.h"
#include "game/session.h"

namespace mcw {
namespace {
constexpr float kFaceShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
}

ItemRenderer::~ItemRenderer() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
  if (glintTex_) glDeleteTextures(1, &glintTex_);
}

namespace {
/// Segundos desde que arrancó el reloj de SDL (anima el destello).
double glintSeconds() { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }
}  // namespace

void ItemRenderer::makeGlintTexture() {
  // Patrón propio (no es el del juego): bandas diagonales moradas que se cruzan, de 64x64 y sin costuras
  constexpr int N = 64;
  std::vector<u8> px(static_cast<std::size_t>(N * N * 4));
  const float tau = 6.2831853f;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const float a = std::sin(tau * static_cast<float>(2 * x + y) / N);
      const float b = std::sin(tau * static_cast<float>(x - 3 * y) / N);
      float t = std::max(0.0f, 0.5f * (a + b) + 0.15f);
      t = std::min(1.0f, t * t * 1.6f);
      u8* o = &px[static_cast<std::size_t>((y * N + x) * 4)];
      o[0] = static_cast<u8>(std::lround(255.0f * std::min(1.0f, 0.62f * t + 0.04f)));
      o[1] = static_cast<u8>(std::lround(255.0f * std::min(1.0f, 0.30f * t + 0.02f)));
      o[2] = static_cast<u8>(std::lround(255.0f * std::min(1.0f, 1.00f * t + 0.08f)));
      o[3] = 255;
    }
  glGenTextures(1, &glintTex_);
  glBindTexture(GL_TEXTURE_2D, glintTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void ItemRenderer::initGL(GLuint textureArray, u16 firstDestroyLayer) {
  texArray_ = textureArray;
  destroyLayer_ = firstDestroyLayer;
  program_ = gl::makeProgram(shaders::kItemVS, shaders::kItemFS, "item");
  uMVP_ = glGetUniformLocation(program_, "uMVP");
  uTex_ = glGetUniformLocation(program_, "uTex");
  uAlphaCutoff_ = glGetUniformLocation(program_, "uAlphaCutoff");
  uTextured_ = glGetUniformLocation(program_, "uTextured");
  uGlint_ = glGetUniformLocation(program_, "uGlint");
  uGlintTex_ = glGetUniformLocation(program_, "uGlintTex");
  uGlintParams_ = glGetUniformLocation(program_, "uGlintParams");
  makeGlintTexture();
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);
}

void ItemRenderer::draw(const std::vector<Vertex>& v, const glm::mat4& mvp, bool textured, float alphaCutoff, GLenum mode) {
  if (v.empty()) return;
  glUseProgram(program_);
  glUniformMatrix4fv(uMVP_, 1, GL_FALSE, &mvp[0][0]);
  glUniform1i(uTex_, 0);
  glUniform1f(uAlphaCutoff_, alphaCutoff);
  glUniform1i(uTextured_, textured ? 1 : 0);
  glUniform1i(uGlint_, 0);
  glUniform1i(uGlintTex_, 1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(mode, 0, static_cast<GLsizei>(v.size()));
  glBindVertexArray(0);
}

void ItemRenderer::drawGlint(const std::vector<Vertex>& v, const glm::mat4& mvp, float alphaCutoff, float patternPx) {
  if (v.empty() || !glintTex_) return;
  GLint prevDepthFunc = GL_LESS;
  glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
  const GLboolean wasBlend = glIsEnabled(GL_BLEND);
  GLboolean prevDepthMask = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
  glUseProgram(program_);
  glUniformMatrix4fv(uMVP_, 1, GL_FALSE, &mvp[0][0]);
  glUniform1i(uTex_, 0);
  glUniform1f(uAlphaCutoff_, alphaCutoff);
  glUniform1i(uTextured_, 1);
  glUniform1i(uGlint_, 1);
  glUniform1i(uGlintTex_, 1);
  const double t = glintSeconds();
  glUniform3f(uGlintParams_, std::max(8.0f, patternPx), static_cast<float>(std::fmod(t / 3.0, 1.0)), static_cast<float>(std::fmod(t / 4.8, 1.0)));
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, glintTex_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  // Solo donde el objeto ya está (misma profundidad) y sumando color
  glDepthFunc(GL_EQUAL);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
  glBindVertexArray(0);
  glUniform1i(uGlint_, 0);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (!wasBlend) glDisable(GL_BLEND);
  glDepthMask(prevDepthMask);
  glDepthFunc(static_cast<GLenum>(prevDepthFunc));
}

void ItemRenderer::appendItem(std::vector<Vertex>& out, const ItemStack& s, const glm::mat4& m, const glm::vec3& light, bool thickSprite,
                              int variant) {
  const ItemIcon& icon = items_.icon(s.id, s.meta, variant);
  auto push = [&](const glm::vec3& p, float u, float v, u16 layer, const glm::vec3& color) {
    const glm::vec4 w = m * glm::vec4(p, 1.0f);
    out.push_back({w.x, w.y, w.z, u, v, static_cast<float>(layer),
                   static_cast<u8>(std::clamp(color.r, 0.0f, 1.0f) * 255), static_cast<u8>(std::clamp(color.g, 0.0f, 1.0f) * 255),
                   static_cast<u8>(std::clamp(color.b, 0.0f, 1.0f) * 255), 255});
  };
  u32 tint0 = icon.tint, tint1 = 0xFFFFFF;
  if (s.id == ItemId::potion) {
    tint1 = potion::color(s.meta);  // el líquido
  } else if (s.id == ItemId::spawn_egg) {
    // Huevos de criatura: el color de fondo y el de las manchas según la criatura (id de entidad en meta)
    static const std::map<int, std::pair<u32, u32>> kEggs = {
        {50, {0x0DA70B, 0x000000}}, {51, {0xC1C1C1, 0x494949}}, {52, {0x342D27, 0xA80E0E}}, {54, {0x00AFAF, 0x799C65}}, {55, {0x51A03E, 0x7EBF6E}},
        {56, {0xF9F9F9, 0xBCBCBC}}, {57, {0xEA9393, 0x4C7129}}, {58, {0x161616, 0x000000}}, {59, {0x0C424E, 0xA80E0E}}, {60, {0x6E6E6E, 0x303030}},
        {61, {0xF6B201, 0xFFFF0B}}, {62, {0x340000, 0xFCFC00}}, {65, {0x4C3E30, 0x0F0F0F}}, {66, {0x340000, 0x51A03E}}, {67, {0x161616, 0x6E6E6E}},
        {68, {0x5A8272, 0xF17D30}}, {90, {0xF0A5A2, 0xDB635F}}, {91, {0xE7E7E7, 0xFFB5B5}}, {92, {0x443626, 0xA1A1A1}}, {93, {0xA1A1A1, 0xFF0000}},
        {94, {0x223B4D, 0x708899}}, {95, {0xD7D3D3, 0xCEAF96}}, {96, {0xA00F10, 0xB7B7B7}}, {98, {0xEFDE7D, 0x564434}}, {100, {0xC09E7D, 0xEEE500}},
        {101, {0x995F40, 0x734831}}, {120, {0x562C3E, 0xB6926F}}};
    const auto it = kEggs.find(s.meta);
    if (it != kEggs.end()) {
      tint0 = it->second.first;
      tint1 = it->second.second;
    }
  } else if (s.id >= ItemId::leather_helmet && s.id <= ItemId::leather_boots) {
    tint0 = s.extra && s.extra->color >= 0 ? static_cast<u32>(s.extra->color) : 0xA06540;  // (cuero sin teñir)
  }
  const glm::vec3 tint(((tint0 >> 16) & 255) / 255.0f, ((tint0 >> 8) & 255) / 255.0f, (tint0 & 255) / 255.0f);
  if (icon.kind == ItemIcon::Kind::Block) {
    const VariantList* vl = blocks_.forState(icon.state);
    if (!vl) return;
    for (const BakedQuad& q : vl->models[0].quads) {
      const glm::vec3 c = light * (q.shade ? kFaceShade[q.face] : 1.0f) * (q.tintIndex >= 0 ? tint : glm::vec3(1));
      const int order[6] = {0, 1, 2, 0, 2, 3};
      for (int k : order) push({q.pos[k][0] - 0.5f, q.pos[k][1] - 0.5f, q.pos[k][2] - 0.5f}, q.uv[k][0], q.uv[k][1], q.layer, c);
    }
    return;
  }
  // Sprite plano (con dos caras y, en el mundo, un poco de grosor para que no desaparezca de canto)
  const u16 layer = icon.kind == ItemIcon::Kind::Flat ? icon.layer : 0;
  const glm::vec3 c = light * tint;
  const float d = thickSprite ? 1.0f / 32.0f : 0.0f;
  const glm::vec3 a(-0.5f, 0.5f, d), b(-0.5f, -0.5f, d), cc(0.5f, -0.5f, d), dd(0.5f, 0.5f, d);
  push(a, 0, 0, layer, c); push(b, 0, 1, layer, c); push(cc, 1, 1, layer, c);
  push(a, 0, 0, layer, c); push(cc, 1, 1, layer, c); push(dd, 1, 0, layer, c);
  const glm::vec3 back = c * 0.85f;
  const glm::vec3 a2(-0.5f, 0.5f, -d), b2(-0.5f, -0.5f, -d), c2(0.5f, -0.5f, -d), d2(0.5f, 0.5f, -d);
  push(a2, 0, 0, layer, back); push(c2, 1, 1, layer, back); push(b2, 0, 1, layer, back);
  push(a2, 0, 0, layer, back); push(d2, 1, 0, layer, back); push(c2, 1, 1, layer, back);
  if (icon.kind == ItemIcon::Kind::Flat && icon.hasLayer2) {
    // Segunda capa (líquido, manchas) justo delante y detrás, con su propio color
    const glm::vec3 c1 = light * glm::vec3(((tint1 >> 16) & 255) / 255.0f, ((tint1 >> 8) & 255) / 255.0f, (tint1 & 255) / 255.0f);
    const float e = d + 0.004f;
    const glm::vec3 fa(-0.5f, 0.5f, e), fb(-0.5f, -0.5f, e), fc(0.5f, -0.5f, e), fd(0.5f, 0.5f, e);
    push(fa, 0, 0, icon.layer2, c1); push(fb, 0, 1, icon.layer2, c1); push(fc, 1, 1, icon.layer2, c1);
    push(fa, 0, 0, icon.layer2, c1); push(fc, 1, 1, icon.layer2, c1); push(fd, 1, 0, icon.layer2, c1);
    const glm::vec3 ba(-0.5f, 0.5f, -e), bb(-0.5f, -0.5f, -e), bc(0.5f, -0.5f, -e), bd(0.5f, 0.5f, -e);
    const glm::vec3 c1b = c1 * 0.85f;
    push(ba, 0, 0, icon.layer2, c1b); push(bc, 1, 1, icon.layer2, c1b); push(bb, 0, 1, icon.layer2, c1b);
    push(ba, 0, 0, icon.layer2, c1b); push(bd, 1, 0, icon.layer2, c1b); push(bc, 1, 1, icon.layer2, c1b);
  }
}

void ItemRenderer::queueIcon(const ItemStack& s, float x, float y) {
  if (!s.empty()) queue_.push_back({s, x, y});
}

void ItemRenderer::flushIcons(int screenW, int screenH, int guiScale) {
  if (queue_.empty()) return;
  std::vector<Vertex> v, glint;
  const float g = static_cast<float>(guiScale);
  viewportH_ = screenH;
  for (const QueuedIcon& q : queue_) {
    const ItemIcon& icon = items_.icon(q.s.id, q.s.meta);
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3((q.x + 8) * g, (q.y + 8) * g, 0.0f));
    m = glm::scale(m, glm::vec3(g, -g, g));  // y hacia abajo en pantalla
    if (icon.kind == ItemIcon::Kind::Block) {
      m = glm::scale(m, glm::vec3(10.0f));
      m = glm::rotate(m, glm::radians(30.0f), glm::vec3(1, 0, 0));
      m = glm::rotate(m, glm::radians(225.0f), glm::vec3(0, 1, 0));
    } else {
      m = glm::scale(m, glm::vec3(16.0f));
    }
    const std::size_t first = v.size();
    appendItem(v, q.s, m, glm::vec3(1.0f), false);
    if (q.s.glints()) glint.insert(glint.end(), v.begin() + static_cast<std::ptrdiff_t>(first), v.end());
  }
  queue_.clear();
  const glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screenW), static_cast<float>(screenH), 0.0f, -1000.0f, 1000.0f);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  draw(v, proj, true, 0.1f);
  drawGlint(glint, proj, 0.1f, 28.0f * g);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

void ItemRenderer::drawWorldItems(const std::vector<ItemEntity>& items, const Camera& cam, float partial, double timeTicks, const LightFn& light) {
  if (items.empty()) return;
  std::vector<Vertex> v, glint;
  for (const ItemEntity& e : items) {
    const glm::dvec3 p = e.prevPos + (e.pos - e.prevPos) * static_cast<double>(partial);
    const glm::vec3 rel(p - cam.pos);
    if (glm::dot(rel, rel) > 64.0f * 64.0f) continue;
    const float t = static_cast<float>(e.age) + partial;
    const float bob = std::sin(t / 10.0f + e.bobOffset) * 0.1f + 0.1f;
    const ItemIcon& icon = items_.icon(e.stack.id, e.stack.meta);
    const float scale = icon.kind == ItemIcon::Kind::Block ? 0.25f : 0.5f;
    const int copies = e.stack.count > 20 ? 4 : e.stack.count > 5 ? 3 : e.stack.count > 1 ? 2 : 1;
    for (int c = 0; c < copies; c++) {
      glm::mat4 m(1.0f);
      const glm::vec3 jitter = c == 0 ? glm::vec3(0) : glm::vec3((c % 2 ? 0.06f : -0.05f), c * 0.03f, (c > 1 ? 0.05f : -0.04f));
      m = glm::translate(m, rel + glm::vec3(0, bob + scale * 0.5f, 0) + jitter);
      m = glm::rotate(m, t / 20.0f + e.bobOffset, glm::vec3(0, 1, 0));
      m = glm::scale(m, glm::vec3(scale));
      const std::size_t first = v.size();
      appendItem(v, e.stack, m, light(p + glm::dvec3(0, 0.2, 0)), true);
      if (e.stack.glints()) glint.insert(glint.end(), v.begin() + static_cast<std::ptrdiff_t>(first), v.end());
    }
  }
  (void)timeTicks;
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  draw(v, cam.viewProj, true, 0.1f);
  drawGlint(glint, cam.viewProj, 0.1f, std::clamp(static_cast<float>(viewportH_) * 0.12f, 24.0f, 120.0f));
}

void ItemRenderer::appendBox(std::vector<Vertex>& out, const glm::mat4& m, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& color) {
  const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                          {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
  struct Face {
    int a, b, c, d;
    float shade;
  };
  static const Face faces[6] = {{4, 7, 6, 5, 1.0f},  // arriba
                                {0, 1, 2, 3, 0.5f},  // abajo
                                {3, 2, 6, 7, 0.8f},  // +Z
                                {0, 4, 5, 1, 0.8f},  // -Z
                                {1, 5, 6, 2, 0.65f}, // +X
                                {0, 3, 7, 4, 0.65f}};  // -X
  for (const Face& f : faces) {
    const glm::vec3 shaded = color * f.shade;
    const u8 r = static_cast<u8>(std::clamp(shaded.r, 0.0f, 1.0f) * 255), g = static_cast<u8>(std::clamp(shaded.g, 0.0f, 1.0f) * 255),
             b = static_cast<u8>(std::clamp(shaded.b, 0.0f, 1.0f) * 255);
    const int idx[6] = {f.a, f.b, f.c, f.a, f.c, f.d};
    for (int i : idx) {
      const glm::vec4 w = m * glm::vec4(c[i], 1.0f);
      out.push_back({w.x, w.y, w.z, 0, 0, 0, r, g, b, 255});
    }
  }
}

void ItemRenderer::drawBooks(const std::vector<BookPose>& books, const Camera& cam) {
  if (books.empty()) return;
  std::vector<Vertex> v;
  constexpr float W = 0.25f, H = 0.36f, kCover = 0.016f, kPages = 0.05f;  // media anchura (a lo largo del lomo), largo de cada mitad
  const glm::vec3 cover(0.62f, 0.13f, 0.16f), spine(0.42f, 0.08f, 0.10f), pages(0.95f, 0.91f, 0.78f), flipPage(0.99f, 0.96f, 0.84f);
  for (const BookPose& b : books) {
    const glm::vec3 rel(b.pos - cam.pos);
    if (glm::dot(rel, rel) > 24.0f * 24.0f) continue;
    const float a = b.open * 1.30f;  // lo que se abre cada mitad respecto a la vertical (1,3 rad = casi plano)
    glm::mat4 base(1.0f);
    base = glm::translate(base, rel);
    base = glm::rotate(base, b.yaw, glm::vec3(0, 1, 0));
    base = glm::rotate(base, glm::radians(52.0f), glm::vec3(1, 0, 0));  // inclinado hacia quien lo mira
    base = glm::translate(base, glm::vec3(0, -H * 0.5f * (1.0f - b.open) - 0.02f, 0));
    const glm::vec3 lit = b.light;
    // Lomo
    appendBox(v, base, {-W, -0.014f, -0.022f}, {W, 0.006f, 0.022f}, spine * lit);
    // Las dos mitades: cubierta por fuera y páginas por dentro (la segunda, reflejada)
    for (int side = 0; side < 2; side++) {
      glm::mat4 m = glm::rotate(base, side == 0 ? a : -a, glm::vec3(1, 0, 0));
      if (side == 1) m = glm::scale(m, glm::vec3(1, 1, -1));
      appendBox(v, m, {-W, 0.0f, 0.0f}, {W, H, kCover}, cover * lit);
      appendBox(v, m, {-W + 0.014f, 0.012f, -kPages}, {W - 0.014f, H - 0.012f, 0.0f}, pages * lit);
    }
    // La página que se está pasando, de un lado al otro
    if (b.open > 0.6f) {
      const float f = glm::mix(a * 0.85f, -a * 0.85f, b.flip);
      const glm::mat4 m = glm::rotate(base, f, glm::vec3(1, 0, 0));
      appendBox(v, m, {-W + 0.03f, 0.014f, -0.004f}, {W - 0.03f, H - 0.03f, 0.004f}, flipPage * lit);
    }
  }
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  draw(v, cam.viewProj, false, 0.0f);
}

void ItemRenderer::drawBlocks(const std::vector<BlockDraw>& blocks, const Camera& cam) {
  if (blocks.empty()) return;
  std::vector<Vertex> v, flash;
  for (const BlockDraw& b : blocks) {
    appendItem(v, b.block, b.m, b.light, false);
    if (b.flash > 0) appendBox(flash, glm::scale(b.m, glm::vec3(1.002f)), {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, glm::vec3(1.0f));
  }
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  draw(v, cam.viewProj, true, 0.1f);
  if (!flash.empty()) {
    // El destello de la dinamita: el mismo cubo, blanco y a medias
    for (Vertex& q : flash) q.a = 150;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    draw(flash, cam.viewProj, false, 0.0f);
    glDisable(GL_BLEND);
  }
}

void ItemRenderer::drawBreaking(BlockState s, const glm::ivec3& pos, float progress, const Camera& cam) {
  const VariantList* vl = blocks_.forState(s);
  if (!vl) vl = &blocks_.missing();
  const int stage = std::clamp(static_cast<int>(progress * 10.0f), 0, 9);
  std::vector<Vertex> v;
  const glm::vec3 base = glm::vec3(glm::dvec3(pos) - cam.pos);
  for (const BakedQuad& q : vl->models[0].quads) {
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k : order) {
      // Ampliado un pelo alrededor del centro para no pelear con la cara del bloque
      const glm::vec3 p = glm::vec3(q.pos[k][0], q.pos[k][1], q.pos[k][2]);
      const glm::vec3 w = base + glm::vec3(0.5f) + (p - glm::vec3(0.5f)) * 1.002f;
      v.push_back({w.x, w.y, w.z, q.uv[k][0], q.uv[k][1], static_cast<float>(destroyLayer_ + stage), 255, 255, 255, 255});
    }
  }
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -10.0f);
  draw(v, cam.viewProj, true, 0.1f);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
}

void ItemRenderer::drawSelection(const std::vector<AABB>& boxes, const glm::ivec3& pos, const Camera& cam) {
  std::vector<Vertex> v;
  const glm::vec3 base = glm::vec3(glm::dvec3(pos) - cam.pos);
  for (const AABB& b : boxes) {
    const glm::vec3 lo = base + glm::vec3(b.min) - 0.002f, hi = base + glm::vec3(b.max) + 0.002f;
    const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                            {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int edges[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    for (int e : edges) v.push_back({c[e].x, c[e].y, c[e].z, 0, 0, 0, 0, 0, 0, 102});
  }
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  draw(v, cam.viewProj, false, 0.0f, GL_LINES);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void ItemRenderer::drawHeld(const ItemStack& s, const Camera& cam, float swing, float bob, const glm::vec3& light, float bowTicks) {
  if (s.empty()) return;
  // El arco tensado cambia de dibujo según el tiempo (como en 1.8: a partir de 1, 14 y 18 ticks)
  const bool drawing = s.id == ItemId::bow && bowTicks > 0.0f;
  const int variant = !drawing ? 0 : (bowTicks >= 18.0f ? 3 : (bowTicks > 13.0f ? 2 : 1));
  const ItemIcon& icon = items_.icon(s.id, s.meta, variant);
  // Espacio de vista: cámara en el origen mirando a -Z. Proyección propia (no choca con paredes).
  const glm::mat4 proj = glm::perspective(glm::radians(70.0f), cam.proj[1][1] / cam.proj[0][0], 0.01f, 10.0f);
  const float sw = std::sin(swing * 3.14159f);
  const float sw2 = std::sin(std::sqrt(swing) * 3.14159f);
  glm::mat4 m(1.0f);
  const float bobX = std::sin(bob) * 0.012f, bobY = -std::abs(std::cos(bob)) * 0.015f;
  if (icon.kind == ItemIcon::Kind::Block) {
    m = glm::translate(m, glm::vec3(0.52f - sw2 * 0.3f + bobX, -0.45f + sw * 0.15f + bobY, -0.75f - sw * 0.15f));
    m = glm::rotate(m, glm::radians(-sw2 * 60.0f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(45.0f), glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(0.34f));
  } else if (s.id == ItemId::bow) {
    // Arco: de pie, con la combadura hacia delante y la cuerda hacia el jugador. En reposo va bajo y
    // ladeado a la derecha; al tensarlo sube hacia el centro, se pone de canto y tiembla al llegar a tope.
    const float e = drawing ? std::min(1.0f, bowTicks / 5.0f) : 0.0f;
    const float ease = e * e * (3.0f - 2.0f * e);
    float power = bowTicks / 20.0f;
    power = drawing ? std::min(1.0f, (power * power + power * 2.0f) / 3.0f) : 0.0f;
    const float shake = power > 0.1f ? std::sin((bowTicks - 0.1f) * 1.3f) * 0.01f * (power - 0.1f) : 0.0f;
    m = glm::translate(m, glm::vec3(0.5f - 0.26f * ease + (bobX - sw2 * 0.3f) * (1 - ease), -0.4f + 0.2f * ease + (bobY + sw * 0.15f) * (1 - ease) + shake,
                                    -0.75f + 0.08f * ease + power * 0.07f - sw * 0.15f * (1 - ease)));
    m = glm::rotate(m, glm::radians(-sw2 * 70.0f * (1 - ease)), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-40.0f - 32.0f * ease), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(48.0f - 4.0f * ease), glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(0.42f - 0.08f * ease));
  } else {
    // Herramientas y sprites: en diagonal, como sujetados por el mango
    m = glm::translate(m, glm::vec3(0.5f - sw2 * 0.3f + bobX, -0.36f + sw * 0.15f + bobY, -0.7f - sw * 0.15f));
    m = glm::rotate(m, glm::radians(-sw2 * 70.0f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-40.0f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(-8.0f), glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(0.42f));
  }
  std::vector<Vertex> v;
  appendItem(v, s, m, light, true, variant);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  draw(v, proj, true, 0.1f);
  if (s.glints()) drawGlint(v, proj, 0.1f, std::clamp(static_cast<float>(viewportH_) * 0.22f, 40.0f, 240.0f));
}

}  // namespace mcw
