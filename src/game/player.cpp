#include "game/player.h"

#include <cmath>

#include "game/armor.h"
#include "world/world.h"

namespace mcw {

float slipperiness(int blockId) {
  if (blockId == B::ice || blockId == B::packed_ice) return 0.98f;
  if (blockId == B::slime) return 0.8f;
  return 0.6f;
}

glm::dvec3 Player::lookDir() const {
  return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

namespace {

bool boxTouchesWater(const World& w, const AABB& b) {
  for (int x = static_cast<int>(std::floor(b.min.x)); x <= static_cast<int>(std::floor(b.max.x)); x++)
    for (int y = static_cast<int>(std::floor(b.min.y)); y <= static_cast<int>(std::floor(b.max.y)); y++)
      for (int z = static_cast<int>(std::floor(b.min.z)); z <= static_cast<int>(std::floor(b.max.z)); z++)
        if (isWater(stateId(w.block(x, y, z)))) return true;
  return false;
}

}  // namespace

void Player::moveWithCollisions(const World& world, glm::dvec3 m) {
  AABB b = box();
  // Agachado en el suelo: no caer por los bordes (se recorta el movimiento horizontal)
  if (sneaking && onGround && !flying) {
    std::vector<AABB> below;
    auto supported = [&](double dx, double dz) {
      collectBlockBoxes(world, b.offset({dx, -1.0, dz}), below);
      const AABB probe = b.offset({dx, -1.0, dz});
      for (const AABB& o : below)
        if (o.intersects(probe)) return true;
      return false;
    };
    const double step = 0.05;
    while (m.x != 0 && !supported(m.x, 0)) m.x = std::abs(m.x) < step ? 0 : m.x - std::copysign(step, m.x);
    while (m.z != 0 && !supported(0, m.z)) m.z = std::abs(m.z) < step ? 0 : m.z - std::copysign(step, m.z);
    while (m.x != 0 && m.z != 0 && !supported(m.x, m.z)) {
      m.x = std::abs(m.x) < step ? 0 : m.x - std::copysign(step, m.x);
      m.z = std::abs(m.z) < step ? 0 : m.z - std::copysign(step, m.z);
    }
  }
  const glm::dvec3 before = pos;
  glm::dvec3 moved = m;
  const MoveResult r = moveBox(world, b, moved, 0.6, onGround);
  pos = {b.center().x, b.min.y, b.center().z};
  collidedHorizontally = r.collidedX || r.collidedZ;
  const double dy = pos.y - before.y;

  // Caídas: acumular distancia y aplicar daño al aterrizar
  if (r.onGround) {
    if (fallDistance > 3.0 && mode == GameMode::Survival && !inWater && !flying) damage(static_cast<float>(std::ceil(fallDistance - 3.0)));
    fallDistance = 0;
  } else if (dy < 0) {
    fallDistance -= dy;
  }
  onGround = r.onGround;
  if (r.collidedX) motion.x = 0;
  if (r.collidedZ) motion.z = 0;
  if (r.collidedY) motion.y = 0;
}

void Player::travel(const World& world, float strafe, float forward, bool jump) {
  auto moveRelative = [&](float accel) {
    float f = strafe * strafe + forward * forward;
    if (f < 1e-4f) return;
    f = accel / std::max(1.0f, std::sqrt(f));
    const float s = strafe * f, fw = forward * f;
    const double sy = std::sin(yaw), cy = std::cos(yaw);
    // adelante = (-sin yaw, -cos yaw), derecha = (cos yaw, -sin yaw)
    motion.x += -sy * fw + cy * s;
    motion.z += -cy * fw - sy * s;
  };

  if (flying) {
    const double vy = motion.y;
    moveRelative(sprinting ? 0.1f : 0.05f);
    moveWithCollisions(world, motion);
    motion.x *= 0.91;
    motion.z *= 0.91;
    motion.y = vy * 0.6;
    return;
  }
  if (inWater) {
    moveRelative(0.02f);
    moveWithCollisions(world, motion);
    motion *= 0.8;
    motion.y -= 0.02;
    if (jump) motion.y += 0.04;
    if (collidedHorizontally) {
      // Salir del agua trepando por la orilla
      AABB test = box().offset({motion.x, motion.y + 0.6, motion.z});
      std::vector<AABB> obs;
      collectBlockBoxes(world, test, obs);
      bool free = true;
      for (const AABB& o : obs) free &= !o.intersects(test);
      if (free) motion.y = 0.3;
    }
    return;
  }
  const int below = stateId(world.block(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y - 0.5)),
                                        static_cast<int>(std::floor(pos.z))));
  const float f4 = onGround ? slipperiness(below) * 0.91f : 0.91f;
  const float speed = 0.1f * (sprinting ? 1.3f : 1.0f);
  const float accel = onGround ? speed * 0.16277136f / (f4 * f4 * f4) : (sprinting ? 0.026f : 0.02f);
  moveRelative(accel);
  moveWithCollisions(world, motion);
  motion.y = (motion.y - 0.08) * 0.98;
  motion.x *= f4;
  motion.z *= f4;
}

