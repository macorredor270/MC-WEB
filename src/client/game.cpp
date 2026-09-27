#include "client/game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>

#include "assets/cc0_pack.h"
#include "assets/item_models.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/environment.h"
#include "client/gl.h"
#include "client/hud.h"
#include "client/item_renderer.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "core/fs.h"
#include "core/jobs.h"
#include "core/log.h"
#include "core/random.h"
#include "data/biomes.h"
#include "game/rules.h"
#include "game/session.h"

namespace mcw {

Game::Game(GameOptions options) : opt_(std::move(options)) {}

Game::~Game() {
  // Primero parar los hilos: sus trabajos apuntan al terreno y a los modelos.
  jobs_.reset();
  session_.reset();
  itemRenderer_.reset();
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
  itemModels_ = std::make_unique<ItemModels>();
  models_->bake(*packs_, *textures_);
  colors_->load(*packs_);
  // Grietas: 10 capas seguidas
  destroyLayer_ = textures_->layerFor("blocks/destroy_stage_0");
  for (int i = 1; i < 10; i++) textures_->layerFor("blocks/destroy_stage_" + std::to_string(i));
  itemModels_->prepare(*packs_, *textures_, *models_, *colors_);
  textures_->load(*packs_);
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
  if (!opt_.hasSeed) opt_.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count());
  log::info("semilla: {}", static_cast<i64>(opt_.seed));

  terrain_ = std::make_unique<Terrain>(*jobs_, opt_.seed, MesherContext{models_.get(), colors_.get()});
  terrain_->initGL(*textures_);
  env_ = std::make_unique<Environment>();
  env_->initGL(*packs_);
  ui_ = std::make_unique<Ui>();
  ui_->initGL(*packs_);
  itemRenderer_ = std::make_unique<ItemRenderer>(*models_, *itemModels_);
  itemRenderer_->initGL(terrain_->textureArray(), destroyLayer_);

  session_ = std::make_unique<GameSession>(*terrain_, opt_.seed);
  session_->setMode(opt_.mode);
  spawn_ = opt_.startPos.value_or(findSpawn());
  session_->setSpawn(spawn_);
  Player& p = session_->player();
  p.pos = p.prevPos = spawn_;
  if (opt_.yawDeg) p.yaw = glm::radians(*opt_.yawDeg);
  if (opt_.pitchDeg) p.pitch = glm::radians(*opt_.pitchDeg);
  cam_.yaw = p.yaw;
  cam_.pitch = p.pitch;
  cam_.pos = p.eyePos();
  worldTime_ = opt_.time;
  touch_.setActive(opt_.touch);
  lastTicks_ = SDL_GetTicksNS();
  gl::checkErrors("Game::init");
  return true;
}

glm::dvec3 Game::findSpawn() const {
  // Espiral desde el origen hasta encontrar tierra firme (no océano, río ni playa)
  const TerrainGenerator& gen = terrain_->generator();
  for (int r = 0; r < 64; r++) {
    for (int i = -r; i <= r; i++) {
      const std::pair<int, int> candidates[] = {{i * 16, -r * 16}, {i * 16, r * 16}, {-r * 16, i * 16}, {r * 16, i * 16}};
      for (auto [x, z] : candidates) {
        const ColumnInfo c = gen.column(x, z);
        if (c.height > TerrainGenerator::kSeaLevel + 1 && !c.river && c.biome != Biome::beach)
          return {x + 0.5, static_cast<double>(c.height) + 0.5, z + 0.5};
      }
    }
  }
  return {0.5, 100, 0.5};
}

