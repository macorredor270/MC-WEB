// Contenedores con lógica: cofres (también dobles), tolvas, dispensadores y soltadores, bloques musicales y tocadiscos.
#include <algorithm>

#include "core/face.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/minecart.h"
#include "game/rails.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kChest = 54, kTrappedChest = 146, kHopper = 154, kDispenser = 23, kDropper = 158, kJukebox = 84, kNoteBlock = 25, kFurnace = 61,
              kLitFurnace = 62;

glm::ivec3 facingDir(int meta) {
  static const glm::ivec3 d[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
  return d[std::clamp(meta & 7, 0, 5)];
}

bool isRecord(int id) { return id >= ItemId::record_13 && id <= ItemId::record_wait; }

/// Mete una pila en una lista de casillas (juntando con las iguales primero). Devuelve lo que no cabe.
ItemStack insertInto(const std::vector<ItemStack*>& slots, ItemStack s) {
  for (ItemStack* t : slots) {
    if (s.empty()) return s;
    if (!t->empty() && t->stacksWith(s) && t->count < t->maxStack()) {
      const int n = std::min<int>(s.count, t->maxStack() - t->count);
      t->count = static_cast<i16>(t->count + n);
      s.count = static_cast<i16>(s.count - n);
    }
  }
  for (ItemStack* t : slots) {
    if (s.empty()) return s;
    if (t->empty()) {
      const int n = std::min<int>(s.count, s.maxStack());
      *t = s;
      t->count = static_cast<i16>(n);
      s.count = static_cast<i16>(s.count - n);
    }
  }
  if (s.count <= 0) s.clear();
  return s;
}

bool isFull(const std::vector<ItemStack*>& slots) {
  for (const ItemStack* t : slots)
    if (t->empty() || t->count < t->maxStack()) return false;
  return true;
}

}  // namespace

/// El otro cofre de un cofre doble (el que está al lado, a lo ancho de lo que miran, y mira igual), si lo hay.
std::optional<glm::ivec3> GameSession::chestPartner(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s), meta = stateMeta(s);
  if (id != kChest && id != kTrappedChest) return std::nullopt;
  const glm::ivec3 step = (meta == 2 || meta == 3) ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
  for (const glm::ivec3& q : {p - step, p + step}) {
    const BlockState o = w.block(q.x, q.y, q.z);
    if (stateId(o) == id && stateMeta(o) == meta) return q;
  }
  return std::nullopt;
}

/// Las casillas del contenedor del bloque de `p` (vacía si no es un contenedor). `title` recibe el nombre de la ventana.
std::vector<ItemStack*> GameSession::containerSlots(const glm::ivec3& p, std::string* title, MenuKind* kind) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s), meta = stateMeta(s);
  std::vector<ItemStack*> out;
  auto add = [&](const glm::ivec3& q, int n) {
    ChestState& c = chests_[{q.x, q.y, q.z}];
    for (int i = 0; i < n; i++) out.push_back(&c.items[static_cast<std::size_t>(i)]);
  };
  if (id == kChest || id == kTrappedChest) {
    if (kind) *kind = MenuKind::Chest;
    // Dos cofres iguales y mirando igual, uno al lado del otro (a lo ancho de lo que miran), forman uno doble: primero el de
    // menor coordenada
    const glm::ivec3 step = (meta == 2 || meta == 3) ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
    auto same = [&](const glm::ivec3& q) {
      const BlockState o = w.block(q.x, q.y, q.z);
      return stateId(o) == id && stateMeta(o) == meta;
    };
    if (same(p - step)) {
      add(p - step, 27);
      add(p, 27);
      if (title) *title = "Cofre grande";
    } else if (same(p + step)) {
      add(p, 27);
      add(p + step, 27);
      if (title) *title = "Cofre grande";
    } else {
      add(p, 27);
      if (title) *title = "Cofre";
    }
  } else if (id == kHopper) {
    add(p, 5);
    if (kind) *kind = MenuKind::Hopper;
    if (title) *title = "Tolva";
  } else if (id == 117) {
    add(p, 4);
    if (kind) *kind = MenuKind::Brewing;
    if (title) *title = "Atril de pociones";
  } else if (id == kDispenser || id == kDropper) {
    add(p, 9);
    if (kind) *kind = id == kDispenser ? MenuKind::Dispenser : MenuKind::Dropper;
    if (title) *title = id == kDispenser ? "Dispensador" : "Soltador";
  }
  return out;
}

