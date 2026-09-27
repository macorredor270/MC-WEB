#include "client/hud.h"

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
    const std::string name = ascii(itemDisplayName(sel.id, sel.meta));
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

int drawMenu(Ui& ui, ItemRenderer& items, const Menu& m, const Player& p, float mx, float my) {
  float left, top;
  menuOrigin(ui, m, left, top);
  // Fondo oscurecido y ventana
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xA0101010);
  ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), static_cast<float>(m.height()), 0, 0);

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
    std::string name = ascii(itemDisplayName(s.id, s.meta));
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

namespace {
float pauseTop(const Ui& ui) { return std::floor(ui.guiHeight() / 4.0f + 8); }
}  // namespace

int pauseButtonAt(const Ui& ui, float x, float y, bool canQuit) {
  const float bx = std::floor(ui.guiWidth() / 2.0f - 100), top = pauseTop(ui);
  const int n = canQuit ? 5 : 4;
  for (int i = 0; i < n; i++) {
    const float by = top + 24 + i * 24;
    if (x >= bx && x < bx + 200 && y >= by && y < by + 20) return i;
  }
  return -1;
}

int drawPauseMenu(Ui& ui, float mx, float my, bool creative, int renderDistance, bool canQuit) {
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xA0101010);
  const float bx = std::floor(ui.guiWidth() / 2.0f - 100), top = pauseTop(ui);
  ui.textCentered(ui.guiWidth() / 2.0f, top, "Menu del juego", 0xFFFFFF);
  const std::string labels[5] = {"Volver al juego", creative ? "Modo: Creativo" : "Modo: Supervivencia",
                                 std::format("Distancia de vision: {} chunks", renderDistance), "Avanzar la hora", "Salir del juego"};
  const int n = canQuit ? 5 : 4;
  for (int i = 0; i < n; i++) ui.button(bx, top + 24 + i * 24, 200, labels[i], mx, my);
  return pauseButtonAt(ui, mx, my, canQuit);
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
