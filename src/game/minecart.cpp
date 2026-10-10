#include "game/minecart.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "data/blockstates.h"
#include "data/items.h"
#include "game/rails.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr double kSlope = 0.0078125;  // lo que acelera una cuesta por tick (1/128)
constexpr double kBase = 0.0625;      // la base de la vagoneta va a esta altura sobre el raíl
constexpr double kPi = std::numbers::pi;

bool solidAt(const World& w, int x, int y, int z) { return blockInfo(stateId(w.block(x, y, z))).opaqueCube; }

double wrapPi(double a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}

/// El yaw que mira a lo largo de (mx, mz) y más se parece al que ya tiene. El modelo es simétrico (da igual de qué lado
/// se mire), así que al invertir el sentido no gira 180º: se queda como estaba.
float alignedYaw(float current, double mx, double mz) {
  double target = std::atan2(-mx, -mz);
  if (std::abs(wrapPi(target - current)) > kPi / 2) target = wrapPi(target + kPi);
  return static_cast<float>(target);
}

/// Rozamiento del tick: poco con alguien montado, mucho vacía; la de horno además se empuja sola.
void applyDrag(Minecart& c, bool rider) {
  if (c.type == CartType::Furnace) {
    double d = c.push.x * c.push.x + c.push.y * c.push.y;
    if (d > 1e-4) {
      d = std::sqrt(d);
      c.push /= d;
      c.motion.x = c.motion.x * 0.8 + c.push.x;
      c.motion.z = c.motion.z * 0.8 + c.push.y;
    } else {
      c.motion.x *= 0.98;
      c.motion.z *= 0.98;
    }
  }
  const double drag = rider ? 0.997 : 0.96;
  c.motion.x *= drag;
  c.motion.z *= drag;
  c.motion.y = 0;
}

}  // namespace

int cartItemId(CartType t) {
  switch (t) {
    case CartType::Chest: return ItemId::chest_minecart;
    case CartType::Furnace: return ItemId::furnace_minecart;
    case CartType::Tnt: return ItemId::tnt_minecart;
    default: return ItemId::minecart;
  }
}

int cartTypeFromItem(int itemId) {
  if (itemId == ItemId::minecart) return static_cast<int>(CartType::Normal);
  if (itemId == ItemId::chest_minecart) return static_cast<int>(CartType::Chest);
  if (itemId == ItemId::furnace_minecart) return static_cast<int>(CartType::Furnace);
  if (itemId == ItemId::tnt_minecart) return static_cast<int>(CartType::Tnt);
  return -1;
}

float cartPitchAt(const World& w, const glm::dvec3& pos, float yaw) {
  glm::ivec3 rp;
  if (!rails::railAt(w, pos.x, pos.y, pos.z, rp)) return 0.0f;
  const BlockState s = w.block(rp.x, rp.y, rp.z);
  glm::dvec2 uphill(0);
  switch (rails::shapeOf(stateId(s), stateMeta(s))) {
    case 2: uphill = {1, 0}; break;   // sube al este
    case 3: uphill = {-1, 0}; break;  // sube al oeste
    case 4: uphill = {0, -1}; break;  // sube al norte
    case 5: uphill = {0, 1}; break;   // sube al sur
    default: return 0.0f;
  }
  const double fx = -std::sin(yaw), fz = -std::cos(yaw);
  return static_cast<float>(fx * uphill.x + fz * uphill.y >= 0 ? kPi / 4 : -kPi / 4);
}

