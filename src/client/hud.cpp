#include "client/hud.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/vec2.hpp>

#include "client/item_renderer.h"
#include "client/ui.h"
#include "game/effects.h"
#include "game/enchantments.h"
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
    case MenuKind::Enchant: return "Encantar";
    case MenuKind::Anvil: return "Reparar y nombrar";
    case MenuKind::Hopper: return "Tolva";
    case MenuKind::Dispenser: return "Dispensador";
    case MenuKind::Dropper: return "Soltador";
  }
  return {};
}

/// La fuente del juego ya tiene ñ, tildes, ¿ y ¡: el texto llega a la interfaz tal cual (UTF-8) y ella lo traduce.
std::string ascii(std::string_view s) { return std::string(s); }

void menuOrigin(const Ui& ui, const Menu& m, float& left, float& top) {
  left = std::floor((ui.guiWidth() - m.width()) / 2.0f);
  top = std::floor((ui.guiHeight() - m.height()) / 2.0f);
  // Las pestañas del creativo sobresalen 28 píxeles por arriba: que no se salgan de la pantalla
  if (m.kind() == MenuKind::Creative) top = std::max(top, 28.0f);
}

// --- Ventana del modo creativo (195x136) ---
// Las pestañas (28x32) van arriba y abajo, metidas 4 píxeles bajo la ventana; la rejilla de objetos (9x5) empieza en
// (9, 18); la barra de desplazamiento está en (175, 18); el campo de búsqueda, en (80, 4).
constexpr float kTabW = 28, kTabH = 32, kTabOverlap = 4;
constexpr float kBarX = 175, kBarY = 18, kBarW = 12, kBarH = 112, kHandleH = 15, kBarTravel = kBarH - kHandleH - 2;
constexpr float kSearchX = 80, kSearchY = 4, kSearchW = 93, kSearchH = 13;
constexpr const char* kTabsTexture = "gui/container/creative_inventory/tabs.png";

/// Esquina de una pestaña respecto a la ventana: las de la derecha del todo sobresalen 2 píxeles del borde.
glm::vec2 creativeTabPos(const Menu& m, CreativeTab t) {
  const float x = creativeTabRightmost(t) ? static_cast<float>(m.width()) - 26.0f : static_cast<float>(creativeTabColumn(t) * 29);
  const float y = creativeTabOnTop(t) ? -(kTabH - kTabOverlap) : static_cast<float>(m.height()) - kTabOverlap;
  return {x, y};
}

