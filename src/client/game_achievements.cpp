// Logros en el cliente: aviso al conseguir uno, mapa de logros y estadísticas.
#include <algorithm>
#include <format>

#include "client/game.h"
#include "client/hud.h"
#include "client/item_renderer.h"
#include "client/ui.h"
#include "data/items.h"

namespace mcw {
namespace {

constexpr float kCell = 26.0f;  // separación entre logros en el mapa

std::string formatStat(const std::string& name, i64 v) {
  if (name.ends_with("OneCm")) return v >= 100000 ? std::format("{:.2f} km", v / 100000.0) : std::format("{:.1f} m", v / 100.0);
  if (name == "stat.playOneMinute" || name == "stat.timeSinceDeath") {
    const i64 s = v / 20;
    return s >= 3600 ? std::format("{}h {}min", s / 3600, (s / 60) % 60) : std::format("{} min {} s", s / 60, s % 60);
  }
  if (name == "stat.damageDealt") return std::format("{:.1f} corazones", v / 20.0);
  return std::to_string(v);
}

}  // namespace

const Achievements& Game::shownAchievements() const { return inWorld_ ? session_->achievements() : menuAchievements_; }

void Game::drawToasts() {
  // Como en 1.8: baja desde arriba a la derecha, se queda unos segundos y vuelve a subir
  if (toasts_.empty()) return;
  Toast& t = toasts_.front();
  if (t.start < 0) t.start = runTime_;  // cada aviso cuenta desde que se ve
  const double age = runTime_ - t.start;
  if (age > 5.0) {
    toasts_.erase(toasts_.begin());
    return;
  }
  const float slide = static_cast<float>(std::min({1.0, age * 3.0, (5.0 - age) * 3.0}));
  const float w = 160, h = 32;
  const float x = ui_->guiWidth() - w, y = -h + h * slide;
  const AchievementInfo& a = achievementInfo(t.achievement);
  ui_->rect(x, y, w, h, 0xF0202020);
  ui_->rect(x, y + h - 1, w, 1, 0xFF000000);
  ui_->rect(x, y, 1, h, 0xFF555555);
  ui_->text(x + 30, y + 7, asciiText("¡Logro conseguido!"), a.special ? 0xFF88FF : 0xFFFF00);
  ui_->text(x + 30, y + 18, asciiText(a.name), 0xFFFFFF);
  ui_->flush();
  itemRenderer_->queueIcon(ItemStack(a.iconId, 1, a.iconMeta), x + 8, y + 8);
  itemRenderer_->flushIcons(ui_->screenWidth(), ui_->screenHeight(), ui_->scale());
}

void Game::drawAchievementScreen(glm::vec2 m) {
  const Achievements& ach = shownAchievements();
  const float gw = static_cast<float>(ui_->guiWidth()), gh = static_cast<float>(ui_->guiHeight());
  ui_->rect(0, 0, gw, gh, 0xE0101010);
  const float cx = std::floor(gw / 2);
  if (achShowStats_) {
    // Estadísticas generales
    ui_->textCentered(cx, 10, "Estadisticas", 0xFFFFFF);
    static const std::pair<const char*, const char*> kGeneral[] = {
        {"stat.playOneMinute", "Tiempo jugado"},   {"stat.timeSinceDeath", "Desde la ultima muerte"},
        {"stat.walkOneCm", "Distancia andando"},   {"stat.sprintOneCm", "Distancia corriendo"},
        {"stat.crouchOneCm", "Distancia agachado"}, {"stat.swimOneCm", "Distancia nadando"},
        {"stat.flyOneCm", "Distancia volando"},    {"stat.jump", "Saltos"},
        {"stat.deaths", "Muertes"},                {"stat.mobKills", "Criaturas matadas"},
        {"stat.damageDealt", "Dano causado"},
    };
    float y = 30;
    for (const auto& [key, label] : kGeneral) {
      ui_->text(cx - 140, y, label, 0xC0C0C0, false);
      const std::string v = formatStat(key, ach.stat(key));
      ui_->text(cx + 140 - ui_->textWidth(v), y, v, 0xFFFFFF, false);
      y += 11;
    }
    ui_->textCentered(cx, y + 6, std::format("Logros: {}/{}", ach.count(), kAchievementCount), 0xFFFF60);
    drawButtons(*ui_, menuButtons(), m.x, m.y);
    return;
  }

  ui_->textCentered(cx, 6, std::format("Logros  {}/{}", ach.count(), kAchievementCount), 0xFFFFFF);
  if (!achWorldName_.empty() && !inWorld_) ui_->textCentered(cx, 16, asciiText("Mundo: " + achWorldName_), 0xA0A0A0);
  const float top = 26, bottom = gh - 48;
  // El mapa va de x -4..9 e y -5..13: se centra en x y se desplaza en y
  const float mapH = 19 * kCell;
  const float maxScroll = std::max(0.0f, mapH - (bottom - top));
  achScroll_.y = std::clamp(achScroll_.y, 0.0f, maxScroll);
  const float ox = cx - 2.5f * kCell, oy = top + 5 * kCell - achScroll_.y + 4;
  auto posOf = [&](int i) {
    const AchievementInfo& a = achievementInfo(i);
    return glm::vec2(ox + a.x * kCell, oy + a.y * kCell);
  };
  auto visible = [&](glm::vec2 p) { return p.y > top - 2 && p.y + 22 < bottom + 2; };
  // Líneas hacia el logro previo
  for (int i = 0; i < kAchievementCount; i++) {
    const int parent = achievementInfo(i).parent;
    if (parent < 0) continue;
    const glm::vec2 a = posOf(i) + 11.0f, b = posOf(parent) + 11.0f;
    const u32 c = ach.has(static_cast<Ach>(i)) ? 0xFF40A040 : (ach.canTake(static_cast<Ach>(i)) ? 0xFF808080 : 0xFF303030);
    const float y1 = std::clamp(a.y, top, bottom), y2 = std::clamp(b.y, top, bottom);
    if (a.y >= top && a.y <= bottom) ui_->rect(std::min(a.x, b.x), a.y - 1, std::abs(a.x - b.x) + 2, 2, c);
    ui_->rect(b.x - 1, std::min(y1, y2), 2, std::abs(y1 - y2), c);
  }
  int hover = -1;
  for (int i = 0; i < kAchievementCount; i++) {
    const glm::vec2 p = posOf(i);
    if (!visible(p)) continue;
    const Ach a = static_cast<Ach>(i);
    const bool got = ach.has(a), can = ach.canTake(a);
    const u32 frame = got ? (achievementInfo(i).special ? 0xFFC060FF : 0xFFFFD040) : (can ? 0xFF909090 : 0xFF404040);
    ui_->rect(p.x - 1, p.y - 1, 24, 24, frame);
    ui_->rect(p.x, p.y, 22, 22, got ? 0xFF3A3A3A : 0xFF202020);
    if (m.x >= p.x && m.x < p.x + 22 && m.y >= p.y && m.y < p.y + 22) hover = i;
  }
  ui_->flush();
  for (int i = 0; i < kAchievementCount; i++) {
    const glm::vec2 p = posOf(i);
    if (!visible(p)) continue;
    const AchievementInfo& a = achievementInfo(i);
    // Los que aún no se pueden conseguir se ven apagados (sin icono)
    if (ach.canTake(static_cast<Ach>(i)) || ach.has(static_cast<Ach>(i))) itemRenderer_->queueIcon(ItemStack(a.iconId, 1, a.iconMeta), p.x + 3, p.y + 3);
  }
  itemRenderer_->flushIcons(ui_->screenWidth(), ui_->screenHeight(), ui_->scale());
  // Descripción del que está debajo del ratón
  if (hover >= 0) {
    const AchievementInfo& a = achievementInfo(hover);
    const Ach id = static_cast<Ach>(hover);
    const std::string state = ach.has(id) ? "Conseguido" : (ach.canTake(id) ? "Disponible" : "Bloqueado: consigue antes \"" + std::string(achievementInfo(a.parent).name) + "\"");
    const std::string name = asciiText(a.name), desc = asciiText(a.desc), st = asciiText(state);
    const float w = std::max({ui_->textWidth(name), ui_->textWidth(desc), ui_->textWidth(st)}) + 8;
    const float x = std::min(m.x + 10, gw - w - 2), y = std::min(m.y - 4, bottom - 36);
    ui_->rect(x, y, w, 36, 0xF0100010);
    ui_->text(x + 4, y + 3, name, ach.has(id) ? (a.special ? 0xFF88FF : 0xFFFF60) : 0xFFFFFF);
    ui_->text(x + 4, y + 14, desc, 0xC0C0C0);
    ui_->text(x + 4, y + 25, st, ach.has(id) ? 0x80FF80 : 0x909090);
  }
  drawButtons(*ui_, menuButtons(), m.x, m.y);
}

}  // namespace mcw
