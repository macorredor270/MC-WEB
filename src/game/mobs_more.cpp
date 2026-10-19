// Criaturas de más de 1.8: calamar, murciélago, conejo, champiñaca, lobo, ocelote, caballo, aldeano, gólems de hierro y de
// nieve, bruja y endermite. Los números y reglas son los de 1.8 (minecraft.wiki, dificultad normal); la implementación es
// propia.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "data/biomes.h"
#include "data/items.h"
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
float approach(float from, float to, float maxStep) { return from + std::clamp(wrap(to - from), -maxStep, maxStep); }
float yawTo(const glm::dvec3& from, const glm::dvec3& to) { return std::atan2(static_cast<float>(-(to.x - from.x)), static_cast<float>(-(to.z - from.z))); }
glm::dvec3 forwardOf(float yaw) { return {-std::sin(yaw), 0.0, -std::cos(yaw)}; }

bool freeCell(const World& w, int x, int y, int z) {
  const int id = stateId(w.block(x, y, z));
  if (isFluid(id)) return false;
  return id == B::air || blockCollision(w, x, y, z).empty();
}

bool isMeat(int id) {
  return id == ItemId::porkchop || id == ItemId::cooked_porkchop || id == ItemId::beef || id == ItemId::cooked_beef || id == ItemId::chicken ||
         id == ItemId::cooked_chicken || id == ItemId::mutton || id == ItemId::cooked_mutton || id == ItemId::rotten_flesh ||
         id == ItemId::rabbit || id == ItemId::cooked_rabbit;
}
bool isFish(int id) { return id == ItemId::fish || id == ItemId::cooked_fish; }

/// El conejo que toca según el bioma (0 marrón, 1 blanco, 2 negro, 3 blanco y negro, 4 dorado, 5 sal y pimienta).
int rabbitCoat(int biome, Random& r) {
  if (biome == Biome::desert) return 4;
  if (biome == Biome::ice_plains || biome == Biome::cold_taiga) return r.nextInt(2) ? 1 : 3;  // (llega ya el bioma de base)
  const int k = r.nextInt(10);
  return k < 5 ? 0 : k < 8 ? 5 : (k < 9 ? 2 : 1);
}

}  // namespace

Player* GameSession::ownerOf(const Mob& m) {
  if (!m.tamed) return nullptr;
  for (Player* p : activePlayers())
    if (p->uid == m.ownerUid) return p;
  return nullptr;
}

bool GameSession::breedingFood(const Mob& m, const ItemStack& s) const {
  if (s.empty()) return false;
  switch (m.type) {
    case MobType::Wolf: return m.tamed && isMeat(s.id);
    case MobType::Ocelot: return m.tamed && isFish(s.id);
    case MobType::Rabbit: return s.id == ItemId::carrot || s.id == ItemId::golden_carrot || (s.id == B::yellow_flower && s.meta == 0);
    case MobType::Horse: return m.tamed && (s.id == ItemId::golden_apple || s.id == ItemId::golden_carrot);
    default: return s.id == breedingItem(m.type);
  }
}

void GameSession::mobStrikesMob(Mob& a, Mob& b, float damage) {
  hurtMob(b, damage, a.pos, 0.4f, false);
}

// --- Movimiento propio -------------------------------------------------------------------------

bool GameSession::moveMobMore(Mob& m) {
  World& w = access_.world();
  if (m.type == MobType::Squid) {
    AABB b = m.box();
    glm::dvec3 mv = m.motion;
    const MoveResult r = moveBox(w, b, mv, 0.0, m.onGround);
    m.pos = {b.center().x, b.min.y, b.center().z};
    m.collidedH = r.collidedX || r.collidedZ;
    if (r.collidedX) m.motion.x = 0;
    if (r.collidedZ) m.motion.z = 0;
    if (r.collidedY) m.motion.y = 0;
    m.onGround = r.onGround;
    if (m.inWater) {
      m.motion *= 0.97;
    } else {  // fuera del agua cae y se agita en el suelo
      m.motion.y = (m.motion.y - 0.08) * 0.98;
      m.motion.x *= 0.7;
      m.motion.z *= 0.7;
    }
    m.fallDistance = 0;
    m.limbAmount += (std::min(1.0f, static_cast<float>(std::hypot(m.pos.x - m.prevPos.x, m.pos.z - m.prevPos.z)) * 4.0f) - m.limbAmount) * 0.4f;
    return true;
  }
  if (m.type == MobType::Bat) {
    if (m.hanging) {
      m.motion = {0, 0, 0};
      return true;
    }
    AABB b = m.box();
    glm::dvec3 mv = m.motion;
    const MoveResult r = moveBox(w, b, mv, 0.0, false);
    m.pos = {b.center().x, b.min.y, b.center().z};
    if (r.collidedX) m.motion.x = 0;
    if (r.collidedY) m.motion.y = 0;
    if (r.collidedZ) m.motion.z = 0;
    m.collidedH = r.collidedX || r.collidedZ;
    m.onGround = false;
    m.fallDistance = 0;
    return true;
  }
  return false;
}

// --- IA -------------------------------------------------------------------------------------------

