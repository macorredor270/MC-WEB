#include "client/touch.h"

#include <algorithm>
#include <cmath>

#include "client/ui.h"

namespace mcw {
namespace {

constexpr u32 kFill = 0x50000000, kFillPressed = 0x70FFFFFF, kEdge = 0x90FFFFFF, kGlyph = 0xE0FFFFFF;

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
  ui.rect(x, y, w, h, pressed ? kFillPressed : kFill);
  ui.rect(x, y, w, 1, kEdge);
  ui.rect(x, y + h - 1, w, 1, kEdge);
  ui.rect(x, y + 1, 1, h - 2, kEdge);
  ui.rect(x + w - 1, y + 1, 1, h - 2, kEdge);
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

void TouchControls::layout() {
  // Botones de unos 52 puntos (tamaño cómodo para un dedo), sin pasarse en pantallas pequeñas
  button_ = std::clamp(52.0f * density_ / guiScale_, 16.0f, std::min(guiW_, guiH_) / 5.0f);
  const float b = button_, m = b * 0.35f, gap = b * 0.15f;
  dpad_ = {m, guiH_ - m - 3 * b, 3 * b, 3 * b};
  down_ = {guiW_ - m - b, guiH_ - m - b, b, b};
  up_ = {guiW_ - m - b, down_.y - gap - b, b, b};
  sprint_ = {down_.x - gap - b, down_.y, b, b};
  const float s = b * 0.72f;
  distance_ = {guiW_ - m - s * 1.6f, m, s * 1.6f, s};
  time_ = {distance_.x - gap - s * 1.6f, m, s * 1.6f, s};
  debug_ = {time_.x - gap - s * 1.2f, m, s * 1.2f, s};
}

TouchControls::Role TouchControls::hit(glm::vec2 p) const {
  if (dpad_.contains(p)) return Role::DPad;
  if (up_.contains(p)) return Role::Up;
  if (down_.contains(p)) return Role::Down;
  if (sprint_.contains(p)) return Role::Sprint;
  if (debug_.contains(p)) return Role::Debug;
  if (time_.contains(p)) return Role::Time;
  if (distance_.contains(p)) return Role::Distance;
  return Role::Look;
}

bool TouchControls::handleEvent(const SDL_Event& e) {
  if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_MOTION && e.type != SDL_EVENT_FINGER_UP &&
      e.type != SDL_EVENT_FINGER_CANCELED)
    return false;
  const glm::vec2 p(e.tfinger.x * guiW_, e.tfinger.y * guiH_);
  const SDL_FingerID id = e.tfinger.fingerID;
  switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
      Finger f;
      f.role = hit(p);
      f.pos = p;
      switch (f.role) {
        case Role::Sprint: sprintOn_ = !sprintOn_; break;
        case Role::Debug: tapDebug_ = true; break;
        case Role::Time: tapTime_ = true; break;
        case Role::Distance: tapDistance_ = true; break;
        default: break;
      }
      fingers_[id] = f;
      break;
    }
    case SDL_EVENT_FINGER_MOTION: {
      auto it = fingers_.find(id);
      if (it == fingers_.end()) break;
      if (it->second.role == Role::Look) lookAccum_ += p - it->second.pos;
      // Deslizar entre subir y bajar cambia de botón sin levantar el dedo
      if (it->second.role == Role::Up && down_.contains(p)) it->second.role = Role::Down;
      else if (it->second.role == Role::Down && up_.contains(p)) it->second.role = Role::Up;
      it->second.pos = p;
      break;
    }
    default:
      fingers_.erase(id);
      break;
  }
  return true;
}

