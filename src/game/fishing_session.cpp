// El anzuelo en la partida: lanzarlo, que flote y pique un pez, y recogerlo.
#include <algorithm>
#include <cmath>
#include <optional>

#include "data/blocks.h"
#include "data/items.h"
#include "game/enchant_effects.h"
#include "game/enchantments.h"
#include "game/physics.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

namespace {

/// Altura del agua en la casilla (una fuente llega a 8/9 de bloque).
double waterSurface(BlockState s, int y) {
  const int level = stateMeta(s) & 7;
  return y + 1.0 - (level + 1) / 9.0;
}

}  // namespace

FishingHook* GameSession::hookOf(u32 owner) {
  for (FishingHook& h : hooks_)
    if (h.owner == owner) return &h;
  return nullptr;
}

bool GameSession::useFishingRod() {
  ItemStack& rod = player_.inventory.selected();
  if (rod.id != ItemId::fishing_rod || player_.dead) return false;
  if (FishingHook* h = hookOf(player_.uid)) {
    reelHook(*h);
    useDelay_ = 4;
    return true;
  }
  const glm::dvec3 look(-std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), -std::cos(player_.yaw) * std::cos(player_.pitch));
  FishingHook h;
  h.id = nextHookId_++;
  h.owner = player_.uid;
  h.pos = h.prevPos = player_.eyePos() - glm::dvec3(0, 0.1, 0) + look * 0.4;
  h.motion = look * 1.5 + glm::dvec3(rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5, rng_.nextFloat() - 0.5) * 0.06;
  h.luck = rod.enchantLevel(Ench::LuckOfTheSea);
  h.lure = rod.enchantLevel(Ench::Lure);
  h.ownerHand = player_.pos + glm::dvec3(0, 1.3, 0);
  hooks_.push_back(h);
  useDelay_ = 4;
  events_.push_back({SessionEvent::Type::HookCast, glm::ivec3(glm::floor(h.pos)), 0, h.pos});
  return true;
}

void GameSession::reelHook(FishingHook& h) {
  int wear = 0;
  bool caught = false;
  const glm::dvec3 to = player_.eyePos() - h.pos;
  const double dist = glm::length(to);
  if (h.state == FishingHook::State::Bobbing && h.nibble > 0) {
    ItemStack loot = fishingLoot(rng_, h.luck, h.lure);
    if (!loot.empty()) {
      spawnItem(h.pos, loot, to * 0.1 + glm::dvec3(0, std::sqrt(dist) * 0.08, 0), 0);
      spawnXp(player_.pos + glm::dvec3(0, 0.5, 0), rng_.range(1, 6));
      caught = true;
    }
    wear = 1;
  } else if (h.state == FishingHook::State::Hooked) {
    for (Mob& m : mobs_)
      if (m.id == h.hookedMob) {
        const glm::dvec3 pull = player_.pos - m.pos;
        m.motion += pull * 0.1 + glm::dvec3(0, std::sqrt(glm::length(pull)) * 0.08, 0);
      }
    wear = 3;
  } else if (h.state == FishingHook::State::Ground) {
    wear = 2;
  }
  events_.push_back({SessionEvent::Type::HookReel, glm::ivec3(glm::floor(h.pos)), 0, h.pos, MobType::Pig, caught ? 1 : 0});
  const u32 id = h.id;
  std::erase_if(hooks_, [id](const FishingHook& x) { return x.id == id; });
  if (wear > 0) damageTool(wear);
}

