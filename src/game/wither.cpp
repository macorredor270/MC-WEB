// El Wither: se invoca con cuatro bloques de arena de almas en T y tres calaveras de esqueleto atrofiado. Nace cargándose
// (11 segundos invulnerable y quieto) y explota con potencia 7; después vuela, dispara calaveras explosivas, se cubre de
// armadura con la mitad de la vida y suelta una estrella del Nether (comportamiento observable de 1.8 según minecraft.wiki).
#include <algorithm>
#include <cmath>
#include <numbers>

#include "data/blocks.h"
#include "data/items.h"
#include "game/block_entity.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
float wrapAngle(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}
float approachAngle(float from, float to, float maxStep) { return from + std::clamp(wrapAngle(to - from), -maxStep, maxStep); }
}  // namespace

bool GameSession::tryBuildWither(const glm::ivec3& placed) {
  World& w = access_.world();
  auto idAt = [&](const glm::ivec3& p) { return stateId(w.block(p.x, p.y, p.z)); };
  auto isWitherSkull = [&](const glm::ivec3& p) {
    if (idAt(p) != kSkullBlock) return false;
    const auto it = tiles_.skulls.find({p.x, p.y, p.z});
    return it != tiles_.skulls.end() && it->second.type == 1;
  };
  static const glm::ivec3 kAxes[2] = {{1, 0, 0}, {0, 0, 1}};
  for (const glm::ivec3& d : kAxes)
    for (int k = -1; k <= 1; k++) {
      const glm::ivec3 mid = placed + d * k;
      const glm::ivec3 down(0, -1, 0);
      if (!isWitherSkull(mid) || !isWitherSkull(mid - d) || !isWitherSkull(mid + d)) continue;
      if (idAt(mid + down) != B::soul_sand || idAt(mid - d + down) != B::soul_sand || idAt(mid + d + down) != B::soul_sand ||
          idAt(mid + down * 2) != B::soul_sand)
        continue;
      // Hace falta aire a los dos lados del pie de la T
      if (w.block(mid.x - d.x, mid.y - 2, mid.z - d.z) != 0 || w.block(mid.x + d.x, mid.y - 2, mid.z + d.z) != 0) continue;
      const glm::ivec3 cells[7] = {mid, mid - d, mid + d, mid + down, mid - d + down, mid + d + down, mid + down * 2};
      for (const glm::ivec3& c : cells) {
        tiles_.skulls.erase({c.x, c.y, c.z});
        setWorldBlock(c.x, c.y, c.z, 0);
      }
      for (const glm::ivec3& c : cells) neighborUpdates(c);
      Mob wither;
      wither.type = MobType::Wither;
      wither.pos = wither.prevPos = glm::dvec3(mid.x + 0.5, mid.y - 2, mid.z + 0.5);
      wither.yaw = wither.prevYaw = wither.headYaw = wither.prevHeadYaw = d.x != 0 ? 0.0f : kPi / 2;  // de cara a un lado de la T
      wither.health = wither.info().maxHealth / 3.0f;
      wither.invul = 220;
      wither.persistent = true;
      addMob(std::move(wither));
      for (Player* p : activePlayers())
        if (glm::length(p->pos - glm::dvec3(mid)) < 50.0 && p == &player_) award(Ach::SpawnWither);
      SessionEvent e{SessionEvent::Type::WitherSpawned, mid, 0};
      e.where = glm::dvec3(mid) + glm::dvec3(0.5, -1.0, 0.5);
      events_.push_back(e);
      return true;
    }
  return false;
}

void GameSession::witherSkull(Mob& m, const glm::dvec3& at, bool blue, int head) {
  Fireball f;
  f.id = nextFireballId_++;
  f.large = false;
  f.skull = blue ? 2 : 1;
  f.owner = m.id;
  const double side = head == 0 ? 0.0 : (head == 1 ? -1.3 : 1.3);
  const glm::dvec3 right(std::cos(m.headYaw), 0.0, -std::sin(m.headYaw));
  const glm::dvec3 origin = m.pos + glm::dvec3(0, head == 0 ? 3.0 : 2.2, 0) + right * side;
  glm::dvec3 d = at - origin;
  const double len = std::max(1e-3, glm::length(d));
  f.pos = f.prevPos = origin;
  f.motion = d / len * (blue ? 0.45 : 0.9);
  fireballs_.push_back(f);
  events_.push_back({SessionEvent::Type::WitherShoot, glm::ivec3(glm::floor(origin)), 0, origin});
}

