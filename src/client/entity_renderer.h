#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "client/entity_models.h"
#include "client/gl.h"
#include "game/mob.h"

namespace mcw {

class PackStack;
struct Camera;
struct FogParams;

/// Dibuja las criaturas (modelos de cajas animados) y las flechas.
class EntityRenderer {
 public:
  explicit EntityRenderer(const PackStack& packs) : packs_(packs) {}
  ~EntityRenderer();
  void initGL();

  using LightFn = std::function<glm::vec3(const glm::dvec3&)>;
  /// Criaturas a menos de `maxDist` bloques. `partial` interpola entre ticks.
  void drawMobs(const std::vector<Mob>& mobs, const Camera& cam, float partial, const LightFn& light, const FogParams& fog,
                float maxDist);
  void drawArrows(const std::vector<Arrow>& arrows, const Camera& cam, float partial, const LightFn& light, const FogParams& fog);
  /// Brazo del jugador en primera persona (con la mano vacía). `swing` 0..1 = golpe.
  void drawFirstPersonArm(const Camera& cam, float swing, float bob, const glm::vec3& light);
  /// Personaje del jugador en la ventana del inventario (en píxeles de pantalla, mirando al ratón).
  void drawPlayerPreview(float cx, float feetY, float scale, float lookX, float lookY, int screenW, int screenH, const glm::vec3& light);

 private:
  struct Vertex {
    float x, y, z, u, v;
    u8 r, g, b, a;
    u8 or_, og, ob, oa;
  };
  struct Tex {
    GLuint id = 0;
    float w = 64, h = 32;
  };
  const Tex& texture(const std::string& path);
  /// Añade un modelo con su pose. `m` lleva de píxeles del modelo a coordenadas de cámara.
  void appendModel(std::vector<Vertex>& out, const EntityModel& model, const Pose& pose, const glm::mat4& m, const Tex& tex,
                   const glm::vec3& color, bool shaded, const glm::vec4& overlay) const;
  void draw(const std::vector<Vertex>& v, GLuint tex, const glm::mat4& viewProj, const FogParams* fog);

  const PackStack& packs_;
  std::unordered_map<std::string, Tex> textures_;
  GLuint program_ = 0, vao_ = 0, vbo_ = 0;
  GLint uViewProj_ = -1, uTex_ = -1, uFogColor_ = -1, uFog_ = -1;
};

/// Color de la lana de las ovejas (16 colores de tinte).
glm::vec3 fleeceColor(int color);

}  // namespace mcw
