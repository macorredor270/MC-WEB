#pragma once
#include <algorithm>

#include "core/types.h"
#include "data/items.h"

namespace mcw {

/// Pila de ítems. En las herramientas `meta` es el desgaste acumulado (como en 1.8).
struct ItemStack {
  i16 id = 0;
  i16 meta = 0;
  i16 count = 0;

  ItemStack() = default;
  ItemStack(int id_, int count_ = 1, int meta_ = 0)
      : id(static_cast<i16>(id_)), meta(static_cast<i16>(meta_)), count(static_cast<i16>(count_)) {}

  bool empty() const { return id <= 0 || count <= 0; }
  void clear() { *this = ItemStack(); }
  int maxStack() const { return std::max(1, itemInfo(id).stackSize); }
  bool isTool() const { return itemInfo(id).maxDurability > 0; }
  /// Se pueden apilar juntas (mismo ítem y variante; las herramientas no se apilan).
  bool stacksWith(const ItemStack& o) const { return !empty() && !o.empty() && id == o.id && meta == o.meta && !isTool(); }
  bool operator==(const ItemStack&) const = default;
};

}  // namespace mcw
