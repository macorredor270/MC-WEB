#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "flat_world.h"
#include "game/minecart.h"
#include "game/rails.h"
#include "game/session.h"

using namespace mcw;

namespace {

using testing::FlatTestWorld;

constexpr int kY = 64;  // el suelo es y=63: los raíles van en y=64

BlockState railState(int id, int shape, bool powered = false) {
  return makeState(id, shape | (powered && rails::straightOnly(id) ? 8 : 0));
}

void put(FlatTestWorld& f, int x, int y, int z, BlockState s) { f.setBlock(x, y, z, s); }

/// Pone un raíl normal y recalcula su forma como al colocarlo.
void lay(FlatTestWorld& f, int x, int y, int z, int id = rails::kRail) {
  put(f, x, y, z, makeState(id, 0));
  rails::connect(f, {x, y, z});
}

int shapeAt(FlatTestWorld& f, int x, int y, int z) {
  const BlockState s = f.w.block(x, y, z);
  return rails::shapeOf(stateId(s), stateMeta(s));
}

}  // namespace

TEST_CASE("Raíles: un raíl solo es norte-sur y dos en línea se alinean") {
  FlatTestWorld f;
  lay(f, 0, kY, 0);
  CHECK(shapeAt(f, 0, kY, 0) == 0);
  lay(f, 1, kY, 0);  // al este del primero
  CHECK(shapeAt(f, 0, kY, 0) == 1);
  CHECK(shapeAt(f, 1, kY, 0) == 1);
  lay(f, -1, kY, 0);
  CHECK(shapeAt(f, -1, kY, 0) == 1);
  CHECK(shapeAt(f, 0, kY, 0) == 1);
}

TEST_CASE("Raíles: las esquinas hacen curva y la forma sale de los lados unidos") {
  FlatTestWorld f;
  lay(f, 0, kY, 0);
  lay(f, 1, kY, 0);  // este
  lay(f, 0, kY, 1);  // sur
  CHECK(shapeAt(f, 0, kY, 0) == 6);  // sur-este
  CHECK(shapeAt(f, 1, kY, 0) == 1);
  CHECK(shapeAt(f, 0, kY, 1) == 0);
  // Un recinto cerrado de 2x2 queda con las cuatro curvas
  FlatTestWorld g;
  lay(g, 0, kY, 0);
  lay(g, 1, kY, 0);
  lay(g, 0, kY, 1);
  lay(g, 1, kY, 1);
  CHECK(shapeAt(g, 0, kY, 0) == 6);  // sur-este
  CHECK(shapeAt(g, 1, kY, 0) == 7);  // sur-oeste
  CHECK(shapeAt(g, 0, kY, 1) == 9);  // norte-este
  CHECK(shapeAt(g, 1, kY, 1) == 8);  // norte-oeste
}

TEST_CASE("Raíles: suben cuestas si hay un raíl un bloque más arriba") {
  FlatTestWorld f;
  put(f, 1, kY, 0, makeState(B::stone));  // el escalón
  lay(f, 0, kY, 0);
  lay(f, 1, kY + 1, 0);  // sobre el escalón, al este
  CHECK(shapeAt(f, 0, kY, 0) == 2);      // sube al este
  CHECK(shapeAt(f, 1, kY + 1, 0) == 1);
  // Hacia el norte
  FlatTestWorld g;
  put(g, 0, kY, -1, makeState(B::stone));
  lay(g, 0, kY, 0);
  lay(g, 0, kY + 1, -1);
  CHECK(shapeAt(g, 0, kY, 0) == 4);  // sube al norte
}

TEST_CASE("Raíles: los especiales solo van rectos y el normal se une a ellos") {
  FlatTestWorld f;
  lay(f, 0, kY, 0, rails::kPowered);
  lay(f, 1, kY, 0);
  lay(f, 0, kY, 1);
  // Un propulsor con vecinos en dos ejes no hace curva: sigue recto
  const int s = shapeAt(f, 0, kY, 0);
  CHECK((s == 0 || s == 1));
  // El normal de al lado se une
  CHECK(rails::isRail(stateId(f.w.block(1, kY, 0))));
}

