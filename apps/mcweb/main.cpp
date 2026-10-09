// Punto de entrada de MC-WEB. Usa los "main callbacks" de SDL3: el mismo código sirve para
// Linux, Windows y el navegador (Emscripten), donde el bucle lo lleva el propio navegador.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "client/game.h"
#include "client/gl.h"
#include "core/log.h"
#include "core/random.h"

#if defined(_WIN32)
// Portátiles con dos GPU (Intel o AMD integrada + NVIDIA o AMD dedicada): sin esto Windows suele
// abrir los juegos de OpenGL en la integrada. Los controladores buscan estos dos símbolos en el .exe.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

mcw::Game* gGame = nullptr;  // (solo para las funciones de prueba del navegador)

struct App {
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  std::unique_ptr<mcw::Game> game;
};

void printHelp() {
  std::puts(
      "MC-WEB - reimplementacion open source de Minecraft 1.8\n"
      "Uso: mcweb [opciones]\n"
      "  --jar RUTA            jar de Minecraft 1.8.x (por defecto se busca en .minecraft)\n"
      "  --cc0                 usar solo el pack libre integrado\n"
      "  --seed SEMILLA        semilla del mundo (numero o texto)\n"
      "  --rd N                distancia de render en chunks (2-32, por defecto 8)\n"
      "  --pos X,Y,Z           posicion inicial de la camara\n"
      "  --yaw G --pitch G     orientacion inicial en grados\n"
      "  --time T              hora en ticks (6000 = mediodia)  --freeze-time\n"
      "  --size AxB            tamano de ventana\n"
      "  --debug               mostrar la pantalla de depuracion (F3)\n"
      "  --screenshot RUTA     guardar una captura cuando el mundo cargue  --exit: salir despues\n"
      "  --threads N           hilos de trabajo  --no-vsync\n"
      "  --log-perf            escribir fps y tiempos por fase cada segundo\n"
      "  --bench N             medir N segundos con el mundo cargado (resumen al final)  --bench-spin: dando vueltas\n"
      "  --no-occlusion        dibujar todo lo que cae en la piramide de vision (sin ocultar lo tapado)\n"
      "  --auto-fps N          rendimiento automatico: mantener N fps bajando la resolucion y la distancia (0 = no)\n"
      "  --fixed-cam           cámara quieta en --pos, sin interfaz ni criaturas (capturas comparables entre versiones)\n"
      "  --workers N           Web Workers en el build web sin hilos (0 = ninguno)\n"
      "  --touch               mostrar los controles tactiles desde el inicio\n"
      "  --mode survival|creative  modo de juego (por defecto supervivencia)\n"
      "  --world CARPETA       abrir directamente un mundo guardado\n"
      "Sin semilla ni modo se empieza en el menu principal.");
}

