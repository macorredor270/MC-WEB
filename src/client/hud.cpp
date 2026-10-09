#include "client/hud.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "client/item_renderer.h"
#include "client/ui.h"
#include "game/player.h"
#include "game/rules.h"

namespace mcw {
namespace {

std::string menuTitle(MenuKind k) {
  switch (k) {
    case MenuKind::Inventory: return "Fabricación";
    case MenuKind::Crafting: return "Mesa de trabajo";
    case MenuKind::Furnace: return "Horno";
    case MenuKind::Creative: return "Modo creativo";
    case MenuKind::Chest: return "Cofre";
  }
  return {};
}

/// Quita tildes para la fuente ASCII del juego (que no las tiene).
std::string ascii(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[++i]);
      static const char* map = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
      const int idx = d - 0x80;
      out += (idx >= 0 && idx < 64) ? map[idx] : '?';
    } else if (c == 0xC2 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[++i]);
      out += d == 0xBF ? '?' : (d == 0xA1 ? '!' : ' ');
    } else if (c < 0x80) {
      out += static_cast<char>(c);
    }
  }
  return out;
}

void menuOrigin(const Ui& ui, const Menu& m, float& left, float& top) {
  left = std::floor((ui.guiWidth() - m.width()) / 2.0f);
  top = std::floor((ui.guiHeight() - m.height()) / 2.0f);
}

}  // namespace

void drawStackOverlay(Ui& ui, const ItemStack& s, float x, float y) {
  if (s.empty()) return;
  if (s.isTool() && s.meta > 0) {
    const float frac = 1.0f - static_cast<float>(s.meta) / itemInfo(s.id).maxDurability;
    const int w = static_cast<int>(std::round(13.0f * frac));
    const u32 color = 0xFF000000 | (static_cast<u32>(255 * (1 - frac)) << 16) | (static_cast<u32>(255 * frac) << 8);
    ui.rect(x + 2, y + 13, 13, 2, 0xFF000000);
    ui.rect(x + 2, y + 13, static_cast<float>(w), 1, color);
  }
  if (s.count > 1) {
    const std::string n = std::to_string(s.count);
    ui.text(x + 17 - ui.textWidth(n), y + 9, n, 0xFFFFFF);
  }
}

void drawHud(Ui& ui, ItemRenderer& items, const Player& p, float nameAlpha) {
  const float w = static_cast<float>(ui.guiWidth()), h = static_cast<float>(ui.guiHeight());
  const float left = std::floor(w / 2 - 91);
  ui.sprite("gui/widgets.png", left, h - 22, 182, 22, 0, 0);
  ui.sprite("gui/widgets.png", left - 1 + p.inventory.selectedIndex() * 20, h - 23, 24, 24, 0, 22);

  if (!p.creative()) {
    // Experiencia (todavía sin puntos: barra vacía)
    ui.sprite("gui/icons.png", left, h - 29, 182, 5, 0, 64);
    // Vida
    const bool flash = p.hurtTime > 0 && (p.hurtTime / 3) % 2 == 1;
    const int hp = static_cast<int>(std::ceil(p.health));
    for (int i = 0; i < 10; i++) {
      const float x = left + i * 8;
      float y = h - 39;
      if (hp <= 4) y += static_cast<float>((i * 7 + static_cast<int>(p.pos.x * 13)) % 3 - 1);  // tiembla con poca vida
      ui.sprite("gui/icons.png", x, y, 9, 9, flash ? 25.0f : 16.0f, 0);
      if (i * 2 + 1 < hp) ui.sprite("gui/icons.png", x, y, 9, 9, 52, 0);
      else if (i * 2 + 1 == hp) ui.sprite("gui/icons.png", x, y, 9, 9, 61, 0);
    }
    // Comida
    for (int i = 0; i < 10; i++) {
      const float x = left + 182 - i * 8 - 9, y = h - 39;
      ui.sprite("gui/icons.png", x, y, 9, 9, 16, 27);
      if (i * 2 + 1 < p.food) ui.sprite("gui/icons.png", x, y, 9, 9, 52, 27);
      else if (i * 2 + 1 == p.food) ui.sprite("gui/icons.png", x, y, 9, 9, 61, 27);
    }
    // Armadura: encima de la vida, un icono por cada dos puntos
    if (const int armor = p.inventory.armorPoints(); armor > 0)
      for (int i = 0; i < 10; i++) {
        const float x = left + i * 8, y = h - 49;
        ui.sprite("gui/icons.png", x, y, 9, 9, i * 2 + 1 < armor ? 34.0f : (i * 2 + 1 == armor ? 25.0f : 16.0f), 9);
      }
    // Aire bajo el agua
    if (p.headInWater || p.air < 300) {
      const int full = static_cast<int>(std::ceil((p.air - 2) * 10.0 / 300.0));
      const int popping = static_cast<int>(std::ceil(p.air * 10.0 / 300.0)) - full;
      for (int i = 0; i < full + popping; i++)
        ui.sprite("gui/icons.png", left + 182 - i * 8 - 9, h - 49, 9, 9, i < full ? 16.0f : 25.0f, 18);
    }
  }
  ui.flush();
  for (int i = 0; i < 9; i++) items.queueIcon(p.inventory.slot(i), left + 3 + i * 20, h - 19);
  items.flushIcons(ui.screenWidth(), ui.screenHeight(), ui.scale());
  for (int i = 0; i < 9; i++) drawStackOverlay(ui, p.inventory.slot(i), left + 3 + i * 20, h - 19);

  // Nombre del objeto al cambiar de casilla
  const ItemStack& sel = p.inventory.selected();
  if (nameAlpha > 0 && !sel.empty()) {
    const std::string name = ascii(itemDisplayNameEs(sel.id, sel.meta));
    const u32 a = static_cast<u32>(std::clamp(nameAlpha, 0.0f, 1.0f) * 255);
    if (a > 8) ui.textCentered(w / 2, h - (p.creative() ? 36 : 49), name, 0xFFFFFF | (a << 24));
  }
}