void Player::tickMovement(const World& world, const MoveInput& in, bool jumpPressed) {
  prevPos = pos;
  if (dead) return;
  if (flyToggleCooldown_ > 0) flyToggleCooldown_--;

  // Doble toque de saltar: volar / dejar de volar (solo en creativo)
  if (jumpPressed) {
    if (mode == GameMode::Creative && jumpTicks_ > 0 && flyToggleCooldown_ == 0) {
      flying = !flying;
      jumpTicks_ = 0;
      flyToggleCooldown_ = 5;
    } else {
      jumpTicks_ = 7;
    }
  } else if (jumpTicks_ > 0) {
    jumpTicks_--;
  }
  if (mode != GameMode::Creative) flying = false;

  inWater = boxTouchesWater(world, box().expand({-0.001, -0.4, -0.001}));
  headInWater = isWater(stateId(world.block(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(eyePos().y)),
                                            static_cast<int>(std::floor(pos.z)))));
  if (inWater) fallDistance = 0;
  sneaking = in.sneak && !flying;

  float forward = in.forward * 0.98f, strafe = in.strafe * 0.98f;
  if (sneaking) {
    forward *= 0.3f;
    strafe *= 0.3f;
  }
  const bool canSprint = mode == GameMode::Creative || food > 6;
  if (in.sprint && forward > 0.5f && !sneaking && canSprint && !collidedHorizontally) sprinting = true;
  if (forward <= 0.5f || sneaking || !canSprint || collidedHorizontally) sprinting = false;

  if (flying) {
    if (in.sneak) motion.y -= 0.15;
    if (in.jump) motion.y += 0.15;
  } else if ((in.jump || (in.autoJump && onGround && !sneaking && stepAhead(world, forward, strafe))) && onGround && !inWater) {
    motion.y = 0.42;
    if (sprinting) {
      motion.x += -std::sin(yaw) * 0.2;
      motion.z += -std::cos(yaw) * 0.2;
    }
    addExhaustion(sprinting ? 0.2f : 0.05f);
  }

  const glm::dvec3 before = pos;
  travel(world, strafe, forward, in.jump);
  if (flying && onGround) flying = false;  // aterrizar deja de volar
  if (sprinting) {
    const double d = std::hypot(pos.x - before.x, pos.z - before.z);
    addExhaustion(static_cast<float>(0.1 * d));
  }
  // Vacío
  if (pos.y < -64 && mode == GameMode::Survival) damage(4.0f);
}

void Player::tickStatus(const World& world) {
  (void)world;
  if (hurtTime > 0) hurtTime--;  // (aquí y no en el movimiento: a un invitado solo se le llama a esta)
  if (dead || mode == GameMode::Creative) return;
  // Hambre (valores de minecraft.wiki: cada 4 de agotamiento baja saturación y luego comida)
  if (exhaustion > 4.0f) {
    exhaustion -= 4.0f;
    if (saturation > 0) saturation = std::max(0.0f, saturation - 1.0f);
    else if (difficulty > 0) food = std::max(0, food - 1);
  }
  // Pacífico: se recupera vida y comida solo
  if (difficulty == 0) {
    peacefulTimer_++;
    if (peacefulTimer_ % 20 == 0 && health < kMaxHealth) health = std::min(kMaxHealth, health + 1.0f);
    if (peacefulTimer_ % 10 == 0 && food < 20) food++;
  }
  foodTimer++;
  if (food >= 18 && health < kMaxHealth) {
    if (foodTimer >= 80) {
      health = std::min(kMaxHealth, health + 1.0f);
      addExhaustion(3.0f);
      foodTimer = 0;
    }
  } else if (food <= 0) {
    if (foodTimer >= 80) {
      // Inanición: en fácil se queda en 5 corazones, en normal en medio y en difícil mata
      if (health > 10.0f || difficulty >= 3 || (health > 1.0f && difficulty == 2)) damage(1.0f);
      foodTimer = 0;
    }
  } else {
    foodTimer = 0;
  }
  // Aire bajo el agua: 300 ticks y luego 2 de daño por segundo
  if (headInWater) {
    if (--air <= -20) {
      air = 0;
      damage(2.0f);
    }
  } else {
    air = 300;
  }
}

