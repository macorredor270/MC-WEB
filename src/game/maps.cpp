#include "game/maps.h"

#include <algorithm>
#include <cmath>

#include "data/blocks.h"
#include "data/items.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

namespace maps {

namespace {

struct Rgb { int r, g, b; };
// Tabla de 1.8: color base -> RGB (sin sombrear)
constexpr Rgb kBase[36] = {
    {0, 0, 0},       {127, 178, 56},  {247, 233, 163}, {199, 199, 199}, {255, 0, 0},     {160, 160, 255}, {167, 167, 167}, {0, 124, 0},
    {255, 255, 255}, {164, 168, 184}, {183, 106, 47},  {112, 112, 112}, {64, 64, 255},   {104, 83, 50},   {255, 252, 245}, {216, 127, 51},
    {178, 76, 216},  {102, 153, 216}, {229, 229, 51},  {127, 204, 25},  {242, 127, 165}, {76, 76, 76},    {153, 153, 153}, {76, 127, 153},
    {127, 63, 178},  {51, 76, 178},   {102, 76, 51},   {102, 127, 51},  {153, 51, 51},   {25, 25, 25},    {250, 238, 77},  {92, 219, 213},
    {74, 128, 255},  {0, 217, 58},    {129, 86, 49},   {112, 2, 0}};

// Los colores de los 16 tintes (meta de lana y de arcilla teñida) -> color base
constexpr int kDye[16] = {8, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29};

}  // namespace

int baseColor(BlockState s) {
  const int id = stateId(s), meta = stateMeta(s);
  switch (id) {
    case 0: return 0;
    case B::grass: return 1;
    case B::dirt: return meta == 2 ? 34 : 10;
    case B::sand: case B::sandstone: case 121: case 89: return 2;  // (también la piedra del End y la piedra luminosa)
    case B::gravel: return 11;
    case 35: return kDye[meta & 15];  // lana (el meta 0 es blanca)
    case 159: return kDye[meta & 15] == 8 ? 9 : kDye[meta & 15];  // arcilla teñida
    case 172: return 15;                      // arcilla endurecida
    case B::stained_glass: case B::stained_glass_pane: case B::glass: case 102: return 0;  // el cristal no se pinta: se ve lo de debajo
    case B::water: case B::flowing_water: return 12;
    case B::lava: case 10: case B::tnt: case 152: case 51: return 4;
    case B::ice: case 174: return 5;
    case B::snow: case B::snow_layer: return 8;
    case B::leaves: case B::leaves2: case B::tallgrass: case 37: case 38: case 6: case 106: case B::cactus: case 86: case B::melon_block: case 175: return 7;
    case 82: return 9;     // arcilla
    case 19: return 18;    // esponja
    case 30: return 3;     // telaraña
    case 110: return 24;   // micelio
    case 42: return 6;     // hierro
    case B::gold_block: return 30;
    case 57: return 31;
    case 22: return 32;
    case B::emerald_block: return 33;
    case 155: case 169: return 14;  // cuarzo y linterna marina
    case 87: case 112: return 35;   // roca del Nether y ladrillo del Nether
    case 88: return 26;             // arena de almas
    case 49: case 173: return 29;   // obsidiana y carbón
    case 45: return 28;             // ladrillo
    case 170: return 18;            // fardo de heno
    case 168: return meta == 0 ? 23 : 31;  // prismarina
    case 138: return 31;
    case 5: case B::log: case B::log2: case 47: case 58: case 54: case 85: case 107: case 53: case 134: case 135: case 136: case 64: case 125: case 126: case 25:
      return 13;  // madera
    default: break;
  }
  return blockInfo(id).opaqueCube ? 11 : 0;  // lo demás: roca si es un bloque entero; si no, no se pinta
}

u32 rgb(u8 color) {
  const int base = color >> 2, shade = color & 3;
  if (base == 0 || base >= 36) return 0;
  static constexpr int kMul[4] = {180, 220, 255, 135};
  const Rgb c = kBase[base];
  return (static_cast<u32>(c.r * kMul[shade] / 255) << 16) | (static_cast<u32>(c.g * kMul[shade] / 255) << 8) | static_cast<u32>(c.b * kMul[shade] / 255);
}

namespace {

/// La altura (y del primer bloque que se pinta, desde arriba) y su color base de la columna; false si no hay chunk.
bool topColor(const World& w, int x, int z, int& height, int& base, int& waterDepth) {
  const Chunk* c = w.chunkAt(x, z);
  if (!c) return false;
  const int lx = x & 15, lz = z & 15;
  int y = std::min(kChunkHeight - 1, c->height(lx, lz) + 4);
  base = 0;
  waterDepth = 0;
  for (; y > 0; y--) {
    const int col = baseColor(c->block(lx, y, lz));
    if (col == 0) continue;
    base = col;
    height = y;
    if (col == 12) {  // agua: cuánta hay hasta el fondo
      int d = 0;
      while (y - d > 0 && isWater(stateId(c->block(lx, y - d, lz)))) d++;
      waterDepth = d;
    }
    return true;
  }
  height = 0;
  return true;
}

}  // namespace

u8 pixel(const World& w, int x, int z, int scale) {
  int h = 0, base = 0, depth = 0;
  if (!topColor(w, x, z, h, base, depth) || base == 0) return 0;
  int shade;
  if (base == 12) {  // el agua se oscurece con la profundidad (y hace un tablero de ajedrez)
    const double d = depth * 0.1 + ((x + z) & 1) * 0.2;
    shade = d < 0.5 ? 2 : d > 0.9 ? 0 : 1;
  } else {
    int hn = h, bn = 0, dn = 0;
    if (!topColor(w, x, z - (1 << scale), hn, bn, dn)) hn = h;
    const double d = (h - hn) * 4.0 / (scale + 4) + (((x + z) & 1) - 0.5) * 0.4;
    shade = d > 0.6 ? 2 : d < -0.6 ? 0 : 1;
  }
  return static_cast<u8>(base * 4 + shade);
}

void centerFor(double x, double z, int scale, int& cx, int& cz) {
  const int span = kMapSize << scale;
  cx = static_cast<int>(std::floor((x + 64.0) / span)) * span + (span / 2 - 64);
  cz = static_cast<int>(std::floor((z + 64.0) / span)) * span + (span / 2 - 64);
}

}  // namespace maps

