#include <doctest/doctest.h>

#include <random>
#include <vector>

#include "client/quad_allocator.h"

using namespace mcw;

TEST_CASE("Arena: reservar abre páginas y liberar las funde otra vez") {
  QuadAllocator a(1000);
  u32 page = QuadAllocator::kNone;
  const auto x = a.allocate(400, &page);
  CHECK(page == 0);
  CHECK(x.page == 0);
  CHECK(x.first == 0);
  const auto y = a.allocate(400);
  CHECK(y.page == 0);
  CHECK(y.first == 400);
  page = QuadAllocator::kNone;
  const auto z = a.allocate(400, &page);  // en la primera solo quedan 200: página nueva
  CHECK(page == 1);
  CHECK(z.page == 1);
  CHECK(a.usedQuads() == 1200);
  a.release(y);
  a.release(x);
  a.release(z);
  CHECK(a.usedQuads() == 0);
  CHECK(a.freeBlocks() == a.pageCount());  // cada página vuelve a ser un solo hueco
}

TEST_CASE("Arena: el hueco más justo se reutiliza") {
  QuadAllocator a(1000);
  const auto p = a.allocate(100), q = a.allocate(300), r = a.allocate(100), s = a.allocate(50);
  (void)s;
  a.release(p);  // hueco de 100 al principio
  a.release(q);  // se funde con el anterior: 400
  a.release(r);  // y con este: 500
  // Hueco de 500 al principio y el resto (450) al final: 200 cabe mejor en el de 450
  const auto t = a.allocate(200);
  CHECK(t.page == 0);
  CHECK(t.first == 550);
  const auto u = a.allocate(500);  // el hueco de 500 del principio, justo
  CHECK(u.first == 0);
}

TEST_CASE("Arena: reservas al azar no se pisan y todo se libera") {
  constexpr u32 kPage = 4096;
  QuadAllocator a(kPage);
  std::mt19937 rng(7);
  std::vector<QuadAllocator::Alloc> live;
  std::vector<std::vector<u8>> used;  // por página: qué quads están reservados
  auto mark = [&](const QuadAllocator::Alloc& al, u8 v) {
    if (used.size() <= al.page) used.resize(al.page + 1, std::vector<u8>(kPage, 0));
    for (u32 i = al.first; i < al.first + al.count; i++) {
      REQUIRE(used[al.page][i] != v);  // al reservar, tiene que estar libre; al liberar, reservado
      used[al.page][i] = v;
    }
  };
  u32 expectUsed = 0;
  for (int step = 0; step < 6000; step++) {
    if (live.empty() || (rng() % 100) < 55) {
      const u32 n = 1 + static_cast<u32>(rng() % 700);
      const auto al = a.allocate(n);
      REQUIRE(al.valid());
      CHECK(al.first + al.count <= kPage);
      mark(al, 1);
      live.push_back(al);
      expectUsed += n;
    } else {
      const std::size_t i = rng() % live.size();
      mark(live[i], 0);
      expectUsed -= live[i].count;
      a.release(live[i]);
      live[i] = live.back();
      live.pop_back();
    }
    CHECK(a.usedQuads() == expectUsed);
  }
  for (const auto& al : live) a.release(al);
  CHECK(a.usedQuads() == 0);
  CHECK(a.freeBlocks() == a.pageCount());
}
