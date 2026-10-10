// Criaturas: aparición, IA, físicas, combate, flechas y explosiones. Las reglas y los números son
// los de 1.8 (minecraft.wiki), en dificultad normal; la implementación es propia.
#include "game/enchant_effects.h"
#include "game/enchantments.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <tuple>

#include "data/biomes.h"
#include "data/items.h"
#include "game/mob.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kMaxPassive = 60, kMaxHostile = 24;

const MobInfo kMobs[] = {
    //  nombre      ancho alto  ojos  vida velocidad hostil daño
    {"Cerdo", 0.9f, 0.9f, 0.765f, 10, 0.25f, false, 0},
    {"Vaca", 0.9f, 1.3f, 1.105f, 10, 0.20f, false, 0},
    {"Oveja", 0.9f, 1.3f, 1.235f, 8, 0.23f, false, 0},
    {"Gallina", 0.4f, 0.7f, 0.644f, 4, 0.25f, false, 0},
    {"Zombi", 0.6f, 1.95f, 1.74f, 20, 0.23f, true, 3},
    {"Esqueleto", 0.6f, 1.95f, 1.74f, 20, 0.25f, true, 0},
    {"Creeper", 0.6f, 1.7f, 1.445f, 20, 0.25f, true, 0},
    {"Araña", 1.4f, 0.9f, 0.65f, 16, 0.30f, true, 2},
};

float wrapAngle(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}

float approachAngle(float from, float to, float maxStep) {
  return from + std::clamp(wrapAngle(to - from), -maxStep, maxStep);
}

/// Yaw que mira de `from` a `to` (forward = (-sin yaw, -cos yaw)).
float yawTowards(const glm::dvec3& from, const glm::dvec3& to) {
  return std::atan2(static_cast<float>(-(to.x - from.x)), static_cast<float>(-(to.z - from.z)));
}

glm::dvec3 forwardOf(float yaw) { return {-std::sin(yaw), 0.0, -std::cos(yaw)}; }

double gaussian(Random& r) {
  // Suma de uniformes: basta para la dispersión de las flechas
  double s = 0;
  for (int i = 0; i < 6; i++) s += r.next();
  return (s - 3.0) / std::sqrt(0.5);
}

/// Rayo contra una caja (método de las losas). Distancia de entrada o nada.
std::optional<double> rayBox(const glm::dvec3& o, const glm::dvec3& d, const AABB& b) {
  double t0 = 0, t1 = 1e30;
  for (int i = 0; i < 3; i++) {
    if (std::abs(d[i]) < 1e-12) {
      if (o[i] < b.min[i] || o[i] > b.max[i]) return std::nullopt;
      continue;
    }
    double a = (b.min[i] - o[i]) / d[i], c = (b.max[i] - o[i]) / d[i];
    if (a > c) std::swap(a, c);
    t0 = std::max(t0, a);
    t1 = std::min(t1, c);
    if (t0 > t1) return std::nullopt;
  }
  return t0;
}

bool isSword(int id) {
  return id == ItemId::wooden_sword || id == ItemId::stone_sword || id == ItemId::iron_sword || id == ItemId::golden_sword ||
         id == ItemId::diamond_sword;
}

bool animalBiome(int biome) {
  // Donde aparecen cerdos, vacas, ovejas y gallinas al generar el mundo
  return biome == Biome::plains || biome == Biome::forest || biome == Biome::birch_forest || biome == Biome::taiga ||
         biome == Biome::extreme_hills || biome == Biome::savanna || biome == Biome::swamp;
}

/// Suelo transitable: sólido debajo y dos bloques libres (sin fluidos) para la criatura.
bool freeForMob(const World& w, int x, int y, int z) {
  const int id = stateId(w.block(x, y, z));
  if (isFluid(id)) return false;
  return id == B::air || blockCollision(w, x, y, z).empty();
}

}  // namespace

const MobInfo& mobInfo(MobType t) { return kMobs[std::min(static_cast<int>(t), static_cast<int>(MobType::Count) - 1)]; }

int breedingItem(MobType t) {
  switch (t) {
    case MobType::Pig: return ItemId::carrot;
    case MobType::Cow: case MobType::Sheep: return ItemId::wheat;
    case MobType::Chicken: return ItemId::wheat_seeds;
    default: return -1;
  }
}

int skyDarkness(double worldTime) {
  // Ángulo del sol (con el suavizado del juego) -> cuánto se oscurece la luz del cielo (0..11)
  double f = std::fmod(worldTime, 24000.0) / 24000.0 - 0.25;
  if (f < 0) f += 1;
  if (f > 1) f -= 1;
  const double f2 = f;
  f = 1.0 - (std::cos(f * std::numbers::pi) + 1.0) / 2.0;
  f = f2 + (f - f2) / 3.0;
  double b = 1.0 - (std::cos(f * 2.0 * std::numbers::pi) * 2.0 + 0.5);
  b = std::clamp(b, 0.0, 1.0);
  return static_cast<int>(b * 11.0);
}

// --- Aparición ------------------------------------------------------------------------------------

Mob* GameSession::spawnMob(MobType type, const glm::dvec3& pos) {
  Mob m;
  m.id = nextMobId_++;
  m.type = type;
  m.pos = m.prevPos = m.lastProgressPos = pos;
  m.health = m.info().maxHealth;
  m.yaw = m.prevYaw = m.headYaw = m.prevHeadYaw = rng_.nextFloat() * 2 * kPi - kPi;
  m.age = 0;
  if (type == MobType::Sheep) {
    // Colores de la lana al aparecer (1.8): casi todas blancas
    const float r = rng_.nextFloat() * 100.0f;
    m.woolColor = r < 5 ? 15 : r < 10 ? 7 : r < 15 ? 8 : r < 18 ? 12 : (rng_.nextInt(500) == 0 ? 6 : 0);
  }
  if (type == MobType::Chicken) m.eggTimer = 6000 + rng_.nextInt(6000);
  mobs_.push_back(m);
  return &mobs_.back();
}

void GameSession::populateChunk(int cx, int cz) {
  World& w = access_.world();
  const Chunk* c = w.chunk(cx, cz);
  if (!c) return;
  if (!rules_.mobSpawning) return;
  Random r(cellSeed(seed_, cx, cz, 0x5EED));
  Random babies(cellSeed(seed_, cx, cz, 0xBABE));
  if (r.nextFloat() >= 0.1f) return;
  if (!animalBiome(c->biome(8, 8))) return;
  const int passive = static_cast<int>(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return !m.info().hostile; }));
  if (passive >= kMaxPassive) return;
  const int roll = r.nextInt(40);
  const MobType type = roll < 12 ? MobType::Sheep : roll < 22 ? MobType::Pig : roll < 32 ? MobType::Chicken : MobType::Cow;
  const int n = 1 + r.nextInt(4);
  const int topY = (c->topSection() + 1) * 16 - 1;
  for (int i = 0; i < n; i++) {
    const int lx = r.nextInt(16), lz = r.nextInt(16);
    // Bajar desde arriba saltando aire, plantas y hojas hasta el suelo: tiene que ser hierba
    int y = topY;
    for (; y > 1; y--) {
      const int id = stateId(c->block(lx, y, lz));
      if (id == B::air || id == B::tallgrass || id == B::yellow_flower || id == B::red_flower || id == B::double_plant ||
          id == B::leaves || id == B::leaves2 || id == B::snow_layer)
        continue;
      break;
    }
    if (stateId(c->block(lx, y, lz)) != B::grass) continue;
    const int wx = cx * 16 + lx, wz = cz * 16 + lz;
    if (!freeForMob(w, wx, y + 1, wz) || !freeForMob(w, wx, y + 2, wz)) continue;
    if (Mob* m = spawnMob(type, {wx + 0.5, y + 1.0, wz + 0.5}); m && babies.nextInt(20) == 0) m->growth = -kBabyTicks;  // 5 % de crías
  }
}

