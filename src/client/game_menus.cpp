// Menús fuera de la partida: pantalla de título, lista de mundos, crear/renombrar/borrar mundos.
// Se dibujan con la interfaz del propio juego (widgets.png y la fuente del pack), como en 1.8.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

#include "client/audio.h"
#include "client/game.h"
#include "client/ui.h"
#include "assets/pack.h"
#include "core/fs.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/random.h"

#include <nlohmann/json.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {
namespace {

// Textos de la pantalla de título: propios (los del juego original son de Mojang)
constexpr const char* kSplashes[] = {
    "Hecho desde cero!", "Codigo libre!", "Ahora con mundos guardados!", "En tu navegador!", "Con cerdos a su velocidad!",
    "Tambien en el movil!", "120 Hz si tu pantalla quiere!", "Bloques cuadrados, ideas redondas!", "Sala limpia!",
    "Sin Java!", "C++20!", "WebGL 2!", "Compila en Windows, Linux y web!", "Semillas de verdad!", "Pico, pala y a picar!",
    "Cuidado con los creepers!", "Sal a por madera!", "El diamante esta mas abajo!", "Hoy toca construir!",
    "Una mesa de trabajo cambia el mundo!", "Con musica generada al vuelo!", "Guardado automatico cada 45 s!",
    "Hardcore para valientes!", "Monta tu propio servidor!", "El nether espera...", "Mira hacia arriba de noche!",
    "Tambien funciona sin raton!", "Hecho con cariño y tests!", "Todo en espanol!", "MIT!",
};

enum MenuId {
  kTitleSingle = 1, kTitleMulti, kTitleSkins, kTitleAchievements, kTitleOptions, kTitleQuit, kTitlePacks, kTitleFullscreen,
  kWorldsPlay = 10, kWorldsCreate, kWorldsRename, kWorldsDelete, kWorldsRecreate, kWorldsCancel, kWorldsImport, kWorldsExport,
  kCreateMode = 20, kCreateDifficulty, kCreateStructures, kCreateType, kCreateCheats, kCreateBonus, kCreatePreset, kCreateGo,
  kCreateCancel,
  kRenameOk = 30, kRenameCancel,
  kDeleteOk = 40, kDeleteCancel,
  kBack = 50,
  kPacksDone = 60, kPacksOpenFolder, kPacksAdd,
  kAchToggle = 70,
};

constexpr float kPackEntryH = 26.0f;

#ifdef __EMSCRIPTEN__
// Web: elegir un .zip del dispositivo y guardarlo en /persist/resourcepacks (IndexedDB)
EM_JS(void, mcw_js_pick_pack, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.zip,application/zip';
  input.onchange = async () => {
    const f = input.files && input.files[0];
    if (!f) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    try {
      FS.mkdirTree('/persist/resourcepacks');
      FS.writeFile('/persist/resourcepacks/' + f.name.replace(/[\\/]/g, '_'), bytes);
      FS.syncfs(false, () => {});
    } catch (e) { console.warn('no se pudo guardar el pack', e); }
  };
  input.click();
});

// Web: elegir un mundo en .zip; se deja en /persist/imports y el juego lo importa solo
EM_JS(void, mcw_js_pick_world, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.zip,application/zip';
  input.onchange = async () => {
    const f = input.files && input.files[0];
    if (!f) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    try {
      FS.mkdirTree('/persist/imports');
      FS.writeFile('/persist/imports/' + f.name.replace(/[\\/]/g, '_'), bytes);
    } catch (e) { console.warn('no se pudo leer el mundo', e); }
  };
  input.click();
});

