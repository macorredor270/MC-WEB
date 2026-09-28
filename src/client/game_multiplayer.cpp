// Pantalla Multijugador: servidores guardados (servers.dat, como en 1.8) con su ping, partidas de la
// LAN, conexión directa y añadir/editar/borrar servidores. El nombre del jugador y, en la web, el
// proxy WebSocket se cambian aquí mismo.
#include <algorithm>
#include <cmath>
#include <format>

#include "client/audio.h"
#include "client/game.h"
#include "client/ui.h"
#include "core/fs.h"
#include "core/log.h"
#include "save/nbt.h"

namespace mcw {
namespace {

enum MpId {
  kMpJoin = 80, kMpDirect, kMpAdd, kMpEdit, kMpDelete, kMpRefresh, kMpBack,
  kAddDone = 90, kAddCancel,
  kDirectJoin = 95, kDirectCancel,
};

constexpr float kServerEntryH = 36.0f;
constexpr float kListTop = 58.0f;

std::filesystem::path serversFile() { return fs::userDataDir() / "servers.dat"; }

// Quita los códigos de color (§x): la fuente del menú los pintaría tal cual
std::string plain(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (static_cast<u8>(s[i]) == 0xC2 && i + 1 < s.size() && static_cast<u8>(s[i + 1]) == 0xA7) {
      i += 2;  // § en UTF-8 + el código
      continue;
    }
    if (s[i] == '\n') out += ' ';
    else out += s[i];
  }
  return out;
}

std::unique_ptr<net::Transport> openTransport(const std::string& proxy, const std::string& host, int port) {
  return net::tcpAvailable() ? net::connectTcp(host, port) : net::connectViaProxy(proxy, host, port);
}

}  // namespace

void Game::loadServerList() {
  servers_.clear();
  auto data = fs::readFile(serversFile());
  if (!data) return;
  auto root = nbt::read(*data);
  if (!root) return;
  if (const nbt::Value* list = root->getList("servers"))
    for (const nbt::Value& s : list->items()) {
      ServerEntry e;
      e.name = s.getString("name", "Servidor de Minecraft");
      e.address = s.getString("ip");
      if (!e.address.empty()) servers_.push_back(std::move(e));
    }
}

void Game::saveServerList() const {
  nbt::Value root = nbt::Value::compound();
  nbt::Value list = nbt::Value::list(nbt::Tag::Compound);
  for (const ServerEntry& e : servers_) {
    nbt::Value s = nbt::Value::compound();
    s.set("name", nbt::Value::string(e.name));
    s.set("ip", nbt::Value::string(e.address));
    list.push(std::move(s));
  }
  root.set("servers", std::move(list));
  const std::vector<u8> bytes = nbt::write(root);
  std::error_code ec;
  std::filesystem::create_directories(serversFile().parent_path(), ec);
  if (!fs::writeFile(serversFile(), bytes.data(), bytes.size())) log::warn("no se pudo guardar {}", serversFile().string());
}

void Game::pingServers() {
  for (ServerEntry& e : servers_) {
    std::string host;
    int port = 25565;
    net::splitAddress(e.address, host, port);
    e.status.reset();
    e.error.clear();
    e.pinger = host.empty() ? nullptr : std::make_unique<net::StatusPinger>(openTransport(settings_.proxyUrl, host, port), host, port);
    if (!e.pinger) e.error = "Direccion no valida";
  }
}

void Game::pollServerPings() {
  for (ServerEntry& e : servers_) {
    if (!e.pinger || !e.pinger->poll(runTime_)) continue;
    if (e.pinger->ok()) e.status = e.pinger->status();
    else if (e.pinger->error().empty() || e.pinger->error().rfind("No se pudo conectar", 0) == 0) e.error = "No se puede conectar al servidor";
    else e.error = e.pinger->error();
    e.pinger.reset();
  }
  if (lanListener_) lanListener_->poll(runTime_);
}

void Game::openMultiplayer() {
  loadServerList();
  if (!lanListener_) lanListener_ = std::make_unique<net::LanListener>();
  selectedServer_ = servers_.empty() ? -1 : std::clamp(selectedServer_, 0, static_cast<int>(servers_.size()) - 1);
  playerNameField_ = {};
  playerNameField_.text = settings_.playerName;
  playerNameField_.maxLength = 16;
  proxyField_ = {};
  proxyField_.text = settings_.proxyUrl;
  proxyField_.maxLength = 128;
  pingServers();
}

