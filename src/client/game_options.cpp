// Pantalla de Ajustes: páginas (Gráficos, Sonido, Controles, Teclas, Juego, Interfaz), cómo se
// manejan con ratón, dedo y teclado, y cómo se aplica cada ajuste al resto del juego.
#include <algorithm>
#include <cmath>
#include <format>

#include "client/audio.h"
#include "client/game.h"
#include "client/gl.h"
#include "client/particles.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "game/session.h"

namespace mcw {
namespace {

const char* yesNo(bool b) { return b ? "Sí" : "No"; }

constexpr int kFpsSteps[] = {30, 60, 75, 90, 120, 144, 165, 240, 0};  // 0 = sin límite
constexpr int kFpsStepCount = static_cast<int>(std::size(kFpsSteps));

int fpsStepIndex(int limit) {
  for (int i = 0; i < kFpsStepCount; i++)
    if (kFpsSteps[i] == limit) return i;
  return kFpsStepCount - 1;
}

std::string percent(float v) { return std::format("{}%", static_cast<int>(std::lround(v * 100.0f))); }

}  // namespace

std::string Game::optionTitle() const {
  switch (optPage_) {
    case OptPage::Graphics: return "Ajustes de gráficos";
    case OptPage::Sound: return "Música y sonidos";
    case OptPage::Controls: return "Controles";
    case OptPage::Keys: return "Teclas";
    case OptPage::Game: return "Ajustes de la partida";
    case OptPage::Interface: return "Interfaz";
    default: return "Ajustes";
  }
}

std::vector<OptionItem> Game::optionItems() {
  std::vector<OptionItem> v;
  Settings& s = settings_;  // (las lambdas capturan settings_ directamente)
  auto header = [&](std::string t) {
    OptionItem i;
    i.type = OptionItem::Type::Header;
    i.text = [t] { return t; };
    v.push_back(std::move(i));
  };
  auto button = [&](std::function<std::string()> text, std::function<void()> press, bool wide = false, bool enabled = true) {
    OptionItem i;
    i.text = std::move(text);
    i.press = std::move(press);
    i.wide = wide;
    i.enabled = enabled;
    v.push_back(std::move(i));
  };
  // Los elementos viven más que esta función: guardan punteros a los campos de settings_
  auto toggle = [&](std::string label, bool& ref) {
    bool* p = &ref;
    button([label, p] { return label + ": " + yesNo(*p); }, [p] { *p = !*p; });
  };
  auto cycle = [&](std::string label, int& ref, std::vector<std::string> names, int first = 0) {
    int* p = &ref;
    button([label, p, names, first] { return label + ": " + names[std::clamp(*p - first, 0, static_cast<int>(names.size()) - 1)]; },
           [p, n = static_cast<int>(names.size()), first] { *p = first + (*p - first + 1) % n; });
  };
  auto slider = [&](std::function<std::string()> text, std::function<float()> get, std::function<void(float)> set, bool wide = false) {
    OptionItem i;
    i.type = OptionItem::Type::Slider;
    i.text = std::move(text);
    i.get = std::move(get);
    i.set = std::move(set);
    i.wide = wide;
    v.push_back(std::move(i));
  };
  auto volume = [&](std::string label, float& ref, bool wide = false) {
    float* p = &ref;
    slider([label, p] { return label + ": " + (*p <= 0.001f ? std::string("No") : percent(*p)); }, [p] { return *p; },
           [p](float x) { *p = std::round(x * 100.0f) / 100.0f; }, wide);
  };
  auto page = [&](std::string label, OptPage p) { button([label] { return label; }, [this, p] { openOptionPage(p); }); };

  switch (optPage_) {
    case OptPage::Main:
      page("Gráficos...", OptPage::Graphics);
      page("Música y sonidos...", OptPage::Sound);
      page("Controles...", OptPage::Controls);
      page("Teclas...", OptPage::Keys);
      page("Partida...", OptPage::Game);
      page("Interfaz...", OptPage::Interface);
      header("Rápido");
      slider([&s = settings_] { return std::format("Distancia: {} chunks", s.renderDistance); },
             [&s = settings_] { return (s.renderDistance - Settings::kMinRenderDistance) / static_cast<float>(Settings::kMaxRenderDistance - Settings::kMinRenderDistance); },
             [&s = settings_](float x) { s.renderDistance = Settings::kMinRenderDistance + static_cast<int>(std::lround(x * (Settings::kMaxRenderDistance - Settings::kMinRenderDistance))); });
      slider([&s = settings_] { return s.fov >= 109.5f ? std::string("Campo de visión: Quake Pro") : std::format("Campo de visión: {}", static_cast<int>(s.fov)); },
             [&s = settings_] { return (s.fov - 30.0f) / 80.0f; }, [&s = settings_](float x) { s.fov = std::round(30.0f + x * 80.0f); });
      volume("Volumen general", s.volume);
      if (inWorld_) page("Partida (dificultad, reglas)...", OptPage::Game);
      else cycle("Dificultad", s.difficulty, {"Pacífica", "Fácil", "Normal", "Difícil"});
      button([this] { return confirmReset_ ? std::string("Pulsa otra vez para restablecer todo") : std::string("Restablecer todos los ajustes"); },
             [this] {
               if (!confirmReset_) {
                 confirmReset_ = true;
                 return;
               }
               confirmReset_ = false;
               settings_ = Settings();
               if (touch_.active()) settings_.renderDistance = 10;
             },
             true);
      break;

    case OptPage::Graphics: {
      header("Calidad");
      button([&s = settings_] {
        static const char* names[] = {"Baja", "Media", "Alta", "Ultra"};
        const int p = s.matchingPreset();
        return std::string("Calidad: ") + (p < 0 ? "Personalizada" : names[p]);
      },
             [&s = settings_] { s.applyPreset((s.matchingPreset() + 1) % 4); }, true);
      slider([&s = settings_] { return std::format("Distancia: {} chunks", s.renderDistance); },
             [&s = settings_] { return (s.renderDistance - Settings::kMinRenderDistance) / static_cast<float>(Settings::kMaxRenderDistance - Settings::kMinRenderDistance); },
             [&s = settings_](float x) { s.renderDistance = Settings::kMinRenderDistance + static_cast<int>(std::lround(x * (Settings::kMaxRenderDistance - Settings::kMinRenderDistance))); });
      slider([&s = settings_] { return "Resolución 3D: " + percent(s.renderScale); }, [&s = settings_] { return (s.renderScale - 0.25f) / 0.75f; },
             [&s = settings_](float x) { s.renderScale = std::round((0.25f + x * 0.75f) * 20.0f) / 20.0f; });
      slider([&s = settings_] { return s.fpsLimit == 0 ? std::string("FPS máximos: Sin límite") : std::format("FPS máximos: {}", s.fpsLimit); },
             [&s = settings_] { return fpsStepIndex(s.fpsLimit) / static_cast<float>(kFpsStepCount - 1); },
             [&s = settings_](float x) { s.fpsLimit = kFpsSteps[std::clamp(static_cast<int>(std::lround(x * (kFpsStepCount - 1))), 0, kFpsStepCount - 1)]; });
#ifdef __EMSCRIPTEN__
      button([] { return std::string("VSync: la del navegador"); }, [] {}, false, false);
#else
      toggle("VSync", s.vsync);
#endif
      slider([&s = settings_] { return s.fov >= 109.5f ? std::string("Campo de visión: Quake Pro") : std::format("Campo de visión: {}", static_cast<int>(s.fov)); },
             [&s = settings_] { return (s.fov - 30.0f) / 80.0f; }, [&s = settings_](float x) { s.fov = std::round(30.0f + x * 80.0f); });
      slider([&s = settings_] {
        if (s.brightness <= 0.005f) return std::string("Brillo: Sombrío");
        if (s.brightness >= 0.995f) return std::string("Brillo: Brillante");
        return "Brillo: +" + percent(s.brightness);
      },
             [&s = settings_] { return s.brightness; }, [&s = settings_](float x) { s.brightness = x; });
      header("Mundo");
      toggle("Luz suave", s.smoothLighting);
      button([&s = settings_] { return std::string("Hojas: ") + (s.fancyLeaves ? "Detalladas" : "Rápidas"); }, [&s = settings_] { s.fancyLeaves = !s.fancyLeaves; });
      toggle("Nubes", s.clouds);
      toggle("Niebla", s.fog);
      toggle("Mipmaps", s.mipmaps);
      cycle("Partículas", s.particles, {"Todas", "Menos", "Mínimas"});
      slider([&s = settings_] { return "Criaturas visibles: " + percent(s.entityDistance); }, [&s = settings_] { return (s.entityDistance - 0.5f) / 4.5f; },
             [&s = settings_](float x) { s.entityDistance = std::round((0.5f + x * 4.5f) * 4.0f) / 4.0f; });
      toggle("Balanceo al andar", s.viewBobbing);
      break;
    }

    case OptPage::Sound:
      volume("Volumen general", s.volume, true);
      volume("Música", s.volMusic);
      volume("Bloques", s.volBlocks);
      volume("Criaturas", s.volMobs);
      volume("Jugador", s.volPlayer);
      volume("Interfaz", s.volUi);
      toggle("Subtítulos", s.subtitles);
      break;

    case OptPage::Controls:
      header("Ratón");
      slider([&s = settings_] { return "Sensibilidad: " + percent(s.sensitivityScale()); }, [&s = settings_] { return s.sensitivity; },
             [&s = settings_](float x) { s.sensitivity = x; });
      toggle("Invertir ratón", s.invertMouse);
      header("Movimiento");
      button([&s = settings_] { return std::format("Correr: {}", s.toggleSprint ? "Alternar" : "Mantener"); }, [&s = settings_] { s.toggleSprint = !s.toggleSprint; });
      button([&s = settings_] { return std::format("Agacharse: {}", s.toggleSneak ? "Alternar" : "Mantener"); }, [&s = settings_] { s.toggleSneak = !s.toggleSneak; });
      cycle("Salto automático", s.autoJump, {"Solo táctil", "No", "Sí"}, -1);
      page("Teclas...", OptPage::Keys);
      header("Pantalla táctil");
      cycle("Puntería", s.touchScheme, {"Mira central", "Tocar para apuntar"});
      slider([&s = settings_] { return "Sensibilidad: " + percent(s.touchSensitivityScale()); }, [&s = settings_] { return s.touchSensitivity; },
             [&s = settings_](float x) { s.touchSensitivity = x; });
      slider([&s = settings_] { return "Suavizado de la cámara: " + percent(s.touchSmoothing); }, [&s = settings_] { return s.touchSmoothing; },
             [&s = settings_](float x) { s.touchSmoothing = std::round(x * 20.0f) / 20.0f; });
      button([&s = settings_] { return std::string("Joystick: ") + (s.floatingJoystick ? "Flotante" : "Fijo"); },
             [&s = settings_] { s.floatingJoystick = !s.floatingJoystick; });
      slider([&s = settings_] { return "Zona muerta del joystick: " + percent(s.touchDeadzone); }, [&s = settings_] { return s.touchDeadzone / 0.4f; },
             [&s = settings_](float x) { s.touchDeadzone = std::round(x * 0.4f * 100.0f) / 100.0f; });
      slider([&s = settings_] { return "Curva del joystick: " + percent(s.touchCurve); }, [&s = settings_] { return s.touchCurve; },
             [&s = settings_](float x) { s.touchCurve = std::round(x * 20.0f) / 20.0f; });
      toggle("Botones de atacar y usar", s.touchActionButtons);
      toggle("Modo zurdo", s.touchLeftHanded);
      toggle("Vibración", s.touchHaptics);
      slider([&s = settings_] { return "Tamaño de los botones: " + percent(s.touchButtonScale); }, [&s = settings_] { return (s.touchButtonScale - 0.6f) / 1.0f; },
             [&s = settings_](float x) { s.touchButtonScale = std::round((0.6f + x) * 20.0f) / 20.0f; });
      slider([&s = settings_] { return "Opacidad: " + percent(s.touchOpacity); }, [&s = settings_] { return (s.touchOpacity - 0.1f) / 0.9f; },
             [&s = settings_](float x) { s.touchOpacity = std::round((0.1f + x * 0.9f) * 20.0f) / 20.0f; });
      break;

    case OptPage::Keys:
      for (int k = 0; k < static_cast<int>(KeyAction::Count); k++) {
        button([this, k] {
          const std::string name = keyActionName(static_cast<KeyAction>(k));
          if (waitingKey_ == k) return name + ": > ? <";
          // Tecla repetida en otra acción: se avisa
          const SDL_Scancode sc = settings_.keys[k];
          bool clash = false;
          for (int o = 0; o < static_cast<int>(KeyAction::Count); o++) clash |= o != k && sc != SDL_SCANCODE_UNKNOWN && settings_.keys[o] == sc;
          return name + ": " + keyName(sc) + (clash ? " (!)" : "");
        },
               [this, k] { waitingKey_ = k; });
      }
      button([] { return std::string("Restablecer teclas"); }, [this] { settings_.resetKeys(); waitingKey_ = -1; }, true);
      break;

    case OptPage::Game: {
      if (!inWorld_) {
        // Fuera de un mundo: lo que tendrán los mundos nuevos
        cycle("Dificultad (mundos nuevos)", s.difficulty, {"Pacífica", "Fácil", "Normal", "Difícil"});
        break;
      }
      const bool cheats = level_.allowCommands || !save_;
      button([this] { return std::string("Modo: ") + (session_->player().creative() ? "Creativo" : "Supervivencia"); },
             [this] { session_->setMode(session_->player().creative() ? GameMode::Survival : GameMode::Creative); }, false, cheats);
      button([this] {
        static const char* names[] = {"Pacífica", "Fácil", "Normal", "Difícil"};
        return std::string("Dificultad: ") + names[std::clamp(level_.difficulty, 0, 3)] + (level_.difficultyLocked ? " (bloqueada)" : "");
      },
             [this] {
               level_.difficulty = (level_.difficulty + 1) % 4;
               applyLevelRules();
             },
             false, !level_.difficultyLocked && !level_.hardcore);
      button([this] { return std::string("Bloquear dificultad: ") + yesNo(level_.difficultyLocked); },
             [this] { level_.difficultyLocked = true; }, false, !level_.difficultyLocked && !level_.hardcore);
      auto rule = [&](std::string label, std::string name, bool def) {
        button([this, label, name, def] { return label + ": " + yesNo(level_.ruleBool(name, def)); },
               [this, name, def] {
                 level_.gameRules[name] = level_.ruleBool(name, def) ? "false" : "true";
                 applyLevelRules();
               },
               false, cheats);
      };
      rule("Ciclo de día y noche", "doDaylightCycle", true);
      rule("Aparecen criaturas", "doMobSpawning", true);
      rule("Conservar inventario", "keepInventory", false);
      rule("Regeneración natural", "naturalRegeneration", true);
      rule("Criaturas rompen bloques", "mobGriefing", true);
      rule("Mensajes de muerte", "showDeathMessages", true);
      slider([this] {
        const int tod = static_cast<int>(std::fmod(worldTime_ + 6000.0, 24000.0));  // 0 = medianoche
        return std::format("Hora: {:02}:{:02}", tod / 1000, (tod % 1000) * 60 / 1000);
      },
             [this] { return static_cast<float>(std::fmod(worldTime_, 24000.0) / 24000.0); },
             [this](float x) {
               if (level_.allowCommands || !save_) worldTime_ = std::floor(worldTime_ / 24000.0) * 24000.0 + std::min(x, 0.9999f) * 24000.0;
             });
      button([this] { return std::format("Mundo: {}   Semilla: {}", level_.name, static_cast<i64>(level_.seed)); }, [] {}, true, false);
      break;
    }

    case OptPage::Interface:
      button([this] {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return settings_.guiScale == 0 ? std::format("Escala de interfaz: Auto ({})", Ui::autoScale(w, h))
                                      : std::format("Escala de interfaz: {}", guiScaleFor(w, h));
      },
             [this] {
               int w = 0, h = 0;
               SDL_GetWindowSizeInPixels(window_, &w, &h);
               settings_.guiScale = settings_.guiScale >= Ui::autoScale(w, h) ? 0 : settings_.guiScale + 1;
             });
      cycle("Cámara", s.perspective, {"Primera persona", "Detrás", "Delante"});
      toggle("Mostrar la mano", s.showHand);
      toggle("Punto de mira", s.showCrosshair);
      toggle("Mostrar FPS", s.showFps);
      toggle("Coordenadas", s.showCoords);
      toggle("Subtítulos", s.subtitles);
      button([this] { return std::string("Pantalla completa: ") + yesNo((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0); },
             [this] { SDL_SetWindowFullscreen(window_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) == 0); });
      button([this] { return std::string("Datos de depuración (F3): ") + yesNo(opt_.showDebug); }, [this] { opt_.showDebug = !opt_.showDebug; }, true);
      break;
  }
  return v;
}

void Game::openOptionPage(OptPage p) {
  optPage_ = p;
  optScroll_ = 0;
  optDrag_ = -1;
  waitingKey_ = -1;
  confirmReset_ = false;
}

void Game::optionsBack() {
  saveSettings();
  if (waitingKey_ >= 0) {
    waitingKey_ = -1;
    return;
  }
  switch (optPage_) {
    case OptPage::Main: openScreen(inWorld_ ? Screen::Pause : Screen::Title); break;
    case OptPage::Keys: openOptionPage(OptPage::Controls); break;
    default: openOptionPage(OptPage::Main); break;
  }
}

void Game::optionsPress(glm::vec2 gui) {
  const std::vector<OptionItem> items = optionItems();
  const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_);
  const int i = optionListHit(l, items, gui.x, gui.y);
  if (i == -1) return;
  if (i == -2) {
    audio_->playFlat(Sfx::Click);
    optionsBack();
    return;
  }
  const OptionItem& it = items[i];
  if (!it.enabled) return;
  if (it.type == OptionItem::Type::Slider) {
    optDrag_ = i;
    if (it.set) it.set(optionSliderAt(l, i, gui.x));
    return;
  }
  audio_->playFlat(Sfx::Click);
  if (optPage_ == OptPage::Main && confirmReset_ && i != static_cast<int>(items.size()) - 1) confirmReset_ = false;
  if (it.press) it.press();
  saveSettings();
}

