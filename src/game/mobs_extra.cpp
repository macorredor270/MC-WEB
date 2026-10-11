// Criaturas del Nether y del End: cerdo zombi, ghast, blaze, cubo de magma, slime, enderman, lepisma, araña de cueva y
// esqueleto atrofiado; bolas de fuego, generadores de monstruos y la aparición fuera del mundo normal. Los números son los de
// 1.8 (minecraft.wiki, dificultad normal); la implementación es propia.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "data/biomes.h"
#include "data/items.h"
#include "game/loot.h"
#include "game/mob.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float wrap(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}
float approach(float from, float to, float maxStep) {
  const float d = wrap(to - from);
  return from + std::clamp(d, -maxStep, maxStep);
}
float yawTo(const glm::dvec3& from, const glm::dvec3& to) { return std::atan2(static_cast<float>(-(to.x - from.x)), static_cast<float>(-(to.z - from.z))); }
glm::dvec3 forward(float yaw) { return {-std::sin(yaw), 0.0, -std::cos(yaw)}; }

bool freeCell(const World& w, int x, int y, int z) {
  const int id = stateId(w.block(x, y, z));
  if (isFluid(id)) return false;
  return id == B::air || blockCollision(w, x, y, z).empty();
}

struct EntityMap {
  int id;
  MobType type;
};
constexpr EntityMap kEntities[] = {
    {90, MobType::Pig},        {91, MobType::Sheep},    {92, MobType::Cow},       {93, MobType::Chicken},    {54, MobType::Zombie},
    {51, MobType::Skeleton},   {50, MobType::Creeper},  {52, MobType::Spider},    {57, MobType::PigZombie},  {56, MobType::Ghast},
    {61, MobType::Blaze},      {62, MobType::MagmaCube}, {55, MobType::Slime},    {58, MobType::Enderman},   {60, MobType::Silverfish},
    {59, MobType::CaveSpider},
};

}  // namespace

int mobEntityId(MobType t) {
  if (t == MobType::WitherSkeleton) return 51;
  for (const EntityMap& e : kEntities)
    if (e.type == t) return e.id;
  return 90;
}

std::optional<MobType> mobFromEntityId(int id) {
  for (const EntityMap& e : kEntities)
    if (e.id == id) return e.type;
  return std::nullopt;
}

// --- Creaciones ---------------------------------------------------------------------------------

Mob* GameSession::spawnSized(MobType type, const glm::dvec3& pos, int size) {
  Mob* m = spawnMob(type, pos);
  if (!m) return nullptr;
  m->size = static_cast<u8>(size);
  m->health = static_cast<float>(size * size);
  return m;
}

void GameSession::launchFireball(const Mob& from, const glm::dvec3& target, bool large) {
  Fireball f;
  f.id = nextFireballId_++;
  f.large = large;
  f.owner = from.id;
  const glm::dvec3 origin = from.pos + glm::dvec3(0, from.info().height * (large ? 0.5 : 0.75), 0);
  f.pos = f.prevPos = origin + (large ? glm::normalize(target - origin) * 2.0 : glm::dvec3(0));
  glm::dvec3 d = target - f.pos;
  const double len = std::max(1e-3, glm::length(d));
  d /= len;
  if (large) {
    f.accel = d * 0.1;  // arranca despacio y acelera
  } else {
    // El blaze las lanza con algo de dispersión
    f.motion = glm::dvec3(d.x + (rng_.nextFloat() - 0.5) * 0.15, d.y + (rng_.nextFloat() - 0.5) * 0.1, d.z + (rng_.nextFloat() - 0.5) * 0.15) * 0.3;
  }
  fireballs_.push_back(f);
}

// --- IA ----------------------------------------------------------------------------------------

bool GameSession::teleportMob(Mob& m, const glm::dvec3& near, double range) {
  World& w = access_.world();
  for (int tries = 0; tries < 24; tries++) {
    const double ang = rng_.nextFloat() * 2 * kPi, dist = rng_.nextFloat() * range;
    const int x = static_cast<int>(std::floor(near.x + std::cos(ang) * dist));
    const int z = static_cast<int>(std::floor(near.z + std::sin(ang) * dist));
    const int y0 = static_cast<int>(std::floor(near.y)) + 8;
    for (int y = y0; y > y0 - 20; y--) {
      if (!blockInfo(stateId(w.block(x, y - 1, z))).opaqueCube || isFluid(stateId(w.block(x, y - 1, z)))) continue;
      if (!freeCell(w, x, y, z) || !freeCell(w, x, y + 1, z) || !freeCell(w, x, y + 2, z)) break;
      SessionEvent e{SessionEvent::Type::Teleport, glm::ivec3(glm::floor(m.pos)), 0};
      e.where = m.pos;
      e.mob = m.type;
      events_.push_back(e);
      m.pos = m.prevPos = {x + 0.5, static_cast<double>(y), z + 0.5};
      m.motion = {0, 0, 0};
      SessionEvent e2 = e;
      e2.where = m.pos;
      events_.push_back(e2);
      return true;
    }
  }
  return false;
}

