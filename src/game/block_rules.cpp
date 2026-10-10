// Colocar bloques como en 1.8: qué bloque pone cada ítem y con qué orientación según la cara
// golpeada y hacia dónde mira el jugador. Comportamiento descrito en minecraft.wiki ("Data values"
// y la página de cada bloque); implementación propia.
#include <cmath>

#include "core/face.h"
#include "data/blocks.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/rules.h"
#include "world/world.h"

namespace mcw {
namespace {

// Índices horizontales de 1.8: 0 sur, 1 oeste, 2 norte, 3 este
constexpr int kHDX[4] = {0, -1, 0, 1};
constexpr int kHDZ[4] = {1, 0, -1, 0};

int opposite(int h) { return (h + 2) & 3; }

/// Índice horizontal de una cara lateral (Face::North...), -1 si es arriba o abajo.
int faceToH(int face) {
  switch (face) {
    case Face::South: return 0;
    case Face::West: return 1;
    case Face::North: return 2;
    case Face::East: return 3;
    default: return -1;
  }
}

/// Cara (Face) de un índice horizontal.
int hToFace(int h) {
  static const int f[4] = {Face::South, Face::West, Face::North, Face::East};
  return f[h & 3];
}

/// Metadata de dirección de 6 valores de 1.8 (0 abajo, 1 arriba, 2 norte, 3 sur, 4 oeste, 5 este) de una cara.
int face6(int face) {
  switch (face) {
    case Face::Down: return 0;
    case Face::Up: return 1;
    case Face::North: return 2;
    case Face::South: return 3;
    case Face::West: return 4;
    default: return 5;
  }
}

bool isSlab(int id) { return id == 44 || id == 126 || id == 182; }
int doubleSlabOf(int id) { return id == 44 ? 43 : (id == 126 ? 125 : 181); }

bool solidAt(const World& w, int x, int y, int z) { return blockInfo(stateId(w.block(x, y, z))).opaqueCube; }

}  // namespace

int horizontalFacing(float yaw) {
  const double fx = -std::sin(yaw), fz = -std::cos(yaw);
  if (std::abs(fx) > std::abs(fz)) return fx > 0 ? 3 : 1;
  return fz > 0 ? 0 : 2;
}

int blockForItem(const ItemStack& s) {
  if (s.empty()) return -1;
  if (isBlockItem(s.id)) return s.id;
  switch (s.id) {
    case ItemId::wooden_door: return 64;
    case ItemId::iron_door: return 71;
    case 427: return 193;
    case 428: return 194;
    case 429: return 195;
    case 430: return 196;
    case 431: return 197;
    case ItemId::sign: return 63;
    case ItemId::bed: return 26;
    case ItemId::reeds: return B::reeds;
    case ItemId::cake: return 92;
    case ItemId::repeater: return 93;
    case ItemId::comparator: return 149;
    case ItemId::cauldron: return 118;
    case ItemId::brewing_stand: return 117;
    case ItemId::flower_pot: return 140;
    case ItemId::skull: return 144;
    case ItemId::banner: return 176;
    case ItemId::redstone: return 55;
    case ItemId::wheat_seeds: return 59;
    case ItemId::carrot: return 141;
    case ItemId::potato: return 142;
    case ItemId::pumpkin_seeds: return 104;
    case ItemId::melon_seeds: return 105;
    case ItemId::nether_wart: return 115;
    case ItemId::string: return 132;
    case ItemId::dye: return s.meta == 3 ? 127 : -1;  // granos de cacao
    default: return -1;
  }
}

bool isPlaceableItem(const ItemStack& s) {
  const int id = blockForItem(s);
  if (id < 0) return false;
  if (id == B::double_plant) return s.meta < 6;
  if (id == B::torch || id == 75 || id == 76 || id == 65) return true;
  if (dependsOnNeighbors(id)) return blockstateOf(makeState(id, 0)).has_value();
  int meta = isBlockItem(s.id) ? s.meta : 0;
  if (id == B::log || id == B::log2) meta &= 3;
  return blockstateOf(makeState(id, meta)).has_value();
}

ItemStack pickItem(BlockState st) {
  int id = stateId(st), meta = stateMeta(st);
  switch (id) {
    case 64: return ItemStack(ItemId::wooden_door);
    case 71: return ItemStack(ItemId::iron_door);
    case 193: case 194: case 195: case 196: case 197: return ItemStack(427 + (id - 193));
    case 63: case 68: return ItemStack(ItemId::sign);
    case 26: return ItemStack(ItemId::bed);
    case B::reeds: return ItemStack(ItemId::reeds);
    case 92: return ItemStack(ItemId::cake);
    case 93: case 94: return ItemStack(ItemId::repeater);
    case 149: case 150: return ItemStack(ItemId::comparator);
    case 118: return ItemStack(ItemId::cauldron);
    case 117: return ItemStack(ItemId::brewing_stand);
    case 140: return ItemStack(ItemId::flower_pot);
    case 144: return ItemStack(ItemId::skull);
    case 176: case 177: return ItemStack(ItemId::banner, 1, 15);
    case 55: return ItemStack(ItemId::redstone);
    case 59: return ItemStack(ItemId::wheat_seeds);
    case 141: return ItemStack(ItemId::carrot);
    case 142: return ItemStack(ItemId::potato);
    case 104: return ItemStack(ItemId::pumpkin_seeds);
    case 105: return ItemStack(ItemId::melon_seeds);
    case 115: return ItemStack(ItemId::nether_wart);
    case 132: return ItemStack(ItemId::string);
    case 127: return ItemStack(ItemId::dye, 1, 3);
    case 75: return ItemStack(76);
    case 124: return ItemStack(123);
    case B::lit_furnace: return ItemStack(B::furnace);
    case B::lit_redstone_ore: return ItemStack(B::redstone_ore);
    case 43: return ItemStack(44, 1, meta & 7);
    case 125: return ItemStack(126, 1, meta & 7);
    case 181: return ItemStack(182, 1, 0);
    case 60: return ItemStack(B::dirt);
    case 34: case 36: case 51: case 90: case 119: case B::air: return {};
    default: break;
  }
  if (isFluid(id)) return {};
  // Metadata que conserva el ítem
  switch (id) {
    case B::log: case B::log2: case B::leaves: case B::leaves2: meta &= 3; break;
    case B::double_plant: meta &= 7; break;
    case 44: case 126: case 182: meta &= 7; break;
    case 145: meta >>= 2; break;  // yunque: el daño
    case B::quartz_block: meta = std::min(meta, 2); break;
    case 170: meta = 0; break;
    case B::sapling: meta &= 7; break;
    case B::stone: case B::dirt: case B::planks: case B::sand: case B::sandstone: case B::wool: case B::stained_glass:
    case B::stained_hardened_clay: case 160: case 171: case B::red_flower: case B::tallgrass: case B::stonebrick: case 97:
    case 139: case 168: case 179: case B::sponge: break;
    default: meta = 0; break;
  }
  if (id == 145) return ItemStack(145, 1, meta);
  return ItemStack(id, 1, meta);
}

glm::ivec3 supportOffset(BlockState st) {
  const int id = stateId(st), meta = stateMeta(st);
  switch (id) {
    case B::torch: case 75: case 76:
      switch (meta) {
        case 1: return {-1, 0, 0};
        case 2: return {1, 0, 0};
        case 3: return {0, 0, -1};
        case 4: return {0, 0, 1};
        default: return {0, -1, 0};
      }
    case 77: case 143: case 69: {
      const int m = meta & 7;
      if (m == 0 || (id == 69 && m == 7)) return {0, 1, 0};
      if (m == 5 || (id == 69 && m == 6)) return {0, -1, 0};
      static const glm::ivec3 side[5] = {{0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
      return side[m];
    }
    case 65: case 68: case 177: {
      switch (meta) {
        case 2: return {0, 0, 1};
        case 3: return {0, 0, -1};
        case 4: return {1, 0, 0};
        case 5: return {-1, 0, 0};
        default: return {0, 0, 0};
      }
    }
    case 131: {  // gancho: mira en la dirección opuesta a su apoyo
      const int h = meta & 3;
      return {-kHDX[h], 0, -kHDZ[h]};
    }
    case 127: {  // cacao: mira hacia el tronco
      const int h = meta & 3;
      return {kHDX[h], 0, kHDZ[h]};
    }
    default: return {0, 0, 0};
  }
}

std::optional<Placement> placementFor(const World& w, const ItemStack& held, const RayHit& hit, float yaw, float pitch) {
  if (!isPlaceableItem(held)) return std::nullopt;
  const int id = blockForItem(held);
  const BlockState target = w.block(hit.block.x, hit.block.y, hit.block.z);
  const double fracY = hit.point.y - std::floor(hit.point.y);
  const int playerH = horizontalFacing(yaw);

  // Losas: poner una encima de otra del mismo tipo forma una losa doble
  if (isSlab(id)) {
    const int type = held.meta & 7;
    auto merges = [&](BlockState s, bool fromAbove) {
      return stateId(s) == id && (stateMeta(s) & 7) == type && (((stateMeta(s) & 8) != 0) == !fromAbove);
    };
    if ((hit.face == Face::Up && merges(target, true)) || (hit.face == Face::Down && merges(target, false)))
      return Placement{hit.block, makeState(doubleSlabOf(id), type)};
  }

  Placement out;
  glm::ivec3 pos = hit.block;
  int face = hit.face;
  if (!isReplaceable(target)) {
    pos += glm::ivec3(kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]);
  } else {
    face = Face::Up;  // se sustituye el propio bloque (p. ej. hierba alta): como apoyar en el de abajo
  }
  if (pos.y < 0 || pos.y >= kChunkHeight) return std::nullopt;
  const BlockState there = w.block(pos.x, pos.y, pos.z);
  // La losa del hueco de al lado también se completa
  if (isSlab(id) && stateId(there) == id && (stateMeta(there) & 7) == (held.meta & 7))
    return Placement{pos, makeState(doubleSlabOf(id), held.meta & 7)};
  if (!isReplaceable(there)) return std::nullopt;

  int meta = isBlockItem(held.id) ? held.meta : 0;
  const int hitH = faceToH(face);
  switch (id) {
    case B::log: case B::log2: {
      const int axis = (face == Face::Up || face == Face::Down) ? 0 : (face == Face::East || face == Face::West ? 4 : 8);
      meta = (held.meta & 3) | axis;
      break;
    }
    case 170: meta = (face == Face::Up || face == Face::Down) ? 0 : (face == Face::East || face == Face::West ? 4 : 8); break;
    case B::quartz_block:
      if (held.meta == 2) meta = (face == Face::Up || face == Face::Down) ? 2 : (face == Face::East || face == Face::West ? 3 : 4);
      break;
    case B::torch: case 75: case 76: {
      static const int kTorchMeta[6] = {-1, 5, 4, 3, 2, 1};  // por cara golpeada: abajo no vale
      meta = kTorchMeta[face];
      if (meta < 0) return std::nullopt;
      break;
    }
    case B::furnace: case B::lit_furnace: case 54: case 146: case 130:
      meta = face6(hToFace(opposite(playerH)));
      break;
    case 23: case 158: case 29: case 33: {  // mira hacia el jugador, también arriba o abajo
      if (pitch < -0.8f) meta = 1;
      else if (pitch > 0.8f) meta = 0;
      else meta = face6(hToFace(opposite(playerH)));
      break;
    }
    case B::leaves: case B::leaves2: meta = (held.meta & 3) | 4; break;  // las que se ponen a mano no se secan
    case B::double_plant: meta = held.meta & 7; break;
    case 44: case 126: case 182:
      meta = (held.meta & 7) | ((face == Face::Down || (face != Face::Up && fracY > 0.5)) ? 8 : 0);
      break;
    case 53: case 67: case 108: case 109: case 114: case 128: case 134: case 135: case 136: case 156: case 163: case 164:
    case 180: {
      static const int kStair[4] = {2, 1, 3, 0};  // índice horizontal -> metadata (sur 2, oeste 1, norte 3, este 0)
      meta = kStair[playerH] | ((face == Face::Down || (face != Face::Up && fracY > 0.5)) ? 4 : 0);
      break;
    }
    case 64: case 71: case 193: case 194: case 195: case 196: case 197: {
      if (pos.y + 1 >= kChunkHeight || !isReplaceable(w.block(pos.x, pos.y + 1, pos.z))) return std::nullopt;
      if (!solidAt(w, pos.x, pos.y - 1, pos.z)) return std::nullopt;
      static const int kDoor[4] = {1, 2, 3, 0};  // índice horizontal -> metadata (0 este, 1 sur, 2 oeste, 3 norte)
      const int lower = kDoor[playerH];
      // Bisagra a la derecha si a la izquierda hay otra puerta (puertas dobles)
      const int leftH = (playerH + 3) & 3;
      const int leftId = stateId(w.block(pos.x + kHDX[leftH], pos.y, pos.z + kHDZ[leftH]));
      const int hinge = leftId == id ? 1 : 0;
      out.pos = pos;
      out.state = makeState(id, lower);
      out.hasSecond = true;
      out.secondPos = pos + glm::ivec3(0, 1, 0);
      out.secondState = makeState(id, 8 | hinge);
      return out;
    }
    case 26: {  // cama: los pies aquí y la cabecera en la dirección en la que mira el jugador
      const glm::ivec3 head = pos + glm::ivec3(kHDX[playerH], 0, kHDZ[playerH]);
      if (!isReplaceable(w.block(head.x, head.y, head.z))) return std::nullopt;
      if (!solidAt(w, pos.x, pos.y - 1, pos.z) || !solidAt(w, head.x, head.y - 1, head.z)) return std::nullopt;
      out.pos = pos;
      out.state = makeState(26, playerH);
      out.hasSecond = true;
      out.secondPos = head;
      out.secondState = makeState(26, 8 | playerH);
      return out;
    }
    case 107: case 183: case 184: case 185: case 186: case 187: meta = playerH; break;
    case 86: case 91: case 120: meta = opposite(playerH); break;
    case 93: case 149: meta = opposite(playerH); break;
    case 145: meta = ((playerH + 1) & 3) | ((held.meta & 3) << 2); break;
    case 96: case 167: {  // trampilla: bisagra contra la cara golpeada
      static const int kTrap[4] = {1, 2, 0, 3};  // índice horizontal -> metadata (0 norte, 1 sur, 2 oeste, 3 este)
      if (hitH >= 0) meta = kTrap[hitH] | (fracY > 0.5 ? 8 : 0);
      else meta = kTrap[opposite(playerH)] | (face == Face::Down ? 8 : 0);
      break;
    }
    case 65: case 68: case 177:  // escalera de mano, cartel y estandarte en la pared
      if (hitH < 0) return std::nullopt;
      meta = face6(face);
      break;
    case 63: case 176: {  // en la pared si se golpea un lado
      if (hitH >= 0) {
        const int wall = id == 63 ? 68 : 177;
        return Placement{pos, makeState(wall, face6(face))};
      }
      if (face == Face::Down) return std::nullopt;
      const double mcYaw = 180.0 - yaw * 180.0 / 3.14159265358979;
      meta = static_cast<int>(std::floor((mcYaw + 180.0) * 16.0 / 360.0 + 0.5)) & 15;
      break;
    }
    case 144: meta = hitH >= 0 ? face6(face) : 1; break;
    case 131: if (hitH < 0) return std::nullopt; meta = hitH; break;
    case 127: {  // cacao: solo en troncos de jungla
      if (hitH < 0) return std::nullopt;
      const BlockState log = target;
      if (stateId(log) != B::log || (stateMeta(log) & 3) != 3) return std::nullopt;
      meta = opposite(hitH);
      break;
    }
    case 106: {  // enredadera: bits 1 sur, 2 oeste, 4 norte, 8 este = lado donde está su apoyo
      if (hitH < 0) return std::nullopt;
      static const int bitOf[4] = {1, 2, 4, 8};
      meta = bitOf[opposite(hitH)];
      break;
    }
    case 69: {  // palanca
      if (face == Face::Up) meta = (playerH == 0 || playerH == 2) ? 5 : 6;
      else if (face == Face::Down) meta = (playerH == 0 || playerH == 2) ? 7 : 0;
      else meta = face6(face) == 5 ? 1 : (face6(face) == 4 ? 2 : (face6(face) == 3 ? 3 : 4));
      break;
    }
    case 77: case 143: {  // botones
      if (face == Face::Up) meta = 5;
      else if (face == Face::Down) meta = 0;
      else meta = face6(face) == 5 ? 1 : (face6(face) == 4 ? 2 : (face6(face) == 3 ? 3 : 4));
      break;
    }
    case 154: {  // tolva: apunta al bloque golpeado
      const int into = kOppositeFace[face];
      meta = into == Face::Up ? 0 : face6(into);
      break;
    }
    case 27: case 28: case 66: case 157: meta = (playerH == 1 || playerH == 3) ? 1 : 0; break;
    case B::wool: case B::stained_glass: case B::stained_hardened_clay: case 160: case 171: meta = held.meta & 15; break;
    case 55: case 59: case 141: case 142: case 104: case 105: case 115: case 132: case 92: case 118: case 117: case 140:
      meta = 0;
      break;
    default: break;
  }
  const BlockState state = makeState(id, meta);
  if (id == B::double_plant) {
    if (pos.y + 1 >= kChunkHeight || !isReplaceable(w.block(pos.x, pos.y + 1, pos.z))) return std::nullopt;
    const int below = stateId(w.block(pos.x, pos.y - 1, pos.z));
    if (!(below == B::grass || below == B::dirt)) return std::nullopt;
    out.pos = pos;
    out.state = state;
    out.hasSecond = true;
    out.secondPos = pos + glm::ivec3(0, 1, 0);
    out.secondState = makeState(B::double_plant, 8);
    return out;
  }
  if (!canStay(w, pos.x, pos.y, pos.z, state)) return std::nullopt;
  out.pos = pos;
  out.state = state;
  return out;
}

}  // namespace mcw
