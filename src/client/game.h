#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "client/camera.h"
#include "client/enchant_books.h"
#include "client/entity_renderer.h"
#include "client/hud.h"
#include "client/perf.h"
#include "client/quality.h"
#include "client/settings.h"
#include "client/skin_store.h"
#include "client/touch.h"
#include "core/types.h"
#include "game/player.h"
#include "game/session.h"
#include "net/client.h"
#include "net/server.h"
#include "net/socket.h"
#include "save/world_save.h"

namespace mcw {

class JobSystem;
class PackStack;
class BlockTextures;
class BlockModels;
class ItemModels;
class Colormaps;
class Terrain;
struct FogParams;
class Environment;
class Ui;
class ItemRenderer;
class EntityRenderer;
class ParticleSystem;
class Audio;
class GameSession;
class WorkerPool;
class Pack;
class Music;

struct GameOptions {
  std::string jarPath;         // vacío = buscar en .minecraft
  bool forceCC0 = false;       // usar solo el pack libre
  bool noDownload = false;     // no descargar los recursos oficiales aunque falten (--sin-red)
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
  bool noVsync = false;        // --no-vsync: manda sobre el ajuste guardado
  bool canQuit = true;         // en web no hay "salir"
  bool logPerf = false;        // escribir fps y tiempo de CPU cada segundo (pruebas de rendimiento)
  double benchSeconds = 0;     // --bench N: mide N segundos con el mundo cargado, escribe el resumen y sale
  bool benchSpin = false;      // --bench-spin: durante la medición, la cámara da vueltas (90 grados por segundo)
  int adaptiveFps = -1;        // --auto-fps N: rendimiento automático con ese objetivo (0 = apagado; manda sobre los ajustes)
  bool noOcclusion = false;    // --no-occlusion: dibujar todo lo que cae en la pirámide de visión (comparar capturas)
  bool fixedCam = false;       // --fixed-cam: cámara quieta en --pos, sin interfaz, criaturas ni mano (capturas comparables)
  std::string demo;            // acciones automáticas para pruebas: "inventario", "crafteo"...
  bool directStart = false;    // entrar directamente en un mundo temporal (pruebas, --seed, --demo)
  std::string world;           // --world CARPETA: abrir ese mundo guardado
};

class Game {
 public:
  explicit Game(GameOptions options);
  ~Game();

  bool init(SDL_Window* window);
  void handleEvent(const SDL_Event& e);
  /// Un frame. Devuelve false para salir.
  bool iterate();
  /// false si este frame se ha saltado por el límite de FPS (no hay nada nuevo que presentar).
  bool rendered() const { return rendered_; }
  /// Tiempo de la presentación (el swap), que se mide fuera del juego.
  void addPresentMs(double ms) { perf_.addPresent(ms); }
  float debugYaw() const { return cam_.yaw; }  // hacia dónde mira la cámara (pruebas en el navegador)
  /// Valores para las pruebas en el navegador: 0 giro, 1 inclinación, 2 a 4 posición x y z, 5 agachado,
  /// 6 corriendo, 7 volando, 8 casilla elegida, 9 en el suelo, 10 pantalla abierta (0 = ninguna), 11 llamadas de
  /// dibujo del terreno, 12 quads dibujados, 13 secciones dibujadas, 14 secciones con malla
  double debugValue(int what) const;