void drawCreativeTab(Ui& ui, const Menu& m, CreativeTab t, bool selected, float left, float top) {
  const glm::vec2 p = creativeTabPos(m, t);
  const float u = kTabW * static_cast<float>(creativeTabColumn(t));
  const float v = (creativeTabOnTop(t) ? 0.0f : 64.0f) + (selected ? 32.0f : 0.0f);
  ui.sprite(kTabsTexture, left + p.x, top + p.y, kTabW, kTabH, u, v);
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
    // Experiencia: la barra (vacía y llena hasta donde toque) y el nivel encima en verde
    ui.sprite("gui/icons.png", left, h - 29, 182, 5, 0, 64);
    if (const int fill = static_cast<int>(p.xpProgress * 183.0f); fill > 0)
      ui.sprite("gui/icons.png", left, h - 29, static_cast<float>(std::min(fill, 182)), 5, 0, 69, static_cast<float>(std::min(fill, 182)), 5);
    if (p.xpLevel > 0) {
      const std::string level = std::to_string(p.xpLevel);
      const float lx = w / 2 - static_cast<float>(ui.textWidth(level)) / 2, ly = h - 35;
      for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) ui.text(lx + dx, ly + dy, level, 0x000000, false);
      ui.text(lx, ly, level, 0x80FF20, false);
    }
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
    // Corazones dorados de la absorción, encima de la armadura
    if (p.absorption > 0) {
      const int gold = static_cast<int>(std::ceil(p.absorption));
      for (int i = 0; i < 10; i++) {
        const float x = left + i * 8, y = h - 59;
        if (i * 2 + 1 < gold) ui.sprite("gui/icons.png", x, y, 9, 9, 160, 0);
        else if (i * 2 + 1 == gold) ui.sprite("gui/icons.png", x, y, 9, 9, 169, 0);
      }
    }
    // Aire bajo el agua
    if (p.headInWater || p.air < 300) {
      const int full = static_cast<int>(std::ceil((p.air - 2) * 10.0 / 300.0));
      const int popping = static_cast<int>(std::ceil(p.air * 10.0 / 300.0)) - full;
      for (int i = 0; i < full + popping; i++)
        ui.sprite("gui/icons.png", left + 182 - i * 8 - 9, h - 49, 9, 9, i < full ? 16.0f : 25.0f, 18);
    }
  }
  // Efectos activos: un cuadro de su color por cada uno arriba a la derecha, con lo que les queda
  {
    float y = 4;
    for (const ActiveEffect& e : p.effects.list) {
      const bool blink = e.ticks < 200 && (e.ticks / 5) % 2 == 0;
      const u32 col = 0xFF000000 | fx::color(e.id);
      ui.rect(w - 28, y, 24, 24, fx::harmful(e.id) ? 0xA0602020 : 0xA0203040);
      ui.rect(w - 25, y + 3, 18, 18, blink ? (0x60000000 | (col & 0xFFFFFF)) : col);
      const std::string letter = std::string(1, fx::name(e.id)[0]);
      ui.textCentered(w - 16, y + 8, letter, 0xFFFFFF, true);
      ui.text(w - 28 - static_cast<float>(ui.textWidth(fx::duration(e.ticks))) - 3, y + 8, fx::duration(e.ticks), 0xE0E0E0, true);
      y += 26;
    }
  }
  ui.flush();
  for (int i = 0; i < 9; i++) items.queueIcon(p.inventory.slot(i), left + 3 + i * 20, h - 19);
  items.flushIcons(ui.screenWidth(), ui.screenHeight(), ui.scale());
  for (int i = 0; i < 9; i++) drawStackOverlay(ui, p.inventory.slot(i), left + 3 + i * 20, h - 19);

  // Nombre del objeto al cambiar de casilla
  const ItemStack& sel = p.inventory.selected();
  if (nameAlpha > 0 && !sel.empty()) {
    const std::string name = ascii(itemName(sel.id, sel.meta));
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

glm::vec2 menuOriginGui(const Ui& ui, const Menu& m) {
  float left, top;
  menuOrigin(ui, m, left, top);
  return {left, top};
}

glm::vec2 creativeTabCenterGui(const Ui& ui, const Menu& m, CreativeTab t) {
  const glm::vec2 o = menuOriginGui(ui, m), p = creativeTabPos(m, t);
  return {o.x + p.x + kTabW / 2.0f, o.y + p.y + (creativeTabOnTop(t) ? 0.0f : kTabOverlap) + (kTabH - kTabOverlap) / 2.0f};
}

int creativeTabAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Creative) return -1;
  float left, top;
  menuOrigin(ui, m, left, top);
  for (int i = 0; i < kCreativeTabCount; i++) {
    const auto t = static_cast<CreativeTab>(i);
    const glm::vec2 p = creativeTabPos(m, t);
    // (la parte que se mete bajo la ventana ya es ventana)
    const float y0 = top + p.y + (creativeTabOnTop(t) ? 0.0f : kTabOverlap);
    if (x >= left + p.x && x < left + p.x + kTabW && y >= y0 && y < y0 + (kTabH - kTabOverlap)) return i;
  }
  return -1;
}

bool creativeScrollbarAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Creative || m.creativeTab() == CreativeTab::Inventory || m.maxScroll() <= 0) return false;
  float left, top;
  menuOrigin(ui, m, left, top);
  return x >= left + kBarX && x < left + kBarX + kBarW && y >= top + kBarY && y < top + kBarY + kBarH;
}

