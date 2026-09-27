#pragma once
#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <unordered_map>

#include "core/types.h"

namespace mcw {

class Ui;

/// Lo que los controles táctiles piden este frame.
struct TouchInput {
  float forward = 0, strafe = 0, vertical = 0;  // -1..1
  bool sprint = false;
  glm::vec2 look{0};  // píxeles de GUI arrastrados desde el último frame
  bool toggleDebug = false, advanceTime = false, cycleRenderDistance = false;
};

/// Controles táctiles al estilo de la edición de bolsillo: cruceta de 8 direcciones abajo a la
/// izquierda, subir/bajar y correr abajo a la derecha, y arrastrar en el resto de la pantalla
/// para mirar. Cada dedo se asigna a un control al tocar y lo conserva hasta levantarse.
class TouchControls {
 public:
  bool active() const { return active_; }
  void setActive(bool a) { active_ = a; }

  /// `density`: píxeles físicos por punto (para que los botones midan lo mismo en cualquier pantalla).
  void setScreen(int guiW, int guiH, int guiScale, float density);
  /// Devuelve true si el evento era de un dedo (y lo ha consumido).
  bool handleEvent(const SDL_Event& e);
  /// Estado de este frame; el arrastre y los toques se reinician al leerlos.
  TouchInput consume();
  void draw(Ui& ui) const;
  /// Altura (en píxeles de GUI) que ocupan los botones de arriba, para no tapar texto con ellos.
  float topInset() const { return active_ ? debug_.y + debug_.h + 3 : 0; }

 private:
  enum class Role { None, DPad, Up, Down, Sprint, Debug, Time, Distance, Look };
  struct Finger {
    Role role = Role::None;
    glm::vec2 pos{0};
  };
  struct Rect {
    float x, y, w, h;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
  };
  void layout();
  Role hit(glm::vec2 p) const;
  glm::vec2 dpadDirection() const;

  bool active_ = false;
  int guiW_ = 320, guiH_ = 240, guiScale_ = 2;
  float density_ = 1;
  float button_ = 24;
  Rect dpad_{}, up_{}, down_{}, sprint_{}, debug_{}, time_{}, distance_{};
  std::unordered_map<SDL_FingerID, Finger> fingers_;
  glm::vec2 lookAccum_{0};
  bool sprintOn_ = false, tapDebug_ = false, tapTime_ = false, tapDistance_ = false;
};

}  // namespace mcw
