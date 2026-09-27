#include "client/touch.h"

#include <algorithm>
#include <cmath>

#include "client/ui.h"

namespace mcw {
namespace {

constexpr u64 kHoldNs = 300'000'000;  // mantener 0,3 s sin mover = romper

// Opacidad de los controles (Ajustes > Controles). Solo se usa al dibujar, en el hilo principal.
float gAlpha = 1.0f;
/// Color con el alfa escalado por la opacidad elegida (0,7 = la de siempre).
u32 fade(u32 c) {
  const float a = std::clamp(static_cast<float>(c >> 24) * gAlpha / 0.7f, 0.0f, 255.0f);
  return (static_cast<u32>(a) << 24) | (c & 0xFFFFFF);
}
u32 fillColor() { return fade(0x50000000); }
u32 pressedColor() { return fade(0x70FFFFFF); }
u32 edgeColor() { return fade(0x90FFFFFF); }
u32 glyphColor() { return fade(0xE0FFFFFF); }

/// Flecha en píxeles de GUI (triángulo escalonado, como un icono de 8 bits).
void arrow(Ui& ui, float cx, float cy, float size, int dir /*0 arriba 1 abajo 2 izq 3 der*/, u32 color) {
  const int rows = std::max(2, static_cast<int>(size / 2));
  for (int i = 0; i < rows; i++) {
    const float half = static_cast<float>(i);
    const float t = static_cast<float>(i) - rows / 2.0f;
    switch (dir) {
      case 0: ui.rect(cx - half - 0.5f, cy + t, half * 2 + 1, 1, color); break;
      case 1: ui.rect(cx - half - 0.5f, cy - t - 1, half * 2 + 1, 1, color); break;
      case 2: ui.rect(cx + t, cy - half - 0.5f, 1, half * 2 + 1, color); break;
      default: ui.rect(cx - t - 1, cy - half - 0.5f, 1, half * 2 + 1, color); break;
    }
  }
}

void frame(Ui& ui, float x, float y, float w, float h, bool pressed) {
  ui.rect(x, y, w, h, pressed ? pressedColor() : fillColor());
  ui.rect(x, y, w, 1, edgeColor());
  ui.rect(x, y + h - 1, w, 1, edgeColor());
  ui.rect(x, y + 1, 1, h - 2, edgeColor());
  ui.rect(x + w - 1, y + 1, 1, h - 2, edgeColor());
}

/// Círculo relleno en filas de píxeles (estilo pixel art).
void disc(Ui& ui, float cx, float cy, float r, u32 color) {
  for (float y = -r; y < r; y += 1.0f) {
    const float half = std::sqrt(std::max(0.0f, r * r - (y + 0.5f) * (y + 0.5f)));
    ui.rect(std::round(cx - half), cy + y, std::round(half * 2), 1, color);
  }
}

void ring(Ui& ui, float cx, float cy, float r, u32 color) {
  for (float y = -r; y < r; y += 1.0f) {
    const float yy = y + 0.5f;
    const float outer = std::sqrt(std::max(0.0f, r * r - yy * yy));
    const float innerR = r - 1.5f;
    const float inner = std::abs(yy) < innerR ? std::sqrt(innerR * innerR - yy * yy) : 0.0f;
    ui.rect(std::round(cx - outer), cy + y, std::round(outer - inner), 1, color);
    ui.rect(std::round(cx + inner), cy + y, std::round(outer - inner), 1, color);
  }
}

}  // namespace

void TouchControls::setScreen(int guiW, int guiH, int guiScale, float density) {
  if (guiW == guiW_ && guiH == guiH_ && guiScale == guiScale_ && density == density_) return;
  guiW_ = guiW;
  guiH_ = guiH;
  guiScale_ = std::max(1, guiScale);
  density_ = std::max(0.5f, density);
  layout();
}

void TouchControls::setOptions(float buttonScale, float opacity, bool floatingStick) {
  opacity_ = opacity;
  floating_ = floatingStick;
  if (buttonScale != buttonScale_) {
    buttonScale_ = buttonScale;
    layout();
  }
}

void TouchControls::layout() {
  // Botones de unos 52 puntos (tamaño cómodo para un dedo), sin pasarse en pantallas pequeñas
  button_ = std::clamp(52.0f * buttonScale_ * density_ / guiScale_, 14.0f, std::min(guiW_, guiH_) / 4.0f);
  const float b = button_, m = b * 0.35f, gap = b * 0.2f;
  stickRadius_ = b * 1.25f;
  stickCenter_ = {m + stickRadius_, guiH_ - m - stickRadius_ - b * 0.3f};
  stick_ = {0, stickCenter_.y - stickRadius_ * 1.6f, stickCenter_.x + stickRadius_ * 1.6f, guiH_ - (stickCenter_.y - stickRadius_ * 1.6f)};
  jump_ = {guiW_ - m - b * 1.2f, guiH_ - m - b * 1.2f - b * 0.3f, b * 1.2f, b * 1.2f};
  sneak_ = {jump_.x - gap - b, jump_.y + b * 0.2f, b, b};
  hotbar_ = {std::floor(guiW_ / 2.0f - 91), static_cast<float>(guiH_ - 22), 182, 22};
  inventory_ = {hotbar_.x + hotbar_.w + 2, hotbar_.y, 22, 22};
  const float s = b * 0.72f;
  pause_ = {guiW_ - m - s, m, s, s};
}

TouchControls::Role TouchControls::hit(glm::vec2 p) const {
  if (pause_.contains(p)) return Role::Pause;
  if (hotbar_.contains(p)) return Role::Hotbar;
  if (inventory_.contains(p)) return Role::Inventory;
  if (jump_.contains(p)) return Role::Jump;
  if (sneak_.contains(p)) return Role::Sneak;
  if (stick_.contains(p)) return Role::Stick;
  // Joystick flotante: vale cualquier punto de la parte izquierda de abajo
  if (floating_ && p.x < guiW_ * 0.4f && p.y > guiH_ * 0.4f) return Role::Stick;
  return Role::World;
}

bool TouchControls::handleEvent(const SDL_Event& e) {
  if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_MOTION && e.type != SDL_EVENT_FINGER_UP &&
      e.type != SDL_EVENT_FINGER_CANCELED)
    return false;
  const glm::vec2 p(e.tfinger.x * guiW_, e.tfinger.y * guiH_);
  const SDL_FingerID id = e.tfinger.fingerID;
  const u64 now = SDL_GetTicksNS();
  switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
      Finger f;
      f.role = hit(p);
      f.start = f.pos = p;
      f.downTicks = now;
      switch (f.role) {
        case Role::Jump: jumpEdge_ = true; break;
        case Role::Sneak: if (!flying_) sneakToggle_ = !sneakToggle_; break;
        case Role::Hotbar: slotTap_ = std::clamp(static_cast<int>((p.x - hotbar_.x) / 20.0f), 0, 8); break;
        case Role::Inventory: invTap_ = true; break;
        case Role::Pause: pauseTap_ = true; break;
        default: break;
      }
      fingers_[id] = f;
      break;
    }
    case SDL_EVENT_FINGER_MOTION: {
      auto it = fingers_.find(id);
      if (it == fingers_.end()) break;
      Finger& f = it->second;
      if (f.role == Role::World) {
        // Solo cuenta como "mirar" si el dedo se ha movido de verdad (unos 10 puntos)
        const float threshold = 10.0f * density_ / guiScale_;
        if (!f.moved && glm::length(p - f.start) > threshold && !f.breaking) f.moved = true;
        if (f.moved && !f.breaking) lookAccum_ += p - f.pos;
      }
      f.pos = p;
      break;
    }
    default: {
      auto it = fingers_.find(id);
      if (it != fingers_.end()) {
        const Finger& f = it->second;
        // Toque corto sin mover sobre el mundo: usar / colocar ahí
        if (f.role == Role::World && !f.moved && !f.breaking) {
          useTap_ = true;
          tapAim_ = f.start;
        }
        fingers_.erase(it);
      }
      break;
    }
  }
  return true;
}

