#pragma once
#include <glm/glm.hpp>

#include "client/gl.h"
#include "client/terrain.h"

namespace mcw {

class PackStack;
struct Camera;

/// Cielo, sol, luna, estrellas, nubes, niebla y el lightmap (brillo según la hora).
class Environment {
 public:
  ~Environment();
  void initGL(const PackStack& packs);

  /// `time` en ticks del día (0..24000; 6000 = mediodía), como el reloj del juego.
  /// `viewDir`: hacia dónde mira la cámara (la niebla toma el color del atardecer al mirar al sol).
  void update(double time, float renderDistanceBlocks, float gamma, const glm::vec3& viewDir);
  void drawSky(const Camera& cam);
  void drawClouds(const Camera& cam, double timeTicks);

  GLuint lightmap() const { return lightmap_; }
  const FogParams& fog() const { return fog_; }
  float daylight() const { return daylight_; }

 private:
  struct Vertex { float x, y, z, u, v; u8 r, g, b, a; };
  void drawQuads(const std::vector<Vertex>& verts, GLuint texture, const Camera& cam, bool additive, float alphaCutoff, bool fog);
  GLuint loadTexture(const PackStack& packs, const char* path, bool repeat);

  GLuint skyProgram_ = 0, bbProgram_ = 0, emptyVao_ = 0, bbVao_ = 0, bbVbo_ = 0, bbEbo_ = 0;
  GLuint lightmap_ = 0, sunTex_ = 0, moonTex_ = 0, cloudTex_ = 0, whiteTex_ = 0;
  glm::vec3 skyColor_{0.47f, 0.65f, 1.0f}, voidColor_{0.1f, 0.1f, 0.2f};
  glm::vec4 sunset_{0};
  glm::vec3 sunDir_{0, 1, 0};
  float daylight_ = 1, starBrightness_ = 0, celestial_ = 0, cloudExtent_ = 256;
  int moonPhase_ = 0;
  FogParams fog_;
  std::vector<glm::vec3> stars_;
};

}  // namespace mcw
