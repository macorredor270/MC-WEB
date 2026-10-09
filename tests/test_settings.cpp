#include <doctest/doctest.h>

#include "client/settings.h"

using namespace mcw;

TEST_CASE("Ajustes: ida y vuelta a texto, teclas cambiadas y valores fuera de rango") {
  Settings a;
  a.renderDistance = 20;
  a.fov = 95;
  a.fancyLeaves = false;
  a.particles = 2;
  a.volMusic = 0.25f;
  a.difficulty = 3;
  a.perspective = 1;
  a.toggleSprint = true;
  a.autoJump = 1;
  a.touchSmoothing = 0.8f;
  a.keys[static_cast<int>(KeyAction::Forward)] = SDL_SCANCODE_UP;
  a.keys[static_cast<int>(KeyAction::Sprint)] = SDL_SCANCODE_UNKNOWN;
  Settings b;
  b.parse(a.serialize());
  CHECK(b.renderDistance == 20);
  CHECK(b.fov == doctest::Approx(95.0f));
  CHECK_FALSE(b.fancyLeaves);
  CHECK(b.particles == 2);
  CHECK(b.volMusic == doctest::Approx(0.25f));
  CHECK(b.difficulty == 3);
  CHECK(b.perspective == 1);
  CHECK(b.toggleSprint);
  CHECK(b.autoJump == 1);
  CHECK(b.touchSmoothing == doctest::Approx(0.8f));
  CHECK(b.keys[static_cast<int>(KeyAction::Forward)] == SDL_SCANCODE_UP);
  CHECK(b.keys[static_cast<int>(KeyAction::Sprint)] == SDL_SCANCODE_UNKNOWN);
  CHECK(b.keys[static_cast<int>(KeyAction::Jump)] == SDL_SCANCODE_SPACE);

  Settings c;
  c.parse("renderDistance:500\nfov:-3\nvolume:abc\ndifficulty:9\nbasura\n");
  CHECK(c.renderDistance == Settings::kMaxRenderDistance);
  CHECK(c.fov == doctest::Approx(30.0f));
  CHECK(c.volume == doctest::Approx(1.0f));
  CHECK(c.difficulty == 3);
}

TEST_CASE("Ajustes: los presets de calidad se reconocen") {
  Settings s;
  CHECK(s.matchingPreset() == 2);  // por defecto: alta
  s.applyPreset(0);
  CHECK(s.matchingPreset() == 0);
  CHECK(s.renderDistance == 6);
  s.applyPreset(3);
  CHECK(s.matchingPreset() == 3);
  s.clouds = false;
  CHECK(s.matchingPreset() == -1);
}