// --- En la partida ------------------------------------------------------------------------------------------------

MapData* GameSession::mapById(int id) {
  const auto it = maps_.find(id);
  return it == maps_.end() ? nullptr : &it->second;
}

int GameSession::createMap(int centerX, int centerZ, int scale) {
  MapData d;
  d.id = nextMapId_++;
  d.scale = std::clamp(scale, 0, 4);
  d.dimension = dimension_;
  d.centerX = centerX;
  d.centerZ = centerZ;
  d.dirty = true;
  return maps_.emplace(d.id, std::move(d)).first->second.id;
}

void GameSession::setMapData(const MapData& d) {
  MapData& m = maps_[d.id];
  m = d;
  m.version++;
  m.dirty = false;
  nextMapId_ = std::max(nextMapId_, d.id + 1);
}

bool GameSession::useEmptyMap() {
  ItemStack& held = player_.inventory.selected();
  if (held.id != ItemId::map || held.empty()) return false;
  int cx, cz;
  maps::centerFor(player_.pos.x, player_.pos.z, 0, cx, cz);
  const int id = createMap(cx, cz, 0);
  const ItemStack filled(ItemId::filled_map, 1, id);
  held.count--;  // (también en creativo, como en 1.8)
  if (held.count <= 0) held.clear();
  if (held.empty()) {
    held = filled;
  } else {
    const ItemStack rest = player_.inventory.add(filled);
    if (!rest.empty()) throwItem(rest);
  }
  return true;
}

void GameSession::updateHeldMap() {
  ItemStack& held = player_.inventory.selected();
  if (held.id != ItemId::filled_map || held.empty()) return;
  int id = held.meta & ~kMapZoomFlag;
  MapData* m = mapById(id);
  if (held.meta & kMapZoomFlag) {  // un mapa de "alejar": se estrena con la escala siguiente, centrado donde se hizo
    const int scale = m ? std::min(4, m->scale + 1) : 0;
    int cx, cz;
    if (m) {
      cx = m->centerX;
      cz = m->centerZ;
      maps::centerFor(cx, cz, scale, cx, cz);
    } else {
      maps::centerFor(player_.pos.x, player_.pos.z, scale, cx, cz);
    }
    id = createMap(cx, cz, scale);
    held.meta = static_cast<i16>(id);
    m = mapById(id);
  }
  if (!m) {  // un mapa sin datos (de otro mundo): se crea vacío donde está el jugador
    int cx, cz;
    maps::centerFor(player_.pos.x, player_.pos.z, 0, cx, cz);
    MapData d;
    d.id = id;
    d.centerX = cx;
    d.centerZ = cz;
    d.dimension = dimension_;
    d.dirty = true;
    nextMapId_ = std::max(nextMapId_, id + 1);
    m = &maps_.emplace(id, std::move(d)).first->second;
  }
  if (m->dimension != dimension_) return;
  // Va pintando lo que tiene alrededor: cada tick, dos columnas (de izquierda a derecha) de los píxeles que quedan cerca de quien lo lleva
  const World& w = access_.world();
  const int span = 1 << m->scale;
  const int x0 = m->centerX - m->blocks() / 2, z0 = m->centerZ - m->blocks() / 2;
  const double reach = 64.0;  // bloques (se pinta un círculo alrededor de quien lo lleva)
  bool changed = false;
  for (int k = 0; k < 2; k++) {
    const int col = (mapTick_ * 2 + k) % kMapSize;
    const int wx = x0 + col * span;
    if (std::abs(wx + span / 2.0 - player_.pos.x) > reach) continue;
    for (int row = 0; row < kMapSize; row++) {
      const int wz = z0 + row * span;
      const double dx = wx + span / 2.0 - player_.pos.x, dz = wz + span / 2.0 - player_.pos.z;
      if (dx * dx + dz * dz > reach * reach) continue;
      const u8 c = maps::pixel(w, wx + span / 2, wz + span / 2, m->scale);
      if (c == 0) continue;
      u8& slot = m->colors[static_cast<std::size_t>(row * kMapSize + col)];
      if (slot != c) {
        slot = c;
        changed = true;
      }
    }
  }
  if (changed) {
    m->version++;
    m->dirty = true;
  }
}

}  // namespace mcw