void GameSession::witherAI(Mob& m) {
  const MobInfo& info = m.info();
  if (m.dying()) {
    m.motion *= 0.9;
    return;
  }
  const glm::dvec3 center = m.pos + glm::dvec3(0, 1.75, 0);
  // Cargándose: quieto e invulnerable; la vida vuelve a llenarse y al final explota
  if (m.invul > 0) {
    m.invul--;
    m.motion = {0, 0, 0};
    m.health = std::min(info.maxHealth, m.health + info.maxHealth * (2.0f / 3.0f) / 220.0f);
    if (m.invul == 0) {
      explodeIgnore_ = m.id;
      explode(center, 7.0f, false);
      explodeIgnore_ = 0;
      m.health = info.maxHealth;
      events_.push_back({SessionEvent::Type::WitherSpawned, glm::ivec3(glm::floor(m.pos)), 0, center, MobType::Wither, 1});
    }
    return;
  }
  // Se regenera un punto por segundo
  if (m.age % 20 == 0) m.health = std::min(info.maxHealth, m.health + 1.0f);
  // Tras un golpe, rompe lo que le rodea (menos lo irrompible)
  if (m.blockBreak > 0 && --m.blockBreak == 0) {
    World& w = access_.world();
    const AABB b = m.box().expand({1.0, 1.0, 1.0});
    access_.beginBatch();
    for (int y = static_cast<int>(std::floor(b.min.y)); y <= static_cast<int>(std::floor(b.max.y)); y++)
      for (int z = static_cast<int>(std::floor(b.min.z)); z <= static_cast<int>(std::floor(b.max.z)); z++)
        for (int x = static_cast<int>(std::floor(b.min.x)); x <= static_cast<int>(std::floor(b.max.x)); x++) {
          const int id = stateId(w.block(x, y, z));
          if (id == 0 || id == B::bedrock || id == 119 || id == 120 || id == 137 || isFluid(id) || blockInfo(id).hardness < 0) continue;
          breakBlockAt({x, y, z}, true);
        }
    access_.endBatch();
  }
  Player* tgt = nearestPlayer(m.pos, true);
  const double dist = tgt ? glm::length(tgt->pos - m.pos) : 1e9;
  const bool armored = m.health <= info.maxHealth * 0.5f;
  glm::dvec3 wish(0, 0, 0);
  if (tgt && dist < 50.0) {
    const glm::dvec3 flat(tgt->pos.x - m.pos.x, 0, tgt->pos.z - m.pos.z);
    const double hd = std::max(1e-3, glm::length(flat));
    // Se queda a unos 12 bloques en horizontal y por encima de quien ataca (con armadura, a su altura)
    const double want = hd > 15.0 ? 0.6 : hd < 9.0 ? -0.5 : 0.0;
    wish = flat / hd * want;
    const double targetY = tgt->pos.y + (armored ? 0.8 : 4.0);
    wish.y = std::clamp((targetY - m.pos.y) * 0.1, -0.4, 0.4);
    wish += glm::dvec3(-flat.z, 0, flat.x) / hd * std::sin(m.age * 0.05) * 0.15;  // un vaivén de lado
    const float want2 = std::atan2(static_cast<float>(-flat.x), static_cast<float>(-flat.z));
    m.yaw = approachAngle(m.yaw, want2, 0.08f);
    m.headYaw = approachAngle(m.headYaw, want2, 0.2f);
    m.pitch = std::clamp(static_cast<float>(std::atan2(tgt->eyePos().y - center.y, hd)), -0.7f, 0.7f);
  } else {
    wish.y = std::sin(m.age * 0.05) * 0.03;
  }
  m.motion += (wish - m.motion) * 0.1;
  // Las cabezas disparan: la del medio al jugador (0,1 % azul); las de los lados, a lo que se cruce (y de vez en cuando, azules al azar)
  if (tgt && dist < 40.0) {
    const glm::dvec3 aim = tgt->eyePos() - glm::dvec3(0, 0.3, 0);
    for (int h = 0; h < 3; h++) {
      if (m.headCd[static_cast<std::size_t>(h)] > 0) {
        m.headCd[static_cast<std::size_t>(h)]--;
        continue;
      }
      if (!canSeePlayer(m, *tgt)) continue;
      const bool blue = h == 0 ? rng_.nextFloat() < 0.001f : rng_.nextInt(12) == 0;
      glm::dvec3 target = aim;
      if (h > 0) {  // los lados buscan otro blanco cerca (criaturas que no sean no-muertos)
        const Mob* other = nullptr;
        double best = 400.0;
        for (const Mob& o : mobs_) {
          if (o.dying() || o.id == m.id || o.type == MobType::Zombie || o.type == MobType::Skeleton || o.type == MobType::WitherSkeleton ||
              o.type == MobType::PigZombie || o.type == MobType::Wither)
            continue;
          const double dd = glm::dot(o.pos - m.pos, o.pos - m.pos);
          if (dd < best && rng_.nextInt(3) == 0) {
            best = dd;
            other = &o;
          }
        }
        if (other) target = other->pos + glm::dvec3(0, other->info().height * 0.6, 0);
      }
      witherSkull(m, target, blue, h);
      m.headCd[static_cast<std::size_t>(h)] = (armored ? 36 : 40) + rng_.nextInt(armored ? 12 : 20) + h * 4;
    }
  }
}

}  // namespace mcw