void Game::trySpawn() {
  if (spawned_) return;
  const int x = static_cast<int>(std::floor(spawn_.x)), z = static_cast<int>(std::floor(spawn_.z));
  if (!terrain_->isReady(x, z)) return;
  // Buscar el suelo real (puede haber un árbol encima de la altura calculada)
  World& w = terrain_->world();
  int y = kChunkHeight - 2;
  if (!opt_.startPos) {
    while (y > 1 && (w.block(x, y, z) == 0 || !blockInfo(stateId(w.block(x, y, z))).fullBox)) y--;
    spawn_.y = y + 1.0;
    session_->setSpawn(spawn_);
  }
  Player& p = session_->player();
  p.pos = p.prevPos = spawn_;
  spawned_ = true;
  log::info("jugador en {:.1f} {:.1f} {:.1f} ({})", spawn_.x, spawn_.y, spawn_.z, p.creative() ? "creativo" : "supervivencia");
  if (!opt_.demo.empty()) runDemo();
}

void Game::setMouseGrab(bool grab) {
  if (touch_.active()) grab = false;
  grabbed_ = grab;
  SDL_SetWindowRelativeMouseMode(window_, grab);
}

void Game::setScreen(Screen s) {
  if (screen_ == Screen::Menu && s != Screen::Menu) session_->closeMenu();
  screen_ = s;
  leftHeld_ = rightHeld_ = false;
  setMouseGrab(s == Screen::None);
}

glm::vec2 Game::mouseGui() const {
  const float density = std::max(0.5f, SDL_GetWindowPixelDensity(window_));
  return {mouseX_ * density / ui_->scale(), mouseY_ * density / ui_->scale()};
}

void Game::clickScreen(int button, bool shift) {
  const glm::vec2 m = mouseGui();
  switch (screen_) {
    case Screen::Menu: {
      Menu* menu = session_->menu();
      if (!menu) { setScreen(Screen::None); return; }
      bool inside = false;
      const int slot = menuSlotAt(*ui_, *menu, m.x, m.y, inside);
      if (slot >= 0) menu->click(slot, button, shift);
      else if (!inside) session_->menuClickOutside(button);
      break;
    }
    case Screen::Pause: {
      switch (pauseButtonAt(*ui_, m.x, m.y, opt_.canQuit)) {
        case 0: setScreen(Screen::None); break;
        case 1: session_->setMode(session_->player().creative() ? GameMode::Survival : GameMode::Creative); break;
        case 2: {
          static constexpr int kSteps[] = {2, 4, 6, 8, 10, 12, 16};
          int next = kSteps[0];
          for (int s : kSteps)
            if (s > opt_.renderDistance) { next = s; break; }
          opt_.renderDistance = next;
          break;
        }
        case 3: worldTime_ += 3000; break;
        case 4: quit_ = true; break;
        default: break;
      }
      break;
    }
    case Screen::Death:
      if (deathButtonAt(*ui_, m.x, m.y)) {
        session_->respawn();
        setScreen(Screen::None);
      }
      break;
    default: break;
  }
}

void Game::pickBlock() {
  const auto& t = session_->target();
  if (!t || !session_->player().creative()) return;
  const BlockState s = terrain_->world().block(t->block.x, t->block.y, t->block.z);
  int id = stateId(s), meta = stateMeta(s);
  if (id == B::log || id == B::log2 || id == B::leaves || id == B::leaves2) meta &= 3;
  if (id == B::grass || id == B::torch || id == B::furnace || id == B::lit_furnace) meta = 0;
  if (id == B::lit_furnace) id = B::furnace;
  if (id == B::double_plant) meta &= 7;
  ItemStack pick(id, 1, meta);
  if (!isPlaceableItem(pick)) return;
  PlayerInventory& inv = session_->player().inventory;
  for (int i = 0; i < PlayerInventory::kHotbar; i++)
    if (inv.slot(i).id == pick.id && inv.slot(i).meta == pick.meta) { inv.select(i); return; }
  inv.selected() = ItemStack(id, 64, meta);
}

