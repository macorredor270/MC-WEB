#include "client/entity_renderer.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <numbers>

#include "assets/pack.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "client/terrain.h"

namespace mcw {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float lerpAngle(float a, float b, float t) {
  float d = b - a;
  while (d > kPi) d -= 2 * kPi;
  while (d < -kPi) d += 2 * kPi;
  return a + d * t;
}

/// Iluminación fija de las entidades: dos luces direccionales desde arriba (como en el juego).
float entityShade(const glm::vec3& n) {
  static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f)), l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
  return std::min(1.0f, 0.4f + 0.6f * (std::max(0.0f, glm::dot(n, l0)) + std::max(0.0f, glm::dot(n, l1))));
}

}  // namespace

glm::vec3 fleeceColor(int c) {
  static const glm::vec3 k[16] = {{1.00f, 1.00f, 1.00f}, {0.85f, 0.50f, 0.20f}, {0.70f, 0.30f, 0.85f}, {0.40f, 0.60f, 0.85f},
                                  {0.90f, 0.90f, 0.20f}, {0.50f, 0.80f, 0.10f}, {0.95f, 0.50f, 0.65f}, {0.30f, 0.30f, 0.30f},
                                  {0.60f, 0.60f, 0.60f}, {0.30f, 0.50f, 0.60f}, {0.50f, 0.25f, 0.70f}, {0.20f, 0.30f, 0.70f},
                                  {0.40f, 0.30f, 0.20f}, {0.40f, 0.50f, 0.20f}, {0.60f, 0.20f, 0.20f}, {0.10f, 0.10f, 0.10f}};
  return k[c & 15];
}

EntityRenderer::~EntityRenderer() {
  for (auto& [k, t] : textures_) glDeleteTextures(1, &t.id);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
}

void EntityRenderer::initGL() {
  program_ = gl::makeProgram(shaders::kEntityVS, shaders::kEntityFS, "entity");
  uViewProj_ = glGetUniformLocation(program_, "uViewProj");
  uTex_ = glGetUniformLocation(program_, "uTex");
  uFogColor_ = glGetUniformLocation(program_, "uFogColor");
  uFog_ = glGetUniformLocation(program_, "uFog");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei stride = sizeof(Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, r)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, or_)));
  glBindVertexArray(0);
}