void TouchControls::newFrame() {
  for (auto& [id, f] : fingers_) f.frames++;
}

TouchInput TouchControls::consume() {
  TouchInput in;
  const u64 now = SDL_GetTicksNS();
  for (auto& [id, f] : fingers_) {
    switch (f.role) {
      case Role::Stick: {
        glm::vec2 d = (f.pos - (floating_ ? f.start : stickCenter_)) / stickRadius_;
        const float len = glm::length(d);
        if (len > 1.0f) d /= len;
        if (len > 0.12f) {
          in.strafe = d.x;
          in.forward = -d.y;
          in.sprint = len > 0.95f && -d.y > 0.7f;  // al borde y hacia delante: correr
        }
        break;
      }
      case Role::Jump: in.jump = true; break;
      case Role::Sneak: if (flying_) in.sneak = true; break;
      case Role::World:
        if (!f.moved && f.frames >= 3 && now - f.downTicks >= kHoldNs) f.breaking = true;
        if (f.breaking) {
          in.attack = true;
          in.aim = f.pos;
        }
        break;
      default: break;
    }
  }
  if (!flying_ && sneakToggle_) in.sneak = true;
  in.jumpPressed = jumpEdge_;
  in.look = lookAccum_;
  if (useTap_) {
    in.usePressed = true;
    in.aim = tapAim_;
  }
  in.selectSlot = slotTap_;
  in.openInventory = invTap_;
  in.pause = pauseTap_;
  lookAccum_ = {0, 0};
  jumpEdge_ = useTap_ = invTap_ = pauseTap_ = false;
  tapAim_.reset();
  slotTap_ = -1;
  return in;
}

