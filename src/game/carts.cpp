// Vagonetas en la partida: ponerlas sobre un raíl, montarlas, golpearlas, el cofre, el horno y la dinamita, y moverlas
// cada tick (la física está en minecart.cpp). Comportamiento observable de 1.8; implementación propia.
#include <algorithm>
#include <cmath>

#include "data/blockstates.h"
#include "game/enchant_effects.h"
#include "game/rails.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

/// Cambia bloques por el camino de la partida (así los invitados también se enteran).
class SessionWorld : public WorldAccess {
 public:
  explicit SessionWorld(GameSession& s) : s_(s) {}
  World& world() override { return s_.access().world(); }
  void setBlock(int x, int y, int z, BlockState st) override { s_.setWorldBlock(x, y, z, st); }

 private:
  GameSession& s_;
};

/// Rayo contra una caja (método de las losas): distancia de entrada o nada.
std::optional<double> rayBoxEntry(const glm::dvec3& o, const glm::dvec3& d, const AABB& b) {
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

bool chunkLoaded(const World& w, const glm::dvec3& p) {
  return w.chunk(static_cast<int>(std::floor(p.x)) >> 4, static_cast<int>(std::floor(p.z)) >> 4) != nullptr;
}

}  // namespace

u32 GameSession::addCart(Minecart c, const ChestState* contents) {
  c.id = nextCartId_++;
  c.prevPos = c.pos;
  c.prevYaw = c.yaw;
  c.prevPitch = c.pitch;
  if (c.type == CartType::Chest) cartChests_[c.id] = contents ? *contents : ChestState{};
  carts_.push_back(c);
  return c.id;
}

void GameSession::removeCart(u32 id) {
  for (Player* p : activePlayers())
    if (p->mount == Player::Mount::Cart && p->mountId == id) dismount(*p);
  std::erase_if(carts_, [id](const Minecart& c) { return c.id == id; });
  cartChests_.erase(id);
  if (targetCart_ && *targetCart_ == id) targetCart_.reset();
  if (menu_ && openCart_ == id) closeMenu();
}

u32 GameSession::spawnCart(CartType type, const glm::ivec3& rail) {
  World& w = access_.world();
  const BlockState s = w.block(rail.x, rail.y, rail.z);
  if (!rails::isRail(stateId(s))) return 0;
  Minecart c;
  c.type = type;
  c.pos = {rail.x + 0.5, rail.y + 0.0625, rail.z + 0.5};
  // De cara a donde corre el raíl
  const rails::Ends& e = rails::endsOf(rails::shapeOf(stateId(s), stateMeta(s)));
  c.yaw = static_cast<float>(std::atan2(-static_cast<double>(e.b.x - e.a.x), -static_cast<double>(e.b.z - e.a.z)));
  const u32 id = addCart(c);
  SessionEvent ev{SessionEvent::Type::CartPlaced, rail, 0};
  ev.where = c.pos;
  events_.push_back(ev);
  return id;
}

std::optional<std::pair<u32, double>> GameSession::raycastCarts(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist) const {
  std::optional<std::pair<u32, double>> best;
  for (const Minecart& c : carts_) {
    if (c.dead || (player_.mount == Player::Mount::Cart && player_.mountId == c.id)) continue;
    const auto t = rayBoxEntry(origin, dir, c.box().expand({0.1, 0.1, 0.1}));
    if (t && *t <= maxDist && (!best || *t < best->second)) best = std::pair{c.id, *t};
  }
  return best;
}

std::vector<std::pair<Minecart, ChestState>> GameSession::cartsInChunk(int cx, int cz, bool take) {
  auto in = [&](const Minecart& c) { return static_cast<int>(std::floor(c.pos.x)) >> 4 == cx && static_cast<int>(std::floor(c.pos.z)) >> 4 == cz; };
  std::vector<std::pair<Minecart, ChestState>> out;
  for (const Minecart& c : carts_) {
    if (c.dead || !in(c)) continue;
    const auto it = cartChests_.find(c.id);
    out.emplace_back(c, it == cartChests_.end() ? ChestState{} : it->second);
  }
  if (take) {
    std::vector<u32> ids;
    for (const Minecart& c : carts_)
      if (in(c) && !(menu_ && openCart_ == c.id)) ids.push_back(c.id);
    for (u32 id : ids) {
      for (Player* p : activePlayers())
        if (p->mount == Player::Mount::Cart && p->mountId == id) p->mount = Player::Mount::None;
      std::erase_if(carts_, [id](const Minecart& c) { return c.id == id; });
      cartChests_.erase(id);
    }
  }
  return out;
}

