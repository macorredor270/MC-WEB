#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "client/quality.h"

using namespace mcw;

namespace {

/// Un equipo de mentira: el coste del terreno crece con el cuadrado de la distancia y el de los píxeles con el
/// cuadrado de la escala. Con `load` se hace más lento (o más rápido) todo.
struct FakeMachine {
  double cpuPerChunk2 = 0.08;  // ms de CPU por chunk^2 de distancia
  double gpuAtFull = 10.0;     // ms de GPU a resolución completa
  double base = 2.0;           // ms fijos
  double load = 1.0;
  double cpu(int dist) const { return (base + cpuPerChunk2 * dist * dist) * load; }
  double gpu(float scale) const { return gpuAtFull * scale * scale * load; }
  double frameMs(int dist, float scale) const { return std::max(cpu(dist), gpu(scale)); }
};

struct Run {
  int changes = 0;
  double lastFps = 0;
};

/// Hace pasar `seconds` segundos de juego con el control y devuelve cuántas veces cambió algo.
Run simulate(QualityController& q, const FakeMachine& m, int target, int seconds, int firstSecond = 0) {
  Run r;
  for (int t = 0; t < seconds; t++) {
    const double ms = m.frameMs(q.distance(), q.scale());
    const double fps = std::min(1000.0 / ms, static_cast<double>(target));  // (la pantalla no pasa de lo pedido)
    r.lastFps = fps;
    if (q.update(fps, m.cpu(q.distance()), m.gpu(q.scale()), target)) r.changes++;
    (void)firstSecond;
  }
  return r;
}

}  // namespace

TEST_CASE("Calidad automática: si no llega a los fps, baja la distancia rápido y se queda ahí") {
  QualityController q;
  q.setUser(16, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.2;  // a distancia 16: 53 ms (19 fps); a 8: 14,8 ms
  const Run r = simulate(q, m, 60, 60);
  CHECK(q.distance() < 16);
  CHECK(q.distance() >= QualityController::kMinDistance);
  CHECK(r.lastFps >= 60 * 0.9);  // al final va bien
  CHECK(q.scale() == doctest::Approx(1.0f));  // (la CPU era el problema: la resolución no se toca)
  // Y no se pone a bajar y subir sin parar
  const Run later = simulate(q, m, 60, 120);
  CHECK(later.changes <= 2);
}

TEST_CASE("Calidad automática: si manda la GPU, baja la resolución antes que la distancia") {
  QualityController q;
  q.setUser(12, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.02;
  m.gpuAtFull = 40.0;  // 25 fps a resolución completa; con 0,7: 19,6 ms (51 fps)
  const Run r = simulate(q, m, 45, 60);
  CHECK(q.scale() < 1.0f);
  CHECK(q.scale() >= QualityController::kMinScale);
  CHECK(q.distance() == 12);
  CHECK(r.lastFps >= 45 * 0.9);
}

TEST_CASE("Calidad automática: sin problemas no toca nada") {
  QualityController q;
  q.setUser(12, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.01;
  m.gpuAtFull = 4.0;
  const Run r = simulate(q, m, 60, 120);
  CHECK(r.changes == 0);
  CHECK(q.distance() == 12);
  CHECK(q.scale() == doctest::Approx(1.0f));
}

TEST_CASE("Calidad automática: recupera lo perdido cuando la carga baja, sin pasarse de lo elegido") {
  QualityController q;
  q.setUser(14, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.15;
  m.load = 2.0;  // un rato de mucha carga (otra pestaña, un incendio...)
  simulate(q, m, 60, 60);
  const int low = q.distance();
  CHECK(low < 14);
  m.load = 1.0;  // la carga pasa
  simulate(q, m, 60, 400);
  CHECK(q.distance() > low);
  CHECK(q.distance() <= 14);
}

TEST_CASE("Calidad automática: nunca baja de los mínimos, aunque no llegue") {
  QualityController q;
  q.setUser(32, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 5.0;  // imposible
  m.gpuAtFull = 500.0;
  simulate(q, m, 60, 200);
  CHECK(q.distance() == QualityController::kMinDistance);
  CHECK(q.scale() == doctest::Approx(QualityController::kMinScale));
}

TEST_CASE("Calidad automática: cambiar los ajustes reinicia el control") {
  QualityController q;
  q.setUser(16, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.3;
  simulate(q, m, 60, 40);
  REQUIRE(q.reduced());
  q.setUser(10, 0.8f);  // el jugador elige otra cosa: manda lo suyo
  CHECK(q.distance() == 10);
  CHECK(q.scale() == doctest::Approx(0.8f));
  CHECK_FALSE(q.reduced());
}

TEST_CASE("Calidad automática: al borde, subir y bajar no oscila sin fin") {
  QualityController q;
  q.setUser(12, 1.0f);
  FakeMachine m;
  m.cpuPerChunk2 = 0.1;  // a 12: 16,4 ms (61 fps); a 13: 18,9 (53 fps): justo en el borde de los 60
  const Run r = simulate(q, m, 60, 600);
  CHECK(r.changes <= 14);
  CHECK(r.lastFps >= 60 * 0.9);
}
