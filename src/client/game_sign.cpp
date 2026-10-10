// Editor del texto de un cartel recién colocado (4 líneas de hasta 15 caracteres, como en 1.8).
#include "client/game.h"
#include "client/hud.h"
#include "client/ui.h"

namespace mcw {

void Game::openSignEditor(const glm::ivec3& pos) {
  signPos_ = pos;
  signLines_ = {};
  signLine_ = 0;
  openScreen(Screen::SignEdit);
}

void Game::finishSignEditor() {
  SignText t;
  t.lines = signLines_;
  if (net_) netSendSign(signPos_, t);
  else session_->setSignText(signPos_, t);
  openScreen(Screen::None);
}

void Game::netSendSign(const glm::ivec3& pos, const SignText& t) {
  if (net_) net_->sendUpdateSign(pos, t.lines);
}

void Game::signText(std::string_view text) {
  std::string& line = signLines_[static_cast<std::size_t>(signLine_)];
  // Hasta 15 caracteres (letras, no bytes)
  auto chars = [](const std::string& s) {
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
  };
  for (std::size_t i = 0; i < text.size();) {
    std::size_t len = 1;
    while (i + len < text.size() && (static_cast<unsigned char>(text[i + len]) & 0xC0) == 0x80) len++;
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c >= 32 && c != 127 && chars(line) < 15) line.append(text.substr(i, len));
    i += len;
  }
}

void Game::signKey(SDL_Scancode sc) {
  std::string& line = signLines_[static_cast<std::size_t>(signLine_)];
  if (sc == SDL_SCANCODE_ESCAPE) {
    finishSignEditor();
  } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_DOWN) {
    if (sc != SDL_SCANCODE_DOWN && signLine_ == 3) finishSignEditor();
    else signLine_ = (signLine_ + 1) & 3;
  } else if (sc == SDL_SCANCODE_UP) {
    signLine_ = (signLine_ + 3) & 3;
  } else if (sc == SDL_SCANCODE_BACKSPACE && !line.empty()) {
    do line.pop_back();
    while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80);
  }
}

void Game::drawSignEditor(glm::vec2 m) {
  const float w = static_cast<float>(ui_->guiWidth()), h = static_cast<float>(ui_->guiHeight());
  ui_->rect(0, 0, w, h, 0xC0101010);
  ui_->textCentered(w / 2, h / 4 - 20, "Edita el texto del cartel", 0xFFFFFF);
  const float bw = 144, bh = 72, bx = std::floor(w / 2 - bw / 2), by = std::floor(h / 2 - bh / 2 - 10);
  ui_->rect(bx - 2, by - 2, bw + 4, bh + 4, 0xFF3A2A12);
  ui_->rect(bx, by, bw, bh, 0xFFA07C46);
  for (int i = 0; i < 4; i++) {
    std::string line = signLines_[static_cast<std::size_t>(i)];
    if (i == signLine_) {
      // La línea que se escribe va entre "> " y " <", con el cursor parpadeando
      if (std::fmod(runTime_, 1.0) < 0.5) line += "_";
      line = "> " + line + " <";
    }
    ui_->textCentered(w / 2, by + 8 + static_cast<float>(i) * 14, line, 0x000000, false);
  }
  const float cx = std::floor(w / 2);
  if (ui_->button(cx - 100, std::floor(h / 4 * 3 - 4), 200, "Hecho", m.x, m.y)) {}
}

}  // namespace mcw