/// Mete `s` en el contenedor de `p` (si lo hay), como hace una tolva desde la dirección `from` (hacia dónde está la tolva).
/// Los hornos aceptan por arriba en la entrada y por los lados solo combustible. Devuelve lo que no cabe.
ItemStack GameSession::containerInsert(const glm::ivec3& p, const ItemStack& s, const glm::ivec3& from) {
  World& w = access_.world();
  const int id = stateId(w.block(p.x, p.y, p.z));
  if (id == kFurnace || id == kLitFurnace) {
    FurnaceState& f = furnaces_[{p.x, p.y, p.z}];
    ItemStack rest = s;
    ItemStack* slot = from.y > 0 ? &f.input : &f.fuel;
    if (slot == &f.fuel && fuelTicks(rest) <= 0) return s;
    if (slot->empty()) {
      *slot = rest;
      rest.clear();
    } else if (slot->stacksWith(rest) && slot->count < slot->maxStack()) {
      const int n = std::min<int>(rest.count, slot->maxStack() - slot->count);
      slot->count = static_cast<i16>(slot->count + n);
      rest.count = static_cast<i16>(rest.count - n);
      if (rest.count <= 0) rest.clear();
    }
    return rest;
  }
  std::vector<ItemStack*> slots = containerSlots(p, nullptr, nullptr);
  if (slots.empty()) return s;
  return insertInto(slots, s);
}

/// Hace un paso de cada tolva: pasa un objeto al contenedor al que apunta y coge uno del de arriba (o del suelo).
void GameSession::tickHoppers() {
  World& w = access_.world();
  std::vector<std::tuple<int, int, int>> hoppers;
  for (const auto& [key, st] : chests_) {
    const auto [x, y, z] = key;
    if (stateId(w.block(x, y, z)) == kHopper) hoppers.push_back(key);
  }
  for (const auto& key : hoppers) {
    int& cd = hopperCooldown_[key];
    if (--cd > 0) continue;
    cd = 0;
    const glm::ivec3 p(std::get<0>(key), std::get<1>(key), std::get<2>(key));
    const BlockState s = w.block(p.x, p.y, p.z);
    if (stateMeta(s) & 8) continue;  // con potencia no hace nada
    ChestState& mine = chests_[key];
    std::vector<ItemStack*> own;
    for (int i = 0; i < 5; i++) own.push_back(&mine.items[static_cast<std::size_t>(i)]);
    bool moved = false;
    // Empujar un objeto hacia donde apunta
    const glm::ivec3 dir = facingDir(stateMeta(s));
    const glm::ivec3 target = p + dir;
    const int tid = stateId(w.block(target.x, target.y, target.z));
    const bool canHold = tid == kChest || tid == kTrappedChest || tid == kHopper || tid == kDispenser || tid == kDropper || tid == kFurnace || tid == kLitFurnace;
    if (canHold) {
      for (ItemStack* src : own) {
        if (src->empty()) continue;
        ItemStack one = *src;
        one.count = 1;
        const ItemStack rest = containerInsert(target, one, -dir);
        if (rest.empty()) {
          if (--src->count <= 0) src->clear();
          moved = true;
          break;
        }
      }
    }
    // Coger un objeto del contenedor de arriba o recoger lo que haya en el suelo encima
    if (!isFull(own)) {
      const glm::ivec3 above = p + glm::ivec3(0, 1, 0);
      const int aid = stateId(w.block(above.x, above.y, above.z));
      bool pulled = false;
      if (aid == kFurnace || aid == kLitFurnace) {
        FurnaceState& f = furnaces_[{above.x, above.y, above.z}];
        if (!f.output.empty()) {
          ItemStack one = f.output;
          one.count = 1;
          if (insertInto(own, one).empty()) {
            if (--f.output.count <= 0) f.output.clear();
            pulled = true;
          }
        }
      } else {
        std::vector<ItemStack*> up = containerSlots(above, nullptr, nullptr);
        for (ItemStack* src : up) {
          if (src->empty()) continue;
          ItemStack one = *src;
          one.count = 1;
          if (insertInto(own, one).empty()) {
            if (--src->count <= 0) src->clear();
            pulled = true;
            break;
          }
        }
      }
      if (!pulled && aid != kChest && aid != kTrappedChest && aid != kHopper && aid != kDispenser && aid != kDropper && aid != kFurnace && aid != kLitFurnace &&
          !blockInfo(aid).opaqueCube) {
        for (ItemEntity& e : items_) {
          if (e.stack.empty() || e.pos.x < p.x || e.pos.x >= p.x + 1 || e.pos.z < p.z || e.pos.z >= p.z + 1 || e.pos.y < p.y + 0.9 || e.pos.y > p.y + 2.1)
            continue;
          const ItemStack rest = insertInto(own, e.stack);
          if (rest.count != e.stack.count) {
            e.stack = rest;
            pulled = true;
            break;
          }
        }
      }
      moved |= pulled;
    }
    if (moved) cd = 8;
  }
  std::erase_if(items_, [](const ItemEntity& e) { return e.stack.empty(); });
}

