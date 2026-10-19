#include "game/beacon.h"

#include "data/blocks.h"
#include "data/items.h"
#include "game/effects.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

namespace {
bool metalBase(int id) { return id == B::iron_block || id == B::gold_block || id == B::diamond_block || id == B::emerald_block; }
}  // namespace

int beaconPyramidLevels(const World& w, int x, int y, int z) {
  int levels = 0;
  for (int l = 1; l <= 4; l++) {
    for (int dz = -l; dz <= l; dz++)
      for (int dx = -l; dx <= l; dx++)
        if (!metalBase(stateId(w.block(x + dx, y - l, z + dz)))) return levels;
    levels = l;
  }
  return levels;
}

bool beaconSkyClear(const World& w, int x, int y, int z) {
  for (int yy = y + 1; yy < 256; yy++) {
    const int id = stateId(w.block(x, yy, z));
    if (id == B::bedrock) return false;
    if (id != 0 && blockInfo(id).opaqueCube && id != B::leaves && id != B::leaves2) return false;
  }
  return true;
}

std::vector<u32> beaconBeamColors(const World& w, int x, int y, int z) {
  static const u32 kDye[16] = {0xFFFFFF, 0xD87F33, 0xB24CD8, 0x6699D8, 0xE5E533, 0x7FCC19, 0xF27FA5, 0x4C4C4C,
                               0x999999, 0x4C7F99, 0x7F3FB2, 0x334CB2, 0x664C33, 0x667F33, 0x993333, 0x191919};
  std::vector<u32> out;
  for (int yy = y + 1; yy < 256; yy++) {
    const BlockState s = w.block(x, yy, z);
    if (stateId(s) == B::stained_glass || stateId(s) == B::stained_glass_pane) out.push_back(kDye[15 - (stateMeta(s) & 15)]);
  }
  return out;
}

bool beaconPrimaryAllowed(int effect, int levels) {
  if (levels >= 1 && (effect == fx::Speed || effect == fx::Haste)) return true;
  if (levels >= 2 && (effect == fx::Resistance || effect == fx::JumpBoost)) return true;
  return levels >= 3 && effect == fx::Strength;
}

bool beaconSecondaryAllowed(int effect, int primary, int levels) {
  if (levels < 4) return false;
  return effect == fx::Regeneration || (effect == primary && primary > 0);
}

bool beaconPayment(int itemId) {
  return itemId == ItemId::iron_ingot || itemId == ItemId::gold_ingot || itemId == ItemId::emerald || itemId == ItemId::diamond;
}

// --- En la partida ------------------------------------------------------------------------------------------------

bool GameSession::confirmBeacon(const glm::ivec3& pos, int primary, int secondary, ItemStack& payment, bool creative) {
  World& w = access_.world();
  if (stateId(w.block(pos.x, pos.y, pos.z)) != B::beacon) return false;
  const int levels = beaconPyramidLevels(w, pos.x, pos.y, pos.z);
  if (levels <= 0 || !beaconSkyClear(w, pos.x, pos.y, pos.z)) return false;
  if (!beaconPrimaryAllowed(primary, levels)) return false;
  if (secondary != 0 && !beaconSecondaryAllowed(secondary, primary, levels)) return false;
  if (!creative) {
    if (payment.empty() || !beaconPayment(payment.id)) return false;
    if (--payment.count <= 0) payment.clear();
  }
  BeaconData& d = tiles_.beacons[{pos.x, pos.y, pos.z}];
  d.primary = primary;
  d.secondary = secondary;
  if (tileListener_) tileListener_(pos);
  return true;
}

void GameSession::tickBeacons() {
  if (tiles_.beacons.empty() || tickCount_ % 80 != 0) return;
  World& w = access_.world();
  for (const auto& [key, data] : tiles_.beacons) {
    const auto [x, y, z] = key;
    if (stateId(w.block(x, y, z)) != B::beacon) continue;
    const int levels = beaconPyramidLevels(w, x, y, z);
    if (levels <= 0 || !beaconSkyClear(w, x, y, z)) continue;
    if (levels == 4 && data.primary != 0 && data.secondary != 0 && localActive_) award(Ach::FullBeacon);
    const double range = levels * 10 + 10;
    const int ticks = (9 + levels * 2) * 20;
    for (Player* p : activePlayers()) {
      if (p->dead || std::abs(p->pos.x - (x + 0.5)) > range || std::abs(p->pos.z - (z + 0.5)) > range) continue;
      if (data.primary > 0) applyEffect(*p, data.primary, levels >= 4 && data.secondary == data.primary ? 1 : 0, ticks);
      if (data.secondary > 0 && data.secondary != data.primary) applyEffect(*p, data.secondary, 0, ticks);
    }
  }
}

}  // namespace mcw
