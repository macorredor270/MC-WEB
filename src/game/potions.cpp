// Efectos de estado, pociones (beber y lanzar), atril de pociones y caldero.
#include <algorithm>
#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "game/effects.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

bool undead(MobType t) {
  return t == MobType::Zombie || t == MobType::Skeleton || t == MobType::PigZombie || t == MobType::WitherSkeleton || t == MobType::EnderDragon;
}

}  // namespace

// --- Efectos -------------------------------------------------------------------------------------------

void GameSession::applyEffect(Player& p, int id, int amp, int ticks) {
  if (p.dead) return;
  switch (id) {
    case fx::InstantHealth: p.health = std::min(Player::kMaxHealth, p.health + static_cast<float>(4 << std::min(amp, 4))); break;
    case fx::InstantDamage: damagePlayer(p, static_cast<float>(6 << std::min(amp, 4)), p.pos, 0.0f, DamageKind::Generic); break;
    case fx::Saturation: p.food = std::min(20, p.food + amp + 1); p.saturation = std::min(static_cast<float>(p.food), p.saturation + static_cast<float>(amp + 1)); break;
    case fx::Absorption:
      p.effects.add(id, amp, ticks);
      p.absorption = std::max(p.absorption, 4.0f * static_cast<float>(amp + 1));
      break;
    default: p.effects.add(id, amp, ticks); break;
  }
}

void GameSession::applyEffect(Mob& m, int id, int amp, int ticks) {
  if (m.dying()) return;
  switch (id) {
    case fx::InstantHealth:
      if (undead(m.type)) hurtMob(m, static_cast<float>(6 << std::min(amp, 4)), m.pos, 0.0f, false);
      else m.health = std::min(m.info().maxHealth, m.health + static_cast<float>(4 << std::min(amp, 4)));
      break;
    case fx::InstantDamage:
      if (undead(m.type)) m.health = std::min(m.info().maxHealth, m.health + static_cast<float>(4 << std::min(amp, 4)));
      else hurtMob(m, static_cast<float>(6 << std::min(amp, 4)), m.pos, 0.0f, false);
      break;
    default: m.effects.add(id, amp, ticks); break;
  }
}

void GameSession::tickEffects() {
  auto step = [&](Effects& list, auto&& regen, auto&& poison, auto&& wither, auto&& hunger, auto&& saturate) {
    for (ActiveEffect& e : list.list) {
      switch (e.id) {
        case fx::Regeneration:
          if (e.ticks % std::max(1, 50 >> std::min(e.amp, 5)) == 0) regen();
          break;
        case fx::Poison:
          if (e.ticks % std::max(1, 25 >> std::min(e.amp, 5)) == 0) poison();
          break;
        case fx::Wither:
          if (e.ticks % std::max(1, 40 >> std::min(e.amp, 5)) == 0) wither();
          break;
        case fx::Hunger: hunger(0.025f * static_cast<float>(e.amp + 1)); break;
        case fx::Saturation: saturate(e.amp + 1); break;
        default: break;
      }
      e.ticks--;
    }
    std::erase_if(list.list, [](const ActiveEffect& e) { return e.ticks <= 0; });
  };
  for (Player* p : activePlayers()) {
    if (p->dead) {
      p->effects.clear();
      p->absorption = 0;
      continue;
    }
    step(
        p->effects, [&] { if (p->health < Player::kMaxHealth) p->health = std::min(Player::kMaxHealth, p->health + 1.0f); },
        [&] { if (p->health > 1.0f) p->damage(1.0f, false, DamageKind::Generic); }, [&] { p->damage(1.0f, false, DamageKind::Generic); },
        [&](float e) { p->addExhaustion(e); },
        [&](int n) { p->food = std::min(20, p->food + n); p->saturation = std::min(static_cast<float>(p->food), p->saturation + static_cast<float>(n)); });
    if (!p->effects.has(fx::Absorption)) p->absorption = 0;
    if (p->health <= 0 && !p->dead) {
      p->dead = true;
      if (p == &player_) onPlayerDeath();
    }
  }
  for (Mob& m : mobs_) {
    if (m.dying() || m.effects.list.empty()) continue;
    step(
        m.effects, [&] { if (m.health < m.info().maxHealth) m.health = std::min(m.info().maxHealth, m.health + 1.0f); },
        [&] { if (m.health > 1.0f) m.health -= 1.0f; }, [&] { hurtMob(m, 1.0f, m.pos, 0.0f, false); }, [](float) {}, [](int) {});
  }
}

// --- Beber -----------------------------------------------------------------------------------------------

bool GameSession::finishDrinking() {
  ItemStack& held = player_.inventory.selected();
  if (held.id == ItemId::milk_bucket) {
    player_.effects.clear();
    player_.absorption = 0;
    if (!player_.creative()) held = ItemStack(ItemId::bucket);
    return true;
  }
  if (held.id != ItemId::potion) return false;
  for (const ActiveEffect& e : potion::effectsOf(held.meta)) applyEffect(player_, e.id, e.amp, e.ticks);
  if (!player_.creative()) held = ItemStack(ItemId::glass_bottle);
  return true;
}

