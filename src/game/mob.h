#pragma once
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

#include "core/types.h"
#include "game/effects.h"
#include "game/physics.h"
#include "game/trading.h"

namespace mcw {

/// Criaturas de 1.8 que hay por ahora.
enum class MobType : u8 {
  Pig, Cow, Sheep, Chicken, Zombie, Skeleton, Creeper, Spider,
  PigZombie, Ghast, Blaze, MagmaCube, Slime, Enderman, Silverfish, CaveSpider, WitherSkeleton, EnderDragon,
  Squid, Bat, Rabbit, Mooshroom, Wolf, Ocelot, Horse, Villager, IronGolem, SnowGolem, Witch, Endermite, Wither, Guardian,
  Count
};

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

/// Animales que se pueden criar.
inline bool isBreedable(MobType t) {
  return t == MobType::Pig || t == MobType::Cow || t == MobType::Sheep || t == MobType::Chicken || t == MobType::Mooshroom ||
         t == MobType::Wolf || t == MobType::Ocelot || t == MobType::Rabbit || t == MobType::Horse;
}
/// Vacas y champiñacas se comportan igual (y se cruzan entre sí).
inline bool isCowLike(MobType t) { return t == MobType::Cow || t == MobType::Mooshroom; }
/// Mascotas que se pueden domar.
inline bool isTameable(MobType t) { return t == MobType::Wolf || t == MobType::Ocelot || t == MobType::Horse; }
/// Criaturas que vuelan o nadan sin gravedad normal.
inline bool isFlyer(MobType t) { return t == MobType::Ghast || t == MobType::Bat; }
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
  bool saddled = false;        // cerdo con silla (se puede montar)
  // Criaturas del Nether y del End
  u8 size = 1;                 // slime y cubo de magma: 1, 2 o 4 (mide size x 0,51)
  int anger = 0;               // cerdo zombi y enderman: ticks de enfado
  int carried = 0;             // enderman: estado del bloque que lleva (0 = nada)
  int teleportCd = 0;
  int shootTicks = 0;          // ghast: carga del disparo (visible en la cara); blaze: ráfaga
  int shots = 0;               // blaze: bolas que quedan de la ráfaga
  int jumpDelay = 0;           // slime y cubo de magma
  int phase = 0, phaseTicks = 0;  // dragón: 0 vuela en círculos, 1 embiste al jugador
  // Criaturas de más (lobo, ocelote, conejo, caballo, aldeano, golems, murciélago, calamar...)
  u8 variant = 0;              // conejo: pelaje; ocelote: 0 salvaje, 1 negro, 2 rojo, 3 siamés; aldeano: oficio; caballo: tipo (0 caballo, 1 burro, 2 mula)
  u32 coat = 0;                // caballo: color (bits 0-7) y manchas (bits 8-15)
  bool tamed = false, sitting = false, angry = false;  // mascotas
  u8 collar = 14;              // lobo: color del collar (14 = rojo)
  u32 ownerUid = 0;            // quién la ha domado (0 = nadie; ver `Player::uid`)
  bool hanging = false;        // murciélago colgado del techo
  bool playerMade = false;     // golem de hierro hecho por el jugador
  // Aldeano: su oficio (0xFF = aún sin repartir), cuántos niveles de ofertas tiene y las ofertas
  u8 career = 0xFF, careerTier = 0;
  std::vector<TradeOffer> offers;
  int tradeReset = 0;        // ticks hasta que se renueven las ofertas tras un trato (0 = nada pendiente)
  bool tradeUnlock = false;  // al renovarlas, abre el siguiente nivel
  int tradeKeep = 0;         // de un invitado: ticks que le quedan a la ocupación si no se renueva (el servidor la renueva mientras la ventana está abierta)
  u32 tradingWith = 0;       // uid de quien está comerciando con él (0 = nadie): se queda quieto
  bool pumpkin = true;         // golem de nieve con calabaza
  // Wither: ticks que le quedan de carga al nacer (invulnerable y quieto), cuenta atrás de cada cabeza y bloques que rompe al ser herido
  int invul = 0;
  std::array<int, 3> headCd{0, 0, 0};
  int blockBreak = 0;
  int target = 0;              // id de la criatura a la que ataca (0 = ninguna)
  // Guardián: a quién apunta con el rayo (uid del jugador; 0 = a nadie), ticks de carga, dónde da el rayo, púas fuera y si es el anciano
  u32 beamTarget = 0;
  int beamTicks = 0;
  glm::dvec3 beamAt{0};
  bool spikes = false, elder = false;
  float tentacle = 0, prevTentacle = 0;  // calamar: giro de los tentáculos
  float swimAngle = 0;         // calamar: hacia dónde nada (radianes)
  float swimSpeed = 0;
  int tradeSeed = 0;           // aldeano: semilla de sus ofertas
  Effects effects;             // efectos de poción
  float effectSpeedFactor() const {
    float f = 1.0f;
    if (const int a = effects.amp(fx::Speed); a >= 0) f *= 1.0f + 0.2f * static_cast<float>(a + 1);
    if (const int a = effects.amp(fx::Slowness); a >= 0) f *= std::max(0.0f, 1.0f - 0.15f * static_cast<float>(a + 1));
    return f;
  }
  bool isSlimeLike() const { return type == MobType::Slime || type == MobType::MagmaCube; }

