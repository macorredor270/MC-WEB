#include <doctest/doctest.h>

#include <cmath>

#include "client/audio.h"
#include "data/blocks.h"

using namespace mcw;

TEST_CASE("Sonidos sintetizados: todos existen, sin silencios ni saturación") {
  for (int s = 0; s < static_cast<int>(Sfx::Count); s++) {
    for (int v = 0; v < 3; v++) {
      const std::vector<float> w = synthesize(static_cast<Sfx>(s), v, 44100);
      CAPTURE(s);
      CAPTURE(v);
      REQUIRE(w.size() > 441);          // más de 10 ms
      CHECK(w.size() < 44100 * 3);      // menos de 3 s
      float peak = 0, energy = 0;
      bool finite = true;
      for (float x : w) {
        finite = finite && std::isfinite(x);
        peak = std::max(peak, std::abs(x));
        energy += x * x;
      }
      CHECK(finite);
      CHECK(peak <= 1.0f);
      CHECK(peak > 0.1f);
      CHECK(energy / w.size() > 1e-5f);
      CHECK(std::abs(w.back()) < 0.05f);  // termina sin chasquido
    }
  }
}

TEST_CASE("Sonido de cada bloque según su material") {
  CHECK(blockSound(B::stone) == Sfx::DigStone);
  CHECK(blockSound(B::planks) == Sfx::DigWood);
  CHECK(blockSound(B::log) == Sfx::DigWood);
  CHECK(blockSound(B::grass) == Sfx::DigGrass);
  CHECK(blockSound(B::dirt) == Sfx::DigGravel);
  CHECK(blockSound(B::sand) == Sfx::DigSand);
  CHECK(blockSound(B::glass) == Sfx::DigGlass);
  CHECK(blockSound(B::wool) == Sfx::DigCloth);
  CHECK(blockSound(B::snow_layer) == Sfx::DigSnow);
}