bool GameSession::mobAIMore(Mob& m) {
  World& w = access_.world();
  const glm::dvec3 eye = m.eyePos();
  auto lookAt = [&](const glm::dvec3& at) {
    m.headYaw = approach(m.headYaw, yawTo(eye, at), 0.35f);
    const float rel = std::clamp(wrap(m.headYaw - m.yaw), -1.3f, 1.3f);
    m.headYaw = m.yaw + rel;
    const double h = std::hypot(at.x - eye.x, at.z - eye.z);
    m.pitch = std::clamp(static_cast<float>(std::atan2(at.y - eye.y, h)), -1.2f, 1.2f);
  };
  auto relaxHead = [&] {
    m.headYaw = approach(m.headYaw, m.yaw, 0.3f);
    m.pitch += (0.0f - m.pitch) * 0.2f;
  };
  const int bx = static_cast<int>(std::floor(m.pos.x)), by = static_cast<int>(std::floor(m.pos.y)), bz = static_cast<int>(std::floor(m.pos.z));

  // Modo amor: buscan a otro igual que también lo esté y tienen una cría
  if (isBreedable(m.type) && m.type != MobType::Mooshroom && m.inLove > 0) {
    if (Mob* mate = findMate(m)) {
      m.headYaw = approach(m.headYaw, yawTo(m.pos, mate->pos), 0.35f);
      walkTowards(m, mate->pos, 1.0f);
      m.walkTarget.reset();
      const glm::dvec3 d = mate->pos - m.pos;
      if (++m.mateTicks >= 60 && d.x * d.x + d.y * d.y + d.z * d.z < 9.0) breedMobs(m, *mate);
      return true;
    }
  }
  switch (m.type) {
    case MobType::Squid: {
      if (++m.phaseTicks >= m.phase) {
        m.swimAngle = rng_.nextFloat() * 2.0f * kPi;
        m.swimSpeed = 0.06f + rng_.nextFloat() * 0.08f;
        m.phase = 40 + rng_.nextInt(80);
        m.phaseTicks = 0;
        m.motion.y = (rng_.nextFloat() - 0.5) * 0.1;
      }
      if (m.inWater) {
        const glm::dvec3 d = forwardOf(m.swimAngle);
        m.motion.x = d.x * m.swimSpeed;
        m.motion.z = d.z * m.swimSpeed;
        m.yaw = approach(m.yaw, m.swimAngle, 0.2f);
        if (m.collidedH) m.phaseTicks = m.phase;  // contra una pared: otro rumbo
        // No sale a la superficie: si encima no hay agua, baja
        const int top = by + 1 + static_cast<int>(m.info().height);
        if (!isWater(stateId(w.block(bx, top, bz)))) m.motion.y = std::min(m.motion.y, -0.02);
      }
      relaxHead();
      return true;
    }
    case MobType::Bat: {
      const Player* near = nearestPlayer(m.pos, false);
      const double pd = near ? glm::length(near->pos - m.pos) : 1e9;
      if (m.hanging) {
        const int above = stateId(w.block(bx, static_cast<int>(std::floor(m.pos.y + m.info().height + 0.1)), bz));
        if (!blockInfo(above).opaqueCube || (pd < 4.0 && rng_.nextInt(4) == 0) || rng_.nextInt(200) == 0) {
          m.hanging = false;
          m.motion.y = -0.2;
        }
        return true;
      }
      if (!m.walkTarget || glm::length(*m.walkTarget - m.pos) < 2.0 || rng_.nextInt(30) == 0) {
        m.walkTarget = glm::dvec3(m.pos.x + rng_.nextInt(15) - 7, m.pos.y + rng_.nextInt(5) - 2, m.pos.z + rng_.nextInt(15) - 7);
        if (m.walkTarget->y < 2) m.walkTarget->y = 2;
      }
      glm::dvec3 d = *m.walkTarget - m.pos;
      const double len = std::max(1e-3, glm::length(d));
      d /= len;
      m.motion += (d * 0.1 - m.motion) * 0.3;
      m.motion.y += (d.y > 0 ? 0.06 : -0.06) * 0.2;
      m.yaw = approach(m.yaw, std::atan2(static_cast<float>(-m.motion.x), static_cast<float>(-m.motion.z)), 0.3f);
      // A veces se cuelga de un techo de roca (y no si hay alguien cerca)
      const double ceil = std::floor(m.pos.y + m.info().height + 0.2);
      if (pd > 6.0 && rng_.nextInt(100) == 0 && blockInfo(stateId(w.block(bx, static_cast<int>(ceil), bz))).opaqueCube) {
        m.hanging = true;
        m.pos.y = ceil - m.info().height;
        m.motion = {0, 0, 0};
      }
      return true;
    }
    case MobType::Rabbit: {
      if (m.jumpDelay > 0) m.jumpDelay--;
      const bool killer = m.variant == 99;
      Player* tgt = killer ? nearestPlayer(m.pos, true) : nullptr;
      if (killer && tgt && glm::length(tgt->pos - m.pos) < 16.0) {
        m.chasing = true;
        lookAt(tgt->eyePos());
        if (m.onGround && m.jumpDelay == 0) {
          m.yaw = yawTo(m.pos, tgt->pos);
          const glm::dvec3 f = forwardOf(m.yaw);
          m.motion = {f.x * 0.25, 0.4, f.z * 0.25};
          m.jumpDelay = 6 + rng_.nextInt(6);
        }
        const glm::dvec3 d = tgt->pos - m.pos;
        if (d.x * d.x + d.z * d.z < 1.5 && std::abs(d.y) < 1.5 && m.attackCooldown == 0) {
          damagePlayer(*tgt, 8.0f, m.pos, 0.4f, DamageKind::Melee, &m);
          m.attackCooldown = 20;
        }
        m.moveForward = 0;
        return true;
      }
      // Huye de los jugadores (salvo si llevan una zanahoria)
      const Player* scary = nearestPlayer(m.pos, false);
      bool flee = scary && glm::length(scary->pos - m.pos) < 8.0 && scary->inventory.selected().id != ItemId::carrot &&
                  scary->inventory.selected().id != ItemId::golden_carrot;
      if (m.panicTicks > 0) {
        m.panicTicks--;
        flee = true;
      }
      if (!m.walkTarget && !flee && m.onGround && rng_.nextInt(80) == 0) {
        const int tx = bx + rng_.nextInt(11) - 5, tz = bz + rng_.nextInt(11) - 5;
        if (freeCell(w, tx, by, tz)) m.walkTarget = glm::dvec3(tx + 0.5, by, tz + 0.5);
      }
      if (flee && scary) {
        const glm::dvec3 away = m.pos - scary->pos;
        m.walkTarget = m.pos + glm::normalize(glm::dvec3(away.x, 0, away.z) + glm::dvec3(1e-3, 0, 1e-3)) * 5.0;
      }
      if (m.walkTarget) {
        const glm::dvec3 d = *m.walkTarget - m.pos;
        if (std::hypot(d.x, d.z) < 0.7) {
          m.walkTarget.reset();
        } else if (m.onGround && m.jumpDelay == 0) {
          m.yaw = yawTo(m.pos, *m.walkTarget);
          const glm::dvec3 f = forwardOf(m.yaw);
          const double sp = flee ? 0.28 : 0.17;
          m.motion = {f.x * sp, 0.36, f.z * sp};
          m.jumpDelay = flee ? 4 + rng_.nextInt(4) : 10 + rng_.nextInt(10);
        }
        if (m.collidedH && m.onGround) m.walkTarget.reset();
      }
      relaxHead();
      m.moveForward = 0;
      return true;
    }
    case MobType::Wolf:
    case MobType::Ocelot: {
      const bool wolf = m.type == MobType::Wolf;
      Player* owner = ownerOf(m);
      if (m.tamed && m.sitting) {
        m.moveForward = 0;
        m.walkTarget.reset();
        if (owner) lookAt(owner->eyePos());
        return true;
      }
      // Objetivo (una criatura) y su valor
      Mob* tmob = m.target ? mobById(static_cast<u32>(m.target)) : nullptr;
      if (tmob && (tmob->dying() || glm::length(tmob->pos - m.pos) > 24.0)) {
        m.target = 0;
        tmob = nullptr;
      }
      if (!tmob && m.age % 20 == 0 && rng_.nextInt(wolf ? 6 : 8) == 0) {
        // Salvajes: los lobos van a por ovejas y conejos; los ocelotes, a por gallinas. Los mansos defienden al dueño.
        double best = 144.0;
        for (Mob& o : mobs_) {
          if (&o == &m || o.dying()) continue;
          const bool prey = !m.tamed && (wolf ? (o.type == MobType::Sheep || o.type == MobType::Rabbit)
                                              : (o.type == MobType::Chicken || o.type == MobType::Rabbit));
          const bool foe = m.tamed && owner && o.info().hostile && o.type != MobType::Creeper && glm::length(o.pos - owner->pos) < 7.0 &&
                           (o.chasing || o.type == MobType::Skeleton);
          if (!prey && !foe) continue;
          const double d = glm::dot(o.pos - m.pos, o.pos - m.pos);
          if (d < best) {
            best = d;
            tmob = &o;
          }
        }
        if (tmob) m.target = static_cast<int>(tmob->id);
      }
      Player* enemy = (wolf && !m.tamed && m.angry) ? nearestPlayer(m.pos, true) : nullptr;
      if (enemy && glm::length(enemy->pos - m.pos) > 20.0) enemy = nullptr;
      if (tmob) {
        m.walkTarget.reset();
        lookAt(tmob->eyePos());
        walkTowards(m, tmob->pos, wolf ? 1.4f : 1.3f);
        const glm::dvec3 d = tmob->pos - m.pos;
        if (d.x * d.x + d.z * d.z < 2.0 && std::abs(d.y) < 1.5 && m.attackCooldown == 0) {
          mobStrikesMob(m, *tmob, wolf ? (m.tamed ? 4.0f : 2.0f) : 3.0f);
          m.attackCooldown = 20;
        }
        return true;
      }
      if (enemy) {
        m.walkTarget.reset();
        lookAt(enemy->eyePos());
        walkTowards(m, enemy->pos, 1.4f);
        const glm::dvec3 d = enemy->pos - m.pos;
        if (d.x * d.x + d.z * d.z < 2.0 && std::abs(d.y) < 1.5 && m.attackCooldown == 0) {
          damagePlayer(*enemy, 2.0f, m.pos, 0.4f, DamageKind::Melee, &m);
          m.attackCooldown = 20;
        }
        return true;
      }
      if (m.tamed && owner) {
        const double d = glm::length(owner->pos - m.pos);
        lookAt(owner->eyePos());
        if (d > 14.0 && !owner->mounted()) {  // se ha quedado muy atrás: aparece junto al dueño
          teleportMob(m, owner->pos, 3.0);
        } else if (d > 4.0) {
          walkTowards(m, owner->pos, 1.2f);
          m.walkTarget.reset();
        } else if (d < 2.0) {
          m.walkTarget.reset();
        } else {
          wander(m, 200, 0.8f);
        }
        return true;
      }
      if (m.tamed) {  // sin dueño cerca: pasea
        wander(m, 120, 1.0f);
        return true;
      }
      if (!wolf) {  // ocelote salvaje: se aparta de los jugadores
        const Player* scary = nearestPlayer(m.pos, false);
        const bool tempt = scary && isFish(scary->inventory.selected().id) && glm::length(scary->pos - m.pos) < 10.0;
        if (tempt) {
          lookAt(scary->eyePos());
          if (glm::length(scary->pos - m.pos) > 3.0) walkTowards(m, scary->pos, 0.9f);
          return true;
        }
        if (scary && glm::length(scary->pos - m.pos) < 10.0) {
          const glm::dvec3 away = m.pos - scary->pos;
          walkTowards(m, m.pos + glm::normalize(glm::dvec3(away.x, 0, away.z) + glm::dvec3(1e-3, 0, 1e-3)) * 6.0, 1.3f);
          relaxHead();
          return true;
        }
      }
      relaxHead();
      wander(m, 120, 1.0f);
      return true;
    }
    case MobType::Villager: {
      if (m.tradingWith != 0) {  // comerciando: se queda quieto mirando al cliente
        m.moveForward = 0;
        m.walkTarget.reset();
        if (const Player* p = nearestPlayer(m.pos, false)) lookAt(p->eyePos());
        return true;
      }
      // Huye de los monstruos que tiene cerca
      const Mob* scary = nullptr;
      double best = 64.0;
      for (const Mob& o : mobs_)
        if (!o.dying() && o.info().hostile && (o.type == MobType::Zombie || o.type == MobType::Witch)) {
          const double d = glm::dot(o.pos - m.pos, o.pos - m.pos);
          if (d < best) {
            best = d;
            scary = &o;
          }
        }
      if (scary) {
        const glm::dvec3 away = m.pos - scary->pos;
        walkTowards(m, m.pos + glm::normalize(glm::dvec3(away.x, 0, away.z) + glm::dvec3(1e-3, 0, 1e-3)) * 6.0, 1.3f);
        return true;
      }
      if (m.lookTicks > 0) {  // si lo miran, el aldeano mira al jugador
        m.lookTicks--;
        if (const Player* p = nearestPlayer(m.pos, false)) lookAt(p->eyePos());
        return true;
      }
      if (const Player* p = nearestPlayer(m.pos, false); p && glm::length(p->pos - m.pos) < 6.0 && rng_.nextInt(60) == 0) m.lookTicks = 40 + rng_.nextInt(40);
      relaxHead();
      wander(m, 100, 0.7f);
      return true;
    }
    case MobType::IronGolem: {
      Mob* tmob = m.target ? mobById(static_cast<u32>(m.target)) : nullptr;
      if (tmob && (tmob->dying() || glm::length(tmob->pos - m.pos) > 24.0)) {
        m.target = 0;
        tmob = nullptr;
      }
      if (!tmob && m.age % 10 == 0) {
        double best = 14.0 * 14.0;
        for (Mob& o : mobs_) {
          if (&o == &m || o.dying() || !o.info().hostile || o.type == MobType::EnderDragon || o.type == MobType::Enderman) continue;
          const double d = glm::dot(o.pos - m.pos, o.pos - m.pos);
          if (d < best) {
            best = d;
            tmob = &o;
          }
        }
        if (tmob) m.target = static_cast<int>(tmob->id);
      }
      Player* enemy = (!m.playerMade && m.angry) ? nearestPlayer(m.pos, true) : nullptr;
      if (enemy && glm::length(enemy->pos - m.pos) > 20.0) {
        m.angry = false;
        enemy = nullptr;
      }
      if (tmob || enemy) {
        const glm::dvec3 at = tmob ? tmob->pos : enemy->pos;
        lookAt(tmob ? tmob->eyePos() : enemy->eyePos());
        walkTowards(m, at, 1.0f);
        const glm::dvec3 d = at - m.pos;
        if (d.x * d.x + d.z * d.z < 6.0 && std::abs(d.y) < 3.0 && m.attackCooldown == 0) {
          const float dmg = 7.0f + static_cast<float>(rng_.nextInt(15));
          if (tmob) {
            hurtMob(*tmob, dmg, m.pos, 0.4f, false);
            tmob->motion.y = 0.4;  // los lanza por los aires
          } else {
            damagePlayer(*enemy, dmg, m.pos, 0.6f, DamageKind::Melee, &m);
            enemy->motion.y = 0.4;
          }
          m.attackCooldown = 40;
        }
        return true;
      }
      relaxHead();
      wander(m, 120, 0.9f);
      return true;
    }
    case MobType::SnowGolem: {
      // Se deshace donde hace calor y deja un rastro de nieve
      const BiomeInfo& biome = biomeInfo(w.biome(bx, bz));
      if (biome.temperature > 1.0f && m.age % 20 == 0) hurtMob(m, 1.0f, m.pos, 0.0f, false);
      if (m.onGround && biome.temperature < 0.8f && w.block(bx, by, bz) == 0 && blockInfo(stateId(w.block(bx, by - 1, bz))).opaqueCube &&
          true)
        setWorldBlock(bx, by, bz, makeState(B::snow_layer, 0));
      Mob* tmob = m.target ? mobById(static_cast<u32>(m.target)) : nullptr;
      if (tmob && (tmob->dying() || glm::length(tmob->pos - m.pos) > 16.0)) {
        m.target = 0;
        tmob = nullptr;
      }
      if (!tmob && m.age % 10 == 0) {
        double best = 10.0 * 10.0;
        for (Mob& o : mobs_) {
          if (&o == &m || o.dying() || !o.info().hostile || o.type == MobType::EnderDragon) continue;
          const double d = glm::dot(o.pos - m.pos, o.pos - m.pos);
          if (d < best) {
            best = d;
            tmob = &o;
          }
        }
        if (tmob) m.target = static_cast<int>(tmob->id);
      }
      if (tmob) {
        lookAt(tmob->eyePos());
        if (m.attackCooldown == 0) {  // una bola de nieve cada 1 a 2 segundos
          Thrown t;
          t.kind = Thrown::Snowball;
          t.id = nextThrownId_++;
          t.owner = m.id;
          t.byLocal = false;
          t.pos = t.prevPos = eye - glm::dvec3(0, 0.1, 0);
          glm::dvec3 d = tmob->eyePos() - t.pos;
          const double len = std::max(1e-3, glm::length(d));
          d.y += std::hypot(d.x, d.z) * 0.12;  // algo de arco
          t.motion = glm::normalize(d) * 1.2;
          (void)len;
          thrown_.push_back(t);
          m.attackCooldown = 20 + rng_.nextInt(20);
        }
        return true;
      }
      relaxHead();
      wander(m, 120, 0.9f);
      return true;
    }
    case MobType::Witch: {
      Player* tgt = nearestPlayer(m.pos, true);
      const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
      if (!tgt || dist > 20.0 || !canSeePlayer(m, *tgt)) {
        m.chasing = false;
        relaxHead();
        wander(m, 120, 1.0f);
        return true;
      }
      m.chasing = true;
      lookAt(tgt->eyePos());
      // Cura: con poca vida se bebe una poción de curación
      if (m.health < m.info().maxHealth * 0.5f && m.shootTicks == 0 && rng_.nextInt(40) == 0) {
        m.health = std::min(m.info().maxHealth, m.health + 6.0f);
        m.shootTicks = 40;
      }
      if (m.shootTicks > 0) m.shootTicks--;
      if (dist > 9.0) walkTowards(m, tgt->pos, 1.0f);
      else if (dist < 4.0) {
        const glm::dvec3 away = m.pos - tgt->pos;
        walkTowards(m, m.pos + glm::normalize(glm::dvec3(away.x, 0, away.z) + glm::dvec3(1e-3, 0, 1e-3)) * 4.0, 1.0f);
      }
      if (m.attackCooldown == 0 && dist < 11.0) {
        // Lanza una poción: lentitud si está lejos, veneno o daño de cerca (al azar)
        const int pick = rng_.nextInt(10);
        int meta = 16394;                              // lentitud (lanzable)
        if (dist >= 8.0) meta = 16394;
        else if (!tgt->effects.has(fx::Poison) && pick < 5) meta = 16388;  // veneno
        else if (pick < 8) meta = 16396;               // daño
        else meta = 16394;
        Thrown t;
        t.kind = Thrown::Potion;
        t.data = meta;
        t.id = nextThrownId_++;
        t.owner = m.id;
        t.byLocal = false;
        t.pos = t.prevPos = eye;
        glm::dvec3 d = tgt->pos + glm::dvec3(0, 1.0, 0) - t.pos;
        d.y += std::hypot(d.x, d.z) * 0.2;
        t.motion = glm::normalize(d) * 0.5;
        thrown_.push_back(t);
        m.attackCooldown = 60;
      }
      return true;
    }
    case MobType::Endermite: {
      Player* tgt = nearestPlayer(m.pos, true);
      const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
      if (m.age > 2400 && m.persistent == false) {  // desaparece a los 2 minutos
        m.health = 0;
        m.deathTime = 20;
        return true;
      }
      if (tgt && dist < 16.0 && canSeePlayer(m, *tgt)) {
        m.chasing = true;
        lookAt(tgt->eyePos());
        walkTowards(m, tgt->pos, 1.0f);
        const glm::dvec3 d = tgt->pos - m.pos;
        if (d.x * d.x + d.z * d.z < 1.2 && std::abs(d.y) < 1.5 && m.attackCooldown == 0) {
          damagePlayer(*tgt, 2.0f, m.pos, 0.3f, DamageKind::Melee, &m);
          m.attackCooldown = 20;
        }
      } else {
        m.chasing = false;
        wander(m, 80, 1.0f);
      }
      return true;
    }
    case MobType::Horse: {
      if (m.angry && !m.noAI) m.angry = false;
      Player* rider = riderOfMob(m);
      if (rider) {  // montado: lo guía quien va encima (si tiene silla o está domado)
        m.yaw = m.headYaw = rider->yaw;
        m.pitch = 0;
        m.moveSpeed = m.info().speed * (0.6f + 0.5f * static_cast<float>(((m.coat >> 16) & 255) % 8) / 7.0f);
        m.moveForward = m.moveSpeed * std::clamp(rider->moveForward, -0.3f, 1.0f);
        m.wantJump = m.collidedH && m.onGround;
        return true;
      }
      relaxHead();
      wander(m, 120, 0.9f);
      return true;
    }
    case MobType::Mooshroom:
    default: return false;
  }
}