void GameSession::spawnHostiles() {
  if (!rules_.mobSpawning || rules_.difficulty == 0) return;
  if (++hostileSpawnTimer_ < 10) return;
  hostileSpawnTimer_ = 0;
  int hostiles = static_cast<int>(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.info().hostile; }));
  if (hostiles >= kMaxHostile) return;
  World& w = access_.world();
  const int darkness = skyDarkness(worldTime_);
  auto validSpot = [&](int x, int y, int z) {
    if (y < 1 || y >= kChunkHeight - 2) return false;
    if (!blockInfo(stateId(w.block(x, y - 1, z))).opaqueCube) return false;
    if (!freeForMob(w, x, y, z) || !freeForMob(w, x, y + 1, z)) return false;
    // Como en 1.8: cuanta más luz de cielo, menos probable; y la luz tiene que ser <= 7 (al azar)
    if (w.skyLight(x, y, z) > rng_.nextInt(32)) return false;
    return effectiveLight(w.skyLight(x, y, z), w.blockLight(x, y, z), darkness) <= rng_.nextInt(8);
  };
  // Alrededor de un jugador (con varios, uno al azar cada vez)
  const std::vector<Player*> players = activePlayers();
  if (players.empty()) return;
  const Player& center = *players[players.size() == 1 ? 0 : rng_.nextInt(static_cast<int>(players.size()))];
  for (int attempt = 0; attempt < 4; attempt++) {
    const double ang = rng_.nextFloat() * 2 * kPi, dist = 24.0 + rng_.nextFloat() * 40.0;
    const int x = static_cast<int>(std::floor(center.pos.x + std::cos(ang) * dist));
    const int z = static_cast<int>(std::floor(center.pos.z + std::sin(ang) * dist));
    const Chunk* c = w.chunkAt(x, z);
    if (!c || c->topSection() < 0) continue;
    const int y = 1 + rng_.nextInt(std::min((c->topSection() + 1) * 16, kChunkHeight - 3));
    if (!validSpot(x, y, z)) continue;
    const MobType type = static_cast<MobType>(static_cast<int>(MobType::Zombie) + rng_.nextInt(4));
    const int pack = 1 + rng_.nextInt(3);
    for (int i = 0; i < pack; i++) {
      const int px = x + rng_.nextInt(5) - 2, pz = z + rng_.nextInt(5) - 2;
      if (i > 0 && !validSpot(px, y, pz)) continue;
      const glm::dvec3 at{px + 0.5, static_cast<double>(y), pz + 0.5};
      if (nearestPlayerDistance(at) < 24.0) continue;
      if (type == MobType::Spider && (!freeForMob(w, px + 1, y, pz) || !freeForMob(w, px - 1, y, pz))) continue;
      spawnMob(type, at);
      if (++hostiles >= kMaxHostile) return;
    }
  }
}

// --- Tick -----------------------------------------------------------------------------------------

void GameSession::tickMobs() {
  World& w = access_.world();
  // Fuera: criaturas de chunks descargados y monstruos lejos del jugador (como en el juego)
  std::erase_if(mobs_, [&](const Mob& m) {
    if (!w.chunkAt(static_cast<int>(std::floor(m.pos.x)), static_cast<int>(std::floor(m.pos.z)))) return true;
    if (m.pos.y < -64) return true;
    if (m.info().hostile && !m.persistent) {
      const double d = nearestPlayerDistance(m.pos);
      if (d > 128) return true;
      if (d > 32 && m.age > 600 && rng_.nextInt(800) == 0) return true;
    }
    return false;
  });

  for (std::size_t i = 0; i < mobs_.size(); i++) {
    Mob& m = mobs_[i];
    m.prevPos = m.pos;
    m.prevYaw = m.yaw;
    m.prevHeadYaw = m.headYaw;
    m.prevPitch = m.pitch;
    m.prevFuse = m.fuse;
    m.prevLimbAmount = m.limbAmount;
    // Lejos del jugador se quedan quietas (ahorra CPU; vuelven a moverse al acercarte)
    if (nearestPlayerDistance(m.pos) > 80 && !m.dying()) continue;
    m.age++;
    if (m.hurtTime > 0) m.hurtTime--;
    if (m.invulnerable > 0) m.invulnerable--;
    if (m.attackCooldown > 0) m.attackCooldown--;
    if (m.dying()) {
      m.deathTime++;
      m.moveForward = 0;
      m.wantJump = false;
      moveMob(m);
      continue;
    }
    // Crecer (o esperar para volver a criar) y modo amor
    if (m.growth < 0) m.growth++;
    else if (m.growth > 0) m.growth--;
    if (m.growth != 0) m.inLove = 0;
    if (m.inLove > 0) {
      if (--m.inLove % 10 == 0) loveHearts(m, 1);
      if (m.inLove == 0) m.lovedByPlayer = false;
    }
    if (m.noAI) {
      m.moveForward = 0;
      m.wantJump = false;
    } else {
      mobAI(mobs_[i]);
    }
    if (i >= mobs_.size()) break;  // una explosión puede haber quitado criaturas
    Mob& mm = mobs_[i];
    moveMob(mm);
    // Fuego: 1 de daño por segundo; el agua lo apaga
    if (mm.fireTicks > 0) {
      if (mm.inWater) {
        mm.fireTicks = 0;
      } else {
        if (mm.fireTicks % 20 == 0) hurtMob(mm, 1.0f, mm.pos, 0.0f, false);
        mm.fireTicks--;
      }
    }
    // Las gallinas ponen huevos
    if (mm.type == MobType::Chicken && !mm.dying() && !mm.baby() && --mm.eggTimer <= 0) {
      spawnItem(mm.pos + glm::dvec3(0, 0.3, 0), ItemStack(ItemId::egg), {0, 0.1, 0}, 10);
      mm.eggTimer = 6000 + rng_.nextInt(6000);
    }
  }
  for (Mob& b : newMobs_) addMob(std::move(b));
  newMobs_.clear();
  // Fin de la animación de muerte: desaparecen (con humo en el cliente)
  for (const Mob& m : mobs_)
    if (m.deathTime >= 20) {
      SessionEvent e{SessionEvent::Type::MobDied, glm::ivec3(glm::floor(m.pos)), 0};
      e.where = m.pos;
      e.mob = m.type;
      events_.push_back(e);
    }
  std::erase_if(mobs_, [](const Mob& m) { return m.deathTime >= 20; });
  pushEntities();
}

