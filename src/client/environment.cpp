#include "client/environment.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "assets/pack.h"
#include "client/camera.h"
#include "client/shaders.h"
#include "core/random.h"

namespace mcw {
namespace {
constexpr float kPi = 3.14159265358979f;

/// Brillo de un nivel de luz (0..15): la curva de la tabla de brillo documentada en minecraft.wiki.
float brightness(float level) {
  const float f = 1.0f - level / 15.0f;
  return (1.0f - f) / (f * 3.0f + 1.0f);
}
}  // namespace

Environment::~Environment() {
  GLuint tex[] = {lightmap_, sunTex_, moonTex_, cloudTex_, whiteTex_};
  glDeleteTextures(5, tex);
  GLuint bufs[] = {bbVbo_, bbEbo_};
  glDeleteBuffers(2, bufs);
  GLuint vaos[] = {emptyVao_, bbVao_};
  glDeleteVertexArrays(2, vaos);
  if (skyProgram_) glDeleteProgram(skyProgram_);
  if (bbProgram_) glDeleteProgram(bbProgram_);
}

GLuint Environment::loadTexture(const PackStack& packs, const char* path, bool repeat) {
  auto img = packs.readImage(path);
  Image fallback(1, 1, 0xFFFFFFFF);
  const Image& im = img ? *img : fallback;
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.width, im.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  return t;
}

void Environment::initGL(const PackStack& packs) {
  skyProgram_ = gl::makeProgram(shaders::kSkyVS, shaders::kSkyFS, "sky");
  bbProgram_ = gl::makeProgram(shaders::kBillboardVS, shaders::kBillboardFS, "billboard");
  glGenVertexArrays(1, &emptyVao_);

  glGenVertexArrays(1, &bbVao_);
  glGenBuffers(1, &bbVbo_);
  glGenBuffers(1, &bbEbo_);
  glBindVertexArray(bbVao_);
  glBindBuffer(GL_ARRAY_BUFFER, bbVbo_);
  std::vector<u32> idx;
  for (u32 q = 0; q < 4096; q++) {
    const u32 b = q * 4;
    idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bbEbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);

  sunTex_ = loadTexture(packs, "assets/minecraft/textures/environment/sun.png", false);
  moonTex_ = loadTexture(packs, "assets/minecraft/textures/environment/moon_phases.png", false);
  cloudTex_ = loadTexture(packs, "assets/minecraft/textures/environment/clouds.png", true);
  const u32 white = 0xFFFFFFFF;
  glGenTextures(1, &whiteTex_);
  glBindTexture(GL_TEXTURE_2D, whiteTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenTextures(1, &lightmap_);
  glBindTexture(GL_TEXTURE_2D, lightmap_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Estrellas fijas en una esfera
  Random rng(10842);
  for (int i = 0; i < 1500; i++) {
    glm::vec3 d(rng.nextFloat() * 2 - 1, rng.nextFloat() * 2 - 1, rng.nextFloat() * 2 - 1);
    const float l = glm::length(d);
    if (l < 0.01f || l > 1) continue;
    stars_.push_back(d / l);
  }
}

void Environment::update(double time, float renderDistanceBlocks, float gamma, const glm::vec3& viewDir) {
  // Ángulo del sol: 0 = mediodía (tick 6000), el sol sale por el este (+X) y se pone por el oeste.
  const double dayFrac = std::fmod(time, 24000.0) / 24000.0;
  celestial_ = static_cast<float>(std::fmod(dayFrac - 0.25 + 1.0, 1.0));
  const float angle = celestial_ * 2 * kPi;
  sunDir_ = glm::normalize(glm::vec3(-std::sin(angle), std::cos(angle), 0.0f) + glm::vec3(0, 0, 0.0001f));
  moonPhase_ = static_cast<int>(std::floor(time / 24000.0)) % 8;

  const float sunHeight = std::cos(angle);
  daylight_ = std::clamp(sunHeight * 2.0f + 0.5f, 0.0f, 1.0f);
  starBrightness_ = std::clamp(1.0f - (sunHeight * 2.0f + 0.75f), 0.0f, 1.0f) * 0.5f;

  const glm::vec3 daySky(0.47f, 0.65f, 1.0f), nightSky(0.01f, 0.01f, 0.03f);
  skyColor_ = glm::mix(nightSky, daySky, daylight_);
  glm::vec3 fogDay(0.75f, 0.85f, 1.0f), fogNight(0.02f, 0.02f, 0.05f);
  glm::vec3 fogColor = glm::mix(fogNight, fogDay, daylight_);
  // Amanecer y atardecer: brillo naranja cerca del horizonte
  const float sunset = std::clamp(1.0f - std::abs(sunHeight) * 2.5f, 0.0f, 1.0f);
  sunset_ = glm::vec4(1.0f, 0.55f, 0.25f, sunset * 0.8f);
  const glm::vec2 v(viewDir.x, viewDir.z), sd(sunDir_.x, sunDir_.z);
  const float towardSun = glm::length(v) > 1e-4f ? std::max(0.0f, glm::dot(glm::normalize(v), glm::normalize(sd))) : 0.0f;
  fogColor = glm::mix(fogColor, glm::vec3(sunset_), sunset * 0.35f * towardSun);
  voidColor_ = fogColor * 0.6f;

  fog_.color = fogColor;
  fog_.end = renderDistanceBlocks;
  fog_.start = renderDistanceBlocks * 0.75f;

  // Lightmap: (x = luz de bloque, y = luz de cielo)
  const float skyScale = 0.2f + 0.8f * daylight_;
  u8 data[16 * 16 * 4];
  for (int s = 0; s < 16; s++)
    for (int b = 0; b < 16; b++) {
      const float sk = brightness(static_cast<float>(s)) * skyScale;
      const float bl = brightness(static_cast<float>(b));
      glm::vec3 c = glm::vec3(sk * (0.95f + 0.05f * daylight_), sk * (0.95f + 0.05f * daylight_), sk) +
                    glm::vec3(bl, bl * (0.8f + 0.2f * bl), bl * (0.6f + 0.4f * bl * bl));
      c = glm::clamp(c * 0.96f + 0.04f, 0.0f, 1.0f);
      const glm::vec3 inv = 1.0f - c;
      c = glm::mix(c, 1.0f - inv * inv * inv * inv, gamma);
      lightmapCpu_[s * 16 + b] = c;
      u8* p = &data[(s * 16 + b) * 4];
      p[0] = static_cast<u8>(c.r * 255);
      p[1] = static_cast<u8>(c.g * 255);
      p[2] = static_cast<u8>(c.b * 255);
      p[3] = 255;
    }
  glBindTexture(GL_TEXTURE_2D, lightmap_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void Environment::drawQuads(const std::vector<Vertex>& verts, GLuint texture, const Camera& cam, bool additive, float alphaCutoff, bool fog) {
  if (verts.empty()) return;
  glUseProgram(bbProgram_);
  glUniformMatrix4fv(glGetUniformLocation(bbProgram_, "uViewProj"), 1, GL_FALSE, &cam.viewProj[0][0]);
  glUniform1i(glGetUniformLocation(bbProgram_, "uTex"), 0);
  glUniform1f(glGetUniformLocation(bbProgram_, "uAlphaCutoff"), alphaCutoff);
  glUniform3f(glGetUniformLocation(bbProgram_, "uFogColor"), fog_.color.r, fog_.color.g, fog_.color.b);
  glUniform2f(glGetUniformLocation(bbProgram_, "uFog"), fog ? fog_.start : 1e6f, fog ? cloudExtent_ * 0.95f : 2e6f);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glEnable(GL_BLEND);
  if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(bbVao_);
  glBindBuffer(GL_ARRAY_BUFFER, bbVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(verts.size() / 4 * 6), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
}

void Environment::drawSky(const Camera& cam) {
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(skyProgram_);
  const glm::mat4 inv = glm::inverse(cam.viewProj);
  glUniformMatrix4fv(glGetUniformLocation(skyProgram_, "uInvViewProj"), 1, GL_FALSE, &inv[0][0]);
  glUniform3f(glGetUniformLocation(skyProgram_, "uSkyColor"), skyColor_.r, skyColor_.g, skyColor_.b);
  glUniform3f(glGetUniformLocation(skyProgram_, "uFogColor"), fog_.color.r, fog_.color.g, fog_.color.b);
  glUniform3f(glGetUniformLocation(skyProgram_, "uVoidColor"), voidColor_.r, voidColor_.g, voidColor_.b);
  glUniform3f(glGetUniformLocation(skyProgram_, "uSunDir"), sunDir_.x, sunDir_.y, sunDir_.z);
  glUniform4f(glGetUniformLocation(skyProgram_, "uSunset"), sunset_.r, sunset_.g, sunset_.b, sunset_.a);
  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // Quad orientado hacia el centro a lo largo de una dirección
  auto body = [](std::vector<Vertex>& out, glm::vec3 dir, float size, float dist, glm::vec4 uv, glm::vec4 col) {
    const glm::vec3 c = dir * dist;
    const glm::vec3 ref = std::abs(dir.z) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    const glm::vec3 right = glm::normalize(glm::cross(dir, ref));
    const glm::vec3 up = glm::normalize(glm::cross(right, dir));
    const auto rgba = [&](Vertex v) {
      v.r = static_cast<u8>(col.r * 255); v.g = static_cast<u8>(col.g * 255); v.b = static_cast<u8>(col.b * 255); v.a = static_cast<u8>(col.a * 255);
      return v;
    };
    const glm::vec3 p0 = c - right * size + up * size, p1 = c - right * size - up * size, p2 = c + right * size - up * size, p3 = c + right * size + up * size;
    out.push_back(rgba({p0.x, p0.y, p0.z, uv.x, uv.y, 0, 0, 0, 0}));
    out.push_back(rgba({p1.x, p1.y, p1.z, uv.x, uv.w, 0, 0, 0, 0}));
    out.push_back(rgba({p2.x, p2.y, p2.z, uv.z, uv.w, 0, 0, 0, 0}));
    out.push_back(rgba({p3.x, p3.y, p3.z, uv.z, uv.y, 0, 0, 0, 0}));
  };

  if (starBrightness_ > 0) {
    std::vector<Vertex> st;
    for (std::size_t i = 0; i < stars_.size(); i++) {
      const float s = 0.15f + 0.1f * static_cast<float>(hash3(static_cast<int>(i), 1, 2) % 100) / 100.0f;
      // Las estrellas giran con el cielo
      const float a = celestial_ * 2 * kPi;
      const glm::vec3 d = stars_[i];
      const glm::vec3 r(d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a), d.z);
      body(st, glm::normalize(r), s, 100, {0, 0, 1, 1}, {1, 1, 1, starBrightness_});
    }
    drawQuads(st, whiteTex_, cam, true, 0.0f, false);
  }
  std::vector<Vertex> sun, moon;
  body(sun, sunDir_, 30, 100, {0, 0, 1, 1}, {1, 1, 1, 1});
  drawQuads(sun, sunTex_, cam, true, 0.0f, false);
  const float mu = (moonPhase_ % 4) / 4.0f, mv = (moonPhase_ / 4) / 2.0f;
  body(moon, -sunDir_, 20, 100, {mu, mv, mu + 0.25f, mv + 0.5f}, {1, 1, 1, 1});
  drawQuads(moon, moonTex_, cam, true, 0.0f, false);

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
}

void Environment::drawClouds(const Camera& cam, double timeTicks) {
  // Capa plana a y=128, celdas de 12 bloques por píxel de clouds.png, desplazándose hacia el oeste.
  const float height = static_cast<float>(128.0 - cam.pos.y) + 0.33f;
  const float extent = std::max(fog_.end * 2.5f, 256.0f);
  cloudExtent_ = extent;
  const double scroll = timeTicks * 0.03;
  const double texScale = 1.0 / (256.0 * 12.0);
  // Se quita la parte entera (la textura se repite) para no perder precisión en float
  const double uc = (cam.pos.x + scroll) * texScale, vc = cam.pos.z * texScale;
  const double ub = std::floor(uc), vb = std::floor(vc);
  const double u0 = uc - ub - extent * texScale, u1 = uc - ub + extent * texScale;
  const double v0 = vc - vb - extent * texScale, v1 = vc - vb + extent * texScale;
  const float b = 0.15f + 0.85f * daylight_;
  const u8 c = static_cast<u8>(b * 255);
  std::vector<Vertex> q = {
      {-extent, height, -extent, static_cast<float>(u0), static_cast<float>(v0), c, c, c, 200},
      {-extent, height, extent, static_cast<float>(u0), static_cast<float>(v1), c, c, c, 200},
      {extent, height, extent, static_cast<float>(u1), static_cast<float>(v1), c, c, c, 200},
      {extent, height, -extent, static_cast<float>(u1), static_cast<float>(v0), c, c, c, 200},
  };
  // Se ven por las dos caras
  q.push_back(q[0]); q.push_back(q[3]); q.push_back(q[2]); q.push_back(q[1]);
  glDepthMask(GL_FALSE);
  drawQuads(q, cloudTex_, cam, false, 0.1f, true);
  glDepthMask(GL_TRUE);
}

}  // namespace mcw

namespace mcw {
glm::vec3 Environment::lightColor(float sky, float block) const {
  const int s = std::clamp(static_cast<int>(std::lround(sky)), 0, 15), b = std::clamp(static_cast<int>(std::lround(block)), 0, 15);
  return lightmapCpu_[s * 16 + b];
}
}  // namespace mcw
