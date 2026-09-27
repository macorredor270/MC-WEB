#include "game/session.h"

#include <cmath>
#include <deque>
#include <set>

#include "game/rules.h"
#include "world/world.h"

namespace mcw {

GameSession::GameSession(WorldAccess& access, u64 seed) : access_(access), rng_(seed ^ 0xC0FFEEull) {}

void GameSession::setMode(GameMode m) {
  player_.mode = m;
  if (m == GameMode::Survival) player_.flying = false;
  breakPos_.reset();
  breakProgress_ = 0;
}

void GameSession::respawn() {
  player_.respawn(spawn_);
  closeMenu();
}

std::optional<BreakState> GameSession::breaking() const {
  if (!breakPos_ || breakProgress_ <= 0) return std::nullopt;
  return BreakState{*breakPos_, breakProgress_};
}

void GameSession::openInventory() {
  menu_ = std::make_unique<Menu>(player_.creative() ? MenuKind::Creative : MenuKind::Inventory, player_);
}

void GameSession::closeMenu() {
  if (!menu_) return;
  std::vector<ItemStack> dropped;
  menu_->close(dropped);
  for (const ItemStack& s : dropped) throwItem(s);
  menu_.reset();
}

void GameSession::menuClickOutside(int button) {
  if (!menu_) return;
  std::vector<ItemStack> dropped;
  menu_->clickOutside(button, dropped);
  for (const ItemStack& s : dropped) throwItem(s);
}

void GameSession::throwItem(const ItemStack& s) {
  if (s.empty()) return;
  const glm::dvec3 dir = player_.lookDir();
  const glm::dvec3 at = player_.eyePos() - glm::dvec3(0, 0.3, 0);
  spawnItem(at, s, dir * 0.3 + glm::dvec3(0, 0.1, 0), 40);
}

void GameSession::spawnItem(const glm::dvec3& at, const ItemStack& s, const glm::dvec3& motion, int pickupDelay) {
  ItemEntity e;
  e.stack = s;
  e.pos = e.prevPos = at;
  e.motion = motion;
  e.pickupDelay = pickupDelay;
  e.bobOffset = rng_.nextFloat() * 6.28f;
  items_.push_back(e);
}

void GameSession::setAndUpdate(int x, int y, int z, BlockState s) {
  access_.setBlock(x, y, z, s);
  neighborUpdates({x, y, z});
}

void GameSession::neighborUpdates(const glm::ivec3& origin) {
  // Cola de actualizaciones: plantas sin suelo, antorchas sin apoyo, arena/grava que cae...
  World& w = access_.world();
  std::deque<glm::ivec3> queue;
  auto push6 = [&](const glm::ivec3& p) {
    queue.push_back(p);
    for (const auto& n : kFaceNormals) queue.push_back(p + glm::ivec3(n[0], n[1], n[2]));
  };
  push6(origin);
  for (int guard = 0; guard < 4096 && !queue.empty(); guard++) {
    const glm::ivec3 p = queue.front();
    queue.pop_front();
    if (p.y < 0 || p.y >= kChunkHeight) continue;
    const BlockState s = w.block(p.x, p.y, p.z);
    const int id = stateId(s);
    if (id == B::air) continue;
    if (!canStay(w, p.x, p.y, p.z, s)) {
      breakBlock(p, false);
      push6(p);
      continue;
    }
    if ((id == B::sand || id == B::gravel) && p.y > 0) {
      int y = p.y;
      while (y > 0 && isReplaceable(w.block(p.x, y - 1, p.z))) y--;
      if (y != p.y) {
        access_.setBlock(p.x, p.y, p.z, 0);
        access_.setBlock(p.x, y, p.z, s);
        push6(p);
        push6({p.x, y, p.z});
      }
    }
  }
}

void GameSession::breakBlock(const glm::ivec3& p, bool byPlayer) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s);
  if (id == B::air) return;
  const glm::dvec3 center = glm::dvec3(p) + 0.5;

  // Contenido del horno
  if (id == B::furnace || id == B::lit_furnace) {
    auto it = furnaces_.find({p.x, p.y, p.z});
    if (it != furnaces_.end()) {
      for (const ItemStack* st : {&it->second.input, &it->second.fuel, &it->second.output})
        if (!st->empty()) spawnItem(center, *st, {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
      furnaces_.erase(it);
    }
    if (menu_ && menu_->kind() == MenuKind::Furnace) closeMenu();
  }

  const bool drops = !byPlayer || !player_.creative();
  if (drops) {
    const ItemStack tool = byPlayer ? player_.inventory.selected() : ItemStack();
    for (const ItemStack& d : blockDrops(s, tool, rng_))
      spawnItem(center - glm::dvec3(0, 0.25, 0) + glm::dvec3(rng_.nextFloat() * 0.5 - 0.25, 0, rng_.nextFloat() * 0.5 - 0.25), d,
                {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  }
  access_.setBlock(p.x, p.y, p.z, 0);
  events_.push_back({SessionEvent::Type::BlockBroken, p, s});
  // Plantas dobles: la otra mitad también desaparece
  if (id == B::double_plant) {
    const glm::ivec3 other = p + glm::ivec3(0, (stateMeta(s) & 8) ? -1 : 1, 0);
    if (stateId(w.block(other.x, other.y, other.z)) == B::double_plant) access_.setBlock(other.x, other.y, other.z, 0);
  }
  neighborUpdates(p);
}

void GameSession::damageTool(int amount) {
  ItemStack& t = player_.inventory.selected();
  if (player_.creative() || !t.isTool()) return;
  t.meta = static_cast<i16>(t.meta + amount);
  if (t.meta >= itemInfo(t.id).maxDurability) t.clear();  // se rompe
}

void GameSession::updateTarget(const TickInput& in) {
  const glm::dvec3 dir = in.aimDir.value_or(player_.lookDir());
  target_ = player_.dead ? std::nullopt : raycastBlocks(access_.world(), player_.eyePos(), dir, reach());
}

void GameSession::handleAttack(const TickInput& in) {
  if (!target_ || !in.attack) {
    breakPos_.reset();
    breakProgress_ = 0;
    if (!in.attack) breakDelay_ = 0;
    return;
  }
  const glm::ivec3 p = target_->block;
  if (breakDelay_ > 0) {
    breakDelay_--;
    return;
  }
  if (player_.creative()) {
    if (in.attackPressed || breakDelay_ == 0) {
      // En creativo no se rompe nada con la espada en la mano (como en el juego)
      const int held = player_.inventory.selected().id;
      if (held == ItemId::wooden_sword || held == ItemId::stone_sword || held == ItemId::iron_sword ||
          held == ItemId::golden_sword || held == ItemId::diamond_sword)
        return;
      breakBlock(p, true);
      breakDelay_ = 5;
    }
    return;
  }
  const BlockState s = access_.world().block(p.x, p.y, p.z);
  if (!breakPos_ || *breakPos_ != p) {
    breakPos_ = p;
    breakProgress_ = 0;
  }
  breakProgress_ += digProgressPerTick(s, player_.inventory.selected(), player_.onGround, player_.headInWater);
  if (breakProgress_ >= 0.9999f) {  // margen por redondeo: la piedra a mano tarda 150 ticks justos
    const float hardness = blockInfo(stateId(s)).hardness;
    breakBlock(p, true);
    player_.addExhaustion(0.025f);
    if (hardness > 0) {
      const int held = player_.inventory.selected().id;
      const bool sword = held == ItemId::wooden_sword || held == ItemId::stone_sword || held == ItemId::iron_sword ||
                         held == ItemId::golden_sword || held == ItemId::diamond_sword;
      damageTool(sword ? 2 : 1);
    }
    breakPos_.reset();
    breakProgress_ = 0;
    breakDelay_ = 5;
  }
}

void GameSession::handleUse(const TickInput& in) {
  ItemStack& held = player_.inventory.selected();
  // Comer: mantener pulsado 32 ticks
  if (in.use && !player_.creative()) {
    if (auto food = foodValue(held); food && player_.food < 20) {
      if (++eatTicks_ >= 32) {
        player_.eat(food->food, food->saturation);
        if (--held.count <= 0) held.clear();
        eatTicks_ = 0;
      }
      return;
    }
  }
  eatTicks_ = 0;
  if (useDelay_ > 0) useDelay_--;
  if (!(in.usePressed || (in.use && useDelay_ == 0)) || !target_) return;
  useDelay_ = 4;

  World& w = access_.world();
  const glm::ivec3 tb = target_->block;
  const int targetId = stateId(w.block(tb.x, tb.y, tb.z));
  if (!player_.sneaking) {
    if (targetId == B::crafting_table) {
      menu_ = std::make_unique<Menu>(MenuKind::Crafting, player_);
      return;
    }
    if (targetId == B::furnace || targetId == B::lit_furnace) {
      FurnaceState& f = furnaces_[{tb.x, tb.y, tb.z}];
      menu_ = std::make_unique<Menu>(MenuKind::Furnace, player_, &f);
      return;
    }
  }
  if (held.empty()) return;
  glm::ivec3 pos;
  const auto state = placementFor(w, held, *target_, player_.yaw, pos);
  if (!state) return;
  // No colocar un bloque sólido donde está el jugador
  const auto boxes = collisionBoxes(stateId(*state), stateMeta(*state));
  for (const Box& b : boxes) {
    const AABB bb{{pos.x + b.x0, pos.y + b.y0, pos.z + b.z0}, {pos.x + b.x1, pos.y + b.y1, pos.z + b.z1}};
    if (bb.intersects(player_.box())) return;
  }
  access_.setBlock(pos.x, pos.y, pos.z, *state);
  if (stateId(*state) == B::double_plant) access_.setBlock(pos.x, pos.y + 1, pos.z, makeState(B::double_plant, 8));
  events_.push_back({SessionEvent::Type::BlockPlaced, pos, *state});
  if (!player_.creative() && --held.count <= 0) held.clear();
  neighborUpdates(pos);
}

void GameSession::tickItems() {
  World& w = access_.world();
  const AABB pickup = player_.box().expand({1.0, 0.5, 1.0});
  for (auto& e : items_) {
    e.prevPos = e.pos;
    e.age++;
    if (e.pickupDelay > 0) e.pickupDelay--;
    const bool inWater = isWater(stateId(w.block(static_cast<int>(std::floor(e.pos.x)), static_cast<int>(std::floor(e.pos.y)),
                                                  static_cast<int>(std::floor(e.pos.z)))));
    if (inWater) e.motion.y = std::min(e.motion.y + 0.01, 0.06);
    else e.motion.y -= 0.04;
    AABB b = AABB::centered(e.pos, 0.25, 0.25);
    glm::dvec3 m = e.motion;
    const MoveResult r = moveBox(w, b, m, 0.0, e.onGround);
    e.pos = {b.center().x, b.min.y, b.center().z};
    e.onGround = r.onGround;
    const double f = e.onGround ? 0.6 * 0.98 : 0.98;
    e.motion.x = (r.collidedX ? 0 : e.motion.x) * f;
    e.motion.z = (r.collidedZ ? 0 : e.motion.z) * f;
    e.motion.y = (r.collidedY ? 0 : e.motion.y) * 0.98;
    if (e.onGround) e.motion.y *= -0.5;

    // Recoger
    if (e.pickupDelay == 0 && !player_.dead && AABB::centered(e.pos, 0.25, 0.25).intersects(pickup)) {
      const int before = e.stack.count;
      e.stack = player_.inventory.add(e.stack);
      if (e.stack.count != before) events_.push_back({SessionEvent::Type::ItemPickedUp, glm::ivec3(e.pos), 0});
    }
  }
  // Juntar ítems iguales cercanos y quitar los recogidos o muy viejos (5 minutos)
  for (std::size_t i = 0; i < items_.size(); i++)
    for (std::size_t j = i + 1; j < items_.size(); j++) {
      ItemEntity& a = items_[i];
      ItemEntity& b = items_[j];
      if (a.stack.empty() || b.stack.empty() || !a.stack.stacksWith(b.stack)) continue;
      if (glm::length(a.pos - b.pos) > 0.5 || a.stack.count + b.stack.count > a.stack.maxStack()) continue;
      a.stack.count = static_cast<i16>(a.stack.count + b.stack.count);
      b.stack.clear();
    }
  std::erase_if(items_, [](const ItemEntity& e) { return e.stack.empty() || e.age > 6000 || e.pos.y < -64; });
}

void GameSession::tickFurnaces() {
  World& w = access_.world();
  for (auto& [key, f] : furnaces_) {
    if (!f.tick()) continue;
    // Encendido/apagado: cambia entre horno y horno encendido (que da luz)
    const auto [x, y, z] = key;
    const BlockState s = w.block(x, y, z);
    const int id = stateId(s);
    if (id != B::furnace && id != B::lit_furnace) continue;
    access_.setBlock(x, y, z, makeState(f.burning() ? B::lit_furnace : B::furnace, stateMeta(s)));
  }
}

void GameSession::tick(const TickInput& in) {
  const bool wasDead = player_.dead;  // se puede morir por una caída durante el movimiento
  player_.yaw = in.yaw;
  player_.pitch = in.pitch;
  const bool menuOpen = menu_ != nullptr;
  MoveInput move = menuOpen ? MoveInput{} : in.move;
  player_.tickMovement(access_.world(), move, menuOpen ? false : in.jumpPressed);

  if (in.selectSlot >= 0) player_.inventory.select(in.selectSlot);
  updateTarget(in);
  if (!menuOpen && !player_.dead) {
    handleAttack(in);
    handleUse(in);
    if (in.drop || in.dropStack) {
      ItemStack& held = player_.inventory.selected();
      if (!held.empty()) {
        ItemStack out = held;
        out.count = static_cast<i16>(in.dropStack ? held.count : 1);
        held.count = static_cast<i16>(held.count - out.count);
        if (held.count <= 0) held.clear();
        throwItem(out);
      }
    }
  } else {
    breakPos_.reset();
    breakProgress_ = 0;
  }
  tickItems();
  tickFurnaces();

  const float hpBefore = player_.health;
  player_.tickStatus(access_.world());
  if (player_.health < hpBefore) events_.push_back({SessionEvent::Type::PlayerHurt, {}, 0});
  if (player_.dead && !wasDead) {
    // Al morir se sueltan todos los objetos
    for (int i = 0; i < PlayerInventory::kSize; i++) {
      ItemStack& s = player_.inventory.slot(i);
      if (s.empty()) continue;
      spawnItem(player_.pos + glm::dvec3(0, 1, 0), s, {rng_.nextFloat() * 0.4 - 0.2, 0.3, rng_.nextFloat() * 0.4 - 0.2}, 40);
      s.clear();
    }
    closeMenu();
    events_.push_back({SessionEvent::Type::PlayerDied, {}, 0});
  }
}

}  // namespace mcw
