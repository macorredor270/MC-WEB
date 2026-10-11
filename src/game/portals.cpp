// Dimensiones: portal del Nether (marco de obsidiana + mechero), portal del End (12 marcos con ojos de ender) y el viaje.
#include <algorithm>
#include <cmath>

#include "core/face.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/generator.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kObsidian = 49, kFire = 51, kPortal = 90, kEndPortal = 119, kEndFrame = 120;

/// Un hueco rectangular de portal: la esquina de abajo a la izquierda, su ancho (a lo largo del eje) y su alto.
struct PortalSize {
  bool valid = false;
  glm::ivec3 corner{0};
  int width = 0, height = 0;
  glm::ivec3 along{1, 0, 0};
};

bool freeCell(int id) { return id == 0 || id == kFire || id == kPortal; }

PortalSize measurePortal(const World& w, const glm::ivec3& start, int axis) {
  PortalSize out;
  out.along = axis == 0 ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
  auto id = [&](const glm::ivec3& p) { return stateId(w.block(p.x, p.y, p.z)); };
  // Baja hasta apoyarse en la obsidiana
  glm::ivec3 base = start;
  while (base.y > 1 && freeCell(id(base - glm::ivec3(0, 1, 0)))) base.y--;
  if (id(base - glm::ivec3(0, 1, 0)) != kObsidian) return out;
  // A lo largo del eje, hacia los dos lados, hasta la obsidiana
  int left = 0, right = 0;
  while (left < 21 && freeCell(id(base - out.along * (left + 1))) && id(base - out.along * (left + 1) - glm::ivec3(0, 1, 0)) == kObsidian) left++;
  if (id(base - out.along * (left + 1)) != kObsidian) return out;
  while (right < 21 && freeCell(id(base + out.along * (right + 1))) && id(base + out.along * (right + 1) - glm::ivec3(0, 1, 0)) == kObsidian) right++;
  if (id(base + out.along * (right + 1)) != kObsidian) return out;
  const int width = left + right + 1;
  if (width < 2 || width > 21) return out;
  const glm::ivec3 corner = base - out.along * left;
  // Altura: sube mientras toda la fila esté libre y las dos columnas laterales sean de obsidiana
  int height = 0;
  for (; height < 21; height++) {
    bool rowFree = true;
    for (int i = 0; i < width && rowFree; i++) rowFree = freeCell(id(corner + out.along * i + glm::ivec3(0, height, 0)));
    if (!rowFree) break;
    if (id(corner - out.along + glm::ivec3(0, height, 0)) != kObsidian || id(corner + out.along * width + glm::ivec3(0, height, 0)) != kObsidian) return out;
  }
  if (height < 3 || height > 21) return out;
  for (int i = 0; i < width; i++)
    if (id(corner + out.along * i + glm::ivec3(0, height, 0)) != kObsidian) return out;
  out.valid = true;
  out.corner = corner;
  out.width = width;
  out.height = height;
  return out;
}

}  // namespace

void GameSession::enterDimension(int dim) {
  if (dimension_ == 1)
    if (const Mob* d = dragon()) dragonHealth_ = d->health;
  std::erase_if(mobs_, [](const Mob& m) { return m.type == MobType::EnderDragon; });
  crystals_.clear();
  fireballs_.clear();
  dimension_ = dim;
  scheduled_.clear();
  arrows_.clear();
  pendingRedstone_.clear();
  portalTicks_ = 0;
  portalCooldown_ = 0;
  travel_.reset();
  hopperCooldown_.clear();
}

/// Un fuego recién puesto en `firePos` puede encender un portal del Nether si está dentro de un marco de obsidiana.
bool GameSession::lightPortal(const glm::ivec3& firePos) {
  if (dimension_ == 1) return false;
  const World& w = access_.world();
  for (int axis = 0; axis < 2; axis++) {
    const PortalSize s = measurePortal(w, firePos, axis);
    if (!s.valid) continue;
    // El fuego tiene que estar dentro del hueco
    for (int i = 0; i < s.width; i++)
      for (int j = 0; j < s.height; j++) {
        const glm::ivec3 p = s.corner + s.along * i + glm::ivec3(0, j, 0);
        setWorldBlock(p.x, p.y, p.z, makeState(kPortal, axis == 0 ? 1 : 2));
      }
    for (int i = 0; i < s.width; i++)
      for (int j = 0; j < s.height; j++) {
        const glm::ivec3 p = s.corner + s.along * i + glm::ivec3(0, j, 0);
        neighborUpdates(p);
      }
    return true;
  }
  return false;
}