// --- Usar lo que se lleva en la mano sobre ellos ------------------------------------------------

bool GameSession::useHeldOnMobMore(Mob& m) {
  ItemStack& held = player_.inventory.selected();
  auto consume = [&] {
    if (!player_.creative() && --held.count <= 0) held.clear();
  };
  auto event = [&](SessionEvent::Type t, int value) {
    SessionEvent e{t, glm::ivec3(glm::floor(m.pos)), 0};
    e.where = m.pos + glm::dvec3(0, m.info().height * m.scale() * 0.8, 0);
    e.mob = m.type;
    e.value = value;
    events_.push_back(e);
  };
  if (m.dying()) return false;
  switch (m.type) {
    case MobType::Mooshroom:
      if (held.id == ItemId::shears && !m.baby()) {  // esquilada, se vuelve vaca y suelta 5 setas
        const glm::dvec3 at = m.pos;
        for (int i = 0; i < 5; i++)
          spawnItem(at + glm::dvec3(0, 1.2, 0), ItemStack(ItemId::red_mushroom), {rng_.nextFloat() * 0.2 - 0.1, 0.25, rng_.nextFloat() * 0.2 - 0.1}, 10);
        m.type = MobType::Cow;
        if (!player_.creative()) damageTool(1);
        SessionEvent e{SessionEvent::Type::SheepSheared, glm::ivec3(glm::floor(m.pos)), 0};
        e.where = m.pos;
        e.mob = MobType::Cow;
        events_.push_back(e);
        return true;
      }
      if (held.id == ItemId::bowl && !m.baby()) {  // un cuenco: sopa de champiñones
        ItemStack stew(ItemId::mushroom_stew);
        if (player_.creative()) return true;
        if (--held.count <= 0) held = stew;
        else if (const ItemStack rest = player_.inventory.add(stew); !rest.empty()) spawnItem(player_.eyePos(), rest, {0, 0.1, 0}, 10);
        return true;
      }
      return false;
    case MobType::Wolf: {
      const bool owned = m.tamed && m.ownerUid == player_.uid;
      if (!m.tamed && !m.angry && held.id == ItemId::bone) {  // domar con huesos
        consume();
        if (rng_.nextInt(3) == 0) {
          m.tamed = true;
          m.ownerUid = player_.uid;
          m.sitting = true;
          m.health = 20.0f;
          m.target = 0;
          m.walkTarget.reset();
          event(SessionEvent::Type::LoveHearts, 7);
          achievements_.addStat("stat.animalsTamed");
        } else {
          event(SessionEvent::Type::LoveHearts, -7);  // humo: no ha funcionado
        }
        return true;
      }
      if (m.tamed && isMeat(held.id) && m.health < 20.0f) {  // la carne la cura
        const auto food = foodValue(held);
        m.health = std::min(20.0f, m.health + static_cast<float>(food ? food->food : 2));
        consume();
        event(SessionEvent::Type::LoveHearts, 3);
        return true;
      }
      if (owned && held.id == ItemId::dye) {  // el collar toma el color del tinte
        const u8 color = static_cast<u8>(held.meta & 15);
        if (m.collar != color) {
          m.collar = color;
          consume();
        }
        return true;
      }
      if (owned && !isMeat(held.id)) {  // el dueño la sienta o la levanta
        m.sitting = !m.sitting;
        m.walkTarget.reset();
        return true;
      }
      return false;
    }
    case MobType::Ocelot: {
      const bool owned = m.tamed && m.ownerUid == player_.uid;
      if (!m.tamed && held.id == ItemId::fish) {  // domar con pescado crudo
        consume();
        if (rng_.nextInt(3) == 0) {
          m.tamed = true;
          m.ownerUid = player_.uid;
          m.variant = static_cast<u8>(1 + rng_.nextInt(3));
          m.sitting = true;
          m.walkTarget.reset();
          event(SessionEvent::Type::LoveHearts, 7);
          achievements_.addStat("stat.animalsTamed");
        } else {
          event(SessionEvent::Type::LoveHearts, -7);
        }
        return true;
      }
      if (m.tamed && isFish(held.id) && m.health < 20.0f) {
        m.health = std::min(20.0f, m.health + 2.0f);
        consume();
        event(SessionEvent::Type::LoveHearts, 3);
        return true;
      }
      if (owned && !isFish(held.id)) {
        m.sitting = !m.sitting;
        m.walkTarget.reset();
        return true;
      }
      return false;
    }
    case MobType::Horse: {
      if (held.id == ItemId::saddle && m.tamed && !m.saddled && !m.baby()) {
        m.saddled = true;
        consume();
        return true;
      }
      if (held.id == ItemId::golden_apple || held.id == ItemId::golden_carrot || held.id == ItemId::apple || held.id == ItemId::wheat ||
          held.id == ItemId::sugar || held.id == 170) {  // (170 = bloque de heno)
        // Domar dándoles de comer: sube su temperamento; con suficiente se dejan montar
        if (!m.tamed && !m.baby()) {
          consume();
          m.tradeSeed += 3;
          if (m.tradeSeed >= 100 || rng_.nextInt(100) < m.tradeSeed) {
            m.tamed = true;
            m.ownerUid = player_.uid;
            event(SessionEvent::Type::LoveHearts, 7);
          }
          return true;
        }
        return false;
      }
      if (!m.baby() && held.id != ItemId::spawn_egg) {
        if (!m.tamed) {  // al montarlo sin domar, el caballo tira al jinete; cada intento sube el temperamento
          m.tradeSeed += 5;
          if (rng_.nextInt(100) < m.tradeSeed) {
            m.tamed = true;
            m.ownerUid = player_.uid;
            event(SessionEvent::Type::LoveHearts, 7);
          } else {
            event(SessionEvent::Type::LoveHearts, -7);
            return true;
          }
        }
        if (m.saddled || m.variant == 0 || m.tamed) {
          if (mountMob(player_, m)) {
            SessionEvent ev{SessionEvent::Type::CartRide, glm::ivec3(glm::floor(m.pos)), 0};
            ev.where = m.pos;
            events_.push_back(ev);
            return true;
          }
        }
      }
      return false;
    }
    case MobType::Villager: return !m.baby() && openTrading(m);
    default: return false;
  }
}

