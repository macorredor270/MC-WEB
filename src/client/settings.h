#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace mcw {

/// Acciones del teclado que se pueden cambiar en Ajustes > Teclas.
enum class KeyAction : int {
  Forward, Back, Left, Right, Jump, Sneak, Sprint, Inventory, Drop, Perspective, HideHud, Debug, Screenshot, Fullscreen, Count
};

/// Ajustes del jugador (pantalla "Ajustes"). Se guardan entre partidas: en options.txt de la
/// carpeta de datos (nativo) o en el almacenamiento del navegador (web).
struct Settings {
  // --- Gráficos ---
  int renderDistance = 12;     // chunks
  float fov = 70.0f;           // grados
  float brightness = 0.5f;     // 0 = oscuro, 1 = brillante
  int fpsLimit = 0;            // 0 = lo que dé la pantalla; si no, fps máximos
  bool vsync = true;           // (nativo)
  float renderScale = 1.0f;    // resolución del mundo en 3D (0,5..1)
  bool smoothLighting = true;  // luz suave y oclusión ambiental
  bool fancyLeaves = true;     // hojas detalladas (rápidas: no se ven las hojas de dentro)
  bool clouds = true;
  int particles = 0;           // 0 todas, 1 menos, 2 mínimas
  bool mipmaps = true;
  bool fog = true;
  bool viewBobbing = true;
  float entityDistance = 1.0f; // multiplicador de la distancia a la que se ven las criaturas

  // --- Sonido ---
  float volume = 1.0f;         // general
  float volBlocks = 1.0f, volMobs = 1.0f, volPlayer = 1.0f, volUi = 1.0f, volMusic = 0.5f;

  // --- Controles ---
  float sensitivity = 0.5f;    // ratón (0,5 = normal)
  bool invertMouse = false;
  float touchSensitivity = 0.5f;
  float touchButtonScale = 1.0f;  // tamaño de los botones táctiles
  float touchOpacity = 0.7f;
  bool floatingJoystick = false;  // el joystick aparece donde pones el pulgar
  int autoJump = -1;              // -1 = según el dispositivo (sí en táctil), 0 no, 1 sí
  bool toggleSprint = false;      // la tecla de correr alterna en vez de mantener
  bool toggleSneak = false;
  std::array<SDL_Scancode, static_cast<int>(KeyAction::Count)> keys{};

  // --- Juego ---
  int difficulty = 2;          // 0 pacífico, 1 fácil, 2 normal, 3 difícil
  bool daylightCycle = true;
  bool keepInventory = false;
  bool mobSpawning = true;

  // --- Interfaz ---
  int guiScale = 0;            // 0 = automático
  bool showFps = false;
  bool showCoords = false;
  bool showCrosshair = true;
  bool showHand = true;
  bool subtitles = false;
  int perspective = 0;         // 0 primera persona, 1 tercera (detrás), 2 tercera (delante)

  // --- Paquetes de recursos (archivos de la carpeta resourcepacks, el primero arriba) ---
  std::vector<std::string> resourcePacks;
  /// Nombre del jugador (se ve en multijugador; en modo offline decide su UUID).
  std::string playerName = "Jugador";
  /// Navegador: proxy WebSocket para entrar a servidores (mcweb-wsproxy).
  std::string proxyUrl = "ws://localhost:25500";

  Settings();
  std::string serialize() const;
  void parse(std::string_view text);  // clave:valor por línea; lo desconocido se ignora
  void resetKeys();

  /// Multiplicador de la sensibilidad (0,25x .. 4x).
  float sensitivityScale() const;
  float touchSensitivityScale() const;
  /// Presets de gráficos: 0 bajo, 1 medio, 2 alto, 3 ultra.
  void applyPreset(int preset);
  /// Preset con el que coinciden los gráficos actuales, o -1 (personalizado).
  int matchingPreset() const;

  static constexpr int kMinRenderDistance = 2, kMaxRenderDistance = 32;
};

const char* keyActionName(KeyAction a);
SDL_Scancode defaultKey(KeyAction a);
/// Nombre de una tecla en español ("Espacio", "Mayús izq."...).
std::string keyName(SDL_Scancode sc);

/// Lee y guarda el texto de las opciones donde toque según la plataforma.
std::string loadSettingsText();
void saveSettingsText(const std::string& text);

}  // namespace mcw
