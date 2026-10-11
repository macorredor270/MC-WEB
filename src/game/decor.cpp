#include "game/decor.h"

#include <algorithm>
#include <cmath>

#include "core/face.h"
#include "data/blockstates.h"
#include "data/blocks.h"
#include "game/armor.h"
#include "world/world.h"

namespace mcw {

const std::vector<PaintingMotive>& paintingMotives() {
  // Nombres de 1.8 y medidas en bloques; (u, v) = su sitio en la textura de cuadros de 256x256
  static const std::vector<PaintingMotive> v = {
      {"Kebab", 1, 1, 0, 0},        {"Aztec", 1, 1, 16, 0},       {"Alban", 1, 1, 32, 0},       {"Aztec2", 1, 1, 48, 0},
      {"Bomb", 1, 1, 64, 0},        {"Plant", 1, 1, 80, 0},       {"Wasteland", 1, 1, 96, 0},   {"Pool", 2, 1, 0, 32},
      {"Courbet", 2, 1, 32, 32},    {"Sea", 2, 1, 64, 32},        {"Sunset", 2, 1, 96, 32},     {"Creebet", 2, 1, 128, 32},
      {"Wanderer", 1, 2, 0, 64},    {"Graham", 1, 2, 16, 64},     {"Match", 2, 2, 0, 128},      {"Bust", 2, 2, 32, 128},
      {"Stone", 2, 2, 64, 128},     {"Void", 2, 2, 96, 128},      {"SkullAndRoses", 2, 2, 128, 128}, {"Wither", 2, 2, 160, 128},
      {"Fighters", 4, 2, 0, 96},    {"Pointer", 4, 4, 0, 192},    {"Pigscene", 4, 4, 64, 192},  {"BurningSkull", 4, 4, 128, 192},
      {"Skeleton", 4, 3, 192, 64},  {"DonkeyKong", 4, 3, 192, 112},
  };
  return v;
}

int paintingMotiveByName(std::string_view name) {
  const auto& all = paintingMotives();
  for (std::size_t i = 0; i < all.size(); i++)
    if (name == all[i].name) return static_cast<int>(i);
  return -1;
}

glm::ivec3 decorNormal(int dir) {
  switch (dir & 3) {
    case 0: return {0, 0, 1};
    case 1: return {-1, 0, 0};
    case 2: return {0, 0, -1};
    default: return {1, 0, 0};
  }
}

int decorDirFromFace(int face) {
  switch (face) {
    case Face::South: return 0;
    case Face::West: return 1;
    case Face::North: return 2;
    case Face::East: return 3;
    default: return -1;
  }
}

namespace {

/// "Hacia la izquierda" de quien mira la cara visible del adorno (un paso a lo ancho).
glm::ivec3 leftOf(int dir) {
  // De cara a la pared, con la normal n apuntando hacia quien mira, su izquierda es n x arriba (a derechas)
  const glm::ivec3 n = decorNormal(dir);
  return {-n.z, 0, n.x};
}

}  // namespace

std::vector<glm::ivec3> hangingCells(const glm::ivec3& cell, int dir, int w, int h) {
  std::vector<glm::ivec3> out;
  const glm::ivec3 l = leftOf(dir);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) out.push_back(cell + l * x + glm::ivec3(0, y, 0));
  return out;
}

glm::dvec3 Decor::center() const {
  int w = 1, h = 1;
  double thick = 1.0 / 32.0;
  if (kind == DecorKind::Painting) {
    const PaintingMotive& m = paintingMotives()[std::min<std::size_t>(motive, paintingMotives().size() - 1)];
    w = m.w;
    h = m.h;
  }
  const glm::ivec3 n = decorNormal(dir), l = leftOf(dir);
  // El centro del lienzo: la casilla (abajo a la derecha) más medio lienzo hacia la izquierda y hacia arriba
  glm::dvec3 c = glm::dvec3(cell) + 0.5 + glm::dvec3(l) * ((w - 1) * 0.5) + glm::dvec3(0, (h - 1) * 0.5, 0);
  c -= glm::dvec3(n) * (0.5 - thick);  // pegado a la pared
  return c;
}

