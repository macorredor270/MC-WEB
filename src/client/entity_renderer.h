#pragma once
#include <array>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/skin.h"
#include "client/block_draw.h"
#include "client/entity_models.h"
#include "client/gl.h"
#include "game/mob.h"
#include "game/session.h"

namespace mcw {

class PackStack;
struct Camera;
struct FogParams;

/// Qué skin lleva un jugador al dibujarlo.
struct SkinRef {
  std::string key;           // skin registrada con setSkin ("" = ninguna)
  bool defaultSlim = false;  // sin skin registrada: Alex (true) o Steve (false), la de serie del paquete de texturas
  u8 parts = kAllSkinParts;  // capas visibles (SkinPart)
};

/// Lo necesario para dibujar al jugador en tercera persona.
struct PlayerPose {
  glm::dvec3 pos{0};         // pies (ya interpolado)
  float bodyYaw = 0, headYaw = 0, pitch = 0;
  float limbSwing = 0, limbAmount = 0;
  float attack = 0;          // 0..1 golpe con el brazo derecho
  bool sneaking = false, hurt = false;
  bool sitting = false;      // montado en una vagoneta (o un cerdo): piernas hacia delante
  SkinRef skin;
  std::array<i16, 4> armor{};  // objetos de armadura puestos: 0 botas, 1 pantalones, 2 pechera, 3 casco (0 = ninguno)
};

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
  /// Vagonetas (la bandeja). Devuelve los bloques que llevan dentro (cofre, horno, dinamita) para que los dibuje quien sabe
  /// de bloques.
  std::vector<BlockDraw> drawCarts(const std::vector<Minecart>& carts, const Camera& cam, float partial, const LightFn& light,
                                   const FogParams& fog);
  /// Orbes de experiencia: un cuadrito que mira a la cámara, más grande cuanto más valen, y que parpadea de verde a amarillo.
  void drawOrbs(const std::vector<XpOrb>& orbs, const Camera& cam, float partial, const LightFn& light, const FogParams& fog);
  /// Un jugador visto desde fuera (tercera persona, o los demás en multijugador).
  void drawPlayer(const PlayerPose& p, const Camera& cam, const glm::vec3& light, const FogParams& fog);
  /// Brazo del jugador en primera persona (con la mano vacía). `swing` 0..1 = golpe.
  void drawFirstPersonArm(const Camera& cam, float swing, float bob, const glm::vec3& light, const SkinRef& skin);
  /// Personaje del jugador en la ventana del inventario o en la pantalla de skins (en píxeles de
  /// pantalla, mirando al ratón). `spin` gira el cuerpo (radianes) para verlo por todos los lados.
  void drawPlayerPreview(float cx, float feetY, float scale, float lookX, float lookY, int screenW, int screenH, const glm::vec3& light,
                         const SkinRef& skin, float spin = 0.0f, const std::array<i16, 4>& armor = {});

  /// La cara de una skin (con el sombrero encima) como icono plano, en píxeles de pantalla.
  void drawSkinFace(const SkinRef& skin, float x, float y, float size, int screenW, int screenH);

  /// Registra (o cambia) la skin `key`. La guarda en la GPU: llamar solo cuando cambia.
  void setSkin(const std::string& key, const PreparedSkin& skin);
  bool hasSkin(const std::string& key) const { return skins_.count(key) != 0; }
  void removeSkin(const std::string& key);

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
  struct SkinTex {
    Tex tex;
    bool slim = false;
  };
  const Tex& texture(const std::string& path);
  Tex upload(const Image& data) const;
  /// La skin que toca: la registrada con esa clave o, si no, Steve o Alex del paquete de texturas.
  const SkinTex& resolveSkin(const SkinRef& ref);
  /// Añade un modelo con su pose. `m` lleva de píxeles del modelo a coordenadas de cámara. `layers`:
  /// qué capas de jugador (SkinPart) se dibujan; las cajas sin capa se dibujan siempre.
  void appendModel(std::vector<Vertex>& out, const EntityModel& model, const Pose& pose, const glm::mat4& m, const Tex& tex,
                   const glm::vec3& color, bool shaded, const glm::vec4& overlay, u32 layers = ~0u) const;
  void draw(const std::vector<Vertex>& v, GLuint tex, const glm::mat4& viewProj, const FogParams* fog);
  /// Añade (por texturas) la armadura puesta sobre un jugador con esa pose.
  void appendArmor(std::unordered_map<GLuint, std::vector<Vertex>>& out, const std::array<i16, 4>& armor, const Pose& pose,
                   const glm::mat4& m, const glm::vec3& light, const glm::vec4& overlay);

  const PackStack& packs_;
  std::unordered_map<std::string, Tex> textures_;
  std::unordered_map<std::string, SkinTex> skins_;
  SkinTex defaultSkins_[2];  // Steve y Alex del paquete de texturas
  bool haveDefaultSkin_[2] = {false, false};
  GLuint program_ = 0, vao_ = 0, vbo_ = 0;
  GLint uViewProj_ = -1, uTex_ = -1, uFogColor_ = -1, uFog_ = -1;
};

/// Color de la lana de las ovejas (16 colores de tinte).
glm::vec3 fleeceColor(int color);

}  // namespace mcw
