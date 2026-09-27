#include "client/particles.h"

#include <algorithm>
#include <cmath>

#include "client/camera.h"
#include "client/terrain.h"
#include "data/blocks.h"
#include "data/items.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kMaxParticles = 4000;

const char* kParticleVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aUVL;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec3 vUVL;
out vec4 vColor;
out vec2 vCorner;
out float vDist;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUVL = aUVL;
  vColor = aColor;
  vDist = length(aPos.xz);
}
)";

const char* kParticleFS = R"(
uniform highp sampler2DArray uTex;
uniform vec3 uFogColor;
uniform vec2 uFog;
in vec3 vUVL;
in vec4 vColor;
in float vDist;
out vec4 fragColor;
void main() {
  vec4 c;
  if (vUVL.z < 0.0) {
    // Mancha redonda y suave (humo, chispas)
    vec2 d = vUVL.xy * 2.0 - 1.0;
    float a = clamp(1.0 - dot(d, d), 0.0, 1.0);
    c = vec4(vColor.rgb, vColor.a * a);
    if (c.a < 0.02) discard;
  } else {
    c = texture(uTex, vUVL) * vColor;
    if (c.a < 0.5) discard;
  }
  float f = clamp((vDist - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
  fragColor = vec4(mix(c.rgb, uFogColor, f), c.a);
}
)";

struct Vertex {
  float x, y, z, u, v, layer;
  u8 r, g, b, a;
};

}  // namespace

ParticleSystem::~ParticleSystem() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
}

void ParticleSystem::initGL(GLuint blockTextureArray) {
  texArray_ = blockTextureArray;
  program_ = gl::makeProgram(kParticleVS, kParticleFS, "particles");
  uViewProj_ = glGetUniformLocation(program_, "uViewProj");
  uTex_ = glGetUniformLocation(program_, "uTex");
  uFogColor_ = glGetUniformLocation(program_, "uFogColor");
  uFog_ = glGetUniformLocation(program_, "uFog");
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

void ParticleSystem::add(const Particle& p) {
  if (particles_.size() >= kMaxParticles) particles_.erase(particles_.begin());
  particles_.push_back(p);
}

void ParticleSystem::blockBreak(const glm::ivec3& pos, u16 layer, const glm::vec3& tint) {
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      for (int k = 0; k < 4; k++) {
        Particle p;
        p.pos = p.prevPos = glm::dvec3(pos) + glm::dvec3((i + 0.5) / 4.0, (j + 0.5) / 4.0, (k + 0.5) / 4.0);
        glm::dvec3 d = p.pos - (glm::dvec3(pos) + 0.5);
        p.motion = d * 0.2 + glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5) * 0.08;
        p.motion.y += 0.05 + rng_.nextFloat() * 0.05;
        p.maxAge = static_cast<int>(4.0f / (rng_.nextFloat() * 0.9f + 0.1f));
        p.size = p.prevSize = 0.05f + rng_.nextFloat() * 0.05f;
        p.layer = layer;
        p.uv = {rng_.nextFloat() * 0.75f, rng_.nextFloat() * 0.75f};
        p.color = glm::vec4(tint * 0.6f, 1.0f);
        add(p);
      }
}

void ParticleSystem::blockHit(const glm::dvec3& point, int face, u16 layer, const glm::vec3& tint) {
  static const int n[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
  Particle p;
  p.pos = p.prevPos = point + glm::dvec3(n[face][0], n[face][1], n[face][2]) * 0.1 +
                      glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5) * 0.3;
  p.motion = glm::dvec3(n[face][0], n[face][1], n[face][2]) * 0.05 + glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat(), rng_.nextFloat() - 0.5) * 0.05;
  p.maxAge = 6 + rng_.nextInt(10);
  p.size = p.prevSize = 0.04f + rng_.nextFloat() * 0.03f;
  p.layer = layer;
  p.uv = {rng_.nextFloat() * 0.75f, rng_.nextFloat() * 0.75f};
  p.color = glm::vec4(tint * 0.6f, 1.0f);
  add(p);
}

