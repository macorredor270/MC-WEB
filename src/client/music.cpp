#include "client/music.h"

#include <algorithm>
#include <cmath>

#include "client/audio.h"

namespace mcw {
namespace {

constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};

}  // namespace

void Music::startPiece() {
  // Tonalidad, modo, tempo lento y una progresión de cuatro acordes
  key_ = rng_.nextInt(12) - 5;
  minor_ = rng_.nextInt(2) == 0;
  beatLen_ = 0.7 + rng_.nextFloat() * 0.35;
  static const int progressions[][4] = {{0, 5, 3, 4}, {0, 3, 5, 4}, {0, 4, 5, 3}, {5, 3, 0, 4}, {0, 2, 3, 0}, {0, 5, 1, 4}};
  const auto& p = progressions[rng_.nextInt(6)];
  std::copy(std::begin(p), std::end(p), chords_);
  beatsLeft_ = (16 + rng_.nextInt(17)) * 4;  // 16..32 compases de 4 tiempos
  beat_ = 0;
  melody_ = 7 + rng_.nextInt(3);
  beatTimer_ = 0;
}

void Music::playBeat(Audio& audio) {
  const int* scale = minor_ ? kMinor : kMajor;
  auto degree = [&](int d) {  // grado de la escala (puede pasar de octava) -> semitonos
    const int oct = static_cast<int>(std::floor(d / 7.0));
    return key_ + scale[((d % 7) + 7) % 7] + oct * 12;
  };
  auto note = [&](int semis, float vol) { audio.playFlat(Sfx::Note, vol, std::pow(2.0f, semis / 12.0f)); };
  const int bar = beat_ / 4, inBar = beat_ % 4;
  const int root = chords_[bar % 4];
  // Las últimas notas se apagan poco a poco
  const float fade = std::min(1.0f, beatsLeft_ / 12.0f) * std::min(1.0f, (beat_ + 4) / 12.0f);
  if (inBar == 0) {
    note(degree(root) - 12, 0.35f * fade);                 // bajo
    note(degree(root + 2), 0.18f * fade);                  // tercera del acorde
  }
  if (inBar == 2 && rng_.nextInt(3) > 0) note(degree(root + 4), 0.16f * fade);  // quinta
  // Melodía: paseo por la escala con saltos cortos, a veces descansa
  if (rng_.nextInt(10) < 6) {
    const int step = rng_.nextInt(5) - 2;
    melody_ = std::clamp(melody_ + step, 5, 14);
    // En el primer tiempo, mejor una nota del acorde
    if (inBar == 0) melody_ = root + 7 + (rng_.nextInt(2) ? 2 : 0);
    note(degree(melody_), (0.22f + rng_.nextFloat() * 0.08f) * fade);
  }
  beat_++;
  beatsLeft_--;
}

void Music::update(double dt, Audio& audio, bool enabled) {
  if (!enabled) {
    beatsLeft_ = 0;
    wait_ = std::max(wait_, 5.0);
    return;
  }
  if (beatsLeft_ <= 0) {
    wait_ -= dt;
    if (wait_ <= 0) {
      startPiece();
      wait_ = 60.0 + rng_.nextFloat() * 120.0;  // silencio de 1 a 3 minutos tras la pieza
    }
    return;
  }
  beatTimer_ -= dt;
  while (beatTimer_ <= 0 && beatsLeft_ > 0) {
    // Un poco de rubato: los tiempos no son exactos
    beatTimer_ += beatLen_ * (0.95 + rng_.nextFloat() * 0.1);
    playBeat(audio);
  }
}

}  // namespace mcw