// --- Golpes y botín ---------------------------------------------------------------------------------

void GameSession::onMobHurtMore(Mob& m, bool byPlayer, const glm::dvec3& from) {
  (void)from;
  if (m.type == MobType::Wolf && byPlayer && !(m.tamed && m.ownerUid == player_.uid)) {
    m.angry = true;
    m.sitting = false;
    m.tamed = m.tamed;  // (un lobo manso al que pega otro jugador también se defiende)
    for (Mob& o : mobs_)  // la manada también
      if (o.type == MobType::Wolf && !o.dying() && !o.tamed && glm::length(o.pos - m.pos) < 16.0) o.angry = true;
  }
  if (m.type == MobType::IronGolem && byPlayer && !m.playerMade) m.angry = true;
  if (m.type == MobType::Villager && byPlayer)  // los gólems defienden al aldeano
    for (Mob& o : mobs_)
      if (o.type == MobType::IronGolem && !o.dying() && !o.playerMade && glm::length(o.pos - m.pos) < 24.0) o.angry = true;
  if (m.sitting && m.tamed) m.sitting = false;
  // El jugador pega a algo: sus lobos mansos van a por ello
  if (byPlayer && m.type != MobType::Wolf && m.type != MobType::Creeper && !(m.tamed && m.ownerUid == player_.uid))
    for (Mob& o : mobs_)
      if (o.type == MobType::Wolf && o.tamed && o.ownerUid == player_.uid && !o.sitting && !o.dying() && glm::length(o.pos - m.pos) < 20.0)
        o.target = static_cast<int>(m.id);
}