/// Un dispensador o soltador recibe un pulso: saca un objeto al azar.
void GameSession::dispense(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s);
  if (id != kDispenser && id != kDropper) return;
  const glm::ivec3 dir = facingDir(stateMeta(s));
  std::vector<ItemStack*> slots = containerSlots(p, nullptr, nullptr);
  std::vector<ItemStack*> filled;
  for (ItemStack* t : slots)
    if (!t->empty()) filled.push_back(t);
  if (filled.empty()) {
    events_.push_back({SessionEvent::Type::Click, p, s});  // nada que sacar: solo el clic
    return;
  }
  ItemStack* slot = filled[static_cast<std::size_t>(rng_.nextInt(static_cast<int>(filled.size())))];
  const ItemStack item = *slot;
  const glm::ivec3 front = p + dir;
  const glm::dvec3 mouth = glm::dvec3(p) + 0.5 + glm::dvec3(dir) * 0.7;
  auto takeOne = [&] {
    if (--slot->count <= 0) slot->clear();
  };
  auto dropOne = [&](ItemStack one) {
    one.count = 1;
    glm::dvec3 v = glm::dvec3(dir) * (0.15 + rng_.nextFloat() * 0.1);
    v.x += (rng_.nextFloat() - 0.5) * 0.05;
    v.z += (rng_.nextFloat() - 0.5) * 0.05;
    v.y += dir.y == 0 ? 0.2 : 0.0;
    spawnItem(mouth, one, v, 10);
  };
  events_.push_back({SessionEvent::Type::Click, p, s});

  if (id == kDropper) {
    // Si delante hay un contenedor, se lo pasa; si no, lo suelta
    ItemStack one = item;
    one.count = 1;
    const int fid = stateId(w.block(front.x, front.y, front.z));
    const bool holder = fid == kChest || fid == kTrappedChest || fid == kHopper || fid == kDispenser || fid == kDropper || fid == kFurnace || fid == kLitFurnace;
    if (holder) {
      if (containerInsert(front, one, -dir).empty()) takeOne();
    } else {
      takeOne();
      dropOne(item);
    }
    return;
  }

  const BlockState there = w.block(front.x, front.y, front.z);
  const int tid = stateId(there);
  switch (item.id) {
    case ItemId::arrow: {
      Arrow a;
      a.pos = a.prevPos = mouth;
      a.motion = glm::dvec3(dir) * 1.1 + glm::dvec3(0, 0.1, 0);
      a.damage = 2.0f;
      a.yaw = std::atan2(static_cast<float>(-a.motion.x), static_cast<float>(-a.motion.z));
      a.pitch = std::atan2(static_cast<float>(a.motion.y), static_cast<float>(std::hypot(a.motion.x, a.motion.z)));
      a.id = nextArrowId_++;
      arrows_.push_back(a);
      takeOne();
      return;
    }
    case ItemId::water_bucket: case ItemId::lava_bucket: {
      if (!fluidFree(there)) break;
      if (there != 0 && !isFluid(tid)) breakBlock(front, false);
      const bool water = item.id == ItemId::water_bucket;
      setWorldBlock(front.x, front.y, front.z, makeState(water ? B::flowing_water : B::flowing_lava, 0));
      schedule(front, water ? 5 : 30, TickKind::Fluid);
      neighborUpdates(front);
      *slot = ItemStack(ItemId::bucket);
      return;
    }
    case ItemId::bucket:
      if (isFluid(tid) && stateMeta(there) == 0) {
        setWorldBlock(front.x, front.y, front.z, 0);
        neighborUpdates(front);
        takeOne();
        const ItemStack full(isWater(tid) ? ItemId::water_bucket : ItemId::lava_bucket);
        const ItemStack rest = insertInto(slots, full);
        if (!rest.empty()) dropOne(rest);
        return;
      }
      break;
    case ItemId::flint_and_steel: case ItemId::fire_charge:
      if (tid == B::tnt) {  // dinamita delante: se enciende
        setWorldBlock(front.x, front.y, front.z, 0);
        schedule(front, 80, TickKind::Tnt);
        neighborUpdates(front);
        if (item.id == ItemId::fire_charge) takeOne();
        else slot->meta = static_cast<i16>(slot->meta + 1);
        return;
      }
      if (useFlame(p, faceIndexOf(dir))) {
        if (item.id == ItemId::fire_charge) {
          takeOne();
        } else {
          slot->meta = static_cast<i16>(slot->meta + 1);
          if (slot->meta >= itemInfo(item.id).maxDurability) slot->clear();
        }
        return;
      }
      break;
    case ItemId::dye:
      if (item.meta == 15 && (tid == B::sapling || tid == 59 || tid == 141 || tid == 142 || tid == 104 || tid == 105 || tid == B::grass)) {
        if (tid == B::sapling) growSapling(front, true);
        else if (tid == B::grass) growGrassPatch(front);
        else setWorldBlock(front.x, front.y, front.z, makeState(tid, std::min(7, stateMeta(there) + 2 + rng_.nextInt(4))));
        takeOne();
        return;
      }
      break;
    case B::tnt:
      takeOne();
      schedule(front, 80, TickKind::Tnt);  // dinamita encendida delante: explota a los 4 s
      return;
    default: break;
  }
  if (cartTypeFromItem(item.id) >= 0) {
    // Una vagoneta sobre el raíl de delante
    const int ct = cartTypeFromItem(item.id);
    glm::ivec3 rail = front;
    if (!rails::isRail(tid) && rails::isRail(stateId(w.block(front.x, front.y - 1, front.z)))) rail = front - glm::ivec3(0, 1, 0);
    if (rails::isRail(stateId(w.block(rail.x, rail.y, rail.z))) && spawnCart(static_cast<CartType>(ct), rail)) {
      takeOne();
      return;
    }
  }
  if (item.id == ItemId::spawn_egg) {
    if (spawnEgg(item.meta, glm::dvec3(front) + glm::dvec3(0.5, 0.0, 0.5))) {
      takeOne();
      return;
    }
  }
  takeOne();
  dropOne(item);
}

