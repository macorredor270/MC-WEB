#pragma once
#include "core/random.h"
#include "core/types.h"

namespace mcw {

class Audio;

/// Música de fondo generativa: piezas tranquilas de piano (acordes, bajo y una melodía que pasea
/// por la escala) con silencios largos entre una y otra, como la música del juego. Todo se compone
/// al vuelo con la nota sintetizada (Sfx::Note), así que no hace falta ningún archivo.
class Music {
 public:
  explicit Music(u64 seed) : rng_(seed) {}
  /// Llamar cada frame con el tiempo transcurrido (s). `enabled` = volumen de música > 0.
  void update(double dt, Audio& audio, bool enabled);
  bool playing() const { return beatsLeft_ > 0; }

 private:
  void startPiece();
  void playBeat(Audio& audio);

  Random rng_;
  double wait_ = 25.0;   // silencio antes de la primera pieza
  double beatTimer_ = 0, beatLen_ = 0.8;
  int beatsLeft_ = 0, beat_ = 0;
  int key_ = 0;          // semitonos sobre Do
  bool minor_ = false;
  int chords_[4] = {0, 5, 3, 4};  // grados de la progresión
  int melody_ = 7;       // posición de la melodía en la escala
};

}  // namespace mcw