void Game::handleEvent(const SDL_Event& e) {
  // Pantalla táctil: el primer toque activa los controles en pantalla
  if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP ||
      e.type == SDL_EVENT_FINGER_CANCELED) {
    if (!touch_.active()) {
      touch_.setActive(true);
      setMouseGrab(false);
    }
    if (screen_ == Screen::None) touch_.handleEvent(e);
    else handleScreenTouch(e);
    return;
  }
  const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
  switch (e.type) {
    case SDL_EVENT_QUIT: quit_ = true; break;
    case SDL_EVENT_MOUSE_MOTION:
      if (e.motion.which == SDL_TOUCH_MOUSEID) break;
      mouseX_ = e.motion.x;
      mouseY_ = e.motion.y;
      if (grabbed_ && screen_ == Screen::None) {
        const float sens = 0.0022f;
        cam_.yaw -= e.motion.xrel * sens;
        cam_.pitch = std::clamp(cam_.pitch - e.motion.yrel * sens, -1.5607f, 1.5607f);
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.which == SDL_TOUCH_MOUSEID) break;
      touch_.setActive(false);  // un ratón de verdad: modo escritorio
      mouseX_ = e.button.x;
      mouseY_ = e.button.y;
      if (screen_ != Screen::None) {
        clickScreen(e.button.button == SDL_BUTTON_RIGHT ? 1 : 0, shift);
        break;
      }
      if (!grabbed_) { setMouseGrab(true); break; }  // el primer clic solo captura el ratón
      if (e.button.button == SDL_BUTTON_LEFT) leftHeld_ = attackPressed_ = true;
      if (e.button.button == SDL_BUTTON_RIGHT) rightHeld_ = usePressed_ = true;
      if (e.button.button == SDL_BUTTON_MIDDLE) pickBlock();
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_LEFT) leftHeld_ = false;
      if (e.button.button == SDL_BUTTON_RIGHT) rightHeld_ = false;
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      if (screen_ == Screen::Menu && session_->menu()) session_->menu()->scroll(e.wheel.y > 0 ? -1 : 1);
      else if (screen_ == Screen::None && e.wheel.y != 0) {
        const int cur = session_->player().inventory.selectedIndex();
        selectSlot_ = (cur + (e.wheel.y > 0 ? -1 : 1) + 9) % 9;
      }
      break;
    case SDL_EVENT_KEY_DOWN: {
      const SDL_Scancode sc = e.key.scancode;
      if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 && screen_ == Screen::None) selectSlot_ = sc - SDL_SCANCODE_1;
      switch (sc) {
        case SDL_SCANCODE_ESCAPE:
          if (screen_ == Screen::None) setScreen(Screen::Pause);
          else if (screen_ != Screen::Death) setScreen(Screen::None);
          break;
        case SDL_SCANCODE_E:
          if (screen_ == Screen::None && !session_->player().dead) {
            session_->openInventory();
            setScreen(Screen::Menu);
          } else if (screen_ == Screen::Menu) {
            setScreen(Screen::None);
          }
          break;
        case SDL_SCANCODE_Q:
          if (screen_ == Screen::None && !e.key.repeat) {
            if (SDL_GetModState() & SDL_KMOD_CTRL) dropStackPressed_ = true;
            else dropPressed_ = true;
          }
          break;
        case SDL_SCANCODE_SPACE:
          if (!e.key.repeat) jumpPressed_ = true;
          break;
        case SDL_SCANCODE_F3: opt_.showDebug = !opt_.showDebug; break;
        case SDL_SCANCODE_F2: wantScreenshot_ = true; break;
        case SDL_SCANCODE_F11: {
          const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
          SDL_SetWindowFullscreen(window_, !fs);
          break;
        }
        case SDL_SCANCODE_PAGEUP: opt_.renderDistance = std::min(32, opt_.renderDistance + 1); break;
        case SDL_SCANCODE_PAGEDOWN: opt_.renderDistance = std::max(2, opt_.renderDistance - 1); break;
        default: break;
      }
      break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (screen_ == Screen::None && !touch_.active() && spawned_) setScreen(Screen::Pause);
      break;
    default: break;
  }
}