bool GameSession::mountCart(Player& p, Minecart& c) {
  if (p.dead || c.dead) return false;
  for (Player* o : activePlayers())
    if (o != &p && o->mount == Player::Mount::Cart && o->mountId == c.id) return false;  // ya va alguien
  p.mount = Player::Mount::Cart;
  p.mountId = c.id;
  p.sprinting = false;
  p.motion = {0, 0, 0};
  p.cartStart = glm::ivec3(glm::floor(c.pos));
  c.rider = 1;
  return true;
}

void GameSession::dismount(Player& p) {
  if (p.mount == Player::Mount::None) return;
  if (p.mount == Player::Mount::Cart) {
    if (Minecart* c = cartById(p.mountId)) {
      c->rider = 0;
      // Se baja encima de la vagoneta
      p.pos = {c->pos.x, c->pos.y + Minecart::kHeight, c->pos.z};
      p.prevPos = p.pos;
    }
  }
  p.mount = Player::Mount::None;
  p.mountId = 0;
  p.cartStart.reset();
  p.motion = {0, 0, 0};
}

GameSession::UseResult GameSession::useHeldOnCart(Minecart& c) {
  using Kind = UseResult::Kind;
  ItemStack& held = player_.inventory.selected();
  if (c.dead) return {};
  switch (c.type) {
    case CartType::Normal: {
      if (mountCart(player_, c)) {
        SessionEvent ev{SessionEvent::Type::CartRide, glm::ivec3(glm::floor(c.pos)), 0};
        ev.where = c.pos;
        events_.push_back(ev);
        return {Kind::Used};
      }
      return {};
    }
    case CartType::Chest: {
      UseResult r{Kind::Chest};
      r.chest = cartItems(c.id);
      if (!r.chest) return {};
      r.cart = c.id;
      return r;
    }
    case CartType::Furnace: {
      if (held.id == ItemId::coal) {  // carbón o carbón vegetal: 3 minutos de combustible
        if (!player_.creative() && --held.count <= 0) held.clear();
        c.fuel += 3600;
      } else {
        return {};
      }
      c.push = {c.pos.x - player_.pos.x, c.pos.z - player_.pos.z};
      return {Kind::Used};
    }
    case CartType::Tnt: {
      if (held.id == ItemId::flint_and_steel && c.fuse < 0) {
        c.fuse = 80;
        if (!player_.creative()) {
          ItemStack& tool = player_.inventory.selected();
          if (tool.isTool()) enchfx::wearItem(tool, 1, rng_);
        }
        return {Kind::Used};
      }
      return {};
    }
  }
  return {};
}

void GameSession::punchCart(Minecart& c) {
  if (c.dead) return;
  if (remote_) {
    if (remote_->useCart) remote_->useCart(c.id, true);
    return;
  }
  const float amount = weaponDamage(player_.inventory.selected());
  c.shakeDir = -c.shakeDir;
  c.hurtTime = 10;
  c.damage += amount * 10.0f;
  SessionEvent ev{SessionEvent::Type::CartHit, glm::ivec3(glm::floor(c.pos)), 0};
  ev.where = c.pos;
  events_.push_back(ev);
  if (player_.creative() || c.damage > 40.0f) killCart(c, !player_.creative(), false);
  player_.addExhaustion(0.3f);
}