std::string Game::selectedServerAddress() const {
  const int n = static_cast<int>(servers_.size());
  if (selectedServer_ >= 0 && selectedServer_ < n) return servers_[selectedServer_].address;
  if (lanListener_ && selectedServer_ >= n && selectedServer_ < n + static_cast<int>(lanListener_->games().size()))
    return lanListener_->games()[selectedServer_ - n].address;
  return {};
}

TextField* Game::focusedMultiplayerField() {
  for (TextField* f : {&playerNameField_, &proxyField_, &serverNameField_, &serverAddrField_, &directField_})
    if (f->focused) return f;
  return nullptr;
}

std::vector<MenuButton> Game::multiplayerButtons() const {
  std::vector<MenuButton> b;
  const float cx = std::floor(ui_->guiWidth() / 2.0f), h = static_cast<float>(ui_->guiHeight());
  switch (screen_) {
    case Screen::Multiplayer: {
      const bool sel = !selectedServerAddress().empty();
      const bool saved = selectedServer_ >= 0 && selectedServer_ < static_cast<int>(servers_.size());
      b.push_back({kMpJoin, cx - 154, h - 52, 100, "Entrar", sel});
      b.push_back({kMpDirect, cx - 50, h - 52, 100, "Conexion directa"});
      b.push_back({kMpAdd, cx + 54, h - 52, 100, "Anadir servidor"});
      b.push_back({kMpEdit, cx - 154, h - 28, 70, "Editar", saved});
      b.push_back({kMpDelete, cx - 80, h - 28, 70, "Borrar", saved});
      b.push_back({kMpRefresh, cx + 4, h - 28, 70, "Refrescar"});
      b.push_back({kMpBack, cx + 78, h - 28, 76, "Volver"});
      break;
    }
    case Screen::AddServer:
      b.push_back({kAddDone, cx - 100, h / 4 + 96, 200, "Listo", !serverAddrField_.text.empty()});
      b.push_back({kAddCancel, cx - 100, h / 4 + 120, 200, "Cancelar"});
      break;
    case Screen::DirectConnect:
      b.push_back({kDirectJoin, cx - 100, h / 4 + 96, 200, "Entrar al servidor", !directField_.text.empty()});
      b.push_back({kDirectCancel, cx - 100, h / 4 + 120, 200, "Cancelar"});
      break;
    default: break;
  }
  return b;
}

