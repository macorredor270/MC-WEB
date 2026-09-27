#pragma once
#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>

#include "client/camera.h"
#include "client/touch.h"
#include "core/types.h"

namespace mcw {

class JobSystem;
class PackStack;
class BlockTextures;
class BlockModels;
class Colormaps;
class Terrain;
class Environment;
class Ui;

struct GameOptions {
  std::string jarPath;         // vacío = buscar en .minecraft
  bool forceCC0 = false;       // usar solo el pack libre
  u64 seed = 0;
  bool hasSeed = false;
  int renderDistance = 8;
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
  void loadAssets();
  glm::dvec3 findSpawn() const;
  void updateCamera(double dt);
  void render(int w, int h);
  void drawDebug(int w, int h);
  bool takeScreenshot(const std::string& path, int w, int h);
  void setMouseGrab(bool grab);

  GameOptions opt_;
  SDL_Window* window_ = nullptr;
  std::unique_ptr<JobSystem> jobs_;
  std::unique_ptr<PackStack> packs_;
  std::unique_ptr<BlockTextures> textures_;
  std::unique_ptr<BlockModels> models_;
  std::unique_ptr<Colormaps> colors_;
  std::unique_ptr<Terrain> terrain_;
  std::unique_ptr<Environment> env_;
  std::unique_ptr<Ui> ui_;
  Camera cam_;
  TouchControls touch_;
  u64 lastTicks_ = 0;
  double tickAccum_ = 0, worldTime_ = 0, runTime_ = 0, settledAt_ = -1;
  bool grabbed_ = false, quit_ = false, screenshotDone_ = false, wantScreenshot_ = false;
  float flySpeed_ = 10.9f;
  int frames_ = 0, fps_ = 0;
  double fpsTimer_ = 0;
  std::string glRenderer_;
};

}  // namespace mcw