/// Un ojo de ender ha entrado en el marco `frame`: si los doce tienen su ojo, se abre el portal del End.
bool GameSession::tryEndPortalFrames(const glm::ivec3& frame) {
  World& w = access_.world();
  // El centro del anillo: probar los 3x3 posibles que dejen a `frame` en el borde (como un anillo de 5x5 sin esquinas)
  for (int cx = frame.x - 2; cx <= frame.x + 2; cx++)
    for (int cz = frame.z - 2; cz <= frame.z + 2; cz++) {
      bool ok = true;
      for (int dx = -2; dx <= 2 && ok; dx++)
        for (int dz = -2; dz <= 2 && ok; dz++) {
          const bool edge = std::abs(dx) == 2 || std::abs(dz) == 2;
          const bool corner = std::abs(dx) == 2 && std::abs(dz) == 2;
          if (!edge || corner) continue;
          const BlockState s = w.block(cx + dx, frame.y, cz + dz);
          ok = stateId(s) == kEndFrame && (stateMeta(s) & 4);
        }
      if (!ok) continue;
      for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) setWorldBlock(cx + dx, frame.y, cz + dz, makeState(kEndPortal));
      events_.push_back({SessionEvent::Type::Fizz, {cx, frame.y, cz}, 0});
      return true;
    }
  return false;
}

std::optional<glm::ivec2> GameSession::nearestStronghold(const glm::dvec3& from) const {
  if (dimension_ != 0) return std::nullopt;
  std::optional<glm::ivec2> best;
  double bestD = 1e18;
  for (const auto& [x, z] : strongholdPositions(seed_)) {
    const double d = (x - from.x) * (x - from.x) + (z - from.z) * (z - from.z);
    if (d < bestD) {
      bestD = d;
      best = glm::ivec2(x, z);
    }
  }
  return best;
}

/// Cada tick: ¿el jugador está dentro de un portal?
void GameSession::tickPortals() {
  if (remote_ || player_.dead || travel_) return;
  World& w = access_.world();
  const AABB box = player_.box();
  bool inNether = false, inEnd = false;
  const int x0 = static_cast<int>(std::floor(box.min.x)), x1 = static_cast<int>(std::floor(box.max.x));
  const int y0 = static_cast<int>(std::floor(box.min.y)), y1 = static_cast<int>(std::floor(box.max.y));
  const int z0 = static_cast<int>(std::floor(box.min.z)), z1 = static_cast<int>(std::floor(box.max.z));
  for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++)
      for (int z = z0; z <= z1; z++) {
        const int id = stateId(w.block(x, y, z));
        if (id == kPortal) inNether = true;
        // El portal del End es una placa a 12/16 de altura
        else if (id == kEndPortal && box.min.y < y + 0.75) inEnd = true;
      }
  if (portalCooldown_ > 0) portalCooldown_--;
  if (!inNether && !inEnd) {
    portalLeave_ = false;
    portalTicks_ = std::max(0, portalTicks_ - 4);
    return;
  }
  if (portalLeave_ || portalCooldown_ > 0) {
    if (portalCooldown_ <= 0 && !portalLeave_) portalLeave_ = true;
    return;
  }
  if (inEnd) {
    Travel t;
    t.dim = dimension_ == 1 ? 0 : 1;
    t.endPortal = true;
    t.target = t.dim == 1 ? glm::dvec3(100.5, 49.0, 0.5) : spawn_;
    travel_ = t;
    return;
  }
  portalTicks_++;
  if (portalTicks_ >= (player_.creative() ? 1 : 80)) {
    Travel t;
    t.dim = dimension_ == -1 ? 0 : -1;
    t.target = dimension_ == -1 ? glm::dvec3(player_.pos.x * 8.0, player_.pos.y, player_.pos.z * 8.0)
                                : glm::dvec3(player_.pos.x / 8.0, player_.pos.y, player_.pos.z / 8.0);
    travel_ = t;
    achievements_.addStat("stat.portal", 1);
    portalTicks_ = 0;
  }
}

/// Construye un portal del Nether de 2x3 con su marco en `at` (la celda inferior izquierda del hueco).
void GameSession::buildNetherPortal(const glm::ivec3& at, int axis, bool forcePlatform) {
  const glm::ivec3 along = axis == 0 ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
  const glm::ivec3 across = axis == 0 ? glm::ivec3(0, 0, 1) : glm::ivec3(1, 0, 0);
  // Despejar el sitio (a los lados del marco) y poner un suelo donde falte
  for (int a = -1; a <= 2; a++)
    for (int c = -1; c <= 1; c++) {
      for (int h = 0; h <= 4; h++) {
        if (c == 0) continue;
        const glm::ivec3 p = at + along * a + across * c + glm::ivec3(0, h, 0);
        setWorldBlock(p.x, p.y, p.z, 0);
      }
      const glm::ivec3 f = at + along * a + across * c + glm::ivec3(0, -1, 0);
      if (c != 0 && (forcePlatform || !blockInfo(stateId(access_.world().block(f.x, f.y, f.z))).opaqueCube)) setWorldBlock(f.x, f.y, f.z, makeState(kObsidian));
    }
  for (int a = -1; a <= 2; a++)
    for (int h = -1; h <= 3; h++) {
      const glm::ivec3 p = at + along * a + glm::ivec3(0, h, 0);
      const bool frame = a == -1 || a == 2 || h == -1 || h == 3;
      setWorldBlock(p.x, p.y, p.z, frame ? makeState(kObsidian) : makeState(kPortal, axis == 0 ? 1 : 2));
    }
}

