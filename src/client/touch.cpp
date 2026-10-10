#include "client/touch.h"

#include <algorithm>
#include <cmath>

#include "client/ui.h"

namespace mcw {
namespace {

constexpr u64 kHoldNs = 250'000'000;  // dedo quieto sobre el mundo: a los 0,25 s empieza a romper
constexpr u64 kTapNs = 350'000'000;   // un toque más corto que esto es un toque, no una pulsación larga
constexpr u64 kLongNs = 500'000'000;  // soltar: más de medio segundo = toda la pila

// Iconos de 13x13 píxeles (# = pintado), dibujados a mano para los botones.
constexpr const char* kSword[13] = {
    "............#", "...........#.", "..........##.", ".........##..", "........##...", ".##....##....", "..##..##.....",
    "...##.#......", "....##.......", "...#.##......", "..#...##.....", ".##....##....", "##...........",
};
constexpr const char* kBlock[13] = {
    "......##.....", "....##..##...", "..##......##.", "##..........#", "###.......###", "#..##...##..#", "#....###....#",
    "#.....#.....#", "#.....#.....#", "#.....#....##", ".##...#..##..", "...##.###....", ".....##......",
};
constexpr const char* kDrop[13] = {
    "......#......", "......#......", "......#......", "......#......", "..#...#...#..", "..##..#..##..", "...##.#.##...",
    "....#####....", ".....#.#.....", ".............", ".#.........#.", ".#.........#.", ".###########.",
};
constexpr const char* kEye[13] = {
    ".............", ".............", "....#####....", "...#.....#...", "..#..###..#..", ".#..#####..#.", "#...#####...#",
    ".#..#####..#.", "..#..###..#..", "...#.....#...", "....#####....", ".............", ".............",
};

constexpr std::size_t idx(TouchButton b) { return static_cast<std::size_t>(b); }

/// Colores con la opacidad elegida (0,7 = la de siempre).
struct Style {
  float alpha = 0.7f;
  u32 fade(u32 c) const {
    const float a = std::clamp(static_cast<float>(c >> 24) * alpha / 0.7f, 0.0f, 255.0f);
    return (static_cast<u32>(a) << 24) | (c & 0xFFFFFF);
  }
  u32 fill() const { return fade(0x50000000); }
  u32 pressed() const { return fade(0x70FFFFFF); }
  u32 edge() const { return fade(0x90FFFFFF); }
  u32 glyph() const { return fade(0xE0FFFFFF); }
};

void rectSafe(Ui& ui, float x, float y, float w, float h, u32 color) {
  if (w > 0 && h > 0 && (color >> 24) != 0) ui.rect(x, y, w, h, color);
}

/// Flecha vertical (punta y rabillo) en píxeles de GUI.
void arrow(Ui& ui, float cx, float cy, float size, bool up, u32 color) {
  const int total = std::max(6, static_cast<int>(std::lround(size)));
  const int head = std::max(3, total * 5 / 9);  // filas de la punta
  const int stemH = total - head;
  const int stemW = std::max(1, (head / 3) | 1);  // ancho impar: queda centrado
  const float x0 = std::floor(cx) + 0.5f;         // centro de un píxel
  const float top = std::round(cy - total / 2.0f);
  for (int i = 0; i < head; i++) {
    const int row = up ? i : total - 1 - i;
    ui.rect(x0 - static_cast<float>(i) - 0.5f, top + static_cast<float>(row), static_cast<float>(i * 2 + 1), 1, color);
  }
  for (int i = 0; i < stemH; i++) {
    const int row = up ? head + i : i;
    ui.rect(x0 - static_cast<float>(stemW) / 2.0f, top + static_cast<float>(row), static_cast<float>(stemW), 1, color);
  }
}

/// Cuadrado con las esquinas cortadas: el relleno y el borde no se solapan (así el alfa no se suma).
void panel(Ui& ui, const Style& st, float x, float y, float w, float h, bool pressed) {
  x = std::round(x);
  y = std::round(y);
  w = std::round(w);
  h = std::round(h);
  ui.rect(x + 1, y + 1, w - 2, h - 2, pressed ? st.pressed() : st.fill());
  ui.rect(x + 1, y, w - 2, 1, st.edge());
  ui.rect(x + 1, y + h - 1, w - 2, 1, st.edge());
  ui.rect(x, y + 1, 1, h - 2, st.edge());
  ui.rect(x + w - 1, y + 1, 1, h - 2, st.edge());
}

/// Círculo en filas de píxeles (estilo pixel art): relleno por dentro y un borde de `thickness`; el relleno y el
/// borde no se solapan. Un color con alfa 0 no se dibuja.
void circle(Ui& ui, float cx, float cy, float r, u32 fill, u32 edge, float thickness = 1.5f) {
  const int n = std::max(2, static_cast<int>(std::lround(r * 2)));
  const float rr = static_cast<float>(n) / 2.0f, ri = rr - thickness;
  const float x0 = std::round(cx), top = std::round(cy - rr);
  for (int i = 0; i < n; i++) {
    const float yy = static_cast<float>(i) + 0.5f - rr;
    const float ho = std::sqrt(std::max(0.0f, rr * rr - yy * yy));
    const float hi = ri > 0 && std::abs(yy) < ri ? std::sqrt(ri * ri - yy * yy) : 0.0f;
    const float xo0 = std::round(x0 - ho), xo1 = std::round(x0 + ho);
    const float y = top + static_cast<float>(i);
    if (hi <= 0.0f) {
      rectSafe(ui, xo0, y, xo1 - xo0, 1, thickness > 0 ? edge : fill);
      continue;
    }
    const float xi0 = std::round(x0 - hi), xi1 = std::round(x0 + hi);
    rectSafe(ui, xo0, y, xi0 - xo0, 1, edge);
    rectSafe(ui, xi0, y, xi1 - xi0, 1, fill);
    rectSafe(ui, xi1, y, xo1 - xi1, 1, edge);
  }
}

/// Un icono de 13x13 con cada píxel de `k` por `k`.
void bitmap(Ui& ui, const char* const* rows, float cx, float cy, int k, u32 color) {
  const float fk = static_cast<float>(k);
  const float x0 = std::round(cx - 13.0f * fk / 2.0f), y0 = std::round(cy - 13.0f * fk / 2.0f);
  for (int y = 0; y < 13; y++) {
    int x = 0;
    while (x < 13) {
      if (rows[y][x] != '#') {
        x++;
        continue;
      }
      int e = x;
      while (e < 13 && rows[y][e] == '#') e++;  // una sola rect por tramo
      ui.rect(x0 + static_cast<float>(x) * fk, y0 + static_cast<float>(y) * fk, static_cast<float>(e - x) * fk, fk, color);
      x = e;
    }
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

void TouchControls::setOptions(const TouchOptions& o) {
  if (o == opt_) return;
  const bool relayout = o.buttonScale != opt_.buttonScale || o.leftHanded != opt_.leftHanded || o.floatingStick != opt_.floatingStick;
  opt_ = o;
  if (relayout) layout();
}

void TouchControls::layout() {
  const float W = static_cast<float>(guiW_), H = static_cast<float>(guiH_);
  // Botones de unos 52 puntos (tamaño cómodo para un dedo), sin pasarse en pantallas pequeñas
  button_ = std::clamp(52.0f * opt_.buttonScale * density_ / static_cast<float>(guiScale_), 14.0f, std::min(W, H) / 4.0f);
  const float b = button_, m = b * 0.35f, gap = b * 0.2f, bottom = m + b * 0.3f;
  stickRadius_ = b * 1.25f;
  const float r = stickRadius_;
  stickCenter_ = {mirrorX(m + r, 0), H - bottom - r};
  if (opt_.floatingStick) {
    // Vale cualquier punto de la parte de abajo a la izquierda (a la derecha si es zurdo): la base nace ahí y se queda
    stickZone_ = {mirrorX(0, W * 0.45f), H * 0.30f, W * 0.45f, H - H * 0.30f};
  } else {
    const float x0 = std::max(0.0f, m + r - 1.6f * r), w = std::min(W, m + r + 1.6f * r) - x0;
    const float y0 = std::max(0.0f, H - bottom - r - 1.6f * r);
    stickZone_ = {mirrorX(x0, w), y0, w, H - y0};
  }
  auto set = [&](TouchButton id, float x, float y, float w, float h) { rect_[idx(id)] = {mirrorX(x, w), y, w, h}; };
  // Pulgar derecho: saltar abajo en la esquina, atacar encima, usar y agacharse en una columna a su izquierda
  const float jb = b * 1.3f, ab = b * 1.2f;
  const float jx = W - m - jb, jy = H - bottom - jb;
  set(TouchButton::Jump, jx, jy, jb, jb);
  set(TouchButton::Sneak, jx - gap - b, jy + jb - b, b, b);
  const float ax = jx + (jb - ab) / 2 - b * 0.1f, ay = jy - gap - ab;
  set(TouchButton::Attack, ax, ay, ab, ab);
  set(TouchButton::Use, ax - gap - b, ay + (ab - b) / 2 + b * 0.3f, b, b);
  // Barra rápida (igual que la del juego) con el inventario a su lado
  hotbar_ = {std::floor(W / 2 - 91), H - 22, 182, 22};
  const float inv = std::max(22.0f, b * 0.7f);
  set(TouchButton::Inventory, hotbar_.x + hotbar_.w + 3, H - inv, inv, inv);
  // Fila de arriba, de derecha a izquierda: pausa, chat, perspectiva y soltar
  const float s = b * 0.72f, sp = m * 0.5f;
  float x = W - m - s;
  for (const TouchButton id : {TouchButton::Pause, TouchButton::Chat, TouchButton::Perspective, TouchButton::Drop}) {
    set(id, x, m, s, s);
    x -= s + sp;
  }
}

bool TouchControls::buttonVisible(TouchButton b) const {
  if (b == TouchButton::Attack || b == TouchButton::Use) return opt_.actionButtons && opt_.scheme == TouchScheme::Crosshair;
  return true;
}

float TouchControls::topInset() const {
  if (!active_) return 0;
  const Rect& r = rect_[idx(TouchButton::Pause)];
  return r.y + r.h + 2;
}

bool TouchControls::stickHeld() const {
  return std::any_of(fingers_.begin(), fingers_.end(), [](const auto& kv) { return kv.second.role == Role::Stick; });
}

int TouchControls::slotAt(float x) const { return std::clamp(static_cast<int>(std::floor((x - (hotbar_.x + 1)) / 20.0f)), 0, 8); }

TouchControls::Role TouchControls::hit(glm::vec2 p, TouchButton& which) const {
  // Un dedo es gordo: cada botón acepta un poco más allá de su borde
  const float margin = button_ * 0.1f;
  for (std::size_t i = 0; i < kButtons; i++) {
    const auto id = static_cast<TouchButton>(i);
    if (!buttonVisible(id)) continue;
    const Rect& r = rect_[i];
    if (Rect{r.x - margin, r.y - margin, r.w + 2 * margin, r.h + 2 * margin}.contains(p)) {
      which = id;
      return Role::Button;
    }
  }
  if (Rect{hotbar_.x, hotbar_.y - 8, hotbar_.w, hotbar_.h + 8}.contains(p)) return Role::Hotbar;
  if (stickZone_.contains(p)) return Role::Stick;
  return Role::World;
}

void TouchControls::releaseAll() {
  fingers_.clear();
  edge_.fill(false);
  useTap_ = dropStack_ = sprintLatch_ = false;
  tapAim_.reset();
  slotTap_ = -1;
  haptic_ = 0;
  lookAccum_ = {0, 0};
}

bool TouchControls::handleEvent(const SDL_Event& e) {
  if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_MOTION && e.type != SDL_EVENT_FINGER_UP &&
      e.type != SDL_EVENT_FINGER_CANCELED)
    return false;
  const glm::vec2 p(e.tfinger.x * static_cast<float>(guiW_), e.tfinger.y * static_cast<float>(guiH_));
  const SDL_FingerID id = e.tfinger.fingerID;
  // La hora a la que ocurrió el toque (con frames lentos, la de llegar aquí sería tarde)
  const u64 now = e.common.timestamp != 0 ? e.common.timestamp : SDL_GetTicksNS();
  switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
      Finger f;
      TouchButton which = TouchButton::Jump;
      f.role = hit(p, which);
      if (f.role == Role::Stick && stickHeld()) f.role = Role::World;  // un segundo dedo en el joystick: mira
      f.start = f.pos = f.center = p;
      f.downTicks = now;
      switch (f.role) {
        case Role::Stick:
          if (!opt_.floatingStick) f.center = stickCenter_;
          break;
        case Role::Button:
          f.button = which;
          buzz(8);
          switch (which) {
            case TouchButton::Jump:
            case TouchButton::Attack:
            case TouchButton::Use: edge_[idx(which)] = true; break;
            case TouchButton::Sneak:
              if (!flying_) {
                // Un toque lo deja puesto o quitado; mantenerlo (más de un toque) agacha solo mientras se aprieta
                f.momentary = !sneakToggle_;
                sneakToggle_ = !sneakToggle_;
              }
              break;
            default: break;  // inventario, pausa, chat, perspectiva y soltar: al levantar el dedo sobre el botón
          }
          break;
        case Role::Hotbar:
          f.slot = slotAt(p.x);
          slotTap_ = f.slot;
          buzz(4);
          break;
        default: break;
      }
      fingers_[id] = f;
      break;
    }
    case SDL_EVENT_FINGER_MOTION: {
      auto it = fingers_.find(id);
      if (it == fingers_.end()) break;
      Finger& f = it->second;
      const glm::vec2 prev = f.pos;
      f.pos = p;
      switch (f.role) {
        case Role::Stick:
          // La base no se mueve nunca: el mando se queda en el aro aunque el dedo se pase (el centro es siempre el mismo)
          break;
        case Role::Hotbar: {
          const int s = slotAt(p.x);
          if (s != f.slot) {
            f.slot = s;
            slotTap_ = s;
            buzz(4);
          }
          break;
        }
        case Role::World: {
          // Con la mira central se puede girar mientras se rompe; en "tocar para apuntar" el dedo apunta
          if (f.breaking && opt_.scheme == TouchScheme::Pocket) break;
          // El umbral solo decide entre toque y arrastre: lo recorrido hasta pasarlo no se pierde
          const float threshold = 8.0f * density_ / static_cast<float>(guiScale_);
          if (!f.moved) {
            if (glm::length(p - f.start) > threshold) {
              f.moved = true;
              lookAccum_ += p - f.start;
            }
          } else {
            lookAccum_ += p - prev;
          }
          break;
        }
        default: break;
      }
      break;
    }
    default: {  // dedo levantado o cancelado
      auto it = fingers_.find(id);
      if (it == fingers_.end()) break;
      const Finger f = it->second;
      fingers_.erase(it);
      if (e.type == SDL_EVENT_FINGER_CANCELED) break;
      const u64 held = now > f.downTicks ? now - f.downTicks : 0;
      switch (f.role) {
        case Role::World:
          // Toque corto sin mover: usar / colocar donde se apunta
          if (!f.moved && !f.breaking && held < kTapNs) {
            useTap_ = true;
            if (opt_.scheme == TouchScheme::Pocket) tapAim_ = f.start;
            buzz(6);
          }
          break;
        case Role::Button: {
          const Rect& r = rect_[idx(f.button)];
          const float margin = button_ * 0.25f;  // soltar un pelín fuera aún cuenta
          const bool inside = Rect{r.x - margin, r.y - margin, r.w + 2 * margin, r.h + 2 * margin}.contains(p);
          switch (f.button) {
            case TouchButton::Inventory:
            case TouchButton::Pause:
            case TouchButton::Chat:
            case TouchButton::Perspective:
              if (inside) edge_[idx(f.button)] = true;
              break;
            case TouchButton::Drop:
              if (inside) {
                edge_[idx(f.button)] = true;
                dropStack_ = held >= kLongNs;
              }
              break;
            case TouchButton::Sneak:
              if (f.momentary && held >= kTapNs) sneakToggle_ = false;
              break;
            default: break;
          }
          break;
        }
        default: break;
      }
      break;
    }
  }
  return true;
}

void TouchControls::newFrame() {
  for (auto& [id, f] : fingers_) f.frames++;
}

float TouchControls::holdProgress(u64 nowNs) const {
  const u64 now = nowNs != 0 ? nowNs : SDL_GetTicksNS();
  for (const auto& [id, f] : fingers_)
    if (f.role == Role::World && !f.moved && !f.breaking)
      return std::min(1.0f, static_cast<float>(now - f.downTicks) / static_cast<float>(kHoldNs));
  return -1.0f;
}

TouchInput TouchControls::consume(u64 nowNs) {
  TouchInput in;
  const u64 now = nowNs != 0 ? nowNs : SDL_GetTicksNS();
  bool stickOn = false;
  for (auto& [id, f] : fingers_) {
    switch (f.role) {
      case Role::Stick: {
        stickOn = true;
        const glm::vec2 raw = (f.pos - f.center) / stickRadius_;
        const float len = glm::length(raw);
        const float dz = std::clamp(opt_.deadzone, 0.0f, 0.4f);
        const float pushed = std::min(len, 1.0f);
        if (pushed > dz) {
          // Zona muerta al centro, y una curva que da más precisión con poco empuje
          const float mag = std::pow((pushed - dz) / (1.0f - dz), 1.0f + std::clamp(opt_.curve, 0.0f, 1.0f) * 0.8f);
          in.strafe = raw.x / len * mag;
          in.forward = -raw.y / len * mag;
        }
        // Candado de correr: empujar a tope hacia delante lo pone y sigue hasta que el pulgar vuelve al centro
        if (len >= 0.95f && -raw.y / len > 0.6f) {
          if (!sprintLatch_) buzz(14);
          sprintLatch_ = true;
        } else if (in.forward <= 0.05f) {
          sprintLatch_ = false;
        }
        break;
      }
      case Role::Button:
        switch (f.button) {
          case TouchButton::Jump: in.jump = true; break;
          case TouchButton::Sneak:
            if (flying_) in.sneak = true;
            break;
          case TouchButton::Attack: in.attack = true; break;
          case TouchButton::Use: in.use = true; break;
          default: break;
        }
        break;
      case Role::World:
        if (!f.moved && !f.breaking && f.frames >= 3 && now - f.downTicks >= kHoldNs) {
          f.breaking = true;
          buzz(12);
        }
        if (f.breaking) {
          in.attack = in.holdWorld = true;
          if (opt_.scheme == TouchScheme::Pocket) in.aim = f.pos;
        }
        break;
      default: break;
    }
  }
  if (!stickOn) sprintLatch_ = false;
  in.sprint = sprintLatch_;
  if (!flying_ && sneakToggle_) in.sneak = true;
  in.jumpPressed = edge_[idx(TouchButton::Jump)];
  in.jump = in.jump || in.jumpPressed;  // un toque más corto que un tick también salta
  in.attackPressed = edge_[idx(TouchButton::Attack)];
  in.attack = in.attack || in.attackPressed;  // un toque más corto que un tick también rompe (en creativo, al instante)
  in.usePressed = edge_[idx(TouchButton::Use)];
  in.openInventory = edge_[idx(TouchButton::Inventory)];
  in.pause = edge_[idx(TouchButton::Pause)];
  in.chat = edge_[idx(TouchButton::Chat)];
  in.perspective = edge_[idx(TouchButton::Perspective)];
  in.drop = edge_[idx(TouchButton::Drop)];
  in.dropStack = dropStack_;
  if (useTap_) {
    in.tap = true;
    if (tapAim_) in.aim = tapAim_;
  }
  in.selectSlot = slotTap_;
  in.haptic = haptic_;
  edge_.fill(false);
  useTap_ = dropStack_ = false;
  tapAim_.reset();
  slotTap_ = -1;
  haptic_ = 0;
  return in;
}

void TouchControls::drawButton(Ui& ui, TouchButton b, bool pressed) const {
  const Style st{opt_.opacity};
  const Rect& r = rect_[idx(b)];
  const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const u32 glyph = st.glyph();
  const bool round = b == TouchButton::Jump || b == TouchButton::Sneak || b == TouchButton::Attack || b == TouchButton::Use;
  if (round) circle(ui, cx, cy, r.w / 2, pressed ? st.pressed() : st.fill(), st.edge());
  else panel(ui, st, r.x, r.y, r.w, r.h, pressed);
  const int k = std::max(1, static_cast<int>(std::lround(r.w * 0.5f / 13.0f)));  // tamaño de cada píxel de los iconos
  switch (b) {
    case TouchButton::Jump: arrow(ui, cx, cy, r.w * 0.5f, true, glyph); break;
    case TouchButton::Sneak: arrow(ui, cx, cy, r.w * 0.5f, false, glyph); break;
    case TouchButton::Attack: bitmap(ui, kSword, cx, cy, k, glyph); break;
    case TouchButton::Use: bitmap(ui, kBlock, cx, cy, k, glyph); break;
    case TouchButton::Drop: bitmap(ui, kDrop, cx, cy, k, glyph); break;
    case TouchButton::Perspective: bitmap(ui, kEye, cx, cy, k, glyph); break;
    case TouchButton::Inventory: {
      // Cuatro casillas
      const float sq = std::max(2.0f, std::round(r.w * 0.2f)), g = std::max(1.0f, std::round(r.w * 0.08f));
      const float x0 = std::round(cx - sq - g / 2), y0 = std::round(cy - sq - g / 2);
      for (int i = 0; i < 4; i++) ui.rect(x0 + static_cast<float>(i % 2) * (sq + g), y0 + static_cast<float>(i / 2) * (sq + g), sq, sq, glyph);
      break;
    }
    case TouchButton::Pause: {
      const float bw = std::max(2.0f, std::round(r.w * 0.12f));
      ui.rect(std::round(r.x + r.w * 0.32f), std::round(r.y + r.h * 0.25f), bw, std::round(r.h * 0.5f), glyph);
      ui.rect(std::round(r.x + r.w * 0.56f), std::round(r.y + r.h * 0.25f), bw, std::round(r.h * 0.5f), glyph);
      break;
    }
    case TouchButton::Chat: {
      // Un bocadillo con su rabillo
      const float bw = std::round(r.w * 0.56f), bh = std::round(r.h * 0.36f);
      const float bx = std::round(r.x + (r.w - bw) / 2), by = std::round(r.y + r.h * 0.24f);
      ui.rect(bx, by, bw, bh, glyph);
      ui.rect(bx + std::round(bw * 0.2f), by + bh, std::max(2.0f, std::round(bw * 0.18f)), std::max(2.0f, std::round(r.h * 0.12f)), glyph);
      break;
    }
    default: break;
  }
}

void TouchControls::draw(Ui& ui, int selectedSlot) const {
  if (!active_) return;
  (void)selectedSlot;
  const Style st{opt_.opacity};
  const float b = button_, R = stickRadius_;
  auto heldButton = [&](TouchButton id) {
    return std::any_of(fingers_.begin(), fingers_.end(),
                       [id](const auto& kv) { return kv.second.role == Role::Button && kv.second.button == id; });
  };

  // Joystick: la base (quieta) y el mando (dentro del aro)
  const Finger* stick = nullptr;
  for (const auto& [id, f] : fingers_)
    if (f.role == Role::Stick) stick = &f;
  if (stick) {
    glm::vec2 d = stick->pos - stick->center;
    const float len = glm::length(d);
    if (len > R) d *= R / len;
    const glm::vec2 c = stick->center, k = c + d;
    circle(ui, c.x, c.y, R, st.fade(0x38000000), st.edge());
    circle(ui, k.x, k.y, b * 0.45f, sprintLatch_ ? st.fade(0xD0FFE070) : st.fade(0xA0FFFFFF), 0, 0);
    if (sprintLatch_) arrow(ui, c.x, c.y - R - 8, 9, true, st.fade(0xE0FFE070));  // corriendo: la flecha del candado
  } else if (opt_.floatingStick) {
    // En reposo, un aro tenue que indica dónde poner el pulgar
    circle(ui, stickCenter_.x, stickCenter_.y, R, st.fade(0x20000000), st.fade(0x50FFFFFF));
    circle(ui, stickCenter_.x, stickCenter_.y, b * 0.45f, st.fade(0x40FFFFFF), 0, 0);
  } else {
    circle(ui, stickCenter_.x, stickCenter_.y, R, st.fade(0x38000000), st.edge());
    circle(ui, stickCenter_.x, stickCenter_.y, b * 0.45f, st.fade(0x80FFFFFF), 0, 0);
  }

  for (std::size_t i = 0; i < kButtons; i++) {
    const auto id = static_cast<TouchButton>(i);
    if (!buttonVisible(id)) continue;
    drawButton(ui, id, heldButton(id) || (id == TouchButton::Sneak && !flying_ && sneakToggle_));
  }

  // "Tocar para apuntar": un aro donde se está rompiendo
  if (opt_.scheme == TouchScheme::Pocket)
    for (const auto& [id, f] : fingers_)
      if (f.role == Role::World && f.breaking) circle(ui, f.pos.x, f.pos.y, b * 0.3f, 0, st.fade(0xC0FFFFFF), 1.5f);
}

void TouchControls::drawHold(Ui& ui) const {
  if (!active_) return;
  const u64 now = SDL_GetTicksNS();
  for (const auto& [id, f] : fingers_) {
    if (f.role != Role::World || f.moved || f.breaking) continue;
    const float t = std::min(1.0f, static_cast<float>(now - f.downTicks) / static_cast<float>(kHoldNs));
    if (t < 0.2f) continue;  // un toque corto no enseña nada
    const glm::vec2 c = opt_.scheme == TouchScheme::Crosshair ? glm::vec2(guiW_ / 2.0f, guiH_ / 2.0f) : f.pos;
    constexpr int kDots = 16;
    const int lit = static_cast<int>(t * kDots);
    for (int i = 0; i < kDots; i++) {
      const float a = static_cast<float>(i) / kDots * 6.2831853f - 1.5707963f;
      ui.rect(std::round(c.x + std::cos(a) * 11.0f) - 1, std::round(c.y + std::sin(a) * 11.0f) - 1, 2, 2,
              i < lit ? 0xE0FFFFFF : 0x40FFFFFF);
    }
    break;
  }
}

void TouchControls::drawClose(Ui& ui, bool pressed) const {
  if (!active_) return;
  const Style st{std::max(0.7f, opt_.opacity)};
  const Rect& r = rect_[idx(TouchButton::Pause)];
  panel(ui, st, r.x, r.y, r.w, r.h, pressed);
  // Aspa en diagonal, píxel a píxel
  const int n = std::max(4, static_cast<int>(r.w * 0.5f));
  const float x0 = std::round(r.x + (r.w - static_cast<float>(n)) / 2), y0 = std::round(r.y + (r.h - static_cast<float>(n)) / 2);
  const float t = std::max(1.0f, std::round(static_cast<float>(n) / 7.0f));
  for (int i = 0; i < n; i++) {
    ui.rect(x0 + static_cast<float>(i), y0 + static_cast<float>(i), t, t, st.glyph());
    ui.rect(x0 + static_cast<float>(n - 1 - i), y0 + static_cast<float>(i), t, t, st.glyph());
  }
}

}  // namespace mcw
