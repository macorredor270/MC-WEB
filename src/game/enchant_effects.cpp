#include "game/enchant_effects.h"

#include <algorithm>
#include <cmath>

#include "data/blocks.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "game/armor.h"
#include "game/enchantments.h"
#include "game/rules.h"

namespace mcw::enchfx {

int protectionPoints(int enchId, int level, DamageKind kind) {
  if (level <= 0 || kind == DamageKind::Void || kind == DamageKind::Starvation) return 0;
  const float base = static_cast<float>(6 + level * level) / 3.0f;
  switch (enchId) {
    case Ench::Protection: return static_cast<int>(std::floor(base * 0.75f));
    case Ench::FireProtection: return kind == DamageKind::Fire ? static_cast<int>(std::floor(base * 1.25f)) : 0;
    case Ench::FeatherFalling: return kind == DamageKind::Fall ? static_cast<int>(std::floor(base * 2.5f)) : 0;
    case Ench::BlastProtection: return kind == DamageKind::Explosion ? static_cast<int>(std::floor(base * 1.5f)) : 0;
    case Ench::ProjectileProtection: return kind == DamageKind::Projectile ? static_cast<int>(std::floor(base * 1.5f)) : 0;
    default: return 0;
  }
}

int protectionModifier(const PlayerInventory& inv, DamageKind kind, Random& rng) {
  if (bypassesEnchantments(kind)) return 0;
  int sum = 0;
  for (int i = 0; i < 4; i++) {
    const ItemStack& piece = inv.armor(i);
    if (piece.empty() || !piece.extra) continue;
    for (const auto& [id, level] : piece.extra->ench) sum += protectionPoints(id, level, kind);
  }
  if (sum <= 0) return 0;
  sum = std::min(sum, 25);
  const int k = ((sum + 1) >> 1) + rng.nextInt((sum >> 1) + 1);
  return std::min(k, 20);
}

bool bypassesArmor(DamageKind kind) {
  return kind == DamageKind::Fall || kind == DamageKind::Drowning || kind == DamageKind::Starvation || kind == DamageKind::Void;
}

bool bypassesEnchantments(DamageKind kind) { return kind == DamageKind::Starvation || kind == DamageKind::Void; }

int thornsDamage(int level, Random& rng) {
  if (level <= 0 || rng.nextFloat() >= 0.15f * static_cast<float>(level)) return 0;
  return level > 10 ? level - 10 : 1 + rng.nextInt(4);
}

bool isUndead(MobType t) { return t == MobType::Zombie || t == MobType::Skeleton; }
bool isArthropod(MobType t) { return t == MobType::Spider; }

float weaponBonus(const ItemStack& weapon, MobType target) {
  float bonus = 1.25f * static_cast<float>(weapon.enchantLevel(Ench::Sharpness));
  if (isUndead(target)) bonus += 2.5f * static_cast<float>(weapon.enchantLevel(Ench::Smite));
  if (isArthropod(target)) bonus += 2.5f * static_cast<float>(weapon.enchantLevel(Ench::BaneOfArthropods));
  return bonus;
}

float efficiencySpeed(float baseSpeed, const ItemStack& tool) {
  const int level = tool.enchantLevel(Ench::Efficiency);
  if (level <= 0 || baseSpeed <= 1.0f) return baseSpeed;
  return baseSpeed + static_cast<float>(level * level + 1);
}

int wearAfterUnbreaking(const ItemStack& item, int amount, Random& rng) {
  const int level = item.enchantLevel(Ench::Unbreaking);
  if (level <= 0) return amount;
  const bool armor = isArmor(item.id);
  int applied = 0;
  for (int i = 0; i < amount; i++) {
    // (en una armadura, el 60 % de las veces el intento ni se mira: el punto de desgaste cuenta)
    const bool negated = armor && rng.nextFloat() < 0.6f ? false : rng.nextInt(level + 1) > 0;
    if (!negated) applied++;
  }
  return applied;
}

bool wearItem(ItemStack& item, int amount, Random& rng) {
  if (item.empty() || !item.isTool()) return false;
  item.meta = static_cast<i16>(item.meta + wearAfterUnbreaking(item, amount, rng));
  if (item.meta >= itemInfo(item.id).maxDurability) {
    item.clear();
    return true;
  }
  return false;
}

ItemStack silkTouchDrop(BlockState s) {
  const int id = stateId(s), meta = stateMeta(s);
  switch (id) {
    case B::stone: case B::grass: case B::dirt: case B::mycelium: case B::coal_ore: case B::diamond_ore: case B::emerald_ore:
    case B::lapis_ore: case B::redstone_ore: case 153 /* mineral de cuarzo */: case B::glass: case B::ice: case B::packed_ice:
    case B::glowstone: case B::sea_lantern: case B::clay: case B::bookshelf: case B::melon_block: case B::snow: case B::web:
    case 130 /* cofre de ender */: case 99: case 100:  // setas gigantes
      return ItemStack(id, 1, id == B::stone || id == B::dirt ? meta : 0);
    case B::stained_glass: case B::stained_glass_pane: return ItemStack(id, 1, meta);
    case 102: return ItemStack(id, 1, 0);  // panel de cristal
    case B::leaves: case B::leaves2: return ItemStack(id, 1, meta & 3);
    case B::lit_redstone_ore: return ItemStack(B::redstone_ore, 1, 0);
    case 124: return ItemStack(123, 1, 0);  // lámpara de redstone encendida
    default: return {};
  }
}

int fortuneCount(int blockId, int baseCount, int level, Random& rng) {
  if (level <= 0) return baseCount;
  switch (blockId) {
    case B::coal_ore: case B::diamond_ore: case B::emerald_ore: case B::lapis_ore: case 153: {
      const int bonus = std::max(0, rng.nextInt(level + 2) - 1);
      return baseCount * (bonus + 1);
    }
    case B::redstone_ore: case B::lit_redstone_ore: return baseCount + rng.nextInt(level + 1);
    case B::glowstone: return std::min(4, baseCount + rng.nextInt(level + 1));
    case B::melon_block: return std::min(9, baseCount + rng.nextInt(level + 1));
    default: return baseCount;
  }
}

}  // namespace mcw::enchfx