/// ¿Puede entrar líquido en esa celda (aire, plantas, líquido)?
bool GameSession::fluidFree(BlockState s) const {
  const int id = stateId(s);
  return s == 0 || isFluid(id) || isReplaceable(s) || collisionBoxes(id, stateMeta(s)).empty();
}

int GameSession::faceIndexOf(const glm::ivec3& d) const {
  for (int f = 0; f < 6; f++)
    if (kFaceNormals[f][0] == d.x && kFaceNormals[f][1] == d.y && kFaceNormals[f][2] == d.z) return f;
  return 1;
}

/// Genera la criatura de un huevo (id de entidad de 1.8). false si esa criatura aún no existe.
bool GameSession::spawnEgg(int entityId, const glm::dvec3& at) {
  const auto type = mobFromEntityId(entityId);
  if (!type) return false;
  if (*type == MobType::Slime || *type == MobType::MagmaCube) spawnSized(*type, at, 1 << rng_.nextInt(3));
  else spawnMob(*type, at);
  return true;
}

// --- Bloque musical -----------------------------------------------------------------------------

/// Hace sonar el bloque musical de `p` (solo si encima hay aire).
void GameSession::playNote(const glm::ivec3& p) {
  World& w = access_.world();
  if (w.block(p.x, p.y + 1, p.z) != 0) return;
  const int below = stateId(w.block(p.x, p.y - 1, p.z));
  int instrument = 0;  // 0 piano, 1 bombo, 2 caja, 3 palillos, 4 bajo
  const std::string_view material = blockInfo(below).material;
  if (below == B::sand || below == B::gravel || below == B::soul_sand) instrument = 2;
  else if (below == B::glass || below == B::stained_glass || below == 102 || below == 160 || below == B::glowstone || below == B::sea_lantern) instrument = 3;
  else if (material == "rock") instrument = 1;
  else if (material == "wood") instrument = 4;
  SessionEvent e{SessionEvent::Type::NotePlay, p, 0};
  e.value = notes_[{p.x, p.y, p.z}].note | (instrument << 5);
  events_.push_back(e);
}

// --- Tocadiscos -----------------------------------------------------------------------------------

bool GameSession::useJukebox(const glm::ivec3& p) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  ChestState& c = chests_[{p.x, p.y, p.z}];
  if (stateMeta(s) != 0) {  // con disco dentro: lo saca
    if (!c.items[0].empty()) spawnItem(glm::dvec3(p) + glm::dvec3(0.5, 1.1, 0.5), c.items[0], {0, 0.2, 0}, 10);
    c.items[0].clear();
    setWorldBlock(p.x, p.y, p.z, makeState(kJukebox, 0));
    SessionEvent e{SessionEvent::Type::RecordStop, p, s};
    events_.push_back(e);
    return true;
  }
  ItemStack& held = player_.inventory.selected();
  if (!isRecord(held.id)) return false;
  c.items[0] = held;
  c.items[0].count = 1;
  c.items[0].extra = nullptr;
  if (!player_.creative() && --held.count <= 0) held.clear();
  setWorldBlock(p.x, p.y, p.z, makeState(kJukebox, 1));
  SessionEvent e{SessionEvent::Type::RecordStart, p, s};
  e.value = c.items[0].id;
  events_.push_back(e);
  return true;
}

}  // namespace mcw