// Web: descargar unos bytes como archivo
EM_JS(void, mcw_js_download, (const char* name, const u8* data, int size), {
  const bytes = HEAPU8.slice(data, data + size);
  const blob = new Blob([bytes], {type: 'application/zip'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = UTF8ToString(name);
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
});
#endif

std::string formatDate(i64 ms) {
  if (ms <= 0) return "-";
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%d/%m/%y %H:%M", std::localtime(&t));
  return buf;
}

std::string formatSize(std::uintmax_t bytes) {
  if (bytes >= 1024 * 1024) return std::format("{:.1f} MB", bytes / 1048576.0);
  return std::format("{} KB", std::max<std::uintmax_t>(1, bytes / 1024));
}

constexpr float kEntryH = 36.0f;

}  // namespace

std::filesystem::path Game::resourcePackDir() {
  const std::filesystem::path p = fs::userDataDir() / "resourcepacks";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

void Game::refreshPackList() {
  // Cada .zip (o carpeta con pack.mcmeta) de la carpeta resourcepacks, con la descripción de su pack.mcmeta
  availablePacks_.clear();
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(resourcePackDir(), ec)) {
    const std::string name = e.path().filename().string();
    std::unique_ptr<Pack> pack;
    if (e.is_directory(ec)) pack = std::make_unique<DirPack>(e.path());
    else if (e.path().extension() == ".zip") pack = ZipPack::open(e.path());
    if (!pack) continue;
    std::string desc;
    if (auto meta = pack->read("pack.mcmeta")) {
      try {
        const auto j = nlohmann::json::parse(meta->begin(), meta->end());
        const auto& d = j.at("pack").at("description");
        desc = d.is_string() ? d.get<std::string>() : (d.contains("text") ? d["text"].get<std::string>() : "");
      } catch (...) {
      }
    } else if (!pack->exists("assets/minecraft/textures/blocks/stone.png") && !pack->exists("assets/minecraft/textures/gui/widgets.png")) {
      continue;  // no parece un pack de recursos
    }
    // Quitar códigos de formato (§x)
    std::string clean;
    for (std::size_t i = 0; i < desc.size(); i++) {
      if (static_cast<unsigned char>(desc[i]) == 0xC2 && i + 2 < desc.size() && static_cast<unsigned char>(desc[i + 1]) == 0xA7) {
        i += 2;
        continue;
      }
      if (desc[i] != '\n') clean += desc[i];
    }
    availablePacks_.push_back({name, clean});
  }
  std::sort(availablePacks_.begin(), availablePacks_.end(), [](const PackEntry& a, const PackEntry& b) { return a.file < b.file; });
  // Los activos que ya no existen se quitan
  std::erase_if(packSelection_, [&](const std::string& f) {
    return std::none_of(availablePacks_.begin(), availablePacks_.end(), [&](const PackEntry& p) { return p.file == f; });
  });
}

std::filesystem::path Game::worldImportDir() {
  const std::filesystem::path p = fs::userDataDir() / "imports";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

void Game::checkWorldImports() {
  // Los .zip que aparecen en la carpeta de importar (elegidos en el diálogo o copiados a mano)
  if (runTime_ - importCheck_ < 0.5) return;
  importCheck_ = runTime_;
  std::error_code ec;
  std::vector<std::filesystem::path> found;
  for (const auto& e : std::filesystem::directory_iterator(worldImportDir(), ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".zip") found.push_back(e.path());
  if (found.empty()) return;
  std::string last;
  int ok = 0, bad = 0;
  for (const auto& path : found) {
    auto data = fs::readFile(path);
    const std::string folder = data ? WorldSave::importZip(*data, path.stem().string()) : std::string();
    if (folder.empty()) bad++;
    else { ok++; last = folder; }
    std::filesystem::remove(path, ec);
  }
  WorldSave::flush();
  worlds_ = WorldSave::list();
  for (int i = 0; i < static_cast<int>(worlds_.size()); i++)
    if (worlds_[i].folder == last) selectedWorld_ = i;
  if (bad > 0) chatMessage("Algún .zip no tenía un mundo (falta level.dat)");
  messageBack_ = Screen::Worlds;
  if (ok > 0) {
    message_ = ok == 1 ? "Mundo importado" : std::format("{} mundos importados", ok);
    messageDetail_ = last;
  } else {
    message_ = "No se pudo importar";
    messageDetail_ = "El .zip no tiene level.dat";
  }
  openScreen(Screen::Message);
}

void Game::drawPackScreen(glm::vec2 m) {
  if (runTime_ - packRefresh_ > 1.0) {  // en web el pack llega por detrás al elegirlo
    packRefresh_ = runTime_;
    refreshPackList();
  }
  const float cx = std::floor(ui_->guiWidth() / 2.0f), top = 32, bottom = static_cast<float>(ui_->guiHeight()) - 56;
  ui_->textCentered(cx, 12, "Paquetes de recursos", 0xFFFFFF);
  ui_->textCentered(cx - 83, top - 10, "Disponibles", 0xC0C0C0);
  ui_->textCentered(cx + 83, top - 10, "Activos (el de arriba manda)", 0xC0C0C0);
  for (int side = 0; side < 2; side++) {
    const float x = side == 0 ? cx - 160 : cx + 6;
    ui_->rect(x, top, 154, bottom - top, 0xC0000000);
    std::vector<const PackEntry*> list;
    if (side == 0) {
      for (const PackEntry& p : availablePacks_)
        if (std::find(packSelection_.begin(), packSelection_.end(), p.file) == packSelection_.end()) list.push_back(&p);
    } else {
      for (const std::string& f : packSelection_)
        for (const PackEntry& p : availablePacks_)
          if (p.file == f) list.push_back(&p);
    }
    for (std::size_t i = 0; i < list.size(); i++) {
      const float y = top + 2 + i * kPackEntryH;
      if (y + kPackEntryH > bottom) break;
      const bool hover = m.x >= x && m.x < x + 154 && m.y >= y && m.y < y + kPackEntryH - 2;
      if (hover) ui_->rect(x + 1, y, 152, kPackEntryH - 2, 0x40FFFFFF);
      std::string name = asciiText(list[i]->file), desc = asciiText(list[i]->description);
      while (ui_->textWidth(name) > 148 && !name.empty()) name.pop_back();
      while (ui_->textWidth(desc) > 148 && !desc.empty()) desc.pop_back();
      ui_->text(x + 3, y + 2, name, 0xFFFFFF);
      ui_->text(x + 3, y + 13, desc, 0x808080);
      if (side == 1 && hover) ui_->text(x + 142, y + 7, "x", 0xFF8080);
    }
    if (list.empty())
      ui_->textCentered(x + 77, (top + bottom) / 2 - 4, side == 0 ? "(ninguno)" : "Solo el juego base", 0x808080);
  }
  ui_->textCentered(cx, bottom + 4, asciiText("Debajo van las texturas de tu jar y, al fondo, el pack libre."), 0x808080);
}

void Game::openScreen(Screen s) {
  if (s == Screen::ResourcePacks) {
    packSelection_ = settings_.resourcePacks;
    refreshPackList();
    packRefresh_ = runTime_;
  }
  if (s == Screen::Title && splash_.empty()) {
    Random r(static_cast<u64>(SDL_GetTicksNS()));
    splash_ = kSplashes[r.nextInt(static_cast<int>(std::size(kSplashes)))];
  }
  if (s == Screen::Multiplayer && screen_ != Screen::AddServer && screen_ != Screen::DirectConnect) openMultiplayer();
  const bool inSkins = s == Screen::Skins || s == Screen::SkinParts, wasInSkins = screen_ == Screen::Skins || screen_ == Screen::SkinParts;
  if (inSkins && !wasInSkins) openSkins();
  if (!inSkins && wasInSkins) closeSkins();
  if (s == Screen::Worlds) {
    worlds_ = WorldSave::list();
    selectedWorld_ = worlds_.empty() ? -1 : 0;
    worldScroll_ = 0;
  }
  if (s == Screen::CreateWorld) {
    nameField_ = {};
    nameField_.text = "Mundo nuevo";
    nameField_.focused = true;
    nameField_.maxLength = 32;
    seedField_ = {};
    seedField_.maxLength = 32;
  }
  if (s == Screen::RenameWorld && selectedWorld_ >= 0 && selectedWorld_ < static_cast<int>(worlds_.size())) {
    renameField_ = {};
    renameField_.text = worlds_[selectedWorld_].name;
    renameField_.focused = true;
  }
  const bool typing = s == Screen::CreateWorld || s == Screen::RenameWorld || s == Screen::Chat || s == Screen::SignEdit || s == Screen::Multiplayer ||
                      s == Screen::AddServer || s == Screen::DirectConnect;
  if (typing) SDL_StartTextInput(window_);
  else SDL_StopTextInput(window_);
  setScreen(s);
}

std::vector<MenuButton> Game::menuButtons() const {
  std::vector<MenuButton> b;
  const float cx = std::floor(ui_->guiWidth() / 2.0f), h = static_cast<float>(ui_->guiHeight());
  switch (screen_) {
    case Screen::Title: {
      // Que quepan los 5 botones y el texto de abajo en pantallas bajas (móvil apaisado)
      const float y = std::min(std::floor(h / 4.0f + 44), h - 14 - 5 * 24);
      b.push_back({kTitleSingle, cx - 100, y, 200, "Un jugador"});
      b.push_back({kTitleMulti, cx - 100, y + 24, 200, "Multijugador"});
      b.push_back({kTitleSkins, cx - 100, y + 48, 98, "Skins"});
      b.push_back({kTitleAchievements, cx + 2, y + 48, 98, "Logros"});
      b.push_back({kTitlePacks, cx - 100, y + 72, 98, "Paquetes..."});
      b.push_back({kTitleOptions, cx + 2, y + 72, 98, "Ajustes..."});
      if (opt_.canQuit) b.push_back({kTitleQuit, cx - 100, y + 96, 200, "Salir del juego"});
      else b.push_back({kTitleFullscreen, cx - 100, y + 96, 200, "Pantalla completa"});
      break;
    }
    case Screen::Worlds: {
      const bool sel = selectedWorld_ >= 0 && selectedWorld_ < static_cast<int>(worlds_.size());
      b.push_back({kWorldsPlay, cx - 154, h - 52, 100, "Jugar mundo", sel});
      b.push_back({kWorldsCreate, cx - 50, h - 52, 100, "Crear nuevo"});
      b.push_back({kWorldsImport, cx + 54, h - 52, 100, "Importar .zip"});
      b.push_back({kWorldsRename, cx - 154, h - 28, 58, "Renombrar", sel});
      b.push_back({kWorldsDelete, cx - 92, h - 28, 58, "Borrar", sel});
      b.push_back({kWorldsRecreate, cx - 30, h - 28, 58, "Recrear", sel});
      b.push_back({kWorldsExport, cx + 32, h - 28, 58, "Exportar", sel});
      b.push_back({kWorldsCancel, cx + 94, h - 28, 60, "Cancelar"});
      break;
    }
    case Screen::CreateWorld: {
      static const char* modes[] = {"Supervivencia", "Extremo (hardcore)", "Creativo"};
      static const char* diffs[] = {"Pacifica", "Facil", "Normal", "Dificil"};
      static const char* types[] = {"Normal", "Plano", "Biomas grandes", "Amplificado"};
      static const char* presets[] = {"Clasico", "Desierto", "Cantera", "Mundo de agua", "Nevado"};
      b.push_back({kCreateMode, cx - 155, 72, 150, std::string("Modo: ") + modes[newMode_]});
      b.push_back({kCreateDifficulty, cx + 5, 72, 150, std::string("Dificultad: ") + (newMode_ == 1 ? "Dificil" : diffs[newDifficulty_]),
                   newMode_ != 1});
      b.push_back({kCreateStructures, cx - 155, 96, 150, std::string("Estructuras: ") + (newStructures_ ? "Si" : "No")});
      b.push_back({kCreateType, cx + 5, 96, 150, std::string("Tipo: ") + types[newWorldType_]});
      b.push_back({kCreateCheats, cx - 155, 120, 150, std::string("Trucos: ") + (newCheats_ && newMode_ != 1 ? "Si" : "No"), newMode_ != 1});
      b.push_back({kCreateBonus, cx + 5, 120, 150, std::string("Cofre extra: ") + (newBonusChest_ ? "Si" : "No")});
      if (newWorldType_ == 1) b.push_back({kCreatePreset, cx - 155, 144, 310, std::string("Superplano: ") + presets[newFlatPreset_]});
      b.push_back({kCreateGo, cx - 155, h - 26, 150, "Crear mundo nuevo"});
      b.push_back({kCreateCancel, cx + 5, h - 26, 150, "Cancelar"});
      break;
    }
    case Screen::RenameWorld:
      b.push_back({kRenameOk, cx - 155, h / 2 + 20, 150, "Renombrar", !renameField_.text.empty()});
      b.push_back({kRenameCancel, cx + 5, h / 2 + 20, 150, "Cancelar"});
      break;
    case Screen::DeleteWorld:
      b.push_back({kDeleteOk, cx - 155, h / 2 + 20, 150, "Borrar"});
      b.push_back({kDeleteCancel, cx + 5, h / 2 + 20, 150, "Cancelar"});
      break;
    case Screen::Achievements:
      b.push_back({kAchToggle, cx - 154, h - 30, 150, achShowStats_ ? "Logros" : "Estadisticas"});
      b.push_back({kBack, cx + 4, h - 30, 150, "Volver"});
      break;
    case Screen::Multiplayer:
    case Screen::AddServer:
    case Screen::DirectConnect: return multiplayerButtons();
    case Screen::Skins:
    case Screen::SkinParts: return skinButtons();
    case Screen::Message:
      b.push_back({kBack, cx - 100, h - 40, 200, "Volver"});
      break;
    case Screen::ResourcePacks:
#ifdef __EMSCRIPTEN__
      b.push_back({kPacksAdd, cx - 160, h - 26, 154, "Añadir pack (.zip)..."});
#else
      b.push_back({kPacksOpenFolder, cx - 160, h - 26, 154, "Abrir carpeta de packs"});
#endif
      b.push_back({kPacksDone, cx + 6, h - 26, 154, "Listo"});
      break;
    default: break;
  }
  return b;
}

void Game::drawTitleLogo() {
  const float cx = ui_->guiWidth() / 2.0f;
  const std::string logo = "MC-WEB";
  const float scale = 4.0f;
  const float w = ui_->textWidth(logo) * scale;
  const float firstButton = menuButtons().empty() ? 100.0f : menuButtons().front().y;
  const float y = std::max(4.0f, std::min(std::floor(ui_->guiHeight() / 4.0f - 38), firstButton - 52));
  ui_->textScaled(cx - w / 2, y, logo, scale, 0xFFFFFF);
  ui_->textCentered(cx, y + 36, "Edicion 1.8", 0xC0C0C0);
  // Frase amarilla que late, como en el juego (a la derecha, debajo del logo)
  const float pulse = 1.0f + 0.06f * std::sin(static_cast<float>(runTime_) * 6.2832f * 1.2f);
  const std::string splash = asciiText(splash_);
  const float sw = ui_->textWidth(splash) * pulse;
  ui_->textScaled(std::min(cx + w / 2 - sw * 0.35f, ui_->guiWidth() - sw - 4), y + 38, splash, pulse, 0xFFFF00);
}

void Game::drawWorldList(glm::vec2 m) {
  checkWorldImports();
  if (screen_ != Screen::Worlds) return;
  const float cx = std::floor(ui_->guiWidth() / 2.0f);
  const float top = 32, bottom = static_cast<float>(ui_->guiHeight()) - 64;
  ui_->rect(0, top, static_cast<float>(ui_->guiWidth()), bottom - top, 0xC0000000);
  if (worlds_.empty()) {
    ui_->textCentered(cx, (top + bottom) / 2 - 4, "No hay mundos guardados: crea uno nuevo", 0xA0A0A0);
    return;
  }
  const float maxScroll = std::max(0.0f, worlds_.size() * kEntryH - (bottom - top - 4));
  worldScroll_ = std::clamp(worldScroll_, 0.0f, maxScroll);
  for (int i = 0; i < static_cast<int>(worlds_.size()); i++) {
    const float y = top + 4 + i * kEntryH - worldScroll_;
    if (y < top || y + kEntryH - 4 > bottom) continue;
    const WorldSummary& w = worlds_[i];
    const float x = cx - 110;
    if (i == selectedWorld_) {
      ui_->rect(x - 2, y - 2, 224, kEntryH, 0xFF808080);
      ui_->rect(x - 1, y - 1, 222, kEntryH - 2, 0xFF000000);
    } else if (m.x >= x && m.x < x + 220 && m.y >= y && m.y < y + kEntryH - 4) {
      ui_->rect(x - 1, y - 1, 222, kEntryH - 2, 0x40FFFFFF);
    }
    ui_->text(x + 2, y + 1, asciiText(w.name), 0xFFFFFF);
    ui_->text(x + 2, y + 12, asciiText(w.folder + " (" + formatDate(w.lastPlayed) + ")"), 0x808080);
    std::string mode = w.hardcore ? "Extremo" : (w.gameType == 1 ? "Creativo" : "Supervivencia");
    if (w.allowCommands) mode += ", trucos";
    ui_->text(x + 2, y + 23, asciiText(mode + " - " + formatSize(w.sizeBytes)), w.hardcore ? 0xFF5555 : 0x808080);
  }
  if (maxScroll > 0) {
    const float h = bottom - top, thumb = std::max(12.0f, h * h / (h + maxScroll));
    ui_->rect(cx + 116, top, 4, h, 0x80000000);
    ui_->rect(cx + 116, top + (h - thumb) * (worldScroll_ / maxScroll), 4, thumb, 0xFFC0C0C0);
  }
}

void Game::drawMenuScreen(int w, int h) {
  (void)w;
  (void)h;
  const glm::vec2 m = mouseGui();
  const float cx = std::floor(ui_->guiWidth() / 2.0f);
  const float gh = static_cast<float>(ui_->guiHeight());
  // Campos de texto: posición según la pantalla (igual al dibujar y al pulsar)
  nameField_.x = seedField_.x = cx - 155;
  nameField_.w = seedField_.w = 310;
  nameField_.y = 24;
  seedField_.y = 48;
  renameField_.x = cx - 100;
  renameField_.w = 200;
  renameField_.y = gh / 2 - 10;
  drawMenuBackground(*ui_);
  switch (screen_) {
    case Screen::Title: {
      drawTitleLogo();
      ui_->text(2, gh - 10, "MC-WEB 1.0 (Minecraft 1.8)", 0xFFFFFF);
      const std::string right = "Codigo libre (MIT). No es de Mojang.";
      ui_->text(ui_->guiWidth() - ui_->textWidth(right) - 2.0f, gh - 10, right, 0xFFFFFF);
      break;
    }
    case Screen::Worlds:
      ui_->textCentered(cx, 16, "Seleccionar mundo", 0xFFFFFF);
      drawWorldList(m);
      break;
    case Screen::CreateWorld:
      ui_->textCentered(cx, 8, "Crear mundo nuevo", 0xFFFFFF);
      drawTextField(*ui_, nameField_, runTime_, "Nombre del mundo");
      drawTextField(*ui_, seedField_, runTime_, "Semilla (vacia = al azar)");
      if (newWorldType_ != 1)
        ui_->textCentered(cx, 150, asciiText(newMode_ == 1   ? "Una sola vida y dificultad maxima"
                                             : newMode_ == 2 ? "Recursos infinitos, volar y romper al instante"
                                                             : "Busca recursos, fabrica, sube de nivel y sobrevive"),
                          0xA0A0A0);
      break;
    case Screen::RenameWorld:
      ui_->textCentered(cx, gh / 2 - 40, "Renombrar mundo", 0xFFFFFF);
      drawTextField(*ui_, renameField_, runTime_);
      break;
    case Screen::DeleteWorld: {
      const std::string name = selectedWorld_ >= 0 && selectedWorld_ < static_cast<int>(worlds_.size()) ? worlds_[selectedWorld_].name : "";
      ui_->textCentered(cx, gh / 2 - 40, asciiText("¿Seguro que quieres borrar este mundo?"), 0xFFFFFF);
      ui_->textCentered(cx, gh / 2 - 20, asciiText("\"" + name + "\" se perderá para siempre."), 0xA0A0A0);
      break;
    }
    case Screen::ResourcePacks: drawPackScreen(m); break;
    case Screen::Achievements: drawAchievementScreen(m); return;
    case Screen::Multiplayer:
    case Screen::AddServer:
    case Screen::DirectConnect: drawMultiplayer(m); break;
    case Screen::Skins:
    case Screen::SkinParts: drawSkins(m); break;
    case Screen::Message:
    {
      ui_->textCentered(cx, gh / 3, asciiText(message_), 0xFFFFFF);
      float y = gh / 3 + 14;
      for (const std::string& line : wrapText(*ui_, asciiText(messageDetail_), ui_->guiWidth() - 40.0f)) {
        ui_->textCentered(cx, y, line, 0xA0A0A0);
        y += 10;
      }
      break;
    }
    default: break;
  }
  drawButtons(*ui_, menuButtons(), m.x, m.y);
}

void Game::menuButton(int id) {
  audio_->playFlat(Sfx::Click);
  if (id >= 80 && id < 100) {
    multiplayerButton(id);
    return;
  }
  if (id >= 100 && id < 130) {
    skinsButton(id);
    return;
  }
  const bool sel = selectedWorld_ >= 0 && selectedWorld_ < static_cast<int>(worlds_.size());
  switch (id) {
    case kTitleSingle: openScreen(Screen::Worlds); break;
    case kTitleMulti: openScreen(Screen::Multiplayer); break;
    case kTitleSkins: openScreen(Screen::Skins); break;
    case kTitleAchievements: {
      // Los del último mundo jugado
      menuAchievements_.clear();
      achWorldName_.clear();
      const auto list = WorldSave::list();
      if (!list.empty()) {
        achWorldName_ = list.front().name;
        if (auto text = fs::readText(WorldSave::savesDir() / list.front().folder / "stats" / (offlineUuid(settings_.playerName) + ".json")))
          menuAchievements_.fromJson(*text);
      }
      achShowStats_ = false;
      achScroll_ = {0, 0};
      openScreen(Screen::Achievements);
      break;
    }
    case kAchToggle: achShowStats_ = !achShowStats_; break;
    case kTitlePacks: openScreen(Screen::ResourcePacks); break;
    case kPacksOpenFolder: SDL_OpenURL(("file://" + resourcePackDir().string()).c_str()); break;
    case kPacksAdd:
#ifdef __EMSCRIPTEN__
      mcw_js_pick_pack();
#endif
      break;
    case kPacksDone:
      if (packSelection_ != settings_.resourcePacks) {
        settings_.resourcePacks = packSelection_;
        saveSettings();
        reloadResources();
      }
      openScreen(Screen::Title);
      break;
    case kTitleOptions:
      openOptionPage(OptPage::Main);
      setScreen(Screen::Options);
      break;
    case kTitleQuit: quit_ = true; break;
    case kTitleFullscreen: SDL_SetWindowFullscreen(window_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) == 0); break;
    case kWorldsPlay:
      if (sel) {
        WorldSave w(WorldSave::savesDir() / worlds_[selectedWorld_].folder);
        LevelInfo info;
        if (w.loadLevel(info)) enterWorld(worlds_[selectedWorld_].folder, std::move(info));
        else {
          message_ = "No se pudo abrir el mundo";
          messageDetail_ = worlds_[selectedWorld_].folder;
          openScreen(Screen::Message);
        }
      }
      break;
    case kWorldsCreate:
      newMode_ = 0;
      newDifficulty_ = settings_.difficulty;
      newWorldType_ = newFlatPreset_ = 0;
      newStructures_ = true;
      newCheats_ = newBonusChest_ = false;
      openScreen(Screen::CreateWorld);
      break;
    case kWorldsRename: if (sel) openScreen(Screen::RenameWorld); break;
    case kWorldsDelete: if (sel) openScreen(Screen::DeleteWorld); break;
    case kWorldsRecreate:
      // Mismo nombre, semilla y opciones; mundo nuevo
      if (sel) {
        WorldSave w(WorldSave::savesDir() / worlds_[selectedWorld_].folder);
        LevelInfo info;
        if (w.loadLevel(info)) {
          openScreen(Screen::CreateWorld);
          nameField_.text = info.name;
          seedField_.text = std::to_string(static_cast<i64>(info.seed));
          newMode_ = info.hardcore ? 1 : (info.gameType == 1 ? 2 : 0);
          newDifficulty_ = info.difficulty;
          newStructures_ = info.mapFeatures;
          newCheats_ = info.allowCommands;
          newWorldType_ = info.generator == "flat" ? 1 : info.generator == "largeBiomes" ? 2 : info.generator == "amplified" ? 3 : 0;
        }
      }
      break;
    case kWorldsCancel: openScreen(Screen::Title); break;
    case kWorldsImport: {
      const std::filesystem::path dir = worldImportDir();
#ifdef __EMSCRIPTEN__
      mcw_js_pick_world();
#else
      // Diálogo del sistema; el archivo elegido se copia a la carpeta de importar y se importa al volver
      static std::filesystem::path target;
      target = dir;
      static const SDL_DialogFileFilter filter{"Mundo de Minecraft (.zip)", "zip"};
      SDL_ShowOpenFileDialog(
          [](void*, const char* const* files, int) {
            if (!files || !files[0]) return;
            std::error_code ec;
            const std::filesystem::path src(files[0]);
            std::filesystem::copy_file(src, target / src.filename(), std::filesystem::copy_options::overwrite_existing, ec);
          },
          nullptr, window_, &filter, 1, nullptr, false);
      message_ = "Importar un mundo";
      messageDetail_ = "Elige el .zip, o copialo en: " + dir.string();
#endif
      break;
    }
    case kWorldsExport:
      if (sel) {
        const std::string folder = worlds_[selectedWorld_].folder;
        const std::vector<u8> zip = WorldSave::exportZip(folder);
        if (zip.empty()) {
          message_ = "No se pudo exportar el mundo";
          messageDetail_ = folder;
          openScreen(Screen::Message);
          break;
        }
#ifdef __EMSCRIPTEN__
        mcw_js_download((folder + ".zip").c_str(), zip.data(), static_cast<int>(zip.size()));
#else
        const std::filesystem::path dir = fs::userDataDir() / "exports";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path out = dir / (folder + ".zip");
        if (fs::writeFile(out, zip.data(), zip.size())) {
          message_ = "Mundo exportado";
          messageDetail_ = out.string();
          SDL_OpenURL(("file://" + dir.string()).c_str());
        } else {
          message_ = "No se pudo escribir el .zip";
          messageDetail_ = out.string();
        }
        openScreen(Screen::Message);
#endif
      }
      break;
    case kCreateMode: newMode_ = (newMode_ + 1) % 3; break;
    case kCreateDifficulty: newDifficulty_ = (newDifficulty_ + 1) % 4; break;
    case kCreateStructures: newStructures_ = !newStructures_; break;
    case kCreateType: newWorldType_ = (newWorldType_ + 1) % 4; break;
    case kCreateCheats: newCheats_ = !newCheats_; break;
    case kCreateBonus: newBonusChest_ = !newBonusChest_; break;
    case kCreatePreset: newFlatPreset_ = (newFlatPreset_ + 1) % 5; break;
    case kCreateGo: createWorldFromForm(); break;
    case kCreateCancel: openScreen(Screen::Worlds); break;
    case kRenameOk:
      if (sel && !renameField_.text.empty()) WorldSave::rename(worlds_[selectedWorld_].folder, renameField_.text);
      openScreen(Screen::Worlds);
      break;
    case kRenameCancel: openScreen(Screen::Worlds); break;
    case kDeleteOk:
      if (sel) WorldSave::remove(worlds_[selectedWorld_].folder);
      openScreen(Screen::Worlds);
      break;
    case kDeleteCancel: openScreen(Screen::Worlds); break;
    case kBack:
      if (net_ && !inWorld_) {  // cancelar la conexión
        net_.reset();
        openScreen(Screen::Multiplayer);
        break;
      }
      if (screen_ == Screen::Message && !inWorld_ && !netAddress_.empty()) {  // tras un fallo de conexión
        netAddress_.clear();
        openScreen(Screen::Multiplayer);
        break;
      }
      if (screen_ == Screen::Message && !inWorld_ && messageBack_ != Screen::Title) {  // a la pantalla de la que venía
        const Screen back = messageBack_;
        messageBack_ = Screen::Title;
        openScreen(back);
        break;
      }
      openScreen(inWorld_ ? Screen::Pause : Screen::Title);
      break;
    default: break;
  }
}

void Game::menuPress(glm::vec2 gui, int button) {
  (void)button;
  // Campos de texto: el que se toca recibe el foco
  if (screen_ == Screen::CreateWorld) {
    nameField_.focused = nameField_.contains(gui.x, gui.y);
    seedField_.focused = seedField_.contains(gui.x, gui.y);
  }
  if (screen_ == Screen::RenameWorld) renameField_.focused = true;
  if (screen_ == Screen::Skins || screen_ == Screen::SkinParts) {
    const int id = buttonAt(menuButtons(), gui.x, gui.y);
    if (id >= 0) menuButton(id);
    else skinsPress(gui);
    return;
  }
  if (screen_ == Screen::Multiplayer || screen_ == Screen::AddServer || screen_ == Screen::DirectConnect) {
    const int id = buttonAt(menuButtons(), gui.x, gui.y);
    if (id >= 0) menuButton(id);
    else multiplayerPress(gui);
    return;
  }
  const int id = buttonAt(menuButtons(), gui.x, gui.y);
  if (id >= 0) {
    menuButton(id);
    return;
  }
  if (screen_ == Screen::ResourcePacks) {
    // Tocar un disponible lo activa (arriba del todo); tocar un activo lo quita
    const float cx = std::floor(ui_->guiWidth() / 2.0f), top = 32;
    const int i = static_cast<int>((gui.y - top - 2) / kPackEntryH);
    if (i < 0 || gui.y < top) return;
    if (gui.x >= cx - 160 && gui.x < cx - 6) {
      int n = 0;
      for (const PackEntry& p : availablePacks_) {
        if (std::find(packSelection_.begin(), packSelection_.end(), p.file) != packSelection_.end()) continue;
        if (n++ == i) {
          packSelection_.insert(packSelection_.begin(), p.file);
          audio_->playFlat(Sfx::Click);
          break;
        }
      }
    } else if (gui.x >= cx + 6 && gui.x < cx + 160 && i < static_cast<int>(packSelection_.size())) {
      packSelection_.erase(packSelection_.begin() + i);
      audio_->playFlat(Sfx::Click);
    }
    return;
  }
  if (screen_ == Screen::Worlds) {
    const float cx = std::floor(ui_->guiWidth() / 2.0f), top = 32, bottom = static_cast<float>(ui_->guiHeight()) - 64;
    if (gui.y < top || gui.y > bottom || gui.x < cx - 112 || gui.x > cx + 112) return;
    const int i = static_cast<int>((gui.y - top - 4 + worldScroll_) / kEntryH);
    if (i < 0 || i >= static_cast<int>(worlds_.size())) return;
    const u64 now = SDL_GetTicksNS();
    if (i == selectedWorld_ && now - lastWorldClick_ < 400'000'000ull) {
      menuButton(kWorldsPlay);  // doble clic: jugar
      return;
    }
    selectedWorld_ = i;
    lastWorldClick_ = now;
  }
}

void Game::menuKey(SDL_Scancode sc) {
  if (multiplayerKey(sc)) return;
  TextField* field = nullptr;
  if (screen_ == Screen::CreateWorld) field = nameField_.focused ? &nameField_ : (seedField_.focused ? &seedField_ : nullptr);
  if (screen_ == Screen::RenameWorld) field = &renameField_;
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      switch (screen_) {
        case Screen::Worlds: openScreen(Screen::Title); break;
        case Screen::CreateWorld:
        case Screen::RenameWorld:
        case Screen::DeleteWorld: openScreen(Screen::Worlds); break;
        case Screen::Title: break;
        case Screen::SkinParts: openScreen(Screen::Skins); break;
        case Screen::Message: menuButton(kBack); break;
        default: openScreen(inWorld_ ? Screen::Pause : Screen::Title); break;
      }
      return;
    case SDL_SCANCODE_BACKSPACE:
      if (field) field->backspace();
      return;
    case SDL_SCANCODE_TAB:
      if (screen_ == Screen::CreateWorld) {
        const bool n = nameField_.focused;
        nameField_.focused = !n;
        seedField_.focused = n;
      }
      return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
      if (screen_ == Screen::CreateWorld) menuButton(kCreateGo);
      else if (screen_ == Screen::RenameWorld) menuButton(kRenameOk);
      else if (screen_ == Screen::Worlds) menuButton(kWorldsPlay);
      else if (screen_ == Screen::DeleteWorld) menuButton(kDeleteOk);
      return;
    case SDL_SCANCODE_UP:
      if (screen_ == Screen::Worlds && selectedWorld_ > 0) selectedWorld_--;
      return;
    case SDL_SCANCODE_DOWN:
      if (screen_ == Screen::Worlds && selectedWorld_ + 1 < static_cast<int>(worlds_.size())) selectedWorld_++;
      return;
    case SDL_SCANCODE_DELETE:
      if (screen_ == Screen::Worlds && selectedWorld_ >= 0) openScreen(Screen::DeleteWorld);
      return;
    default: break;
  }
}

void Game::menuText(std::string_view text) {
  if (screen_ == Screen::Menu) {  // el campo de búsqueda del inventario creativo, o el nombre del yunque
    if (creativeSearchActive()) {
      session_->menu()->typeSearch(text);
    } else if (anvilNameActive()) {
      std::string name = session_->menu()->anvilName();
      for (char c : text)
        if (static_cast<unsigned char>(c) >= 32 && c != 127) name += c;
      setAnvilName(std::move(name));
    }
  } else if (screen_ == Screen::CreateWorld) {
    if (nameField_.focused) nameField_.insert(text);
    else if (seedField_.focused) seedField_.insert(text);
  } else if (screen_ == Screen::RenameWorld) {
    renameField_.insert(text);
  } else if (screen_ == Screen::Chat) {
    chatField_.insert(text);
  } else {
    multiplayerText(text);
  }
}

}  // namespace mcw
