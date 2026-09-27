#pragma once
#include <algorithm>
#include <glm/glm.hpp>
#include <optional>

#include "core/types.h"
#include "game/physics.h"

namespace mcw {

/// Criaturas de 1.8 que hay por ahora.
enum class MobType : u8 { Pig, Cow, Sheep, Chicken, Zombie, Skeleton, Creeper, Spider, Count };

/// Datos fijos de cada criatura (valores de 1.8 según minecraft.wiki, dificultad normal).
struct MobInfo {
  const char* name;       // nombre para mostrar
  float width, height;    // caja de colisión (bloques)
  float eyeHeight;
  float maxHealth;        // medios corazones
  float speed;            // atributo de velocidad de movimiento
  bool hostile;
  float attackDamage;     // cuerpo a cuerpo
};
const MobInfo& mobInfo(MobType t);

/// Una criatura viva (o muriéndose: `deathTime` cuenta los ticks de la animación).
struct Mob {
  u32 id = 0;
  MobType type = MobType::Pig;
  glm::dvec3 pos{0}, prevPos{0}, motion{0};
  float yaw = 0, prevYaw = 0;          // cuerpo (radianes, como el jugador: 0 = norte, -Z)
  float headYaw = 0, prevHeadYaw = 0;  // cabeza, absoluto
  float pitch = 0, prevPitch = 0;      // cabeza (positivo = arriba)
  float health = 10;
  bool onGround = false, inWater = false, collidedH = false;
  double fallDistance = 0;
  int hurtTime = 0;       // 10 ticks de parpadeo rojo tras un golpe
  int invulnerable = 0;   // 20 ticks: los golpes de la primera mitad no cuentan
  float lastDamage = 0;
  int deathTime = 0;      // >0: muriéndose (20 ticks y desaparece)
  float limbSwing = 0, limbAmount = 0, prevLimbAmount = 0;  // animación de andar
  int age = 0;

  // IA
  float moveForward = 0;       // 0..1 este tick
  float moveSpeed = 0;         // velocidad efectiva este tick
  bool wantJump = false;
  std::optional<glm::dvec3> walkTarget;
  int wanderCooldown = 0, panicTicks = 0, stuckTicks = 0, attackCooldown = 0, lookTicks = 0;
  bool chasing = false;
  glm::dvec3 lastProgressPos{0};

  // Propias de algunas criaturas
  int fuse = 0, prevFuse = 0;  // creeper: 0..30 ticks hasta explotar
  int fireTicks = 0;           // ardiendo (zombis y esqueletos al sol)
  u8 woolColor = 0;            // oveja
  bool sheared = false;
  int eatGrassTicks = 0;       // oveja comiendo hierba (40 ticks)
  int eggTimer = 0;            // gallina: ticks hasta poner un huevo

  const MobInfo& info() const { return mobInfo(type); }
  bool dying() const { return deathTime > 0; }
  AABB box() const { return AABB::centered(pos, info().width, info().height); }
  glm::dvec3 eyePos() const { return pos + glm::dvec3(0, info().eyeHeight, 0); }
};

/// Flecha disparada (por esqueletos).
struct Arrow {
  glm::dvec3 pos{0}, prevPos{0}, motion{0};
  float yaw = 0, pitch = 0;
  bool inGround = false;
  int life = 0;       // ticks desde que se clavó
  float damage = 2.0f;
  bool pickup = true;  // se puede recoger al clavarse
  int shake = 0;       // vibración al clavarse
};

/// Oscuridad del cielo según la hora (0 de día .. 11 a medianoche), como el "skylight subtracted" del juego.
int skyDarkness(double worldTime);
/// Luz para la aparición de monstruos: max(luz de bloque, luz de cielo - oscuridad).
inline int effectiveLight(int sky, int block, int darkness) { return std::max(block, sky - darkness); }

}  // namespace mcw
