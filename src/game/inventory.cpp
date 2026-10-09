#include "game/inventory.h"

#include "game/armor.h"

namespace mcw {

ItemStack PlayerInventory::add(ItemStack stack) {
  if (stack.empty()) return {};
  // 1) Juntar con pilas iguales
  for (int pass = 0; pass < 2 && !stack.empty(); pass++) {
    for (int i = 0; i < kSize && !stack.empty(); i++) {
      ItemStack& s = slots_[i];
      if (pass == 0) {
        if (!s.stacksWith(stack)) continue;
        const int move = std::min<int>(stack.count, s.maxStack() - s.count);
        s.count = static_cast<i16>(s.count + move);
        stack.count = static_cast<i16>(stack.count - move);
      } else if (s.empty()) {
        // 2) Huecos vacíos
        const int move = std::min<int>(stack.count, stack.maxStack());
        s = stack;
        s.count = static_cast<i16>(move);
        stack.count = static_cast<i16>(stack.count - move);
      }
    }
  }
  if (stack.count <= 0) stack.clear();
  return stack;
}

int PlayerInventory::roomFor(const ItemStack& stack) const {
  int room = 0;
  for (const ItemStack& s : slots_) {
    if (s.empty()) room += stack.maxStack();
    else if (s.stacksWith(stack)) room += s.maxStack() - s.count;
  }
  return room;
}

bool PlayerInventory::isEmpty() const {
  for (const ItemStack& s : slots_)
    if (!s.empty()) return false;
  for (const ItemStack& s : armor_)
    if (!s.empty()) return false;
  return true;
}

int PlayerInventory::armorPoints() const {
  int points = 0;
  for (const ItemStack& s : armor_)
    if (const auto info = s.empty() ? std::nullopt : armorInfo(s.id)) points += info->defense;
  return points;
}

}  // namespace mcw