int menuSlotAt(const Ui& ui, const Menu& m, float x, float y, bool& inside) {
  float left, top;
  menuOrigin(ui, m, left, top);
  inside = x >= left && y >= top && x < left + m.width() && y < top + m.height();
  const auto& slots = m.slots();
  for (int i = 0; i < static_cast<int>(slots.size()); i++) {
    const float sx = left + slots[i].x - 1, sy = top + slots[i].y - 1;
    if (x >= sx && y >= sy && x < sx + 18 && y < sy + 18) return i;
  }
  return -1;
}

int drawMenu(Ui& ui, ItemRenderer& items, const Menu& m, const Player& p, float mx, float my,
             const std::function<void(float, float)>& preview) {
  float left, top;
  menuOrigin(ui, m, left, top);
  // Fondo oscurecido y ventana
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xA0101010);
  if (m.kind() == MenuKind::Chest) {
    // Parte de arriba con 3 filas y el inventario de la ventana de 6 filas
    ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), 71, 0, 0, static_cast<float>(m.width()), 71);
    ui.sprite(m.texture(), left, top + 71, static_cast<float>(m.width()), 96, 0, 126, static_cast<float>(m.width()), 96);
  } else {
    ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), static_cast<float>(m.height()), 0, 0);
  }

  // Horno: llama y flecha de progreso
  if (m.kind() == MenuKind::Furnace && m.furnace()) {
    const FurnaceState& f = *m.furnace();
    if (f.burning() && f.burnTotal > 0) {
      const int k = f.burnTime * 13 / f.burnTotal;
      ui.sprite(m.texture(), left + 56, top + 36 + 12 - k, 14, static_cast<float>(k + 1), 176, static_cast<float>(12 - k), 14,
                static_cast<float>(k + 1));
    }
    const int prog = f.cookTime * 24 / FurnaceState::kCookTicks;
    ui.sprite(m.texture(), left + 79, top + 34, static_cast<float>(prog + 1), 16, 176, 14, static_cast<float>(prog + 1), 16);
  }

  // Títulos
  const u32 titleColor = 0x404040;
  switch (m.kind()) {
    case MenuKind::Inventory: ui.text(left + 86, top + 6, ascii(menuTitle(m.kind())), titleColor, false); break;
    case MenuKind::Creative:
      ui.text(left + 8, top + 6, ascii(menuTitle(m.kind())), titleColor, false);
      ui.text(left + 8, top + 128, "Inventario", titleColor, false);
      break;
    default:
      ui.textCentered(left + m.width() / 2.0f, top + 6, ascii(menuTitle(m.kind())), titleColor, false);
      ui.text(left + 8, top + m.height() - 94, "Inventario", titleColor, false);
      break;
  }
  // Barra de desplazamiento del creativo
  if (m.kind() == MenuKind::Creative && m.maxScroll() > 0) {
    ui.rect(left + m.width() + 2, top + 18, 6, 108, 0xFF8B8B8B);
    const float t = static_cast<float>(m.scrollRow()) / m.maxScroll();
    ui.rect(left + m.width() + 2, top + 18 + t * 93, 6, 15, 0xFFE0E0E0);
  }

  if (preview) {
    ui.flush();
    preview(left, top);
  }

  bool inside = false;
  const int hover = menuSlotAt(ui, m, mx, my, inside);
  ui.flush();
  for (const MenuSlot& s : m.slots()) items.queueIcon(*s.stack, left + s.x, top + s.y);
  items.flushIcons(ui.screenWidth(), ui.screenHeight(), ui.scale());
  for (const MenuSlot& s : m.slots()) drawStackOverlay(ui, *s.stack, left + s.x, top + s.y);
  if (hover >= 0) {
    const MenuSlot& s = m.slots()[hover];
    ui.rect(left + s.x, top + s.y, 16, 16, 0x80FFFFFF);
  }

  // Lo que se lleva en el cursor
  if (!p.cursor.empty()) {
    ui.flush();
    items.queueIcon(p.cursor, mx - 8, my - 8);
    items.flushIcons(ui.screenWidth(), ui.screenHeight(), ui.scale());
    drawStackOverlay(ui, p.cursor, mx - 8, my - 8);
  } else if (hover >= 0 && !m.slots()[hover].stack->empty()) {
    // Nombre del objeto
    const ItemStack& s = *m.slots()[hover].stack;
    std::string name = ascii(itemDisplayNameEs(s.id, s.meta));
    if (s.isTool()) name += std::format(" ({}/{})", itemInfo(s.id).maxDurability - s.meta, itemInfo(s.id).maxDurability);
    const float tw = static_cast<float>(ui.textWidth(name));
    const float tx = std::min(mx + 12, ui.guiWidth() - tw - 4), ty = my - 12;
    ui.rect(tx - 3, ty - 3, tw + 6, 14, 0xF0100010);
    ui.rect(tx - 3, ty - 3, tw + 6, 1, 0xFF5000FF);
    ui.rect(tx - 3, ty + 10, tw + 6, 1, 0xFF28007F);
    ui.text(tx, ty, name, 0xFFFFFF);
  }
  return hover;
}