void GameSession::tickHooks() {
  if (hooks_.empty()) return;
  World& w = access_.world();
  auto blockAt = [&](const glm::dvec3& p) {
    return w.block(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z)));
  };
  for (FishingHook& h : hooks_) {
    h.prevPos = h.pos;
    h.life++;
    if (h.remote) continue;
    // El del jugador de esta partida se suelta si cambia de objeto, muere o se aleja
    if (h.owner == player_.uid) {
      h.ownerHand = player_.pos + glm::dvec3(0, 1.3, 0);
      if (player_.inventory.selected().id != ItemId::fishing_rod || player_.dead || glm::distance(player_.pos, h.pos) > 32.0) {
        h.life = 1 << 30;
        continue;
      }
    }
    switch (h.state) {
      case FishingHook::State::Flying: {
        const double len = glm::length(h.motion);
        double reach = len;  // hasta dónde llega este tick sin chocar con un bloque
        std::optional<RayHit> blockHit;
        if (len > 1e-6)
          if (const auto r = raycastBlocks(w, h.pos, h.motion / len, len)) {
            blockHit = r;
            reach = glm::distance(r->point, h.pos);
          }
        // ¿Cruza agua antes de chocar? (un anzuelo rápido no se salta un estanque poco profundo)
        if (len > 1e-6) {
          for (double d = 0.2; d <= reach + 1e-9; d += 0.2) {
            const glm::dvec3 q = h.pos + h.motion / len * std::min(d, reach);
            const BlockState here = blockAt(q);
            if (isWater(stateId(here))) {
              h.pos = q;
              h.state = FishingHook::State::Bobbing;
              h.surface = waterSurface(here, static_cast<int>(std::floor(q.y)));
              h.motion = {0, 0, 0};
              h.pos.y = h.surface - 0.1;
              events_.push_back({SessionEvent::Type::HookSplash, glm::ivec3(glm::floor(h.pos)), 0, h.pos});
              break;
            }
          }
          if (h.state == FishingHook::State::Bobbing) break;
        }
        if (blockHit) {
          h.pos = blockHit->point;
          h.motion = {0, 0, 0};
          h.state = FishingHook::State::Ground;
          break;
        }
        if (h.life > 1) {
          const AABB box = AABB::centered(h.pos + h.motion * 0.5, 0.25, 0.25);
          bool hooked = false;
          for (Mob& m : mobs_)
            if (!m.dying() && m.box().expand({0.1, 0.1, 0.1}).intersects(box)) {
              h.state = FishingHook::State::Hooked;
              h.hookedMob = m.id;
              hurtMob(m, 0.0001f, player_.pos, 0.4f, h.owner == player_.uid);
              hooked = true;
              break;
            }
          if (hooked) break;
        }
        h.pos += h.motion;
        h.motion *= 0.92;
        h.motion.y -= 0.04;
        break;
      }
      case FishingHook::State::Bobbing: {
        const glm::ivec3 b(static_cast<int>(std::floor(h.pos.x)), static_cast<int>(std::floor(h.pos.y + 0.2)), static_cast<int>(std::floor(h.pos.z)));
        if (!isWater(stateId(w.block(b.x, b.y, b.z)))) {  // se ha quedado sin agua
          h.state = FishingHook::State::Flying;
          break;
        }
        if (h.nibble > 0) {
          if (--h.nibble == 0) h.waited = false;  // se escapó: vuelta a esperar
        } else {
          if (!h.waited) {
            h.wait = fishingWaitTicks(rng_, h.lure);
            h.waited = true;
          }
          // Sin cielo encima la espera va a la mitad de velocidad; con lluvia encima, algo más deprisa
          bool sky = true;
          for (int y = b.y + 1; y < b.y + 24 && sky; y++) sky = !blockInfo(stateId(w.block(b.x, y, b.z))).opaqueCube;
          int dec = 1;
          if (!sky && rng_.nextInt(2) == 0) dec = 0;
          if (sky && weather_.raining && rng_.nextInt(4) == 0) dec++;
          h.wait -= dec;
          if (h.wait <= 0) {
            h.nibble = rng_.range(20, 40);
            h.dip = 0.3;
            events_.push_back({SessionEvent::Type::HookSplash, glm::ivec3(glm::floor(h.pos)), 0, h.pos, MobType::Pig, 1});
          }
        }
        h.dip *= 0.85;
        h.pos.y = h.surface - 0.1 - h.dip;
        break;
      }
      case FishingHook::State::Hooked: {
        Mob* target = nullptr;
        for (Mob& m : mobs_)
          if (m.id == h.hookedMob && !m.dying()) target = &m;
        if (!target) {
          h.state = FishingHook::State::Flying;
          h.motion = {0, 0, 0};
        } else {
          h.pos = target->pos + glm::dvec3(0, target->info().height * target->scale() * 0.8, 0);
        }
        break;
      }
      case FishingHook::State::Ground:
        if (h.life > 3600) h.life = 1 << 30;  // (un anzuelo de un invitado que se queda tirado)
        break;
    }
  }
  std::erase_if(hooks_, [](const FishingHook& h) { return !h.remote && (h.life >= (1 << 30) || h.pos.y < -64); });
}

}  // namespace mcw
