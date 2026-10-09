#include "game/session.h"

#include <cmath>
#include <deque>
#include <set>

#include "core/face.h"
#include "data/items.h"
#include "game/rules.h"
#include "world/world.h"

namespace mcw {
namespace {

/// Bloques que reaccionan al clic derecho (se abren, se pulsan, cambian de estado...). Con uno de ellos
/// delante, el clic es para el bloque y no para lo que se lleve en la mano (p. ej. el arco).
bool isInteractiveBlock(int id) {
  return id == B::crafting_table || id == B::furnace || id == B::lit_furnace || id == 54 || id == 146 || id == 130 ||
         id == 64 || id == 96 || id == 107 || (id >= 183 && id <= 187) || (id >= 193 && id <= 197) || id == 69 ||
         id == 77 || id == 143 || id == 93 || id == 94 || id == 149 || id == 150 || id == 92 || id == 26 || id == 116 ||
         id == 145 || id == 117 || id == 154 || id == 23 || id == 158 || id == 84 || id == 25;
}

}  // namespace

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
  award(Ach::OpenInventory);
}

void GameSession::award(Ach a) {
  if (!achievements_.award(a)) return;
  SessionEvent e{SessionEvent::Type::Achievement, {}, 0};
  e.value = static_cast<int>(a);
  events_.push_back(e);
}

void GameSession::trackAchievements() {
  // Lo fabricado y lo sacado del horno desde el último tick
  for (const ItemStack& s : player_.crafted) {
    achievements_.addStat("stat.craftItem.minecraft." + std::string(itemInfo(s.id).name), s.count);
    switch (s.id) {
      case B::crafting_table: award(Ach::BuildWorkBench); break;
      case B::furnace: award(Ach::BuildFurnace); break;
      case 116: award(Ach::Enchantments); break;
      case B::bookshelf: award(Ach::Bookcase); break;
      case ItemId::bread: award(Ach::MakeBread); break;
      case ItemId::cake: award(Ach::BakeCake); break;
      case ItemId::golden_apple: if (s.meta == 1) award(Ach::Overpowered); break;
      case ItemId::wooden_pickaxe: award(Ach::BuildPickaxe); break;
      case ItemId::stone_pickaxe: case ItemId::iron_pickaxe: case ItemId::golden_pickaxe: case ItemId::diamond_pickaxe:
        award(Ach::BuildPickaxe);
        award(Ach::BuildBetterPickaxe);
        break;
      case ItemId::wooden_hoe: case ItemId::stone_hoe: case ItemId::iron_hoe: case ItemId::golden_hoe: case ItemId::diamond_hoe:
        award(Ach::BuildHoe);
        break;
      case ItemId::wooden_sword: case ItemId::stone_sword: case ItemId::iron_sword: case ItemId::golden_sword:
      case ItemId::diamond_sword:
        award(Ach::BuildSword);
        break;
      default: break;
    }
  }
  player_.crafted.clear();
  for (const ItemStack& s : player_.smelted) {
    if (s.id == ItemId::iron_ingot) award(Ach::AcquireIron);
    if (s.id == ItemId::cooked_fish) award(Ach::CookFish);
  }
  player_.smelted.clear();
}

void GameSession::onPickup(const ItemStack& s) {
  achievements_.addStat("stat.pickup.minecraft." + std::string(itemInfo(s.id).name), s.count);
  switch (s.id) {
    case B::log: case B::log2: award(Ach::MineWood); break;
    case ItemId::leather: award(Ach::KillCow); break;
    case ItemId::diamond: award(Ach::Diamonds); break;
    case ItemId::blaze_rod: award(Ach::BlazeRod); break;
    default: break;
  }
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
  if (remote_) {
    if (remote_->drop) remote_->drop(s.count > 1);
    return;
  }
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
  e.id = nextItemId_++;
  items_.push_back(e);
}

void GameSession::setAndUpdate(int x, int y, int z, BlockState s) {
  setWorldBlock(x, y, z, s);
  neighborUpdates({x, y, z});
}

void GameSession::neighborUpdates(const glm::ivec3& origin) {
  if (remote_) return;  // en un servidor, lo decide él
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
        setWorldBlock(p.x, p.y, p.z, 0);
        setWorldBlock(p.x, y, p.z, s);
        push6(p);
        push6({p.x, y, p.z});
      }
    }
  }
  redstoneNotify(origin);
}

