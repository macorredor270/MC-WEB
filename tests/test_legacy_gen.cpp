#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <ostream>

#include "world/legacy_gen.h"

using namespace mcw;

namespace {

u32 hash32(u32 x) {
  x ^= x >> 15;
  x *= 0xd168aaadu;
  x ^= x >> 15;
  x *= 0xaf723597u;
  x ^= x >> 15;
  return x;
}

// La prueba de referencia de Cubiomes para 1.7/1.8: un bioma (a escala 1:4) por cada una de las semillas (z << bits) ^ x.
u32 referenceHash(int bits) {
  const int r = 1 << (bits - 1);
  u32 h = 0;
  for (int x = -r; x < r; x++)
    for (int z = -r; z < r; z++) {
      const int s = (z * (1 << bits)) ^ x;
      legacy::BiomeLayers layers(static_cast<u64>(static_cast<i64>(s)), false);
      std::vector<int> out;
      layers.area4(x, z, 1, 1, out);
      h ^= hash32(static_cast<u32>(s) ^ static_cast<u32>(out[0] << (2 * bits)));
    }
  return h;
}

}  // namespace

TEST_CASE("Biomas de 1.8: la pila de capas da los mismos biomas que el juego (hash de referencia)") {
  CHECK((referenceHash(6) == 0xbc75e996u));
}

// La prueba gruesa de Cubiomes (un millón de semillas): tarda medio minuto, así que solo se corre a mano con --no-skip.
TEST_CASE("Biomas de 1.8: hash de referencia con un millón de semillas" * doctest::skip()) {
  CHECK((referenceHash(10) == 0xb3363967u));
}

#include <cmath>

#include "core/random.h"
#include "world/generator.h"

TEST_CASE("Estructuras de 1.8: cada región da siempre la misma posición, dentro de su región, y los monumentos tienden al centro") {
  for (int rz = -3; rz <= 3; rz++)
    for (int rx = -3; rx <= 3; rx++) {
      const legacy::StructPos v = legacy::villageAttempt(12345, rx, rz);
      CHECK(v.x == legacy::villageAttempt(12345, rx, rz).x);
      CHECK(v.x >= rx * 512);
      CHECK(v.x < rx * 512 + 24 * 16);
      CHECK(v.z >= rz * 512);
      CHECK(v.z < rz * 512 + 24 * 16);
      const legacy::StructPos m = legacy::monumentAttempt(12345, rx, rz);
      CHECK(m.x >= rx * 512);
      CHECK(m.x < rx * 512 + 27 * 16);
    }
  // El reparto triangular (suma de dos tiradas) acumula cerca del medio del rango (chunk 13 de 0..26)
  int middle = 0, total = 0;
  for (int rz = -20; rz < 20; rz++)
    for (int rx = -20; rx < 20; rx++) {
      const int c = (legacy::monumentAttempt(777, rx, rz).x - rx * 512) / 16;
      middle += c >= 9 && c <= 17;
      total++;
    }
  CHECK(middle > total * 45 / 100);  // (uniforme daría un 33 %)
}

TEST_CASE("Minas de 1.8: un chunk de cada 250 lejos del origen, y casi ninguno pegado a él") {
  int far = 0, near = 0;
  for (int i = 0; i < 20000; i++) {
    far += legacy::mineshaftChunk(99, 100 + i % 200, 100 + i / 200);
    near += legacy::mineshaftChunk(99, i % 20 - 10, i / 20 % 20 - 10);
  }
  CHECK(far > 40);
  CHECK(far < 120);  // (0,4 % de 20000 son 80)
  CHECK(near < far / 20);
}

TEST_CASE("Fortines de 1.8: tres, en el primer anillo (entre 640 y 1150 bloques, con el margen de buscar un bioma válido) y sin repetirse") {
  for (u64 seed = 1; seed <= 10; seed++) {
    const auto sites = strongholdPositions(seed);
    for (std::size_t i = 0; i < 3; i++) {
      const double d = std::sqrt(double(sites[i].first) * sites[i].first + double(sites[i].second) * sites[i].second);
      CHECK(d > 500);
      CHECK(d < 1400);
      CHECK((sites[i].first & 15) == 4);  // la escalera, en el (4, 4) del chunk
      for (std::size_t j = i + 1; j < 3; j++) CHECK((sites[i] != sites[j]));
    }
  }
}

TEST_CASE("Punto de aparición de 1.8: cerca del origen y en tierra firme") {
  int ok = 0;
  for (u64 seed = 1; seed <= 12; seed++) {
    TerrainGenerator g(seed);
    const auto s = g.findSpawn();
    if (std::abs(s[0]) < 600 && std::abs(s[2]) < 600 && s[1] > TerrainGenerator::kSeaLevel) ok++;
  }
  CHECK(ok >= 11);
}

TEST_CASE("Semilla de texto: el String.hashCode() de Java, como en 'crear mundo'") {
  CHECK(javaStringSeed("a") == 97u);
  CHECK(javaStringSeed("hello") == 99162322u);
  CHECK(javaStringSeed("") == 0u);
  // Con signo: "polygenelubricants".hashCode() es Integer.MIN_VALUE
  CHECK(static_cast<i64>(javaStringSeed("polygenelubricants")) == -2147483648LL);
}