void Game::drawMultiplayer(glm::vec2 m) {
  const float cx = std::floor(ui_->guiWidth() / 2.0f), gh = static_cast<float>(ui_->guiHeight());
  if (screen_ == Screen::AddServer || screen_ == Screen::DirectConnect) {
    const float y0 = std::floor(gh / 4);
    if (screen_ == Screen::AddServer) {
      ui_->textCentered(cx, 17, editingServer_ >= 0 ? "Editar servidor" : "Anadir servidor", 0xFFFFFF);
      serverNameField_.x = serverAddrField_.x = cx - 100;
      serverNameField_.w = serverAddrField_.w = 200;
      serverNameField_.y = y0 + 6;
      serverAddrField_.y = y0 + 46;
      ui_->text(cx - 100, y0 - 4, "Nombre del servidor", 0xA0A0A0);
      drawTextField(*ui_, serverNameField_, runTime_, "Servidor de Minecraft");
      ui_->text(cx - 100, y0 + 36, "Direccion del servidor", 0xA0A0A0);
      drawTextField(*ui_, serverAddrField_, runTime_, "ip:puerto");
    } else {
      ui_->textCentered(cx, 17, "Conexion directa", 0xFFFFFF);
      directField_.x = cx - 100;
      directField_.w = 200;
      directField_.y = y0 + 46;
      ui_->text(cx - 100, y0 + 36, "Direccion del servidor", 0xA0A0A0);
      drawTextField(*ui_, directField_, runTime_, "ip:puerto");
    }
    return;
  }

  pollServerPings();
  ui_->textCentered(cx, 6, "Multijugador", 0xFFFFFF);
  // Nombre del jugador (modo offline) y, sin TCP (web), el proxy WebSocket
  const bool web = !net::tcpAvailable();
  playerNameField_.x = cx - 154;
  playerNameField_.w = web ? 110 : 308;
  playerNameField_.y = 28;
  proxyField_.x = cx - 40;
  proxyField_.w = 194;
  proxyField_.y = 28;
  ui_->text(cx - 154, 18, "Tu nombre", 0xA0A0A0);
  drawTextField(*ui_, playerNameField_, runTime_, "Jugador");
  if (web) {
    ui_->text(cx - 40, 18, "Proxy (mcweb-wsproxy)", 0xA0A0A0);
    drawTextField(*ui_, proxyField_, runTime_, "ws://localhost:25500");
  }

  const float top = kListTop, bottom = gh - 60;
  ui_->rect(0, top, static_cast<float>(ui_->guiWidth()), bottom - top, 0xC0000000);
  const std::vector<net::LanGame> lan = lanListener_ ? lanListener_->games() : std::vector<net::LanGame>{};
  const int nServers = static_cast<int>(servers_.size());
  const int rows = nServers + 1 + std::max<int>(1, static_cast<int>(lan.size()));  // + cabecera de la LAN
  const float maxScroll = std::max(0.0f, rows * kServerEntryH - (bottom - top - 4));
  serverScroll_ = std::clamp(serverScroll_, 0.0f, maxScroll);
  const float x = cx - 150;
  auto entryFrame = [&](int index, float y) {
    if (index == selectedServer_) {
      ui_->rect(x - 2, y - 2, 304, kServerEntryH, 0xFF808080);
      ui_->rect(x - 1, y - 1, 302, kServerEntryH - 2, 0xFF000000);
    } else if (m.x >= x && m.x < x + 300 && m.y >= y && m.y < y + kServerEntryH - 4 && m.y >= top && m.y < bottom) {
      ui_->rect(x - 1, y - 1, 302, kServerEntryH - 2, 0x40FFFFFF);
    }
  };
  for (int i = 0; i < nServers; i++) {
    const float y = top + 4 + i * kServerEntryH - serverScroll_;
    if (y < top || y + kServerEntryH - 4 > bottom) continue;
    const ServerEntry& e = servers_[i];
    entryFrame(i, y);
    ui_->text(x + 2, y + 1, asciiText(e.name), 0xFFFFFF);
    std::string right;
    u32 rightColor = 0x808080;
    if (e.status) {
      const int p = e.status->pingMs;
      right = p >= 0 ? std::format("{} ms", p) : "?";
      rightColor = p < 0 ? 0x808080 : p < 150 ? 0x55FF55 : p < 400 ? 0xFFFF55 : 0xFF5555;
      const std::string players = std::format("{}/{}", e.status->online, e.status->max);
      ui_->text(x + 298 - ui_->textWidth(players), y + 12, players, 0x808080);
      ui_->text(x + 2, y + 12, asciiText(plain(e.status->motd)).substr(0, 44), 0xA0A0A0);
      if (e.status->protocol != net::kProtocolVersion)
        ui_->text(x + 2, y + 23, asciiText("Version distinta: " + plain(e.status->version)), 0xFF5555);
      else
        ui_->text(x + 2, y + 23, asciiText(e.address), 0x606060);
    } else if (!e.error.empty()) {
      right = "X";
      rightColor = 0xFF5555;
      ui_->text(x + 2, y + 12, asciiText(e.error).substr(0, 48), 0xFF5555);
      ui_->text(x + 2, y + 23, asciiText(e.address), 0x606060);
    } else {
      static const char* dots[] = {"o..", ".o.", "..o"};
      right = dots[static_cast<int>(runTime_ * 4) % 3];
      ui_->text(x + 2, y + 12, "Comprobando...", 0x808080);
      ui_->text(x + 2, y + 23, asciiText(e.address), 0x606060);
    }
    ui_->text(x + 298 - ui_->textWidth(right), y + 1, right, rightColor);
  }
  // Partidas de la LAN (anuncios multicast, como "Abrir en LAN" de 1.8)
  float y = top + 4 + nServers * kServerEntryH - serverScroll_;
  auto visible = [&](float yy) { return yy >= top && yy + kServerEntryH - 4 <= bottom; };
  if (visible(y)) ui_->textCentered(cx, y + 12, "Partidas en tu red local", 0xFFFFFF);
  y += kServerEntryH;
  if (lan.empty() && visible(y)) {
    static const char* dots[] = {"o..", ".o.", "..o"};
    ui_->textCentered(cx, y + 4, web ? "La web no puede buscar partidas LAN: usa la direccion" : "Buscando partidas LAN...", 0x808080);
    if (!web) ui_->textCentered(cx, y + 16, dots[static_cast<int>(runTime_ * 4) % 3], 0x808080);
  }
  for (int i = 0; i < static_cast<int>(lan.size()); i++, y += kServerEntryH) {
    if (!visible(y)) continue;
    entryFrame(nServers + i, y);
    ui_->text(x + 2, y + 1, "Partida LAN", 0xFFFFFF);
    ui_->text(x + 2, y + 12, asciiText(plain(lan[i].motd)).substr(0, 48), 0xA0A0A0);
    ui_->text(x + 2, y + 23, lan[i].address, 0x606060);
  }
  if (maxScroll > 0) {
    const float lh = bottom - top, thumb = std::max(12.0f, lh * lh / (lh + maxScroll));
    ui_->rect(cx + 156, top, 4, lh, 0x80000000);
    ui_->rect(cx + 156, top + (lh - thumb) * (serverScroll_ / maxScroll), 4, thumb, 0xFFC0C0C0);
  }
}

