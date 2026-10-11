#pragma once
// Adornos que son entidades: marcos para objetos, cuadros y soportes para armadura. Su estado y su geometría (sin saber de la
// partida: solo el mundo). Comportamiento observable de 1.8; implementación propia.
#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "game/item_stack.h"
#include "game/physics.h"

namespace mcw {

class World;

enum class DecorKind : u8 { ItemFrame, Painting, ArmorStand };

/// Un lienzo de cuadro: tamaño en bloques y de dónde se toma su imagen en la textura de cuadros (256x256, en píxeles).
struct PaintingMotive {
  const char* name;  // el nombre de 1.8 que se guarda en el mundo ("Kebab", "Pigscene"...)
  u8 w, h;           // bloques
  u16 u, v;          // esquina de arriba a la izquierda en la textura
};
const std::vector<PaintingMotive>& paintingMotives();
int paintingMotiveByName(std::string_view name);  // -1 si no existe

/// Direcciones de los adornos colgados, como en 1.8: hacia donde mira la cara visible (0 sur +Z, 1 oeste -X, 2 norte -Z, 3 este +X).
glm::ivec3 decorNormal(int dir);
/// Del lado de un bloque (cara 2 = norte ... 5 = este, como el rayo) a la dirección de 1.8; -1 si es arriba o abajo.
int decorDirFromFace(int face);

/// Ranuras del soporte: 0 mano, 1 botas, 2 pantalones, 3 peto, 4 casco.
constexpr int kStandSlots = 5;

struct Decor {
  u32 id = 0;  // para el multijugador y el guardado
  DecorKind kind = DecorKind::ItemFrame;
  // Marco y cuadro
  glm::ivec3 cell{0};  // el bloque de aire donde está pegado (delante de la pared); en un cuadro grande, la casilla de abajo a la derecha
  u8 dir = 0;          // ver `decorNormal`
  ItemStack item;      // marco: lo que muestra
  u8 rotation = 0;     // marco: 0..7 (pasos de 45º)
  u8 motive = 0;       // cuadro: índice en `paintingMotives()`
  // Soporte
  glm::dvec3 pos{0};   // soporte: los pies
  float yaw = 0;       // soporte: hacia dónde mira (radianes, 0 = norte)
  std::array<ItemStack, kStandSlots> equip;
  bool arms = false, small = false, noBase = false;
  double fallSpeed = 0;
  int hurtTime = 0;    // golpe reciente: el siguiente lo rompe (soporte) o hace que se bambolee
  bool dead = false;

  /// Centro del marco o del cuadro (en el aire pegado a la pared).
  glm::dvec3 center() const;
  AABB box() const;
};

/// ¿Cabe un adorno colgado ahí? Pared maciza detrás de todas las casillas del lienzo y sin bloques con colisión dentro.
/// `w`, `h` en bloques. Los demás adornos los comprueba quien llama.
bool hangingFits(const World& world, const glm::ivec3& cell, int dir, int w, int h);
/// Casillas del lienzo (de abajo a la derecha hacia la izquierda y arriba), para marco (1x1) o cuadro.
std::vector<glm::ivec3> hangingCells(const glm::ivec3& cell, int dir, int w, int h);
/// El mayor lienzo que cabe ahí (azar entre los de mayor área). nullopt si no cabe ninguno (ni el pequeño).
std::optional<int> pickPainting(const World& world, const glm::ivec3& cell, int dir, const std::vector<const Decor*>& others, u32 randomBits);

/// ¿Choca la caja con algún bloque con colisión? (con un pelo de margen)
bool boxHitsBlocks(const World& world, const AABB& box);

/// Un adorno cuya caja corta con otra (para no solapar marcos y cuadros).
bool decorOverlaps(const Decor& a, const Decor& b);

/// ¿Sigue habiendo pared detrás del marco o el cuadro?
bool hangingSupported(const World& world, const Decor& d);

/// Ranura del soporte que le corresponde a un objeto de armadura o un casco (-1 si no se puede poner).
int standSlotFor(const ItemStack& s);
/// Ranura de una altura del impacto sobre los pies del soporte (para sacar lo que lleva con la mano vacía).
int standSlotAtHeight(double y, bool small);

}  // namespace mcw
