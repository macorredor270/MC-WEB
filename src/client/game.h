#pragma once
#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>

#include "client/camera.h"
#include "client/touch.h"
#include "core/types.h"
#include "game/player.h"

namespace mcw {

class JobSystem;
class PackStack;
class BlockTextures;
class BlockModels;
class ItemModels;
class Colormaps;
class Terrain;
class Environment;
class Ui;
class ItemRenderer;
class GameSession;

struct GameOptions {
  std::string jarPath;         // vacío = buscar en .minecraft
  bool forceCC0 = false;       // usar solo el pack libre
  u64 seed = 0;
  bool hasSeed = false;
  int renderDistance = 8;
  GameMode mode = GameMode::Survival;
  std::optional<glm::dvec3> startPos;
  std::optional<float> yawDeg, pitchDeg;
  double time = 1000;          // ticks del día
  bool freezeTime = false;
  float gamma = 0.5f;
  bool showDebug = false;
  std::string screenshotPath;  // captura automática cuando el mundo termina de cargar
  double screenshotDelay = 0.5;
  bool exitAfterScreenshot = false;
  int threads = -1;
  bool touch = false;          // mostrar los controles táctiles desde el principio
  bool canQuit = true;         // en web no hay "salir"
  std::string demo;            // acciones automáticas para pruebas: "inventario", "crafteo"...
};

class Game {
 public:
  explicit Game(GameOptions options);
  ~Game();

  bool init(SDL_Window* window);
  void handleEvent(const SDL_Event& e);
  /// Un frame. Devuelve false para salir.
  bool iterate();

 private:
  enum class Screen { None, Menu, Pause, Death };

  void loadAssets();
  glm::dvec3 findSpawn() const;
  void trySpawn();
  void gameTick();
  void updateCamera(float partial);
  void render(int w, int h, float partial);
  void drawDebug(int w, int h);
  bool takeScreenshot(const std::string& path, int w, int h);
  void setMouseGrab(bool grab);
  void setScreen(Screen s);
  void clickScreen(int button, bool shift);
  void handleScreenTouch(const SDL_Event& e);
  glm::dvec3 aimFromGui(glm::vec2 gui) const;
  glm::vec2 mouseGui() const;
  void pickBlock();
  void runDemo();

  GameOptions opt_;
  SDL_Window* window_ = nullptr;
  std::unique_ptr<JobSystem> jobs_;
  std::unique_ptr<PackStack> packs_;
  std::unique_ptr<BlockTextures> textures_;
  std::unique_ptr<BlockModels> models_;
  std::unique_ptr<ItemModels> itemModels_;
  std::unique_ptr<Colormaps> colors_;
  std::unique_ptr<Terrain> terrain_;
  std::unique_ptr<Environment> env_;
  std::unique_ptr<Ui> ui_;
  std::unique_ptr<ItemRenderer> itemRenderer_;
  std::unique_ptr<GameSession> session_;
  Camera cam_;
  TouchControls touch_;
  Screen screen_ = Screen::None;
  u16 destroyLayer_ = 0;

  // Entrada acumulada entre ticks
  bool leftHeld_ = false, rightHeld_ = false, attackPressed_ = false, usePressed_ = false, jumpPressed_ = false;
  bool dropPressed_ = false, dropStackPressed_ = false;
  int selectSlot_ = -1;
  float mouseX_ = 0, mouseY_ = 0;  // en puntos de ventana
  bool touchAttacking_ = false;
  std::optional<SDL_FingerID> screenFinger_;  // dedo que hace de ratón en los menús
  u64 screenFingerDown_ = 0;
  glm::vec2 screenFingerStart_{0}, screenFingerLast_{0};
  bool screenFingerMoved_ = false;

  u64 lastTicks_ = 0;
  double tickAccum_ = 0, worldTime_ = 0, runTime_ = 0, settledAt_ = -1;
  bool grabbed_ = false, quit_ = false, screenshotDone_ = false, wantScreenshot_ = false, spawned_ = false;
  glm::dvec3 spawn_{0};
  int frames_ = 0, fps_ = 0, lastSelected_ = 0, demoStep_ = 0;
  double fpsTimer_ = 0;
  float swing_ = 0, bob_ = 0, fovMod_ = 1, nameTimer_ = 0, hurtFlash_ = 0;
  std::string glRenderer_;
};

}  // namespace mcw
