#pragma once
// Rendimiento automático: decide, una vez por segundo, si hay que bajar o subir la resolución del mundo y la
// distancia de render para mantener los fps que se piden. Sin GL ni SDL: se prueba sola.
#include <algorithm>
#include <cmath>

namespace mcw {

class QualityController {
 public:
  static constexpr int kMinDistance = 4;
  static constexpr float kMinScale = 0.5f;

  /// Lo que ha elegido el jugador (los máximos). Reinicia el control.
  void setUser(int distance, float scale) {
    userDist_ = dist_ = distance;
    userScale_ = scale_ = scale;
    slow_ = fast_ = 0;
    cooldown_ = 5;
  }
  int distance() const { return dist_; }
  float scale() const { return scale_; }
  /// ¿Se ha tocado algo respecto a lo elegido?
  bool reduced() const { return dist_ < userDist_ || scale_ < userScale_ - 0.001f; }

  /// Se llama cada segundo con lo medido en él. `cpuMs`: CPU del hilo principal por frame (sin esperar al swap);
  /// `gpuMs`: tiempo de GPU por frame (0 si no se puede medir). Devuelve true si ha cambiado algo.
  bool update(double fps, double cpuMs, double gpuMs, int targetFps) {
    if (cooldown_ > 0) cooldown_ -= 1.0;
    sinceRaise_ += 1.0;
    if (targetFps <= 0 || fps <= 0) return false;
    const double budget = 1000.0 / targetFps, period = 1000.0 / fps;
    const bool cpuBound = cpuMs > 0.7 * period;
    const bool slow = fps < targetFps * 0.92;
    const bool headroom = fps >= targetFps * 0.97 && std::max(cpuMs, gpuMs) < budget * 0.65;
    slow_ = slow ? slow_ + 1 : 0;
    fast_ = headroom ? fast_ + 1 : 0;
    if (cooldown_ > 0) return false;
    if (slow_ >= 2) {
      const int dist = dist_;
      const float scale = scale_;
      // El coste del terreno crece con el cuadrado de la distancia y el de los píxeles con el de la escala: si va a
      // r veces los fps pedidos, hay que dejar lo que pesa en sqrt(r) de lo que era
      const double k = std::sqrt(std::clamp(fps / targetFps, 0.2, 1.0));
      auto lowerDistance = [&] {
        if (dist_ > kMinDistance) dist_ = std::clamp(static_cast<int>(std::floor(dist_ * k)), kMinDistance, dist_ - 1);
      };
      auto lowerScale = [&](float floorScale) {
        if (scale_ <= floorScale + 0.001f) return;  // (ya está en el suelo de este paso: no se toca, ni se sube)
        const float want = static_cast<float>(std::floor(scale_ * k * 10.0) / 10.0);
        scale_ = std::round(std::max(floorScale, std::min(scale_ - 0.1f, want)) * 10.0f) / 10.0f;
      };
      if (cpuBound) {
        lowerDistance();
        if (dist_ == dist) lowerScale(kMinScale);
      } else {
        lowerScale(0.7f);
        if (scale_ == scale) lowerDistance();
        if (dist_ == dist && scale_ == scale) lowerScale(kMinScale);
      }
      const bool changed = dist_ != dist || scale_ != scale;
      if (changed && sinceRaise_ < 15.0) need_ = std::min(120, need_ * 2);  // subió hace poco y no aguantó
      slow_ = fast_ = 0;
      cooldown_ = fps < targetFps * 0.6 ? 2 : 3;
      return changed;
    }
    if (fast_ >= need_) {
      fast_ = 0;
      if (reduced()) {
        // Primero la resolución (se nota enseguida y es barata de recuperar), luego la distancia
        if (scale_ < userScale_ - 0.001f) scale_ = std::min(userScale_, std::round((scale_ + 0.1f) * 10.0f) / 10.0f);
        else dist_ = std::min(userDist_, dist_ + 1);
        sinceRaise_ = 0;
        cooldown_ = 3;
        return true;
      }
      need_ = std::max(8, need_ / 2);  // un buen rato sin problemas: se vuelve a fiar
    }
    return false;
  }

 private:
  int userDist_ = 12, dist_ = 12;
  float userScale_ = 1.0f, scale_ = 1.0f;
  int slow_ = 0, fast_ = 0, need_ = 8;
  double cooldown_ = 0, sinceRaise_ = 1000;
};

}  // namespace mcw
