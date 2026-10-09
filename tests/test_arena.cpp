#include <doctest/doctest.h>

#include <map>
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

// ---------------------------------------------------------------------------------------------------------------
// Visibilidad entre caras de una sección y recorrido por secciones

#include <set>
#include <tuple>

#include "client/mesher.h"
#include "client/visibility.h"
#include "data/blocks.h"

namespace {

MeshInput solidSection() {
  MeshInput in;
  in.blocks.fill(makeState(B::stone));
  return in;
}
void carve(MeshInput& in, int x, int y, int z) { in.blocks[MeshInput::idx(x + 1, y + 1, z + 1)] = 0; }

}  // namespace

TEST_CASE("Visibilidad: aire entero, piedra entera y un túnel") {
  MeshInput air;
  air.blocks.fill(0);
  CHECK(computeVisibility(air) == kVisAll);
  CHECK(computeVisibility(solidSection()) == 0);

  // Un túnel recto de lado a lado (de oeste a este) por el centro: solo esas dos caras se ven
  MeshInput tunnel = solidSection();
  for (int x = 0; x < 16; x++) carve(tunnel, x, 8, 8);
  CHECK(computeVisibility(tunnel) == visPairBit(Face::West, Face::East));

  // Un túnel en L: de la cara oeste a la de arriba
  MeshInput bend = solidSection();
  for (int x = 0; x <= 8; x++) carve(bend, x, 4, 8);
  for (int y = 4; y < 16; y++) carve(bend, 8, y, 8);
  CHECK(computeVisibility(bend) == visPairBit(Face::West, Face::Up));

  // Un hueco cerrado que no toca ninguna cara no ve nada
  MeshInput room = solidSection();
  for (int z = 5; z < 10; z++)
    for (int y = 5; y < 10; y++)
      for (int x = 5; x < 10; x++) carve(room, x, y, z);
  CHECK(computeVisibility(room) == 0);
}

TEST_CASE("Visibilidad: una pared parte la sección en dos regiones") {
  MeshInput in;
  in.blocks.fill(0);
  for (int z = 0; z < 16; z++)
    for (int y = 0; y < 16; y++) in.blocks[MeshInput::idx(8 + 1, y + 1, z + 1)] = makeState(B::stone);  // plano x = 8
  const u16 v = computeVisibility(in);
  CHECK((v & visPairBit(Face::West, Face::East)) == 0);  // la pared las separa
  CHECK((v & visPairBit(Face::West, Face::Up)) != 0);    // dentro de cada mitad se ve todo
  CHECK((v & visPairBit(Face::East, Face::Down)) != 0);
  CHECK((v & visPairBit(Face::North, Face::South)) != 0);
}

TEST_CASE("Visibilidad: los bloques transparentes (cristal, agua) dejan pasar la vista") {
  MeshInput in = solidSection();
  for (int x = 0; x < 16; x++) in.blocks[MeshInput::idx(x + 1, 9, 9)] = makeState(B::glass);
  CHECK(computeVisibility(in) == visPairBit(Face::West, Face::East));
  for (int x = 0; x < 16; x++) in.blocks[MeshInput::idx(x + 1, 9, 9)] = makeState(B::water);
  CHECK(computeVisibility(in) == visPairBit(Face::West, Face::East));
}

namespace {

/// Un mundo de secciones de mentira para probar el recorrido: `solid` dice cuáles tapan todo.
struct FakeWorld {
  int radius = 4;
  std::set<std::tuple<int, int, int>> solid;
  std::map<std::tuple<int, int, int>, u16> custom;  // visibilidad concreta
  SectionTraversal::Info info(int x, int y, int z) const {
    SectionTraversal::Info i;
    i.exists = std::abs(x) <= radius + 1 && std::abs(z) <= radius + 1;
    if (auto it = custom.find({x, y, z}); it != custom.end()) i.vis = it->second;
    else if (solid.count({x, y, z})) i.vis = 0;
    return i;
  }
};

std::set<std::tuple<int, int, int>> walk(SectionTraversal& t, const FakeWorld& w, int cx, int cy, int cz) {
  std::set<std::tuple<int, int, int>> seen;
  t.run(cx, cy, cz, w.radius,
        [&](int x, int y, int z) { return w.info(x, y, z); },
        [](int, int, int) { return true; },
        [&](int x, int y, int z) { seen.insert({x, y, z}); });
  return seen;
}

}  // namespace

