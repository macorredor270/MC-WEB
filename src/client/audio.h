#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <glm/glm.hpp>
#include <mutex>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Efectos de sonido. Todos se sintetizan al arrancar (no hacen falta los sonidos del juego, que en
/// 1.8 ni siquiera vienen en el jar): ruido filtrado, osciladores y envolventes.
enum class Sfx : u8 {
  DigStone, DigWood, DigGravel, DigGrass, DigSand, DigGlass, DigCloth, DigSnow,
  Pop, Hurt, Explosion, Fuse, Bow, ArrowHit, Eat, Burp, Click, Splash,
  PigSay, PigHurt, CowSay, CowHurt, SheepSay, ChickenSay, ChickenHurt,
  ZombieSay, ZombieHurt, SkeletonSay, SkeletonHurt, SpiderSay, SpiderHurt, CreeperHurt,
  Count
};

/// Mezclador con sonido posicional sencillo (volumen por distancia y panorama izquierda/derecha).
class Audio {
 public:
  ~Audio();
  bool init();
  bool ok() const { return stream_ != nullptr; }

  void setVolume(float v) { volume_ = v; }
  void setListener(const glm::dvec3& pos, float yaw);
  /// Sonido en un punto del mundo (se oye hasta ~16 bloques, más si `volume` > 1).
  void play(Sfx s, const glm::dvec3& pos, float volume = 1.0f, float pitch = 1.0f);
  /// Sonido sin posición (interfaz, el propio jugador).
  void playFlat(Sfx s, float volume = 1.0f, float pitch = 1.0f);

 private:
  struct Voice {
    const std::vector<float>* data = nullptr;
    double pos = 0, step = 1;
    float gainL = 0, gainR = 0;
  };
  static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
  void mix(float* out, int frames);
  void start(Sfx s, float gainL, float gainR, float pitch);

  SDL_AudioStream* stream_ = nullptr;
  int rate_ = 44100;
  std::array<std::vector<std::vector<float>>, static_cast<int>(Sfx::Count)> sounds_;  // variantes de cada efecto
  std::vector<Voice> voices_;
  std::mutex mutex_;
  glm::dvec3 listener_{0};
  float listenerYaw_ = 0, volume_ = 1.0f;
  u32 rng_ = 12345;
  std::vector<float> mixBuf_;
};

/// Sintetiza una variante de un efecto (muestras mono en [-1, 1]).
std::vector<float> synthesize(Sfx s, int variant, int sampleRate);

/// Sonido de romper/pisar según el bloque (piedra, madera, grava, hierba...).
Sfx blockSound(int blockId);

}  // namespace mcw