void Game::handleScreenTouch(const SDL_Event& e) {
  // En los menús el dedo hace de ratón: tocar = clic, mantener = clic derecho, arrastrar = desplazar
  int w = 0, h = 0;
  SDL_GetWindowSize(window_, &w, &h);
  mouseX_ = e.tfinger.x * static_cast<float>(w);
  mouseY_ = e.tfinger.y * static_cast<float>(h);
  const glm::vec2 gui = mouseGui();
  switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
      if (screenFinger_) break;  // solo cuenta el primer dedo
      screenFinger_ = e.tfinger.fingerID;
      screenFingerDown_ = SDL_GetTicksNS();
      screenFingerStart_ = screenFingerLast_ = gui;
      screenFingerMoved_ = false;
      break;
    case SDL_EVENT_FINGER_MOTION:
      if (screenFinger_ != e.tfinger.fingerID) break;
      if (glm::length(gui - screenFingerStart_) > 8.0f) screenFingerMoved_ = true;
      if (screenFingerMoved_ && screen_ == Screen::Menu && session_->menu()) {
        // Cada fila de 18 píxeles arrastrada desplaza una fila la lista del creativo
        while (gui.y - screenFingerLast_.y <= -18.0f) {
          session_->menu()->scroll(1);
          screenFingerLast_.y -= 18.0f;
        }
        while (gui.y - screenFingerLast_.y >= 18.0f) {
          session_->menu()->scroll(-1);
          screenFingerLast_.y += 18.0f;
        }
      }
      break;
    case SDL_EVENT_FINGER_UP: {
      if (screenFinger_ != e.tfinger.fingerID) break;
      screenFinger_.reset();
      if (screenFingerMoved_) break;
      if (screen_ == Screen::Menu && touch_.closeHit(gui)) {
        setScreen(Screen::None);
        break;
      }
      clickScreen(SDL_GetTicksNS() - screenFingerDown_ >= 400'000'000 ? 1 : 0, false);
      break;
    }
    case SDL_EVENT_FINGER_CANCELED:
      if (screenFinger_ == e.tfinger.fingerID) screenFinger_.reset();
      break;
    default: break;
  }
}

glm::dvec3 Game::aimFromGui(glm::vec2 gui) const {
  // Punto de la pantalla -> dirección en el mundo (deshaciendo la proyección de la cámara)
  const glm::vec2 ndc(gui.x / ui_->guiWidth() * 2.0f - 1.0f, 1.0f - gui.y / ui_->guiHeight() * 2.0f);
  const glm::mat4 inv = glm::inverse(cam_.viewProj);
  glm::vec4 near = inv * glm::vec4(ndc, -1.0f, 1.0f), far = inv * glm::vec4(ndc, 1.0f, 1.0f);
  near /= near.w;
  far /= far.w;
  const glm::vec3 d = glm::vec3(far) - glm::vec3(near);
  return glm::length(d) > 0 ? glm::normalize(glm::dvec3(d)) : glm::dvec3(cam_.forward());
}

