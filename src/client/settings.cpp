#include "client/settings.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>

#include "core/fs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {
namespace {

float toFloat(std::string_view v, float fallback) {
  try {
    const float out = std::stof(std::string(v));
    return std::isfinite(out) ? out : fallback;
  } catch (...) {
    return fallback;
  }
}

const char* kKeyIds[] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "inventory", "drop",
                         "perspective", "hidehud", "debug", "screenshot", "fullscreen"};
static_assert(std::size(kKeyIds) == static_cast<std::size_t>(KeyAction::Count));

}  // namespace

const char* keyActionName(KeyAction a) {
  static const char* names[] = {"Adelante", "Atras", "Izquierda", "Derecha", "Saltar", "Agacharse", "Correr",
                                "Inventario", "Soltar objeto", "Perspectiva", "Ocultar interfaz", "Depuracion",
                                "Captura", "Pantalla completa"};
  return names[static_cast<int>(a)];
}

SDL_Scancode defaultKey(KeyAction a) {
  switch (a) {
    case KeyAction::Forward: return SDL_SCANCODE_W;
    case KeyAction::Back: return SDL_SCANCODE_S;
    case KeyAction::Left: return SDL_SCANCODE_A;
    case KeyAction::Right: return SDL_SCANCODE_D;
    case KeyAction::Jump: return SDL_SCANCODE_SPACE;
    case KeyAction::Sneak: return SDL_SCANCODE_LSHIFT;
    case KeyAction::Sprint: return SDL_SCANCODE_LCTRL;
    case KeyAction::Inventory: return SDL_SCANCODE_E;
    case KeyAction::Drop: return SDL_SCANCODE_Q;
    case KeyAction::Perspective: return SDL_SCANCODE_F5;
    case KeyAction::HideHud: return SDL_SCANCODE_F1;
    case KeyAction::Debug: return SDL_SCANCODE_F3;
    case KeyAction::Screenshot: return SDL_SCANCODE_F2;
    case KeyAction::Fullscreen: return SDL_SCANCODE_F11;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}

std::string keyName(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_UNKNOWN: return "Ninguna";
    case SDL_SCANCODE_SPACE: return "Espacio";
    case SDL_SCANCODE_LSHIFT: return "Mayus izq.";
    case SDL_SCANCODE_RSHIFT: return "Mayus der.";
    case SDL_SCANCODE_LCTRL: return "Ctrl izq.";
    case SDL_SCANCODE_RCTRL: return "Ctrl der.";
    case SDL_SCANCODE_LALT: return "Alt";
    case SDL_SCANCODE_RALT: return "Alt Gr";
    case SDL_SCANCODE_RETURN: return "Intro";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_CAPSLOCK: return "Bloq Mayus";
    case SDL_SCANCODE_BACKSPACE: return "Retroceso";
    case SDL_SCANCODE_UP: return "Flecha arriba";
    case SDL_SCANCODE_DOWN: return "Flecha abajo";
    case SDL_SCANCODE_LEFT: return "Flecha izq.";
    case SDL_SCANCODE_RIGHT: return "Flecha der.";
    default: break;
  }
  const char* n = SDL_GetScancodeName(sc);
  return n && *n ? n : std::format("Tecla {}", static_cast<int>(sc));
}

Settings::Settings() { resetKeys(); }

void Settings::resetKeys() {
  for (int i = 0; i < static_cast<int>(KeyAction::Count); i++) keys[i] = defaultKey(static_cast<KeyAction>(i));
}