 private:
  enum class Screen {
    None, Menu, Pause, Options, Death, Chat,
    // Fuera de la partida (game_menus.cpp)
    Title, Worlds, CreateWorld, RenameWorld, DeleteWorld, Loading, Multiplayer, AddServer, DirectConnect, Skins, SkinParts, Achievements, ResourcePacks, Message
  };
  /// Copia del inventario y la armadura del jugador, para ver qué ha cambiado un clic.
  struct InvSnapshot {
    std::array<ItemStack, PlayerInventory::kSize> slots;
    std::array<ItemStack, 4> armor;
  };
  InvSnapshot snapshotInventory() const;
  // --- Inventario del modo creativo (pestañas, búsqueda, barra de desplazamiento) ---
  bool creativeSearchActive() const;  // la pestaña de búsqueda está abierta: el teclado escribe en ella
  bool anvilNameActive() const;       // el yunque tiene un objeto: el teclado escribe su nombre
  void setAnvilName(std::string name);  // cambia el nombre escrito (y se lo cuenta al servidor)
  void selectCreativeTab(CreativeTab tab);
  void syncMenuTextInput();           // teclado de texto encendido solo mientras se busca
  void dragCreativeBar(float guiY);
  void updateMenuInertia(double dt);  // la lista del creativo sigue deslizándose al soltar el dedo
  bool creativeBarDrag_ = false;
  bool creativeStopTap_ = false;      // el dedo bajó para frenar el deslizamiento: al soltar no es un toque
  float creativeFling_ = 0;           // velocidad de la lista al soltar el dedo (píxeles de GUI por segundo, hacia abajo +)
  float creativeScrollAccum_ = 0;     // píxeles arrastrados que aún no suman una fila
  float creativeVel_ = 0;             // velocidad estimada del dedo mientras arrastra
  u64 creativeLastMoveNs_ = 0;

  void initRenderers();
  /// Vuelve a cargar texturas, modelos y todo lo que depende de ellos (al cambiar de paquetes de recursos).
  void reloadResources();
  static std::filesystem::path resourcePackDir();
  void refreshPackList();
  void drawPackScreen(glm::vec2 m);
  /// Carpeta donde se dejan los .zip de mundos para importarlos.
  static std::filesystem::path worldImportDir();
  void checkWorldImports();
  double importCheck_ = 0;
  // Logros: avisos arriba a la derecha y pantalla con el mapa
  struct Toast {
    int achievement;
    double start;
  };
  std::vector<Toast> toasts_;
  glm::vec2 achScroll_{0, 0};
  void drawToasts();
  void drawAchievementScreen(glm::vec2 m);
  const Achievements& shownAchievements() const;
  Achievements menuAchievements_;  // los del último mundo, para verlos desde el título
  std::string achWorldName_;
  bool achShowStats_ = false;

  // --- Multijugador (game_net.cpp) ---
  std::unique_ptr<net::Server> server_;
  std::unique_ptr<net::LanBroadcaster> lanBroadcaster_;
  struct OtherPlayer {
    i32 eid = 0;
    std::string name, uuid;
    u8 parts = kAllSkinParts;  // capas de su skin visibles
    u32 skinVersion = 0;       // la versión de su skin que ya está en el renderer
    glm::dvec3 pos{0}, prevPos{0};
    float yaw = 0, prevYaw = 0, pitch = 0;
    float limbSwing = 0, limbAmount = 0, prevLimbAmount = 0;
    bool sneaking = false, sitting = false;
    float swing = 0;
    std::array<i16, 4> armor{};  // armadura puesta (0 botas .. 3 casco)
  };
  std::map<i32, OtherPlayer> others_;
  std::set<std::string> peerSkins_;  // UUIDs de los jugadores con skin propia en el renderer
  /// Decodifica y registra la skin (PNG) de otro jugador; nada si no es una skin válida.
  void applyPeerSkin(const std::string& uuid, std::span<const u8> png, bool slim);
  void dropPeerSkins();
  void openToLan();
  std::string lanAddressText() const;
  void stopNet();
  void tickNet();
  void drawOtherPlayers(const Camera& view, float partial, const FogParams& fog, const std::function<glm::vec3(const glm::dvec3&)>& light);
  void drawNameTags(float partial);
  // Jugar en un servidor
  // --- Skins (game_skins.cpp) ---
  std::vector<SkinEntry> skinList_;
  int skinSelected_ = 0;
  float skinScroll_ = 0;
  bool skinDeleteArmed_ = false;
  double skinImportCheck_ = 0;
  PreparedSkin localSkin_;          // la skin con la que juegas
  std::vector<u8> localSkinPng_;    // la misma, para mandarla a los demás
  u32 localSkinVersion_ = 0;        // sube cada vez que cambia
  std::string currentSkinId() const;
  /// Carga la skin elegida (o la de serie que toque) y la registra en el renderer como "local".
  void applyLocalSkin();
  SkinRef localSkinRef() const;
  static std::filesystem::path skinImportDir();
  void openSkins();
  void closeSkins();
  void registerSkinIcons();
  void checkSkinImports();
  void chooseSkin(int index);
  std::vector<MenuButton> skinButtons() const;
  void drawSkins(glm::vec2 m);
  void skinsButton(int id);
  void skinsPress(glm::vec2 gui);

