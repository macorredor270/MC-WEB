#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include "client/touch.h"

using namespace mcw;

namespace {

// Un móvil apaisado normal: 2400x1080 píxeles a 3 por punto = 800x360 puntos; la GUI va a escala 4, así que son
// 600x270 píxeles de GUI y los botones miden 52 puntos = 39 píxeles de GUI
constexpr int kW = 600, kH = 270;
constexpr u64 kT0 = 10'000'000'000ull;  // hora de las pruebas (en ns): no dependen del reloj de verdad
constexpr u64 kMs = 1'000'000ull;

TouchControls makeControls(TouchOptions o = {}) {
  TouchControls t;
  t.setActive(true);
  t.setScreen(kW, kH, 4, 3.0f);
  t.setOptions(o);
  return t;
}

/// Evento de dedo en coordenadas de GUI (el juego lo recibe normalizado 0..1) a una hora dada.
SDL_Event finger(Uint32 type, SDL_FingerID id, float guiX, float guiY, u64 at) {
  SDL_Event e{};
  e.type = type;
  e.common.timestamp = at;
  e.tfinger.fingerID = id;
  e.tfinger.x = guiX / kW;
  e.tfinger.y = guiY / kH;
  return e;
}

void down(TouchControls& t, SDL_FingerID id, float x, float y, u64 at = kT0) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_DOWN, id, x, y, at))); }
void move(TouchControls& t, SDL_FingerID id, float x, float y, u64 at = kT0) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_MOTION, id, x, y, at))); }
void up(TouchControls& t, SDL_FingerID id, float x, float y, u64 at = kT0) { REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_UP, id, x, y, at))); }
void frames(TouchControls& t, int n) {
  for (int i = 0; i < n; i++) t.newFrame();
}
void tap(TouchControls& t, glm::vec2 p, u64 at = kT0, u64 length = 60 * kMs) {
  down(t, 99, p.x, p.y, at);
  up(t, 99, p.x, p.y, at + length);
}

}  // namespace

TEST_CASE("Táctil: arrastrar sobre el mundo gira la cámara sin perder el recorrido inicial") {
  TouchControls t = makeControls();
  down(t, 1, 300, 100);
  // Un temblor pequeño no cuenta como mirar
  move(t, 1, 302, 100);
  CHECK(t.takeLook().x == doctest::Approx(0.0f));
  // Al pasar el umbral se entrega TODO lo recorrido desde que se puso el dedo, no solo el último paso
  move(t, 1, 315, 100);
  const glm::vec2 first = t.takeLook();
  CHECK(first.x == doctest::Approx(15.0f));
  move(t, 1, 325, 110);
  const glm::vec2 second = t.takeLook();
  CHECK(second.x == doctest::Approx(10.0f));
  CHECK(second.y == doctest::Approx(10.0f));
  up(t, 1, 325, 110);
  // Un arrastre no es un toque: no se usa ni se coloca nada
  CHECK_FALSE(t.consume(kT0 + 50 * kMs).tap);
}

TEST_CASE("Táctil: el giro se lee en cada frame, aunque el tick no haya pasado") {
  TouchControls t = makeControls();
  down(t, 1, 300, 100);
  move(t, 1, 330, 100);
  // Lo que pasa en un tick (consume) no se come el giro
  (void)t.consume(kT0 + 50 * kMs);
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
  CHECK(t.takeLook().x == doctest::Approx(0.0f));  // y se entrega una sola vez
  move(t, 1, 340, 100);
  CHECK(t.takeLook().x == doctest::Approx(10.0f));
}

