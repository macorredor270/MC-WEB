#include "game/session.h"

#include <cmath>
#include <deque>
#include <set>

#include "core/face.h"
#include "data/items.h"
#include "game/rules.h"
#include "world/world.h"

namespace mcw {

GameSession::GameSession(WorldAccess& access, u64 seed) : access_(access), rng_(seed ^ 0xC0FFEEull), seed_(seed) {}

void GameSession::setMode(GameMode m) {
  player_.mode = m;
  if (m == GameMode::Survival) player_.flying = false;
  breakPos_.reset();
  breakProgress_ = 0;
}

void GameSession::setRules(const GameRules& r) {
  rules_ = r;
  player_.difficulty = r.difficulty;
  if (r.difficulty == 0) std::erase_if(mobs_, [](const Mob& m) { return m.info().hostile; });
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

  // Contenido del cofre
  if (id == 54 || id == 146) {
    auto it = chests_.find({p.x, p.y, p.z});
    if (it != chests_.end()) {
      for (const ItemStack& st : it->second.items)
        if (!st.empty()) spawnItem(center, st, {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
      chests_.erase(it);
    }
    if (menu_ && menu_->kind() == MenuKind::Chest) closeMenu();
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
  targetMob_.reset();
  if (player_.dead) return;
  // Una criatura delante del bloque apuntado (y a mano: 3 bloques, 5 en creativo) tiene prioridad
  const double limit = std::min(entityReach(), target_ ? target_->distance : 1e9);
  if (const auto hit = raycastMobs(player_.eyePos(), dir, limit)) {
    targetMob_ = mobs_[hit->first].id;
    target_.reset();
  }
}

void GameSession::handleAttack(const TickInput& in) {
  if (touchAttackTimer_ > 0) touchAttackTimer_--;
  if (targetMob_) {
    breakPos_.reset();
    breakProgress_ = 0;
    // Clic: un golpe. En táctil, mantener el dedo sobre la criatura golpea cada medio segundo.
    const bool hit = in.attackPressed || (in.fromTouch && in.attack && touchAttackTimer_ == 0);
    if (hit) {
      for (Mob& m : mobs_)
        if (m.id == *targetMob_) attackMob(m);
      touchAttackTimer_ = 10;
    }
    return;
  }
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
  // Sobre una criatura: esquilar ovejas con tijeras; un toque en la pantalla la golpea
  if (targetMob_ && in.usePressed) {
    for (Mob& m : mobs_) {
      if (m.id != *targetMob_) continue;
      if (m.type == MobType::Sheep && held.id == ItemId::shears && !m.sheared && !m.dying()) {
        m.sheared = true;
        const int n = 1 + rng_.nextInt(3);
        spawnItem(m.pos + glm::dvec3(0, 1.0, 0), ItemStack(B::wool, n, m.woolColor),
                  {rng_.nextFloat() * 0.2 - 0.1, 0.25, rng_.nextFloat() * 0.2 - 0.1}, 10);
        if (!player_.creative()) damageTool(1);
        SessionEvent e{SessionEvent::Type::SheepSheared, glm::ivec3(glm::floor(m.pos)), 0};
        e.where = m.pos;
        e.mob = m.type;
        events_.push_back(e);
      } else if (in.fromTouch) {
        attackMob(m);
      }
      break;
    }
    return;
  }
  if (!(in.usePressed || (in.use && useDelay_ == 0)) || !target_) return;
  useDelay_ = 4;

  World& w = access_.world();
  const glm::ivec3 tb = target_->block;
  const int targetId = stateId(w.block(tb.x, tb.y, tb.z));
  // Azada: la tierra y la hierba con aire encima se vuelven tierra de cultivo
  const bool hoe = held.id == ItemId::wooden_hoe || held.id == ItemId::stone_hoe || held.id == ItemId::iron_hoe ||
                   held.id == ItemId::golden_hoe || held.id == ItemId::diamond_hoe;
  if (hoe && target_->face != Face::Down && w.block(tb.x, tb.y + 1, tb.z) == 0 &&
      (targetId == B::grass || (targetId == B::dirt && stateMeta(w.block(tb.x, tb.y, tb.z)) != 2))) {
    const BlockState old = w.block(tb.x, tb.y, tb.z);
    access_.setBlock(tb.x, tb.y, tb.z, makeState(60, 0));
    events_.push_back({SessionEvent::Type::BlockPlaced, tb, old});
    damageTool(1);
    return;
  }
  // Polvo de hueso: hace crecer cultivos (y sale una tanda de hierba)
  if (held.id == ItemId::dye && held.meta == 15) {
    const BlockState st = w.block(tb.x, tb.y, tb.z);
    const int m = stateMeta(st);
    int grown = -1;
    if ((targetId == 59 || targetId == 141 || targetId == 142) && m < 7) grown = std::min(7, m + 2 + rng_.nextInt(4));
    if ((targetId == 104 || targetId == 105) && m < 7) grown = std::min(7, m + 2 + rng_.nextInt(4));
    if (targetId == 127 && (m >> 2) < 2) grown = m + 4;
    if (grown >= 0) {
      access_.setBlock(tb.x, tb.y, tb.z, makeState(targetId, grown));
      if (!player_.creative() && --held.count <= 0) held.clear();
      return;
    }
  }
  if (!player_.sneaking) {
    if (useBlock(tb)) return;
    if (targetId == B::crafting_table) {
      menu_ = std::make_unique<Menu>(MenuKind::Crafting, player_);
      return;
    }
    if (targetId == 54 || targetId == 146 || targetId == 130) {
      // Cofre: no se abre con un bloque sólido encima (como en 1.8)
      if (blockInfo(stateId(w.block(tb.x, tb.y + 1, tb.z))).opaqueCube) return;
      ItemStack* items = targetId == 130 ? player_.enderItems.data() : chests_[{tb.x, tb.y, tb.z}].items.data();
      menu_ = std::make_unique<Menu>(MenuKind::Chest, player_, nullptr, items);
      events_.push_back({SessionEvent::Type::DoorOpened, tb, w.block(tb.x, tb.y, tb.z)});
      return;
    }
    if (targetId == B::furnace || targetId == B::lit_furnace) {
      FurnaceState& f = furnaces_[{tb.x, tb.y, tb.z}];
      menu_ = std::make_unique<Menu>(MenuKind::Furnace, player_, &f);
      return;
    }
  }
  if (held.empty()) return;
  const auto place = placementFor(w, held, *target_, player_.yaw, player_.pitch);
  if (!place) return;
  // No colocar un bloque sólido donde está el jugador
  auto blocksPlayer = [&](const glm::ivec3& pos, BlockState st) {
    for (const Box& b : collisionBoxes(stateId(st), stateMeta(st))) {
      const AABB bb{{pos.x + b.x0, pos.y + b.y0, pos.z + b.z0}, {pos.x + b.x1, pos.y + b.y1, pos.z + b.z1}};
      if (bb.intersects(player_.box())) return true;
    }
    return false;
  };
  if (blocksPlayer(place->pos, place->state) || (place->hasSecond && blocksPlayer(place->secondPos, place->secondState))) return;
  access_.setBlock(place->pos.x, place->pos.y, place->pos.z, place->state);
  if (place->hasSecond) access_.setBlock(place->secondPos.x, place->secondPos.y, place->secondPos.z, place->secondState);
  events_.push_back({SessionEvent::Type::BlockPlaced, place->pos, place->state});
  if (!player_.creative() && --held.count <= 0) held.clear();
  neighborUpdates(place->pos);
  if (place->hasSecond) neighborUpdates(place->secondPos);
}

bool GameSession::useBlock(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s), meta = stateMeta(s);
  auto set = [&](const glm::ivec3& at, BlockState st) {
    access_.setBlock(at.x, at.y, at.z, st);
  };
  switch (id) {
    // Puertas de madera: el estado abierto se guarda en la mitad de abajo
    case 64: case 193: case 194: case 195: case 196: case 197: {
      const glm::ivec3 lower = (meta & 8) ? p - glm::ivec3(0, 1, 0) : p;
      const BlockState ls = w.block(lower.x, lower.y, lower.z);
      if (stateId(ls) != id) return false;
      const int nm = stateMeta(ls) ^ 4;
      set(lower, makeState(id, nm));
      events_.push_back({(nm & 4) ? SessionEvent::Type::DoorOpened : SessionEvent::Type::DoorClosed, p, s});
      return true;
    }
    case 96: {  // trampilla de madera
      set(p, makeState(id, meta ^ 4));
      events_.push_back({(meta & 4) ? SessionEvent::Type::DoorClosed : SessionEvent::Type::DoorOpened, p, s});
      return true;
    }
    case 107: case 183: case 184: case 185: case 186: case 187: {  // puerta de valla: se abre hacia fuera del jugador
      int nm = meta ^ 4;
      if (nm & 4) {
        const int h = horizontalFacing(player_.yaw);
        if ((nm & 3) == ((h + 2) & 3)) nm = (nm & ~3) | h;
      }
      set(p, makeState(id, nm));
      events_.push_back({(nm & 4) ? SessionEvent::Type::DoorOpened : SessionEvent::Type::DoorClosed, p, s});
      neighborUpdates(p);
      return true;
    }
    case 69:  // palanca
      set(p, makeState(id, meta ^ 8));
      events_.push_back({SessionEvent::Type::Click, p, s});
      neighborUpdates(p);
      return true;
    case 77: case 143:  // botón: se suelta solo (piedra 1 s, madera 1,5 s)
      if (meta & 8) return true;
      set(p, makeState(id, meta | 8));
      scheduled_.push_back({p, id == 77 ? 20 : 30});
      events_.push_back({SessionEvent::Type::Click, p, s});
      return true;
    case 93: case 94:  // repetidor: cambia el retardo
      set(p, makeState(id, (meta & 3) | ((meta + 4) & 12)));
      events_.push_back({SessionEvent::Type::Click, p, s});
      return true;
    case 149: case 150:  // comparador: compara / resta
      set(p, makeState(id, meta ^ 4));
      events_.push_back({SessionEvent::Type::Click, p, s});
      return true;
    case 92: {  // tarta: un trozo si hay hambre (o en creativo)
      if (!player_.creative() && player_.food >= 20) return false;
      if (!player_.creative()) {
        player_.food = std::min(20, player_.food + 2);
        player_.saturation = std::min(static_cast<float>(player_.food), player_.saturation + 0.4f);
      }
      if (meta >= 5) set(p, 0);
      else set(p, makeState(id, meta + 1));
      events_.push_back({SessionEvent::Type::Ate, p, s});
      return true;
    }
    case 26: {  // cama: de noche se duerme hasta la mañana y se guarda el punto de aparición
      const double t = std::fmod(worldTime_, 24000.0);
      spawn_ = glm::dvec3(p) + glm::dvec3(0.5, 0.6, 0.5);
      if (t > 12541 && t < 23458) {
        sleepRequest_ = worldTime_ - t + 24000.0;
        events_.push_back({SessionEvent::Type::Slept, p, s});
      }
      return true;
    }
    default: return false;
  }
}

void GameSession::tickScheduled() {
  World& w = access_.world();
  for (auto& t : scheduled_) {
    if (--t.ticks > 0) continue;
    const BlockState s = w.block(t.pos.x, t.pos.y, t.pos.z);
    if ((stateId(s) == 77 || stateId(s) == 143) && (stateMeta(s) & 8)) {
      access_.setBlock(t.pos.x, t.pos.y, t.pos.z, makeState(stateId(s), stateMeta(s) & 7));
      events_.push_back({SessionEvent::Type::Click, t.pos, s});
    }
  }
  std::erase_if(scheduled_, [](const Scheduled& t) { return t.ticks <= 0; });
}

void GameSession::randomTicks() {
  // Como en 1.8: cada tick, `randomTickSpeed` bloques al azar por sección de 16x16x16 en los
  // chunks cercanos al jugador. Aquí solo crecen las plantas.
  if (randomTickSpeed_ <= 0) return;
  World& w = access_.world();
  const int pcx = static_cast<int>(std::floor(player_.pos.x)) >> 4, pcz = static_cast<int>(std::floor(player_.pos.z)) >> 4;
  const int pcy = std::clamp(static_cast<int>(std::floor(player_.pos.y)) >> 4, 0, 15);
  for (int cz = pcz - 6; cz <= pcz + 6; cz++)
    for (int cx = pcx - 6; cx <= pcx + 6; cx++)
      for (int sy = std::max(0, pcy - 3); sy <= std::min(15, pcy + 3); sy++)
        for (int k = 0; k < randomTickSpeed_; k++) {
          const u32 r = tickRng_.nextInt(1 << 12);
          const int x = cx * 16 + (r & 15), z = cz * 16 + ((r >> 4) & 15), y = sy * 16 + ((r >> 8) & 15);
          const BlockState s = w.block(x, y, z);
          const int id = stateId(s), meta = stateMeta(s);
          switch (id) {
            case 59: case 141: case 142: {  // cultivos: más rápido con la tierra húmeda
              if (meta >= 7) break;
              const BlockState below = w.block(x, y - 1, z);
              const float chance = (stateMeta(below) > 0 ? 4.0f : 2.0f);
              if (tickRng_.nextFloat() * (25.0f / chance) < 1.0f) access_.setBlock(x, y, z, makeState(id, meta + 1));
              break;
            }
            case 104: case 105: {  // tallos: crecen y al final ponen el fruto al lado
              if (tickRng_.nextInt(3)) break;
              if (meta < 7) { access_.setBlock(x, y, z, makeState(id, meta + 1)); break; }
              static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
              const int fruit = id == 104 ? 86 : 103;
              bool has = false;
              for (int d = 0; d < 4; d++) has |= stateId(w.block(x + dx[d], y, z + dz[d])) == fruit;
              if (has) break;
              const int d = tickRng_.nextInt(4);
              const int bx = x + dx[d], bz = z + dz[d];
              const int ground = stateId(w.block(bx, y - 1, bz));
              if (w.block(bx, y, bz) == 0 && (ground == 60 || ground == B::dirt || ground == B::grass))
                access_.setBlock(bx, y, bz, makeState(fruit, fruit == 86 ? tickRng_.nextInt(4) : 0));
              break;
            }
            case 115:  // verruga del Nether
              if (meta < 3 && tickRng_.nextInt(10) == 0) access_.setBlock(x, y, z, makeState(id, meta + 1));
              break;
            case 127:  // cacao
              if ((meta >> 2) < 2 && tickRng_.nextInt(5) == 0) access_.setBlock(x, y, z, makeState(id, meta + 4));
              break;
            case B::reeds: case B::cactus: {  // caña y cactus: hasta 3 de alto
              if (w.block(x, y + 1, z) != 0) break;
              int h = 1;
              while (h < 3 && stateId(w.block(x, y - h, z)) == id) h++;
              if (h >= 3) break;
              if (meta >= 15) {
                access_.setBlock(x, y, z, makeState(id, 0));
                access_.setBlock(x, y + 1, z, makeState(id, 0));
              } else {
                access_.setBlock(x, y, z, makeState(id, meta + 1));
              }
              break;
            }
            case 60: {  // tierra de cultivo: se humedece si hay agua cerca (4 bloques)
              bool water = false;
              for (int dz = -4; dz <= 4 && !water; dz++)
                for (int dx = -4; dx <= 4 && !water; dx++)
                  for (int dy = 0; dy <= 1 && !water; dy++) water = isWater(stateId(w.block(x + dx, y + dy, z + dz)));
              if (water && meta < 7) access_.setBlock(x, y, z, makeState(id, 7));
              else if (!water && meta > 0) access_.setBlock(x, y, z, makeState(id, meta - 1));
              break;
            }
            default: break;
          }
        }
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
  worldTime_ = in.worldTime;
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
  tickScheduled();
  randomTickSpeed_ = in.randomTickSpeed;
  randomTicks();
  tickMobs();
  tickArrows();
  spawnHostiles();

  const float hpBefore = player_.health;
  player_.tickStatus(access_.world());
  if (player_.health < hpBefore) events_.push_back({SessionEvent::Type::PlayerHurt, {}, 0});
  if (player_.dead && !wasDead) onPlayerDeath();
}

void GameSession::onPlayerDeath() {
  // Al morir se sueltan todos los objetos (salvo con "conservar inventario")
  for (int i = 0; i < PlayerInventory::kSize && !rules_.keepInventory; i++) {
    ItemStack& s = player_.inventory.slot(i);
    if (s.empty()) continue;
    spawnItem(player_.pos + glm::dvec3(0, 1, 0), s, {rng_.nextFloat() * 0.4 - 0.2, 0.3, rng_.nextFloat() * 0.4 - 0.2}, 40);
    s.clear();
  }
  closeMenu();
  events_.push_back({SessionEvent::Type::PlayerDied, {}, 0});
}

void GameSession::killPlayer() {
  if (player_.dead) return;
  player_.health = 0;
  player_.dead = true;
  onPlayerDeath();
}

namespace {
bool inChunk(const glm::dvec3& p, int cx, int cz) {
  return static_cast<int>(std::floor(p.x)) >> 4 == cx && static_cast<int>(std::floor(p.z)) >> 4 == cz;
}
}  // namespace

std::vector<Mob> GameSession::mobsInChunk(int cx, int cz, bool take) {
  std::vector<Mob> out;
  for (const Mob& m : mobs_)
    if (!m.dying() && inChunk(m.pos, cx, cz)) out.push_back(m);
  if (take) {
    std::erase_if(mobs_, [&](const Mob& m) { return inChunk(m.pos, cx, cz); });
    if (targetMob_ && std::none_of(mobs_.begin(), mobs_.end(), [&](const Mob& m) { return m.id == *targetMob_; })) targetMob_.reset();
  }
  return out;
}

std::vector<ItemEntity> GameSession::itemsInChunk(int cx, int cz, bool take) {
  std::vector<ItemEntity> out;
  for (const ItemEntity& e : items_)
    if (inChunk(e.pos, cx, cz)) out.push_back(e);
  if (take) std::erase_if(items_, [&](const ItemEntity& e) { return inChunk(e.pos, cx, cz); });
  return out;
}

std::vector<std::pair<glm::ivec3, FurnaceState>> GameSession::furnacesInChunk(int cx, int cz, bool take) {
  std::vector<std::pair<glm::ivec3, FurnaceState>> out;
  for (auto it = furnaces_.begin(); it != furnaces_.end();) {
    const auto [x, y, z] = it->first;
    if ((x >> 4) == cx && (z >> 4) == cz) {
      out.emplace_back(glm::ivec3(x, y, z), it->second);
      if (take && !menu_) {
        it = furnaces_.erase(it);
        continue;
      }
    }
    ++it;
  }
  return out;
}

std::vector<std::pair<glm::ivec3, ChestState>> GameSession::chestsInChunk(int cx, int cz, bool take) {
  std::vector<std::pair<glm::ivec3, ChestState>> out;
  for (auto it = chests_.begin(); it != chests_.end();) {
    const auto [x, y, z] = it->first;
    if ((x >> 4) == cx && (z >> 4) == cz) {
      out.emplace_back(glm::ivec3(x, y, z), it->second);
      if (take && !menu_) {
        it = chests_.erase(it);
        continue;
      }
    }
    ++it;
  }
  return out;
}

void GameSession::addMob(Mob m) {
  m.id = nextMobId_++;
  mobs_.push_back(std::move(m));
}

void GameSession::clearWorldState() {
  closeMenu();
  mobs_.clear();
  items_.clear();
  arrows_.clear();
  furnaces_.clear();
  chests_.clear();
  targetMob_.reset();
  target_.reset();
  breakPos_.reset();
  breakProgress_ = 0;
  events_.clear();
}

}  // namespace mcw
