#pragma once
#include <array>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "game/mob.h"

namespace mcw {

/// Caja de un modelo de criatura. Unidades: 1/16 de bloque ("píxeles"). Espacio del modelo: y hacia
/// arriba, la criatura mira hacia -Z y su lado derecho es +X. Las texturas siguen la disposición
/// estándar de las texturas de entidades: una caja de w×h×d en (u, v) ocupa (2d+2w)×(d+h) píxeles
/// con arriba/abajo en la primera fila y derecha/delante/izquierda/detrás en la segunda.
struct ModelBox {
  glm::vec3 min{0}, max{0};  // relativo al pivote de la pieza
  glm::vec3 size{0};         // w, h, d para las UV (sin el inflado)
  glm::vec2 uv{0};
  bool mirror = false;
  u8 layer = 0;              // 0 = siempre; si no, el bit de SkinPart que la muestra (capas del jugador)
};

/// Pieza articulada: gira alrededor de su pivote (Z, luego Y, luego X) y puede colgar de otra.
struct ModelPart {
  glm::vec3 pivot{0};
  glm::vec3 rot{0};  // ángulos de reposo (radianes)
  std::vector<ModelBox> boxes;
  int parent = -1;
};

struct EntityModel {
  std::vector<ModelPart> parts;
  float texWidth = 64, texHeight = 32;  // tamaño nominal de la textura (para las UV)
};

/// Qué pieza es cada cosa, para animar.
struct ModelRig {
  int head = -1, body = -1;
  std::array<int, 8> legs{-1, -1, -1, -1, -1, -1, -1, -1};  // en cuadrúpedos: 0/1 detrás, 2/3 delante
  int rightArm = -1, leftArm = -1, rightWing = -1, leftWing = -1;
};

struct MobModel {
  EntityModel model;
  ModelRig rig;
  std::string texture;                 // textures/entity/...
  std::string overlay;                 // capa extra (lana de la oveja, ojos de la araña)
  EntityModel overlayModel;            // mismo esqueleto con otras cajas (lana)
  bool overlayEmissive = false;        // se ve igual a oscuras (ojos)
};

const MobModel& mobModel(MobType t);
/// Modelo del jugador con la disposición de skin de 1.8 (64x64): cada extremidad con su sitio en
/// la textura y la segunda capa (sombrero, chaqueta, mangas y perneras). `slim`: brazos de 3
/// píxeles (el modelo "Alex") en vez de 4.
const MobModel& playerModel(bool slim);

/// Ángulos de las piezas en un instante (añadidos a los de reposo).
struct Pose {
  std::vector<glm::vec3> rot;
};

/// Calcula la pose: andar, cabeza, brazos del zombi, alas de la gallina, patas de la araña...
Pose poseFor(MobType t, const MobModel& m, float limbSwing, float limbAmount, float headYawRel, float pitch, float ageTicks,
             bool onGround, int eatGrassTicks);

}  // namespace mcw