void Game::optionsDrag(glm::vec2 gui) {
  if (optDrag_ < 0) return;
  const std::vector<OptionItem> items = optionItems();
  if (optDrag_ >= static_cast<int>(items.size())) return;
  const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_);
  if (items[optDrag_].set) items[optDrag_].set(optionSliderAt(l, optDrag_, gui.x));
}

void Game::optionsRelease() {
  if (optDrag_ >= 0) saveSettings();
  optDrag_ = -1;
}

void Game::optionsScroll(float guiPixels) {
  const std::vector<OptionItem> items = optionItems();
  const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_ + guiPixels);
  optScroll_ = l.scroll;
}

void Game::optionsKey(SDL_Scancode sc) {
  if (waitingKey_ < 0) return;
  // Esc cancela; Supr/Retroceso deja la acción sin tecla
  if (sc != SDL_SCANCODE_ESCAPE)
    settings_.keys[waitingKey_] = (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) ? SDL_SCANCODE_UNKNOWN : sc;
  waitingKey_ = -1;
  saveSettings();
}

void Game::saveSettings() { saveSettingsText(settings_.serialize()); }

bool Game::keyHeld(KeyAction a) const {
  const SDL_Scancode sc = settings_.keys[static_cast<int>(a)];
  if (sc == SDL_SCANCODE_UNKNOWN) return false;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  return keys[sc];
}

