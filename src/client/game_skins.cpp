// Pantalla Skins: elegir la skin (las de serie y las que subas), ver el personaje girando y
// personalizar las capas (sombrero, chaqueta, mangas y perneras), como la pantalla "Personalizar
// skin" de 1.8. La skin elegida es la que ven los demás en multijugador.
#include <algorithm>
#include <cmath>
#include <format>

#include "assets/pack.h"
#include "client/audio.h"
#include "client/game.h"
#include "client/ui.h"
#include "core/fs.h"
#include "core/hash.h"
#include "core/log.h"
#include "save/world_save.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {
namespace {

enum SkinMenuId {
  kSkUpload = 100, kSkSlim, kSkParts, kSkDelete, kSkDone,
  kSkPartBase = 110,  // + 0..5: una por capa
  kSkPartsDone = 120,
};

struct PartInfo {
  u8 bit;
  const char* label;
};
constexpr PartInfo kParts[] = {{kSkinHat, "Sombrero"},          {kSkinJacket, "Chaqueta"},
                               {kSkinLeftSleeve, "Manga izquierda"}, {kSkinRightSleeve, "Manga derecha"},
                               {kSkinLeftPants, "Pernera izquierda"}, {kSkinRightPants, "Pernera derecha"}};

constexpr float kSkinTop = 24.0f, kSkinEntryH = 24.0f;
constexpr const char* kIconPrefix = "list:";

#ifdef __EMSCRIPTEN__
// Web: elegir un PNG; se deja en /persist/skin_imports y el juego lo importa solo
EM_JS(void, mcw_js_pick_skin, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.png,image/png';
  input.onchange = async () => {
    const f = input.files && input.files[0];
    if (!f) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    try {
      FS.mkdirTree('/persist/skin_imports');
      FS.writeFile('/persist/skin_imports/' + f.name.replace(/[\\/]/g, '_'), bytes);
    } catch (e) { console.warn('no se pudo leer la skin', e); }
  };
  input.click();
});
#endif

}  // namespace

std::string Game::currentSkinId() const {
  if (!settings_.skin.empty()) return settings_.skin;
  // Sin elegir: Steve o Alex según el UUID del nombre, como lo vería cualquiera en un servidor offline
  return defaultSkinSlim(offlineUuid(settings_.playerName)) ? "alex" : "steve";
}

SkinRef Game::localSkinRef() const {
  return {"local", defaultSkinSlim(offlineUuid(settings_.playerName)), static_cast<u8>(settings_.skinParts)};
}

void Game::applyLocalSkin() {
  const SkinStore store;
  std::optional<PreparedSkin> p;
  if (packs_)
    if (auto e = store.find(currentSkinId())) p = store.load(*e, *packs_);
  if (!p && packs_) {  // la elegida ya no está (o no se lee): la de serie
    const bool slim = defaultSkinSlim(offlineUuid(settings_.playerName));
    p = store.load({slim ? "alex" : "steve", slim ? "Alex" : "Steve", true, slim}, *packs_);
  }
  localSkin_ = p ? *p : PreparedSkin{Image(64, 64, 0xFFFF00FF), false};
  localSkinPng_ = skinToNetworkPng(localSkin_);
  localSkinVersion_++;
  if (entityRenderer_) entityRenderer_->setSkin("local", localSkin_);
}

