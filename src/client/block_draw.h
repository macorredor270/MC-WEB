#pragma once
// Un bloque que se dibuja dentro de otra cosa (el cofre, el horno o la dinamita que lleva una vagoneta).
#include <glm/glm.hpp>

#include "game/item_stack.h"

namespace mcw {

struct BlockDraw {
  ItemStack block;       // el bloque
  glm::mat4 m{1.0f};     // del espacio del bloque (cubo de lado 1 centrado) a coordenadas relativas a la cámara
  glm::vec3 light{1.0f};
  float flash = 0.0f;    // 0..1: destello blanco (la dinamita a punto de explotar)
};

}  // namespace mcw