void GameSession::moveMob(Mob& m) {
  World& w = access_.world();
  auto idAt = [&](const glm::dvec3& p) {
    return stateId(w.block(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))));
  };
  m.inWater = isWater(idAt(m.pos + glm::dvec3(0, 0.4, 0))) || isLava(idAt(m.pos + glm::dvec3(0, 0.4, 0)));
  const glm::dvec3 dir = forwardOf(m.yaw);
  float slip = 0.91f;
  if (m.inWater) {
    m.motion += dir * static_cast<double>(m.moveForward * 0.02f);
    if (m.wantJump || rng_.nextFloat() < 0.8f) m.motion.y += 0.04;  // nadan hacia arriba
  } else {
    if (m.onGround) slip = slipperiness(idAt(m.pos - glm::dvec3(0, 0.5, 0))) * 0.91f;
    const double accel = m.onGround ? m.moveSpeed * 0.16277136 / (slip * slip * slip) : 0.02;
    m.motion += dir * (m.moveForward * accel);
    if (m.wantJump && m.onGround) m.motion.y = 0.42;
  }
  AABB b = m.box();
  glm::dvec3 mv = m.motion;
  const MoveResult r = moveBox(w, b, mv, 0.6, m.onGround);
  m.pos = {b.center().x, b.min.y, b.center().z};
  m.collidedH = r.collidedX || r.collidedZ;
  if (r.collidedX) m.motion.x = 0;
  if (r.collidedZ) m.motion.z = 0;
  if (r.collidedY) m.motion.y = 0;
  // Caídas
  if (m.inWater) {
    m.fallDistance = 0;
  } else if (r.onGround) {
    if (m.fallDistance > 3.0 && m.type != MobType::Chicken && !m.dying())
      hurtMob(m, static_cast<float>(std::ceil(m.fallDistance - 3.0)), m.pos, 0.0f, false);
    m.fallDistance = 0;
  } else if (mv.y < 0) {
    m.fallDistance -= mv.y;
  }
  m.onGround = r.onGround;
  if (m.inWater) {
    m.motion *= 0.8;
    m.motion.y -= 0.02;
  } else {
    m.motion.y = (m.motion.y - 0.08) * 0.98;
    m.motion.x *= slip;
    m.motion.z *= slip;
    if (m.type == MobType::Chicken && !m.onGround && m.motion.y < 0) m.motion.y *= 0.6;  // aletea
  }
  // Animación de andar
  const double dx = m.pos.x - m.prevPos.x, dz = m.pos.z - m.prevPos.z;
  const float speed = std::min(1.0f, static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 4.0f);
  m.limbAmount += (speed - m.limbAmount) * 0.4f;
  m.limbSwing += m.limbAmount;
}

void GameSession::walkTowards(Mob& m, const glm::dvec3& target, float speedMul) {
  const glm::dvec3 d = target - m.pos;
  if (std::hypot(d.x, d.z) < 0.25) return;
  m.yaw = approachAngle(m.yaw, std::atan2(static_cast<float>(-d.x), static_cast<float>(-d.z)), 0.52f);  // 30º por tick
  // Como en 1.8: el avance de la IA ES su velocidad (0,25 en un cerdo), y la aceleración vuelve a
  // multiplicar por ella. Con avance 1 las criaturas corrían 4 veces más de la cuenta.
  m.moveSpeed = m.info().speed * speedMul;
  m.moveForward = m.moveSpeed;
  if ((m.collidedH && m.onGround) || m.inWater) m.wantJump = true;
}

void GameSession::wander(Mob& m, int chance, float speedMul) {
  World& w = access_.world();
  if (m.walkTarget) {
    const glm::dvec3 d = *m.walkTarget - m.pos;
    if (std::hypot(d.x, d.z) < 0.8) {
      m.walkTarget.reset();
      return;
    }
    if (m.age % 40 == 0) {
      if (glm::length(m.pos - m.lastProgressPos) < 0.4) {  // atascada: otro destino
        m.walkTarget.reset();
        return;
      }
      m.lastProgressPos = m.pos;
    }
    // No tirarse por un precipicio al pasear
    const glm::dvec3 ahead = m.pos + forwardOf(m.yaw) * (m.info().width * 0.5 + 0.5);
    const int ax = static_cast<int>(std::floor(ahead.x)), az = static_cast<int>(std::floor(ahead.z));
    const int ay = static_cast<int>(std::floor(m.pos.y));
    bool ground = false;
    for (int dy = 0; dy <= 3 && !ground; dy++) {
      const int id = stateId(w.block(ax, ay - dy, az));
      ground = (id != B::air && !collisionBoxes(id, stateMeta(w.block(ax, ay - dy, az))).empty()) || isWater(id);
    }
    if (!ground && m.onGround && !m.inWater) {
      m.walkTarget.reset();
      return;
    }
    walkTowards(m, *m.walkTarget, speedMul);
    return;
  }
  if (m.inWater) m.wantJump = true;
  if (rng_.nextInt(chance) != 0) return;
  for (int tries = 0; tries < 6; tries++) {
    const int tx = static_cast<int>(std::floor(m.pos.x)) + rng_.nextInt(21) - 10;
    const int tz = static_cast<int>(std::floor(m.pos.z)) + rng_.nextInt(21) - 10;
    const int ty0 = static_cast<int>(std::floor(m.pos.y));
    for (int ty = ty0 + 3; ty >= ty0 - 4; ty--) {
      if (!blockInfo(stateId(w.block(tx, ty - 1, tz))).opaqueCube) continue;
      if (!freeForMob(w, tx, ty, tz) || !freeForMob(w, tx, ty + 1, tz)) break;
      m.walkTarget = glm::dvec3(tx + 0.5, ty, tz + 0.5);
      m.lastProgressPos = m.pos;
      return;
    }
  }
}

std::vector<Player*> GameSession::activePlayers() {
  std::vector<Player*> out;
  if (localActive_) out.push_back(&player_);
  for (Player* p : others_)
    if (p) out.push_back(p);
  return out;
}

Player* GameSession::nearestPlayer(const glm::dvec3& at, bool attackable) {
  Player* best = nullptr;
  double bestD = 0;
  for (Player* p : activePlayers()) {
    if (p->dead || (attackable && p->creative())) continue;
    const double d = glm::length(p->pos - at);
    if (!best || d < bestD) {
      best = p;
      bestD = d;
    }
  }
  return best;
}

double GameSession::nearestPlayerDistance(const glm::dvec3& at) {
  double best = 1e9;
  for (Player* p : activePlayers()) best = std::min(best, glm::length(p->pos - at));
  return best;
}

bool GameSession::canSeePlayer(const Mob& m, const Player& p) const {
  const glm::dvec3 from = m.eyePos(), to = p.eyePos();
  const glm::dvec3 d = to - from;
  const double len = glm::length(d);
  if (len < 1e-6) return true;
  const auto hit = raycastBlocks(const_cast<WorldAccess&>(access_).world(), from, d / len, len);
  return !hit;
}