void GameSession::mobDropsMore(const Mob& m, int looting) {
  auto drop = [&](int id, int count, int meta = 0) {
    if (count <= 0) return;
    spawnItem(m.pos + glm::dvec3(0, 0.5, 0), ItemStack(id, count, meta), {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  };
  const int L = std::max(0, looting);
  const bool burning = m.fireTicks > 0;
  switch (m.type) {
    case MobType::Squid: drop(ItemId::dye, 1 + rng_.nextInt(3 + L), 0); break;
    case MobType::Rabbit:
      drop(ItemId::rabbit_hide, rng_.nextInt(2 + L));
      drop(burning ? ItemId::cooked_rabbit : ItemId::rabbit, rng_.nextInt(2 + L));
      if (rng_.nextInt(10) < 1 + L) drop(ItemId::rabbit_foot, 1);
      break;
    case MobType::Mooshroom:
      drop(ItemId::leather, rng_.nextInt(3 + L));
      drop(burning ? ItemId::cooked_beef : ItemId::beef, 1 + rng_.nextInt(3 + L));
      break;
    case MobType::Horse:
      drop(ItemId::leather, rng_.nextInt(3 + L));
      if (m.saddled) drop(ItemId::saddle, 1);
      break;
    case MobType::IronGolem:
      drop(ItemId::iron_ingot, 3 + rng_.nextInt(3));
      drop(B::red_flower, rng_.nextInt(3), 0);
      break;
    case MobType::SnowGolem: drop(ItemId::snowball, rng_.nextInt(16)); break;
    case MobType::Witch: {
      static const int kLoot[] = {ItemId::glass_bottle, ItemId::glowstone_dust, ItemId::gunpowder, ItemId::redstone, ItemId::spider_eye,
                                  ItemId::sugar, ItemId::stick};
      for (int i = 0, n = 1 + rng_.nextInt(3); i < n; i++) drop(kLoot[rng_.nextInt(7)], 1 + rng_.nextInt(2 + L));
      break;
    }
    default: break;
  }
}

// --- Aparición ----------------------------------------------------------------------------------

void GameSession::populateMore(int cx, int cz) {
  World& w = access_.world();
  const Chunk* c = w.chunk(cx, cz);
  if (!c) return;
  Random r(cellSeed(seed_, cx, cz, 0xFA11A));
  const int biome = c->biome(8, 8);
  const int topY = (c->topSection() + 1) * 16 - 1;
  auto groundAt = [&](int lx, int lz, int* yOut) {
    int y = topY;
    for (; y > 1; y--) {
      const int id = stateId(c->block(lx, y, lz));
      if (id == B::air || id == B::tallgrass || id == B::yellow_flower || id == B::red_flower || id == B::double_plant || id == B::leaves ||
          id == B::leaves2 || id == B::snow_layer)
        continue;
      break;
    }
    *yOut = y;
    return stateId(c->block(lx, y, lz));
  };
  auto place = [&](MobType type, int n, auto&& setup) {
    for (int i = 0; i < n; i++) {
      const int lx = r.nextInt(16), lz = r.nextInt(16);
      int y;
      const int ground = groundAt(lx, lz, &y);
      if (ground != B::grass && ground != B::sand && ground != B::dirt && ground != B::snow && ground != B::mycelium && type != MobType::Squid) continue;
      const int wx = cx * 16 + lx, wz = cz * 16 + lz;
      if (!freeCell(w, wx, y + 1, wz) || !freeCell(w, wx, y + 2, wz)) continue;
      if (Mob* m = spawnMob(type, {wx + 0.5, y + 1.0, wz + 0.5})) setup(*m);
    }
  };
  // Calamares: en agua de al menos tres bloques de profundidad
  if (biome == Biome::ocean || biome == Biome::deep_ocean || biome == Biome::river || biome == Biome::frozen_ocean) {
    if (r.nextFloat() < 0.08f) {
      const int n = 1 + r.nextInt(3);
      for (int i = 0; i < n; i++) {
        const int lx = r.nextInt(16), lz = r.nextInt(16);
        int y = 62;
        while (y > 40 && !isWater(stateId(c->block(lx, y, lz)))) y--;
        if (y <= 42 || !isWater(stateId(c->block(lx, y - 1, lz))) || !isWater(stateId(c->block(lx, y - 2, lz)))) continue;
        spawnMob(MobType::Squid, {cx * 16 + lx + 0.5, y - 1.5, cz * 16 + lz + 0.5});
      }
    }
    return;
  }
  const int passive = static_cast<int>(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return !m.info().hostile; }));
  if (passive >= 60) return;
  const int base = baseBiome(biome);  // (las colinas y mesetas llevan lo mismo que su bioma de base)
  if ((base == Biome::forest || base == Biome::taiga || base == Biome::cold_taiga || base == Biome::birch_forest || base == Biome::mega_taiga) &&
      r.nextFloat() < 0.04f)
    place(MobType::Wolf, 2 + r.nextInt(3), [](Mob&) {});
  if ((base == Biome::desert || base == Biome::ice_plains || base == Biome::taiga || base == Biome::cold_taiga || base == Biome::plains) &&
      r.nextFloat() < 0.05f)
    place(MobType::Rabbit, 2 + r.nextInt(2), [&](Mob& m) { m.variant = static_cast<u8>(rabbitCoat(base, r)); });
  if ((base == Biome::plains || base == Biome::savanna) && r.nextFloat() < 0.05f)
    place(MobType::Horse, 2 + r.nextInt(4), [&](Mob& m) {
      m.variant = r.nextInt(12) == 0 ? (r.nextInt(2) ? 1 : 2) : 0;  // algún burro o mula
      m.coat = static_cast<u32>(r.nextInt(7)) | (static_cast<u32>(r.nextInt(5)) << 8) | (static_cast<u32>(r.nextInt(8)) << 16);
      m.health = m.info().maxHealth - 7.0f + static_cast<float>(r.nextInt(15));
    });
  if ((biome == Biome::mushroom_island || biome == Biome::mushroom_shore) && r.nextFloat() < 0.3f) place(MobType::Mooshroom, 2 + r.nextInt(3), [](Mob&) {});
  if ((base == Biome::jungle || biome == Biome::jungle_edge) && r.nextFloat() < 0.1f) place(MobType::Ocelot, 1 + r.nextInt(2), [](Mob&) {});
}

