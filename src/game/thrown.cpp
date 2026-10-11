// Objetos lanzados con el clic derecho: bola de nieve, huevo, perla de ender, frasco de experiencia y ojo de ender.
#include <algorithm>
#include <cmath>

#include "data/items.h"
#include "game/physics.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

bool GameSession::throwHeld() {
  ItemStack& held = player_.inventory.selected();
  Thrown t;
  switch (held.id) {
    case ItemId::snowball: t.kind = Thrown::Snowball; break;
    case ItemId::egg: t.kind = Thrown::Egg; break;
    case ItemId::ender_pearl: t.kind = Thrown::Pearl; break;
    case ItemId::experience_bottle: t.kind = Thrown::XpBottle; break;
    case ItemId::ender_eye: t.kind = Thrown::Eye; break;
    default: return false;
  }
  const glm::dvec3 look(-std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), -std::cos(player_.yaw) * std::cos(player_.pitch));
  t.id = nextThrownId_++;
  t.pos = t.prevPos = player_.eyePos() - glm::dvec3(0, 0.1, 0) + look * 0.3;
  if (t.kind == Thrown::Eye) {
    // Va hacia el fortín más cercano (en el mundo normal); en otra dimensión no encuentra nada y sube un poco
    const auto s = nearestStronghold(player_.pos);
    glm::dvec2 dir = s ? glm::dvec2(s->x + 0.5 - player_.pos.x, s->y + 0.5 - player_.pos.z) : glm::dvec2(look.x, look.z);
    const double len = std::max(1e-3, glm::length(dir));
    t.target = dir / len;  // (la dirección)
    t.motion = {t.target.x * 0.3, 0.35, t.target.y * 0.3};
  } else {
    const double speed = t.kind == Thrown::XpBottle ? 0.7 : 1.5;
    t.motion = look * speed + glm::dvec3(rng_.nextFloat() * 0.01, rng_.nextFloat() * 0.01, rng_.nextFloat() * 0.01);
    if (t.kind == Thrown::XpBottle) t.motion.y += 0.1;
  }
  thrown_.push_back(t);
  if (!player_.creative() && --held.count <= 0) held.clear();
  useDelay_ = 4;
  events_.push_back({SessionEvent::Type::Click, glm::ivec3(glm::floor(player_.pos)), 0});
  return true;
}

void GameSession::tickThrown() {
  World& w = access_.world();
  for (Thrown& t : thrown_) {
    t.prevPos = t.pos;
    t.life++;
    if (t.kind == Thrown::Eye) {
      // Sube y avanza unos 12 bloques hacia el destino; luego cae. A los 80 ticks se acaba (80 % de las veces se puede recoger)
      t.pos += t.motion;
      t.traveled += 0.3;
      if (t.traveled > 12.0) t.motion = {0, t.motion.y * 0.9 - 0.03, 0};
      else t.motion.y = std::max(0.02, t.motion.y - 0.012);
      if (t.life >= 80) {
        if (rng_.nextInt(5) != 0) spawnItem(t.pos, ItemStack(ItemId::ender_eye), {0, 0.1, 0}, 10);
        else events_.push_back({SessionEvent::Type::Fizz, glm::ivec3(glm::floor(t.pos)), 0});
        t.life = 100000;
      }
      continue;
    }
    glm::dvec3 step = t.motion;
    const double len = glm::length(step);
    bool hit = false;
    glm::dvec3 at = t.pos;
    Mob* hitMob = nullptr;
    if (len > 1e-6)
      if (const auto r = raycastBlocks(w, t.pos, step / len, len)) {
        hit = true;
        at = r->point;
      }
    if (!hit && t.life > 2) {
      const AABB box = AABB::centered(t.pos + step * 0.5, 0.25, 0.25);
      for (Mob& m : mobs_)
        if (!m.dying() && m.box().expand({0.1, 0.1, 0.1}).intersects(box)) {
          hitMob = &m;
          hit = true;
          at = t.pos;
          break;
        }
    }
    if (!hit) {
      t.pos += step;
      const bool water = isWater(stateId(w.block(static_cast<int>(std::floor(t.pos.x)), static_cast<int>(std::floor(t.pos.y)), static_cast<int>(std::floor(t.pos.z)))));
      t.motion *= water ? 0.8 : 0.99;
      t.motion.y -= t.kind == Thrown::XpBottle ? 0.07 : 0.03;
      continue;
    }
    switch (t.kind) {
      case Thrown::Snowball:
        if (hitMob) hurtMob(*hitMob, hitMob->type == MobType::Blaze ? 3.0f : 0.0001f, player_.pos, 0.4f, true);
        break;
      case Thrown::Egg:
        if (hitMob) hurtMob(*hitMob, 0.0001f, player_.pos, 0.4f, true);
        if (rng_.nextInt(8) == 0) {
          const int n = rng_.nextInt(32) == 0 ? 4 : 1;
          for (int i = 0; i < n; i++)
            if (Mob* c = spawnMob(MobType::Chicken, at)) c->growth = -kBabyTicks;
        }
        break;
      case Thrown::Pearl:
        if (t.byLocal && !player_.dead) {
          player_.pos = player_.prevPos = glm::dvec3(at.x, std::max(at.y, 1.0), at.z);
          player_.motion = {0, 0, 0};
          player_.fallDistance = 0;
          if (!player_.creative()) player_.damage(5.0f, false, DamageKind::Fall);
          events_.push_back({SessionEvent::Type::Teleport, glm::ivec3(glm::floor(at)), 0});
        }
        break;
      case Thrown::XpBottle:
        spawnXp(at, 3 + rng_.nextInt(5) + rng_.nextInt(5));
        events_.push_back({SessionEvent::Type::Fizz, glm::ivec3(glm::floor(at)), 0});
        break;
      default: break;
    }
    t.life = 100000;
  }
  std::erase_if(thrown_, [](const Thrown& t) { return t.life > 600 || t.pos.y < -64; });
}

}  // namespace mcw