void GameSession::mobAI(Mob& m) {
  World& w = access_.world();
  m.moveForward = 0;
  m.wantJump = false;
  const MobInfo& info = m.info();
  const int darkness = skyDarkness(worldTime_);
  const glm::dvec3 eye = m.eyePos();
  const int ex = static_cast<int>(std::floor(eye.x)), ey = static_cast<int>(std::floor(eye.y)), ez = static_cast<int>(std::floor(eye.z));

  auto lookAtPlayer = [&](const Player& who) {
    const glm::dvec3 pe = who.eyePos();
    const float target = yawTowards(eye, pe);
    m.headYaw = approachAngle(m.headYaw, target, 0.35f);
    // La cabeza no gira más de 75º respecto al cuerpo
    const float rel = std::clamp(wrapAngle(m.headYaw - m.yaw), -1.3f, 1.3f);
    m.headYaw = m.yaw + rel;
    const double h = std::hypot(pe.x - eye.x, pe.z - eye.z);
    m.pitch = std::clamp(static_cast<float>(std::atan2(pe.y - eye.y, h)), -1.2f, 1.2f);
  };
  auto relaxHead = [&] {
    m.headYaw = approachAngle(m.headYaw, m.yaw, 0.3f);
    m.pitch += (0.0f - m.pitch) * 0.2f;
  };

  // Zombis y esqueletos arden al sol si les da el cielo
  if ((m.type == MobType::Zombie || m.type == MobType::Skeleton) && darkness < 4 && !m.inWater &&
      w.skyLight(ex, ey, ez) >= 15 && rng_.nextFloat() * 30.0f < 1.2f)
    m.fireTicks = std::max(m.fireTicks, 160);

  if (info.hostile) {
    // Van a por el jugador más cercano que se pueda atacar (ni muerto ni en creativo)
    Player* target = nearestPlayer(m.pos, true);
    const double dist = target ? glm::length(m.pos - target->pos) : 1e9;
    bool aggressive = target != nullptr;
    if (m.type == MobType::Spider && !m.chasing &&
        effectiveLight(w.skyLight(ex, ey, ez), w.blockLight(ex, ey, ez), darkness) > 9)
      aggressive = false;  // con luz, las arañas no atacan salvo que las provoques
    const double follow = m.type == MobType::Zombie ? 35.0 : 16.0;
    if (aggressive && !m.chasing && dist < follow && canSeePlayer(m, *target)) m.chasing = true;
    if (!aggressive || dist > follow + 8) m.chasing = false;

    if (!m.chasing) {
      if (m.type == MobType::Creeper) m.fuse = std::max(0, m.fuse - 1);
      relaxHead();
      wander(m, 120, 1.0f);
      return;
    }
    Player& tgt = *target;
    m.walkTarget.reset();
    lookAtPlayer(tgt);
    const bool see = canSeePlayer(m, tgt);
    switch (m.type) {
      case MobType::Zombie:
      case MobType::Spider: {
        walkTowards(m, tgt.pos, 1.0f);
        if (m.type == MobType::Spider) {
          if (m.collidedH) m.motion.y = 0.2;  // trepa por las paredes
          if (m.onGround && dist > 2.0 && dist < 4.0 && rng_.nextInt(10) == 0) {
            const glm::dvec3 d = glm::normalize(glm::dvec3(tgt.pos.x - m.pos.x, 0, tgt.pos.z - m.pos.z));
            m.motion.x = d.x * 0.4 + m.motion.x * 0.2;
            m.motion.z = d.z * 0.4 + m.motion.z * 0.2;
            m.motion.y = 0.4;  // salto de ataque
          }
        }
        const double reach = info.width * 2.0;
        const glm::dvec3 d = tgt.pos - m.pos;
        if (d.x * d.x + d.z * d.z <= reach * reach + Player::kWidth && std::abs(d.y) < 2.0 && m.attackCooldown == 0 && see) {
          damagePlayer(tgt, info.attackDamage, m.pos, 0.4f, DamageKind::Melee, &m);
          m.attackCooldown = 20;
        }
        break;
      }
      case MobType::Creeper: {
        // Se enciende a menos de 3 bloques y se apaga si te alejas más de 7
        const int state = (!see || dist > 7.0) ? -1 : (dist < 3.0 ? 1 : (m.fuse > 0 ? 1 : -1));
        if (state > 0) {
          if (m.fuse == 0) {
            SessionEvent e{SessionEvent::Type::CreeperFuse, glm::ivec3(glm::floor(m.pos)), 0};
            e.where = m.pos;
            e.mob = m.type;
            events_.push_back(e);
          }
          m.fuse++;
        } else {
          m.fuse = std::max(0, m.fuse - 1);
        }
        if (m.fuse == 0) walkTowards(m, tgt.pos, 1.0f);
        if (m.fuse >= 30) {
          const glm::dvec3 at = m.pos + glm::dvec3(0, info.height * 0.5, 0);
          m.health = 0;
          m.deathTime = 20;  // desaparece sin animación ni botín
          explode(at, 3.0f);
          return;
        }
        break;
      }
      case MobType::Skeleton: {
        if (dist > 12.0 || !see) walkTowards(m, tgt.pos, 1.0f);
        else m.yaw = approachAngle(m.yaw, yawTowards(m.pos, tgt.pos), 0.5f);
        if (see && dist < 15.0 && m.attackCooldown == 0) {
          shootArrow(m, tgt);
          m.attackCooldown = 20 + static_cast<int>(dist / 15.0 * 40.0);
        }
        break;
      }
      default: break;
    }
    return;
  }

  // --- Animales ---
  if (m.panicTicks > 0) {
    m.panicTicks--;
    if (!m.walkTarget || m.panicTicks % 20 == 0)
      m.walkTarget = m.pos + glm::dvec3(rng_.nextInt(11) - 5, 0, rng_.nextInt(11) - 5);
    const float speed = m.type == MobType::Cow ? 2.0f : (m.type == MobType::Chicken ? 1.4f : 1.25f);
    walkTowards(m, *m.walkTarget, speed);
    relaxHead();
    return;
  }
  // Modo amor: buscan a otro igual que también lo esté y, tras 60 ticks juntos y a menos de 3 bloques, tienen una cría
  if (m.inLove > 0) {
    if (Mob* mate = findMate(m)) {
      const glm::dvec3 target = mate->pos;
      m.headYaw = approachAngle(m.headYaw, yawTowards(m.pos, target), 0.35f);
      walkTowards(m, target, 1.0f);
      m.walkTarget.reset();
      const glm::dvec3 d = target - m.pos;
      if (++m.mateTicks >= 60 && d.x * d.x + d.y * d.y + d.z * d.z < 9.0) breedMobs(m, *mate);
      return;
    }
  }
  m.mateTicks = 0;
  // Comida en la mano de un jugador cercano: le siguen (al más cercano de los que la llevan)
  auto tempts = [&](const Player& p) {
    const int held = p.inventory.selected().id;
    return (m.type == MobType::Pig && held == ItemId::carrot) ||
           ((m.type == MobType::Cow || m.type == MobType::Sheep) && held == ItemId::wheat) ||
           (m.type == MobType::Chicken && held == ItemId::wheat_seeds);
  };
  const Player* tempter = nullptr;
  double tdist = 10;
  for (Player* p : activePlayers()) {
    const double d = glm::length(m.pos - p->pos);
    if (!p->dead && d < tdist && tempts(*p)) {
      tempter = p;
      tdist = d;
    }
  }
  if (tempter) {
    lookAtPlayer(*tempter);
    if (tdist > 2.5) walkTowards(m, tempter->pos, 1.1f);
    m.walkTarget.reset();
    return;
  }
  // Las crías siguen al adulto más cercano de los suyos (a menos de 8 bloques) si se alejan más de 3
  if (m.baby()) {
    const Mob* parent = nullptr;
    double best = 64.0;
    for (const Mob& o : mobs_) {
      if (o.type != m.type || o.baby() || o.dying() || std::abs(o.pos.y - m.pos.y) > 4.0) continue;
      const glm::dvec3 d = o.pos - m.pos;
      const double dd = d.x * d.x + d.y * d.y + d.z * d.z;
      if (dd < best) {
        best = dd;
        parent = &o;
      }
    }
    if (parent && best >= 9.0) {
      walkTowards(m, parent->pos, 1.1f);
      m.walkTarget.reset();
      relaxHead();
      return;
    }
  }
  // Ovejas: de vez en cuando comen hierba (y les vuelve a crecer la lana)
  if (m.type == MobType::Sheep) {
    const int bx = static_cast<int>(std::floor(m.pos.x)), by = static_cast<int>(std::floor(m.pos.y)) - 1,
              bz = static_cast<int>(std::floor(m.pos.z));
    if (m.eatGrassTicks > 0) {
      m.eatGrassTicks--;
      m.pitch = -0.9f;
      if (m.eatGrassTicks == 4 && stateId(w.block(bx, by, bz)) == B::grass) {
        setAndUpdate(bx, by, bz, makeState(B::dirt));
        m.sheared = false;
      }
      return;
    }
    if (m.onGround && rng_.nextInt(1000) == 0 && stateId(w.block(bx, by, bz)) == B::grass) {
      m.eatGrassTicks = 40;
      m.walkTarget.reset();
      return;
    }
  }
  // De vez en cuando miran al jugador más cercano
  const Player* watched = nullptr;
  double wdist = 1e9;
  for (const Player* p : activePlayers())
    if (const double d = glm::length(m.pos - p->pos); d < wdist) {
      watched = p;
      wdist = d;
    }
  if (m.lookTicks > 0) {
    m.lookTicks--;
    if (watched) lookAtPlayer(*watched);
    else relaxHead();
  } else {
    relaxHead();
    if (wdist < 6 && rng_.nextInt(50) == 0) m.lookTicks = 40 + rng_.nextInt(40);
  }
  wander(m, 120, 1.0f);
}