void GameSession::spawnAmbient() {
  // Murciélagos en cuevas oscuras bajo el nivel del mar (y como mucho 15 a la vez)
  if (!rules_.mobSpawning || dimension_ != 0 || rules_.difficulty == 0) return;
  if (std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.type == MobType::Bat; }) >= 15) return;
  const std::vector<Player*> players = activePlayers();
  if (players.empty()) return;
  World& w = access_.world();
  const Player& center = *players[players.size() == 1 ? 0 : tickRng_.nextInt(static_cast<int>(players.size()))];
  for (int attempt = 0; attempt < 3; attempt++) {
    const int x = static_cast<int>(std::floor(center.pos.x)) + tickRng_.nextInt(41) - 20;
    const int z = static_cast<int>(std::floor(center.pos.z)) + tickRng_.nextInt(41) - 20;
    const int y = 6 + tickRng_.nextInt(52);
    if (!w.chunkAt(x, z)) continue;
    if (!freeCell(w, x, y, z) || !freeCell(w, x, y + 1, z) || w.skyLight(x, y, z) > 0 || w.blockLight(x, y, z) > 4) continue;
    if (glm::length(center.pos - glm::dvec3(x, y, z)) < 8.0) continue;
    if (Mob* b = spawnMob(MobType::Bat, {x + 0.5, static_cast<double>(y), z + 0.5})) {
      // Si hay techo de roca encima, empieza colgado
      b->hanging = blockInfo(stateId(w.block(x, y + 1, z))).opaqueCube && tickRng_.nextInt(2) == 0;
      if (b->hanging) b->pos.y = y + 1 - b->info().height;
    }
    return;
  }
}

