#pragma once
// Pesca: el anzuelo de la caña y el botín de 1.8 (peces, basura y tesoros). Comportamiento observable de 1.8; implementación propia.
#include <glm/glm.hpp>

#include "core/random.h"
#include "core/types.h"
#include "game/item_stack.h"

namespace mcw {

/// Lo que cuelga de la caña: vuela, se queda flotando en el agua esperando un pez o se engancha a una criatura.
struct FishingHook {
  enum class State : u8 { Flying, Bobbing, Hooked, Ground } state = State::Flying;
  u32 id = 0;      // para el multijugador
  u32 owner = 1;   // uid del jugador que lo lanzó
  glm::dvec3 pos{0}, prevPos{0}, motion{0};
  u32 hookedMob = 0;     // criatura a la que está enganchado
  int wait = 0;          // ticks que faltan para que algo pique (0 = ya está picando o aún no se ha calculado)
  int nibble = 0;        // ticks que quedan con el pez en el anzuelo
  double dip = 0;        // lo que se hunde cuando pica
  double surface = 0;    // altura del agua donde flota
  int life = 0;
  bool waited = false;   // ya se ha tirado el tiempo de espera de este intento
  glm::dvec3 ownerHand{0};  // de dónde sale el sedal (la mano de quien lo lanzó)
  bool remote = false;     // llega del servidor: solo se dibuja, no se simula
  int lure = 0, luck = 0;  // Cebo y Suerte marina de la caña con la que se lanzó
};

/// Qué pesca el anzuelo: un pez, basura o un tesoro. `luck` es el nivel de Suerte marina y `lure` el de Cebo.
ItemStack fishingLoot(Random& rng, int luck, int lure);
/// Ticks de espera hasta el siguiente picotazo: de 100 a 600 menos 100 por cada nivel de Cebo.
int fishingWaitTicks(Random& rng, int lure);

}  // namespace mcw
