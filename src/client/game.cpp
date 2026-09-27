#include "client/game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>

#include "assets/cc0_pack.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/environment.h"
#include "client/gl.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "core/fs.h"
#include "core/jobs.h"
#include "core/log.h"
#include "core/random.h"
#include "data/biomes.h"

namespace mcw {

Game::Game(GameOptions options) : opt_(std::move(options)) {}

Game::~Game() {
  // Primero parar los hilos: sus trabajos apuntan al terreno y a los modelos.
  jobs_.reset();
  terrain_.reset();
  env_.reset();
  ui_.reset();
}

void Game::loadAssets() {
  packs_ = std::make_unique<PackStack>();
  packs_->pushBottom(makeCC0Pack());
  if (!opt_.forceCC0) {
    std::optional<std::filesystem::path> jar;
    if (!opt_.jarPath.empty()) jar = opt_.jarPath;
    else jar = findMinecraftJar();
    if (jar) {
      if (auto pack = ZipPack::open(*jar)) {
        log::info("usando los assets de {}", jar->string());
        packs_->pushTop(std::shared_ptr<const Pack>(std::move(pack)));
      } else {
        log::warn("no se pudo abrir {}", jar->string());
      }
    } else {
      log::info("no se encontró Minecraft 1.8 instalado: se usa el pack libre (CC0)");
    }
  }
  textures_ = std::make_unique<BlockTextures>();
  models_ = std::make_unique<BlockModels>();
  colors_ = std::make_unique<Colormaps>();
  models_->bake(*packs_, *textures_);
  textures_->load(*packs_);
  colors_->load(*packs_);
}

bool Game::init(SDL_Window* window) {
  window_ = window;
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  glRenderer_ = renderer ? renderer : "?";
  log::info("GL: {} / {}", glRenderer_, version ? version : "?");

  const int threads = opt_.threads >= 0 ? opt_.threads : JobSystem::defaultThreadCount();
  jobs_ = std::make_unique<JobSystem>(threads);
  log::info("hilos de trabajo: {}", threads);

  loadAssets();
  if (!opt_.hasSeed) {
    opt_.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count());
  }
  log::info("semilla: {}", static_cast<i64>(opt_.seed));

  terrain_ = std::make_unique<Terrain>(*jobs_, opt_.seed, MesherContext{models_.get(), colors_.get()});
  terrain_->initGL(*textures_);
  env_ = std::make_unique<Environment>();
  env_->initGL(*packs_);
  ui_ = std::make_unique<Ui>();
  ui_->initGL(*packs_);

  cam_.pos = opt_.startPos.value_or(findSpawn());
  if (opt_.yawDeg) cam_.yaw = glm::radians(*opt_.yawDeg);
  if (opt_.pitchDeg) cam_.pitch = glm::radians(*opt_.pitchDeg);
  worldTime_ = opt_.time;
  lastTicks_ = SDL_GetTicksNS();
  gl::checkErrors("Game::init");
  return true;
}

glm::dvec3 Game::findSpawn() const {
  // Espiral desde el origen hasta encontrar tierra firme (no océano ni río)
  const TerrainGenerator& gen = terrain_->generator();
  for (int r = 0; r < 64; r++) {
    for (int i = -r; i <= r; i++) {
      const std::pair<int, int> candidates[] = {{i * 16, -r * 16}, {i * 16, r * 16}, {-r * 16, i * 16}, {r * 16, i * 16}};
      for (auto [x, z] : candidates) {
        const ColumnInfo c = gen.column(x, z);
        if (c.height > TerrainGenerator::kSeaLevel + 1 && !c.river && c.biome != Biome::beach)
          return {x + 0.5, c.height + 1.62 + 8, z + 0.5};
      }
    }
  }
  return {0.5, 100, 0.5};
}

void Game::setMouseGrab(bool grab) {
  grabbed_ = grab;
  SDL_SetWindowRelativeMouseMode(window_, grab);
}