AABB Decor::box() const {
  if (kind == DecorKind::ArmorStand) {
    const double ww = small ? 0.25 : 0.5, hh = small ? 0.9875 : 1.975;
    return AABB::centered(pos, ww, hh);
  }
  int w = 1, h = 1;
  double half = 0.375;  // el marco mide 12 píxeles
  if (kind == DecorKind::Painting) {
    const PaintingMotive& m = paintingMotives()[std::min<std::size_t>(motive, paintingMotives().size() - 1)];
    w = m.w;
    h = m.h;
    half = 0.5;
  }
  const glm::dvec3 c = center();
  const glm::ivec3 n = decorNormal(dir);
  const glm::dvec3 ext(n.x != 0 ? 1.0 / 32.0 : (half * (kind == DecorKind::Painting ? w : 1)),
                       half * (kind == DecorKind::Painting ? h : 1),
                       n.z != 0 ? 1.0 / 32.0 : (half * (kind == DecorKind::Painting ? w : 1)));
  return {c - ext, c + ext};
}

bool boxHitsBlocks(const World& world, const AABB& box) {
  std::vector<AABB> boxes;
  const AABB shrunk{box.min + 0.001, box.max - 0.001};
  collectBlockBoxes(world, shrunk, boxes);
  for (const AABB& b : boxes)
    if (b.intersects(shrunk)) return true;
  return false;
}

bool decorOverlaps(const Decor& a, const Decor& b) {
  if (a.kind == DecorKind::ArmorStand || b.kind == DecorKind::ArmorStand) return false;
  return a.box().intersects(b.box());
}

namespace {

bool wallSolid(const World& world, const glm::ivec3& p) {
  const int id = stateId(world.block(p.x, p.y, p.z));
  if (id == 0) return false;
  const BlockInfo& bi = blockInfo(id);
  if (bi.fluid) return false;
  return bi.fullBox || blockCollision(world, p.x, p.y, p.z).size() > 0;
}

}  // namespace

bool hangingFits(const World& world, const glm::ivec3& cell, int dir, int w, int h) {
  const glm::ivec3 n = decorNormal(dir);
  for (const glm::ivec3& c : hangingCells(cell, dir, w, h)) {
    if (!wallSolid(world, c - n)) return false;
    if (world.chunk(c.x >> 4, c.z >> 4) == nullptr) return false;
  }
  return true;
}

std::optional<int> pickPainting(const World& world, const glm::ivec3& cell, int dir, const std::vector<const Decor*>& others, u32 randomBits) {
  const auto& all = paintingMotives();
  std::vector<int> fit;
  int best = 0;
  for (std::size_t i = 0; i < all.size(); i++) {
    Decor d;
    d.kind = DecorKind::Painting;
    d.cell = cell;
    d.dir = static_cast<u8>(dir);
    d.motive = static_cast<u8>(i);
    if (!hangingFits(world, cell, dir, all[i].w, all[i].h)) continue;
    // Sin bloques con colisión dentro y sin solapar otros adornos
    bool clash = boxHitsBlocks(world, d.box());
    for (const Decor* o : others)
      if (!clash && o && decorOverlaps(d, *o)) clash = true;
    if (clash) continue;
    fit.push_back(static_cast<int>(i));
    best = std::max(best, all[i].w * all[i].h);
  }
  std::erase_if(fit, [&](int i) { return all[static_cast<std::size_t>(i)].w * all[static_cast<std::size_t>(i)].h < best; });
  if (fit.empty()) return std::nullopt;
  return fit[randomBits % fit.size()];
}

bool hangingSupported(const World& world, const Decor& d) {
  int w = 1, h = 1;
  if (d.kind == DecorKind::Painting) {
    const PaintingMotive& m = paintingMotives()[std::min<std::size_t>(d.motive, paintingMotives().size() - 1)];
    w = m.w;
    h = m.h;
  }
  const glm::ivec3 n = decorNormal(d.dir);
  for (const glm::ivec3& c : hangingCells(d.cell, d.dir, w, h))
    if (!wallSolid(world, c - n)) return false;
  return true;
}

int standSlotFor(const ItemStack& s) {
  if (s.empty()) return -1;
  if (const auto info = armorInfo(s.id)) {
    switch (info->piece) {
      case ArmorPiece::Boots: return 1;
      case ArmorPiece::Leggings: return 2;
      case ArmorPiece::Chestplate: return 3;
      case ArmorPiece::Helmet: return 4;
    }
  }
  // Calabaza tallada y cabezas (bloque 86 y 144) se ponen como casco
  if (s.id == 86 || s.id == 144) return 4;
  return -1;
}

int standSlotAtHeight(double y, bool small) {
  const double k = small ? 0.5 : 1.0;
  if (y >= 1.6 * k) return 4;
  if (y >= 0.9 * k) return 3;
  if (y >= 0.4 * k) return 2;
  return 1;
}

}  // namespace mcw