TEST_CASE("Raíles: la forma de los especiales conserva el bit de activado") {
  CHECK(rails::shapeOf(rails::kPowered, 8 | 1) == 1);
  CHECK(rails::active(rails::kPowered, 8 | 1));
  CHECK_FALSE(rails::active(rails::kPowered, 1));
  CHECK(rails::withShape(rails::kPowered, 8 | 1, 0) == 8);
  // El normal no tiene "activado" y usa los cuatro bits para la forma
  CHECK(rails::shapeOf(rails::kRail, 9) == 9);
  CHECK_FALSE(rails::active(rails::kRail, 9));
}

TEST_CASE("Raíles: pointOnRail da el punto de la vía y su altura (más alta en el extremo alto de la cuesta)") {
  const glm::ivec3 p{0, kY, 0};
  // Recto este-oeste: x libre, z en el centro de la celda
  rails::OnRail r = rails::pointOnRail(p, 1, 0.9, 0.2);
  CHECK(r.x == doctest::Approx(0.9));
  CHECK(r.z == doctest::Approx(0.5));
  CHECK(r.y == doctest::Approx(kY + 0.0625));
  // Cuesta que sube al este: la altura crece con x
  const rails::OnRail lo = rails::pointOnRail(p, 2, 0.0, 0.5), hi = rails::pointOnRail(p, 2, 1.0, 0.5);
  CHECK(hi.y - lo.y == doctest::Approx(1.0));
  // Curva sur-este: se queda sobre el arco (entre los dos lados que une)
  const rails::OnRail c = rails::pointOnRail(p, 6, 0.9, 0.9);
  CHECK(c.x >= 0.5 - 1e-9);
  CHECK(c.z >= 0.5 - 1e-9);
}

TEST_CASE("Vagoneta: en un propulsor con potencia acelera y sin potencia se frena") {
  FlatTestWorld f;
  for (int x = 0; x < 20; x++) put(f, x, kY, 0, railState(rails::kPowered, 1, true));
  Minecart c;
  c.pos = {1.5, kY + 0.0625, 0.5};
  c.motion = {0.05, 0, 0};
  const double v0 = c.motion.x;
  for (int i = 0; i < 5; i++) stepCart(c, f.w, {});
  CHECK(c.motion.x > v0);
  CHECK(c.pos.x > 1.5);
  CHECK(c.pos.z == doctest::Approx(0.5));
  // Sin potencia: frena en pocos ticks
  FlatTestWorld g;
  for (int x = 0; x < 20; x++) put(g, x, kY, 0, railState(rails::kPowered, 1, false));
  Minecart d;
  d.pos = {1.5, kY + 0.0625, 0.5};
  d.motion = {0.2, 0, 0};
  for (int i = 0; i < 12; i++) stepCart(d, g.w, {});
  CHECK(std::abs(d.motion.x) < 0.01);
}

TEST_CASE("Vagoneta: nunca pasa de 0,4 bloques por tick sobre la vía") {
  FlatTestWorld f;
  for (int x = 0; x < 60; x++) put(f, x, kY, 0, railState(rails::kPowered, 1, true));
  Minecart c;
  c.pos = {1.5, kY + 0.0625, 0.5};
  c.motion = {0.3, 0, 0};
  for (int i = 0; i < 40; i++) {
    const double before = c.pos.x;
    stepCart(c, f.w, {});
    CHECK(c.pos.x - before <= 0.4 + 1e-9);
  }
  CHECK(c.pos.x > 10);
}

