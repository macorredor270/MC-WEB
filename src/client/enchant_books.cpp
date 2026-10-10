#include "client/enchant_books.h"

#include <algorithm>
#include <cmath>

#include "data/blockstates.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kEnchantingTable = 116;
constexpr int kRadius = 14, kUp = 6, kDown = 6;  // dónde se buscan mesas (en bloques, alrededor del jugador)
constexpr std::size_t kMaxBooks = 24;

float approach(float v, float target, float step) {
  if (v < target) return std::min(target, v + step);
  return std::max(target, v - step);
}

float wrapAngle(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}

}  // namespace

void EnchantBooks::scan(const World& world, const glm::dvec3& player) {
  const int px = static_cast<int>(std::floor(player.x)), py = static_cast<int>(std::floor(player.y)), pz = static_cast<int>(std::floor(player.z));
  std::vector<glm::ivec3> found;
  for (int y = py - kDown; y <= py + kUp; y++) {
    if (y < 0 || y > 255) continue;
    for (int z = pz - kRadius; z <= pz + kRadius; z++)
      for (int x = px - kRadius; x <= px + kRadius; x++)
        if (stateId(world.block(x, y, z)) == kEnchantingTable) found.push_back({x, y, z});
  }
  // Las más cercanas primero, y no más de las que caben
  std::sort(found.begin(), found.end(), [&](const glm::ivec3& a, const glm::ivec3& b) {
    const glm::dvec3 da = glm::dvec3(a) + 0.5 - player, db = glm::dvec3(b) + 0.5 - player;
    return glm::dot(da, da) < glm::dot(db, db);
  });
  if (found.size() > kMaxBooks) found.resize(kMaxBooks);
  std::unordered_map<std::uint64_t, State> next;
  for (const glm::ivec3& t : found) {
    auto it = states_.find(key(t));
    if (it != states_.end()) {
      next.emplace(key(t), it->second);
    } else {
      State s;
      s.table = t;
      seed_ = seed_ * 1664525u + 1013904223u;
      s.phase = static_cast<float>(seed_ >> 8) * (6.2831853f / 16777216.0f);
      s.yaw = s.phase;
      s.flipSpeed = 0.8f + static_cast<float>((seed_ >> 4) & 15) / 16.0f;
      next.emplace(key(t), s);
    }
  }
  states_ = std::move(next);
}

void EnchantBooks::update(const World& world, const glm::dvec3& player, double dt) {
  dt = std::clamp(dt, 0.0, 0.25);
  time_ += dt;
  sinceScan_ += dt;
  if (sinceScan_ >= 0.5) {
    sinceScan_ = 0;
    scan(world, player);
  }
  poses_.clear();
  const float step = static_cast<float>(dt);
  for (auto& [k, s] : states_) {
    const glm::dvec3 centre = glm::dvec3(s.table) + glm::dvec3(0.5, 0.0, 0.5);
    const double dx = player.x - centre.x, dz = player.z - centre.z, dy = player.y - centre.y;
    const bool near = dx * dx + dz * dz < 9.0 && dy > -2.0 && dy < 3.0;  // a menos de 3 bloques
    s.open = approach(s.open, near ? 1.0f : 0.0f, step * 1.6f);
    if (near) {
      // Mira hacia el jugador (como mucho 4 rad/s, por el camino corto)
      const float want = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
      const float diff = wrapAngle(want - s.yaw);
      s.yaw = wrapAngle(s.yaw + std::clamp(diff, -4.0f * step, 4.0f * step));
    } else {
      s.yaw = wrapAngle(s.yaw + 0.35f * step);
    }
    // Pasa páginas mientras está abierto
    if (s.open > 0.6f) {
      s.flip += s.flipSpeed * step;
      if (s.flip >= 1.0f) {
        s.flip = 0.0f;
        s.page++;
        seed_ = seed_ * 1664525u + 1013904223u;
        s.flipSpeed = 0.8f + static_cast<float>((seed_ >> 4) & 15) / 12.0f;
      }
    }
    BookPose p;
    // Sobre la mesa (que llega a 12/16 de alto), flotando
    const float hover = 0.28f + 0.05f * std::sin(static_cast<float>(time_) * 1.3f + s.phase) + 0.03f * s.open;
    p.pos = centre + glm::dvec3(0.0, 0.75 + static_cast<double>(hover), 0.0);
    p.yaw = s.yaw;
    p.open = s.open;
    p.flip = s.flip;
    poses_.push_back(p);
  }
}

}  // namespace mcw