// --- Golems hechos por el jugador --------------------------------------------------------------

bool GameSession::tryBuildGolem(const glm::ivec3& p) {
  World& w = access_.world();
  auto idAt = [&](int x, int y, int z) { return stateId(w.block(x, y, z)); };
  const int pumpkin = B::pumpkin;
  if (idAt(p.x, p.y, p.z) != pumpkin) return false;
  // Gólem de nieve: dos bloques de nieve debajo
  if (idAt(p.x, p.y - 1, p.z) == B::snow && idAt(p.x, p.y - 2, p.z) == B::snow) {
    for (int k = 0; k <= 2; k++) setWorldBlock(p.x, p.y - k, p.z, 0);
    if (Mob* g = spawnMob(MobType::SnowGolem, {p.x + 0.5, static_cast<double>(p.y - 2), p.z + 0.5})) g->persistent = true;
    return true;
  }
  // Gólem de hierro: cuerpo de tres bloques de hierro con los brazos en cruz (en X o en Z) y la calabaza encima
  if (idAt(p.x, p.y - 1, p.z) == B::iron_block && idAt(p.x, p.y - 2, p.z) == B::iron_block && idAt(p.x, p.y - 3, p.z) == B::iron_block) {
    for (int axis = 0; axis < 2; axis++) {
      const glm::ivec3 d = axis == 0 ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
      const glm::ivec3 a = p + glm::ivec3(0, -2, 0) + d, b = p + glm::ivec3(0, -2, 0) - d;
      if (idAt(a.x, a.y, a.z) == B::iron_block && idAt(b.x, b.y, b.z) == B::iron_block) {
        // Hace falta sitio libre para el gólem y que los brazos no tengan nada debajo (ni más hierro)
        for (int k = 0; k <= 3; k++) setWorldBlock(p.x, p.y - k, p.z, 0);
        setWorldBlock(a.x, a.y, a.z, 0);
        setWorldBlock(b.x, b.y, b.z, 0);
        if (Mob* g = spawnMob(MobType::IronGolem, {p.x + 0.5, static_cast<double>(p.y - 3) + 0.0, p.z + 0.5})) {
          g->playerMade = true;
          g->persistent = true;
        }
        return true;
      }
    }
  }
  return false;
}

}  // namespace mcw