TEST_CASE("Vagoneta: una cuesta la acelera hacia abajo y la frena hacia arriba") {
  // Cuesta que sube al este: una vagoneta soltada en ella baja hacia el oeste
  FlatTestWorld f;
  put(f, 5, kY, 0, makeState(B::stone));
  for (int x = 0; x < 5; x++) put(f, x, kY, 0, railState(rails::kRail, 1));
  put(f, 4, kY, 0, railState(rails::kRail, 2));  // sube al este
  put(f, 5, kY + 1, 0, railState(rails::kRail, 1));
  Minecart c;
  c.pos = {4.5, kY + 0.5, 0.5};
  for (int i = 0; i < 6; i++) stepCart(c, f.w, {});
  CHECK(c.motion.x < 0);  // baja hacia el oeste
  CHECK(c.pos.x < 4.5);
  // Una vagoneta con velocidad hacia arriba va perdiéndola
  Minecart d;
  d.pos = {4.2, kY + 0.3, 0.5};
  d.motion = {0.1, 0, 0};
  double last = 0.1;
  bool slower = false;
  for (int i = 0; i < 4; i++) {
    stepCart(d, f.w, {});
    if (d.motion.x < last - 1e-9) slower = true;
    last = d.motion.x;
  }
  CHECK(slower);
}

TEST_CASE("Vagoneta: choca con un bloque al final de la vía y se queda parada") {
  FlatTestWorld f;
  for (int x = 0; x < 6; x++) put(f, x, kY, 0, railState(rails::kRail, 1));
  put(f, 6, kY, 0, makeState(B::stone));
  Minecart c;
  c.pos = {1.5, kY + 0.0625, 0.5};
  c.motion = {0.3, 0, 0};
  for (int i = 0; i < 40; i++) stepCart(c, f.w, {});
  CHECK(c.pos.x < 6.0);
  CHECK(std::abs(c.motion.x) < 0.02);
}

TEST_CASE("Vagoneta: sigue la curva de una esquina") {
  FlatTestWorld f;
  // De oeste a este por z=0 hasta x=3 y luego hacia el sur por x=3
  for (int x = 0; x < 3; x++) put(f, x, kY, 0, railState(rails::kRail, 1));
  put(f, 3, kY, 0, railState(rails::kRail, 7));  // sur-oeste
  for (int z = 1; z < 8; z++) put(f, 3, kY, z, railState(rails::kRail, 0));
  Minecart c;
  c.pos = {0.5, kY + 0.0625, 0.5};
  c.motion = {0.3, 0, 0};
  for (int i = 0; i < 40; i++) stepCart(c, f.w, {});
  CHECK(c.pos.x == doctest::Approx(3.5).epsilon(0.05));
  CHECK(c.pos.z > 2.0);
}

TEST_CASE("Vagoneta: sin raíl cae y rueda con rozamiento") {
  FlatTestWorld f;
  Minecart c;
  c.pos = {0.5, kY + 5.0, 0.5};
  for (int i = 0; i < 40; i++) stepCart(c, f.w, {});
  CHECK(c.onGround);
  CHECK(c.pos.y == doctest::Approx(kY));
  c.motion = {0.3, 0, 0};
  for (int i = 0; i < 40; i++) stepCart(c, f.w, {});
  CHECK(std::abs(c.motion.x) < 0.01);
}

TEST_CASE("Vagoneta: quien va montado la arranca empujando hacia delante") {
  FlatTestWorld f;
  for (int x = 0; x < 20; x++) put(f, x, kY, 0, railState(rails::kRail, 1));
  Minecart c;
  c.pos = {10.5, kY + 0.0625, 0.5};
  CartDriver d;
  d.hasRider = true;
  d.forward = 1;
  d.yaw = -1.5707963f;  // mira al este (+x): -sin(yaw) = 1
  for (int i = 0; i < 4; i++) stepCart(c, f.w, d);
  CHECK(c.motion.x > 0);
  CHECK(c.pos.x > 10.5);
}

TEST_CASE("Vagoneta: los objetos de cada tipo ida y vuelta") {
  for (CartType t : {CartType::Normal, CartType::Chest, CartType::Furnace, CartType::Tnt})
    CHECK(cartTypeFromItem(cartItemId(t)) == static_cast<int>(t));
  CHECK(cartTypeFromItem(ItemId::stick) == -1);
}