void ParticleSystem::sprintDust(const glm::dvec3& feet, u16 layer, const glm::vec3& tint) {
  Particle p;
  p.pos = p.prevPos = feet + glm::dvec3((rng_.nextFloat() - 0.5) * 0.6, 0.1, (rng_.nextFloat() - 0.5) * 0.6);
  p.motion = {(rng_.nextFloat() - 0.5) * 0.1, 0.12, (rng_.nextFloat() - 0.5) * 0.1};
  p.maxAge = 8 + rng_.nextInt(8);
  p.size = p.prevSize = 0.04f + rng_.nextFloat() * 0.04f;
  p.layer = layer;
  p.uv = {rng_.nextFloat() * 0.75f, rng_.nextFloat() * 0.75f};
  p.color = glm::vec4(tint * 0.6f, 1.0f);
  add(p);
}

void ParticleSystem::smoke(const glm::dvec3& at, int count, float spread, bool large) {
  for (int i = 0; i < count; i++) {
    Particle p;
    p.pos = p.prevPos = at + glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5) * static_cast<double>(spread);
    p.motion = glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() * 0.6, rng_.nextFloat() - 0.5) * (large ? 0.2 : 0.1);
    p.maxAge = (large ? 16 : 10) + rng_.nextInt(large ? 20 : 12);
    p.size = p.prevSize = large ? 0.4f + rng_.nextFloat() * 0.6f : 0.12f + rng_.nextFloat() * 0.12f;
    p.grow = large ? 0.03f : 0.01f;
    const float g = 0.55f + rng_.nextFloat() * 0.35f;
    p.color = {g, g, g, 0.85f};
    p.gravity = -0.004f;
    p.drag = 0.9f;
    p.collide = false;
    p.fade = true;
    add(p);
  }
}

void ParticleSystem::crit(const glm::dvec3& at) {
  for (int i = 0; i < 16; i++) {
    Particle p;
    p.pos = p.prevPos = at;
    const glm::dvec3 d = glm::normalize(glm::dvec3(rng_.nextFloat() * 2 - 1, rng_.nextFloat() * 2 - 1, rng_.nextFloat() * 2 - 1) + glm::dvec3(1e-3));
    p.motion = d * (0.2 + rng_.nextFloat() * 0.3);
    p.motion.y += 0.1;
    p.maxAge = 8 + rng_.nextInt(6);
    p.size = p.prevSize = 0.08f;
    p.color = {1.0f, 0.95f, 0.55f, 1.0f};
    p.gravity = 0.02f;
    p.drag = 0.7f;
    p.fullBright = true;
    p.fade = true;
    add(p);
  }
}

void ParticleSystem::flame(const glm::dvec3& at) {
  Particle p;
  p.pos = p.prevPos = at + glm::dvec3((rng_.nextFloat() - 0.5) * 0.5, rng_.nextFloat() * 0.5, (rng_.nextFloat() - 0.5) * 0.5);
  p.motion = {0, 0.02 + rng_.nextFloat() * 0.02, 0};
  p.maxAge = 8 + rng_.nextInt(6);
  p.size = p.prevSize = 0.1f + rng_.nextFloat() * 0.06f;
  p.grow = -0.006f;
  p.color = {1.0f, 0.55f + rng_.nextFloat() * 0.3f, 0.1f, 1.0f};
  p.gravity = -0.002f;
  p.drag = 0.96f;
  p.collide = false;
  p.fullBright = true;
  add(p);
}

void ParticleSystem::explosion(const glm::dvec3& at) {
  // Bolas grandes de humo y fogonazos
  smoke(at, 24, 4.0f, true);
  for (int i = 0; i < 12; i++) {
    Particle p;
    p.pos = p.prevPos = at + glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5) * 2.5;
    p.maxAge = 6 + rng_.nextInt(6);
    p.size = p.prevSize = 0.8f + rng_.nextFloat() * 0.8f;
    p.grow = 0.08f;
    p.color = {1.0f, 0.8f + rng_.nextFloat() * 0.2f, 0.55f, 0.9f};
    p.gravity = 0;
    p.drag = 1;
    p.collide = false;
    p.fullBright = true;
    p.fade = true;
    add(p);
  }
}