namespace {
/// ¿Mira el jugador a la criatura (a menos de 64 bloques y en línea recta hacia su cabeza)?
bool lookedAtBy(const Player& p, const Mob& m) {
  if (p.dead || p.creative()) return false;
  if (p.inventory.armor(3).id == B::pumpkin) return false;  // con una calabaza puesta no los enfada
  const glm::dvec3 eye = p.eyePos();
  const glm::dvec3 look(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  glm::dvec3 d = m.eyePos() - eye;
  const double len = glm::length(d);
  if (len > 64 || len < 1e-3) return false;
  d /= len;
  return glm::dot(look, d) > 1.0 - 0.025 / len;
}
}  // namespace

bool GameSession::mobAIExtra(Mob& m) {
  World& w = access_.world();
  const MobInfo& info = m.info();
  const glm::dvec3 eye = m.eyePos();
  auto lookAt = [&](const Player& who, float step) {
    const glm::dvec3 pe = who.eyePos();
    m.headYaw = approach(m.headYaw, yawTo(eye, pe), step);
    const float rel = std::clamp(wrap(m.headYaw - m.yaw), -1.3f, 1.3f);
    m.headYaw = m.yaw + rel;
    m.pitch = std::clamp(static_cast<float>(std::atan2(pe.y - eye.y, std::hypot(pe.x - eye.x, pe.z - eye.z))), -1.2f, 1.2f);
  };
  auto relax = [&] {
    m.headYaw = approach(m.headYaw, m.yaw, 0.3f);
    m.pitch += (0.0f - m.pitch) * 0.2f;
  };
  auto melee = [&](Player& tgt, float reachMul, float damage, int cooldown) {
    const glm::dvec3 d = tgt.pos - m.pos;
    const double reach = info.width * reachMul;
    if (d.x * d.x + d.z * d.z <= reach * reach + Player::kWidth && std::abs(d.y) < 2.0 && m.attackCooldown == 0) {
      damagePlayer(tgt, damage, m.pos, 0.4f, DamageKind::Melee, &m);
      m.attackCooldown = cooldown;
    }
  };

  switch (m.type) {
    case MobType::PigZombie: {
      if (m.anger > 0) m.anger--;
      Player* tgt = m.anger > 0 ? nearestPlayer(m.pos, true) : nullptr;
      if (!tgt) {
        relax();
        wander(m, 120, 1.0f);
        return true;
      }
      lookAt(*tgt, 0.35f);
      walkTowards(m, tgt->pos, 1.45f);
      melee(*tgt, 2.0f, info.attackDamage, 20);
      return true;
    }
    case MobType::Ghast: {
      Player* tgt = nearestPlayer(m.pos, true);
      const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
      const bool see = tgt && dist < 64.0 && canSeePlayer(m, *tgt);
      // Rumbo: un punto cercano al azar que se renueva cada poco
      if (!m.walkTarget || m.age % 60 == 0 || glm::length(*m.walkTarget - m.pos) < 3.0) {
        for (int tries = 0; tries < 8; tries++) {
          const glm::dvec3 c = m.pos + glm::dvec3((rng_.nextFloat() * 2 - 1) * 16.0, (rng_.nextFloat() * 2 - 1) * 8.0, (rng_.nextFloat() * 2 - 1) * 16.0);
          const int x = static_cast<int>(std::floor(c.x)), y = static_cast<int>(std::floor(c.y)), z = static_cast<int>(std::floor(c.z));
          if (y > 8 && y < 120 && freeCell(w, x, y, z)) {
            m.walkTarget = c;
            break;
          }
        }
      }
      if (m.walkTarget) {
        glm::dvec3 d = *m.walkTarget - m.pos;
        const double len = std::max(1e-3, glm::length(d));
        m.motion += d / len * 0.06;
      }
      if (see) {
        m.yaw = approach(m.yaw, yawTo(m.pos, tgt->pos), 0.2f);
        m.headYaw = m.yaw;
        if (++m.shootTicks == 20) {
          launchFireball(m, tgt->eyePos() - glm::dvec3(0, 0.5, 0), true);
          m.shootTicks = -40;
        }
      } else {
        m.shootTicks = std::max(0, m.shootTicks - 2);
        if (std::abs(m.motion.x) + std::abs(m.motion.z) > 0.02) m.yaw = approach(m.yaw, std::atan2(static_cast<float>(-m.motion.x), static_cast<float>(-m.motion.z)), 0.1f);
      }
      m.headYaw = m.yaw;
      return true;
    }
    case MobType::Blaze: {
      if (m.inWater && m.age % 10 == 0) hurtMob(m, 1.0f, m.pos, 0.0f, false);
      Player* tgt = nearestPlayer(m.pos, true);
      const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
      const bool see = tgt && dist < 48.0 && canSeePlayer(m, *tgt);
      if (!see) {
        m.chasing = false;
        m.shots = 0;
        m.shootTicks = 0;
        relax();
        wander(m, 120, 1.0f);
        // flota: si cae, frena
        return true;
      }
      m.chasing = true;
      m.walkTarget.reset();
      lookAt(*tgt, 0.4f);
      m.yaw = approach(m.yaw, yawTo(m.pos, tgt->pos), 0.4f);
      if (dist > 8.0) walkTowards(m, tgt->pos, 1.0f);
      // Se mantiene a la altura del jugador
      if (tgt->pos.y + 0.5 > m.pos.y + 1.0 && m.motion.y < 0.3) m.motion.y += 0.06;
      if (dist < 3.0) {
        melee(*tgt, 2.5f, info.attackDamage, 20);
      } else {
        // Ráfaga de tres bolas pequeñas cada pocos segundos
        if (m.shots > 0) {
          if (m.age % 6 == 0) {
            launchFireball(m, tgt->eyePos(), false);
            if (--m.shots == 0) m.shootTicks = 0;
          }
        } else if (++m.shootTicks >= 100) {
          m.shots = 3;
        }
      }
      return true;
    }
    case MobType::Slime:
    case MobType::MagmaCube: {
      if (m.jumpDelay > 0) m.jumpDelay--;
      Player* tgt = nearestPlayer(m.pos, true);
      const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
      const bool chase = tgt && dist < 16.0 && canSeePlayer(m, *tgt);
      m.moveForward = 0;
      m.wantJump = false;
      if (m.onGround && m.jumpDelay <= 0) {
        m.jumpDelay = chase ? 10 + rng_.nextInt(10) : 20 + rng_.nextInt(40);
        if (chase) m.yaw = yawTo(m.pos, tgt->pos);
        else if (rng_.nextInt(3) == 0) m.yaw += (rng_.nextFloat() - 0.5f) * 2.5f;
        else m.jumpDelay += 20;  // se queda quieta un rato más
        if (chase || m.jumpDelay < 40) {
          const glm::dvec3 f = forward(m.yaw);
          const double speed = 0.18 + 0.06 * std::min<int>(m.size, 3);
          m.motion = {f.x * speed, 0.42 + (m.type == MobType::MagmaCube ? 0.05 * m.size : 0.0), f.z * speed};
        }
      }
      m.headYaw = m.yaw;
      if (chase) {
        const double reach = 0.6 * m.size + Player::kWidth * 0.5;
        const glm::dvec3 d = tgt->pos - m.pos;
        const float dmg = m.type == MobType::MagmaCube ? 2.0f + m.size : (m.size > 1 ? static_cast<float>(m.size) : 0.0f);
        if (dmg > 0 && d.x * d.x + d.z * d.z < reach * reach && d.y > -1.0 && d.y < 0.6 * m.size + 1.0 && m.attackCooldown == 0) {
          damagePlayer(*tgt, dmg, m.pos, 0.4f, DamageKind::Melee, &m);
          m.attackCooldown = 20;
        }
      }
      return true;
    }
    case MobType::Enderman: {
      if (m.anger > 0) m.anger--;
      if (m.teleportCd > 0) m.teleportCd--;
      if (m.inWater) {
        if (m.age % 5 == 0) hurtMob(m, 1.0f, m.pos, 0.0f, false);
        if (m.teleportCd == 0 && teleportMob(m, m.pos, 24.0)) m.teleportCd = 20;
      }
      // Si algún jugador lo mira, se enfada
      for (Player* p : activePlayers())
        if (lookedAtBy(*p, m)) {
          m.anger = std::max(m.anger, 600);
          m.walkTarget.reset();
        }
      Player* tgt = m.anger > 0 ? nearestPlayer(m.pos, true) : nullptr;
      if (!tgt) {
        relax();
        // Tranquilo: pasea, de vez en cuando se teletransporta y mueve bloques de sitio
        if (m.teleportCd == 0 && rng_.nextInt(400) == 0 && teleportMob(m, m.pos, 16.0)) m.teleportCd = 40;
        const int ex = static_cast<int>(std::floor(m.pos.x)), ey = static_cast<int>(std::floor(m.pos.y)), ez = static_cast<int>(std::floor(m.pos.z));
        if (m.carried == 0 && rng_.nextInt(20) == 0) {
          const int px = ex + rng_.nextInt(5) - 2, py = ey + rng_.nextInt(3), pz = ez + rng_.nextInt(5) - 2;
          const int id = stateId(w.block(px, py, pz));
          const bool pickable = id == B::grass || id == B::dirt || id == B::sand || id == B::gravel || id == B::yellow_flower || id == B::red_flower ||
                                id == B::brown_mushroom || id == B::red_mushroom || id == B::tnt || id == B::cactus || id == B::clay ||
                                id == B::pumpkin || id == B::melon_block || id == B::mycelium;
          if (pickable) {
            m.carried = static_cast<int>(w.block(px, py, pz));
            setAndUpdate(px, py, pz, 0);
          }
        } else if (m.carried != 0 && rng_.nextInt(2000) == 0) {
          const int px = ex + rng_.nextInt(5) - 2, py = ey + rng_.nextInt(3), pz = ez + rng_.nextInt(5) - 2;
          if (w.block(px, py, pz) == 0 && blockInfo(stateId(w.block(px, py - 1, pz))).opaqueCube &&
              canStay(w, px, py, pz, static_cast<BlockState>(m.carried))) {
            setAndUpdate(px, py, pz, static_cast<BlockState>(m.carried));
            m.carried = 0;
          }
        }
        wander(m, 160, 1.0f);
        return true;
      }
      lookAt(*tgt, 0.5f);
      const double dist = glm::length(tgt->pos - m.pos);
      if (dist > 2.0) walkTowards(m, tgt->pos, 2.8f);
      if (dist > 5.0 && m.teleportCd == 0 && rng_.nextInt(40) == 0 && teleportMob(m, tgt->pos, 4.0)) m.teleportCd = 30;
      melee(*tgt, 2.0f, info.attackDamage, 20);
      return true;
    }
    case MobType::EnderDragon: dragonAI(m); return true;
    default: return false;
  }
}

// --- El dragón del End -----------------------------------------------------------------------------------

void GameSession::dragonAI(Mob& m) {
  World& w = access_.world();
  if (m.dying()) {
    // Agoniza en el sitio, dando vueltas, y al final explota en luz
    m.motion *= 0.9;
    m.yaw += 0.05f;
    if (m.deathTime % 20 == 0) {
      SessionEvent e{SessionEvent::Type::Explosion, glm::ivec3(glm::floor(m.pos)), 0};
      e.where = m.pos + glm::dvec3((rng_.nextFloat() - 0.5) * 12, 3 + (rng_.nextFloat() - 0.5) * 4, (rng_.nextFloat() - 0.5) * 12);
      events_.push_back(e);
    }
    return;
  }
  // Los cristales le devuelven vida
  if (m.age % 10 == 0 && m.health < m.info().maxHealth)
    for (const EndCrystal& c : crystals_)
      if (c.alive && glm::length(c.pos - m.pos) < 40.0) {
        m.health = std::min(m.info().maxHealth, m.health + 1.0f);
        break;
      }
  Player* tgt = nearestPlayer(m.pos, true);
  const double pdist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
  m.phaseTicks++;
  glm::dvec3 target;
  double speed = 0.5;
  if (m.phase == 0) {
    const double a = m.age * 0.013;
    target = glm::dvec3(std::cos(a) * 45.0, 72.0 + std::sin(m.age * 0.03) * 10.0, std::sin(a) * 45.0);
    if (tgt && m.phaseTicks > 300 && pdist < 100.0 && rng_.nextInt(120) == 0) {
      m.phase = 1;
      m.phaseTicks = 0;
    }
  } else {
    if (!tgt || m.phaseTicks > 140 || pdist < 6.0) {
      m.phase = 0;
      m.phaseTicks = 0;
      target = m.pos;
    } else {
      target = tgt->pos + glm::dvec3(0, 1.0, 0);
      speed = 0.95;
    }
  }
  const glm::dvec3 here = m.pos + glm::dvec3(0, m.info().height * 0.5, 0);
  glm::dvec3 d = target - here;
  const double len = std::max(1e-3, glm::length(d));
  m.motion += (d / len * speed - m.motion) * 0.06;
  const double horiz = std::hypot(m.motion.x, m.motion.z);
  if (horiz > 0.05) m.yaw = approach(m.yaw, std::atan2(static_cast<float>(-m.motion.x), static_cast<float>(-m.motion.z)), 0.08f);
  m.headYaw = m.yaw;
  m.pitch = std::clamp(static_cast<float>(std::atan2(m.motion.y, std::max(horiz, 0.2))), -0.7f, 0.7f);

  // Rompe lo que toca (menos obsidiana, roca madre y piedra del End)
  const AABB b = m.box();
  int broken = 0;
  access_.beginBatch();
  for (int y = static_cast<int>(std::floor(b.min.y)); y <= static_cast<int>(std::floor(b.max.y)); y++)
    for (int z = static_cast<int>(std::floor(b.min.z)); z <= static_cast<int>(std::floor(b.max.z)); z++)
      for (int x = static_cast<int>(std::floor(b.min.x)); x <= static_cast<int>(std::floor(b.max.x)); x++) {
        const int id = stateId(w.block(x, y, z));
        if (id == 0 || id == B::bedrock || id == B::obsidian || id == B::end_stone || id == 119) continue;
        // (solo la parte central: unas alas anchas no tallan una pared)
        if (std::abs(x + 0.5 - m.pos.x) > 6.0 || std::abs(z + 0.5 - m.pos.z) > 6.0) continue;
        setWorldBlock(x, y, z, 0);
        broken++;
      }
  access_.endBatch();
  if (broken > 0 && m.age % 8 == 0) {
    SessionEvent e{SessionEvent::Type::Explosion, glm::ivec3(glm::floor(m.pos)), 0};
    e.where = m.pos;
    events_.push_back(e);
  }
  // Golpea a quien toca
  if (m.attackCooldown == 0) {
    for (Player* p : activePlayers()) {
      if (p->dead || p->creative()) continue;
      if (b.expand({1.0, 0.5, 1.0}).intersects(p->box())) {
        damagePlayer(*p, m.info().attackDamage, m.pos, 1.5f, DamageKind::Melee, &m);
        m.attackCooldown = 20;
      }
    }
  }
}

void GameSession::dragonDefeated(const glm::dvec3& at) {
  World& w = access_.world();
  const bool first = !dragonKilled_;
  dragonKilled_ = true;
  dragonHealth_ = 0;
  spawnXp(at, first ? 12000 : 500);
  award(Ach::TheEnd2);
  // La salida: un pozo de roca madre con el portal de vuelta (y el huevo de dragón la primera vez)
  int y0 = 62;
  for (int y = 100; y > 40; y--)
    if (blockInfo(stateId(w.block(0, y, 0))).opaqueCube) {
      y0 = y + 1;
      break;
    }
  for (int dz = -3; dz <= 3; dz++)
    for (int dx = -3; dx <= 3; dx++) {
      const double r = std::sqrt(double(dx * dx + dz * dz));
      if (r > 3.2) continue;
      for (int y = y0; y <= y0 + 6; y++) setWorldBlock(dx, y, dz, 0);
      if (r <= 1.6) {
        setWorldBlock(dx, y0, dz, makeState(119));
      } else {
        setWorldBlock(dx, y0 - 1, dz, makeState(B::bedrock));
        setWorldBlock(dx, y0, dz, makeState(B::bedrock));
        if (r > 2.4) setWorldBlock(dx, y0 + 1, dz, makeState(B::bedrock));
      }
    }
  for (int y = y0; y <= y0 + 3; y++) setWorldBlock(0, y, 0, makeState(B::bedrock));
  if (first) setWorldBlock(0, y0 + 4, 0, makeState(122));
  setWorldBlock(1, y0 + 2, 0, makeState(B::torch, 1));
  setWorldBlock(-1, y0 + 2, 0, makeState(B::torch, 2));
  setWorldBlock(0, y0 + 2, 1, makeState(B::torch, 3));
  setWorldBlock(0, y0 + 2, -1, makeState(B::torch, 4));
}

void GameSession::onMobHurtExtra(Mob& m, bool byPlayer, const glm::dvec3& from) {
  if (m.type == MobType::PigZombie && byPlayer) {
    // Se enfada con quien le pega, y avisa a los de su clase en 32 bloques
    m.anger = 400 + rng_.nextInt(400);
    for (Mob& o : mobs_)
      if (o.type == MobType::PigZombie && !o.dying() && glm::length(o.pos - m.pos) < 32.0) o.anger = std::max(o.anger, 400 + rng_.nextInt(400));
  } else if (m.type == MobType::Enderman) {
    if (byPlayer) m.anger = 600;
    if (m.teleportCd == 0 && teleportMob(m, m.pos, 16.0)) m.teleportCd = 20;
  }
}

void GameSession::onMobKilled(Mob& m, bool byPlayer) {
  if (m.type == MobType::Enderman && m.carried != 0) {
    for (const ItemStack& d : blockDrops(static_cast<BlockState>(m.carried), ItemStack(), rng_))
      spawnItem(m.pos + glm::dvec3(0, 1, 0), d, {0, 0.2, 0}, 10);
    m.carried = 0;
  }
  if (m.isSlimeLike() && m.size > 1) {
    // Se divide en 2 a 4 más pequeñas (se copia lo necesario: al añadir criaturas `m` puede dejar de ser válida)
    const MobType type = m.type;
    const int size = m.size;
    const glm::dvec3 base = m.pos;
    const int n = 2 + rng_.nextInt(3);
    for (int i = 0; i < n; i++) {
      const glm::dvec3 at = base + glm::dvec3(((i % 2) - 0.5) * size * 0.5, 0.5, ((i / 2) - 0.5) * size * 0.5);
      if (Mob* c = spawnSized(type, at, size / 2)) c->yaw = rng_.nextFloat() * 2 * kPi - kPi;
    }
  }
  (void)byPlayer;
}

void GameSession::mobDropsExtra(const Mob& m, int looting) {
  auto drop = [&](int id, int count, int meta = 0) {
    if (count <= 0) return;
    spawnItem(m.pos + glm::dvec3(0, 0.5, 0), ItemStack(id, count, meta), {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  };
  const int L = std::max(0, looting);
  switch (m.type) {
    case MobType::PigZombie:
      drop(ItemId::rotten_flesh, rng_.nextInt(2 + L));
      drop(ItemId::gold_nugget, rng_.nextInt(2 + L));
      if (rng_.nextInt(40) - 2 * L < 1) drop(ItemId::gold_ingot, 1);
      break;
    case MobType::Ghast:
      drop(ItemId::ghast_tear, rng_.nextInt(2 + L));
      drop(ItemId::gunpowder, rng_.nextInt(3 + L));
      break;
    case MobType::Blaze:
      drop(ItemId::blaze_rod, rng_.nextInt(2 + L));
      break;
    case MobType::MagmaCube:
      if (m.size > 1 && rng_.nextInt(4) - L <= 0) drop(ItemId::magma_cream, 1 + rng_.nextInt(1 + L));
      break;
    case MobType::Slime:
      if (m.size == 1) drop(ItemId::slime_ball, rng_.nextInt(3 + L));
      break;
    case MobType::Enderman:
      drop(ItemId::ender_pearl, rng_.nextInt(2 + L));
      break;
    case MobType::CaveSpider:
      drop(ItemId::string, rng_.nextInt(3 + L));
      if (rng_.nextInt(3) == 0 || rng_.nextInt(1 + L) > 0) drop(ItemId::spider_eye, 1);
      break;
    case MobType::WitherSkeleton:
      drop(ItemId::coal, rng_.nextInt(2 + L));
      drop(ItemId::bone, rng_.nextInt(3 + L));
      if (rng_.nextInt(40) < 1 + L) drop(ItemId::skull, 1, 1);
      break;
    default: break;
  }
}

// --- Bolas de fuego -------------------------------------------------------------------------------

std::vector<glm::dvec3> GameSession::crystalsInChunk(int cx, int cz, bool take) {
  std::vector<glm::dvec3> out;
  auto inside = [&](const EndCrystal& c) {
    return (static_cast<int>(std::floor(c.pos.x)) >> 4) == cx && (static_cast<int>(std::floor(c.pos.z)) >> 4) == cz;
  };
  for (const EndCrystal& c : crystals_)
    if (c.alive && inside(c)) out.push_back(c.pos);
  if (take) std::erase_if(crystals_, inside);
  return out;
}

bool GameSession::punchFireball() {
  // Un cristal del End se rompe de un golpe y explota
  {
    const glm::dvec3 eye = player_.eyePos();
    const glm::dvec3 look(-std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), -std::cos(player_.yaw) * std::cos(player_.pitch));
    for (std::size_t i = 0; i < crystals_.size(); i++) {
      const glm::dvec3 d = crystals_[i].pos + glm::dvec3(0, 1.0, 0) - eye;
      const double t = glm::dot(d, look);
      if (t < 0 || t > 5.0 || glm::length(d - look * t) > 0.9) continue;
      const glm::dvec3 at = crystals_[i].pos + glm::dvec3(0, 1.0, 0);
      crystals_.erase(crystals_.begin() + static_cast<std::ptrdiff_t>(i));
      explode(at, 6.0f);
      return true;
    }
  }
  const glm::dvec3 eye = player_.eyePos();
  const glm::dvec3 look(-std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), -std::cos(player_.yaw) * std::cos(player_.pitch));
  Fireball* best = nullptr;
  double bestT = 4.5;
  for (Fireball& f : fireballs_) {
    const glm::dvec3 d = f.pos - eye;
    const double t = glm::dot(d, look);
    if (t < 0 || t > bestT) continue;
    const double miss = glm::length(d - look * t);
    if (miss < (f.large ? 0.8 : 0.3)) {
      best = &f;
      bestT = t;
    }
  }
  if (!best) return false;
  best->motion = look * 0.4;
  best->accel = look * 0.1;
  best->owner = 0;
  best->fromPlayer = true;
  return true;
}

void GameSession::tickFireballs() {
  World& w = access_.world();
  for (Fireball& f : fireballs_) {
    f.prevPos = f.pos;
    f.life++;
    glm::dvec3 step = f.motion;
    const double len = glm::length(step);
    bool boom = false;
    glm::dvec3 hitAt = f.pos;
    Mob* hitMob = nullptr;
    Player* hitPlayer = nullptr;
    if (len > 1e-6) {
      if (const auto hit = raycastBlocks(w, f.pos, step / len, len)) {
        boom = true;
        hitAt = hit->point;
        // Una bola pequeña enciende el hueco de delante
        if (!f.large) {
          const glm::ivec3 n(kFaceNormals[hit->face][0], kFaceNormals[hit->face][1], kFaceNormals[hit->face][2]);
          const glm::ivec3 at = hit->block + n;
          if (w.block(at.x, at.y, at.z) == 0) fireIgnite(at, 0);
        }
      }
    }
    if (!boom) {
      const AABB box = AABB::centered(f.pos + step * 0.5, f.large ? 1.0 : 0.3, f.large ? 1.0 : 0.3);
      for (Mob& m : mobs_) {
        if (m.dying() || m.id == f.owner) continue;
        if (m.box().expand({0.2, 0.2, 0.2}).intersects(box)) {
          hitMob = &m;
          break;
        }
      }
      if (!hitMob)
        for (Player* p : activePlayers())
          if (!p->dead && p->box().intersects(box)) {
            hitPlayer = p;
            break;
          }
      if (hitMob || hitPlayer) boom = true;
    }
    if (boom) {
      if (f.large) {
        if (hitMob) {
          const bool kills = f.fromPlayer && hitMob->type == MobType::Ghast;
          hurtMob(*hitMob, kills ? 1000.0f : 6.0f, hitAt, 0.4f, f.fromPlayer);
          if (kills && hitMob->dying()) award(Ach::Ghast);
        }
        explode(hitAt, 1.0f, true);
      } else {
        if (hitPlayer) {
          damagePlayer(*hitPlayer, 5.0f, f.pos, 0.3f, DamageKind::Generic);
          hitPlayer->fireTicks = std::max(hitPlayer->fireTicks, 100);
        } else if (hitMob) {
          hurtMob(*hitMob, 5.0f, f.pos, 0.3f, f.fromPlayer);
          hitMob->fireTicks = std::max(hitMob->fireTicks, 100);
        }
      }
      f.life = 100000;
      continue;
    }
    f.pos += step;
    f.motion += f.accel;
    f.motion *= 0.95;
    if (!f.large) f.motion = f.motion / 0.95 * 0.99;  // las pequeñas casi no frenan
  }
  std::erase_if(fireballs_, [](const Fireball& f) { return f.life > 600 || f.pos.y < -64 || f.pos.y > 400; });
}

// --- Generadores de monstruos y baldosas de generación --------------------------------------------------

std::vector<std::pair<glm::ivec3, GameSession::SpawnerState>> GameSession::spawnersInChunk(int cx, int cz, bool take) {
  std::vector<std::pair<glm::ivec3, SpawnerState>> out;
  for (auto it = spawners_.begin(); it != spawners_.end();) {
    const auto [x, y, z] = it->first;
    if ((x >> 4) == cx && (z >> 4) == cz) {
      out.emplace_back(glm::ivec3(x, y, z), it->second);
      if (take) {
        it = spawners_.erase(it);
        continue;
      }
    }
    ++it;
  }
  return out;
}

void GameSession::tickSpawners() {
  if (spawners_.empty() || !rules_.mobSpawning || rules_.difficulty == 0) return;
  World& w = access_.world();
  for (auto& [key, st] : spawners_) {
    const auto [x, y, z] = key;
    if (stateId(w.block(x, y, z)) != 52) continue;
    const glm::dvec3 c(x + 0.5, y + 0.5, z + 0.5);
    if (nearestPlayerDistance(c) > 16.0) continue;
    if (--st.delay > 0) continue;
    st.delay = 200 + rng_.nextInt(600);
    const auto type = mobFromEntityId(st.entityId);
    if (!type) continue;
    int nearby = 0;
    for (const Mob& m : mobs_)
      if (m.type == *type && glm::length(m.pos - c) < 8.0) nearby++;
    if (nearby >= 6) continue;
    for (int i = 0; i < 4; i++) {
      const double px = x + (rng_.nextFloat() * 2 - 1) * 4.0 + 0.5, pz = z + (rng_.nextFloat() * 2 - 1) * 4.0 + 0.5;
      const int py = y + rng_.nextInt(3) - 1;
      const int bx = static_cast<int>(std::floor(px)), bz = static_cast<int>(std::floor(pz));
      if (!freeCell(w, bx, py, bz) || !freeCell(w, bx, py + 1, bz) || !blockInfo(stateId(w.block(bx, py - 1, bz))).opaqueCube) continue;
      if (Mob* m = spawnMob(*type, {px, static_cast<double>(py), pz})) m->persistent = true;
    }
  }
}

void GameSession::applyGenTiles(int cx, int cz) {
  const Chunk* c = access_.world().chunk(cx, cz);
  if (!c) return;
  for (const GenTile& t : c->genTiles()) {
    const glm::ivec3 p(cx * 16 + t.x, t.y, cz * 16 + t.z);
    switch (t.kind) {
      case GenTile::LootChest: {
        Random rng(cellSeed(seed_, p.x, p.z, 0x100700 + static_cast<u32>(p.y)));
        ChestState& chest = chests_[{p.x, p.y, p.z}];
        fillLoot(chest, t.param, rng);
        break;
      }
      case GenTile::Spawner: {
        SpawnerState st;
        st.entityId = t.param;
        st.delay = 100 + rng_.nextInt(100);
        spawners_[{p.x, p.y, p.z}] = st;
        break;
      }
      case GenTile::FluidTick: schedule(p, 30, TickKind::Fluid); break;
      case GenTile::EndCrystal: addCrystal(glm::dvec3(p) + glm::dvec3(0.5, 0.0, 0.5)); break;
      default: break;
    }
  }
}

// --- Aparición fuera del mundo normal ------------------------------------------------------------------

void GameSession::spawnNonOverworld() {
  if (!rules_.mobSpawning || rules_.difficulty == 0) return;
  World& w = access_.world();
  const std::vector<Player*> players = activePlayers();
  if (players.empty()) return;
  const int cap = dimension_ == -1 ? 40 : 18;
  int hostiles = static_cast<int>(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.info().hostile; }));
  if (hostiles >= cap) return;
  const Player& center = *players[players.size() == 1 ? 0 : rng_.nextInt(static_cast<int>(players.size()))];
  for (int attempt = 0; attempt < 6; attempt++) {
    const double ang = rng_.nextFloat() * 2 * kPi, dist = 24.0 + rng_.nextFloat() * 36.0;
    const int x = static_cast<int>(std::floor(center.pos.x + std::cos(ang) * dist));
    const int z = static_cast<int>(std::floor(center.pos.z + std::sin(ang) * dist));
    const Chunk* c = w.chunkAt(x, z);
    if (!c) continue;
    const int y = dimension_ == -1 ? 8 + rng_.nextInt(112) : 40 + rng_.nextInt(40);
    const int below = stateId(w.block(x, y - 1, z));
    if (!blockInfo(below).opaqueCube || isFluid(below)) continue;
    if (!freeCell(w, x, y, z) || !freeCell(w, x, y + 1, z)) continue;
    if (w.blockLight(x, y, z) > rng_.nextInt(12)) continue;
    MobType type;
    int pack = 1;
    if (dimension_ == 1) {
      type = MobType::Enderman;
      pack = 1 + rng_.nextInt(4);
    } else {
      const int roll = rng_.nextInt(100);
      if (below == B::nether_brick) {  // en el fortín: blaze, esqueletos atrofiados y cerdos zombi
        type = roll < 40 ? MobType::Blaze : roll < 75 ? MobType::WitherSkeleton : MobType::PigZombie;
        pack = type == MobType::PigZombie ? 2 : 1;
      } else if (roll < 55) {
        type = MobType::PigZombie;
        pack = 1 + rng_.nextInt(4);
      } else if (roll < 62) {
        // El ghast necesita un hueco libre de 4x4x4 (y flota: se pone en el aire)
        bool room = true;
        for (int dx = -2; dx <= 2 && room; dx++)
          for (int dy = 0; dy <= 4 && room; dy++)
            for (int dz = -2; dz <= 2 && room; dz++) room = freeCell(w, x + dx, y + dy, z + dz);
        if (!room) continue;
        type = MobType::Ghast;
      } else if (roll < 90) {
        type = MobType::MagmaCube;
        pack = 1 + rng_.nextInt(4);
      } else {
        type = MobType::Enderman;
      }
    }
    for (int i = 0; i < pack; i++) {
      const int px = x + rng_.nextInt(5) - 2, pz = z + rng_.nextInt(5) - 2;
      if (i > 0 && (!blockInfo(stateId(w.block(px, y - 1, pz))).opaqueCube || !freeCell(w, px, y, pz) || !freeCell(w, px, y + 1, pz))) continue;
      const glm::dvec3 at{px + 0.5, static_cast<double>(y) + (type == MobType::Ghast ? 3.0 : 0.0), pz + 0.5};
      if (nearestPlayerDistance(at) < 24.0) continue;
      if (type == MobType::MagmaCube || type == MobType::Slime) spawnSized(type, at, 1 << rng_.nextInt(3));
      else spawnMob(type, at);
      if (++hostiles >= cap) return;
    }
  }
}

}  // namespace mcw