int creativeScrollRowAt(const Ui& ui, const Menu& m, float y) {
  float left, top;
  menuOrigin(ui, m, left, top);
  const float t = std::clamp((y - (top + kBarY) - kHandleH / 2.0f) / kBarTravel, 0.0f, 1.0f);
  return static_cast<int>(std::lround(t * static_cast<float>(m.maxScroll())));
}

bool creativeGridAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Creative || m.creativeTab() == CreativeTab::Inventory) return false;
  float left, top;
  menuOrigin(ui, m, left, top);
  return x >= left + 8 && x < left + 8 + 9 * 18 + 2 && y >= top + 17 && y < top + 17 + 5 * 18 + 2;
}

bool anvilNameFieldAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Anvil) return false;
  const glm::vec2 o = menuOriginGui(ui, m);
  return x >= o.x + 59 && x < o.x + 59 + 110 && y >= o.y + 20 && y < o.y + 20 + 16;
}

bool creativeSearchFieldAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Creative || m.creativeTab() != CreativeTab::Search) return false;
  float left, top;
  menuOrigin(ui, m, left, top);
  return x >= left + kSearchX && x < left + kSearchX + kSearchW && y >= top + kSearchY && y < top + kSearchY + kSearchH;
}

void drawItemTooltip(Ui& ui, const ItemStack& s, float mx, float my) {
  // Líneas: el nombre (de color según lo raro que sea), los encantamientos en gris, la descripción y el desgaste
  std::vector<std::pair<std::string, u32>> lines;
  const bool book = s.id == ItemId::enchanted_book;
  const bool enchanted = s.hasEnchants();
  u32 nameColor = 0xFFFFFF;
  if (enchanted) nameColor = 0x55FFFF;                              // encantado: azul claro
  else if (book && s.extra && !s.extra->stored.empty()) nameColor = 0xFFFF55;  // libro con encantamientos: amarillo
  else if (s.id == ItemId::golden_apple && s.meta == 1) nameColor = 0xFF55FF;
  std::string name = s.extra && !s.extra->name.empty() ? s.extra->name : itemName(s.id, s.meta);
  lines.emplace_back(ascii(name), nameColor);
  if (s.extra) {
    for (const auto& [id, lvl] : s.extra->ench) lines.emplace_back(ascii(enchantDisplayName(id, lvl)), 0xAAAAAA);
    for (const auto& [id, lvl] : s.extra->stored) lines.emplace_back(ascii(enchantDisplayName(id, lvl)), 0xAAAAAA);
    for (const std::string& l : s.extra->lore) lines.emplace_back(ascii(l), 0xAA00AA);
  }
  if (s.isTool()) lines.emplace_back(std::format("Durabilidad: {}/{}", itemInfo(s.id).maxDurability - s.meta, itemInfo(s.id).maxDurability), 0xAAAAAA);
  drawTooltip(ui, lines, mx, my);
}

void drawTooltip(Ui& ui, const std::vector<std::pair<std::string, u32>>& lines, float mx, float my) {
  if (lines.empty()) return;
  float tw = 0;
  for (const auto& [text, color] : lines) tw = std::max(tw, static_cast<float>(ui.textWidth(text)));
  const float th = 10.0f + 10.0f * static_cast<float>(lines.size() - 1) + (lines.size() > 1 ? 2.0f : 0.0f);
  const float tx = std::min(mx + 12, ui.guiWidth() - tw - 4), ty = std::clamp(my - 12, 6.0f, std::max(6.0f, ui.guiHeight() - th - 8.0f));
  ui.rect(tx - 3, ty - 3, tw + 6, th + 4, 0xF0100010);
  ui.rect(tx - 3, ty - 3, tw + 6, 1, 0xFF5000FF);
  ui.rect(tx - 3, ty + th, tw + 6, 1, 0xFF28007F);
  float y = ty;
  for (std::size_t i = 0; i < lines.size(); i++) {
    ui.text(tx, y, lines[i].first, lines[i].second);
    y += i == 0 ? 12.0f : 10.0f;
  }
}