void Game::multiplayerButton(int id) {
  auto commitFields = [&] {
    std::string name = playerNameField_.text;
    // Nombres de 1.8: 3 a 16 caracteres, letras, números y _
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_'); }),
               name.end());
    if (name.size() >= 3 && name != settings_.playerName) {
      settings_.playerName = name;
      saveSettings();
    }
    playerNameField_.text = settings_.playerName;
    const std::string& px = proxyField_.text;
    if ((px.rfind("ws://", 0) == 0 || px.rfind("wss://", 0) == 0) && px != settings_.proxyUrl) {
      settings_.proxyUrl = proxyField_.text;
      saveSettings();
    }
    proxyField_.text = settings_.proxyUrl;
  };
  switch (id) {
    case kMpJoin: {
      const std::string address = selectedServerAddress();
      if (address.empty()) break;
      commitFields();
      connectToServer(address);
      break;
    }
    case kMpDirect:
      commitFields();
      directField_.maxLength = 128;
      directField_.focused = true;
      directField_.text = settings_.lastServer;
      openScreen(Screen::DirectConnect);
      break;
    case kMpAdd:
    case kMpEdit: {
      const bool edit = id == kMpEdit && selectedServer_ >= 0 && selectedServer_ < static_cast<int>(servers_.size());
      if (id == kMpEdit && !edit) break;
      commitFields();
      editingServer_ = edit ? selectedServer_ : -1;
      serverNameField_ = {};
      serverNameField_.maxLength = 32;
      serverNameField_.text = edit ? servers_[selectedServer_].name : "Servidor de Minecraft";
      serverAddrField_ = {};
      serverAddrField_.maxLength = 128;
      serverAddrField_.text = edit ? servers_[selectedServer_].address : "";
      serverAddrField_.focused = true;
      openScreen(Screen::AddServer);
      break;
    }
    case kMpDelete:
      if (selectedServer_ >= 0 && selectedServer_ < static_cast<int>(servers_.size())) {
        servers_.erase(servers_.begin() + selectedServer_);
        saveServerList();
        selectedServer_ = std::min(selectedServer_, static_cast<int>(servers_.size()) - 1);
      }
      break;
    case kMpRefresh:
      commitFields();
      pingServers();
      break;
    case kMpBack:
      commitFields();
      lanListener_.reset();
      servers_.clear();
      openScreen(Screen::Title);
      break;
    case kAddDone: {
      if (serverAddrField_.text.empty()) break;
      loadServerList();  // la lista de disco (sin pings a medias)
      ServerEntry e;
      e.name = serverNameField_.text.empty() ? "Servidor de Minecraft" : serverNameField_.text;
      e.address = serverAddrField_.text;
      if (editingServer_ >= 0 && editingServer_ < static_cast<int>(servers_.size())) {
        servers_[editingServer_] = std::move(e);
        selectedServer_ = editingServer_;
      } else {
        servers_.push_back(std::move(e));
        selectedServer_ = static_cast<int>(servers_.size()) - 1;
      }
      saveServerList();
      openScreen(Screen::Multiplayer);
      break;
    }
    case kAddCancel:
    case kDirectCancel: openScreen(Screen::Multiplayer); break;
    case kDirectJoin:
      if (directField_.text.empty()) break;
      settings_.lastServer = directField_.text;
      saveSettings();
      connectToServer(directField_.text);
      break;
    default: break;
  }
}