void TouchControls::draw(Ui& ui, int selectedSlot) const {
  if (!active_) return;
  (void)selectedSlot;
  gAlpha = opacity_;
  const float b = button_;
  auto held = [&](Role r) {
    return std::any_of(fingers_.begin(), fingers_.end(), [r](const auto& kv) { return kv.second.role == r; });
  };

  // Joystick: aro y mando que sigue al dedo
  glm::vec2 center = stickCenter_, knob = stickCenter_;
  for (const auto& [id, f] : fingers_)
    if (f.role == Role::Stick) {
      if (floating_) center = f.start;
      glm::vec2 d = f.pos - center;
      if (glm::length(d) > stickRadius_) d = glm::normalize(d) * stickRadius_;
      knob = center + d;
    }
  disc(ui, center.x, center.y, stickRadius_, fade(0x38000000));
  ring(ui, center.x, center.y, stickRadius_, edgeColor());
  disc(ui, knob.x, knob.y, b * 0.45f, fade(held(Role::Stick) ? 0xB0FFFFFF : 0x80FFFFFF));

  // Saltar (o subir al volar) y agacharse (o bajar)
  const float g = b * 0.35f;
  frame(ui, jump_.x, jump_.y, jump_.w, jump_.h, held(Role::Jump));
  arrow(ui, jump_.x + jump_.w / 2, jump_.y + jump_.h / 2, g * 2.2f, 0, glyphColor());
  frame(ui, sneak_.x, sneak_.y, sneak_.w, sneak_.h, flying_ ? held(Role::Sneak) : sneakToggle_);
  arrow(ui, sneak_.x + b / 2, sneak_.y + b / 2, g * 2, 1, glyphColor());

  // Inventario (junto a la barra rápida) y pausa
  frame(ui, inventory_.x, inventory_.y, inventory_.w, inventory_.h, held(Role::Inventory));
  for (int i = 0; i < 3; i++) ui.rect(inventory_.x + 5 + i * 5, inventory_.y + 10, 2, 2, glyphColor());
  frame(ui, pause_.x, pause_.y, pause_.w, pause_.h, held(Role::Pause));
  ui.rect(pause_.x + pause_.w * 0.32f, pause_.y + pause_.h * 0.25f, std::max(2.0f, pause_.w * 0.12f), pause_.h * 0.5f, glyphColor());
  ui.rect(pause_.x + pause_.w * 0.56f, pause_.y + pause_.h * 0.25f, std::max(2.0f, pause_.w * 0.12f), pause_.h * 0.5f, glyphColor());

  // Punto de mira donde se está rompiendo
  for (const auto& [id, f] : fingers_)
    if (f.role == Role::World && f.breaking) ring(ui, f.pos.x, f.pos.y, b * 0.3f, fade(0xC0FFFFFF));
}

void TouchControls::drawClose(Ui& ui, bool pressed) const {
  if (!active_) return;
  gAlpha = std::max(0.7f, opacity_);
  frame(ui, pause_.x, pause_.y, pause_.w, pause_.h, pressed);
  // Aspa en diagonal, píxel a píxel
  const int n = std::max(4, static_cast<int>(pause_.w * 0.5f));
  const float x0 = pause_.x + (pause_.w - n) / 2, y0 = pause_.y + (pause_.h - n) / 2;
  const float t = std::max(1.0f, std::round(n / 7.0f));
  for (int i = 0; i < n; i++) {
    ui.rect(x0 + i, y0 + i, t, t, glyphColor());
    ui.rect(x0 + n - 1 - i, y0 + i, t, t, glyphColor());
  }
}

}  // namespace mcw
