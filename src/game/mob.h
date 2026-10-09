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

/// Animales que se pueden criar (cerdo, vaca, oveja y gallina).
inline bool isBreedable(MobType t) { return t == MobType::Pig || t == MobType::Cow || t == MobType::Sheep || t == MobType::Chicken; }
/// Lo que hay que darles para que entren en modo amor (1.8: zanahoria, trigo y semillas de trigo).
int breedingItem(MobType t);

/// Una cría tarda 20 minutos en crecer; tras criar, los padres esperan 5 minutos.
constexpr int kBabyTicks = 24000, kBreedCooldown = 6000, kLoveTicks = 600;

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
  bool persistent = false;     // no desaparece sola aunque esté lejos (PersistenceRequired)
  bool noAI = false;           // sin IA (etiqueta NoAI de 1.8): se queda quieta, solo le afectan la gravedad y los golpes
  glm::dvec3 lastProgressPos{0};

  // Propias de algunas criaturas
  int fuse = 0, prevFuse = 0;  // creeper: 0..30 ticks hasta explotar
  int fireTicks = 0;           // ardiendo (zombis y esqueletos al sol)
  u8 woolColor = 0;            // oveja
  bool sheared = false;
  int eatGrassTicks = 0;       // oveja comiendo hierba (40 ticks)
  int eggTimer = 0;            // gallina: ticks hasta poner un huevo
  // Cría de animales (la edad de 1.8: negativa = cría; positiva = espera para volver a criar)
  int growth = 0;              // <0: cría, ticks que le faltan para crecer
  int inLove = 0;              // >0: en modo amor; busca a otro igual que también lo esté
  int mateTicks = 0;           // ticks seguidos junto a su pareja (con 60 tienen la cría)
  bool lovedByPlayer = false;  // la ha alimentado el jugador (para el logro de criar vacas)

  const MobInfo& info() const { return mobInfo(type); }
  bool dying() const { return deathTime > 0; }
  bool baby() const { return growth < 0; }
  /// Las crías miden la mitad.
  float scale() const { return baby() ? 0.5f : 1.0f; }
  AABB box() const { return AABB::centered(pos, info().width * scale(), info().height * scale()); }
  glm::dvec3 eyePos() const { return pos + glm::dvec3(0, info().eyeHeight * scale(), 0); }
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
  bool fromPlayer = false;  // disparada por el jugador con su arco (da a criaturas; a él solo tras 5 ticks)
  int flight = 0;           // ticks en el aire
  bool crit = false;        // a plena potencia: hace algo más de daño
  int punch = 0;            // nivel de Retroceso
  bool flame = false;       // Flama: prende a quien alcanza
};

/// Oscuridad del cielo según la hora (0 de día .. 11 a medianoche), como el "skylight subtracted" del juego.
int skyDarkness(double worldTime);
/// Luz para la aparición de monstruos: max(luz de bloque, luz de cielo - oscuridad).
inline int effectiveLight(int sky, int block, int darkness) { return std::max(block, sky - darkness); }

}  // namespace mcw