std::string asciiText(std::string_view s) { return ascii(s); }

std::vector<std::string> wrapText(const Ui& ui, std::string_view text, float maxWidth) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flushWord = [&] {
    if (word.empty()) return;
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && ui.textWidth(candidate) > maxWidth) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ' || c == '\n') {
      flushWord();
      if (c == '\n') {
        lines.push_back(line);
        line.clear();
      }
    } else {
      word += c;
    }
  }
  flushWord();
  if (!line.empty() || lines.empty()) lines.push_back(line);
  return lines;
}

void drawButtons(Ui& ui, const std::vector<MenuButton>& buttons, float mx, float my) {
  for (const MenuButton& b : buttons) ui.button(b.x, b.y, b.w, ascii(b.label), mx, my, b.enabled);
}

int buttonAt(const std::vector<MenuButton>& buttons, float x, float y) {
  for (const MenuButton& b : buttons)
    if (b.enabled && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + 20) return b.id;
  return -1;
}

void TextField::backspace() {
  if (text.empty()) return;
  std::size_t n = text.size() - 1;
  while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) n--;  // continuación UTF-8
  text.resize(n);
}

void TextField::insert(std::string_view s) {
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t') continue;
    if (text.size() >= maxLength) break;
    text += c;
  }
}

void drawTextField(Ui& ui, const TextField& f, double time, std::string_view placeholder) {
  ui.rect(f.x - 1, f.y - 1, f.w + 2, f.h + 2, f.focused ? 0xFFFFFFFF : 0xFFA0A0A0);
  ui.rect(f.x, f.y, f.w, f.h, 0xFF000000);
  std::string shown = ascii(f.text);
  // Si no cabe, se ve el final (donde se escribe)
  while (!shown.empty() && ui.textWidth(shown) > f.w - 10) shown.erase(shown.begin());
  const float ty = f.y + (f.h - 8) / 2;
  if (shown.empty() && !f.focused && !placeholder.empty()) ui.text(f.x + 4, ty, ascii(placeholder), 0x707070, false);
  ui.text(f.x + 4, ty, shown, 0xE0E0E0);
  if (f.focused && static_cast<long>(time * 1000 / 400) % 2 == 0) ui.text(f.x + 4 + ui.textWidth(shown), ty, "_", 0xE0E0E0);
}