void Game::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_QUIT: quit_ = true; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (!grabbed_) setMouseGrab(true);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      if (grabbed_) {
        const float sens = 0.0022f;
        cam_.yaw -= e.motion.xrel * sens;
        cam_.pitch = std::clamp(cam_.pitch - e.motion.yrel * sens, -1.5607f, 1.5607f);
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      flySpeed_ = std::clamp(flySpeed_ * (e.wheel.y > 0 ? 1.25f : 0.8f), 1.0f, 200.0f);
      break;
    case SDL_EVENT_KEY_DOWN:
      switch (e.key.scancode) {
        case SDL_SCANCODE_ESCAPE: setMouseGrab(false); break;
        case SDL_SCANCODE_F3: opt_.showDebug = !opt_.showDebug; break;
        case SDL_SCANCODE_F2: wantScreenshot_ = true; break;
        case SDL_SCANCODE_F11: {
          const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
          SDL_SetWindowFullscreen(window_, !fs);
          break;
        }
        case SDL_SCANCODE_PAGEUP: opt_.renderDistance = std::min(32, opt_.renderDistance + 1); break;
        case SDL_SCANCODE_PAGEDOWN: opt_.renderDistance = std::max(2, opt_.renderDistance - 1); break;
        case SDL_SCANCODE_T: worldTime_ += 1000; break;
        default: break;
      }
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: setMouseGrab(false); break;
    default: break;
  }
}

void Game::updateCamera(double dt) {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  glm::vec3 move(0);
  const glm::vec3 fwd(-std::sin(cam_.yaw), 0, -std::cos(cam_.yaw));
  const glm::vec3 right(std::cos(cam_.yaw), 0, -std::sin(cam_.yaw));
  if (keys[SDL_SCANCODE_W]) move += fwd;
  if (keys[SDL_SCANCODE_S]) move -= fwd;
  if (keys[SDL_SCANCODE_D]) move += right;
  if (keys[SDL_SCANCODE_A]) move -= right;
  if (keys[SDL_SCANCODE_SPACE]) move.y += 1;
  if (keys[SDL_SCANCODE_LSHIFT]) move.y -= 1;
  if (glm::length(move) > 0) {
    const float speed = flySpeed_ * (keys[SDL_SCANCODE_LCTRL] ? 2.5f : 1.0f);
    cam_.pos += glm::dvec3(glm::normalize(move)) * static_cast<double>(speed * dt);
  }
  cam_.pos.y = std::clamp(cam_.pos.y, -64.0, 512.0);
}

bool Game::iterate() {
  const u64 now = SDL_GetTicksNS();
  const double dt = std::min(0.1, (now - lastTicks_) / 1e9);
  lastTicks_ = now;
  runTime_ += dt;

  updateCamera(dt);

  // Ticks del juego a 20 por segundo: reloj del mundo y animaciones de texturas
  tickAccum_ += dt * 20.0;
  int ticks = 0;
  while (tickAccum_ >= 1.0 && ticks < 10) {
    tickAccum_ -= 1.0;
    ticks++;
    if (!opt_.freezeTime) worldTime_ += 1;
    const auto changed = textures_->tick();
    if (!changed.empty()) terrain_->refreshTextureLayers(*textures_, changed);
  }

  jobs_->pump(8.0);
  terrain_->update(cam_.pos, opt_.renderDistance);

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  render(w, h);

  frames_++;
  fpsTimer_ += dt;
  if (fpsTimer_ >= 1.0) {
    fps_ = frames_;
    frames_ = 0;
    fpsTimer_ -= 1.0;
  }

  // Captura automática cuando el mundo está cargado (o tras 60 s como máximo)
  if (!opt_.screenshotPath.empty() && !screenshotDone_) {
    if (settledAt_ < 0 && terrain_->settled()) settledAt_ = runTime_;
    if ((settledAt_ >= 0 && runTime_ - settledAt_ >= opt_.screenshotDelay) || runTime_ > 60) {
      if (runTime_ > 60) log::warn("el mundo no terminó de cargar en 60 s; captura igualmente");
      takeScreenshot(opt_.screenshotPath, w, h);
      screenshotDone_ = true;
      if (opt_.exitAfterScreenshot) quit_ = true;
    }
  }
  if (wantScreenshot_) {
    wantScreenshot_ = false;
    std::time_t t = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof(name), "%Y-%m-%d_%H.%M.%S.png", std::localtime(&t));
    takeScreenshot((fs::userDataDir() / "screenshots" / name).string(), w, h);
  }
  return !quit_;
}