TEST_CASE("Táctil: un toque corto sobre el mundo usa o coloca; con la mira central no manda punto") {
  TouchControls t = makeControls();
  tap(t, {300, 100});
  TouchInput in = t.consume(kT0 + 100 * kMs);
  CHECK(in.tap);
  CHECK_FALSE(in.aim.has_value());  // se usa donde apunta la mira, no donde se tocó
  CHECK_FALSE(in.usePressed);       // (eso es del botón Usar)
  CHECK_FALSE(t.consume(kT0 + 150 * kMs).tap);  // solo una vez

  TouchOptions pocket;
  pocket.scheme = TouchScheme::Pocket;
  TouchControls p = makeControls(pocket);
  tap(p, {300, 100});
  in = p.consume(kT0 + 100 * kMs);
  CHECK(in.tap);
  REQUIRE(in.aim.has_value());  // "tocar para apuntar": se usa en el punto tocado
  CHECK(in.aim->x == doctest::Approx(300.0f));
  CHECK(in.aim->y == doctest::Approx(100.0f));
}

TEST_CASE("Táctil: un toque largo sin mover ya no es un toque") {
  TouchControls t = makeControls();
  tap(t, {300, 100}, kT0, 500 * kMs);
  CHECK_FALSE(t.consume(kT0 + 600 * kMs).tap);
}

TEST_CASE("Táctil: mantener quieto rompe, y con la mira central se puede girar sin soltar") {
  TouchControls t = makeControls();
  down(t, 1, 300, 100);
  frames(t, 4);
  CHECK(t.holdProgress(kT0 + 100 * kMs) == doctest::Approx(0.4f));
  TouchInput in = t.consume(kT0 + 100 * kMs);
  CHECK_FALSE(in.attack);  // todavía no
  in = t.consume(kT0 + 300 * kMs);
  CHECK(in.attack);
  CHECK(in.holdWorld);
  CHECK_FALSE(in.aim.has_value());
  CHECK(t.holdProgress(kT0 + 300 * kMs) == doctest::Approx(-1.0f));  // el anillo ya no hace falta
  move(t, 1, 330, 100);  // se mueve el dedo: gira la cámara...
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
  in = t.consume(kT0 + 400 * kMs);
  CHECK(in.attack);  // ...y sigue rompiendo
  up(t, 1, 330, 100, kT0 + 450 * kMs);
  in = t.consume(kT0 + 500 * kMs);
  CHECK_FALSE(in.attack);
  CHECK_FALSE(in.tap);  // soltar después de romper no coloca nada
}

TEST_CASE("Táctil: un dedo que se mueve antes de tiempo gira y no rompe") {
  TouchControls t = makeControls();
  down(t, 1, 300, 100);
  frames(t, 4);
  move(t, 1, 320, 100, kT0 + 100 * kMs);
  const TouchInput in = t.consume(kT0 + 400 * kMs);
  CHECK_FALSE(in.attack);
  CHECK(t.takeLook().x == doctest::Approx(20.0f));
}

TEST_CASE("Táctil: con frames lentos no se rompe hasta haber visto unos cuantos") {
  TouchControls t = makeControls();
  down(t, 1, 300, 100);
  frames(t, 1);
  CHECK_FALSE(t.consume(kT0 + 400 * kMs).attack);  // el reloj dice que sí, pero el movimiento pudo llegar tarde
  frames(t, 3);
  CHECK(t.consume(kT0 + 450 * kMs).attack);
}

TEST_CASE("Táctil: 'tocar para apuntar' rompe donde está el dedo y ahí no se gira") {
  TouchOptions o;
  o.scheme = TouchScheme::Pocket;
  TouchControls t = makeControls(o);
  down(t, 1, 300, 100);
  frames(t, 4);
  TouchInput in = t.consume(kT0 + 300 * kMs);
  CHECK(in.attack);
  REQUIRE(in.aim.has_value());
  CHECK(in.aim->x == doctest::Approx(300.0f));
  move(t, 1, 340, 120);
  CHECK(t.takeLook().x == doctest::Approx(0.0f));  // el dedo apunta, no gira
  in = t.consume(kT0 + 350 * kMs);
  REQUIRE(in.aim.has_value());
  CHECK(in.aim->x == doctest::Approx(340.0f));
  CHECK(in.aim->y == doctest::Approx(120.0f));
}