bool parseDouble(std::string_view s, double& out) {
  try {
    std::size_t n = 0;
    out = std::stod(std::string(s), &n);
    return n == s.size();
  } catch (...) {
    return false;
  }
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
  mcw::GameOptions opt;
#if defined(__EMSCRIPTEN__)
  opt.canQuit = false;
#endif
  int width = 1280, height = 720;
  bool vsync = true;
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    auto next = [&]() -> std::string_view { return i + 1 < argc ? std::string_view(argv[++i]) : std::string_view(); };
    double d = 0;
    if (a == "--help" || a == "-h") { printHelp(); return SDL_APP_SUCCESS; }
    else if (a == "--jar") opt.jarPath = next();
    else if (a == "--cc0") opt.forceCC0 = true;
    else if (a == "--seed") {
      const std::string_view s = next();
      long long v = 0;
      auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
      opt.seed = (ec == std::errc() && p == s.data() + s.size()) ? static_cast<mcw::u64>(v) : mcw::seedFromString(s);
      opt.hasSeed = true;
    } else if (a == "--rd" && parseDouble(next(), d)) {
      opt.renderDistance = std::clamp(static_cast<int>(d), 2, 32);
      opt.renderDistanceSet = true;
    }
    else if (a == "--pos") {
      double x, y, z;
      const std::string s(next());
      if (std::sscanf(s.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) opt.startPos = glm::dvec3(x, y, z);
    } else if (a == "--yaw" && parseDouble(next(), d)) opt.yawDeg = static_cast<float>(d);
    else if (a == "--pitch" && parseDouble(next(), d)) opt.pitchDeg = static_cast<float>(d);
    else if (a == "--time" && parseDouble(next(), d)) opt.time = d;
    else if (a == "--freeze-time") opt.freezeTime = true;
    else if (a == "--gamma" && parseDouble(next(), d)) {
      opt.gamma = static_cast<float>(d);
      opt.gammaSet = true;
    }
    else if (a == "--debug") opt.showDebug = true;
    else if (a == "--screenshot") opt.screenshotPath = next();
    else if (a == "--screenshot-delay" && parseDouble(next(), d)) opt.screenshotDelay = d;
    else if (a == "--exit") opt.exitAfterScreenshot = true;
    else if (a == "--threads" && parseDouble(next(), d)) opt.threads = static_cast<int>(d);
    else if (a == "--workers" && parseDouble(next(), d)) opt.webWorkers = static_cast<int>(d);
    else if (a == "--log-perf") opt.logPerf = true;
    else if (a == "--bench" && parseDouble(next(), d)) opt.benchSeconds = std::max(0.5, d);
    else if (a == "--bench-spin") opt.benchSpin = true;
    else if (a == "--fixed-cam") opt.fixedCam = true;
    else if (a == "--auto-fps" && parseDouble(next(), d)) opt.adaptiveFps = static_cast<int>(d);
    else if (a == "--no-occlusion") opt.noOcclusion = true;
    else if (a == "--no-vsync") {
      vsync = false;
      opt.noVsync = true;
    }
    else if (a == "--touch") opt.touch = true;
    else if (a == "--mode") {
      const std::string_view m = next();
      opt.mode = (m == "creative" || m == "creativo" || m == "1") ? mcw::GameMode::Creative : mcw::GameMode::Survival;
    } else if (a == "--demo") opt.demo = next();
    else if (a == "--world") opt.world = next();
    else if (a == "--size") {
      const std::string s(next());
      std::sscanf(s.c_str(), "%dx%d", &width, &height);
    } else {
      mcw::log::warn("opción desconocida: {}", a);
    }
  }

  // Con semilla, posición, modo o una demo de juego se entra directamente en un mundo de prueba;
  // si no, se empieza en el menú principal. Las demos de menús (titulo, mundos, crear) no entran.
  {
    const bool menuDemo = opt.demo == "titulo" || opt.demo == "mundos" || opt.demo == "crear" || opt.demo == "nuevo" || opt.demo == "packs" || opt.demo == "recarga" || opt.demo == "multi" || opt.demo == "skins" || opt.demo == "capas" || opt.demo.rfind("unirse:", 0) == 0 || opt.demo.rfind("opciones", 0) == 0;
    bool modeGiven = false;
    for (int i = 1; i < argc; i++) modeGiven |= std::string_view(argv[i]) == "--mode" || std::string_view(argv[i]) == "--pos";
    opt.directStart = opt.world.empty() && !menuDemo && (opt.hasSeed || modeGiven || !opt.demo.empty());
  }

  // Los toques llegan como eventos de dedo; no queremos clics de ratón sintéticos
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    mcw::log::error("SDL_Init: {}", SDL_GetError());
    return SDL_APP_FAILURE;
  }
#if defined(__EMSCRIPTEN__)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  auto app = std::make_unique<App>();
  app->window = SDL_CreateWindow("MC-WEB", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!app->window) {
    mcw::log::error("no se pudo crear la ventana: {}", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  app->gl = SDL_GL_CreateContext(app->window);
  if (!app->gl) {
    mcw::log::error("no se pudo crear el contexto OpenGL 3.3: {}", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_GL_MakeCurrent(app->window, app->gl);
#if !defined(__EMSCRIPTEN__)
  if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
    mcw::log::error("no se pudieron cargar las funciones de OpenGL");
    return SDL_APP_FAILURE;
  }
#endif
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);

  app->game = std::make_unique<mcw::Game>(opt);
  gGame = app->game.get();
  try {
    if (!app->game->init(app->window)) return SDL_APP_FAILURE;
  } catch (const std::exception& e) {
    mcw::log::error("error al iniciar: {}", e.what());
    return SDL_APP_FAILURE;
  }
  *appstate = app.release();
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  auto* app = static_cast<App*>(appstate);
  if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
  app->game->handleEvent(*event);
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
  auto* app = static_cast<App*>(appstate);
  if (!app->game->iterate()) return SDL_APP_SUCCESS;
  if (app->game->rendered()) {
    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GL_SwapWindow(app->window);
    app->game->addPresentMs(static_cast<double>(SDL_GetTicksNS() - t0) / 1e6);
  }
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
  auto* app = static_cast<App*>(appstate);
  if (!app) return;
  gGame = nullptr;
  app->game.reset();
  if (app->gl) SDL_GL_DestroyContext(app->gl);
  if (app->window) SDL_DestroyWindow(app->window);
  delete app;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
// Para las pruebas automáticas en el navegador (Playwright): hacia dónde mira la cámara, en radianes.
extern "C" EMSCRIPTEN_KEEPALIVE double mcw_debug_yaw() { return gGame ? static_cast<double>(gGame->debugYaw()) : 0.0; }
extern "C" EMSCRIPTEN_KEEPALIVE double mcw_debug(int what) { return gGame ? gGame->debugValue(what) : 0.0; }
#endif