CartStep stepCart(Minecart& c, const World& w, const CartDriver& driver) {
  CartStep out;
  c.prevPos = c.pos;
  c.prevYaw = c.yaw;
  c.prevPitch = c.pitch;
  if (c.hurtTime > 0) c.hurtTime--;
  if (c.damage > 0) c.damage = std::max(0.0f, c.damage - 1.0f);
  c.motion.y -= 0.04;
  const double vmax = c.maxSpeed();

  glm::ivec3 rp;
  if (rails::railAt(w, c.pos.x, c.pos.y, c.pos.z, rp)) {
    const BlockState s = w.block(rp.x, rp.y, rp.z);
    const int id = stateId(s), meta = stateMeta(s);
    const int shape = rails::shapeOf(id, meta);
    const rails::OnRail lineBefore = rails::pointOnRail(rp, shape, c.pos.x, c.pos.z);
    out.onRail = true;
    out.rail = rp;
    out.railId = id;
    out.activatorPowered = id == rails::kActivator && rails::active(id, meta);
    c.fallDistance = 0;
    const bool powered = id == rails::kPowered && rails::active(id, meta);
    bool brake = id == rails::kPowered && !powered;
    // En una cuesta se mueve desde el nivel de arriba (así no choca con el bloque que sube junto a ella), y baja si sale por abajo
    c.pos.y = rp.y + kBase;
    switch (shape) {
      case 2: c.motion.x -= kSlope; c.pos.y += 1; break;
      case 3: c.motion.x += kSlope; c.pos.y += 1; break;
      case 4: c.motion.z += kSlope; c.pos.y += 1; break;
      case 5: c.motion.z -= kSlope; c.pos.y += 1; break;
      default: break;
    }
    // Solo se mueve a lo largo del raíl, en el sentido que llevaba
    const rails::Ends& e = rails::endsOf(shape);
    double dx = e.b.x - e.a.x, dz = e.b.z - e.a.z;
    const double len = std::hypot(dx, dz);
    if (c.motion.x * dx + c.motion.z * dz < 0) {
      dx = -dx;
      dz = -dz;
    }
    const double speed = std::min(std::hypot(c.motion.x, c.motion.z), 2.0);
    c.motion.x = speed * dx / len;
    c.motion.z = speed * dz / len;
    // Quien va montado la empuja si casi no se mueve
    if (driver.hasRider && driver.forward > 0) {
      if (c.motion.x * c.motion.x + c.motion.z * c.motion.z < 0.01) {
        c.motion.x += -std::sin(driver.yaw) * 0.1;
        c.motion.z += -std::cos(driver.yaw) * 0.1;
        brake = false;
      }
    }
    // Sobre un propulsor sin potencia frena en seco
    if (brake) {
      if (std::hypot(c.motion.x, c.motion.z) < 0.03) {
        c.motion.x = c.motion.z = 0;
      } else {
        c.motion.x *= 0.5;
        c.motion.z *= 0.5;
      }
    }
    // Se coloca sobre la línea del raíl y avanza (un bloque en medio la para)
    const rails::OnRail onLine = rails::pointOnRail(rp, shape, c.pos.x, c.pos.z);
    c.pos.x = onLine.x;
    c.pos.z = onLine.z;
    const double rf = driver.hasRider ? 0.75 : 1.0;
    glm::dvec3 step(std::clamp(c.motion.x * rf, -vmax, vmax), 0.0, std::clamp(c.motion.z * rf, -vmax, vmax));
    const double wanted = step.x * step.x + step.z * step.z;
    AABB box = c.box();
    const MoveResult mr = moveBox(w, box, step, 0.0, true);
    c.pos = {box.center().x, box.min.y, box.center().z};
    if (mr.collidedX) c.motion.x = 0;
    if (mr.collidedZ) c.motion.z = 0;
    out.collided = mr.collidedX || mr.collidedZ;
    if (out.collided) out.impactSq = wanted;
    // Si ha salido de la cuesta por el extremo de abajo, baja un nivel
    const int fx = static_cast<int>(std::floor(c.pos.x)) - rp.x, fz = static_cast<int>(std::floor(c.pos.z)) - rp.z;
    if (e.a.y != 0 && fx == e.a.x && fz == e.a.z) c.pos.y += e.a.y;
    else if (e.b.y != 0 && fx == e.b.x && fz == e.b.z) c.pos.y += e.b.y;
    applyDrag(c, driver.hasRider);
    // Su altura es la del raíl en el que ha quedado; sube o baja y gana o pierde velocidad según el desnivel
    glm::ivec3 rp2;
    if (rails::railAt(w, c.pos.x, c.pos.y, c.pos.z, rp2)) {
      const BlockState s2 = w.block(rp2.x, rp2.y, rp2.z);
      const rails::OnRail lineAfter = rails::pointOnRail(rp2, rails::shapeOf(stateId(s2), stateMeta(s2)), c.pos.x, c.pos.z);
      const double sp = std::hypot(c.motion.x, c.motion.z);
      if (sp > 0) {
        const double faster = sp + (lineBefore.y - lineAfter.y) * 0.05;
        c.motion.x = c.motion.x / sp * faster;
        c.motion.z = c.motion.z / sp * faster;
      }
      c.pos.y = lineAfter.y;
    }
    // Al pasar a otra celda sigue por donde ha salido
    const int cx = static_cast<int>(std::floor(c.pos.x)), cz = static_cast<int>(std::floor(c.pos.z));
    if (cx != rp.x || cz != rp.z) {
      const double sp = std::hypot(c.motion.x, c.motion.z);
      c.motion.x = sp * (cx - rp.x);
      c.motion.z = sp * (cz - rp.z);
    }
    // Un propulsor con potencia la acelera (y la arranca si está parada contra un bloque)
    if (powered) {
      const double sp = std::hypot(c.motion.x, c.motion.z);
      if (sp > 0.01) {
        c.motion.x += c.motion.x / sp * 0.06;
        c.motion.z += c.motion.z / sp * 0.06;
      } else if (shape == 1) {
        if (solidAt(w, rp.x - 1, rp.y, rp.z)) c.motion.x = 0.02;
        else if (solidAt(w, rp.x + 1, rp.y, rp.z)) c.motion.x = -0.02;
      } else if (shape == 0) {
        if (solidAt(w, rp.x, rp.y, rp.z - 1)) c.motion.z = 0.02;
        else if (solidAt(w, rp.x, rp.y, rp.z + 1)) c.motion.z = -0.02;
      }
    }
    c.onGround = false;
  } else {
    // Sin raíl: rueda suelta, con rozamiento y cayendo
    c.motion.x = std::clamp(c.motion.x, -vmax, vmax);
    c.motion.z = std::clamp(c.motion.z, -vmax, vmax);
    if (c.onGround) c.motion *= 0.5;
    AABB box = c.box();
    glm::dvec3 step = c.motion;
    const double wanted = step.x * step.x + step.z * step.z;
    const MoveResult mr = moveBox(w, box, step, 0.0, c.onGround);
    c.pos = {box.center().x, box.min.y, box.center().z};
    c.onGround = mr.onGround;
    if (mr.collidedX) c.motion.x = 0;
    if (mr.collidedZ) c.motion.z = 0;
    out.collided = mr.collidedX || mr.collidedZ;
    if (out.collided) out.impactSq = wanted;
    if (mr.collidedY) {
      if (c.motion.y < 0) {
        out.landed = c.fallDistance;
        c.fallDistance = 0;
      }
      c.motion.y = 0;
    } else if (step.y < 0) {
      c.fallDistance -= step.y;
    }
    if (!c.onGround) c.motion *= 0.95;
  }
  // Hacia dónde mira: a lo largo de lo que ha avanzado, y la inclinación de la cuesta
  const double mx = c.pos.x - c.prevPos.x, mz = c.pos.z - c.prevPos.z;
  if (mx * mx + mz * mz > 0.001) c.yaw = alignedYaw(c.yaw, mx, mz);
  c.pitch = cartPitchAt(w, c.pos, c.yaw);
  return out;
}

}  // namespace mcw