const EntityRenderer::Tex& EntityRenderer::texture(const std::string& path) {
  auto it = textures_.find(path);
  if (it != textures_.end()) return it->second;
  Tex t;
  auto img = packs_.readImage("assets/minecraft/textures/" + path);
  Image data = img ? *img : Image(64, 32, 0xFFFF00FF);
  glGenTextures(1, &t.id);
  glBindTexture(GL_TEXTURE_2D, t.id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // Tamaño nominal para las UV: el ancho de los modelos es 64 y el alto sale de la proporción
  // (así sirven texturas de 64x32, 64x64 o de packs en alta resolución).
  t.w = 64.0f;
  t.h = 64.0f * static_cast<float>(data.height) / static_cast<float>(std::max(1, data.width));
  return textures_.emplace(path, t).first->second;
}

void EntityRenderer::appendModel(std::vector<Vertex>& out, const EntityModel& model, const Pose& pose, const glm::mat4& m,
                                 const Tex& tex, const glm::vec3& color, bool shaded, const glm::vec4& overlay) const {
  std::vector<glm::mat4> world(model.parts.size());
  for (std::size_t i = 0; i < model.parts.size(); i++) {
    const ModelPart& p = model.parts[i];
    const glm::vec3 r = p.rot + (i < pose.rot.size() ? pose.rot[i] : glm::vec3(0));
    glm::mat4 t = p.parent >= 0 ? world[static_cast<std::size_t>(p.parent)] : m;
    t = glm::translate(t, p.pivot);
    if (r.z != 0) t = glm::rotate(t, r.z, glm::vec3(0, 0, 1));
    if (r.y != 0) t = glm::rotate(t, r.y, glm::vec3(0, 1, 0));
    if (r.x != 0) t = glm::rotate(t, r.x, glm::vec3(1, 0, 0));
    world[i] = t;
  }
  const u8 oR = static_cast<u8>(overlay.r * 255), oG = static_cast<u8>(overlay.g * 255), oB = static_cast<u8>(overlay.b * 255),
           oA = static_cast<u8>(overlay.a * 255);
  for (std::size_t i = 0; i < model.parts.size(); i++) {
    const glm::mat4& t = world[i];
    const glm::mat3 nrm(t);
    for (const ModelBox& b : model.parts[i].boxes) {
      const float x0 = b.min.x, y0 = b.min.y, z0 = b.min.z, x1 = b.max.x, y1 = b.max.y, z1 = b.max.z;
      const float U = b.uv.x, V = b.uv.y, w = b.size.x, h = b.size.y, d = b.size.z;
      struct Face {
        glm::vec3 c[4];     // arriba-izq, abajo-izq, abajo-der, arriba-der (visto desde fuera)
        glm::vec4 uv;       // u0, v0, u1, v1
        glm::vec3 n;
      };
      Face faces[6] = {
          {{{x1, y1, z1}, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}}, {U, V + d, U + d, V + d + h}, {1, 0, 0}},                 // derecha
          {{{x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}}, {U + d, V + d, U + d + w, V + d + h}, {0, 0, -1}},         // delante
          {{{x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}}, {U + d + w, V + d, U + 2 * d + w, V + d + h}, {-1, 0, 0}},  // izquierda
          {{{x0, y1, z1}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}}, {U + 2 * d + w, V + d, U + 2 * d + 2 * w, V + d + h}, {0, 0, 1}},  // detrás
          {{{x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {x0, y1, z1}}, {U + d, V, U + d + w, V + d}, {0, 1, 0}},                  // arriba
          {{{x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {x0, y0, z0}}, {U + d + w, V, U + d + 2 * w, V + d}, {0, -1, 0}},         // abajo
      };
      if (b.mirror) std::swap(faces[0].uv, faces[2].uv);
      for (Face& f : faces) {
        float u0 = f.uv.x, u1 = f.uv.z;
        if (b.mirror) std::swap(u0, u1);
        const float us[4] = {u0, u0, u1, u1}, vs[4] = {f.uv.y, f.uv.w, f.uv.w, f.uv.y};
        const glm::vec3 n = glm::normalize(nrm * f.n);
        const float k = shaded ? entityShade(n) : 1.0f;
        const u8 cr = static_cast<u8>(std::clamp(color.r * k, 0.0f, 1.0f) * 255), cg = static_cast<u8>(std::clamp(color.g * k, 0.0f, 1.0f) * 255),
                 cb = static_cast<u8>(std::clamp(color.b * k, 0.0f, 1.0f) * 255);
        Vertex q[4];
        for (int j = 0; j < 4; j++) {
          const glm::vec4 p = t * glm::vec4(f.c[j], 1.0f);
          q[j] = {p.x, p.y, p.z, us[j] / tex.w, vs[j] / tex.h, cr, cg, cb, 255, oR, oG, oB, oA};
        }
        // Dos triángulos
        out.push_back(q[0]);
        out.push_back(q[1]);
        out.push_back(q[2]);
        out.push_back(q[0]);
        out.push_back(q[2]);
        out.push_back(q[3]);
      }
    }
  }
}

void EntityRenderer::draw(const std::vector<Vertex>& v, GLuint tex, const glm::mat4& viewProj, const FogParams* fog) {
  if (v.empty()) return;
  glUseProgram(program_);
  glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, &viewProj[0][0]);
  glUniform1i(uTex_, 0);
  if (fog) {
    glUniform3f(uFogColor_, fog->color.r, fog->color.g, fog->color.b);
    glUniform2f(uFog_, fog->start, fog->end);
  } else {
    glUniform3f(uFogColor_, 0, 0, 0);
    glUniform2f(uFog_, 1e6f, 2e6f);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
  glBindVertexArray(0);
}

void EntityRenderer::drawMobs(const std::vector<Mob>& mobs, const Camera& cam, float partial, const LightFn& light,
                              const FogParams& fog, float maxDist) {
  if (mobs.empty()) return;
  std::map<GLuint, std::vector<Vertex>> batches, emissive;
  const glm::vec3 fwd = cam.forward();
  for (const Mob& mob : mobs) {
    const glm::dvec3 pos = mob.prevPos + (mob.pos - mob.prevPos) * static_cast<double>(partial);
    const glm::vec3 rel(pos - cam.pos);
    const float dist = glm::length(rel);
    if (dist > maxDist) continue;
    if (dist > 4.0f && glm::dot(rel, fwd) < -mob.info().width * 2.0f) continue;  // detrás de la cámara

    const MobModel& mm = mobModel(mob.type);
    const float yaw = lerpAngle(mob.prevYaw, mob.yaw, partial);
    const float headYaw = lerpAngle(mob.prevHeadYaw, mob.headYaw, partial);
    const float pitch = mob.prevPitch + (mob.pitch - mob.prevPitch) * partial;
    const float amount = mob.prevLimbAmount + (mob.limbAmount - mob.prevLimbAmount) * partial;
    const float swing = mob.limbSwing - mob.limbAmount * (1.0f - partial);
    const float age = static_cast<float>(mob.age) + partial;
    float headRel = headYaw - yaw;
    while (headRel > kPi) headRel -= 2 * kPi;
    while (headRel < -kPi) headRel += 2 * kPi;

    glm::mat4 m = glm::translate(glm::mat4(1.0f), rel);
    m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
    if (mob.dying()) {
      // Cae de lado
      float f = std::sqrt(std::max(0.0f, (mob.deathTime + partial - 1.0f) / 20.0f * 1.6f));
      m = glm::rotate(m, std::min(f, 1.0f) * kPi / 2, glm::vec3(0, 0, 1));
    }
    glm::vec4 overlay(0);
    if (mob.type == MobType::Creeper && (mob.fuse > 0 || mob.prevFuse > 0)) {
      // Se hincha y parpadea en blanco antes de explotar
      float f = std::clamp((mob.prevFuse + (mob.fuse - mob.prevFuse) * partial) / 28.0f, 0.0f, 1.0f);
      const float wobble = 1.0f + std::sin(f * 100.0f) * f * 0.01f;
      const float f4 = f * f * f * f;
      m = glm::scale(m, glm::vec3((1.0f + f4 * 0.4f) * wobble, (1.0f + f4 * 0.1f) / wobble, (1.0f + f4 * 0.4f) * wobble));
      if (static_cast<int>(f * 10.0f) % 2 == 1) overlay = {1, 1, 1, std::clamp(f * 0.2f + 0.2f, 0.0f, 1.0f)};
    }
    if (mob.hurtTime > 0 || mob.dying()) overlay = {1, 0, 0, 0.3f};
    if (mob.fireTicks > 0 && overlay.a == 0) overlay = {1.0f, 0.55f, 0.1f, 0.15f + 0.1f * std::sin(age * 1.7f)};
    m = glm::scale(m, glm::vec3(1.0f / 16.0f));

    const glm::vec3 lc = light(pos + glm::dvec3(0, mob.info().height * 0.5, 0));
    const Pose pose = poseFor(mob.type, mm, swing, amount, headRel, pitch, age, mob.onGround, mob.eatGrassTicks);
    const Tex& tex = texture(mm.texture);
    appendModel(batches[tex.id], mm.model, pose, m, tex, lc, true, overlay);
    if (!mm.overlay.empty()) {
      if (mob.type == MobType::Sheep && mob.sheared) continue;
      const Tex& ot = texture(mm.overlay);
      if (mm.overlayEmissive) {
        appendModel(emissive[ot.id], mm.overlayModel, pose, m, ot, glm::vec3(1), false, glm::vec4(0));
      } else {
        const glm::vec3 tint = mob.type == MobType::Sheep ? fleeceColor(mob.woolColor) : glm::vec3(1);
        appendModel(batches[ot.id], mm.overlayModel, pose, m, ot, lc * tint, true, overlay);
      }
    }
  }
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  for (auto& [tex, v] : batches) draw(v, tex, cam.viewProj, &fog);
  // Ojos de araña: por encima, sin luz (se ven a oscuras)
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  for (auto& [tex, v] : emissive) draw(v, tex, cam.viewProj, &fog);
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void EntityRenderer::drawArrows(const std::vector<Arrow>& arrows, const Camera& cam, float partial, const LightFn& light,
                                const FogParams& fog) {
  if (arrows.empty()) return;
  const Tex& tex = texture("entity/arrow.png");
  std::vector<Vertex> v;
  for (const Arrow& a : arrows) {
    const glm::dvec3 pos = a.prevPos + (a.pos - a.prevPos) * static_cast<double>(partial);
    const glm::vec3 rel(pos - cam.pos);
    if (glm::length(rel) > 64.0f) continue;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), rel);
    m = glm::rotate(m, a.yaw, glm::vec3(0, 1, 0));
    m = glm::rotate(m, a.pitch, glm::vec3(1, 0, 0));
    if (a.shake > 0) m = glm::rotate(m, std::sin(a.shake * 3.0f) * a.shake * 0.05f, glm::vec3(1, 0, 0));
    m = glm::scale(m, glm::vec3(0.05625f));
    const glm::vec3 lc = light(pos);
    const u8 r = static_cast<u8>(std::clamp(lc.r, 0.0f, 1.0f) * 255), g = static_cast<u8>(std::clamp(lc.g, 0.0f, 1.0f) * 255),
             b = static_cast<u8>(std::clamp(lc.b, 0.0f, 1.0f) * 255);
    auto quad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float u0, float v0, float u1, float v1) {
      const glm::vec3 ps[4] = {p0, p1, p2, p3};
      const float us[4] = {u0, u0, u1, u1}, vs[4] = {v0, v1, v1, v0};
      Vertex q[4];
      for (int j = 0; j < 4; j++) {
        const glm::vec4 p = m * glm::vec4(ps[j], 1.0f);
        q[j] = {p.x, p.y, p.z, us[j] / tex.w, vs[j] / tex.h, r, g, b, 255, 0, 0, 0, 0};
      }
      v.insert(v.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
    };
    // Dos planos cruzados a lo largo de la flecha (la punta hacia -Z) y las plumas detrás
    quad({0, 2.5f, 8}, {0, -2.5f, 8}, {0, -2.5f, -8}, {0, 2.5f, -8}, 0, 0, 16, 5);
    quad({-2.5f, 0, 8}, {2.5f, 0, 8}, {2.5f, 0, -8}, {-2.5f, 0, -8}, 0, 0, 16, 5);
    quad({-2.5f, 2.5f, 7}, {-2.5f, -2.5f, 7}, {2.5f, -2.5f, 7}, {2.5f, 2.5f, 7}, 0, 5, 5, 10);
  }
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  draw(v, tex.id, cam.viewProj, &fog);
}