void Game::gameTick() {
  TickInput in;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  const TouchInput t = touch_.consume();
  if (screen_ == Screen::None) {
    in.move.forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f) + t.forward;
    in.move.strafe = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f) + t.strafe;
    in.move.forward = std::clamp(in.move.forward, -1.0f, 1.0f);
    in.move.strafe = std::clamp(in.move.strafe, -1.0f, 1.0f);
    in.move.jump = keys[SDL_SCANCODE_SPACE] || t.jump;
    in.move.sneak = keys[SDL_SCANCODE_LSHIFT] || t.sneak;
    in.move.sprint = keys[SDL_SCANCODE_LCTRL] || t.sprint;
    in.attack = leftHeld_;
    in.use = rightHeld_;
    in.attackPressed = attackPressed_;
    in.usePressed = usePressed_;
    in.drop = dropPressed_;
    in.dropStack = dropStackPressed_;
    in.jumpPressed = jumpPressed_ || t.jumpPressed;
    // Dedo sobre el mundo: mantener rompe (o come, con comida en la mano); tocar usa/coloca
    if (t.attack) {
      const Player& p = session_->player();
      if (!p.creative() && foodValue(p.inventory.selected()) && p.food < 20) {
        in.use = true;
      } else {
        in.attack = true;
        in.attackPressed = in.attackPressed || !touchAttacking_;
      }
    }
    touchAttacking_ = t.attack;
    if (t.usePressed) in.usePressed = true;
    if (t.aim) in.aimDir = aimFromGui(*t.aim);
  } else {
    touchAttacking_ = false;
  }
  in.selectSlot = t.selectSlot >= 0 ? t.selectSlot : selectSlot_;
  in.yaw = cam_.yaw;
  in.pitch = cam_.pitch;
  attackPressed_ = usePressed_ = jumpPressed_ = dropPressed_ = dropStackPressed_ = false;
  selectSlot_ = -1;

  session_->tick(in);

  // Animaciones y avisos
  if (in.attack && session_->target() && screen_ == Screen::None && swing_ <= 0) swing_ = 1.0f;
  for (const SessionEvent& ev : session_->takeEvents()) {
    switch (ev.type) {
      case SessionEvent::Type::BlockPlaced: swing_ = 1.0f; break;
      case SessionEvent::Type::PlayerHurt: hurtFlash_ = 1.0f; break;
      case SessionEvent::Type::PlayerDied: setScreen(Screen::Death); break;
      default: break;
    }
  }
  if (session_->menu() && screen_ == Screen::None) setScreen(Screen::Menu);  // mesa de trabajo, horno...
  if (!session_->menu() && screen_ == Screen::Menu) setScreen(Screen::None);
  if (session_->player().inventory.selectedIndex() != lastSelected_) {
    lastSelected_ = session_->player().inventory.selectedIndex();
    nameTimer_ = 2.0f;
  }
  touch_.setFlying(session_->player().flying);
  if (screen_ == Screen::None && t.openInventory && !session_->player().dead) {
    session_->openInventory();
    setScreen(Screen::Menu);
  }
  if (screen_ == Screen::None && t.pause) setScreen(Screen::Pause);
  // Mirar con el dedo
  if (t.look.x != 0 || t.look.y != 0) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    const float perGuiPixel = 0.0045f * Ui::autoScale(w, h) / std::max(0.5f, SDL_GetWindowPixelDensity(window_));
    cam_.yaw -= t.look.x * perGuiPixel;
    cam_.pitch = std::clamp(cam_.pitch - t.look.y * perGuiPixel, -1.5607f, 1.5607f);
  }
}