std::string Settings::serialize() const {
  std::string s = std::format(
      "renderDistance:{}\nfov:{}\nbrightness:{}\nfpsLimit:{}\nvsync:{}\nrenderScale:{}\nsmoothLighting:{}\nfancyLeaves:{}\n"
      "clouds:{}\nparticles:{}\nmipmaps:{}\nfog:{}\nviewBobbing:{}\nentityDistance:{}\nocclusionCulling:{}\nadaptiveFps:{}\n",
      renderDistance, fov, brightness, fpsLimit, vsync, renderScale, smoothLighting, fancyLeaves, clouds, particles, mipmaps,
      fog, viewBobbing, entityDistance, occlusionCulling, adaptiveFps);
  s += std::format("volume:{}\nvolBlocks:{}\nvolMobs:{}\nvolPlayer:{}\nvolUi:{}\nvolMusic:{}\n", volume, volBlocks, volMobs,
                   volPlayer, volUi, volMusic);
  s += std::format(
      "sensitivity:{}\ninvertMouse:{}\ntouchSensitivity:{}\ntouchButtonScale:{}\ntouchOpacity:{}\ntouchSmoothing:{}\n"
      "touchDeadzone:{}\ntouchCurve:{}\nfloatingJoystick:{}\ntouchActionButtons:{}\ntouchLeftHanded:{}\ntouchHaptics:{}\n"
      "touchScheme:{}\ntouchVersion:3\nautoJump:{}\ntoggleSprint:{}\ntoggleSneak:{}\n",
      sensitivity, invertMouse, touchSensitivity, touchButtonScale, touchOpacity, touchSmoothing, touchDeadzone, touchCurve,
      floatingJoystick, touchActionButtons, touchLeftHanded, touchHaptics, touchScheme, autoJump, toggleSprint, toggleSneak);
  for (int i = 0; i < static_cast<int>(KeyAction::Count); i++) s += std::format("key_{}:{}\n", kKeyIds[i], static_cast<int>(keys[i]));
  s += std::format("difficulty:{}\ndaylightCycle:{}\nkeepInventory:{}\nmobSpawning:{}\n", difficulty, daylightCycle,
                   keepInventory, mobSpawning);
  s += std::format("guiScale:{}\nshowFps:{}\nshowCoords:{}\nshowCrosshair:{}\nshowHand:{}\nsubtitles:{}\nperspective:{}\n",
                   guiScale, showFps, showCoords, showCrosshair, showHand, subtitles, perspective);
  s += "playerName:" + playerName + "\n";
  s += "proxyUrl:" + proxyUrl + "\n";
  s += "lastServer:" + lastServer + "\n";
  s += "skin:" + skin + "\n";
  s += "skinParts:" + std::to_string(skinParts) + "\n";
  s += "resourcePacks:";
  for (std::size_t i = 0; i < resourcePacks.size(); i++) s += (i ? "|" : "") + resourcePacks[i];
  s += "\n";
  return s;
}

void Settings::parse(std::string_view text) {
  int touchVersion = 0;  // los ajustes antiguos no lo traen: el joystick fijo (que no se mueve) vuelve a ser el de serie
  while (!text.empty()) {
    const std::size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.remove_suffix(1);
    const bool on = value == "true";
    auto f = [&](float& dst, float lo, float hi) { dst = std::clamp(toFloat(value, dst), lo, hi); };
    auto i = [&](int& dst, int lo, int hi) { dst = std::clamp(static_cast<int>(std::lround(toFloat(value, static_cast<float>(dst)))), lo, hi); };
    if (key == "renderDistance") i(renderDistance, kMinRenderDistance, kMaxRenderDistance);
    else if (key == "fov") f(fov, 30, 110);
    else if (key == "brightness") f(brightness, 0, 1);
    else if (key == "fpsLimit") i(fpsLimit, 0, 480);
    else if (key == "vsync") vsync = on;
    else if (key == "renderScale") f(renderScale, 0.25f, 1);
    else if (key == "occlusionCulling") occlusionCulling = on;
    else if (key == "adaptiveFps") i(adaptiveFps, 0, 360);
    else if (key == "smoothLighting") smoothLighting = on;
    else if (key == "fancyLeaves") fancyLeaves = on;
    else if (key == "clouds") clouds = on;
    else if (key == "particles") i(particles, 0, 2);
    else if (key == "mipmaps") mipmaps = on;
    else if (key == "fog") fog = on;
    else if (key == "viewBobbing") viewBobbing = on;
    else if (key == "entityDistance") f(entityDistance, 0.5f, 5);
    else if (key == "volume") f(volume, 0, 1);
    else if (key == "volBlocks") f(volBlocks, 0, 1);
    else if (key == "volMobs") f(volMobs, 0, 1);
    else if (key == "volPlayer") f(volPlayer, 0, 1);
    else if (key == "volUi") f(volUi, 0, 1);
    else if (key == "volMusic") f(volMusic, 0, 1);
    else if (key == "sensitivity") f(sensitivity, 0, 1);
    else if (key == "invertMouse") invertMouse = on;
    else if (key == "touchSensitivity") f(touchSensitivity, 0, 1);
    else if (key == "touchButtonScale") f(touchButtonScale, 0.6f, 1.6f);
    else if (key == "touchOpacity") f(touchOpacity, 0.1f, 1);
    else if (key == "touchSmoothing") f(touchSmoothing, 0, 1);
    else if (key == "touchDeadzone") f(touchDeadzone, 0, 0.4f);
    else if (key == "touchCurve") f(touchCurve, 0, 1);
    else if (key == "floatingJoystick") floatingJoystick = on;
    else if (key == "touchActionButtons") touchActionButtons = on;
    else if (key == "touchLeftHanded") touchLeftHanded = on;
    else if (key == "touchHaptics") touchHaptics = on;
    else if (key == "touchScheme") i(touchScheme, 0, 1);
    else if (key == "touchVersion") touchVersion = std::atoi(std::string(value).c_str());
    else if (key == "autoJump") i(autoJump, -1, 1);
    else if (key == "toggleSprint") toggleSprint = on;
    else if (key == "toggleSneak") toggleSneak = on;
    else if (key == "difficulty") i(difficulty, 0, 3);
    else if (key == "daylightCycle") daylightCycle = on;
    else if (key == "keepInventory") keepInventory = on;
    else if (key == "mobSpawning") mobSpawning = on;
    else if (key == "guiScale") i(guiScale, 0, 6);
    else if (key == "showFps") showFps = on;
    else if (key == "showCoords") showCoords = on;
    else if (key == "showCrosshair") showCrosshair = on;
    else if (key == "showHand") showHand = on;
    else if (key == "subtitles") subtitles = on;
    else if (key == "perspective") i(perspective, 0, 2);
    else if (key == "lastServer") lastServer = std::string(value).substr(0, 128);
    else if (key == "skin") skin = std::string(value).substr(0, 64);
    else if (key == "skinParts") skinParts = std::clamp(std::atoi(std::string(value).c_str()), 0, 0x7F);
    else if (key == "proxyUrl") {
      if (value.rfind("ws://", 0) == 0 || value.rfind("wss://", 0) == 0) proxyUrl = std::string(value);
    }
    else if (key == "playerName") {
      std::string n;
      for (char c : value)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') n += c;
      if (n.size() >= 3 && n.size() <= 16) playerName = n;
    }
    else if (key == "resourcePacks") {
      resourcePacks.clear();
      for (std::size_t start = 0; start < value.size();) {
        const std::size_t bar = value.find('|', start);
        const std::string_view name = value.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        if (!name.empty() && name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos)
          resourcePacks.emplace_back(name);
        start = bar == std::string_view::npos ? value.size() : bar + 1;
      }
    }
    else if (key.starts_with("key_")) {
      for (int k = 0; k < static_cast<int>(KeyAction::Count); k++)
        if (key.substr(4) == kKeyIds[k]) {
          int sc = static_cast<int>(keys[k]);
          i(sc, 0, SDL_SCANCODE_COUNT - 1);
          keys[k] = static_cast<SDL_Scancode>(sc);
        }
    }
  }
  if (touchVersion < 3) floatingJoystick = false;
}