namespace {

// Las tres opciones de la mesa: cajas de 108x19 a partir de (60, 14), una debajo de otra (como el dibujo de la ventana)
constexpr float kEnchX = 60, kEnchY = 14, kEnchW = 108, kEnchH = 19;

/// Palabras inventadas con las que se rellena la escritura rúnica de cada opción (no significan nada).
const char* const kRuneWords[] = {"ankor", "vel", "uzmir", "tholo", "kesh", "raun", "ilvu", "orsa", "namek", "drel", "quor",
                                  "zeph", "mura", "olkan", "feth", "yrin", "gael", "sovu", "bril", "tamo", "ekis", "hov",
                                  "lunar", "praxe", "ondu", "withe", "carzak", "melu", "tyr", "abasco", "ven", "ixo"};

/// Hasta dos líneas de runas que caben en `maxWidth`, siempre las mismas para la misma semilla y opción.
std::vector<std::string> runeLines(const Ui& ui, i32 seed, int option, float maxWidth) {
  u32 state = static_cast<u32>(seed) * 2654435761u + static_cast<u32>(option + 1) * 40503u + 12345u;
  auto next = [&] {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };
  std::vector<std::string> lines;
  std::string line;
  for (int guard = 0; guard < 24 && lines.size() < 2; guard++) {
    const std::string word = kRuneWords[next() % (sizeof(kRuneWords) / sizeof(kRuneWords[0]))];
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (ui.runeWidth(candidate) > maxWidth) {
      if (line.empty()) break;
      lines.push_back(line);
      line = ui.runeWidth(word) > maxWidth ? std::string() : word;
    } else {
      line = candidate;
    }
  }
  if (lines.size() < 2 && !line.empty()) lines.push_back(line);
  return lines;
}

/// Las tres opciones con su icono de nivel, las runas y el coste a la derecha (sin el recuadro de la pista).
constexpr float kAnvilFieldX = 59, kAnvilFieldY = 20, kAnvilFieldW = 110, kAnvilFieldH = 16;

/// El campo del nombre, la X de "no se puede" y lo que cuesta, sobre el dibujo de la ventana del yunque.
void drawAnvilExtras(Ui& ui, const Menu& m, const Player& p, float left, float top) {
  const bool hasItem = !m.anvilSlot(0).empty();
  ui.sprite(m.texture(), left + kAnvilFieldX, top + kAnvilFieldY, kAnvilFieldW, kAnvilFieldH, 0, hasItem ? 166.0f : 182.0f, kAnvilFieldW, kAnvilFieldH);
  if ((hasItem || !m.anvilSlot(1).empty()) && m.anvilOutput().empty()) ui.sprite(m.texture(), left + 99, top + 45, 28, 21, 176, 0, 28, 21);
  if (hasItem) {
    std::string shown = ascii(m.anvilName());
    while (!shown.empty() && ui.textWidth(shown) > kAnvilFieldW - 10) shown.erase(shown.begin());  // si no cabe, se ve el final
    ui.text(left + kAnvilFieldX + 4, top + kAnvilFieldY + 4, shown, 0xE0E0E0);
    if ((SDL_GetTicks() / 400) % 2 == 0) ui.text(left + kAnvilFieldX + 4 + static_cast<float>(ui.textWidth(shown)), top + kAnvilFieldY + 4, "_", 0xE0E0E0);
  }
  // Lo que cuesta: verde si se puede sacar, rojo si faltan niveles; y "demasiado caro" a partir de 40
  if (m.anvilTooExpensive() && !p.creative()) {
    const std::string text = "Demasiado caro!";
    const float x = left + m.width() - 8 - static_cast<float>(ui.textWidth(text));
    ui.rect(x - 3, top + 67, static_cast<float>(ui.textWidth(text)) + 6, 12, 0x4F000000);
    ui.text(x, top + 69, text, 0xFF6060);
  } else if (!m.anvilOutput().empty()) {
    const std::string text = "Coste de encantamiento: " + std::to_string(m.anvilCost());
    const float x = left + m.width() - 8 - static_cast<float>(ui.textWidth(text));
    ui.rect(x - 3, top + 67, static_cast<float>(ui.textWidth(text)) + 6, 12, 0x4F000000);
    ui.text(x, top + 69, text, m.anvilCanTake() ? 0x80FF20 : 0xFF6060);
  }
}

void drawEnchantOptions(Ui& ui, const Menu& m, const Player& p, float left, float top, float mx, float my) {
  const int hover = enchantOptionAt(ui, m, mx, my);
  for (int i = 0; i < 3; i++) {
    const EnchantOffer& o = m.offers()[static_cast<std::size_t>(i)];
    const float bx = left + kEnchX, by = top + kEnchY + kEnchH * static_cast<float>(i);
    if (o.cost <= 0) {
      ui.sprite(m.texture(), bx, by, kEnchW, kEnchH, 0, 185);  // sin opción
      continue;
    }
    const bool affordable = m.canEnchant(i);
    const bool hovered = affordable && hover == i;
    ui.sprite(m.texture(), bx, by, kEnchW, kEnchH, 0, affordable ? (hovered ? 204.0f : 166.0f) : 185.0f);
    ui.sprite(m.texture(), bx + 1, by + 1, 16, 16, static_cast<float>(16 * i), affordable ? 223.0f : 239.0f);
    const std::string level = std::to_string(o.cost);
    const float runeWidth = 86.0f - static_cast<float>(ui.textWidth(level));
    u32 runeColor = affordable ? (hovered ? 0xFFFF80u : 0x685E4Au) : 0x342F25u;
    float ty = by + 2;
    for (const std::string& line : runeLines(ui, m.runeSeed(), i, runeWidth)) {
      ui.runeText(bx + 20, ty, line, runeColor);
      ty += 9;
    }
    ui.text(bx + 106 - static_cast<float>(ui.textWidth(level)), by + 9, level, affordable ? 0x80FF20 : 0x407F10);
  }
  (void)p;
}

}  // namespace