bool Player::stepAhead(const World& world, float forward, float strafe) const {
  if (std::abs(forward) < 0.1f && std::abs(strafe) < 0.1f) return false;
  const double sy = std::sin(yaw), cy = std::cos(yaw);
  glm::dvec3 dir(-sy * forward + cy * strafe, 0.0, -cy * forward - sy * strafe);
  dir = glm::normalize(dir) * 0.35;
  // Chocaría a la altura de los pies, pero un bloque más arriba cabe
  const AABB low = box().offset({dir.x, 0.05, dir.z}), high = box().offset({dir.x, 1.05, dir.z});
  std::vector<AABB> obs;
  collectBlockBoxes(world, low.expand({0, 1.0, 0}), obs);
  bool blocked = false, free = true;
  for (const AABB& o : obs) {
    blocked |= o.intersects(low);
    free &= !o.intersects(high);
  }
  return blocked && free;
}

bool Player::damage(float amount, bool armored) {
  if (dead || mode == GameMode::Creative || amount <= 0) return false;
  // Invulnerabilidad breve tras recibir daño (solo cuenta si el golpe nuevo es mayor)
  if (hurtTime > 0 && amount <= lastDamage) return false;
  float dealt = hurtTime > 0 ? amount - lastDamage : amount;
  if (armored) {
    // Cada punto de armadura quita un 4 %; cada pieza se desgasta max(daño / 4, 1) (con el daño sin reducir)
    const int points = inventory.armorPoints();
    if (points > 0) {
      const int wear = std::max(1, static_cast<int>(dealt / 4.0f));
      for (int i = 0; i < 4; i++) {
        ItemStack& piece = inventory.armor(i);
        if (piece.empty() || !isArmor(piece.id)) continue;
        piece.meta = static_cast<i16>(piece.meta + wear);
        if (piece.meta >= itemInfo(piece.id).maxDurability) piece.clear();  // se rompe
      }
      dealt = dealt * static_cast<float>(25 - points) / 25.0f;
    }
  }
  health -= dealt;
  lastDamage = amount;
  hurtTime = 10;
  addExhaustion(0.3f);
  if (health <= 0) {
    health = 0;
    dead = true;
  }
  return true;
}

int Player::addXp(int amount) {
  if (amount <= 0) return 0;
  int gained = 0;
  xpProgress += static_cast<float>(amount) / static_cast<float>(xpBarCap());
  xpTotal += amount;
  while (xpProgress >= 1.0f) {
    xpProgress = (xpProgress - 1.0f) * static_cast<float>(xpBarCap());  // lo que sobra, en puntos del nivel que se deja
    addXpLevels(1);
    gained++;
    xpProgress /= static_cast<float>(xpBarCap());                       // y otra vez como fracción, del nuevo nivel
  }
  return gained;
}

void Player::addXpLevels(int levels) {
  xpLevel += levels;
  if (xpLevel < 0) {
    xpLevel = 0;
    xpProgress = 0.0f;
    xpTotal = 0;
  }
}

void Player::eat(int foodPoints, float saturationModifier) {
  food = std::min(20, food + foodPoints);
  saturation = std::min(static_cast<float>(food), saturation + foodPoints * saturationModifier * 2.0f);
}

void Player::respawn(const glm::dvec3& at) {
  pos = prevPos = at;
  motion = {0, 0, 0};
  health = kMaxHealth;
  food = 20;
  saturation = 5;
  exhaustion = 0;
  fallDistance = 0;
  air = 300;
  hurtTime = 0;
  dead = false;
  flying = false;
}

}  // namespace mcw
