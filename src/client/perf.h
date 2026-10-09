#pragma once
// Medición del rendimiento: tiempos de CPU por fase del frame y de GPU (donde el sistema lo permita).
// Los usan F3, `--log-perf` y `--bench`.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "client/gl.h"
#include "core/types.h"

namespace mcw {

/// Fases de un frame que se miden (CPU, en ms).
enum class Phase : u8 { Net, Load, Tick, Prep, Sky, Terrain, Entities, Translucent, Hand, Ui, Present, Count };

inline const char* phaseName(Phase p) {
  static constexpr const char* kNames[] = {"red", "carga", "tick", "prep", "cielo", "terreno", "entid", "transl", "mano", "ui", "swap"};
  return kNames[static_cast<std::size_t>(p)];
}

/// Tiempo de CPU de cada fase de un frame. `lap` atribuye lo que ha pasado desde la marca anterior a una fase;
/// la presentación (el swap) se mide fuera del juego y se añade con `addPresent`.
class FramePerf {
 public:
  static constexpr std::size_t kPhases = static_cast<std::size_t>(Phase::Count);
  struct Summary {
    double avg[kPhases] = {}, max[kPhases] = {};
  };

  void startFrame() {
    last_ = SDL_GetTicksNS();
    frame_.fill(0.0);
  }
  void lap(Phase p) {
    const u64 t = SDL_GetTicksNS();
    frame_[static_cast<std::size_t>(p)] += static_cast<double>(t - last_) / 1e6;
    last_ = t;
  }
  /// Cierra el frame: lo suma a la ventana actual (y a la medición de `--bench`, si está en marcha).
  void endFrame(double periodMs) {
    frames_++;
    for (std::size_t i = 0; i < kPhases; i++) {
      sum_[i] += frame_[i];
      max_[i] = std::max(max_[i], frame_[i]);
    }
    if (recording_) {
      periods_.push_back(static_cast<float>(periodMs));
      for (std::size_t i = 0; i < kPhases; i++) benchSum_[i] += frame_[i];
    }
  }
  void addPresent(double ms) {
    constexpr std::size_t i = static_cast<std::size_t>(Phase::Present);
    sum_[i] += ms;
    max_[i] = std::max(max_[i], ms);
    if (recording_) benchSum_[i] += ms;
  }
  /// Cierra la ventana (cada segundo): deja los promedios y los peores tiempos a mano.
  void roll() {
    for (std::size_t i = 0; i < kPhases; i++) {
      last_avg_[i] = frames_ > 0 ? sum_[i] / frames_ : 0.0;
      last_max_[i] = max_[i];
      sum_[i] = max_[i] = 0.0;
    }
    frames_ = 0;
  }
  double avg(Phase p) const { return last_avg_[static_cast<std::size_t>(p)]; }
  double max(Phase p) const { return last_max_[static_cast<std::size_t>(p)]; }
  /// Suma de las fases de CPU del último segundo (sin el swap).
  double cpuAvg() const {
    double s = 0;
    for (std::size_t i = 0; i < kPhases - 1; i++) s += last_avg_[i];
    return s;
  }

  // --- Medición larga (`--bench`) ---
  void startRecording() {
    recording_ = true;
    periods_.clear();
    benchSum_.fill(0.0);
  }
  void stopRecording() { recording_ = false; }
  bool recording() const { return recording_; }
  std::size_t recordedFrames() const { return periods_.size(); }
  /// Percentil (0 a 1) del tiempo entre frames, en ms.
  double percentile(double q) const {
    if (periods_.empty()) return 0.0;
    std::vector<float> v = periods_;
    const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
  }
  double periodAvg() const {
    if (periods_.empty()) return 0.0;
    double s = 0;
    for (const float p : periods_) s += p;
    return s / static_cast<double>(periods_.size());
  }
  double periodMax() const { return periods_.empty() ? 0.0 : *std::max_element(periods_.begin(), periods_.end()); }
  double benchAvg(Phase p) const { return periods_.empty() ? 0.0 : benchSum_[static_cast<std::size_t>(p)] / static_cast<double>(periods_.size()); }

 private:
  u64 last_ = 0;
  std::array<double, kPhases> frame_{}, sum_{}, max_{}, benchSum_{}, last_avg_{}, last_max_{};
  int frames_ = 0;
  bool recording_ = false;
  std::vector<float> periods_;
};

/// Tiempo de GPU del trabajo de un frame, con consultas de tiempo (OpenGL 3.3, y en el navegador la
/// extensión EXT_disjoint_timer_query_webgl2, que no tienen todos los móviles). Los resultados llegan con
/// unos frames de retraso: no frena a la GPU. Sin soporte, `supported()` es false y `ms()` es 0.
class GpuTimer {
 public:
  void init();
  bool supported() const { return supported_; }
  /// Antes de dibujar el mundo y la interfaz de este frame, y al terminar.
  void begin();
  void end();
  /// Media móvil del tiempo de GPU de un frame, en ms.
  double ms() const { return ms_; }

 private:
  static constexpr int kQueries = 4;
  GLuint queries_[kQueries] = {};
  bool inFlight_[kQueries] = {};
  int next_ = 0;
  bool supported_ = false, open_ = false, haveValue_ = false;
  double ms_ = 0;
};

}  // namespace mcw