TEST_CASE("Recorrido: en un mundo abierto se ven todas las secciones") {
  SectionTraversal t;
  FakeWorld w;
  const auto seen = walk(t, w, 0, 8, 0);
  CHECK(seen.size() == static_cast<std::size_t>(9 * 9 * 16));
  CHECK(seen.count({0, 8, 0}) == 1);
  CHECK(seen.count({4, 0, -4}) == 1);
}

TEST_CASE("Recorrido: una pared tapa lo que hay detrás, y un agujero deja ver por él") {
  SectionTraversal t;
  FakeWorld w;
  for (int z = -4; z <= 4; z++)
    for (int y = 0; y < 16; y++) w.solid.insert({2, y, z});  // pared en x = 2
  auto seen = walk(t, w, 0, 8, 0);
  CHECK(seen.count({2, 8, 0}) == 1);  // la pared misma se ve (es lo que se ve de ella)
  CHECK(seen.count({3, 8, 0}) == 0);
  CHECK(seen.count({4, 8, 3}) == 0);
  CHECK(seen.count({1, 8, 3}) == 1);  // el lado de la cámara sigue abierto

  w.custom[{2, 8, 0}] = kVisAll;  // un agujero en la pared
  seen = walk(t, w, 0, 8, 0);
  CHECK(seen.count({3, 8, 0}) == 1);  // se ve por el agujero...
  CHECK(seen.count({4, 8, 0}) == 1);
  // ...y la pared sigue tapando lo que tiene detrás de sus otras secciones: nada de su cara de atrás se alcanza
  // por delante (solo se llega a lo de detrás pasando por el agujero)
  CHECK(seen.count({2, 8, 1}) == 1);  // (la propia pared, que se ve por delante)
}

TEST_CASE("Recorrido: un túnel solo conecta de un lado al otro") {
  SectionTraversal t;
  FakeWorld w;
  for (int x = -4; x <= 4; x++)
    for (int y = 0; y < 16; y++)
      for (int z = -4; z <= 4; z++)
        if (!(y == 8 && z == 0)) w.solid.insert({x, y, z});
  for (int x = -4; x <= 4; x++) w.custom[{x, 8, 0}] = visPairBit(Face::West, Face::East);
  const auto seen = walk(t, w, 0, 8, 0);
  for (int x = 0; x <= 4; x++) CHECK(seen.count({x, 8, 0}) == 1);
  for (int x = -4; x < 0; x++) CHECK(seen.count({x, 8, 0}) == 1);  // y hacia el otro lado también
  CHECK(seen.count({3, 8, 1}) == 0);  // las secciones de piedra de al lado del túnel no se ven
  CHECK(seen.count({3, 9, 0}) == 0);
  // La de la cámara siempre se ve con sus seis vecinas
  CHECK(seen.count({0, 9, 0}) == 1);
  CHECK(seen.count({0, 8, 1}) == 1);
}

TEST_CASE("Recorrido: lo que queda fuera de la vista no se visita") {
  SectionTraversal t;
  FakeWorld w;
  std::set<std::tuple<int, int, int>> seen;
  t.run(0, 8, 0, w.radius, [&](int x, int y, int z) { return w.info(x, y, z); },
        [](int x, int, int) { return x >= 0; },  // solo el lado de x positivo está dentro de la pirámide
        [&](int x, int y, int z) { seen.insert({x, y, z}); });
  for (const auto& [x, y, z] : seen) CHECK(x >= 0);
  CHECK(seen.count({4, 8, 0}) == 1);
}