int enchantOptionAt(const Ui& ui, const Menu& m, float x, float y) {
  if (m.kind() != MenuKind::Enchant) return -1;
  float left, top;
  menuOrigin(ui, m, left, top);
  for (int i = 0; i < 3; i++) {
    const float bx = left + kEnchX, by = top + kEnchY + kEnchH * static_cast<float>(i);
    if (x >= bx && y >= by && x < bx + kEnchW && y < by + kEnchH) return i;
  }
  return -1;
}

std::vector<std::pair<std::string, u32>> enchantOptionTooltip(const Menu& m, const Player& p, int option) {
  std::vector<std::pair<std::string, u32>> lines;
  if (option < 0 || option > 2) return lines;
  const EnchantOffer& o = m.offers()[static_cast<std::size_t>(option)];
  if (o.cost <= 0) return lines;
  constexpr u32 kRed = 0xFF5555, kGray = 0xAAAAAA;
  const bool clue = o.clueEnchant >= 0 && enchantInfo(o.clueEnchant);
  if (clue) lines.emplace_back(std::string(enchantInfo(o.clueEnchant)->nameEs) + " . . . ?", 0xFFFFFF);
  if (!p.creative()) {
    if (clue) lines.emplace_back("", 0xFFFFFF);
    const int need = option + 1;
    if (p.xpLevel < o.cost) {
      lines.emplace_back(std::format("Nivel requerido: {}", o.cost), kRed);
    } else {
      const ItemStack& lapis = m.enchantSlot(1);
      const bool haveLapis = !lapis.empty() && lapis.count >= need;
      lines.emplace_back(need == 1 ? "1 lapislázuli" : std::format("{} lapislázuli", need), haveLapis ? kGray : kRed);
      lines.emplace_back(need == 1 ? "1 nivel de encantamiento" : std::format("{} niveles de encantamiento", need),
                         p.xpLevel >= need ? kGray : kRed);
    }
  }
  for (auto& l : lines) l.first = ascii(l.first);
  return lines;
}