void GameSession::arrive(const Travel& t) {
  World& w = access_.world();
  portalLeave_ = true;
  portalCooldown_ = 10;
  portalTicks_ = 0;
  if (t.endPortal && t.dim == 1) {
    // La plataforma de obsidiana del End
    for (int x = 98; x <= 102; x++)
      for (int z = -2; z <= 2; z++) {
        setWorldBlock(x, 48, z, makeState(kObsidian));
        for (int y = 49; y <= 52; y++) setWorldBlock(x, y, z, 0);
      }
    player_.pos = player_.prevPos = glm::dvec3(100.5, 49.0, 0.5);
    player_.motion = {0, 0, 0};
    player_.fallDistance = 0;
    achievements_.addStat("stat.endEntered", 1);
    award(Ach::TheEnd);
    if (!dragonKilled_ && !dragon()) {
      if (Mob* d = spawnMob(MobType::EnderDragon, {0.5, 90.0, 0.5})) {
        d->persistent = true;
        d->health = dragonHealth_ > 0 ? std::min(200.0f, dragonHealth_) : 200.0f;
      }
    }
    return;
  }
  if (t.dim == 0 && t.endPortal) {
    // Salir del End: al punto de reaparición
    glm::dvec3 at = spawn_;
    int y = std::min(250, static_cast<int>(at.y) + 4);
    while (y > 1 && !blockInfo(stateId(w.block(static_cast<int>(std::floor(at.x)), y - 1, static_cast<int>(std::floor(at.z))))).fullBox) y--;
    at.y = y;
    player_.pos = player_.prevPos = at;
    player_.motion = {0, 0, 0};
    player_.fallDistance = 0;
    return;
  }
  // Portal del Nether: buscar uno cerca del destino; si no hay, construirlo
  const int tx = static_cast<int>(std::floor(t.target.x)), tz = static_cast<int>(std::floor(t.target.z));
  const int radius = 40;
  std::optional<glm::ivec3> found;
  double bestD = 1e18;
  for (int x = tx - radius; x <= tx + radius; x++)
    for (int z = tz - radius; z <= tz + radius; z++) {
      if (!w.chunkAt(x, z)) continue;
      for (int y = kChunkHeight - 1; y >= 1; y--) {
        if (stateId(w.block(x, y, z)) != kPortal) continue;
        if (stateId(w.block(x, y - 1, z)) == kPortal) continue;  // la celda de abajo del hueco
        const double d = double((x - tx) * (x - tx) + (z - tz) * (z - tz)) + double((y - t.target.y) * (y - t.target.y)) * 0.01;
        if (d < bestD) {
          bestD = d;
          found = glm::ivec3(x, y, z);
        }
      }
    }
  glm::ivec3 base;
  int axis = 0;
  if (found) {
    base = *found;
    axis = stateMeta(w.block(base.x, base.y, base.z)) == 2 ? 1 : 0;
  } else {
    // Sitio libre: suelo con 4 de aire encima. En el Nether se busca de abajo arriba junto al nivel de lava; arriba, desde el cielo
    int y = -1;
    const int top = dimension_ == -1 ? 120 : 250, bottom = dimension_ == -1 ? 34 : 3;
    for (int yy = top; yy > bottom; yy--) {
      const bool floor = blockInfo(stateId(w.block(tx, yy - 1, tz))).opaqueCube && !isFluid(stateId(w.block(tx, yy - 1, tz)));
      if (!floor) continue;
      bool air = true;
      for (int h = 0; h < 4 && air; h++) air = stateId(w.block(tx, yy + h, tz)) == 0;
      if (air) {
        y = yy;
        break;
      }
    }
    const bool forced = y < 0;
    if (forced) y = std::clamp(static_cast<int>(t.target.y), dimension_ == -1 ? 40 : 64, dimension_ == -1 ? 110 : 120);
    base = glm::ivec3(tx, y, tz);
    buildNetherPortal(base, 0, forced);
    achievements_.addStat("stat.portalBuilt", 1);
  }
  // De pie delante del portal (no dentro): se sale por un lado del plano
  const glm::ivec3 across = axis == 0 ? glm::ivec3(0, 0, 1) : glm::ivec3(1, 0, 0);
  player_.pos = player_.prevPos = glm::dvec3(base.x + 0.5, base.y, base.z + 0.5) + glm::dvec3(across) * 1.0 + (axis == 0 ? glm::dvec3(0.5, 0, 0) : glm::dvec3(0, 0, 0.5));
  player_.motion = {0, 0, 0};
  player_.fallDistance = 0;
  award(Ach::Portal);
}

}  // namespace mcw