void drawMenuBackground(Ui& ui, float y0, float y1, u32 tint) {
  const float w = static_cast<float>(ui.guiWidth());
  if (y1 < 0) y1 = static_cast<float>(ui.guiHeight());
  if (ui.hasTexture("gui/options_background.png")) ui.tiled("gui/options_background.png", 0, y0, w, y1 - y0, 32, tint);
  else ui.rect(0, y0, w, y1 - y0, 0xFF1E1812);
}

std::vector<MenuButton> pauseButtons(const Ui& ui, bool lanOpen, bool canQuitGame) {
  // Como el de 1.8: volver; logros | estadísticas; ajustes | abrir en LAN; guardar y salir
  const float cx = std::floor(ui.guiWidth() / 2.0f), top = std::floor(ui.guiHeight() / 4.0f + 8);
  std::vector<MenuButton> b;
  b.push_back({kPauseResume, cx - 100, top + 24, 200, "Volver al juego"});
  b.push_back({kPauseAchievements, cx - 100, top + 48, 98, "Logros"});
  b.push_back({kPauseStats, cx + 2, top + 48, 98, "Estadísticas"});
  b.push_back({kPauseOptions, cx - 100, top + 72, 98, "Ajustes..."});
  b.push_back({kPauseLan, cx + 2, top + 72, 98, lanOpen ? "LAN abierta" : "Abrir en LAN", !lanOpen});
  b.push_back({kPauseQuit, cx - 100, top + 96, 200, "Guardar y salir al título"});
  if (canQuitGame) b.push_back({kPauseMode, cx - 100, top + 120, 200, "Cambiar modo (creativo/supervivencia)"});
  return b;
}

void drawPauseMenu(Ui& ui, const std::vector<MenuButton>& buttons, float mx, float my) {
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xA0101010);
  ui.textCentered(ui.guiWidth() / 2.0f, std::floor(ui.guiHeight() / 4.0f + 8), "Menu del juego", 0xFFFFFF);
  drawButtons(ui, buttons, mx, my);
}

namespace {
constexpr float kRow = 24.0f;
}  // namespace

OptionListLayout layoutOptionList(const Ui& ui, const std::vector<OptionItem>& items, float scroll) {
  OptionListLayout l;
  const float cx = std::floor(ui.guiWidth() / 2.0f);
  l.top = 30.0f;
  l.doneY = static_cast<float>(ui.guiHeight()) - 27.0f;
  l.doneX = cx - 100;
  l.bottom = l.doneY - 6.0f;
  // Colocar en filas: dos columnas de 150, o la fila entera
  struct Placed { int item; int row; int col; bool wide; };
  std::vector<Placed> placed;
  int row = 0, col = 0;
  for (int i = 0; i < static_cast<int>(items.size()); i++) {
    const OptionItem& it = items[i];
    const bool wide = it.wide || it.type == OptionItem::Type::Header;
    if (wide) {
      if (col == 1) { row++; col = 0; }
      placed.push_back({i, row, 0, true});
      row++;
    } else {
      placed.push_back({i, row, col, false});
      if (col == 1) { row++; col = 0; } else { col = 1; }
    }
  }
  if (col == 1) row++;
  const float content = row * kRow;
  l.maxScroll = std::max(0.0f, content - (l.bottom - l.top));
  scroll = std::clamp(scroll, 0.0f, l.maxScroll);
  l.scroll = scroll;
  for (const Placed& p : placed) {
    const float y = l.top + p.row * kRow - scroll;
    if (y < l.top - 0.5f || y + 20 > l.bottom + 0.5f) continue;  // solo los que caben enteros
    l.rects.push_back({p.item, p.wide ? cx - 155 : cx - 155 + p.col * 160.0f, y, p.wide ? 310.0f : 150.0f});
  }
  return l;
}