void Game::multiplayerPress(glm::vec2 gui) {
  for (TextField* f : {&playerNameField_, &proxyField_, &serverNameField_, &serverAddrField_, &directField_}) f->focused = false;
  switch (screen_) {
    case Screen::AddServer:
      if (serverNameField_.contains(gui.x, gui.y)) serverNameField_.focused = true;
      else serverAddrField_.focused = true;
      return;
    case Screen::DirectConnect: directField_.focused = true; return;
    case Screen::Multiplayer: break;
    default: return;
  }
  if (playerNameField_.contains(gui.x, gui.y)) {
    playerNameField_.focused = true;
    return;
  }
  if (!net::tcpAvailable() && proxyField_.contains(gui.x, gui.y)) {
    proxyField_.focused = true;
    return;
  }
  const float cx = std::floor(ui_->guiWidth() / 2.0f), bottom = static_cast<float>(ui_->guiHeight()) - 60;
  if (gui.y < kListTop || gui.y > bottom || gui.x < cx - 152 || gui.x > cx + 152) return;
  const int nServers = static_cast<int>(servers_.size());
  int row = static_cast<int>((gui.y - kListTop - 4 + serverScroll_) / kServerEntryH);
  if (row == nServers) return;  // cabecera de la LAN
  if (row > nServers) row--;
  const int nLan = lanListener_ ? static_cast<int>(lanListener_->games().size()) : 0;
  if (row < 0 || row >= nServers + nLan) return;
  const u64 now = SDL_GetTicksNS();
  if (row == selectedServer_ && now - lastServerClick_ < 400'000'000ull) {
    multiplayerButton(kMpJoin);  // doble clic: entrar
    return;
  }
  selectedServer_ = row;
  lastServerClick_ = now;
  audio_->playFlat(Sfx::Click);
}

bool Game::multiplayerKey(SDL_Scancode sc) {
  if (screen_ != Screen::Multiplayer && screen_ != Screen::AddServer && screen_ != Screen::DirectConnect) return false;
  TextField* field = focusedMultiplayerField();
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      multiplayerButton(screen_ == Screen::Multiplayer ? kMpBack : kAddCancel);
      return true;
    case SDL_SCANCODE_BACKSPACE:
      if (field) field->backspace();
      return true;
    case SDL_SCANCODE_TAB:
      if (screen_ == Screen::AddServer) {
        const bool n = serverNameField_.focused;
        serverNameField_.focused = !n;
        serverAddrField_.focused = n;
      }
      return true;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
      if (screen_ == Screen::AddServer) multiplayerButton(kAddDone);
      else if (screen_ == Screen::DirectConnect) multiplayerButton(kDirectJoin);
      else if (field) field->focused = false;
      else multiplayerButton(kMpJoin);
      return true;
    case SDL_SCANCODE_UP:
      if (screen_ == Screen::Multiplayer && !field && selectedServer_ > 0) selectedServer_--;
      return true;
    case SDL_SCANCODE_DOWN: {
      const int total = static_cast<int>(servers_.size()) + (lanListener_ ? static_cast<int>(lanListener_->games().size()) : 0);
      if (screen_ == Screen::Multiplayer && !field && selectedServer_ + 1 < total) selectedServer_++;
      return true;
    }
    case SDL_SCANCODE_DELETE:
      if (screen_ == Screen::Multiplayer && !field) multiplayerButton(kMpDelete);
      return true;
    default: return true;
  }
}

void Game::multiplayerText(std::string_view text) {
  if (TextField* f = focusedMultiplayerField()) f->insert(text);
}

}  // namespace mcw
