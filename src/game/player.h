#pragma once
#include <array>
#include <glm/glm.hpp>

#include "game/inventory.h"
#include "game/physics.h"

namespace mcw {

class World;

enum class GameMode { Survival, Creative };

struct MoveInput {
  float forward = 0, strafe = 0;  // -1..1
  bool jump = false, sneak = false, sprint = false;
};

/// Jugador: física, vida, hambre e inventario. Un tick = 1/20 s, como en el juego.
class Player {
 public:
  static constexpr double kWidth = 0.6, kHeight = 1.8, kEyeHeight = 1.62, kSneakEyeOffset = 0.08;
  static constexpr float kMaxHealth = 20.0f;

  glm::dvec3 pos{0, 80, 0};      // pies
  glm::dvec3 prevPos{0, 80, 0};  // para interpolar el render entre ticks
  glm::dvec3 motion{0};
  float yaw = 0, pitch = 0;      // radianes; yaw 0 = norte (-Z)

  bool onGround = false, flying = false, sprinting = false, sneaking = false;
  bool inWater = false, headInWater = false, collidedHorizontally = false;
  double fallDistance = 0;

  GameMode mode = GameMode::Survival;
  float health = kMaxHealth;
  int food = 20;
  float saturation = 5.0f, exhaustion = 0.0f;
  int foodTimer = 0, hurtTime = 0, air = 300;
  bool dead = false;
  float lastDamage = 0;

  PlayerInventory inventory;
  ItemStack cursor;  // lo que se lleva con el ratón en las pantallas de inventario

  AABB box() const { return AABB::centered(pos, kWidth, kHeight); }
  glm::dvec3 eyePos() const { return pos + glm::dvec3(0, kEyeHeight - (sneaking && !flying ? kSneakEyeOffset : 0.0), 0); }
  glm::dvec3 lookDir() const;
  bool creative() const { return mode == GameMode::Creative; }

  /// Un tick de movimiento. `jumpPressed` es el flanco de pulsar saltar (doble toque = volar en creativo).
  void tickMovement(const World& world, const MoveInput& in, bool jumpPressed);
  /// Un tick de vida: hambre, regeneración, inanición, ahogo.
  void tickStatus(const World& world);

  /// Aplica daño (en medios corazones). Devuelve true si se ha aplicado.
  bool damage(float amount);
  void addExhaustion(float e) { exhaustion = std::min(40.0f, exhaustion + e); }
  void eat(int foodPoints, float saturationModifier);
  void respawn(const glm::dvec3& at);

 private:
  void travel(const World& world, float strafe, float forward, bool jump);
  void moveWithCollisions(const World& world, glm::dvec3 motionThisTick);
  int jumpTicks_ = 0;  // ventana para el doble toque
  int flyToggleCooldown_ = 0;
};

/// Deslizamiento del bloque bajo los pies (hielo 0.98, slime 0.8, el resto 0.6).
float slipperiness(int blockId);

}  // namespace mcw