void EntityRenderer::drawPlayerPreview(float cx, float feetY, float scale, float lookX, float lookY, int screenW, int screenH,
                                       const glm::vec3& light) {
  const MobModel& mm = playerModel();
  const Tex& tex = texture(mm.texture);
  // Mira hacia el ratón, como en el inventario del juego
  const float bodyYaw = std::atan(lookX / 40.0f) * 0.35f;
  const float headYaw = std::atan(lookX / 40.0f) * 0.7f - bodyYaw;
  const float pitch = std::atan(-lookY / 40.0f) * 0.5f;
  Pose pose;
  pose.rot.assign(mm.model.parts.size(), glm::vec3(0));
  pose.rot[mm.rig.head] = {pitch, headYaw, 0};
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, feetY, 0.0f));
  m = glm::scale(m, glm::vec3(scale / 16.0f, -scale / 16.0f, scale / 16.0f));  // en pantalla y crece hacia abajo
  m = glm::rotate(m, kPi + bodyYaw, glm::vec3(0, 1, 0));  // de cara a quien mira
  std::vector<Vertex> v;
  appendModel(v, mm.model, pose, m, tex, light, true, glm::vec4(0));
  const glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screenW), static_cast<float>(screenH), 0.0f, -1000.0f, 1000.0f);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  draw(v, tex.id, proj, nullptr);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

}  // namespace mcw