  const MobInfo& info() const { return mobInfo(type); }
  bool dying() const { return deathTime > 0; }
  bool baby() const { return growth < 0; }
  /// Las crías miden la mitad.
  float scale() const { return isSlimeLike() ? static_cast<float>(size) : (baby() ? 0.5f : (elder ? 2.35f : 1.0f)); }
  AABB box() const { return AABB::centered(pos, info().width * scale(), info().height * scale()); }
  glm::dvec3 eyePos() const { return pos + glm::dvec3(0, info().eyeHeight * scale(), 0); }
};

/// Id de entidad de 1.8 de una criatura (el de los paquetes, los huevos de criatura y los archivos guardados).
int mobEntityId(MobType t);
/// La criatura de un id de entidad de 1.8 (y si es una variante, `wither` / tamaño). Nada si aún no existe.
std::optional<MobType> mobFromEntityId(int id);

/// Bola de fuego: la grande de un ghast (explota) o la pequeña de un blaze (daño y fuego).
struct Fireball {
  u32 id = 0;  // para el multijugador
  glm::dvec3 pos{0}, prevPos{0}, motion{0}, accel{0};
  bool large = true;
  bool remote = false;      // llega del servidor: solo se dibuja
  u8 skull = 0;             // 0 bola de fuego; 1 calavera del Wither; 2 calavera azul (la peligrosa)
  u32 owner = 0;            // criatura que la lanzó (0 = ninguna o el jugador)
  bool fromPlayer = false;  // desviada con un golpe: si mata a un ghast, "Devolución al remitente"
  int life = 0;
};

/// Lo que se lanza con el clic derecho: bola de nieve, huevo, perla de ender, frasco de experiencia y ojo de ender.
struct Thrown {
  enum Kind : u8 { Snowball, Egg, Pearl, XpBottle, Eye, Potion } kind = Snowball;
  int data = 0;  // poción: su daño (qué poción es)
  u32 id = 0;  // para el multijugador
  glm::dvec3 pos{0}, prevPos{0}, motion{0};
  int life = 0;
  glm::dvec2 target{0};  // ojo de ender: hacia dónde va (x, z)
  double traveled = 0;
  bool byLocal = true;   // lo lanzó el jugador de esta partida
  u32 owner = 0;         // criatura que lo lanzó (gólem de nieve, bruja); 0 = el jugador
  u32 thrower = 0;       // el jugador que lo lanzó (uid): la perla lo teletransporta a él
  bool netDriven = false;  // cliente de red: lo mueve el servidor, aquí solo se dibuja
};

/// Flecha disparada (por esqueletos).
struct Arrow {
  u32 id = 0;  // para el multijugador
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