void GameSession::killCart(Minecart& c, bool drops, bool byExplosion) {
  if (c.dead) return;
  c.dead = true;
  for (Player* p : activePlayers())
    if (p->mount == Player::Mount::Cart && p->mountId == c.id) dismount(*p);
  SessionEvent ev{SessionEvent::Type::CartBroken, glm::ivec3(glm::floor(c.pos)), 0};
  ev.where = c.pos;
  events_.push_back(ev);
  auto drop = [&](const ItemStack& s) {
    spawnItem(c.pos + glm::dvec3(0, 0.3, 0), s, {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  };
  const double speedSq = c.motion.x * c.motion.x + c.motion.z * c.motion.z;
  if (drops && !suppressDrops_) {
    drop(ItemStack(ItemId::minecart));
    if (c.type == CartType::Chest) {
      drop(ItemStack(54));  // el cofre
      if (auto it = cartChests_.find(c.id); it != cartChests_.end())
        for (const ItemStack& s : it->second.items)
          if (!s.empty()) drop(s);
    } else if (c.type == CartType::Furnace) {
      drop(ItemStack(B::furnace));
    } else if (c.type == CartType::Tnt && !byExplosion) {
      drop(ItemStack(B::tnt));
    }
  }
  if (c.type == CartType::Tnt && (byExplosion || speedSq >= 0.01)) explodeCart(c, speedSq);
}

void GameSession::explodeCart(Minecart& c, double speedSq) {
  if (c.type != CartType::Tnt) return;
  c.dead = true;
  const double speed = std::min(std::sqrt(speedSq), 5.0);
  explode(c.pos, static_cast<float>(4.0 + rng_.next() * 1.5 * speed));
}

void GameSession::detectorPulse(const glm::ivec3& rail) {
  World& w = access_.world();
  const BlockState s = w.block(rail.x, rail.y, rail.z);
  if (stateId(s) != rails::kDetector) return;
  if (!(stateMeta(s) & 8)) {
    setWorldBlock(rail.x, rail.y, rail.z, makeState(rails::kDetector, stateMeta(s) | 8));
    redstoneNotify(rail);
    redstoneNotify(rail - glm::ivec3(0, 1, 0));
    events_.push_back({SessionEvent::Type::Click, rail, s});
  }
  schedule(rail, 20, TickKind::Detector);
}

/// Las vagonetas se empujan entre sí y con quien las roza (jugadores y criaturas), como en el juego.
void GameSession::pushCartsAndEntities() {
  const std::vector<Player*> players = activePlayers();
  for (std::size_t i = 0; i < carts_.size(); i++) {
    Minecart& a = carts_[i];
    if (a.dead) continue;
    const AABB zone = a.box().expand({0.2, 0.0, 0.2});
    auto nudge = [&](const glm::dvec3& other, glm::dvec3& otherMotion) {
      double dx = other.x - a.pos.x, dz = other.z - a.pos.z;
      double d2 = dx * dx + dz * dz;
      if (d2 < 1e-4) return;
      const double d = std::sqrt(d2);
      dx /= d;
      dz /= d;
      const double k = std::min(1.0, 1.0 / d) * 0.1 * 0.5;
      a.motion.x -= dx * k;
      a.motion.z -= dz * k;
      otherMotion.x += dx * k / 4.0;
      otherMotion.z += dz * k / 4.0;
    };
    for (Player* p : players)
      if (!p->dead && !(p->mount == Player::Mount::Cart && p->mountId == a.id) && p->box().intersects(zone)) nudge(p->pos, p->motion);
    for (Mob& m : mobs_)
      if (!m.dying() && m.box().intersects(zone)) nudge(m.pos, m.motion);
    for (std::size_t j = i + 1; j < carts_.size(); j++) {
      Minecart& b = carts_[j];
      if (b.dead || !b.box().intersects(zone)) continue;
      const double dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z;
      const double d2 = dx * dx + dz * dz;
      if (d2 < 1e-4) continue;
      const double d = std::sqrt(d2);
      double nx = dx / d, nz = dz / d;
      // Solo chocan de frente (a lo largo de la vía): si no, se ignoran
      const double ax = -std::sin(a.yaw), az = -std::cos(a.yaw);
      if (std::abs(nx * ax + nz * az) < 0.8) continue;
      const double k = std::min(1.0, 1.0 / d) * 0.1 * 0.5;
      const double ox = nx * k, oz = nz * k;
      double sx = (b.motion.x + a.motion.x), sz = (b.motion.z + a.motion.z);
      if (b.type == CartType::Furnace && a.type != CartType::Furnace) {
        a.motion.x *= 0.2;
        a.motion.z *= 0.2;
        a.motion.x += b.motion.x - ox;
        a.motion.z += b.motion.z - oz;
        b.motion.x *= 0.95;
        b.motion.z *= 0.95;
      } else if (a.type == CartType::Furnace && b.type != CartType::Furnace) {
        b.motion.x *= 0.2;
        b.motion.z *= 0.2;
        b.motion.x += a.motion.x + ox;
        b.motion.z += a.motion.z + oz;
        a.motion.x *= 0.95;
        a.motion.z *= 0.95;
      } else {
        sx /= 2.0;
        sz /= 2.0;
        a.motion.x *= 0.2;
        a.motion.z *= 0.2;
        a.motion.x += sx - ox;
        a.motion.z += sz - oz;
        b.motion.x *= 0.2;
        b.motion.z *= 0.2;
        b.motion.x += sx + ox;
        b.motion.z += sz + oz;
      }
    }
  }
}

void GameSession::tickCarts() {
  World& w = access_.world();
  const std::vector<Player*> players = activePlayers();
  for (Minecart& c : carts_) {
    if (c.dead) continue;
    if (!chunkLoaded(w, c.pos)) continue;  // sin chunk no hay vía: se queda quieta
    Player* rider = nullptr;
    for (Player* p : players)
      if (!p->dead && p->mount == Player::Mount::Cart && p->mountId == c.id) rider = p;
    c.rider = rider ? 1 : 0;
    if (c.type == CartType::Furnace) {
      if (c.fuel > 0) c.fuel--;
      if (c.fuel <= 0) c.push = {0, 0};
    }
    if (c.type == CartType::Tnt && c.fuse > 0) c.fuse--;
    CartDriver driver;
    if (rider) {
      driver.hasRider = true;
      driver.forward = rider->moveForward;
      driver.yaw = rider->yaw;
    }
    const CartStep st = stepCart(c, w, driver);
    if (st.onRail && st.railId == rails::kDetector) detectorPulse(st.rail);
    if (st.activatorPowered) {
      if (c.type == CartType::Tnt && c.fuse < 0) c.fuse = 80;
      if (rider) {  // el raíl activador baja a quien va montado
        dismount(*rider);
        rider = nullptr;
        c.hurtTime = 10;
        c.damage = 50.0f;
      }
    }
    if (c.type == CartType::Tnt) {
      const double sq = c.motion.x * c.motion.x + c.motion.z * c.motion.z;
      if (c.fuse == 0) explodeCart(c, sq);
      else if (st.collided && st.impactSq >= 0.01) explodeCart(c, st.impactSq);
      else if (st.landed >= 3.0) explodeCart(c, (st.landed / 10.0) * (st.landed / 10.0));
    }
    if (c.pos.y < -64) c.dead = true;
    // Quien va montado la acompaña
    if (rider && !c.dead) {
      rider->prevPos = rider->pos;
      rider->pos = {c.pos.x, c.pos.y - 0.35, c.pos.z};
      rider->motion = {0, 0, 0};
      rider->fallDistance = 0;
      rider->onGround = true;
      rider->sprinting = false;
      // Kilómetro de ruta: a más de 1000 bloques de donde se subió
      if (rider == &player_ && rider->cartStart) {
        const glm::ivec3 d = glm::ivec3(glm::floor(c.pos)) - *rider->cartStart;
        if (static_cast<long long>(d.x) * d.x + static_cast<long long>(d.y) * d.y + static_cast<long long>(d.z) * d.z >= 1000000LL) award(Ach::OnARail);
        achievements_.addStat("stat.minecartOneCm", static_cast<i64>(std::round(glm::length(c.pos - c.prevPos) * 100.0)));
      }
    }
  }
  pushCartsAndEntities();
  // Las que han explotado o se han ido
  std::vector<u32> gone;
  for (const Minecart& c : carts_)
    if (c.dead) gone.push_back(c.id);
  for (u32 id : gone) {
    // (las que se rompen con drops ya soltaron lo suyo en killCart; aquí solo se quitan)
    for (Player* p : players)
      if (p->mount == Player::Mount::Cart && p->mountId == id) p->mount = Player::Mount::None;
    std::erase_if(carts_, [id](const Minecart& c) { return c.id == id; });
    if (menu_ && openCart_ == id) closeMenu();
    cartChests_.erase(id);
    if (targetCart_ && *targetCart_ == id) targetCart_.reset();
  }
}

void GameSession::connectRail(const glm::ivec3& pos) {
  SessionWorld access(*this);
  rails::connect(access, pos);
}

}  // namespace mcw