glm::vec2 TouchControls::dpadDirection() const {
  for (const auto& [id, f] : fingers_) {
    if (f.role != Role::DPad) continue;
    // Dirección desde el centro, en 8 direcciones (como la cruceta al deslizar el dedo)
    const glm::vec2 c(dpad_.x + dpad_.w / 2, dpad_.y + dpad_.h / 2);
    glm::vec2 d = f.pos - c;
    const float len = glm::length(d);
    if (len < button_ * 0.3f) return {0, 0};
    d /= len;
    return {std::abs(d.x) > 0.38f ? (d.x > 0 ? 1.0f : -1.0f) : 0.0f, std::abs(d.y) > 0.38f ? (d.y > 0 ? 1.0f : -1.0f) : 0.0f};
  }
  return {0, 0};
}

TouchInput TouchControls::consume() {
  TouchInput in;
  const glm::vec2 d = dpadDirection();
  in.strafe = d.x;
  in.forward = -d.y;
  for (const auto& [id, f] : fingers_) {
    if (f.role == Role::Up) in.vertical += 1;
    if (f.role == Role::Down) in.vertical -= 1;
  }
  in.vertical = std::clamp(in.vertical, -1.0f, 1.0f);
  in.sprint = sprintOn_;
  in.look = lookAccum_;
  in.toggleDebug = tapDebug_;
  in.advanceTime = tapTime_;
  in.cycleRenderDistance = tapDistance_;
  lookAccum_ = {0, 0};
  tapDebug_ = tapTime_ = tapDistance_ = false;
  return in;
}

void TouchControls::draw(Ui& ui) const {
  if (!active_) return;
  const float b = button_;
  const glm::vec2 d = dpadDirection();
  auto held = [&](Role r) {
    return std::any_of(fingers_.begin(), fingers_.end(), [r](const auto& kv) { return kv.second.role == r; });
  };

  // Cruceta: 4 botones alrededor de un centro vacío
  const float x0 = dpad_.x, y0 = dpad_.y, g = b * 0.35f;
  frame(ui, x0 + b, y0, b, b, d.y < 0);
  arrow(ui, x0 + 1.5f * b, y0 + 0.5f * b, g * 2, 0, kGlyph);
  frame(ui, x0 + b, y0 + 2 * b, b, b, d.y > 0);
  arrow(ui, x0 + 1.5f * b, y0 + 2.5f * b, g * 2, 1, kGlyph);
  frame(ui, x0, y0 + b, b, b, d.x < 0);
  arrow(ui, x0 + 0.5f * b, y0 + 1.5f * b, g * 2, 2, kGlyph);
  frame(ui, x0 + 2 * b, y0 + b, b, b, d.x > 0);
  arrow(ui, x0 + 2.5f * b, y0 + 1.5f * b, g * 2, 3, kGlyph);

  // Subir / bajar / correr
  frame(ui, up_.x, up_.y, up_.w, up_.h, held(Role::Up));
  arrow(ui, up_.x + b / 2, up_.y + b / 2, g * 2, 0, kGlyph);
  frame(ui, down_.x, down_.y, down_.w, down_.h, held(Role::Down));
  arrow(ui, down_.x + b / 2, down_.y + b / 2, g * 2, 1, kGlyph);
  frame(ui, sprint_.x, sprint_.y, sprint_.w, sprint_.h, sprintOn_);
  // Correr: dos flechas hacia la derecha (»)
  arrow(ui, sprint_.x + b * 0.4f, sprint_.y + b / 2, g * 1.6f, 3, kGlyph);
  arrow(ui, sprint_.x + b * 0.4f + g * 0.9f, sprint_.y + b / 2, g * 1.6f, 3, kGlyph);

  // Botones pequeños de arriba a la derecha
  auto labeled = [&](const Rect& r, const char* text, Role role) {
    frame(ui, r.x, r.y, r.w, r.h, held(role));
    ui.text(r.x + (r.w - ui.textWidth(text)) / 2 + 0.5f, r.y + (r.h - 8) / 2, text, 0xFFFFFF);
  };
  labeled(debug_, "F3", Role::Debug);
  labeled(time_, "Hora", Role::Time);
  labeled(distance_, "Dist", Role::Distance);
}

}  // namespace mcw