void GameSession::pushEntities() {
  // Las criaturas no se atraviesan: se empujan un poco (y al jugador)
  auto push = [](glm::dvec3& ma, glm::dvec3& mb, const glm::dvec3& pa, const glm::dvec3& pb, double minDist, double ka, double kb) {
    double dx = pb.x - pa.x, dz = pb.z - pa.z;
    double d = std::max(std::abs(dx), std::abs(dz));
    if (d >= minDist || d < 0.01) return;
    d = std::sqrt(d);
    dx /= d;
    dz /= d;
    const double k = std::min(1.0, 1.0 / d) * 0.05;
    ma.x -= dx * k * ka;
    ma.z -= dz * k * ka;
    mb.x += dx * k * kb;
    mb.z += dz * k * kb;
  };
  for (std::size_t i = 0; i < mobs_.size(); i++) {
    Mob& a = mobs_[i];
    if (a.dying()) continue;
    for (std::size_t j = i + 1; j < mobs_.size(); j++) {
      Mob& b = mobs_[j];
      if (b.dying() || std::abs(a.pos.y - b.pos.y) > 1.5) continue;
      push(a.motion, b.motion, a.pos, b.pos, (a.info().width + b.info().width) * 0.5, 1.0, 1.0);
    }
    // (solo al jugador local: el movimiento de los invitados lo lleva su cliente)
    if (localActive_ && !player_.dead && std::abs(a.pos.y - player_.pos.y) < 1.5 && !player_.flying)
      push(player_.motion, a.motion, player_.pos, a.pos, (a.info().width + Player::kWidth) * 0.5, 0.3, 1.0);
  }
}

// --- Cría -----------------------------------------------------------------------------------------

void GameSession::loveHearts(const Mob& m, int count) {
  SessionEvent e{SessionEvent::Type::LoveHearts, glm::ivec3(glm::floor(m.pos)), 0};
  e.where = m.pos + glm::dvec3(0, m.info().height * m.scale() * 0.8, 0);
  e.mob = m.type;
  e.value = count;
  events_.push_back(e);
}

bool GameSession::feedAnimal(Mob& m) {
  ItemStack& held = player_.inventory.selected();
  if (m.dying() || !isBreedable(m.type) || held.empty() || held.id != breedingItem(m.type)) return false;
  auto consume = [&] {
    if (!player_.creative() && --held.count <= 0) held.clear();
  };
  if (m.growth == 0 && m.inLove == 0) {  // adulta y sin esperar: entra en modo amor
    consume();
    m.inLove = kLoveTicks;
    m.lovedByPlayer = true;
    m.mateTicks = 0;
    loveHearts(m, 7);
    return true;
  }
  if (m.baby()) {  // una cría crece antes: cada vez le quitan el 10 % de lo que le falta
    consume();
    m.growth = std::min(0, m.growth + static_cast<int>(static_cast<float>(-m.growth / 20) * 0.1f) * 20);
    return true;
  }
  return false;
}

Mob* GameSession::findMate(const Mob& m) {
  Mob* best = nullptr;
  double bestD = 64.0;  // hasta 8 bloques
  for (Mob& o : mobs_) {
    if (&o == &m || o.type != m.type || o.dying() || o.baby() || o.inLove <= 0) continue;
    const glm::dvec3 d = o.pos - m.pos;
    const double dd = d.x * d.x + d.y * d.y + d.z * d.z;
    if (dd < bestD) {
      bestD = dd;
      best = &o;
    }
  }
  return best;
}

void GameSession::breedMobs(Mob& a, Mob& b) {
  Mob baby;
  baby.type = a.type;
  baby.pos = baby.prevPos = baby.lastProgressPos = a.pos;
  baby.yaw = baby.prevYaw = baby.headYaw = baby.prevHeadYaw = rng_.nextFloat() * 2 * kPi - kPi;
  baby.health = baby.info().maxHealth;
  baby.growth = -kBabyTicks;
  baby.eggTimer = 6000 + rng_.nextInt(6000);
  if (a.type == MobType::Sheep) baby.woolColor = rng_.nextInt(2) ? a.woolColor : b.woolColor;  // el color de uno de los dos
  newMobs_.push_back(baby);
  // Los padres esperan 5 minutos para volver a criar
  loveHearts(a, 7);
  loveHearts(b, 7);
  if (a.lovedByPlayer || b.lovedByPlayer) {
    achievements_.addStat("stat.animalsBred");
    if (a.type == MobType::Cow) award(Ach::BreedCow);
  }
  a.growth = b.growth = kBreedCooldown;
  a.inLove = b.inLove = 0;
  a.mateTicks = b.mateTicks = 0;
  a.lovedByPlayer = b.lovedByPlayer = false;
  spawnXp(a.pos, 1 + rng_.nextInt(7));  // criar da de 1 a 7 puntos
}

// --- Combate --------------------------------------------------------------------------------------

std::optional<std::pair<std::size_t, double>> GameSession::raycastMobs(const glm::dvec3& origin, const glm::dvec3& dir,
                                                                       double maxDist) const {
  std::optional<std::pair<std::size_t, double>> best;
  for (std::size_t i = 0; i < mobs_.size(); i++) {
    const Mob& m = mobs_[i];
    if (m.dying()) continue;
    const auto t = rayBox(origin, dir, m.box().expand({0.1, 0.1, 0.1}));
    if (t && *t <= maxDist && (!best || *t < best->second)) best = std::pair{i, *t};
  }
  return best;
}

const Mob* GameSession::targetedMob() const {
  if (!targetMob_) return nullptr;
  for (const Mob& m : mobs_)
    if (m.id == *targetMob_) return &m;
  return nullptr;
}

void GameSession::attackMob(Mob& m) {
  if (m.dying()) return;
  if (remote_) {
    if (remote_->useEntity) remote_->useEntity(m.id, true);
    return;
  }
  const ItemStack held = player_.inventory.selected();
  float damage = weaponDamage(held);
  const bool crit = player_.fallDistance > 0 && !player_.onGround && !player_.inWater && !player_.flying;
  if (crit) damage *= 1.5f;
  damage += enchfx::weaponBonus(held, m.type);  // (Filo, Pesadez y Perdición de los artrópodos se suman tras el crítico)
  const float before = m.health;
  const int hurtBefore = m.invulnerable;
  hurtMob(m, damage, player_.pos, 0.4f, true, held.enchantLevel(Ench::Looting));
  if (m.health >= before && m.invulnerable == hurtBefore) return;  // no ha contado (invulnerable)
  // Correr y Retroceso empujan más (medio bloque por punto) y le quitan la carrera al que golpea
  const int push = held.enchantLevel(Ench::Knockback) + (player_.sprinting ? 1 : 0);
  if (push > 0) {
    const glm::dvec3 f = forwardOf(player_.yaw);
    m.motion += glm::dvec3(f.x * 0.5 * push, 0.1, f.z * 0.5 * push);
    player_.sprinting = false;
    player_.motion.x *= 0.6;
    player_.motion.z *= 0.6;
  }
  // Aspecto ígneo: 4 s ardiendo por nivel
  if (const int fire = held.enchantLevel(Ench::FireAspect); fire > 0 && !m.dying()) m.fireTicks = std::max(m.fireTicks, 80 * fire);
  if (crit) {
    SessionEvent e{SessionEvent::Type::MobCrit, glm::ivec3(glm::floor(m.pos)), 0};
    e.where = m.pos + glm::dvec3(0, m.info().height * 0.6, 0);
    e.mob = m.type;
    events_.push_back(e);
  }
  if (!player_.creative()) {
    if (isSword(held.id)) damageTool(1);
    else if (held.isTool()) damageTool(2);
  }
  player_.addExhaustion(0.3f);
}

