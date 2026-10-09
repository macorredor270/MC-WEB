#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include "client/touch.h"

using namespace mcw;

namespace {

// Pantalla de 320x240 puntos de GUI a escala 2 y densidad 2 (un móvil normal): los botones miden ~52 pt
constexpr int kW = 320, kH = 240;

TouchControls makeControls(bool floating = false) {
  TouchControls t;
  t.setActive(true);
  t.setScreen(kW, kH, 2, 2.0f);
  t.setOptions(1.0f, 0.7f, floating);
  return t;
}

/// Evento de dedo en coordenadas de GUI (el juego lo recibe normalizado 0..1).
SDL_Event finger(Uint32 type, SDL_FingerID id, float guiX, float guiY) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.fingerID = id;
  e.tfinger.x = guiX / kW;
  e.tfinger.y = guiY / kH;
  return e;
}

void down(TouchControls& t, SDL_FingerID id, float x, float y) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_DOWN, id, x, y))); }
void move(TouchControls& t, SDL_FingerID id, float x, float y) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_MOTION, id, x, y))); }
void up(TouchControls& t, SDL_FingerID id, float x, float y) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_UP, id, x, y))); }

}  // namespace

TEST_CASE("Táctil: arrastrar sobre el mundo gira la cámara sin perder el recorrido inicial") {
  TouchControls t = makeControls();
  down(t, 1, 200, 100);
  // Un temblor pequeño no cuenta como mirar
  move(t, 1, 203, 100);
  CHECK(t.takeLook().x == doctest::Approx(0.0f));
  // Al pasar el umbral se entrega TODO lo recorrido desde que se puso el dedo, no solo el último paso
  move(t, 1, 215, 100);
  const glm::vec2 first = t.takeLook();
  CHECK(first.x == doctest::Approx(15.0f));
  move(t, 1, 225, 110);
  const glm::vec2 second = t.takeLook();
  CHECK(second.x == doctest::Approx(10.0f));
  CHECK(second.y == doctest::Approx(10.0f));
  up(t, 1, 225, 110);
  // Un arrastre no es un toque: no se usa ni se coloca nada
  CHECK_FALSE(t.consume().usePressed);
}

TEST_CASE("Táctil: el giro se lee en cada frame, aunque el tick no haya pasado") {
  TouchControls t = makeControls();
  down(t, 1, 200, 100);
  move(t, 1, 230, 100);
  // Lo que pasa en un tick (consume) no se come el giro
  (void)t.consume();
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
  CHECK(t.takeLook().x == doctest::Approx(0.0f));  // y se entrega una sola vez
  move(t, 1, 240, 100);
  CHECK(t.takeLook().x == doctest::Approx(10.0f));
}

TEST_CASE("Táctil: un toque corto sobre el mundo usa o coloca donde se tocó") {
  TouchControls t = makeControls();
  down(t, 1, 200, 100);
  up(t, 1, 200, 100);
  const TouchInput in = t.consume();
  CHECK(in.usePressed);
  REQUIRE(in.aim.has_value());
  CHECK(in.aim->x == doctest::Approx(200.0f));
  CHECK(in.aim->y == doctest::Approx(100.0f));
  CHECK_FALSE(t.consume().usePressed);  // solo una vez
}

TEST_CASE("Táctil: el joystick mueve con zona muerta y corre al llegar al borde") {
  TouchControls t = makeControls();
  t.newFrame();
  const glm::vec2 c = t.stickCenter();
  const float r = t.stickRadius();
  down(t, 1, c.x, c.y);
  TouchInput in = t.consume();
  CHECK(in.forward == doctest::Approx(0.0f));  // en el centro: quieto
  move(t, 1, c.x, c.y - 0.05f * r);            // un pellizco: dentro de la zona muerta
  in = t.consume();
  CHECK(in.forward == doctest::Approx(0.0f));
  move(t, 1, c.x, c.y - 0.5f * r);             // a medias hacia delante
  in = t.consume();
  CHECK(in.forward > 0.2f);
  CHECK(in.forward < 0.9f);
  CHECK_FALSE(in.sprint);
  move(t, 1, c.x, c.y - 1.5f * r);             // pasado el aro: todo a tope y correr
  in = t.consume();
  CHECK(in.forward == doctest::Approx(1.0f));
  CHECK(in.sprint);
  move(t, 1, c.x + 0.6f * r, c.y);             // a la derecha: avance lateral, sin correr
  in = t.consume();
  CHECK(in.strafe > 0.3f);
  CHECK_FALSE(in.sprint);
  up(t, 1, c.x + 0.6f * r, c.y);
  in = t.consume();
  CHECK(in.forward == doctest::Approx(0.0f));
  CHECK(in.strafe == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: el joystick flotante nace donde se pone el pulgar") {
  TouchControls t = makeControls(true);
  const float r = t.stickRadius();
  down(t, 1, 90, 150);  // dentro de la zona flotante (abajo a la izquierda), lejos del centro "fijo"
  (void)t.consume();
  move(t, 1, 90, 150 - 0.5f * r);
  const TouchInput in = t.consume();
  CHECK(in.forward > 0.2f);  // el centro es donde se puso el dedo
  CHECK(in.strafe == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: saltar es un botón, y a la vez se puede mirar con otro dedo") {
  TouchControls t = makeControls();
  const glm::vec2 j = t.jumpCenter();
  down(t, 1, j.x, j.y);
  down(t, 2, 200, 60);
  move(t, 2, 230, 60);
  const TouchInput in = t.consume();
  CHECK(in.jump);
  CHECK(in.jumpPressed);
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
  up(t, 1, j.x, j.y);
  CHECK_FALSE(t.consume().jump);
}
