#pragma once
#include <array>
#include <glm/glm.hpp>
#include <vector>

#include "game/inventory.h"
#include "game/physics.h"

namespace mcw {

class World;

enum class GameMode { Survival, Creative };

struct MoveInput {
  float forward = 0, strafe = 0;  // -1..1
  bool jump = false, sneak = false, sprint = false;
  bool autoJump = false;  // saltar solo al chocar con un escalón de un bloque (como en la edición de bolsillo)
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
  int difficulty = 2;  // 0 pacífico, 1 fácil, 2 normal, 3 difícil (hambre y regeneración)

  // Experiencia (como en 1.8): nivel, fracción de la barra hasta el siguiente y puntos totales (la "puntuación")
  int xpLevel = 0;
  float xpProgress = 0.0f;
  int xpTotal = 0;
  int xpSeed = 0;  // semilla de la mesa de encantamientos (cambia al encantar)

  PlayerInventory inventory;
  std::array<ItemStack, 27> enderItems{};  // cofre de ender
  ItemStack cursor;  // lo que se lleva con el ratón en las pantallas de inventario
  /// Lo fabricado y lo sacado del horno desde el último tick (para logros y estadísticas).
  std::vector<ItemStack> crafted, smelted;

  AABB box() const { return AABB::centered(pos, kWidth, kHeight); }
  glm::dvec3 eyePos() const { return pos + glm::dvec3(0, kEyeHeight - (sneaking && !flying ? kSneakEyeOffset : 0.0), 0); }
  glm::dvec3 lookDir() const;
  bool creative() const { return mode == GameMode::Creative; }

  /// Un tick de movimiento. `jumpPressed` es el flanco de pulsar saltar (doble toque = volar en creativo).
  void tickMovement(const World& world, const MoveInput& in, bool jumpPressed);
  /// Un tick de vida: hambre, regeneración, inanición, ahogo.
  void tickStatus(const World& world);

  /// Aplica daño (en medios corazones). Devuelve true si se ha aplicado. Con `armored` (golpes, flechas,
  /// explosiones) la armadura puesta lo reduce y se desgasta; caídas, ahogo, hambre y vacío la ignoran.
  bool damage(float amount, bool armored = false);
  void addExhaustion(float e) { exhaustion = std::min(40.0f, exhaustion + e); }
  /// Puntos que hacen falta para pasar del nivel actual al siguiente (2n+7, 5n-38, 9n-158).
  int xpBarCap() const { return xpCapForLevel(xpLevel); }
  static int xpCapForLevel(int level) { return level >= 30 ? 112 + (level - 30) * 9 : (level >= 15 ? 37 + (level - 15) * 5 : 7 + level * 2); }
  /// Suma puntos de experiencia (sube de nivel si llega a la raya). Devuelve cuántos niveles ha subido.
  int addXp(int amount);
  /// Suma (o quita, con negativo) niveles enteros. Los niveles no bajan de 0.
  void addXpLevels(int levels);
  /// Encantar con la mesa: gasta `cost` niveles y cambia la semilla de las ofertas (como en 1.8).
  void onEnchant(int cost) {
    addXpLevels(-cost);
    xpSeed = static_cast<int>(static_cast<unsigned>(xpSeed) * 1664525u + 1013904223u);
  }
  int enchanted = 0;  // veces que ha encantado (estadística)
  /// Puntos que suelta al morir: 7 por nivel, hasta 100.
  int xpDroppedOnDeath() const { return std::min(xpLevel * 7, 100); }
  void resetXp() { xpLevel = xpTotal = 0; xpProgress = 0.0f; }
  void eat(int foodPoints, float saturationModifier);
  void respawn(const glm::dvec3& at);

 private:
  void travel(const World& world, float strafe, float forward, bool jump);
  void moveWithCollisions(const World& world, glm::dvec3 motionThisTick);
  /// ¿Hay delante (según la dirección de movimiento) un escalón de un bloque que se puede subir saltando?
  bool stepAhead(const World& world, float forward, float strafe) const;
  int jumpTicks_ = 0;  // ventana para el doble toque
  int flyToggleCooldown_ = 0;
  int peacefulTimer_ = 0;
};

/// Deslizamiento del bloque bajo los pies (hielo 0.98, slime 0.8, el resto 0.6).
float slipperiness(int blockId);

}  // namespace mcw