void Game::applySettings() {
  const Settings& s = settings_;
  terrain_->setMeshFlags(static_cast<u8>((s.smoothLighting ? kMeshSmoothLight : 0) | (s.fancyLeaves ? kMeshFancyLeaves : 0)));
  terrain_->setMipmaps(s.mipmaps);
  particles_->setLevel(s.particles);
  audio_->setVolume(s.volume);
  audio_->setCategoryVolume(SoundCategory::Blocks, s.volBlocks);
  audio_->setCategoryVolume(SoundCategory::Mobs, s.volMobs);
  audio_->setCategoryVolume(SoundCategory::Players, s.volPlayer);
  audio_->setCategoryVolume(SoundCategory::Ui, s.volUi);
  audio_->setCategoryVolume(SoundCategory::Music, s.volMusic);
  {
    TouchOptions t;
    t.buttonScale = s.touchButtonScale;
    t.opacity = s.touchOpacity;
    t.deadzone = s.touchDeadzone;
    t.curve = s.touchCurve;
    t.floatingStick = s.floatingJoystick;
    t.actionButtons = s.touchActionButtons;
    t.leftHanded = s.touchLeftHanded;
    t.haptics = s.touchHaptics;
    t.scheme = s.touchScheme == 1 ? TouchScheme::Pocket : TouchScheme::Crosshair;
    touch_.setOptions(t);
  }
#ifndef __EMSCRIPTEN__
  if (!appliedOnce_ || s.vsync != applied_.vsync) SDL_GL_SetSwapInterval(s.vsync ? 1 : 0);
#endif
  applied_ = s;
  appliedOnce_ = true;
}

}  // namespace mcw