TEST_CASE("Raíles: un propulsor con potencia enciende hasta 8 más en fila") {
  FlatTestWorld f;
  for (int x = 0; x < 14; x++) put(f, x, kY, 0, railState(rails::kPowered, 1, true));
  const rails::PowerFn direct = [](const glm::ivec3& p) { return p.x == 0; };
  for (int x = 1; x <= 8; x++) CHECK_MESSAGE(rails::poweredByChain(f.w, {x, kY, 0}, direct), "x = ", x);
  CHECK_FALSE(rails::poweredByChain(f.w, {9, kY, 0}, direct));
  CHECK_FALSE(rails::poweredByChain(f.w, {13, kY, 0}, direct));
  // Si uno de en medio está apagado, la cadena se corta
  put(f, 4, kY, 0, railState(rails::kPowered, 1, false));
  CHECK(rails::poweredByChain(f.w, {3, kY, 0}, direct));
  CHECK_FALSE(rails::poweredByChain(f.w, {5, kY, 0}, direct));
  // Un raíl activador no se pasa potencia con uno propulsor
  FlatTestWorld g;
  put(g, 0, kY, 0, railState(rails::kPowered, 1, true));
  put(g, 1, kY, 0, railState(rails::kActivator, 1, true));
  CHECK_FALSE(rails::poweredByChain(g.w, {1, kY, 0}, direct));
  // Ni uno cruzado (norte-sur junto a este-oeste)
  put(g, 1, kY, 0, railState(rails::kPowered, 0, true));
  CHECK_FALSE(rails::poweredByChain(g.w, {1, kY, 0}, direct));
}

TEST_CASE("Vagoneta: en la cuesta baja un nivel al salir por abajo y no choca con el bloque de al lado") {
  // Cuesta que sube al este con un bloque en la cima: la vagoneta empujada hacia arriba llega a la cima sin quedarse
  // atascada contra el bloque de al lado
  FlatTestWorld f;
  put(f, 5, kY, 0, makeState(B::stone));
  for (int x = 0; x < 4; x++) put(f, x, kY, 0, railState(rails::kPowered, 1, true));
  put(f, 4, kY, 0, railState(rails::kRail, 2));       // sube al este
  put(f, 5, kY + 1, 0, railState(rails::kRail, 1));   // la cima
  put(f, 6, kY + 1, 0, railState(rails::kRail, 1));
  put(f, 7, kY + 1, 0, railState(rails::kRail, 1));
  Minecart c;
  c.pos = {3.5, kY + 0.0625, 0.5};
  c.motion = {0.4, 0, 0};
  for (int i = 0; i < 8; i++) stepCart(c, f.w, {});
  CHECK(c.pos.x > 5.0);                      // ha subido a la cima
  CHECK(c.pos.y > kY + 1.0);                 // y está a su altura
}

TEST_CASE("Vagoneta: la inclinación sale de la cuesta y el sentido") {
  FlatTestWorld f;
  put(f, 4, kY, 0, railState(rails::kRail, 2));  // sube al este
  put(f, 3, kY, 0, railState(rails::kRail, 1));
  // Yendo hacia el este (yaw -90º: -sin(yaw) = 1) sube; hacia el oeste baja
  CHECK(cartPitchAt(f.w, {4.5, kY + 0.5, 0.5}, -1.5707963f) > 0.5f);
  CHECK(cartPitchAt(f.w, {4.5, kY + 0.5, 0.5}, 1.5707963f) < -0.5f);
  CHECK(cartPitchAt(f.w, {3.5, kY + 0.0625, 0.5}, 0.0f) == 0.0f);
}

TEST_CASE("Vagoneta: al invertirse no da una vuelta entera de yaw") {
  FlatTestWorld f;
  for (int x = 0; x < 30; x++) put(f, x, kY, 0, railState(rails::kRail, 1));
  Minecart c;
  c.pos = {10.5, kY + 0.0625, 0.5};
  c.motion = {0.3, 0, 0};
  stepCart(c, f.w, {});
  const float east = c.yaw;
  c.motion = {-0.3, 0, 0};
  float biggest = 0;
  for (int i = 0; i < 6; i++) {
    stepCart(c, f.w, {});
    float d = std::abs(c.yaw - c.prevYaw);
    if (d > 3.14159f) d = 6.2831853f - d;
    biggest = std::max(biggest, d);
  }
  CHECK(biggest < 1.6f);  // como mucho 90º por tick: es simétrica
  (void)east;
}
