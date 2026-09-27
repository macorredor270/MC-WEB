#pragma once
#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "client/camera.h"
#include "client/settings.h"
#include "client/touch.h"
#include "core/types.h"
#include "game/player.h"
#include "game/session.h"

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
class EntityRenderer;
class ParticleSystem;
class Audio;
class GameSession;
class WorkerPool;
class Pack;

struct GameOptions {
  std::string jarPath;         // vacío = buscar en .minecraft
  bool forceCC0 = false;       // usar solo el pack libre
  u64 seed = 0;
  bool hasSeed = false;
  int renderDistance = 12;
  bool renderDistanceSet = false;  // --rd en la línea de órdenes: manda sobre las opciones guardadas
  GameMode mode = GameMode::Survival;
  std::optional<glm::dvec3> startPos;
  std::optional<float> yawDeg, pitchDeg;
  double time = 1000;          // ticks del día
  bool freezeTime = false;
  float gamma = 0.5f;
  bool gammaSet = false;
  bool showDebug = false;
  std::string screenshotPath;  // captura automática cuando el mundo termina de cargar
  double screenshotDelay = 0.5;
  bool exitAfterScreenshot = false;
  int threads = -1;
  int webWorkers = -1;         // Web Workers en el build web sin hilos (-1 = según los núcleos, 0 = ninguno)
  bool touch = false;          // mostrar los controles táctiles desde el principio
  bool canQuit = true;         // en web no hay "salir"
  bool logPerf = false;        // escribir fps y tiempo de CPU cada segundo (pruebas de rendimiento)
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
  enum class Screen { None, Menu, Pause, Options, Death };

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
  /// Pantalla de opciones: pulsar (empieza a arrastrar un deslizador o pulsa un botón), arrastrar y soltar.
  void optionsPress(glm::vec2 gui);
  void optionsDrag(glm::vec2 gui);
  void optionsRelease();
  void saveSettings();
  int guiScaleFor(int w, int h) const;
  void handleScreenTouch(const SDL_Event& e);
  glm::dvec3 aimFromGui(glm::vec2 gui) const;
  glm::vec2 mouseGui() const;
  void pickBlock();
  void runDemo();
  /// Textura (capa) y tinte para las partículas de un bloque.
  bool particleLayer(BlockState s, const glm::ivec3& pos, u16& layer, glm::vec3& tint) const;
  /// Sonidos y partículas de lo que ha pasado en este tick.
  void tickEffects(const std::vector<SessionEvent>& events);

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
  std::unique_ptr<EntityRenderer> entityRenderer_;
  std::unique_ptr<ParticleSystem> particles_;
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<GameSession> session_;
  std::unique_ptr<WorkerPool> workers_;
  std::shared_ptr<const Pack> cc0Pack_;
  int bakedLayers_ = 0;  // capas de textura justo después de hornear los modelos de bloque
  Camera cam_;
  Settings settings_;
  int dragSlider_ = -1;  // deslizador de la pantalla de opciones que se está arrastrando
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
  // Tiempo de CPU de cada frame en el hilo principal (media y peor del último segundo)
  double cpuSum_ = 0, cpuMaxAcc_ = 0, cpuAvg_ = 0, cpuMax_ = 0;
  double frameInterval_ = 1.0 / 60.0;  // intervalo entre frames (media): da la frecuencia de la pantalla
  bool loggedLoaded_ = false;
  float swing_ = 0, fovMod_ = 1, nameTimer_ = 0, hurtFlash_ = 0, shake_ = 0;
  // Balanceo al andar: distancia andada y amplitud, por tick (se interpolan al dibujar)
  float walked_ = 0, prevWalked_ = 0, bobAmp_ = 0, prevBobAmp_ = 0, nextStep_ = 1;
  int effectTick_ = 0, lastFood_ = 20;
  bool wasInWater_ = false;
  double lastFrameDt_ = 1.0 / 60.0;
  std::string glRenderer_;
};

}  // namespace mcw
