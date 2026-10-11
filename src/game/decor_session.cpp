// Marcos, cuadros y soportes para armadura en la partida: colgarlos, usarlos, romperlos y mantenerlos (la pared que los
// sostiene, la caída del soporte). Comportamiento observable de 1.8; implementación propia.
#include <algorithm>
#include <cmath>

#include "data/blockstates.h"
#include "game/armor.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

/// Rayo contra una caja (método de las losas): distancia de entrada o nada.
std::optional<double> rayBox(const glm::dvec3& o, const glm::dvec3& d, const AABB& b) {
  double t0 = 0, t1 = 1e30;
  for (int i = 0; i < 3; i++) {
    if (std::abs(d[i]) < 1e-12) {
      if (o[i] < b.min[i] || o[i] > b.max[i]) return std::nullopt;
      continue;
    }
    double a = (b.min[i] - o[i]) / d[i], e = (b.max[i] - o[i]) / d[i];
    if (a > e) std::swap(a, e);
    t0 = std::max(t0, a);
    t1 = std::min(t1, e);
    if (t0 > t1) return std::nullopt;
  }
  return t0;
}

constexpr float kPiF = 3.14159265358979f;

}  // namespace

u32 GameSession::addDecor(Decor d) {
  d.id = nextDecorId_++;
  decor_.push_back(std::move(d));
  return decor_.back().id;
}

std::vector<Decor> GameSession::decorsInChunk(int cx, int cz, bool take) {
  auto in = [&](const Decor& d) {
    const glm::dvec3 p = d.kind == DecorKind::ArmorStand ? d.pos : d.center();
    return static_cast<int>(std::floor(p.x)) >> 4 == cx && static_cast<int>(std::floor(p.z)) >> 4 == cz;
  };
  std::vector<Decor> out;
  for (const Decor& d : decor_)
    if (!d.dead && in(d)) out.push_back(d);
  if (take) std::erase_if(decor_, in);
  return out;
}

std::optional<std::pair<u32, double>> GameSession::raycastDecor(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist) const {
  std::optional<std::pair<u32, double>> best;
  for (const Decor& d : decor_) {
    if (d.dead) continue;
    // Los marcos y cuadros son muy finos: se les da un margen para poder apuntarlos
    const auto t = rayBox(origin, dir, d.box().expand({0.02, 0.02, 0.02}));
    if (t && *t <= maxDist && (!best || *t < best->second)) best = std::pair{d.id, *t};
  }
  return best;
}

