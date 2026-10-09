#include "client/item_renderer.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "assets/item_models.h"
#include "assets/models.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "core/face.h"
#include "game/session.h"

namespace mcw {
namespace {
constexpr float kFaceShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
}

ItemRenderer::~ItemRenderer() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
}

void ItemRenderer::initGL(GLuint textureArray, u16 firstDestroyLayer) {
  texArray_ = textureArray;
  destroyLayer_ = firstDestroyLayer;
  program_ = gl::makeProgram(shaders::kItemVS, shaders::kItemFS, "item");
  uMVP_ = glGetUniformLocation(program_, "uMVP");
  uTex_ = glGetUniformLocation(program_, "uTex");
  uAlphaCutoff_ = glGetUniformLocation(program_, "uAlphaCutoff");
  uTextured_ = glGetUniformLocation(program_, "uTextured");
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
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(mode, 0, static_cast<GLsizei>(v.size()));
  glBindVertexArray(0);
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
  const glm::vec3 tint(((icon.tint >> 16) & 255) / 255.0f, ((icon.tint >> 8) & 255) / 255.0f, (icon.tint & 255) / 255.0f);
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
}

void ItemRenderer::queueIcon(const ItemStack& s, float x, float y) {
  if (!s.empty()) queue_.push_back({s, x, y});
}

void ItemRenderer::flushIcons(int screenW, int screenH, int guiScale) {
  if (queue_.empty()) return;
  std::vector<Vertex> v;
  const float g = static_cast<float>(guiScale);
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
    appendItem(v, q.s, m, glm::vec3(1.0f), false);
  }
  queue_.clear();
  const glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screenW), static_cast<float>(screenH), 0.0f, -1000.0f, 1000.0f);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  draw(v, proj, true, 0.1f);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

void ItemRenderer::drawWorldItems(const std::vector<ItemEntity>& items, const Camera& cam, float partial, double timeTicks, const LightFn& light) {
  if (items.empty()) return;
  std::vector<Vertex> v;
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
      appendItem(v, e.stack, m, light(p + glm::dvec3(0, 0.2, 0)), true);
    }
  }
  (void)timeTicks;
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  draw(v, cam.viewProj, true, 0.1f);
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
}

}  // namespace mcw
