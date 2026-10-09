#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/types.h"
#include "data/items.h"

namespace mcw {

/// Lo que 1.8 guarda en la etiqueta NBT ("tag") de un objeto. Casi todos los objetos no la tienen, así que
/// la pila solo lleva un puntero (compartido: copiar una pila es barato y las etiquetas no se tocan, se sustituyen).
struct ItemExtra {
  std::vector<std::pair<i16, i16>> ench;    // encantamientos (id, nivel)
  std::vector<std::pair<i16, i16>> stored;  // libro encantado: los que guarda (StoredEnchantments)
  std::string name;                         // nombre puesto en el yunque (display.Name)
  std::vector<std::string> lore;            // descripción (display.Lore)
  i32 repairCost = 0;                       // RepairCost: lo que encarece el yunque
  i32 color = -1;                           // cuero teñido (display.color, RGB); -1 = el de serie
  bool empty() const { return ench.empty() && stored.empty() && name.empty() && lore.empty() && repairCost == 0 && color < 0; }
  bool operator==(const ItemExtra&) const = default;
};

/// Pila de ítems. En las herramientas `meta` es el desgaste acumulado (como en 1.8).
struct ItemStack {
  i16 id = 0;
  i16 meta = 0;
  i16 count = 0;
  std::shared_ptr<const ItemExtra> extra;  // etiquetas (encantamientos, nombre...); nulo si no tiene

  ItemStack() = default;
  ItemStack(int id_, int count_ = 1, int meta_ = 0)
      : id(static_cast<i16>(id_)), meta(static_cast<i16>(meta_)), count(static_cast<i16>(count_)) {}

  bool empty() const { return id <= 0 || count <= 0; }
  void clear() { *this = ItemStack(); }
  int maxStack() const { return std::max(1, itemInfo(id).stackSize); }
  bool isTool() const { return itemInfo(id).maxDurability > 0; }
  /// Mismas etiquetas (dos pilas sin etiquetas, o con las mismas).
  bool sameExtra(const ItemStack& o) const {
    if (extra == o.extra) return true;
    if (!extra || !o.extra) return false;
    return *extra == *o.extra;
  }
  /// Se pueden apilar juntas (mismo ítem, variante y etiquetas; las herramientas no se apilan).
  bool stacksWith(const ItemStack& o) const { return !empty() && !o.empty() && id == o.id && meta == o.meta && !isTool() && sameExtra(o); }
  bool operator==(const ItemStack& o) const { return id == o.id && meta == o.meta && count == o.count && sameExtra(o); }

  // --- Etiquetas ---
  bool hasEnchants() const { return extra && !extra->ench.empty(); }
  /// Nivel de un encantamiento (0 si no lo tiene).
  int enchantLevel(int enchId) const {
    if (extra)
      for (const auto& [e, l] : extra->ench)
        if (e == enchId) return l;
    return 0;
  }
  /// Quita las etiquetas vacías y devuelve el puntero compartido listo para guardar.
  void setExtra(ItemExtra e) { extra = e.empty() ? nullptr : std::make_shared<const ItemExtra>(std::move(e)); }
  ItemExtra copyExtra() const { return extra ? *extra : ItemExtra{}; }
  /// Pone (o sube) un encantamiento.
  void addEnchant(int enchId, int level) {
    ItemExtra e = copyExtra();
    for (auto& [id, l] : e.ench)
      if (id == enchId) {
        l = static_cast<i16>(level);
        setExtra(std::move(e));
        return;
      }
    e.ench.emplace_back(static_cast<i16>(enchId), static_cast<i16>(level));
    setExtra(std::move(e));
  }
};

}  // namespace mcw