bool GameSession::placeDecor(const RayHit& hit) {
  ItemStack& held = player_.inventory.selected();
  if (held.empty()) return false;
  const int id = held.id;
  if (id != ItemId::item_frame && id != ItemId::painting && id != ItemId::armor_stand) return false;
  World& w = access_.world();
  const glm::ivec3 n(kFaceNormals[static_cast<std::size_t>(hit.face)][0], kFaceNormals[static_cast<std::size_t>(hit.face)][1],
                     kFaceNormals[static_cast<std::size_t>(hit.face)][2]);
  const glm::ivec3 cell = hit.block + n;
  std::vector<const Decor*> others;
  for (const Decor& d : decor_)
    if (!d.dead) others.push_back(&d);
  auto consume = [&] {
    if (!player_.creative() && --held.count <= 0) held.clear();
  };
  if (id == ItemId::armor_stand) {
    if (hit.face == Face::Down) return false;
    // Cabe de pie: la casilla y la de encima, sin bloques con colisión ni otro soporte
    Decor d;
    d.kind = DecorKind::ArmorStand;
    d.pos = {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
    // De cara a quien lo pone, en pasos de 45º
    const float a = player_.yaw + kPiF;
    d.yaw = std::round(a / (kPiF / 4.0f)) * (kPiF / 4.0f);
    const AABB b = d.box();
    if (boxHitsBlocks(w, b)) return false;
    for (const Decor* o : others)
      if (o->kind == DecorKind::ArmorStand && o->box().intersects(b)) return false;
    if (b.intersects(player_.box())) return false;
    addDecor(d);
    SessionEvent ev{SessionEvent::Type::DecorPlaced, cell, 0};
    ev.where = d.pos;
    events_.push_back(ev);
    consume();
    return true;
  }
  const int dir = decorDirFromFace(hit.face);
  if (dir < 0) return false;  // (por arriba y por abajo no se cuelga nada)
  Decor d;
  d.cell = cell;
  d.dir = static_cast<u8>(dir);
  if (id == ItemId::item_frame) {
    d.kind = DecorKind::ItemFrame;
    if (!hangingFits(w, cell, dir, 1, 1)) return false;
  } else {
    d.kind = DecorKind::Painting;
    const auto m = pickPainting(w, cell, dir, others, rng_.nextU32());
    if (!m) return false;
    d.motive = static_cast<u8>(*m);
  }
  // Sin bloques con colisión dentro y sin solapar otro marco o cuadro
  if (boxHitsBlocks(w, d.box())) return false;
  for (const Decor* o : others)
    if (decorOverlaps(d, *o)) return false;
  addDecor(d);
  SessionEvent ev{SessionEvent::Type::DecorPlaced, cell, 0};
  ev.where = d.center();
  events_.push_back(ev);
  consume();
  return true;
}

bool GameSession::useHeldOnDecor(Decor& d, const glm::dvec3& point) {
  if (d.dead) return false;
  ItemStack& held = player_.inventory.selected();
  auto event = [&](SessionEvent::Type t, const glm::dvec3& where) {
    SessionEvent ev{t, glm::ivec3(glm::floor(where)), 0};
    ev.where = where;
    events_.push_back(ev);
  };
  switch (d.kind) {
    case DecorKind::Painting: return false;
    case DecorKind::ItemFrame: {
      if (d.item.empty()) {
        if (held.empty()) return false;
        d.item = held;
        d.item.count = 1;
        d.rotation = 0;
        if (!player_.creative() && --held.count <= 0) held.clear();
      } else {
        d.rotation = static_cast<u8>((d.rotation + 1) & 7);
      }
      event(SessionEvent::Type::DecorTurned, d.center());
      if (decorListener_) decorListener_(d.id);
      return true;
    }
    case DecorKind::ArmorStand: {
      const int slot = held.empty() ? standSlotAtHeight(point.y - d.pos.y, d.small) : standSlotFor(held);
      if (slot < 0) return false;
      ItemStack& at = d.equip[static_cast<std::size_t>(slot)];
      if (held.empty()) {
        if (at.empty()) return false;
        held = at;
        at.clear();
      } else {
        ItemStack put = held;
        put.count = 1;
        const ItemStack old = at;
        at = put;
        if (!player_.creative()) {
          if (--held.count <= 0) held.clear();
          if (!old.empty()) {
            if (held.empty()) held = old;
            else if (const ItemStack rest = player_.inventory.add(old); !rest.empty()) spawnItem(player_.eyePos(), rest, {0, 0.1, 0}, 10);
          }
        }
      }
      event(SessionEvent::Type::DecorTurned, d.pos + glm::dvec3(0, 1.0, 0));
      if (decorListener_) decorListener_(d.id);
      return true;
    }
  }
  return false;
}

void GameSession::punchDecor(Decor& d) {
  if (d.dead) return;
  if (remote_) {
    if (remote_->useDecor) remote_->useDecor(d.id, true, glm::vec3(0));
    return;
  }
  auto event = [&](SessionEvent::Type t, const glm::dvec3& where) {
    SessionEvent ev{t, glm::ivec3(glm::floor(where)), 0};
    ev.where = where;
    events_.push_back(ev);
  };
  switch (d.kind) {
    case DecorKind::ItemFrame:
      if (!player_.creative() && !d.item.empty()) {  // el objeto sale y el marco se queda
        spawnItem(d.center() + glm::dvec3(0, 0.1, 0), d.item, {rng_.nextFloat() * 0.1 - 0.05, 0.1, rng_.nextFloat() * 0.1 - 0.05}, 10);
        d.item.clear();
        d.rotation = 0;
        event(SessionEvent::Type::DecorHit, d.center());
        if (decorListener_) decorListener_(d.id);
      } else {
        killDecor(d, !player_.creative());
      }
      break;
    case DecorKind::Painting: killDecor(d, !player_.creative()); break;
    case DecorKind::ArmorStand:
      if (player_.creative() || d.hurtTime > 0) {
        killDecor(d, !player_.creative());
      } else {
        d.hurtTime = 5;
        event(SessionEvent::Type::DecorHit, d.pos + glm::dvec3(0, 1.0, 0));
        if (decorListener_) decorListener_(d.id);
      }
      break;
  }
  player_.addExhaustion(0.3f);
}

void GameSession::killDecor(Decor& d, bool drops) {
  if (d.dead) return;
  d.dead = true;
  const glm::dvec3 at = d.kind == DecorKind::ArmorStand ? d.pos + glm::dvec3(0, 1.0, 0) : d.center();
  SessionEvent ev{SessionEvent::Type::DecorBroken, glm::ivec3(glm::floor(at)), 0};
  ev.where = at;
  events_.push_back(ev);
  if (drops && !suppressDrops_) {
    auto drop = [&](const ItemStack& s) {
      if (!s.empty()) spawnItem(at, s, {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
    };
    switch (d.kind) {
      case DecorKind::ItemFrame:
        drop(ItemStack(ItemId::item_frame));
        drop(d.item);
        break;
      case DecorKind::Painting: drop(ItemStack(ItemId::painting)); break;
      case DecorKind::ArmorStand:
        drop(ItemStack(ItemId::armor_stand));
        for (const ItemStack& s : d.equip) drop(s);
        break;
    }
  }
  if (decorListener_) decorListener_(d.id);
}

void GameSession::tickDecor() {
  if (decor_.empty() || remote_) return;  // (en un servidor, los adornos los mueve y los quita él)
  World& w = access_.world();
  decorTick_++;
  for (Decor& d : decor_) {
    if (d.dead) continue;
    if (d.hurtTime > 0) d.hurtTime--;
    if (d.kind == DecorKind::ArmorStand) {
      if (w.chunk(static_cast<int>(std::floor(d.pos.x)) >> 4, static_cast<int>(std::floor(d.pos.z)) >> 4) == nullptr) continue;
      // Cae si no hay suelo debajo
      AABB box = d.box();
      glm::dvec3 motion(0, d.fallSpeed - 0.04, 0);
      const MoveResult r = moveBox(w, box, motion, 0.0, true);
      d.pos.y = box.min.y;
      d.fallSpeed = r.collidedY ? 0.0 : motion.y * 0.98;
      if (d.pos.y < -64) d.dead = true;  // el vacío se lo traga
      continue;
    }
    // La pared que lo sostiene: si falta, se suelta al cabo de un segundo
    if ((decorTick_ + d.id) % 20 == 0 && w.chunk(d.cell.x >> 4, d.cell.z >> 4) != nullptr && !hangingSupported(w, d))
      killDecor(d, true);
  }
  std::erase_if(decor_, [](const Decor& d) { return d.dead; });
}

}  // namespace mcw
