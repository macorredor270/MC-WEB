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

TEST_CASE("Ajustes táctiles: ida y vuelta, y el joystick fijo (que no se mueve) vuelve a ser el de serie en archivos antiguos") {
  CHECK_FALSE(Settings{}.floatingJoystick);  // de serie, fijo
  Settings a;
  a.floatingJoystick = true;
  a.touchDeadzone = 0.25f;
  a.touchCurve = 0.9f;
  a.touchActionButtons = false;
  a.touchLeftHanded = true;
  a.touchHaptics = false;
  a.touchScheme = 1;
  Settings b;
  b.parse(a.serialize());
  CHECK(b.floatingJoystick);  // lo guardado con esta versión se respeta
  CHECK(b.touchDeadzone == doctest::Approx(0.25f));
  CHECK(b.touchCurve == doctest::Approx(0.9f));
  CHECK_FALSE(b.touchActionButtons);
  CHECK(b.touchLeftHanded);
  CHECK_FALSE(b.touchHaptics);
  CHECK(b.touchScheme == 1);

  Settings old;  // un archivo anterior no trae touchVersion: el joystick flotante de entonces era el de serie, no una elección
  old.parse("floatingJoystick:true\ntouchOpacity:0.5\n");
  CHECK_FALSE(old.floatingJoystick);
  CHECK(old.touchOpacity == doctest::Approx(0.5f));
  Settings v2;  // y con la versión 2 (cuando el flotante era el de serie) pasa lo mismo
  v2.parse("floatingJoystick:true\ntouchVersion:2\n");
  CHECK_FALSE(v2.floatingJoystick);

  Settings wild;
  wild.parse("touchDeadzone:7\ntouchCurve:-2\ntouchScheme:9\ntouchVersion:3\n");
  CHECK(wild.touchDeadzone == doctest::Approx(0.4f));
  CHECK(wild.touchCurve == doctest::Approx(0.0f));
  CHECK(wild.touchScheme == 1);
}
