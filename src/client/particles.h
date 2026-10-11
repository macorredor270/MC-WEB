#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <vector>

#include "client/gl.h"
#include "core/random.h"
#include "core/types.h"

namespace mcw {

class World;
struct Camera;
struct FogParams;

/// Partículas: trozos de bloque al romper y al pisar, humo, chispas de crítico, llamas.
/// Son solo efecto visual (del cliente); se mueven a 20 ticks por segundo y chocan con los bloques.
class ParticleSystem {
 public:
  ~ParticleSystem();
  void initGL(GLuint blockTextureArray);

  void tick(const World& world);
  /// Cantidad de partículas: 0 todas, 1 menos (la mitad), 2 mínimas (una de cada cinco).
  void setLevel(int level) { level_ = level; }

  /// Trozos de la textura de un bloque (4x4x4 al romperlo, como en el juego).
  void blockBreak(const glm::ivec3& pos, u16 layer, const glm::vec3& tint);
  /// Una esquirla en la cara golpeada mientras se pica.
  void blockHit(const glm::dvec3& point, int face, u16 layer, const glm::vec3& tint);
  /// Polvo bajo los pies al correr.
  void sprintDust(const glm::dvec3& feet, u16 layer, const glm::vec3& tint);
  void smoke(const glm::dvec3& at, int count, float spread, bool large);
  void crit(const glm::dvec3& at);
  void flame(const glm::dvec3& at);
  void explosion(const glm::dvec3& at);
  /// Corazones que suben sobre un animal en modo amor.
  void hearts(const glm::dvec3& at, int count);

  using LightFn = std::function<glm::vec3(const glm::dvec3&)>;
  void draw(const Camera& cam, float partial, const LightFn& light, const FogParams& fog);
  std::size_t count() const { return particles_.size(); }
  void clear() { particles_.clear(); }

 private:
  struct Particle {
    glm::dvec3 pos{0}, prevPos{0}, motion{0};
    int age = 0, maxAge = 20;
    float size = 0.1f, prevSize = 0.1f, grow = 0.0f;
    int layer = -1;                  // >= 0: trozo de textura de bloque; -1: mancha de color
    glm::vec2 uv{0};                 // esquina del trozo (0..1)
    glm::vec4 color{1};
    float gravity = 0.04f, drag = 0.98f;
    bool collide = true, fullBright = false, fade = false;
    bool heart = false;              // se dibuja como un corazón de píxeles (no como un cuadrado)
  };
  void add(const Particle& p);

  std::vector<Particle> particles_;
  Random rng_{0xA11CE};
  int level_ = 0;
  GLuint program_ = 0, vao_ = 0, vbo_ = 0, texArray_ = 0;
  GLint uViewProj_ = -1, uTex_ = -1, uFogColor_ = -1, uFog_ = -1;
};

}  // namespace mcw