bool Game::iterate() {
  const u64 now = SDL_GetTicksNS();
  const double dt = std::min(0.1, (now - lastTicks_) / 1e9);
  lastTicks_ = now;
  runTime_ += dt;

  jobs_->pump(8.0);
  const glm::dvec3 center = spawned_ ? session_->player().pos : spawn_;
  terrain_->update(center, opt_.renderDistance);
  trySpawn();

  touch_.newFrame();
  // Ticks del juego a 20 por segundo
  tickAccum_ += dt * 20.0;
  int ticks = 0;
  while (tickAccum_ >= 1.0 && ticks < 10) {
    tickAccum_ -= 1.0;
    ticks++;
    if (!opt_.freezeTime) worldTime_ += 1;
    const auto changed = textures_->tick();
    if (!changed.empty()) terrain_->refreshTextureLayers(*textures_, changed);
    if (spawned_) gameTick();
    if (swing_ > 0) swing_ = std::max(0.0f, swing_ - 1.0f / 6.0f);
    if (nameTimer_ > 0) nameTimer_ -= 0.05f;
    if (hurtFlash_ > 0) hurtFlash_ = std::max(0.0f, hurtFlash_ - 0.1f);
  }
  const float partial = static_cast<float>(tickAccum_);

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  updateCamera(partial);
  render(w, h, partial);

  frames_++;
  fpsTimer_ += dt;
  if (fpsTimer_ >= 1.0) {
    fps_ = frames_;
    frames_ = 0;
    fpsTimer_ -= 1.0;
  }

  // Captura automática cuando el mundo está cargado (o tras 60 s como máximo)
  if (!opt_.screenshotPath.empty() && !screenshotDone_) {
    if (settledAt_ < 0 && spawned_ && terrain_->settled()) settledAt_ = runTime_;
    if ((settledAt_ >= 0 && runTime_ - settledAt_ >= opt_.screenshotDelay) || runTime_ > 60) {
      if (runTime_ > 60) log::warn("el mundo no terminó de cargar en 60 s; captura igualmente");
      takeScreenshot(opt_.screenshotPath, w, h);
      screenshotDone_ = true;
      if (opt_.exitAfterScreenshot) quit_ = true;
    }
  }
  if (wantScreenshot_) {
    wantScreenshot_ = false;
    std::time_t tt = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof(name), "%Y-%m-%d_%H.%M.%S.png", std::localtime(&tt));
    takeScreenshot((fs::userDataDir() / "screenshots" / name).string(), w, h);
  }
  return !quit_;
}

void Game::updateCamera(float partial) {
  const Player& p = session_->player();
  if (!spawned_) {
    cam_.pos = spawn_ + glm::dvec3(0, 20, 0);
    return;
  }
  const glm::dvec3 pos = p.prevPos + (p.pos - p.prevPos) * static_cast<double>(partial);
  cam_.pos = pos + glm::dvec3(0, p.eyePos().y - p.pos.y, 0);
  // Balanceo al andar
  const double speed = glm::length(glm::dvec2(p.pos.x - p.prevPos.x, p.pos.z - p.prevPos.z));
  if (p.onGround && !p.flying) bob_ += static_cast<float>(speed) * 0.6f;
  // FOV: se abre al correr y al volar (con suavizado)
  const float target = (p.sprinting ? 1.15f : 1.0f) * (p.flying ? 1.1f : 1.0f);
  fovMod_ += (target - fovMod_) * 0.25f;
  cam_.fovDeg = 70.0f * fovMod_;
}