TEST_CASE("Táctil: el joystick fijo mueve con zona muerta y corre al llegar al borde, y el candado sigue") {
  TouchOptions o;
  o.floatingStick = false;
  TouchControls t = makeControls(o);
  t.newFrame();
  const glm::vec2 c = t.stickCenter();
  const float r = t.stickRadius();
  down(t, 1, c.x, c.y);
  TouchInput in = t.consume(kT0);
  CHECK(in.forward == doctest::Approx(0.0f));  // en el centro: quieto
  CHECK_FALSE(in.sprint);
  move(t, 1, c.x, c.y - 0.05f * r);            // un pellizco: dentro de la zona muerta
  CHECK(t.consume(kT0).forward == doctest::Approx(0.0f));
  move(t, 1, c.x, c.y - 0.5f * r);             // a medias hacia delante
  in = t.consume(kT0);
  CHECK(in.forward > 0.2f);
  CHECK(in.forward < 0.5f);
  CHECK_FALSE(in.sprint);
  move(t, 1, c.x, c.y - 1.5f * r);             // pasado el aro: todo a tope y correr
  in = t.consume(kT0);
  CHECK(in.forward == doctest::Approx(1.0f));
  CHECK(in.sprint);
  CHECK(t.sprintLocked());
  CHECK(in.haptic > 0);                        // un toque de vibración al poner el candado
  move(t, 1, c.x, c.y - 0.6f * r);             // se afloja: sigue corriendo sin tener que apretar a tope
  in = t.consume(kT0);
  CHECK(in.forward > 0.3f);
  CHECK(in.sprint);
  move(t, 1, c.x + 0.6f * r, c.y);             // a la derecha: avance lateral, el candado se quita
  in = t.consume(kT0);
  CHECK(in.strafe > 0.3f);
  CHECK_FALSE(in.sprint);
  CHECK_FALSE(t.sprintLocked());
  up(t, 1, c.x + 0.6f * r, c.y);
  in = t.consume(kT0);
  CHECK(in.forward == doctest::Approx(0.0f));
  CHECK(in.strafe == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: soltar el joystick con el candado puesto deja de correr") {
  TouchOptions o;
  o.floatingStick = false;
  TouchControls t = makeControls(o);
  const glm::vec2 c = t.stickCenter();
  down(t, 1, c.x, c.y);
  move(t, 1, c.x, c.y - 2 * t.stickRadius());
  CHECK(t.consume(kT0).sprint);
  up(t, 1, c.x, c.y - 2 * t.stickRadius());
  CHECK_FALSE(t.consume(kT0).sprint);
}

TEST_CASE("Táctil: la curva da más precisión con poco empuje y la zona muerta es ajustable") {
  TouchOptions linear;
  linear.floatingStick = false;
  linear.deadzone = 0.0f;
  linear.curve = 0.0f;
  TouchOptions soft = linear;
  soft.curve = 1.0f;
  TouchControls a = makeControls(linear), b = makeControls(soft);
  const glm::vec2 c = a.stickCenter();
  const float r = a.stickRadius();
  down(a, 1, c.x, c.y - 0.5f * r);
  down(b, 1, c.x, c.y - 0.5f * r);
  CHECK(a.consume(kT0).forward == doctest::Approx(0.5f));
  const float softer = b.consume(kT0).forward;
  CHECK(softer < 0.35f);
  CHECK(softer > 0.2f);

  TouchOptions wide = linear;
  wide.deadzone = 0.4f;
  TouchControls w = makeControls(wide);
  down(w, 1, c.x, c.y - 0.3f * r);
  CHECK(w.consume(kT0).forward == doctest::Approx(0.0f));  // dentro de la zona muerta ancha
  move(w, 1, c.x, c.y - 0.7f * r);
  CHECK(w.consume(kT0).forward == doctest::Approx(0.5f));  // (0,7 - 0,4) / (1 - 0,4)
}

TEST_CASE("Táctil: el joystick flotante nace donde se pone el pulgar y su base sigue al dedo") {
  TouchControls t = makeControls();
  const float r = t.stickRadius();
  down(t, 1, 100, 200);  // dentro de la zona flotante (abajo a la izquierda), lejos del centro "fijo"
  (void)t.consume(kT0);
  move(t, 1, 100, 200 - 0.5f * r);
  TouchInput in = t.consume(kT0);
  CHECK(in.forward > 0.2f);  // el centro es donde se puso el dedo
  CHECK(in.strafe == doctest::Approx(0.0f));
  move(t, 1, 100, 200 - 2.0f * r);  // el dedo se va lejos: la base lo sigue
  in = t.consume(kT0);
  CHECK(in.forward == doctest::Approx(1.0f));
  move(t, 1, 100, 200 - 1.5f * r);  // volver medio radio atrás ya no es "a tope": la base se quedó donde estaba el dedo
  in = t.consume(kT0);
  CHECK(in.forward > 0.2f);
  CHECK(in.forward < 0.5f);
}

TEST_CASE("Táctil: un segundo dedo sobre el joystick mira en vez de crear otro joystick") {
  TouchControls t = makeControls();
  down(t, 1, 100, 200);
  down(t, 2, 150, 210);
  move(t, 2, 180, 210);
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
}

TEST_CASE("Táctil: saltar es un botón, aunque el toque dure menos que un tick, y a la vez se puede mirar") {
  TouchControls t = makeControls();
  const glm::vec2 j = t.buttonCenter(TouchButton::Jump);
  tap(t, j, kT0, 10 * kMs);  // pulsar y soltar entre dos ticks
  TouchInput in = t.consume(kT0 + 50 * kMs);
  CHECK(in.jump);
  CHECK(in.jumpPressed);
  CHECK(t.consume(kT0 + 100 * kMs).jump == false);

  down(t, 1, j.x, j.y);
  down(t, 2, 300, 60);
  move(t, 2, 330, 60);
  in = t.consume(kT0 + 50 * kMs);
  CHECK(in.jump);
  CHECK(t.takeLook().x == doctest::Approx(30.0f));
  up(t, 1, j.x, j.y);
  CHECK_FALSE(t.consume(kT0 + 100 * kMs).jump);
}

TEST_CASE("Táctil: atacar y usar son botones que se mantienen, y un toque corto también cuenta") {
  TouchControls t = makeControls();
  const glm::vec2 a = t.buttonCenter(TouchButton::Attack), u = t.buttonCenter(TouchButton::Use);
  tap(t, a, kT0, 10 * kMs);  // un toque más corto que un tick: rompe igualmente
  TouchInput in = t.consume(kT0 + 50 * kMs);
  CHECK(in.attack);
  CHECK(in.attackPressed);
  CHECK_FALSE(t.consume(kT0 + 60 * kMs).attack);
  down(t, 1, a.x, a.y);
  in = t.consume(kT0 + 50 * kMs);
  CHECK(in.attack);
  CHECK(in.attackPressed);
  CHECK_FALSE(in.holdWorld);  // (no es el dedo del mundo: con comida o arco en la mano sigue siendo atacar)
  in = t.consume(kT0 + 100 * kMs);
  CHECK(in.attack);
  CHECK_FALSE(in.attackPressed);  // el golpe suelto solo se avisa una vez
  up(t, 1, a.x, a.y);
  CHECK_FALSE(t.consume(kT0 + 150 * kMs).attack);

  tap(t, u, kT0, 10 * kMs);
  in = t.consume(kT0 + 50 * kMs);
  CHECK(in.usePressed);
  CHECK_FALSE(in.tap);  // el botón Usar no golpea criaturas como el toque en el mundo
  down(t, 2, u.x, u.y);
  in = t.consume(kT0 + 100 * kMs);
  CHECK(in.use);
}

TEST_CASE("Táctil: con 'tocar para apuntar' no hay botones de atacar y usar y esa zona es del mundo") {
  TouchOptions o;
  o.scheme = TouchScheme::Pocket;
  TouchControls t = makeControls(o);
  CHECK_FALSE(t.buttonVisible(TouchButton::Attack));
  CHECK_FALSE(t.buttonVisible(TouchButton::Use));
  const glm::vec2 a = t.buttonCenter(TouchButton::Attack);
  tap(t, a);
  const TouchInput in = t.consume(kT0 + 100 * kMs);
  CHECK(in.tap);
  CHECK_FALSE(in.attackPressed);

  TouchOptions off;
  off.actionButtons = false;
  CHECK_FALSE(makeControls(off).buttonVisible(TouchButton::Attack));
}

TEST_CASE("Táctil: agacharse — un toque lo deja puesto, mantener agacha solo mientras se aprieta") {
  TouchControls t = makeControls();
  const glm::vec2 s = t.buttonCenter(TouchButton::Sneak);
  tap(t, s, kT0, 80 * kMs);
  CHECK(t.consume(kT0 + 100 * kMs).sneak);  // toque corto: puesto
  CHECK(t.consume(kT0 + 150 * kMs).sneak);  // y sigue
  tap(t, s, kT0 + 200 * kMs, 80 * kMs);
  CHECK_FALSE(t.consume(kT0 + 300 * kMs).sneak);  // otro toque: quitado

  down(t, 1, s.x, s.y, kT0 + 400 * kMs);
  CHECK(t.consume(kT0 + 450 * kMs).sneak);
  up(t, 1, s.x, s.y, kT0 + 1000 * kMs);  // se mantuvo más de un toque: al soltar se levanta
  CHECK_FALSE(t.consume(kT0 + 1050 * kMs).sneak);

  // Volando, agacharse es bajar mientras se aprieta
  t.setFlying(true);
  down(t, 2, s.x, s.y, kT0 + 1100 * kMs);
  CHECK(t.consume(kT0 + 1150 * kMs).sneak);
  up(t, 2, s.x, s.y, kT0 + 1200 * kMs);
  CHECK_FALSE(t.consume(kT0 + 1250 * kMs).sneak);
}

TEST_CASE("Táctil: los botones de arriba actúan al soltar el dedo encima, no al apoyarlo") {
  const std::array<TouchButton, 4> top = {TouchButton::Pause, TouchButton::Chat, TouchButton::Perspective, TouchButton::Inventory};
  for (const TouchButton b : top) {
    TouchControls t = makeControls();
    const glm::vec2 c = t.buttonCenter(b);
    down(t, 1, c.x, c.y);
    TouchInput in = t.consume(kT0);
    CHECK_FALSE((in.pause || in.chat || in.perspective || in.openInventory));  // aún no
    up(t, 1, c.x, c.y);
    in = t.consume(kT0);
    const bool fired = b == TouchButton::Pause ? in.pause : b == TouchButton::Chat ? in.chat : b == TouchButton::Perspective ? in.perspective : in.openInventory;
    CHECK(fired);
    // Apoyar el dedo, sacarlo fuera y soltar: se cancela
    down(t, 2, c.x, c.y);
    move(t, 2, 300, 150);
    up(t, 2, 300, 150);
    in = t.consume(kT0);
    CHECK_FALSE((in.pause || in.chat || in.perspective || in.openInventory));
  }
}

TEST_CASE("Táctil: soltar — un toque suelta uno y mantener suelta toda la pila") {
  TouchControls t = makeControls();
  const glm::vec2 d = t.buttonCenter(TouchButton::Drop);
  tap(t, d, kT0, 100 * kMs);
  TouchInput in = t.consume(kT0 + 150 * kMs);
  CHECK(in.drop);
  CHECK_FALSE(in.dropStack);
  tap(t, d, kT0 + 200 * kMs, 800 * kMs);
  in = t.consume(kT0 + 1100 * kMs);
  CHECK(in.drop);
  CHECK(in.dropStack);
}

TEST_CASE("Táctil: la barra rápida elige casilla al tocar y al deslizar") {
  TouchControls t = makeControls();
  const float left = 300.0f - 91.0f + 1.0f;  // la barra está centrada: la casilla i empieza en left + 20 i
  const float y = kH - 10.0f;
  down(t, 1, left + 4 * 20 + 10, y);
  CHECK(t.consume(kT0).selectSlot == 4);
  CHECK(t.consume(kT0).selectSlot == -1);  // una vez
  move(t, 1, left + 6 * 20 + 10, y);
  CHECK(t.consume(kT0).selectSlot == 6);
  move(t, 1, left + 6 * 20 + 15, y);       // misma casilla: nada
  CHECK(t.consume(kT0).selectSlot == -1);
  up(t, 1, left + 6 * 20 + 15, y);
  // Un dedo en la barra no gira la cámara
  CHECK(t.takeLook().x == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: zurdo — los botones y el joystick se reflejan") {
  TouchControls right = makeControls();
  TouchOptions o;
  o.leftHanded = true;
  TouchControls left = makeControls(o);
  for (const TouchButton b : {TouchButton::Jump, TouchButton::Sneak, TouchButton::Attack, TouchButton::Use, TouchButton::Pause}) {
    CHECK(left.buttonCenter(b).x == doctest::Approx(kW - right.buttonCenter(b).x));
    CHECK(left.buttonCenter(b).y == doctest::Approx(right.buttonCenter(b).y));
  }
  CHECK(left.stickCenter().x == doctest::Approx(kW - right.stickCenter().x));
  // Un dedo abajo a la derecha (sin botones) es el joystick para un zurdo y la cámara para un diestro
  down(left, 1, 450, 200);
  move(left, 1, 450, 200 - 0.5f * left.stickRadius());
  CHECK(left.consume(kT0).forward > 0.2f);
  down(right, 1, 450, 200);
  move(right, 1, 450, 200 - 0.5f * right.stickRadius());
  CHECK(right.consume(kT0).forward == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: los botones no se pisan entre sí ni tapan el centro de la pantalla") {
  TouchControls t = makeControls();
  const float b = 39.0f;  // tamaño de un botón normal en esta pantalla
  const glm::vec2 centre(kW / 2.0f, kH / 2.0f);
  for (std::size_t i = 0; i < static_cast<std::size_t>(TouchButton::Count); i++) {
    const auto bi = static_cast<TouchButton>(i);
    const glm::vec2 ci = t.buttonCenter(bi);
    CHECK(glm::length(ci - centre) > 2 * b);  // la mira queda libre
    for (std::size_t j = i + 1; j < static_cast<std::size_t>(TouchButton::Count); j++) {
      const glm::vec2 cj = t.buttonCenter(static_cast<TouchButton>(j));
      CHECK(glm::length(ci - cj) > 0.6f * b);
    }
    CHECK(ci.x > 0);
    CHECK(ci.x < kW);
    CHECK(ci.y > 0);
    CHECK(ci.y < kH);
  }
}

TEST_CASE("Táctil: la vibración se puede apagar") {
  TouchControls t = makeControls();
  const glm::vec2 j = t.buttonCenter(TouchButton::Jump);
  down(t, 1, j.x, j.y);
  CHECK(t.consume(kT0).haptic > 0);
  TouchOptions o;
  o.haptics = false;
  TouchControls q = makeControls(o);
  down(q, 1, j.x, j.y);
  CHECK(q.consume(kT0).haptic == 0);
}

TEST_CASE("Táctil: al abrir una pantalla se sueltan todos los dedos") {
  TouchControls t = makeControls();
  down(t, 1, 100, 200);
  move(t, 1, 100, 100);
  CHECK(t.consume(kT0).forward > 0.5f);
  t.releaseAll();  // (los eventos de dedo van ahora al menú: el joystick no se enteraría de que se suelta)
  const TouchInput in = t.consume(kT0);
  CHECK(in.forward == doctest::Approx(0.0f));
  CHECK_FALSE(in.sprint);
  CHECK(t.takeLook().x == doctest::Approx(0.0f));
}

TEST_CASE("Táctil: lo que no es un dedo no lo toca") {
  TouchControls t = makeControls();
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  CHECK_FALSE(t.handleEvent(e));
  // Un dedo cancelado no cuenta como toque
  down(t, 1, 300, 100);
  REQUIRE(t.handleEvent(finger(SDL_EVENT_FINGER_CANCELED, 1, 300, 100, kT0 + 50 * kMs)));
  CHECK_FALSE(t.consume(kT0 + 100 * kMs).tap);
}
