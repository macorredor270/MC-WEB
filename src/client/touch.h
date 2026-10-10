#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>

#include "core/types.h"

namespace mcw {

class Ui;

/// Lo que los controles táctiles piden este tick.
struct TouchInput {
  float forward = 0, strafe = 0;  // joystick analógico (-1..1), ya con zona muerta y curva
  bool jump = false, jumpPressed = false, sneak = false, sprint = false;
  bool attack = false;            // romper: dedo quieto sobre el mundo, o el botón Atacar mantenido
  bool holdWorld = false;         // ... y es el dedo del mundo (con comida o arco en la mano, mantener es usar)
  bool attackPressed = false;     // el botón Atacar acaba de pulsarse (un golpe)
  bool use = false;               // el botón Usar mantenido (comer, tensar el arco, colocar sin parar)
  bool usePressed = false;        // el botón Usar acaba de pulsarse (clic derecho)
  bool tap = false;               // toque corto sobre el mundo: usar o colocar donde se apunta, o golpear a una criatura
  std::optional<glm::vec2> aim;   // solo con "tocar para apuntar": punto de la pantalla (GUI) al que apunta el dedo
  int selectSlot = -1;            // toque o deslizamiento sobre la barra rápida
  bool openInventory = false, pause = false, chat = false, perspective = false;
  bool drop = false, dropStack = false;  // soltar: un toque suelta uno; mantener, toda la pila
  int haptic = 0;                 // milisegundos de vibración que piden los botones (0 = nada)
};

/// Cómo se apunta: con una mira en el centro (el dedo solo gira la cámara) o tocando el bloque (estilo Pocket).
enum class TouchScheme { Crosshair, Pocket };

/// Los botones de la pantalla.
enum class TouchButton : u8 { Jump, Sneak, Attack, Use, Inventory, Drop, Pause, Chat, Perspective, Count };

/// Ajustes > Controles > Pantalla táctil.
struct TouchOptions {
  float buttonScale = 1.0f, opacity = 0.7f;
  float deadzone = 0.12f;  // zona muerta del joystick, de 0 a 0,4
  float curve = 0.5f;      // 0 = lineal, 1 = muy suave al empezar (más precisión con poco empuje)
  bool floatingStick = false;  // fijo (siempre en el mismo sitio) o que nace donde cae el pulgar; en ninguno de los dos sigue al dedo
  bool actionButtons = true;  // los botones Atacar y Usar (solo con la mira central)
  bool leftHanded = false;    // joystick a la derecha y botones a la izquierda
  bool haptics = true;
  TouchScheme scheme = TouchScheme::Crosshair;
  bool operator==(const TouchOptions&) const = default;
};

/// Controles táctiles: un joystick (que aparece donde se pone el pulgar y sigue al dedo), saltar y agacharse
/// abajo, atacar y usar a un lado, y en el resto de la pantalla un dedo que gira la cámara: toque corto = usar
/// o colocar, mantener = romper (sin dejar de poder girar).
class TouchControls {
 public:
  bool active() const { return active_; }
  void setActive(bool a) { active_ = a; }

