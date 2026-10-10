#pragma once
// Vagonetas: su estado y su física sobre los raíles (sin saber de la partida: solo el mundo).
#include <array>
#include <glm/glm.hpp>

#include "core/types.h"
#include "game/item_stack.h"
#include "game/physics.h"

namespace mcw {

class World;

enum class CartType : u8 { Normal, Chest, Furnace, Tnt };

struct Minecart {
  static constexpr double kWidth = 0.98, kHeight = 0.7;
  u32 id = 0;  // para el multijugador y el guardado
  CartType type = CartType::Normal;
  glm::dvec3 pos{0}, prevPos{0}, motion{0};  // pos = centro de la base
  float yaw = 0, prevYaw = 0;                // hacia donde va (0 = norte, como el jugador); solo importa módulo 180º
  float pitch = 0, prevPitch = 0;            // inclinación al subir o bajar una cuesta
  bool onGround = false;
  float damage = 0;       // golpes acumulados (se rompe al pasar de 40)
  int hurtTime = 0, shakeDir = 1;
  int rider = 0;          // 0 nadie; 1 el jugador local; si no, el id de entidad de un invitado
  int fuel = 0;           // con horno: ticks de combustible que le quedan
  glm::dvec2 push{0};     // con horno: hacia dónde empuja
  int fuse = -1;          // con TNT: ticks hasta explotar (-1 = apagada)
  double fallDistance = 0;
  bool dead = false;      // marcada para quitar

  AABB box() const { return AABB::centered(pos, kWidth, kHeight); }
  /// Lo más que avanza por tick sobre un raíl (con horno va más despacio).
  double maxSpeed() const { return type == CartType::Furnace ? 0.2 : 0.4; }
};

/// Objeto de la vagoneta de cada tipo (minecart, chest_minecart, furnace_minecart, tnt_minecart) y al revés (-1 si no lo es).
int cartItemId(CartType t);
int cartTypeFromItem(int itemId);

/// Qué hay que saber del que va montado para mover la vagoneta.
struct CartDriver {
  bool hasRider = false;
  float forward = 0;  // lo que empuja hacia delante (> 0 la empuja si va despacio)
  float yaw = 0;      // hacia dónde mira el jinete
};

struct CartStep {
  bool onRail = false;
  glm::ivec3 rail{0};
  int railId = 0;
  bool activatorPowered = false;  // está sobre un raíl activador con potencia
  bool collided = false;          // ha chocado de lado contra un bloque
  double impactSq = 0;            // si ha chocado: la velocidad (al cuadrado) con la que iba antes de parar
  double landed = 0;              // si ha tocado el suelo este tick: lo que ha caído
};

/// Un tick de física: gravedad, seguir el raíl (cuestas, curvas, propulsores y frenos) o rodar suelta.
CartStep stepCart(Minecart& c, const World& world, const CartDriver& driver);

/// Inclinación (radianes, positiva = morro arriba) de una vagoneta en `pos` que mira hacia `yaw`: la de la cuesta en la
/// que está, o 0.
float cartPitchAt(const World& world, const glm::dvec3& pos, float yaw);

}  // namespace mcw