std::filesystem::path Game::skinImportDir() {
  const std::filesystem::path p = fs::userDataDir() / "skin_imports";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

void Game::registerSkinIcons() {
  if (!entityRenderer_ || !packs_) return;
  const SkinStore store;
  for (const SkinEntry& e : skinList_)
    if (auto p = store.load(e, *packs_)) entityRenderer_->setSkin(kIconPrefix + e.id, *p);
}

void Game::openSkins() {
  skinList_ = SkinStore().list();
  const std::string id = currentSkinId();
  skinSelected_ = 0;
  for (int i = 0; i < static_cast<int>(skinList_.size()); i++)
    if (skinList_[i].id == id) skinSelected_ = i;
  skinScroll_ = 0;
  skinDeleteArmed_ = false;
  registerSkinIcons();
}

void Game::closeSkins() {
  if (entityRenderer_)
    for (const SkinEntry& e : skinList_) entityRenderer_->removeSkin(kIconPrefix + e.id);
  skinList_.clear();
}

void Game::chooseSkin(int index) {
  if (index < 0 || index >= static_cast<int>(skinList_.size())) return;
  skinSelected_ = index;
  skinDeleteArmed_ = false;
  settings_.skin = skinList_[index].id;
  saveSettings();
  applyLocalSkin();
}

void Game::checkSkinImports() {
  // Los PNG que aparecen en la carpeta de importar (elegidos en el diálogo o copiados a mano)
  if (runTime_ - skinImportCheck_ < 0.5) return;
  skinImportCheck_ = runTime_;
  std::error_code ec;
  std::vector<std::filesystem::path> found;
  for (const auto& e : std::filesystem::directory_iterator(skinImportDir(), ec))
    if (e.is_regular_file(ec)) found.push_back(e.path());
  if (found.empty()) return;
  SkinStore store;
  std::string lastId, error;
  for (const auto& path : found) {
    auto data = fs::readFile(path);
    if (!data) {
      error = "No se pudo leer el archivo";
    } else {
      const auto r = store.import(*data, path.stem().string());
      if (r.id.empty()) error = r.error;
      else lastId = r.id;
    }
    std::filesystem::remove(path, ec);
  }
  WorldSave::flush();
  if (!lastId.empty()) {
    // La skin nueva queda elegida
    closeSkins();
    skinList_ = store.list();
    for (int i = 0; i < static_cast<int>(skinList_.size()); i++)
      if (skinList_[i].id == lastId) skinSelected_ = i;
    registerSkinIcons();
    chooseSkin(skinSelected_);
    // Que se vea en la lista
    skinScroll_ = std::max(0.0f, (skinSelected_ + 1) * kSkinEntryH - (ui_->guiHeight() - 60 - kSkinTop) + 4);
  }
  if (!error.empty()) {
    message_ = "No se pudo usar la skin";
    messageDetail_ = error;
    messageBack_ = Screen::Skins;
    openScreen(Screen::Message);
  }
}

std::vector<MenuButton> Game::skinButtons() const {
  std::vector<MenuButton> b;
  const float cx = std::floor(ui_->guiWidth() / 2.0f), gh = static_cast<float>(ui_->guiHeight());
  if (screen_ == Screen::SkinParts) {
    const float step = std::min(24.0f, (gh - kSkinTop - 38.0f) / 6.0f);
    for (int i = 0; i < 6; i++)
      b.push_back({kSkPartBase + i, cx + 2, kSkinTop + i * step, 152,
                   std::string(kParts[i].label) + ": " + ((settings_.skinParts & kParts[i].bit) ? "Sí" : "No")});
    b.push_back({kSkPartsDone, cx - 100, gh - 28, 200, "Listo"});
    return b;
  }
  const bool mine = skinSelected_ >= 0 && skinSelected_ < static_cast<int>(skinList_.size()) && !skinList_[skinSelected_].builtin;
  const bool slim = mine && skinList_[skinSelected_].slim;
  b.push_back({kSkUpload, cx - 154, gh - 52, 100, "Subir skin..."});
  b.push_back({kSkSlim, cx - 50, gh - 52, 100, std::string("Brazos: ") + (slim ? "finos" : "anchos"), mine});
  b.push_back({kSkParts, cx + 54, gh - 52, 100, "Capas..."});
  b.push_back({kSkDelete, cx - 154, gh - 28, 100, skinDeleteArmed_ ? "¿Seguro?" : "Borrar", mine});
  b.push_back({kSkDone, cx + 54, gh - 28, 100, "Listo"});
  return b;
}

void Game::drawSkins(glm::vec2 m) {
  const float cx = std::floor(ui_->guiWidth() / 2.0f), gh = static_cast<float>(ui_->guiHeight());
  const bool partsScreen = screen_ == Screen::SkinParts;
  if (!partsScreen) checkSkinImports();
  if (screen_ != Screen::Skins && !partsScreen) return;  // (la importación ha cambiado de pantalla)
  ui_->textCentered(cx, 8, partsScreen ? "Personalizar skin" : "Skins", 0xFFFFFF);

  const float top = kSkinTop, bottom = partsScreen ? gh - 36.0f : gh - 60.0f;
  // El personaje, girando
  const float px = cx - 154, pw = 150;
  ui_->rect(px, top, pw, bottom - top, 0xC0000000);
  ui_->flush();
  if (entityRenderer_) {
    const float s = static_cast<float>(ui_->scale());
    const float scale = std::max(16.0f, (bottom - top - 14.0f) / 2.0f);  // 32 píxeles de modelo de alto
    entityRenderer_->drawPlayerPreview((px + pw / 2) * s, (bottom - 7) * s, scale * s, 0, 0, ui_->screenWidth(), ui_->screenHeight(),
                                       glm::vec3(1.0f), localSkinRef(), static_cast<float>(runTime_) * 0.8f);
  }
  if (partsScreen) return;  // los botones de las capas los dibuja el menú

  // La lista de skins
  const float lx = cx, lw = 154;
  ui_->rect(lx, top, lw, bottom - top, 0xC0000000);
  const int n = static_cast<int>(skinList_.size());
  const float maxScroll = std::max(0.0f, n * kSkinEntryH + 4 - (bottom - top));
  skinScroll_ = std::clamp(skinScroll_, 0.0f, maxScroll);
  struct Icon {
    std::string key;
    float x, y;
  };
  std::vector<Icon> icons;
  std::vector<std::pair<std::string, std::pair<float, float>>> texts;
  for (int i = 0; i < n; i++) {
    const float y = top + 2 + i * kSkinEntryH - skinScroll_;
    if (y < top || y + kSkinEntryH - 2 > bottom) continue;
    const SkinEntry& e = skinList_[i];
    if (i == skinSelected_) {
      ui_->rect(lx + 1, y - 1, lw - 8, kSkinEntryH - 1, 0xFF808080);
      ui_->rect(lx + 2, y, lw - 10, kSkinEntryH - 3, 0xFF000000);
    } else if (m.x >= lx && m.x < lx + lw - 6 && m.y >= y && m.y < y + kSkinEntryH - 2) {
      ui_->rect(lx + 2, y, lw - 10, kSkinEntryH - 3, 0x40FFFFFF);
    }
    icons.push_back({kIconPrefix + e.id, lx + 6, y + 2});
    ui_->text(lx + 28, y + 3, asciiText(e.name), 0xFFFFFF);
    ui_->text(lx + 28, y + 13, asciiText(std::string(e.builtin ? "De serie" : "Subida") + ", brazos " + (e.slim ? "finos" : "anchos")), 0x808080);
  }
  if (maxScroll > 0) {
    const float h = bottom - top, thumb = std::max(12.0f, h * h / (h + maxScroll));
    ui_->rect(lx + lw - 5, top, 4, h, 0x80000000);
    ui_->rect(lx + lw - 5, top + (h - thumb) * (skinScroll_ / maxScroll), 4, thumb, 0xFFC0C0C0);
  }
  // Caras de cada skin (se dibujan aparte: no son del atlas de la interfaz)
  ui_->flush();
  if (entityRenderer_) {
    const float s = static_cast<float>(ui_->scale());
    for (const Icon& ic : icons)
      entityRenderer_->drawSkinFace({ic.key, false, kAllSkinParts}, ic.x * s, ic.y * s, 16.0f * s, ui_->screenWidth(), ui_->screenHeight());
  }
}

void Game::skinsPress(glm::vec2 gui) {
  if (screen_ != Screen::Skins) return;
  const float cx = std::floor(ui_->guiWidth() / 2.0f), bottom = static_cast<float>(ui_->guiHeight()) - 60.0f;
  if (gui.x < cx || gui.x >= cx + 148 || gui.y < kSkinTop || gui.y >= bottom) return;
  const int i = static_cast<int>((gui.y - kSkinTop - 2 + skinScroll_) / kSkinEntryH);
  if (i < 0 || i >= static_cast<int>(skinList_.size())) return;
  if (i != skinSelected_) audio_->playFlat(Sfx::Click);
  chooseSkin(i);
}

void Game::skinsButton(int id) {
  const bool mine = skinSelected_ >= 0 && skinSelected_ < static_cast<int>(skinList_.size()) && !skinList_[skinSelected_].builtin;
  if (id != kSkDelete) skinDeleteArmed_ = false;
  if (id >= kSkPartBase && id < kSkPartBase + 6) {
    settings_.skinParts ^= kParts[id - kSkPartBase].bit;
    saveSettings();
    return;
  }
  switch (id) {
    case kSkUpload: {
#ifdef __EMSCRIPTEN__
      mcw_js_pick_skin();
#else
      // Diálogo del sistema; el PNG elegido se copia a la carpeta de importar y se importa al volver
      static std::filesystem::path target;
      target = skinImportDir();
      static const SDL_DialogFileFilter filter{"Skin (.png)", "png"};
      SDL_ShowOpenFileDialog(
          [](void*, const char* const* files, int) {
            if (!files || !files[0]) return;
            std::error_code ec;
            const std::filesystem::path src(files[0]);
            std::filesystem::copy_file(src, target / src.filename(), std::filesystem::copy_options::overwrite_existing, ec);
          },
          nullptr, window_, &filter, 1, nullptr, false);
#endif
      break;
    }
    case kSkSlim:
      if (mine) {
        SkinEntry& e = skinList_[skinSelected_];
        SkinStore().setSlim(e.id, !e.slim);
        WorldSave::flush();
        e.slim = !e.slim;
        if (entityRenderer_)
          if (auto p = SkinStore().load(e, *packs_)) entityRenderer_->setSkin(kIconPrefix + e.id, *p);
        if (settings_.skin == e.id || (settings_.skin.empty() && currentSkinId() == e.id)) applyLocalSkin();
      }
      break;
    case kSkParts: openScreen(Screen::SkinParts); break;
    case kSkDelete:
      if (!mine) break;
      if (!skinDeleteArmed_) {
        skinDeleteArmed_ = true;  // hace falta pulsar otra vez
        break;
      }
      skinDeleteArmed_ = false;
      {
        const std::string gone = skinList_[skinSelected_].id;
        SkinStore().remove(gone);
        WorldSave::flush();
        if (settings_.skin == gone) {
          settings_.skin.clear();
          saveSettings();
          applyLocalSkin();
        }
        if (entityRenderer_) entityRenderer_->removeSkin(kIconPrefix + gone);
        skinList_ = SkinStore().list();
        const std::string now = currentSkinId();
        skinSelected_ = 0;
        for (int i = 0; i < static_cast<int>(skinList_.size()); i++)
          if (skinList_[i].id == now) skinSelected_ = i;
      }
      break;
    case kSkDone: openScreen(inWorld_ ? Screen::Pause : Screen::Title); break;
    case kSkPartsDone: openScreen(Screen::Skins); break;
    default: break;
  }
}

}  // namespace mcw