  /// `density`: píxeles físicos por punto (para que los botones midan lo mismo en cualquier pantalla).
  void setScreen(int guiW, int guiH, int guiScale, float density);
  void setFlying(bool f) {
    if (f && !flying_) sneakToggle_ = false;  // al volar, agacharse es bajar mientras se aprieta
    flying_ = f;
  }
  void setOptions(const TouchOptions& o);
  const TouchOptions& options() const { return opt_; }
  /// Devuelve true si el evento era de un dedo (y lo ha consumido).
  bool handleEvent(const SDL_Event& e);
  /// Olvida todos los dedos y toques pendientes (al abrir o cerrar una pantalla: los eventos de dedo van
  /// a otro sitio y no llegarían a soltar nada).
  void releaseAll();
  /// Estado de este tick; arrastres y toques se reinician al leerlos. `nowNs`: la hora (0 = la de ahora; las
  /// pruebas pasan la suya para no esperar de verdad).
  TouchInput consume(u64 nowNs = 0);
  /// Píxeles de GUI arrastrados para mirar desde la última vez que se llamó. Se lee en cada frame (no en
  /// el tick de 20 Hz): así la cámara gira tan fluida como la pantalla.
  glm::vec2 takeLook() {
    const glm::vec2 v = lookAccum_;
    lookAccum_ = {0, 0};
    return v;
  }
  /// Llamar una vez por frame: un toque solo cuenta como "mantener" si han pasado varios frames
  /// (con frames lentos, el movimiento del dedo puede llegar tarde).
  void newFrame();
  void draw(Ui& ui, int selectedSlot) const;
  /// Un anillo alrededor de la mira que se completa mientras el dedo se mantiene quieto (empieza a romper).
  void drawHold(Ui& ui) const;
  /// Cuánto falta para que el dedo quieto sobre el mundo empiece a romper (0 a 1), o -1 si no hay ninguno.
  float holdProgress(u64 nowNs = 0) const;
  /// Posición de los controles en píxeles de GUI (para dibujar ayudas y para las pruebas).
  glm::vec2 stickCenter() const { return stickCenter_; }
  float stickRadius() const { return stickRadius_; }
  glm::vec2 buttonCenter(TouchButton b) const {
    const Rect& r = rect_[static_cast<std::size_t>(b)];
    return {r.x + r.w / 2, r.y + r.h / 2};
  }
  bool buttonVisible(TouchButton b) const;
  /// ¿Está el candado de correr puesto (empujaste el joystick a tope hacia delante)?
  bool sprintLocked() const { return sprintLatch_; }
  /// Altura (en píxeles de GUI) que ocupan los botones de arriba, para no tapar texto con ellos.
  float topInset() const;
  /// Botón de cerrar de los menús (en el mismo sitio que el de pausa).
  bool closeHit(glm::vec2 guiPoint) const { return active_ && rect_[static_cast<std::size_t>(TouchButton::Pause)].contains(guiPoint); }
  void drawClose(Ui& ui, bool pressed) const;

 private:
  enum class Role { None, Stick, Button, Hotbar, World };
  struct Finger {
    Role role = Role::None;
    TouchButton button = TouchButton::Jump;
    glm::vec2 start{0}, pos{0}, center{0};  // center: la base del joystick (no se mueve mientras el dedo está puesto)
    u64 downTicks = 0;
    int frames = 0;
    int slot = -1;  // barra rápida: casilla bajo el dedo
    bool moved = false, breaking = false;
    bool momentary = false;  // agacharse: si se mantiene, al soltar se vuelve a levantar
  };
  struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
  };
  static constexpr std::size_t kButtons = static_cast<std::size_t>(TouchButton::Count);

  void layout();
  Role hit(glm::vec2 p, TouchButton& which) const;
  bool stickHeld() const;
  int slotAt(float x) const;
  void buzz(int ms) {
    if (opt_.haptics) haptic_ = std::max(haptic_, ms);
  }
  float mirrorX(float x, float w) const { return opt_.leftHanded ? static_cast<float>(guiW_) - x - w : x; }
  void drawButton(Ui& ui, TouchButton b, bool pressed) const;

  bool active_ = false, flying_ = false;
  TouchOptions opt_;
  int guiW_ = 320, guiH_ = 240, guiScale_ = 2;
  float density_ = 1, button_ = 24, stickRadius_ = 30;
  glm::vec2 stickCenter_{0};   // centro del joystick fijo
  Rect stickZone_{};           // donde un dedo se convierte en joystick
  Rect hotbar_{};
  std::array<Rect, kButtons> rect_{};
  std::array<bool, kButtons> edge_{};  // pulsados desde el último tick
  std::unordered_map<SDL_FingerID, Finger> fingers_;
  glm::vec2 lookAccum_{0};
  bool sneakToggle_ = false, useTap_ = false, sprintLatch_ = false, dropStack_ = false;
  int haptic_ = 0;
  std::optional<glm::vec2> tapAim_;
  int slotTap_ = -1;
};

}  // namespace mcw