  // Pantalla Multijugador (game_multiplayer.cpp)
  struct ServerEntry {
    std::string name, address;
    std::unique_ptr<net::StatusPinger> pinger;
    std::optional<net::ServerStatus> status;
    std::string error;
  };
  std::vector<ServerEntry> servers_;
  int selectedServer_ = -1, editingServer_ = -1;
  float serverScroll_ = 0;
  u64 lastServerClick_ = 0;
  std::unique_ptr<net::LanListener> lanListener_;
  TextField playerNameField_, proxyField_, serverNameField_, serverAddrField_, directField_;
  void openMultiplayer();
  void loadServerList();
  void saveServerList() const;
  void pingServers();
  void pollServerPings();
  std::string selectedServerAddress() const;
  std::vector<MenuButton> multiplayerButtons() const;
  void drawMultiplayer(glm::vec2 m);
  void multiplayerButton(int id);
  void multiplayerPress(glm::vec2 gui);
  bool multiplayerKey(SDL_Scancode sc);
  void multiplayerText(std::string_view text);
  TextField* focusedMultiplayerField();
  std::unique_ptr<net::Client> net_;
  std::string netAddress_;
  bool netPositioned_ = false;
  /// Qué es cada entidad que cuenta el servidor.
  enum NetKind { kNetMob = 1, kNetItem = 2, kNetPlayer = 3, kNetOrb = 4, kNetCart = 5 };
  struct NetEntity {
    u32 localId = 0;
    int kind = 0;  // NetKind
    glm::dvec3 target{0};
    float yaw = 0, head = 0, pitch = 0;
  };
  std::map<i32, NetEntity> netEntities_;
  std::map<u32, i32> netMobEid_;
  std::map<u32, i32> netCartEid_;  // vagoneta local -> id de entidad del servidor
  std::map<std::string, std::string> netNames_;  // uuid -> nombre
  ChestState netChest_;
  FurnaceState netFurnace_;
  int netWindow_ = 0, netSelected_ = -1;
  bool netSneaking_ = false, netSprinting_ = false;
  void connectToServer(const std::string& address);
  void pollNet();
  void enterRemoteWorld(const net::ClientEvent& e);
  void handleNetEvent(const net::ClientEvent& e);
  void leaveRemote(const std::string& reason);
  /// `clicked`: lo que había en la casilla antes del clic (es lo que pide el protocolo, y el servidor lo compara).
  void netMenuClick(int slot, int button, bool shift, const InvSnapshot& before, const ItemStack& clicked);
  /// Creativo en un servidor: manda las casillas (inventario y armadura) que han cambiado desde `before`.
  void netCreativeSync(const InvSnapshot& before);
  bool inMenuScreen() const { return screen_ >= Screen::Title; }

  // --- Mundos (game_world.cpp) ---
  /// Abre un mundo: `folder` vacío = mundo temporal que no se guarda (pruebas, --seed).
  void enterWorld(const std::string& folder, LevelInfo level);
  /// Guarda (si hay carpeta) y vuelve al menú principal.
  void leaveWorld();
  /// Guarda el nivel, el jugador y todos los chunks pendientes.
  void saveWorld();
  void createWorldFromForm();
  void applyLevelRules();
  // --- Menús fuera de la partida (game_menus.cpp) ---
  void openScreen(Screen s);
  void drawMenuScreen(int w, int h);
  void menuPress(glm::vec2 gui, int button);
  void menuKey(SDL_Scancode sc);
  void menuText(std::string_view text);
  std::vector<MenuButton> menuButtons() const;
  void menuButton(int id);
  void drawTitleLogo();
  void drawWorldList(glm::vec2 m);
  // --- Chat y comandos (game_commands.cpp) ---
  void chatMessage(std::string text, u32 color = 0xFFFFFF);
  void runCommand(const std::string& line);
  void drawChat(bool open);
  enum class OptPage { Main, Graphics, Sound, Controls, Keys, Game, Interface };