float Settings::sensitivityScale() const { return std::pow(2.0f, (sensitivity - 0.5f) * 4.0f); }
float Settings::touchSensitivityScale() const { return std::pow(2.0f, (touchSensitivity - 0.5f) * 4.0f); }

void Settings::applyPreset(int preset) {
  switch (preset) {
    case 0:  // Bajo: lo mínimo para ir fluido en cualquier sitio
      renderDistance = 6; renderScale = 0.75f; smoothLighting = false; fancyLeaves = false; clouds = false;
      particles = 2; mipmaps = false; entityDistance = 0.75f;
      break;
    case 1:
      renderDistance = 8; renderScale = 1.0f; smoothLighting = true; fancyLeaves = false; clouds = true;
      particles = 1; mipmaps = true; entityDistance = 1.0f;
      break;
    case 2:
      renderDistance = 12; renderScale = 1.0f; smoothLighting = true; fancyLeaves = true; clouds = true;
      particles = 0; mipmaps = true; entityDistance = 1.0f;
      break;
    default:  // Ultra
      renderDistance = 20; renderScale = 1.0f; smoothLighting = true; fancyLeaves = true; clouds = true;
      particles = 0; mipmaps = true; entityDistance = 2.0f;
      break;
  }
}

int Settings::matchingPreset() const {
  for (int p = 0; p < 4; p++) {
    Settings t = *this;
    t.applyPreset(p);
    if (t.renderDistance == renderDistance && t.renderScale == renderScale && t.smoothLighting == smoothLighting &&
        t.fancyLeaves == fancyLeaves && t.clouds == clouds && t.particles == particles && t.mipmaps == mipmaps &&
        t.entityDistance == entityDistance)
      return p;
  }
  return -1;
}

#ifdef __EMSCRIPTEN__
EM_JS(char*, mcw_js_load_settings, (), {
  let s = "";
  try { s = localStorage.getItem('mcweb.options') || ""; } catch (e) {}
  return stringToNewUTF8(s);
});
EM_JS(void, mcw_js_save_settings, (const char* text), {
  try { localStorage.setItem('mcweb.options', UTF8ToString(text)); } catch (e) {}
});

std::string loadSettingsText() {
  char* p = mcw_js_load_settings();
  std::string s = p ? p : "";
  free(p);
  return s;
}
void saveSettingsText(const std::string& text) { mcw_js_save_settings(text.c_str()); }
#else
std::string loadSettingsText() { return fs::readText(fs::userDataDir() / "options.txt").value_or(""); }
void saveSettingsText(const std::string& text) { fs::writeFile(fs::userDataDir() / "options.txt", text.data(), text.size()); }
#endif

}  // namespace mcw