int optionListHit(const OptionListLayout& l, const std::vector<OptionItem>& items, float x, float y) {
  if (x >= l.doneX && x < l.doneX + 200 && y >= l.doneY && y < l.doneY + 20) return -2;
  for (const auto& r : l.rects) {
    if (items[r.item].type == OptionItem::Type::Header) continue;
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + 20) return r.item;
  }
  return -1;
}

float optionSliderAt(const OptionListLayout& l, int item, float x) {
  for (const auto& r : l.rects)
    if (r.item == item) return std::clamp((x - (r.x + 4)) / (r.w - 8), 0.0f, 1.0f);
  return 0.0f;
}

void drawOptionList(Ui& ui, const std::string& title, const std::vector<OptionItem>& items, const OptionListLayout& l, float mx,
                    float my, const std::string& doneLabel) {
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xB0101010);
  ui.textCentered(ui.guiWidth() / 2.0f, 12, ascii(title), 0xFFFFFF);
  for (const auto& r : l.rects) {
    const OptionItem& it = items[r.item];
    const std::string label = ascii(it.text ? it.text() : std::string());
    switch (it.type) {
      case OptionItem::Type::Header:
        ui.textCentered(r.x + r.w / 2, r.y + 8, label, 0xFFE070);
        break;
      case OptionItem::Type::Button: ui.button(r.x, r.y, r.w, label, mx, my, it.enabled); break;
      case OptionItem::Type::Slider: {
        // Fondo de botón apagado y un tirador de 8 píxeles, como en 1.8
        const bool hover = mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + 20;
        ui.sprite("gui/widgets.png", r.x, r.y, r.w / 2, 20, 0, 46, r.w / 2, 20);
        ui.sprite("gui/widgets.png", r.x + r.w / 2, r.y, r.w / 2, 20, 200 - r.w / 2, 46, r.w / 2, 20);
        const float v = it.get ? std::clamp(it.get(), 0.0f, 1.0f) : 0.0f;
        const float hx = std::floor(r.x + v * (r.w - 8));
        ui.sprite("gui/widgets.png", hx, r.y, 4, 20, 0, 66, 4, 20);
        ui.sprite("gui/widgets.png", hx + 4, r.y, 4, 20, 196, 66, 4, 20);
        ui.textCentered(r.x + r.w / 2, r.y + 6, label, hover ? 0xFFFFA0 : 0xE0E0E0);
        break;
      }
    }
  }
  // Barra de desplazamiento si no cabe todo
  if (l.maxScroll > 0) {
    const float x = std::floor(ui.guiWidth() / 2.0f) + 160;
    const float h = l.bottom - l.top;
    ui.rect(x, l.top, 4, h, 0x80000000);
    const float thumb = std::max(12.0f, h * h / (h + l.maxScroll));
    ui.rect(x, l.top + (h - thumb) * (l.scroll / l.maxScroll), 4, thumb, 0xFFC0C0C0);
  }
  ui.button(l.doneX, l.doneY, 200, ascii(doneLabel), mx, my);
}

bool deathButtonAt(const Ui& ui, float x, float y) {
  const float bx = std::floor(ui.guiWidth() / 2.0f - 100), by = std::floor(ui.guiHeight() / 4.0f + 72);
  return x >= bx && x < bx + 200 && y >= by && y < by + 20;
}

bool drawDeathScreen(Ui& ui, float mx, float my) {
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0x80701010);
  const float cx = ui.guiWidth() / 2.0f, top = std::floor(ui.guiHeight() / 4.0f);
  ui.textCentered(cx, top + 20, "Has muerto!", 0xFFFFFF);
  ui.button(std::floor(cx - 100), std::floor(top + 72), 200, "Reaparecer", mx, my);
  return deathButtonAt(ui, mx, my);
}

}  // namespace mcw