  void loadAssets();
  glm::dvec3 findSpawn() const;
  void trySpawn();
  void gameTick();
  /// Gira la cámara con lo arrastrado por el dedo, en cada frame (con un suavizado corto que se ajusta en Ajustes).
  void applyTouchLook(double dt);
  /// Vibra el dispositivo (solo en el navegador del móvil; en escritorio no hace nada).
  void vibrate(int ms);
  void updateCamera(float partial);
  void render(int w, int h, float partial);
  void drawDebug(int w, int h);
  bool takeScreenshot(const std::string& path, int w, int h);
  void setMouseGrab(bool grab);
  void setScreen(Screen s);
  void clickScreen(int button, bool shift);
  // --- Ajustes (game_options.cpp) ---
  /// Elementos de la página de ajustes abierta.
  std::vector<OptionItem> optionItems();
  std::string optionTitle() const;
  /// Pulsar (empieza a arrastrar un deslizador o pulsa un botón), arrastrar, soltar y desplazar la lista.
  void optionsPress(glm::vec2 gui);
  void optionsDrag(glm::vec2 gui);
  void optionsRelease();
  void optionsScroll(float guiPixels);
  /// "Listo" / Esc: vuelve a la página anterior (o a la pausa).
  void optionsBack();
  void openOptionPage(OptPage p);
  /// Tecla pulsada mientras se espera una tecla nueva en Ajustes > Teclas.
  void optionsKey(SDL_Scancode sc);
  /// Lleva los ajustes a cada parte del juego (solo lo que ha cambiado cuesta algo).
  void applySettings();
  void saveSettings();
  bool keyHeld(KeyAction a) const;
  bool isKey(SDL_Scancode sc, KeyAction a) const {
    return sc != SDL_SCANCODE_UNKNOWN && settings_.keys[static_cast<int>(a)] == sc;
  }
  /// ¿El ratón está capturado de verdad? (en el navegador, Esc suelta el puntero sin avisar)
  bool mouseLocked() const;
  /// Cámara con la que se dibuja: la de los ojos o, en tercera persona, detrás o delante del jugador.
  Camera viewCamera(int w, int h) const;
  /// Framebuffer del mundo a menor resolución (Ajustes > Gráficos > Resolución 3D).
  bool bindSceneTarget(int w, int h);
  void drawSubtitles();
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
  EnchantBooks books_;  // los libros que flotan sobre las mesas de encantamientos
  double renderDt_ = 0.016;  // segundos del último frame (para animaciones que no son del tick)
  std::unique_ptr<EntityRenderer> entityRenderer_;
  std::unique_ptr<ParticleSystem> particles_;
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<GameSession> session_;
  std::unique_ptr<WorkerPool> workers_;
  std::shared_ptr<const Pack> cc0Pack_;
  int bakedLayers_ = 0;  // capas de textura justo después de hornear los modelos de bloque
  Camera cam_;
  Settings settings_, applied_;
  bool appliedOnce_ = false;
  OptPage optPage_ = OptPage::Main;
  float optScroll_ = 0;
  int optDrag_ = -1;          // deslizador que se está arrastrando
  int waitingKey_ = -1;       // acción esperando tecla nueva
  bool confirmReset_ = false;
  std::unique_ptr<Music> music_;
  TouchControls touch_;
  std::array<int, 10> netEnchant_{};  // propiedades de la mesa de encantamientos que manda el servidor
  glm::vec2 lookPending_{0};  // giro táctil por aplicar, en radianes (x = guiñada, y = cabeceo)
  Screen screen_ = Screen::None;
  u16 destroyLayer_ = 0;

