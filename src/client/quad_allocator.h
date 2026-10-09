#pragma once
#include <iterator>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Reserva de quads seguidos dentro de páginas de tamaño fijo (los buffers grandes de la GPU donde viven las
/// mallas del terreno). Cada página lleva sus huecos libres ordenados por posición, para fundir los vecinos al
/// liberar, y todos los huecos están además ordenados por tamaño: se elige el más justo. No toca la GPU ni SDL:
/// quien lo usa crea el buffer de cada página nueva y copia ahí los datos.
class QuadAllocator {
 public:
  static constexpr u32 kNone = 0xFFFFFFFFu;
  struct Alloc {
    u32 page = kNone, first = 0, count = 0;  // `first` y `count` en quads, dentro de la página
    bool valid() const { return page != kNone; }
  };

  explicit QuadAllocator(u32 pageQuads) : pageQuads_(pageQuads) {}

  /// Reserva `count` quads seguidos (de 1 a `pageQuads`). Si ninguna página tiene hueco, abre una nueva y
  /// pone su índice en `newPage` (si no, lo deja como estaba).
  Alloc allocate(u32 count, u32* newPage = nullptr) {
    auto it = bySize_.lower_bound({count, 0u, 0u});
    if (it == bySize_.end()) {
      const u32 page = static_cast<u32>(pages_.size());
      pages_.push_back({});
      pages_[page].freeByStart[0] = pageQuads_;
      bySize_.insert({pageQuads_, page, 0u});
      if (newPage) *newPage = page;
      it = bySize_.lower_bound({count, 0u, 0u});
    }
    const auto [size, page, start] = *it;
    bySize_.erase(it);
    auto& m = pages_[page].freeByStart;
    m.erase(start);
    if (size > count) {
      m[start + count] = size - count;
      bySize_.insert({size - count, page, start + count});
    }
    pages_[page].used += count;
    used_ += count;
    return {page, start, count};
  }

  /// Devuelve un tramo reservado (y lo funde con los huecos de al lado).
  void release(const Alloc& a) {
    if (!a.valid() || a.count == 0) return;
    Page& pg = pages_[a.page];
    auto& m = pg.freeByStart;
    u32 start = a.first, size = a.count;
    auto next = m.lower_bound(start);
    if (next != m.end() && next->first == start + size) {  // el hueco de después
      bySize_.erase({next->second, a.page, next->first});
      size += next->second;
      next = m.erase(next);
    }
    if (next != m.begin()) {
      auto prev = std::prev(next);
      if (prev->first + prev->second == start) {  // el hueco de antes
        bySize_.erase({prev->second, a.page, prev->first});
        start = prev->first;
        size += prev->second;
        m.erase(prev);
      }
    }
    m[start] = size;
    bySize_.insert({size, a.page, start});
    pg.used -= a.count;
    used_ -= a.count;
  }

  u32 pageCount() const { return static_cast<u32>(pages_.size()); }
  u32 pageQuads() const { return pageQuads_; }
  u32 usedQuads() const { return used_; }
  u32 pageUsed(u32 page) const { return pages_[page].used; }
  /// Cuántos huecos libres hay en total (más huecos = más fragmentación).
  std::size_t freeBlocks() const { return bySize_.size(); }

 private:
  struct Page {
    std::map<u32, u32> freeByStart;  // inicio -> tamaño del hueco
    u32 used = 0;
  };
  u32 pageQuads_;
  u32 used_ = 0;
  std::vector<Page> pages_;
  std::set<std::tuple<u32, u32, u32>> bySize_;  // (tamaño, página, inicio)
};

}  // namespace mcw