void Game::render(int w, int h, float partial) {
  glViewport(0, 0, w, h);
  cam_.farPlane = opt_.renderDistance * 16.0f * 2.0f + 400.0f;
  cam_.update(w, h);
  env_->update(worldTime_, opt_.renderDistance * 16.0f, opt_.gamma, cam_.forward());

  const FogParams& fog = env_->fog();
  glClearColor(fog.color.r, fog.color.g, fog.color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  env_->drawSky(cam_);

  World& world = terrain_->world();
  auto lightAt = [&](const glm::dvec3& p) {
    const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y)), z = static_cast<int>(std::floor(p.z));
    return env_->lightColor(static_cast<float>(world.skyLight(x, y, z)), static_cast<float>(world.blockLight(x, y, z)));
  };

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  terrain_->drawOpaque(cam_, env_->lightmap(), fog);
  glDisable(GL_CULL_FACE);
  itemRenderer_->drawWorldItems(session_->items(), cam_, partial, worldTime_, lightAt);

  const Player& player = session_->player();
  if (spawned_ && screen_ == Screen::None && session_->target() && !player.dead) {
    const RayHit& hit = *session_->target();
    std::vector<AABB> boxes;
    selectionBoxes(world.block(hit.block.x, hit.block.y, hit.block.z), world.block(hit.block.x, hit.block.y - 1, hit.block.z), boxes);
    itemRenderer_->drawSelection(boxes, hit.block, cam_);
  }
  if (auto br = session_->breaking())
    itemRenderer_->drawBreaking(world.block(br->pos.x, br->pos.y, br->pos.z), br->pos, br->progress, cam_);

  env_->drawClouds(cam_, worldTime_);
  glEnable(GL_CULL_FACE);
  terrain_->drawTranslucent(cam_, env_->lightmap(), fog);
  glDisable(GL_CULL_FACE);

  if (spawned_ && !player.dead)
    itemRenderer_->drawHeld(player.inventory.selected(), cam_, swing_ > 0 ? 1.0f - swing_ : 0.0f, bob_, lightAt(player.eyePos()));

  // --- Interfaz ---
  const int scale = Ui::autoScale(w, h);
  ui_->begin(w, h, scale);
  touch_.setScreen(ui_->guiWidth(), ui_->guiHeight(), scale, SDL_GetWindowPixelDensity(window_));
  if (hurtFlash_ > 0) ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()),
                                (static_cast<u32>(hurtFlash_ * 90) << 24) | 0xB00000);
  if (player.headInWater) ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()), 0x50102060);

  if (!spawned_) {
    ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()), 0xFF1E1812);
    ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() / 2.0f - 4, "Generando el mundo...", 0xFFFFFF);
  } else {
    // En táctil se apunta con el dedo: sin punto de mira
    if (screen_ == Screen::None && !touch_.active()) ui_->crosshair();
    drawHud(*ui_, *itemRenderer_, player, std::min(1.0f, nameTimer_));
    if (screen_ == Screen::None) touch_.draw(*ui_, player.inventory.selectedIndex());
    if (opt_.showDebug && screen_ == Screen::None) drawDebug(w, h);
    const glm::vec2 m = mouseGui();
    switch (screen_) {
      case Screen::Menu:
        if (session_->menu()) drawMenu(*ui_, *itemRenderer_, *session_->menu(), player, m.x, m.y);
        touch_.drawClose(*ui_, screenFinger_ && touch_.closeHit(m));
        break;
      case Screen::Pause: drawPauseMenu(*ui_, m.x, m.y, player.creative(), opt_.renderDistance, opt_.canQuit); break;
      case Screen::Death: drawDeathScreen(*ui_, m.x, m.y); break;
      default:
        if (!grabbed_ && !touch_.active()) {
          const char* msg = "Haz clic para jugar  -  E: inventario  -  ESC: menu";
          ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() - 60.0f, msg, 0xFFFFFF);
        }
        break;
    }
  }
  ui_->end();
}