bool GameSession::attackPlayer(Player& attacker, Player& victim) {
  if (&attacker == &victim || attacker.dead || victim.dead || victim.creative()) return false;
  const ItemStack held = attacker.inventory.selected();
  float damage = weaponDamage(held);
  const bool crit = attacker.fallDistance > 0 && !attacker.onGround && !attacker.inWater && !attacker.flying;
  if (crit) damage *= 1.5f;
  damage += enchfx::weaponBonus(held, MobType::Pig);  // (Filo; Pesadez y Perdición solo valen contra criaturas)
  if (!victim.damage(damage, true, DamageKind::Melee)) return false;  // (la armadura cuenta; tras un golpe hay un rato de invulnerabilidad)
  // Empujón desde el atacante (más si corre); a un invitado se lo manda el servidor como velocidad
  const double dx = attacker.pos.x - victim.pos.x, dz = attacker.pos.z - victim.pos.z;
  const double len = std::max(1e-4, std::sqrt(dx * dx + dz * dz));
  const int push = held.enchantLevel(Ench::Knockback) + (attacker.sprinting ? 1 : 0);
  const double kb = 0.4 + 0.5 * push;
  victim.motion.x = victim.motion.x * 0.5 - dx / len * kb;
  victim.motion.z = victim.motion.z * 0.5 - dz / len * kb;
  victim.motion.y = std::min(0.4, victim.motion.y * 0.5 + 0.4);
  if (&victim == &player_) events_.push_back({SessionEvent::Type::PlayerHurt, glm::ivec3(glm::floor(victim.pos)), 0});
  if (const int fire = held.enchantLevel(Ench::FireAspect); fire > 0) victim.fireTicks = std::max(victim.fireTicks, 80 * fire);
  reflectThorns(victim, nullptr, &attacker);
  if (!attacker.creative()) {
    ItemStack& t = attacker.inventory.selected();
    if (t.isTool()) enchfx::wearItem(t, isSword(t.id) ? 1 : 2, rng_);
  }
  attacker.addExhaustion(0.3f);
  attacker.sprinting = false;
  return true;
}

void GameSession::hurtMob(Mob& m, float amount, const glm::dvec3& from, float knockback, bool byPlayer, int looting) {
  if (m.dying() || amount <= 0) return;
  // Tras un golpe hay medio segundo en que solo cuenta lo que supere al golpe anterior
  if (m.invulnerable > 10) {
    if (amount <= m.lastDamage) return;
    m.health -= amount - m.lastDamage;
    m.lastDamage = amount;
  } else {
    m.health -= amount;
    m.lastDamage = amount;
    m.invulnerable = 20;
    m.hurtTime = 10;
  }
  if (knockback > 0) {
    double dx = from.x - m.pos.x, dz = from.z - m.pos.z;
    double len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4) {
      dx = rng_.nextFloat() - 0.5;
      dz = rng_.nextFloat() - 0.5;
      len = std::sqrt(dx * dx + dz * dz);
    }
    m.motion *= 0.5;
    m.motion.x -= dx / len * knockback;
    m.motion.z -= dz / len * knockback;
    m.motion.y = std::min(0.4, m.motion.y + knockback);
  }
  if (!m.info().hostile) {
    m.panicTicks = 60 + rng_.nextInt(40);
    m.walkTarget.reset();
  } else if (byPlayer) {
    m.chasing = true;
  }
  SessionEvent e{SessionEvent::Type::MobHurt, glm::ivec3(glm::floor(m.pos)), 0};
  e.where = m.pos;
  e.mob = m.type;
  e.value = m.baby() ? 1 : 0;  // las crías suenan más agudas
  events_.push_back(e);
  if (byPlayer && amount >= 18.0f) award(Ach::Overkill);
  if (byPlayer) achievements_.addStat("stat.damageDealt", static_cast<i64>(amount * 10));
  if (m.health <= 0) {
    m.health = 0;
    m.deathTime = 1;
    mobDrops(m, looting);
    if (byPlayer) {
      achievements_.addStat("stat.mobKills");
      if (m.info().hostile) award(Ach::KillEnemy);
      // Experiencia: 5 los monstruos, 1 a 3 los animales y nada las crías
      const int xp = m.info().hostile ? 5 : (m.baby() ? 0 : 1 + rng_.nextInt(3));
      if (xp > 0) spawnXp(m.pos + glm::dvec3(0, m.info().height * m.scale() * 0.5, 0), xp);
    }
  }
}