int drawMenu(Ui& ui, ItemRenderer& items, const Menu& m, const Player& p, float mx, float my,
             const std::function<void(float, float)>& preview) {
  float left, top;
  menuOrigin(ui, m, left, top);
  // Fondo oscurecido y ventana
  ui.rect(0, 0, static_cast<float>(ui.guiWidth()), static_cast<float>(ui.guiHeight()), 0xA0101010);
  const bool creative = m.kind() == MenuKind::Creative;
  if (creative) {
    // Las pestañas sin elegir van detrás de la ventana, y la elegida delante (tapa el borde y se une a ella)
    for (int i = 0; i < kCreativeTabCount; i++)
      if (static_cast<CreativeTab>(i) != m.creativeTab()) drawCreativeTab(ui, m, static_cast<CreativeTab>(i), false, left, top);
    ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), static_cast<float>(m.height()), 0, 0);
    drawCreativeTab(ui, m, m.creativeTab(), true, left, top);
  } else if (m.kind() == MenuKind::Chest) {
    // Parte de arriba con las filas que haya (3 o 6) y el inventario de la ventana de 6 filas
    const float topH = static_cast<float>(m.height() - 96);
    ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), topH, 0, 0, static_cast<float>(m.width()), topH);
    ui.sprite(m.texture(), left, top + topH, static_cast<float>(m.width()), 96, 0, 126, static_cast<float>(m.width()), 96);
  } else {
    ui.sprite(m.texture(), left, top, static_cast<float>(m.width()), static_cast<float>(m.height()), 0, 0);
  }

  // Mesa de encantamientos: las tres opciones sobre el dibujo de la ventana
  if (m.kind() == MenuKind::Enchant) drawEnchantOptions(ui, m, p, left, top, mx, my);
  if (m.kind() == MenuKind::Anvil) drawAnvilExtras(ui, m, p, left, top);

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

  // Atril de pociones: la flecha y las burbujas del tiempo que falta
  if (m.kind() == MenuKind::Brewing && m.brewTime() > 0) {
    const int bt = m.brewTime();
    const int arrow = static_cast<int>(28.0f * (1.0f - static_cast<float>(bt) / 400.0f));
    if (arrow > 0) ui.sprite(m.texture(), left + 97, top + 16, 9, static_cast<float>(arrow), 176, 0, 9, static_cast<float>(arrow));
    static const int kBubbles[7] = {29, 24, 20, 16, 11, 6, 0};
    if (const int i = kBubbles[(bt / 2) % 7]; i > 0) ui.sprite(m.texture(), left + 63, top + 14 + 29 - static_cast<float>(i), 12, static_cast<float>(i), 185, static_cast<float>(29 - i), 12, static_cast<float>(i));
  }

  // Títulos
  const u32 titleColor = 0x404040;
  switch (m.kind()) {
    case MenuKind::Inventory: ui.text(left + 86, top + 6, ascii(menuTitle(m.kind())), titleColor, false); break;
    case MenuKind::Creative: {
      // El título es el nombre de la pestaña; en la búsqueda se escribe en el campo y en el inventario no hay título
      if (m.creativeTab() == CreativeTab::Search) {
        ui.text(left + 8, top + 6, "Buscar", titleColor, false);
        std::string shown = ascii(m.searchText());
        while (!shown.empty() && ui.textWidth(shown) > kSearchW - 8) shown.erase(shown.begin());  // si no cabe, se ve el final
        const bool caret = (SDL_GetTicks() / 400) % 2 == 0;
        if (shown.empty()) ui.text(left + kSearchX + 9, top + 6, "Escribe...", 0x707070, false);
        else ui.text(left + kSearchX + 3, top + 6, shown, 0xE0E0E0);
        if (caret) ui.text(left + kSearchX + 3 + static_cast<float>(ui.textWidth(shown)), top + 6, "_", 0xE0E0E0);
      } else if (m.creativeTab() != CreativeTab::Inventory) {
        ui.text(left + 8, top + 6, ascii(creativeTabName(m.creativeTab())), titleColor, false);
      }
      break;
    }
    case MenuKind::Anvil:  // (sin la palabra "Inventario": ahí va el coste)
      ui.text(left + 60, top + 6, ascii(menuTitle(m.kind())), titleColor, false);
      break;
    case MenuKind::Enchant:  // el título a la izquierda, como en la ventana de 1.8
      ui.text(left + 12, top + 5, ascii(menuTitle(m.kind())), titleColor, false);
      ui.text(left + 8, top + m.height() - 94, "Inventario", titleColor, false);
      break;
    case MenuKind::Brewing:
      ui.textCentered(left + m.width() / 2.0f, top + 6, ascii(m.title().empty() ? menuTitle(m.kind()) : m.title()), titleColor, false);
      ui.text(left + 8, top + m.height() - 94, "Inventario", titleColor, false);
      break;
    case MenuKind::Chest: case MenuKind::Hopper: case MenuKind::Dispenser: case MenuKind::Dropper:  // el título a la izquierda
      ui.text(left + 8, top + 6, ascii(m.title().empty() ? menuTitle(m.kind()) : m.title()), titleColor, false);
      ui.text(left + 8, top + m.height() - 94, "Inventario", titleColor, false);
      break;
    default:
      ui.textCentered(left + m.width() / 2.0f, top + 6, ascii(menuTitle(m.kind())), titleColor, false);
      ui.text(left + 8, top + m.height() - 94, "Inventario", titleColor, false);
      break;
  }
  // Barra de desplazamiento del creativo (apagada si cabe todo)
  if (creative && m.creativeTab() != CreativeTab::Inventory) {
    const bool scrollable = m.maxScroll() > 0;
    const float t = scrollable ? static_cast<float>(m.scrollRow()) / static_cast<float>(m.maxScroll()) : 0.0f;
    ui.sprite(kTabsTexture, left + kBarX, top + kBarY + std::round(t * kBarTravel), kBarW, kHandleH, scrollable ? 232.0f : 244.0f, 0);
  }

  if (preview) {
    ui.flush();
    preview(left, top);
  }

  bool inside = false;
  const int hover = menuSlotAt(ui, m, mx, my, inside);
  ui.flush();
  for (const MenuSlot& s : m.slots()) items.queueIcon(*s.stack, left + s.x, top + s.y);
  if (creative)
    for (int i = 0; i < kCreativeTabCount; i++) {
      const auto t = static_cast<CreativeTab>(i);
      const glm::vec2 p = creativeTabPos(m, t);
      items.queueIcon(creativeTabIcon(t), left + p.x + 6, top + p.y + 8 + (creativeTabOnTop(t) ? 1.0f : -1.0f));
    }
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
    drawItemTooltip(ui, *m.slots()[hover].stack, mx, my);
  } else if (m.kind() == MenuKind::Enchant) {
    drawTooltip(ui, enchantOptionTooltip(m, p, enchantOptionAt(ui, m, mx, my)), mx, my);
  } else if (creative) {
    if (const int tab = creativeTabAt(ui, m, mx, my); tab >= 0)
      drawTooltip(ui, {{ascii(creativeTabName(static_cast<CreativeTab>(tab))), 0xFFFFFF}}, mx, my);
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