void GameSession::splashPotion(const glm::dvec3& at, int meta) {
  const auto effects = potion::effectsOf(meta);
  const AABB zone = AABB{at - glm::dvec3(4, 2, 4), at + glm::dvec3(4, 2, 4)};
  auto intensityFor = [&](const AABB& box) {
    if (!box.intersects(zone)) return 0.0;
    const glm::dvec3 c = box.center();
    const double d = glm::length(c - at);
    return d >= 4.0 ? 0.0 : 1.0 - d / 4.0;
  };
  for (Player* p : activePlayers()) {
    const double k = intensityFor(p->box());
    if (k <= 0) continue;
    for (const ActiveEffect& e : effects)
      applyEffect(*p, e.id, e.amp, fx::instant(e.id) ? 1 : static_cast<int>(k * e.ticks + 0.5));
  }
  for (std::size_t i = 0; i < mobs_.size(); i++) {
    const double k = intensityFor(mobs_[i].box());
    if (k <= 0 || mobs_[i].dying()) continue;
    for (const ActiveEffect& e : effects) {
      const std::size_t before = mobs_.size();
      applyEffect(mobs_[i], e.id, e.amp, fx::instant(e.id) ? 1 : static_cast<int>(k * e.ticks + 0.5));
      if (mobs_.size() != before) break;
    }
  }
  SessionEvent ev{SessionEvent::Type::PotionShatter, glm::ivec3(glm::floor(at)), 0};
  ev.where = at;
  ev.value = static_cast<int>(potion::color(meta));
  events_.push_back(ev);
}

// --- Atril de pociones -------------------------------------------------------------------------------------

void GameSession::tickBrewing() {
  World& w = access_.world();
  for (auto& [key, chest] : chests_) {
    const auto [x, y, z] = key;
    if (stateId(w.block(x, y, z)) != 117) continue;
    ItemStack& ingredient = chest.items[3];
    auto usable = [&](const ItemStack& bottle) { return bottle.id == ItemId::potion && potion::brew(bottle.meta, ingredient.id, ingredient.meta) >= 0; };
    const bool can = !ingredient.empty() && (usable(chest.items[0]) || usable(chest.items[1]) || usable(chest.items[2]));
    if (!can) {
      chest.brewTime = 0;
    } else if (chest.brewTime == 0) {
      chest.brewTime = 400;
    } else if (--chest.brewTime == 0) {
      for (int i = 0; i < 3; i++)
        if (usable(chest.items[static_cast<std::size_t>(i)])) chest.items[static_cast<std::size_t>(i)].meta = static_cast<i16>(potion::brew(chest.items[static_cast<std::size_t>(i)].meta, ingredient.id, ingredient.meta));
      if (--ingredient.count <= 0) ingredient.clear();
      events_.push_back({SessionEvent::Type::Fizz, glm::ivec3(x, y, z), 0});
      award(Ach::Potion);
    }
    // Las botellas que lleva se ven en el bloque (bits 0 a 2 del metadato)
    int bits = 0;
    for (int i = 0; i < 3; i++)
      if (!chest.items[static_cast<std::size_t>(i)].empty()) bits |= 1 << i;
    const BlockState cur = w.block(x, y, z);
    if (stateMeta(cur) != bits) setWorldBlock(x, y, z, makeState(117, bits));
  }
}

// --- Caldero --------------------------------------------------------------------------------------------------

bool GameSession::useCauldron(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  if (stateId(s) != 118) return false;
  const int level = stateMeta(s);
  ItemStack& held = player_.inventory.selected();
  auto setLevel = [&](int n) { setWorldBlock(p.x, p.y, p.z, makeState(118, std::clamp(n, 0, 3))); };
  auto give = [&](const ItemStack& what) {
    if (held.count <= 1) {
      held = what;
    } else {
      held.count--;
      const ItemStack rest = player_.inventory.add(what);
      if (!rest.empty()) throwItem(rest);
    }
  };
  if (held.id == ItemId::water_bucket && level < 3) {
    setLevel(3);
    if (!player_.creative()) held = ItemStack(ItemId::bucket);
    events_.push_back({SessionEvent::Type::BucketEmptied, p, 0});
    return true;
  }
  if (held.id == ItemId::bucket && level == 3) {
    setLevel(0);
    if (!player_.creative()) give(ItemStack(ItemId::water_bucket));
    events_.push_back({SessionEvent::Type::BucketFilled, p, 0});
    return true;
  }
  if (held.id == ItemId::glass_bottle && level > 0) {
    setLevel(level - 1);
    if (!player_.creative()) give(ItemStack(ItemId::potion, 1, 0));
    events_.push_back({SessionEvent::Type::BucketFilled, p, 0});
    return true;
  }
  if (held.id == ItemId::potion && held.meta == 0 && level < 3) {
    setLevel(level + 1);
    if (!player_.creative()) held = ItemStack(ItemId::glass_bottle);
    events_.push_back({SessionEvent::Type::BucketEmptied, p, 0});
    return true;
  }
  if (level > 0) {
    // Lavar: el cuero pierde su tinte y el estandarte, su último dibujo
    if (held.extra && held.extra->color >= 0 && (held.id >= ItemId::leather_helmet && held.id <= ItemId::leather_boots)) {
      ItemExtra e = *held.extra;
      e.color = -1;
      held.setExtra(e);
      setLevel(level - 1);
      return true;
    }
    if (held.id == ItemId::banner && held.extra && !held.extra->patterns.empty()) {
      ItemExtra e = *held.extra;
      e.patterns.pop_back();
      held.setExtra(e);
      setLevel(level - 1);
      return true;
    }
  }
  return false;
}

}  // namespace mcw
