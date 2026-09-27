#pragma once
#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>

#include "core/types.h"

namespace mcw {

class Ui;

/// Lo que los controles táctiles piden este tick.
struct TouchInput {
  float forward = 0, strafe = 0;  // joystick analógico (-1..1)
  bool jump = false, jumpPressed = false, sneak = false, sprint = false;
  glm::vec2 look{0};              // píxeles de GUI arrastrados desde el último tick
  bool attack = false;            // mantener el dedo quieto sobre el mundo: romper
  bool usePressed = false;        // toque corto sobre el mundo: usar / colocar
  std::optional<glm::vec2> aim;   // punto de la pantalla (GUI) al que apunta el dedo
  int selectSlot = -1;            // toque en la barra rápida
  bool openInventory = false, pause = false;
};

/// Controles táctiles al estilo de la edición de bolsillo: joystick abajo a la izquierda,
/// saltar/agacharse (o subir/bajar al volar) abajo a la derecha, y en el resto de la pantalla:
/// arrastrar para mirar, tocar para colocar y mantener para romper.
class TouchControls {
 public:
  bool active() const { return active_; }
  void setActive(bool a) { active_ = a; }

  /// `density`: píxeles físicos por punto (para que los botones midan lo mismo en cualquier pantalla).
  void setScreen(int guiW, int guiH, int guiScale, float density);
  void setFlying(bool f) { flying_ = f; }
  /// Devuelve true si el evento era de un dedo (y lo ha consumido).
  bool handleEvent(const SDL_Event& e);
  /// Estado de este tick; arrastres y toques se reinician al leerlos.
  TouchInput consume();
  /// Llamar una vez por frame: un toque solo cuenta como "mantener" si han pasado varios frames
  /// (con frames lentos, el movimiento del dedo puede llegar tarde).
  void newFrame();
  void draw(Ui& ui, int selectedSlot) const;
  /// Altura (en píxeles de GUI) que ocupan los botones de arriba, para no tapar texto con ellos.
  float topInset() const { return active_ ? pause_.y + pause_.h + 3 : 0; }
  /// Botón de cerrar de los menús (en el mismo sitio que el de pausa).
  bool closeHit(glm::vec2 guiPoint) const { return active_ && pause_.contains(guiPoint); }
  void drawClose(Ui& ui, bool pressed) const;

 private:
  enum class Role { None, Stick, Jump, Sneak, Hotbar, Inventory, Pause, World };
  struct Finger {
    Role role = Role::None;
    glm::vec2 start{0}, pos{0};
    u64 downTicks = 0;
    int frames = 0;
    bool moved = false, breaking = false;
  };
  struct Rect {
    float x, y, w, h;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
  };
  void layout();
  Role hit(glm::vec2 p) const;

  bool active_ = false, flying_ = false;
  int guiW_ = 320, guiH_ = 240, guiScale_ = 2;
  float density_ = 1, button_ = 24, stickRadius_ = 30;
  glm::vec2 stickCenter_{0};
  Rect stick_{}, jump_{}, sneak_{}, hotbar_{}, inventory_{}, pause_{};
  std::unordered_map<SDL_FingerID, Finger> fingers_;
  glm::vec2 lookAccum_{0};
  bool sneakToggle_ = false, jumpEdge_ = false, useTap_ = false, invTap_ = false, pauseTap_ = false;
  std::optional<glm::vec2> tapAim_;
  int slotTap_ = -1;
};

}  // namespace mcw