void Game::drawDebug(int w, int h) {
  const TerrainStats st = terrain_->stats();
  const Player& p = session_->player();
  const int bx = static_cast<int>(std::floor(p.pos.x)), by = static_cast<int>(std::floor(p.pos.y)),
            bz = static_cast<int>(std::floor(p.pos.z));
  World& world = terrain_->world();
  const long day = static_cast<long>(worldTime_ / 24000);
  const int tod = static_cast<int>(std::fmod(worldTime_ + 6000.0, 24000.0));  // 0 = medianoche
  const int hh = tod / 1000, mm = (tod % 1000) * 60 / 1000;
  std::string target = "-";
  if (const auto& t = session_->target()) {
    const BlockState s = world.block(t->block.x, t->block.y, t->block.z);
    target = std::format("{} {} {}: {}:{} ({})", t->block.x, t->block.y, t->block.z, stateId(s), stateMeta(s), blockInfo(stateId(s)).name);
  }
  const std::string left[] = {
      "MC-WEB 0.2.0 (sala limpia, Minecraft 1.8)",
      std::format("{} fps, {} secciones dibujadas / {}", fps_, st.drawnSections, st.sections),
      std::format("Chunks: {}  generando: {}  mallando: {}", st.chunks, st.pendingGen, st.pendingMesh),
      std::format("Distancia de render: {} chunks", opt_.renderDistance),
      "",
      std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.pos.x, p.pos.y, p.pos.z),
      std::format("Bloque: {} {} {}", bx, by, bz),
      std::format("Chunk: {} {} {} en {} {} {}", bx & 15, by & 15, bz & 15, bx >> 4, by >> 4, bz >> 4),
      std::format("Mirando: {} ({:.1f} / {:.1f})", cam_.facing(), glm::degrees(cam_.yaw), glm::degrees(cam_.pitch)),
      std::format("Bioma: {}", biomeInfo(world.biome(bx, bz)).displayName),
      std::format("Luz: {} cielo, {} bloque", world.skyLight(bx, by + 1, bz), world.blockLight(bx, by + 1, bz)),
      std::format("Dia {}, {:02}:{:02}", day, hh, mm),
      "",
      std::format("Modo: {}{}  suelo: {}", p.creative() ? "creativo" : "supervivencia", p.flying ? " (volando)" : "", p.onGround ? "si" : "no"),
      std::format("Vida {:.0f}  comida {}  saturacion {:.1f}", p.health, p.food, p.saturation),
      std::format("Apuntando: {}", target),
  };
  const std::string right[] = {
      std::format("GL: {}", glRenderer_),
      std::format("Pantalla: {}x{}", w, h),
      std::format("Pack: {}", packs_->description()),
      std::format("Semilla: {}", static_cast<i64>(opt_.seed)),
      std::format("Hilos: {}", jobs_->threadCount()),
      std::format("Mallas en GPU: {:.1f} MB", st.gpuBytes / 1048576.0),
      std::format("Objetos en el suelo: {}", session_->items().size()),
  };
  const float top = std::max(2.0f, touch_.topInset());
  float y = top;
  for (const auto& line : left) {
    if (!line.empty()) {
      ui_->rect(1, y - 1, ui_->textWidth(line) + 1, 9, 0x90505050);
      ui_->text(2, y, line, 0xE0E0E0, false);
    }
    y += 9;
  }
  y = top;
  for (const auto& line : right) {
    const float x = ui_->guiWidth() - ui_->textWidth(line) - 2.0f;
    ui_->rect(x - 1, y - 1, ui_->textWidth(line) + 1, 9, 0x90505050);
    ui_->text(x, y, line, 0xE0E0E0, false);
    y += 9;
  }
}

void Game::runDemo() {
  // Acciones automáticas para capturas de prueba (--demo). No se usan al jugar.
  Player& p = session_->player();
  PlayerInventory& inv = p.inventory;
  auto give = [&](int id, int n, int meta = 0) { inv.add(ItemStack(id, n, meta)); };
  if (opt_.demo == "inventario" || opt_.demo == "hud") {
    give(ItemId::diamond_pickaxe, 1);
    give(B::torch, 32);
    give(B::planks, 48, 2);
    give(ItemId::apple, 5);
    give(B::cobblestone, 64);
    give(B::crafting_table, 1);
    give(ItemId::iron_ingot, 12);
    give(B::red_flower, 3);
    give(B::log, 16, 0);
    inv.slot(0).meta = 700;  // pico gastado: se ve la barra
    inv.select(1);
    nameTimer_ = 2.0f;
    p.health = 13;
    p.food = 15;
    if (opt_.demo == "inventario") {
      session_->openInventory();
      setScreen(Screen::Menu);
      // Tablones en la rejilla de 2x2 para ver una receta (mesa de trabajo)
      Menu* m = session_->menu();
      int plankSlot = -1;
      for (int i = 0; i < static_cast<int>(m->slots().size()); i++)
        if (m->slots()[i].stack->id == B::planks) plankSlot = i;
      m->click(plankSlot, 0, false);
      for (int i = 1; i <= 4; i++) m->click(i, 1, false);
      m->click(plankSlot, 0, false);
    }
  } else if (opt_.demo == "creativo") {
    session_->setMode(GameMode::Creative);
    session_->openInventory();
    setScreen(Screen::Menu);
  } else if (opt_.demo == "pausa") {
    setScreen(Screen::Pause);
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
