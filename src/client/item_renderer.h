#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <vector>

#include "client/gl.h"
#include "game/item_stack.h"
#include "game/physics.h"

namespace mcw {

class BlockModels;
class ItemModels;
struct Camera;
struct ItemEntity;

/// Dibuja ítems (iconos de interfaz, objetos en el suelo, el de la mano), las grietas al romper
/// y el contorno del bloque apuntado. Comparte el texture array de bloques/ítems con el terreno.
class ItemRenderer {
 public:
  ItemRenderer(const BlockModels& blocks, const ItemModels& items) : blocks_(blocks), items_(items) {}
  ~ItemRenderer();
  void initGL(GLuint textureArray, u16 firstDestroyLayer);

  // --- Interfaz (coordenadas en píxeles de GUI) ---
  void queueIcon(const ItemStack& s, float x, float y);
  /// Dibuja los iconos pendientes encima de lo que haya (limpia el depth buffer).
  void flushIcons(int screenW, int screenH, int guiScale);

  // --- Mundo ---
  using LightFn = std::function<glm::vec3(const glm::dvec3&)>;
  void drawWorldItems(const std::vector<ItemEntity>& items, const Camera& cam, float partial, double timeTicks, const LightFn& light);
  void drawBreaking(BlockState s, const glm::ivec3& pos, float progress, const Camera& cam);
  void drawSelection(const std::vector<AABB>& boxes, const glm::ivec3& pos, const Camera& cam);
  /// Objeto en la mano. `swing` 0..1 = animación de golpear/usar. `bowTicks` > 0: el arco se está tensando
  /// (ticks, con decimales de la interpolación) y se dibuja levantado y con la cuerda hacia atrás.
  void drawHeld(const ItemStack& s, const Camera& cam, float swing, float bob, const glm::vec3& light, float bowTicks = 0.0f);

 private:
  struct Vertex {
    float x, y, z, u, v, layer;
    u8 r, g, b, a;
  };
  /// Añade la geometría de un ítem transformada por `m` (espacio del modelo: bloque 0..1 centrado).
  void appendItem(std::vector<Vertex>& out, const ItemStack& s, const glm::mat4& m, const glm::vec3& light, bool thickSprite,
                  int variant = 0);
  void draw(const std::vector<Vertex>& v, const glm::mat4& mvp, bool textured, float alphaCutoff, GLenum mode = GL_TRIANGLES);

  const BlockModels& blocks_;
  const ItemModels& items_;
  GLuint program_ = 0, vao_ = 0, vbo_ = 0, texArray_ = 0;
  GLint uMVP_ = -1, uTex_ = -1, uAlphaCutoff_ = -1, uTextured_ = -1;
  u16 destroyLayer_ = 0;
  struct QueuedIcon { ItemStack s; float x, y; };
  std::vector<QueuedIcon> queue_;
};

}  // namespace mcw