void GameSession::breakBlock(const glm::ivec3& p, bool byPlayer) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s);
  if (id == B::air) return;
  if (remote_ && byPlayer) {
    // En un servidor: avisar y quitarlo ya en local (si no está de acuerdo, lo vuelve a poner)
    if (remote_->dig) remote_->dig(player_.creative() ? 0 : 2, p, target_ ? target_->face : 1);
    setWorldBlock(p.x, p.y, p.z, 0);
    events_.push_back({SessionEvent::Type::BlockBroken, p, s});
    return;
  }
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

  const bool drops = !suppressDrops_ && (!byPlayer || !player_.creative());
  if (drops) {
    const ItemStack tool = byPlayer ? player_.inventory.selected() : ItemStack();
    for (const ItemStack& d : blockDrops(s, tool, rng_))
      spawnItem(center - glm::dvec3(0, 0.25, 0) + glm::dvec3(rng_.nextFloat() * 0.5 - 0.25, 0, rng_.nextFloat() * 0.5 - 0.25), d,
                {rng_.nextFloat() * 0.2 - 0.1, 0.2, rng_.nextFloat() * 0.2 - 0.1}, 10);
  }
  setWorldBlock(p.x, p.y, p.z, 0);
  events_.push_back({SessionEvent::Type::BlockBroken, p, s});
  // Plantas dobles: la otra mitad también desaparece
  if (id == B::double_plant) {
    const glm::ivec3 other = p + glm::ivec3(0, (stateMeta(s) & 8) ? -1 : 1, 0);
    if (stateId(w.block(other.x, other.y, other.z)) == B::double_plant) setWorldBlock(other.x, other.y, other.z, 0);
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
    if (remote_ && remote_->dig) remote_->dig(0, p, target_->face);
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
  // Arco: mantener pulsado lo tensa y soltar dispara. Con un bloque que se usa delante (mesa, puerta...)
  // el clic es para el bloque, como en el juego. Cambiar de ranura lo cancela sin disparar.
  if (held.id != ItemId::bow || remote_) {
    bowTicks_ = 0;
  } else {
    const bool hasArrow = player_.creative() || arrowCount() > 0;
    if (bowTicks_ > 0) {
      if (in.use && hasArrow) {
        bowTicks_ = std::min(bowTicks_ + 1, 72000);
        return;
      }
      const int ticks = bowTicks_;
      bowTicks_ = 0;
      shootBow(ticks);
      return;
    }
    if (in.use && hasArrow && (in.usePressed || useDelay_ == 0)) {
      const bool blockFirst = target_ && !player_.sneaking &&
                              isInteractiveBlock(stateId(access_.world().block(target_->block.x, target_->block.y, target_->block.z)));
      if (!blockFirst) {
        bowTicks_ = 1;
        return;
      }
    }
  }
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
  if (remote_) {
    // En un servidor: se manda el clic; abrir cosas y cambiar bloques con estado lo hace él.
    // Colocar un bloque se adelanta en local para que no se note la espera.
    if (remote_->use) remote_->use(tb, target_->face, glm::vec3(target_->point - glm::dvec3(tb)), held);
    if (remote_->swing) remote_->swing();
    const bool interactive = isInteractiveBlock(targetId);
    if ((interactive && !player_.sneaking) || held.empty()) return;
    const auto place = placementFor(w, held, *target_, player_.yaw, player_.pitch);
    if (!place) return;
    setWorldBlock(place->pos.x, place->pos.y, place->pos.z, place->state);
    if (place->hasSecond) setWorldBlock(place->secondPos.x, place->secondPos.y, place->secondPos.z, place->secondState);
    events_.push_back({SessionEvent::Type::BlockPlaced, place->pos, place->state});
    if (!player_.creative() && --held.count <= 0) held.clear();
    return;
  }
  // Azada: la tierra y la hierba con aire encima se vuelven tierra de cultivo
  const bool hoe = held.id == ItemId::wooden_hoe || held.id == ItemId::stone_hoe || held.id == ItemId::iron_hoe ||
                   held.id == ItemId::golden_hoe || held.id == ItemId::diamond_hoe;
  if (hoe && target_->face != Face::Down && w.block(tb.x, tb.y + 1, tb.z) == 0 &&
      (targetId == B::grass || (targetId == B::dirt && stateMeta(w.block(tb.x, tb.y, tb.z)) != 2))) {
    const BlockState old = w.block(tb.x, tb.y, tb.z);
    setWorldBlock(tb.x, tb.y, tb.z, makeState(60, 0));
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
      setWorldBlock(tb.x, tb.y, tb.z, makeState(targetId, grown));
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
  setWorldBlock(place->pos.x, place->pos.y, place->pos.z, place->state);
  if (place->hasSecond) setWorldBlock(place->secondPos.x, place->secondPos.y, place->secondPos.z, place->secondState);
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
    setWorldBlock(at.x, at.y, at.z, st);
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
      schedule(p, id == 77 ? 20 : 30, TickKind::ButtonRelease);
      redstoneNotify(p);
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

void GameSession::randomTicks() {
  // Como en 1.8: cada tick, `randomTickSpeed` bloques al azar por sección de 16x16x16 en los
  // chunks cercanos al jugador. Aquí solo crecen las plantas.
  if (randomTickSpeed_ <= 0) return;
  World& w = access_.world();
  std::set<std::pair<int, int>> done;  // con varios jugadores, cada chunk una sola vez
  for (const Player* pl : activePlayers()) {
  const int pcx = static_cast<int>(std::floor(pl->pos.x)) >> 4, pcz = static_cast<int>(std::floor(pl->pos.z)) >> 4;
  const int pcy = std::clamp(static_cast<int>(std::floor(pl->pos.y)) >> 4, 0, 15);
  for (int cz = pcz - 6; cz <= pcz + 6; cz++)
    for (int cx = pcx - 6; cx <= pcx + 6; cx++) {
      if (!done.insert({cx, cz}).second) continue;
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
              if (tickRng_.nextFloat() * (25.0f / chance) < 1.0f) setWorldBlock(x, y, z, makeState(id, meta + 1));
              break;
            }
            case 104: case 105: {  // tallos: crecen y al final ponen el fruto al lado
              if (tickRng_.nextInt(3)) break;
              if (meta < 7) { setWorldBlock(x, y, z, makeState(id, meta + 1)); break; }
              static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
              const int fruit = id == 104 ? 86 : 103;
              bool has = false;
              for (int d = 0; d < 4; d++) has |= stateId(w.block(x + dx[d], y, z + dz[d])) == fruit;
              if (has) break;
              const int d = tickRng_.nextInt(4);
              const int bx = x + dx[d], bz = z + dz[d];
              const int ground = stateId(w.block(bx, y - 1, bz));
              if (w.block(bx, y, bz) == 0 && (ground == 60 || ground == B::dirt || ground == B::grass))
                setWorldBlock(bx, y, bz, makeState(fruit, fruit == 86 ? tickRng_.nextInt(4) : 0));
              break;
            }
            case 115:  // verruga del Nether
              if (meta < 3 && tickRng_.nextInt(10) == 0) setWorldBlock(x, y, z, makeState(id, meta + 1));
              break;
            case 127:  // cacao
              if ((meta >> 2) < 2 && tickRng_.nextInt(5) == 0) setWorldBlock(x, y, z, makeState(id, meta + 4));
              break;
            case B::reeds: case B::cactus: {  // caña y cactus: hasta 3 de alto
              if (w.block(x, y + 1, z) != 0) break;
              int h = 1;
              while (h < 3 && stateId(w.block(x, y - h, z)) == id) h++;
              if (h >= 3) break;
              if (meta >= 15) {
                setWorldBlock(x, y, z, makeState(id, 0));
                setWorldBlock(x, y + 1, z, makeState(id, 0));
              } else {
                setWorldBlock(x, y, z, makeState(id, meta + 1));
              }
              break;
            }
            case 60: {  // tierra de cultivo: se humedece si hay agua cerca (4 bloques)
              bool water = false;
              for (int dz = -4; dz <= 4 && !water; dz++)
                for (int dx = -4; dx <= 4 && !water; dx++)
                  for (int dy = 0; dy <= 1 && !water; dy++) water = isWater(stateId(w.block(x + dx, y + dy, z + dz)));
              if (water && meta < 7) setWorldBlock(x, y, z, makeState(id, 7));
              else if (!water && meta > 0) setWorldBlock(x, y, z, makeState(id, meta - 1));
              break;
            }
            default: break;
          }
        }
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
    if (e.pickupDelay == 0 && localActive_ && !player_.dead && AABB::centered(e.pos, 0.25, 0.25).intersects(pickup)) {
      const int before = e.stack.count;
      const ItemStack taken = e.stack;
      e.stack = player_.inventory.add(e.stack);
      if (e.stack.count != before) {
        events_.push_back({SessionEvent::Type::ItemPickedUp, glm::ivec3(e.pos), 0});
        onPickup(ItemStack(taken.id, before - e.stack.count, taken.meta));
      }
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
    setWorldBlock(x, y, z, makeState(f.burning() ? B::lit_furnace : B::furnace, stateMeta(s)));
  }
}

void GameSession::tickWorld(const TickInput& in) {
  if (remote_) return;  // en un servidor, el mundo lo mueve el servidor
  tickItems();
  tickFurnaces();
  tickScheduled();
  tickPlates();
  randomTickSpeed_ = in.randomTickSpeed;
  randomTicks();
  tickMobs();
  tickArrows();
  spawnHostiles();
}

void GameSession::tick(const TickInput& in) {
  worldTime_ = in.worldTime;
  if (!localActive_) {  // servidor dedicado: solo el mundo
    tickWorld(in);
    return;
  }
  const bool wasDead = player_.dead;  // se puede morir por una caída durante el movimiento
  player_.yaw = in.yaw;
  player_.pitch = in.pitch;
  const bool menuOpen = menu_ != nullptr;
  MoveInput move = menuOpen ? MoveInput{} : in.move;
  if (eatTicks_ > 0 || bowTicks_ > 0) {  // comer o tensar el arco frena al 20 % (y corta la carrera)
    move.forward *= 0.2f;
    move.strafe *= 0.2f;
  }
  const glm::dvec3 before = player_.pos;
  const bool wasOnGround = player_.onGround;
  player_.tickMovement(access_.world(), move, menuOpen ? false : in.jumpPressed);
  // Estadísticas de movimiento (en centímetros, como en 1.8)
  {
    const double dh = glm::length(glm::dvec2(player_.pos.x - before.x, player_.pos.z - before.z));
    const i64 cm = static_cast<i64>(std::round(dh * 100.0));
    if (cm > 0 && !player_.dead) {
      if (player_.flying) achievements_.addStat("stat.flyOneCm", cm);
      else if (player_.inWater) achievements_.addStat("stat.swimOneCm", cm);
      else if (player_.sneaking) achievements_.addStat("stat.crouchOneCm", cm);
      else if (player_.sprinting) achievements_.addStat("stat.sprintOneCm", cm);
      else achievements_.addStat("stat.walkOneCm", cm);
    }
    if (wasOnGround && !player_.onGround && player_.pos.y > before.y && !player_.flying) achievements_.addStat("stat.jump");
    achievements_.addStat("stat.playOneMinute");
    achievements_.addStat("stat.timeSinceDeath");
  }

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
    eatTicks_ = bowTicks_ = 0;
  }
  tickWorld(in);
  trackAchievements();

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
  achievements_.addStat("stat.deaths");
  achievements_.addStat("stat.timeSinceDeath", -achievements_.stat("stat.timeSinceDeath"));
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

std::optional<ItemStack> GameSession::takeItem(u32 id) {
  for (ItemEntity& e : items_)
    if (e.id == id && !e.stack.empty()) {
      ItemStack s = e.stack;
      e.stack.clear();  // se quita en el siguiente tick
      return s;
    }
  return std::nullopt;
}

void GameSession::hurtMobById(u32 id, float amount, const glm::dvec3& from) {
  for (Mob& m : mobs_)
    if (m.id == id) {
      hurtMob(m, amount, from, 0.4f, false);
      return;
    }
}

u32 GameSession::addMob(Mob m) {
  m.id = nextMobId_++;
  mobs_.push_back(std::move(m));
  return mobs_.back().id;
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