  // Entrada acumulada entre ticks
  bool leftHeld_ = false, rightHeld_ = false, attackPressed_ = false, usePressed_ = false, jumpPressed_ = false;
  bool dropPressed_ = false, dropStackPressed_ = false;
  // Correr con doble toque de adelante (como en el juego), y correr/agacharse alternando
  u64 lastForwardTap_ = 0;
  bool sprintLatched_ = false, sprintToggled_ = false, sneakToggled_ = false;
  bool pointerLockSeen_ = false, hideHud_ = false, rendered_ = true;
  u64 lastRender_ = 0;
  bool motionLocked_ = false;  // el último movimiento del ratón llegó con el ratón capturado
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
  // Rendimiento automático: lo que se usa de verdad (con el control activo puede ser menos que lo elegido en Ajustes)
  int effDist_ = 12;
  float effScale_ = 1.0f;
  int userDist_ = -1;
  float userScale_ = -1;
  double adaptWarm_ = 0;
  QualityController quality_;
  void syncQuality();   // los ajustes han cambiado: manda lo elegido
  void adaptQuality();  // una vez por segundo: bajar o subir la calidad según los fps
  FramePerf perf_;      // tiempo de CPU por fase del frame
  GpuTimer gpuTimer_;   // tiempo de GPU (si el sistema lo permite)
  double benchStart_ = -1;
  void logBench();
  double frameInterval_ = 1.0 / 60.0;  // intervalo entre frames (media): da la frecuencia de la pantalla
  bool loggedLoaded_ = false;
  float swing_ = 0, fovMod_ = 1, nameTimer_ = 0, hurtFlash_ = 0, shake_ = 0;
  // Balanceo al andar: distancia andada y amplitud, por tick (se interpolan al dibujar)
  float walked_ = 0, prevWalked_ = 0, bobAmp_ = 0, prevBobAmp_ = 0, nextStep_ = 1;
  int effectTick_ = 0, lastFood_ = 20;
  // Tercera persona: el cuerpo sigue a la cabeza con retraso, y las piernas se mueven al andar
  float bodyYaw_ = 0, prevBodyYaw_ = 0, limbSwing_ = 0, limbAmount_ = 0, prevLimbAmount_ = 0;
  // Resolución 3D reducida
  unsigned sceneFbo_ = 0, sceneColor_ = 0, sceneDepth_ = 0;
  int sceneW_ = 0, sceneH_ = 0;
  struct Subtitle {
    std::string text;
    glm::dvec3 pos;
    bool positional;
    double time;
  };
  std::vector<Subtitle> subtitles_;

  // Mundo abierto
  bool inWorld_ = false;
  bool keepPlayerPos_ = false;  // mundo guardado: no buscar el suelo del spawn
  std::unique_ptr<WorldSave> save_;  // nullptr: mundo temporal
  LevelInfo level_;
  double autosaveTimer_ = 0;
  bool pendingFlush_ = false;
  u64 worldStartTicks_ = 0;

  // Menús fuera de la partida
  std::vector<WorldSummary> worlds_;
  int selectedWorld_ = -1;
  float worldScroll_ = 0;
  u64 lastWorldClick_ = 0;
  TextField nameField_, seedField_, renameField_, chatField_;
  int newMode_ = 0;           // 0 supervivencia, 1 hardcore, 2 creativo
  int newDifficulty_ = 2, newWorldType_ = 0, newFlatPreset_ = 0;
  bool newStructures_ = true, newCheats_ = false, newBonusChest_ = false;
  std::string message_, messageDetail_;
  Screen messageBack_ = Screen::Title;
  std::string splash_;
  // Paquetes de recursos
  struct PackEntry {
    std::string file, description;
  };
  std::vector<PackEntry> availablePacks_;
  std::vector<std::string> packSelection_;  // activos, el primero arriba
  double packRefresh_ = 0;
  // Chat
  struct ChatLine {
    std::string text;
    u32 color;
    double time;
  };
  std::vector<ChatLine> chat_;
  std::vector<std::string> chatHistory_;
  int chatHistoryPos_ = -1;
  u64 suppressTextUntil_ = 0;  // la letra de la tecla que abre el chat no se escribe
  bool wasInWater_ = false;
  double lastFrameDt_ = 1.0 / 60.0;
  std::string glRenderer_;
};

}  // namespace mcw