void Game::render(int w, int h) {
  glViewport(0, 0, w, h);
  cam_.farPlane = opt_.renderDistance * 16.0f * 2.0f + 400.0f;
  cam_.update(w, h);
  env_->update(worldTime_, opt_.renderDistance * 16.0f, opt_.gamma, cam_.forward());

  const FogParams& fog = env_->fog();
  glClearColor(fog.color.r, fog.color.g, fog.color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  env_->drawSky(cam_);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  terrain_->drawOpaque(cam_, env_->lightmap(), fog);
  glDisable(GL_CULL_FACE);
  env_->drawClouds(cam_, worldTime_);
  glEnable(GL_CULL_FACE);
  terrain_->drawTranslucent(cam_, env_->lightmap(), fog);
  glDisable(GL_CULL_FACE);

  const int scale = Ui::autoScale(w, h);
  ui_->begin(w, h, scale);
  ui_->crosshair();
  if (opt_.showDebug) drawDebug(w, h);
  else if (!grabbed_) {
    const char* msg = "Haz clic para jugar  -  F3: depuracion  -  ESC: soltar raton";
    ui_->text(ui_->guiWidth() / 2.0f - ui_->textWidth(msg) / 2.0f, ui_->guiHeight() - 20.0f, msg, 0xFFFFFF);
  }
  ui_->end();
}

void Game::drawDebug(int w, int h) {
  const TerrainStats st = terrain_->stats();
  const int bx = static_cast<int>(std::floor(cam_.pos.x)), by = static_cast<int>(std::floor(cam_.pos.y)),
            bz = static_cast<int>(std::floor(cam_.pos.z));
  World& world = terrain_->world();
  const long day = static_cast<long>(worldTime_ / 24000);
  const int tod = static_cast<int>(std::fmod(worldTime_ + 6000.0, 24000.0));  // 0 = medianoche
  const int hh = tod / 1000, mm = (tod % 1000) * 60 / 1000;
  const std::string left[] = {
      "MC-WEB 0.1.0 (sala limpia, Minecraft 1.8)",
      std::format("{} fps, {} secciones dibujadas / {}", fps_, st.drawnSections, st.sections),
      std::format("Chunks: {}  generando: {}  mallando: {}", st.chunks, st.pendingGen, st.pendingMesh),
      std::format("Distancia de render: {} chunks (RePag/AvPag)", opt_.renderDistance),
      "",
      std::format("XYZ: {:.3f} / {:.3f} / {:.3f}", cam_.pos.x, cam_.pos.y, cam_.pos.z),
      std::format("Bloque: {} {} {}", bx, by, bz),
      std::format("Chunk: {} {} {} en {} {} {}", bx & 15, by & 15, bz & 15, bx >> 4, by >> 4, bz >> 4),
      std::format("Mirando: {} ({:.1f} / {:.1f})", cam_.facing(), glm::degrees(cam_.yaw), glm::degrees(cam_.pitch)),
      std::format("Bioma: {}", biomeInfo(world.biome(bx, bz)).displayName),
      std::format("Luz: {} cielo, {} bloque", world.skyLight(bx, by, bz), world.blockLight(bx, by, bz)),
      std::format("Dia {}, {:02}:{:02}", day, hh, mm),
  };
  const std::string right[] = {
      std::format("GL: {}", glRenderer_),
      std::format("Pantalla: {}x{}", w, h),
      std::format("Pack: {}", packs_->description()),
      std::format("Semilla: {}", static_cast<i64>(opt_.seed)),
      std::format("Hilos: {}", jobs_->threadCount()),
      std::format("Mallas en GPU: {:.1f} MB", st.gpuBytes / 1048576.0),
      std::format("Velocidad: {:.1f} bloques/s (rueda)", flySpeed_),
  };
  float y = 2;
  for (const auto& line : left) {
    if (!line.empty()) {
      ui_->rect(1, y - 1, ui_->textWidth(line) + 1, 9, 0x90505050);
      ui_->text(2, y, line, 0xE0E0E0, false);
    }
    y += 9;
  }
  y = 2;
  for (const auto& line : right) {
    const float x = ui_->guiWidth() - ui_->textWidth(line) - 2.0f;
    ui_->rect(x - 1, y - 1, ui_->textWidth(line) + 1, 9, 0x90505050);
    ui_->text(x, y, line, 0xE0E0E0, false);
    y += 9;
  }
}

bool Game::takeScreenshot(const std::string& path, int w, int h) {
  Image img(w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  std::vector<u8> px(static_cast<std::size_t>(w) * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < h; y++)
    std::copy_n(&px[static_cast<std::size_t>(h - 1 - y) * w * 4], static_cast<std::size_t>(w) * 4, &img.rgba[static_cast<std::size_t>(y) * w * 4]);
  for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const bool ok = writePng(path.c_str(), img);
  if (ok) log::info("captura guardada en {}", path);
  else log::error("no se pudo guardar la captura en {}", path);
  return ok;
}

}  // namespace mcw