void ParticleSystem::tick(const World& w) {
  for (Particle& p : particles_) {
    p.prevPos = p.pos;
    p.prevSize = p.size;
    p.age++;
    p.size = std::max(0.01f, p.size + p.grow);
    p.motion.y -= p.gravity;
    glm::dvec3 next = p.pos + p.motion;
    if (p.collide) {
      // Choque sencillo: si el siguiente punto cae dentro de un bloque sólido, se para en ese eje
      auto solid = [&](const glm::dvec3& q) {
        const BlockState s = w.block(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)), static_cast<int>(std::floor(q.z)));
        return s != 0 && blockInfo(stateId(s)).fullBox;
      };
      if (solid({p.pos.x, next.y, p.pos.z})) {
        next.y = p.pos.y;
        p.motion.y = 0;
        p.motion.x *= 0.7;
        p.motion.z *= 0.7;
      }
      if (solid({next.x, next.y, p.pos.z})) {
        next.x = p.pos.x;
        p.motion.x = 0;
      }
      if (solid({next.x, next.y, next.z})) {
        next.z = p.pos.z;
        p.motion.z = 0;
      }
    }
    p.pos = next;
    p.motion *= p.drag;
  }
  std::erase_if(particles_, [](const Particle& p) { return p.age >= p.maxAge; });
}

void ParticleSystem::draw(const Camera& cam, float partial, const LightFn& light, const FogParams& fog) {
  if (particles_.empty()) return;
  const glm::vec3 fwd = cam.forward();
  const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
  const glm::vec3 up = glm::cross(right, fwd);
  std::vector<Vertex> v;
  v.reserve(particles_.size() * 6);
  std::vector<std::pair<float, std::size_t>> order;  // las de color, de lejos a cerca (transparencia)
  for (std::size_t i = 0; i < particles_.size(); i++) {
    const Particle& p = particles_[i];
    const glm::vec3 rel(p.prevPos + (p.pos - p.prevPos) * static_cast<double>(partial) - cam.pos);
    if (glm::dot(rel, fwd) < -0.5f || glm::dot(rel, rel) > 96.0f * 96.0f) continue;
    order.push_back({-glm::dot(rel, rel), i});
  }
  std::sort(order.begin(), order.end());
  for (const auto& [d, i] : order) {
    const Particle& p = particles_[i];
    const glm::dvec3 wp = p.prevPos + (p.pos - p.prevPos) * static_cast<double>(partial);
    const glm::vec3 rel(wp - cam.pos);
    const float s = p.prevSize + (p.size - p.prevSize) * partial;
    glm::vec4 c = p.color;
    if (!p.fullBright) c = glm::vec4(glm::vec3(c) * light(wp), c.a);
    if (p.fade) c.a *= 1.0f - static_cast<float>(p.age) / static_cast<float>(p.maxAge);
    const u8 r = static_cast<u8>(std::clamp(c.r, 0.0f, 1.0f) * 255), g = static_cast<u8>(std::clamp(c.g, 0.0f, 1.0f) * 255),
             b = static_cast<u8>(std::clamp(c.b, 0.0f, 1.0f) * 255), a = static_cast<u8>(std::clamp(c.a, 0.0f, 1.0f) * 255);
    const glm::vec3 rx = right * s, uy = up * s;
    const float layer = static_cast<float>(p.layer);
    const float u0 = p.layer >= 0 ? p.uv.x : 0.0f, v0 = p.layer >= 0 ? p.uv.y : 0.0f;
    const float u1 = p.layer >= 0 ? p.uv.x + 0.25f : 1.0f, v1 = p.layer >= 0 ? p.uv.y + 0.25f : 1.0f;
    const Vertex q0{rel.x - rx.x + uy.x, rel.y - rx.y + uy.y, rel.z - rx.z + uy.z, u0, v0, layer, r, g, b, a};
    const Vertex q1{rel.x - rx.x - uy.x, rel.y - rx.y - uy.y, rel.z - rx.z - uy.z, u0, v1, layer, r, g, b, a};
    const Vertex q2{rel.x + rx.x - uy.x, rel.y + rx.y - uy.y, rel.z + rx.z - uy.z, u1, v1, layer, r, g, b, a};
    const Vertex q3{rel.x + rx.x + uy.x, rel.y + rx.y + uy.y, rel.z + rx.z + uy.z, u1, v0, layer, r, g, b, a};
    v.insert(v.end(), {q0, q1, q2, q0, q2, q3});
  }
  if (v.empty()) return;
  glUseProgram(program_);
  glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, &cam.viewProj[0][0]);
  glUniform1i(uTex_, 0);
  glUniform3f(uFogColor_, fog.color.r, fog.color.g, fog.color.b);
  glUniform2f(uFog_, fog.start, fog.end);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

}  // namespace mcw