void GameSession::mobDrops(const Mob& m, int looting) {
  auto drop = [&](int id, int count, int meta = 0) {
    if (count <= 0) return;
    spawnItem(m.pos + glm::dvec3(0, 0.5, 0), ItemStack(id, count, meta),
              {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  };
  const bool burning = m.fireTicks > 0;
  const int L = std::max(0, looting);
  const int more = L > 0 ? rng_.nextInt(1 + L) : 0;  // (Botín: unos cuantos más en lo que sueltan los animales)
  switch (m.type) {
    case MobType::Pig: drop(burning ? ItemId::cooked_porkchop : ItemId::porkchop, 1 + rng_.nextInt(3) + more); break;
    case MobType::Cow:
      drop(ItemId::leather, rng_.nextInt(3) + more);
      drop(burning ? ItemId::cooked_beef : ItemId::beef, 1 + rng_.nextInt(3) + (L > 0 ? rng_.nextInt(1 + L) : 0));
      break;
    case MobType::Sheep:
      if (!m.sheared) drop(B::wool, 1, m.woolColor);
      drop(burning ? ItemId::cooked_mutton : ItemId::mutton, 1 + rng_.nextInt(2) + more);
      break;
    case MobType::Chicken:
      drop(ItemId::feather, rng_.nextInt(3) + more);
      drop(burning ? ItemId::cooked_chicken : ItemId::chicken, 1 + (L > 0 ? rng_.nextInt(1 + L) : 0));
      break;
    case MobType::Zombie:
      drop(ItemId::rotten_flesh, rng_.nextInt(3 + L));
      if (rng_.nextInt(200) - L < 5) {
        const int r = rng_.nextInt(3);
        drop(r == 0 ? ItemId::iron_ingot : (r == 1 ? ItemId::carrot : ItemId::potato), 1);
      }
      break;
    case MobType::Skeleton:
      drop(ItemId::arrow, rng_.nextInt(3 + L));
      drop(ItemId::bone, rng_.nextInt(3 + L));
      break;
    case MobType::Creeper: drop(ItemId::gunpowder, rng_.nextInt(3 + L)); break;
    case MobType::Spider:
      drop(ItemId::string, rng_.nextInt(3 + L));
      if (rng_.nextInt(3) == 0 || rng_.nextInt(1 + L) > 0) drop(ItemId::spider_eye, 1);
      break;
    default: break;
  }
}

void GameSession::damagePlayer(Player& p, float amount, const glm::dvec3& from, float knockback, DamageKind kind, Mob* attacker) {
  // Daño de criaturas y explosiones según la dificultad (como en 1.8)
  switch (rules_.difficulty) {
    case 0: return;
    case 1: amount = std::min(amount, amount / 2.0f + 1.0f); break;
    case 3: amount *= 1.5f; break;
    default: break;
  }
  if (!p.damage(amount, true, kind)) return;  // (golpes, flechas y explosiones: la armadura cuenta)
  if (&p == &player_) events_.push_back({SessionEvent::Type::PlayerHurt, glm::ivec3(glm::floor(p.pos)), 0});
  if (attacker && kind == DamageKind::Melee) reflectThorns(p, attacker, nullptr);
  // (a un invitado, el servidor le manda el empujón como velocidad)
  if (knockback > 0) {
    double dx = from.x - p.pos.x, dz = from.z - p.pos.z;
    const double len = std::max(1e-4, std::sqrt(dx * dx + dz * dz));
    p.motion.x = p.motion.x * 0.5 - dx / len * knockback;
    p.motion.z = p.motion.z * 0.5 - dz / len * knockback;
    p.motion.y = std::min(0.4, p.motion.y * 0.5 + knockback);
  }
}

void GameSession::reflectThorns(Player& victim, Mob* mobAttacker, Player* playerAttacker) {
  for (int i = 0; i < 4; i++) {
    ItemStack& piece = victim.inventory.armor(i);
    const int level = piece.enchantLevel(Ench::Thorns);
    if (level <= 0) continue;
    const int dmg = enchfx::thornsDamage(level, victim.rng);
    if (dmg > 0) {
      if (mobAttacker && !mobAttacker->dying()) hurtMob(*mobAttacker, static_cast<float>(dmg), victim.pos, 0.0f, false);
      else if (playerAttacker) playerAttacker->damage(static_cast<float>(dmg));
      enchfx::wearItem(piece, 3, victim.rng);  // (la pieza gasta 3 al devolver el golpe y 1 si no lo devuelve)
    } else {
      enchfx::wearItem(piece, 1, victim.rng);
    }
  }
}

// --- Flechas --------------------------------------------------------------------------------------

void GameSession::shootArrow(const Mob& from, const Player& victim) {
  Arrow a;
  a.pos = a.prevPos = from.eyePos() - glm::dvec3(0, 0.1, 0);
  const glm::dvec3 target = victim.pos + glm::dvec3(0, Player::kHeight / 3.0, 0);
  glm::dvec3 d = target - a.pos;
  d.y += std::hypot(d.x, d.z) * 0.2;  // apuntar un poco alto: la flecha cae
  d = glm::normalize(d);
  const double spread = 0.0075 * (14 - 4 * std::max(1, rules_.difficulty));  // menos puntería en fácil
  d += glm::dvec3(gaussian(rng_), gaussian(rng_), gaussian(rng_)) * spread;
  a.motion = glm::normalize(d) * 1.6;
  a.damage = 2.0f + rng_.nextFloat() * 0.25f + 0.22f;
  a.pickup = false;  // las de los esqueletos no se recogen (1.8)
  a.yaw = std::atan2(static_cast<float>(-a.motion.x), static_cast<float>(-a.motion.z));
  a.pitch = std::atan2(static_cast<float>(a.motion.y), static_cast<float>(std::hypot(a.motion.x, a.motion.z)));
  a.id = nextArrowId_++;
  arrows_.push_back(a);
  SessionEvent e{SessionEvent::Type::ArrowShot, glm::ivec3(glm::floor(from.pos)), 0};
  e.where = from.pos;
  e.mob = from.type;
  events_.push_back(e);
}

int GameSession::arrowCount() const {
  int n = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (const ItemStack& s = player_.inventory.slot(i); s.id == ItemId::arrow) n += s.count;
  return n;
}

bool GameSession::takeArrow() {
  for (int i = 0; i < PlayerInventory::kSize; i++) {
    ItemStack& s = player_.inventory.slot(i);
    if (s.id != ItemId::arrow || s.count <= 0) continue;
    if (--s.count <= 0) s.clear();
    return true;
  }
  return false;
}

void GameSession::shootBow(int ticks) {
  // Potencia según lo tensado (1 s = a tope); por debajo de 0,1 la flecha ni sale
  float f = static_cast<float>(ticks) / 20.0f;
  f = (f * f + f * 2.0f) / 3.0f;
  if (f < 0.1f) return;
  f = std::min(f, 1.0f);
  const bool creative = player_.creative();
  const ItemStack bow = player_.inventory.selected();
  const bool infinite = bow.enchantLevel(Ench::Infinity) > 0;  // (con Infinidad hace falta una flecha, pero no se gasta)
  if (!creative) {
    if (arrowCount() == 0) return;
    if (!infinite) takeArrow();
  }
  damageTool(1);
  Arrow a;
  // Sale un poco a la derecha de los ojos y algo más abajo, como si saliera del arco
  const glm::dvec3 right(std::cos(player_.yaw), 0.0, -std::sin(player_.yaw));
  a.pos = a.prevPos = player_.eyePos() + right * 0.16 - glm::dvec3(0, 0.1, 0);
  glm::dvec3 d = player_.lookDir() + glm::dvec3(gaussian(rng_), gaussian(rng_), gaussian(rng_)) * 0.0075;
  a.motion = glm::normalize(d) * (f * 3.0);
  a.damage = 2.0f;
  // Poder: medio punto más por nivel y medio de propina; Golpe empuja; Llama prende
  if (const int power = bow.enchantLevel(Ench::Power); power > 0) a.damage += 0.5f * static_cast<float>(power) + 0.5f;
  a.punch = bow.enchantLevel(Ench::Punch);
  a.flame = bow.enchantLevel(Ench::Flame) > 0;
  a.crit = f >= 1.0f;
  a.fromPlayer = true;
  a.pickup = !creative && !infinite;
  a.yaw = std::atan2(static_cast<float>(-a.motion.x), static_cast<float>(-a.motion.z));
  a.pitch = std::atan2(static_cast<float>(a.motion.y), static_cast<float>(std::hypot(a.motion.x, a.motion.z)));
  a.id = nextArrowId_++;
  arrows_.push_back(a);
  achievements_.addStat("stat.useItem.minecraft.bow");
  SessionEvent e{SessionEvent::Type::BowShot, glm::ivec3(glm::floor(player_.pos)), 0};
  e.where = player_.pos;
  e.value = static_cast<int>(f * 100.0f);
  events_.push_back(e);
}

void GameSession::arrowHitsMob(Arrow& a, Mob& m, double speed, const glm::dvec3& dir) {
  int dmg = static_cast<int>(std::ceil(speed * a.damage));
  if (a.crit) dmg += rng_.nextInt(dmg / 2 + 2);
  const bool wasAlive = !m.dying();
  hurtMob(m, static_cast<float>(dmg), player_.pos, 0.4f, true);
  if (a.punch > 0) {  // Retroceso: empuja en la dirección de la flecha
    const double h = std::hypot(dir.x, dir.z);
    if (h > 1e-4) m.motion += glm::dvec3(dir.x * a.punch * 0.6 / h, 0.1, dir.z * a.punch * 0.6 / h);
  }
  if (a.flame) m.fireTicks = 100;
  // Arquero: un esqueleto abatido desde 50 bloques o más (en horizontal)
  if (wasAlive && m.dying() && m.type == MobType::Skeleton) {
    const double dx = player_.pos.x - m.pos.x, dz = player_.pos.z - m.pos.z;
    if (dx * dx + dz * dz >= 2500.0) award(Ach::SnipeSkeleton);
  }
  SessionEvent e{SessionEvent::Type::ArrowHit, glm::ivec3(glm::floor(m.pos)), 0};
  e.where = a.pos;
  e.mob = m.type;
  events_.push_back(e);
  a.life = 1 << 20;  // se rompe al dar
}

void GameSession::tickArrows() {
  World& w = access_.world();
  for (Arrow& a : arrows_) {
    a.prevPos = a.pos;
    if (a.inGround) {
      a.life++;
      if (a.shake > 0) a.shake--;
      if (a.pickup && localActive_ && !player_.dead &&
          player_.box().expand({1.0, 0.5, 1.0}).intersects(AABB::centered(a.pos, 0.5, 0.5))) {
        const ItemStack rest = player_.inventory.add(ItemStack(ItemId::arrow));
        if (rest.empty()) {
          a.life = 1 << 20;
          events_.push_back({SessionEvent::Type::ItemPickedUp, glm::ivec3(glm::floor(a.pos)), 0});
        }
      }
      continue;
    }
    const double speed = glm::length(a.motion);
    if (speed < 1e-6) {
      a.inGround = true;
      continue;
    }
    const glm::dvec3 dir = a.motion / speed;
    const auto hit = raycastBlocks(w, a.pos, dir, speed);
    const double travel = hit ? hit->distance : speed;
    // Las flechas del jugador dan a la criatura más cercana del recorrido
    if (a.fromPlayer) {
      Mob* best = nullptr;
      double bestT = travel;
      for (Mob& m : mobs_) {
        if (m.dying()) continue;
        if (const auto t = rayBox(a.pos, dir, m.box().expand({0.3, 0.3, 0.3})); t && *t <= bestT) {
          best = &m;
          bestT = *t;
        }
      }
      if (best) {
        a.pos += dir * bestT;
        arrowHitsMob(a, *best, speed, dir);
        continue;
      }
    }
    bool hitPlayer = false;
    for (Player* p : activePlayers()) {
      if (p->dead || (a.fromPlayer && p == &player_ && a.flight < 5)) continue;  // (recién disparada no le da a quien la dispara)
      if (const auto t = rayBox(a.pos, dir, p->box().expand({0.3, 0.3, 0.3})); t && *t <= travel) {
        damagePlayer(*p, std::ceil(static_cast<float>(speed) * a.damage), a.pos - dir, 0.4f, DamageKind::Projectile);
        a.life = 1 << 20;  // se rompe al dar
        hitPlayer = true;
        break;
      }
    }
    if (hitPlayer) continue;
    if (hit) {
      a.pos = hit->point - dir * 0.05;
      a.inGround = true;
      a.shake = 7;
      SessionEvent e{SessionEvent::Type::ArrowHit, hit->block, 0};
      e.where = a.pos;
      events_.push_back(e);
      continue;
    }
    a.flight++;
    a.pos += a.motion;
    a.yaw = std::atan2(static_cast<float>(-a.motion.x), static_cast<float>(-a.motion.z));
    a.pitch = std::atan2(static_cast<float>(a.motion.y), static_cast<float>(std::hypot(a.motion.x, a.motion.z)));
    const bool water = isWater(stateId(w.block(static_cast<int>(std::floor(a.pos.x)), static_cast<int>(std::floor(a.pos.y)),
                                               static_cast<int>(std::floor(a.pos.z)))));
    a.motion *= water ? 0.6 : 0.99;
    a.motion.y -= 0.05;
  }
  std::erase_if(arrows_, [](const Arrow& a) { return a.life > 1200 || a.pos.y < -64; });
}

// --- Explosiones ----------------------------------------------------------------------------------

void GameSession::explode(const glm::dvec3& c, float power) {
  World& w = access_.world();
  // Rayos desde el centro (16x16x16 por la superficie de un cubo): cada uno pierde fuerza con la
  // distancia y con la resistencia de los bloques que atraviesa.
  std::set<std::tuple<int, int, int>> affected;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++)
      for (int k = 0; k < 16; k++) {
        if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15) continue;
        glm::dvec3 dir(i / 15.0 * 2.0 - 1.0, j / 15.0 * 2.0 - 1.0, k / 15.0 * 2.0 - 1.0);
        dir = glm::normalize(dir);
        float intensity = power * (0.7f + rng_.nextFloat() * 0.6f);
        glm::dvec3 p = c;
        for (; intensity > 0.0f; intensity -= 0.22500001f) {
          const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y)), z = static_cast<int>(std::floor(p.z));
          const BlockState s = w.block(x, y, z);
          if (s != 0) {
            const BlockInfo& bi = blockInfo(stateId(s));
            float res = bi.resistance;
            if (res <= 0 && bi.hardness > 0) res = bi.hardness;  // (los troncos vienen sin resistencia en los datos)
            intensity -= (res + 0.3f) * 0.3f;
            if (intensity > 0.0f && !isFluid(stateId(s)) && bi.hardness >= 0) affected.insert({x, y, z});
          }
          p += dir * 0.3;
        }
      }

  // Daño y empujón a lo que esté cerca, según la distancia y lo expuesto que esté
  const double radius = power * 2.0;
  auto exposure = [&](const AABB& box) {
    int seen = 0, total = 0;
    for (int ix = 0; ix <= 1; ix++)
      for (int iy = 0; iy <= 2; iy++)
        for (int iz = 0; iz <= 1; iz++) {
          const glm::dvec3 p(box.min.x + (box.max.x - box.min.x) * (0.1 + 0.8 * ix), box.min.y + (box.max.y - box.min.y) * (0.1 + 0.4 * iy),
                             box.min.z + (box.max.z - box.min.z) * (0.1 + 0.8 * iz));
          const glm::dvec3 d = p - c;
          const double len = glm::length(d);
          total++;
          if (len < 1e-4 || !raycastBlocks(w, c, d / len, len)) seen++;
        }
    return static_cast<double>(seen) / total;
  };
  auto blast = [&](const glm::dvec3& pos, const AABB& box, auto&& hurt, glm::dvec3& motion) {
    const double dist = glm::length(pos - c) / radius;
    if (dist > 1.0) return;
    const double impact = (1.0 - dist) * exposure(box);
    const float damage = static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * radius + 1.0);
    hurt(damage);
    glm::dvec3 d = pos - c;
    const double len = glm::length(d);
    if (len > 1e-4) motion += d / len * impact;
  };
  for (Player* p : activePlayers())
    if (!p->dead) blast(p->eyePos(), p->box(), [&](float dmg) { damagePlayer(*p, dmg, c, 0.0f, DamageKind::Explosion); }, p->motion);
  for (Mob& m : mobs_)
    if (!m.dying()) blast(m.eyePos(), m.box(), [&](float dmg) { hurtMob(m, dmg, c, 0.0f, false); }, m.motion);
  for (Minecart& cart : carts_)
    if (!cart.dead)
      blast(cart.pos + glm::dvec3(0, Minecart::kHeight / 2, 0), cart.box(),
            [&](float dmg) {
              cart.damage += dmg * 10.0f;
              cart.hurtTime = 10;
              if (cart.damage > 40.0f) killCart(cart, true, true);
            },
            cart.motion);

  // Bloques: un tercio suelta su objeto (1 / potencia), el resto se pierde
  access_.beginBatch();
  for (const auto& [x, y, z] : affected) {
    const BlockState s = w.block(x, y, z);
    if (s == 0) continue;
    if (rng_.nextFloat() < 1.0f / power)
      for (const ItemStack& d : blockDrops(s, ItemStack(), rng_))
        spawnItem(glm::dvec3(x + 0.5, y + 0.3, z + 0.5), d, {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
    furnaces_.erase({x, y, z});
    setWorldBlock(x, y, z, 0);
  }
  access_.endBatch();
  for (const auto& [x, y, z] : affected) neighborUpdates({x, y, z});
  // Los objetos que había en el suelo se destruyen
  std::erase_if(items_, [&](const ItemEntity& e) { return e.age > 5 && glm::length(e.pos - c) < power; });

  SessionEvent e{SessionEvent::Type::Explosion, glm::ivec3(glm::floor(c)), 0};
  e.where = c;
  events_.push_back(e);
}

}  // namespace mcw
