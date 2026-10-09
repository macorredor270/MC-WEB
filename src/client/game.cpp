#include "client/game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <format>

#include "assets/cc0_pack.h"
#include "assets/item_models.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/audio.h"
#include "client/entity_renderer.h"
#include "client/environment.h"
#include "client/gl.h"
#include "client/hud.h"
#include "client/item_renderer.h"
#include "client/music.h"
#include "client/particles.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "client/worker_pool.h"
#include "core/fs.h"
#include "core/jobs.h"
#include "core/log.h"
#include "core/random.h"
#include "data/biomes.h"
#include "data/items.h"
#include "game/enchantments.h"
#include "game/rules.h"
#include "save/anvil.h"
#include "game/session.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {

#ifdef __EMSCRIPTEN__
// En el navegador, Esc suelta el puntero sin que llegue la tecla: hay que preguntarlo
EM_JS(int, mcw_js_pointer_locked, (), { return document.pointerLockElement ? 1 : 0; });
// Vibración del móvil (Android: Chrome y Firefox; Safari no la tiene)
EM_JS(void, mcw_js_vibrate, (int ms), { try { if (navigator.vibrate) navigator.vibrate(ms); } catch (e) {} });
#endif

Game::Game(GameOptions options) : opt_(std::move(options)) {}

Game::~Game() {
  // Primero parar los hilos: sus trabajos apuntan al terreno y a los modelos.
  jobs_.reset();
  workers_.reset();
  session_.reset();
  itemRenderer_.reset();
  entityRenderer_.reset();
  particles_.reset();
  audio_.reset();
  terrain_.reset();
  env_.reset();
  ui_.reset();
  if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
  if (sceneColor_) glDeleteTextures(1, &sceneColor_);
  if (sceneDepth_) glDeleteRenderbuffers(1, &sceneDepth_);
}

void Game::loadAssets() {
  packs_ = std::make_unique<PackStack>();
  cc0Pack_ = makeCC0Pack();
  packs_->pushBottom(cc0Pack_);
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
  // Paquetes de recursos elegidos (Faithful...), encima de todo; el primero de la lista manda
  for (auto it = settings_.resourcePacks.rbegin(); it != settings_.resourcePacks.rend(); ++it) {
    const std::filesystem::path p = resourcePackDir() / *it;
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) {
      packs_->pushTop(std::make_shared<DirPack>(p));
      log::info("paquete de recursos (carpeta): {}", *it);
    } else if (auto pack = ZipPack::open(p)) {
      packs_->pushTop(std::shared_ptr<const Pack>(std::move(pack)));
      log::info("paquete de recursos: {}", *it);
    } else {
      log::warn("no se pudo abrir el paquete de recursos {}", *it);
    }
  }
  textures_ = std::make_unique<BlockTextures>();
  models_ = std::make_unique<BlockModels>();
  colors_ = std::make_unique<Colormaps>();
  itemModels_ = std::make_unique<ItemModels>();
  models_->bake(*packs_, *textures_);
  bakedLayers_ = textures_->layerCount();
  colors_->load(*packs_);
  // Grietas: 10 capas seguidas
  destroyLayer_ = textures_->layerFor("blocks/destroy_stage_0");
  for (int i = 1; i < 10; i++) textures_->layerFor("blocks/destroy_stage_" + std::to_string(i));
  itemModels_->prepare(*packs_, *textures_, *models_, *colors_);
  textures_->load(*packs_);
}

void Game::initRenderers() {
  terrain_ = std::make_unique<Terrain>(*jobs_, opt_.seed, MesherContext{models_.get(), colors_.get()});
  terrain_->initGL(*textures_);
  // Web sin hilos: generar y mallar en Web Workers (los otros núcleos del dispositivo)
  if (WorkerPool::supported() && opt_.webWorkers != 0) {
    workers_ = std::make_unique<WorkerPool>(*jobs_);
    const int n = opt_.webWorkers > 0 ? opt_.webWorkers : WorkerPool::suggestedCount();
    if (workers_->start(n, opt_.seed, bundleModelFiles(*packs_, cc0Pack_.get()), bakedLayers_))
      terrain_->setWorkerPool(workers_.get());
    else
      workers_.reset();
  }
  env_ = std::make_unique<Environment>();
  env_->initGL(*packs_);
  ui_ = std::make_unique<Ui>();
  ui_->initGL(*packs_);
  itemRenderer_ = std::make_unique<ItemRenderer>(*models_, *itemModels_);
  itemRenderer_->initGL(terrain_->textureArray(), destroyLayer_);
  entityRenderer_ = std::make_unique<EntityRenderer>(*packs_);
  entityRenderer_->initGL();
  applyLocalSkin();  // (también tras cambiar de paquetes de recursos: Steve y Alex salen de ellos)
  particles_ = std::make_unique<ParticleSystem>();
  particles_->initGL(terrain_->textureArray());
  // Sesión vacía hasta que se abra un mundo (así nada tiene que comprobar si existe)
  session_ = std::make_unique<GameSession>(*terrain_, opt_.seed);
  appliedOnce_ = false;
}

void Game::reloadResources() {
  // Solo fuera de un mundo: se rehace todo lo que depende de las texturas y los modelos
  if (inWorld_) leaveWorld();
  jobs_->waitIdle();
  session_.reset();
  particles_.reset();
  entityRenderer_.reset();
  itemRenderer_.reset();
  ui_.reset();
  env_.reset();
  terrain_.reset();
  workers_.reset();
  loadAssets();
  initRenderers();
  applySettings();
}

bool Game::init(SDL_Window* window) {
  window_ = window;
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  glRenderer_ = renderer ? renderer : "?";
  log::info("GL: {} / {}", glRenderer_, version ? version : "?");
  gpuTimer_.init();

  const int threads = opt_.threads >= 0 ? opt_.threads : JobSystem::defaultThreadCount();
  jobs_ = std::make_unique<JobSystem>(threads);
  log::info("hilos de trabajo: {}", threads);

  // Opciones guardadas (en táctil se empieza con algo menos de distancia); la línea de órdenes manda
  settings_.renderDistance = opt_.touch ? 10 : 12;
#ifdef __EMSCRIPTEN__
  settings_.adaptiveFps = 60;  // en el navegador, rendimiento automático desde el principio (se puede apagar en Ajustes)
#endif
  settings_.parse(loadSettingsText());
  if (!opt_.hasSeed) opt_.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count());
  log::info("semilla: {}", static_cast<i64>(opt_.seed));
  loadAssets();
  initRenderers();
  audio_ = std::make_unique<Audio>();
  audio_->init();
  touch_.setActive(opt_.touch);
  if (opt_.fixedCam) hideHud_ = true;  // (sin interfaz ni mano)
  if (opt_.renderDistanceSet) settings_.renderDistance = opt_.renderDistance;
  if (opt_.adaptiveFps >= 0) settings_.adaptiveFps = opt_.adaptiveFps;
  if (opt_.gammaSet) settings_.brightness = std::clamp(opt_.gamma, 0.0f, 1.0f);
  if (opt_.noVsync) settings_.vsync = false;
  music_ = std::make_unique<Music>(opt_.seed ^ 0x6D75736963ull);
  applySettings();
  lastTicks_ = SDL_GetTicksNS();
  gl::checkErrors("Game::init");

  if (!opt_.world.empty()) {
    WorldSave w(WorldSave::savesDir() / opt_.world);
    LevelInfo info;
    if (w.loadLevel(info)) {
      enterWorld(opt_.world, info);
      return true;
    }
    log::warn("no existe el mundo {}", opt_.world);
  }
  if (opt_.directStart) {
    // Mundo temporal con lo que diga la línea de órdenes (pruebas y capturas)
    LevelInfo info;
    info.name = "Prueba";
    info.seed = opt_.seed;
    info.gameType = opt_.mode == GameMode::Creative ? 1 : 0;
    info.dayTime = static_cast<i64>(opt_.time);
    info.difficulty = settings_.difficulty;
    info.allowCommands = true;
    enterWorld("", info);
  } else {
    openScreen(Screen::Title);
    // Demos de menús para capturas
    if (opt_.demo == "mundos") openScreen(Screen::Worlds);
    if (opt_.demo == "packs") openScreen(Screen::ResourcePacks);
    if (opt_.demo == "multi") openScreen(Screen::Multiplayer);
    if (opt_.demo == "skins") openScreen(Screen::Skins);
    if (opt_.demo == "capas") openScreen(Screen::SkinParts);
    if (opt_.demo.rfind("unirse:", 0) == 0) connectToServer(opt_.demo.substr(7));
    if (opt_.demo == "recarga") {
      // Cambiar de packs dos veces y entrar en un mundo nuevo (prueba de recarga de recursos)
      openScreen(Screen::ResourcePacks);
      packSelection_.clear();
      menuButton(60);
      openScreen(Screen::ResourcePacks);
      for (const PackEntry& p : availablePacks_) packSelection_.push_back(p.file);
      menuButton(60);
      openScreen(Screen::CreateWorld);
      nameField_.text = "Recarga";
      createWorldFromForm();
    }
    if (opt_.demo == "crear") {
      openScreen(Screen::Worlds);
      menuButton(11);  // crear mundo nuevo
    }
    if (opt_.demo.rfind("opciones", 0) == 0) runDemo();
    if (opt_.demo == "nuevo") {
      // Crear un mundo desde el formulario, como si se pulsara "Crear mundo nuevo"
      openScreen(Screen::CreateWorld);
      nameField_.text = "Demo guardado";
      seedField_.text = "mcweb";
      createWorldFromForm();
    }
  }
  return true;
}

glm::dvec3 Game::findSpawn() const {
  // Espiral desde el origen hasta encontrar tierra firme (no océano, río ni playa)
  const auto [x, height, z] = terrain_->generator().findSpawn();
  return {x + 0.5, height + 0.5, z + 0.5};
}

void Game::trySpawn() {
  if (spawned_ || !inWorld_) return;
  if (net_ && !netPositioned_) return;  // en un servidor, esperar a que diga dónde estamos
  const glm::dvec3 at = keepPlayerPos_ ? session_->player().pos : spawn_;
  const int x = static_cast<int>(std::floor(at.x)), z = static_cast<int>(std::floor(at.z));
  if (!terrain_->isReady(x, z)) return;
  // Buscar el suelo real (puede haber un árbol encima de la altura calculada)
  World& w = terrain_->world();
  int y = kChunkHeight - 2;
  if (keepPlayerPos_) {
    // Mundo guardado: el jugador sigue donde lo dejó
    spawned_ = true;
    log::info("jugador en {:.1f} {:.1f} {:.1f} (guardado)", session_->player().pos.x, session_->player().pos.y, session_->player().pos.z);
    if (!opt_.demo.empty()) runDemo();
    return;
  }
  if (!opt_.startPos) {
    // El suelo de verdad: sin contar copas ni troncos de árbol
    auto tree = [](int id) { return id == B::leaves || id == B::leaves2 || id == B::log || id == B::log2; };
    auto ground = [&](int gx, int gz) {
      int gy = kChunkHeight - 2;
      while (gy > 1 && (w.block(gx, gy, gz) == 0 || !blockInfo(stateId(w.block(gx, gy, gz))).fullBox || tree(stateId(w.block(gx, gy, gz)))))
        gy--;
      return gy;
    };
    y = ground(x, z);
    // Si ha caído justo en un tronco, a la columna de al lado que esté libre
    const int around[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4 && tree(stateId(w.block(x, y + 1, z))); i++) {
      const int nx = x + around[i][0], nz = z + around[i][1], ny = ground(nx, nz);
      if (!tree(stateId(w.block(nx, ny + 1, nz)))) {
        spawn_.x += around[i][0];
        spawn_.z += around[i][1];
        y = ny;
        break;
      }
    }
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
  pointerLockSeen_ = false;
  motionLocked_ = false;
  SDL_SetWindowRelativeMouseMode(window_, grab);
}

bool Game::mouseLocked() const {
#ifdef __EMSCRIPTEN__
  return grabbed_ && mcw_js_pointer_locked();
#else
  return grabbed_;
#endif
}

void Game::setScreen(Screen s) {
  if (screen_ == Screen::Menu && s != Screen::Menu) {
    // Creativo en un servidor: lo que lleve el cursor vuelve al inventario y hay que contárselo
    const bool sync = net_ && session_->menu() && session_->menu()->kind() == MenuKind::Creative;
    const InvSnapshot before = sync ? snapshotInventory() : InvSnapshot{};
    session_->closeMenu();
    if (sync) netCreativeSync(before);
    if (s != Screen::Chat) SDL_StopTextInput(window_);
  }
  if (screen_ != s) touch_.releaseAll();  // los dedos que había ya no llegarán aquí (van a los menús)
  screen_ = s;
  creativeBarDrag_ = false;
  creativeFling_ = creativeScrollAccum_ = 0;
  leftHeld_ = rightHeld_ = false;
  setMouseGrab(s == Screen::None);
  if (s == Screen::Menu) syncMenuTextInput();
}

Game::InvSnapshot Game::snapshotInventory() const {
  InvSnapshot snap;
  const PlayerInventory& inv = session_->player().inventory;
  for (int i = 0; i < PlayerInventory::kSize; i++) snap.slots[static_cast<std::size_t>(i)] = inv.slot(i);
  for (int i = 0; i < 4; i++) snap.armor[static_cast<std::size_t>(i)] = inv.armor(i);
  return snap;
}

bool Game::creativeSearchActive() const {
  if (screen_ != Screen::Menu || !session_) return false;
  const Menu* m = session_->menu();
  return m && m->kind() == MenuKind::Creative && m->creativeTab() == CreativeTab::Search;
}

void Game::syncMenuTextInput() {
  if (creativeSearchActive()) SDL_StartTextInput(window_);
  else SDL_StopTextInput(window_);
}

void Game::selectCreativeTab(CreativeTab tab) {
  Menu* m = session_->menu();
  if (!m || m->kind() != MenuKind::Creative) return;
  creativeBarDrag_ = false;
  creativeFling_ = creativeScrollAccum_ = 0;
  if (m->creativeTab() != tab) {
    m->setCreativeTab(tab);
    audio_->playFlat(Sfx::Click);
  }
  syncMenuTextInput();
}

void Game::dragCreativeBar(float guiY) {
  if (Menu* m = session_->menu()) m->setScrollRow(creativeScrollRowAt(*ui_, *m, guiY));
}

void Game::updateMenuInertia(double dt) {
  if (screen_ != Screen::Menu || std::abs(creativeFling_) < 8.0f) {
    creativeFling_ = 0;
    return;
  }
  Menu* m = session_->menu();
  if (!m || m->kind() != MenuKind::Creative || screenFinger_) {
    creativeFling_ = 0;
    return;
  }
  creativeScrollAccum_ += creativeFling_ * static_cast<float>(dt);
  const int rows = static_cast<int>(creativeScrollAccum_ / 18.0f);
  if (rows != 0) {
    const int before = m->scrollRow();
    m->scroll(rows);
    creativeScrollAccum_ -= static_cast<float>(rows) * 18.0f;
    if (m->scrollRow() == before) creativeFling_ = creativeScrollAccum_ = 0;  // tope de la lista
  }
  creativeFling_ *= std::exp(-3.2f * static_cast<float>(dt));  // fricción
}

glm::vec2 Game::mouseGui() const {
  const float density = std::max(0.5f, SDL_GetWindowPixelDensity(window_));
  return {mouseX_ * density / ui_->scale(), mouseY_ * density / ui_->scale()};
}

void Game::clickScreen(int button, bool shift) {
  const glm::vec2 m = mouseGui();
  if (inMenuScreen()) {
    menuPress(m, button);
    return;
  }
  const std::vector<MenuButton> pause = pauseButtons(*ui_, false, level_.allowCommands || !save_);
  if (screen_ == Screen::Pause && buttonAt(pause, m.x, m.y) >= 0) audio_->playFlat(Sfx::Click);
  if (screen_ == Screen::Death && deathButtonAt(*ui_, m.x, m.y)) audio_->playFlat(Sfx::Click);
  switch (screen_) {
    case Screen::Menu: {
      Menu* menu = session_->menu();
      if (!menu) { setScreen(Screen::None); return; }
      if (menu->kind() == MenuKind::Creative) {
        if (const int tab = creativeTabAt(*ui_, *menu, m.x, m.y); tab >= 0) {
          if (button == 0) selectCreativeTab(static_cast<CreativeTab>(tab));
          break;
        }
        if (button == 0 && creativeScrollbarAt(*ui_, *menu, m.x, m.y)) {
          creativeBarDrag_ = true;
          dragCreativeBar(m.y);
          break;
        }
        if (creativeSearchFieldAt(*ui_, *menu, m.x, m.y)) {  // (en táctil, tocar el campo saca el teclado)
          SDL_StartTextInput(window_);
          break;
        }
      }
      // Mesa de encantamientos: las tres opciones no son casillas; se pagan con lapislázuli y niveles
      if (const int option = enchantOptionAt(*ui_, *menu, m.x, m.y); option >= 0) {
        if (button == 0 && menu->canEnchant(option)) {
          if (net_) net_->sendEnchantItem(netWindow_, option);  // lo hace el servidor y nos manda el resultado
          else menu->enchant(option);
          audio_->playFlat(Sfx::Enchant, 0.7f);
        }
        break;
      }
      bool inside = false;
      const int slot = menuSlotAt(*ui_, *menu, m.x, m.y, inside);
      const InvSnapshot before = snapshotInventory();
      const ItemStack clicked = slot >= 0 ? *menu->slots()[static_cast<std::size_t>(slot)].stack : ItemStack();
      if (slot >= 0) menu->click(slot, button, shift);
      else if (!inside) session_->menuClickOutside(button);
      if (net_) netMenuClick(slot >= 0 ? slot : -999, button, shift, before, clicked);
      break;
    }
    case Screen::Pause: {
      const int id = buttonAt(pause, m.x, m.y);
      switch (id) {
        case kPauseResume: setScreen(Screen::None); break;
        case kPauseMode: session_->setMode(session_->player().creative() ? GameMode::Survival : GameMode::Creative); break;
        case kPauseOptions:
          openOptionPage(OptPage::Main);
          setScreen(Screen::Options);
          break;
        case kPauseAchievements:
        case kPauseStats:
          achShowStats_ = id == kPauseStats;
          achScroll_ = {0, 0};
          openScreen(Screen::Achievements);
          break;
        case kPauseLan: openToLan(); break;
        case kPauseQuit:
          leaveWorld();
          openScreen(Screen::Title);
          break;
        default: break;
      }
      break;
    }
    case Screen::Options:
      optionsPress(m);
      optionsRelease();
      break;
    case Screen::Death:
      if (deathButtonAt(*ui_, m.x, m.y)) {
        if (level_.hardcore) {
          // Extremo: el mundo se acaba; se borra como en el juego
          const std::string folder = save_ ? save_->dir().filename().string() : "";
          leaveWorld();
          if (!folder.empty()) WorldSave::remove(folder);
          openScreen(Screen::Title);
        } else if (net_) {
          net_->sendRespawn();
        } else {
          session_->respawn();
          setScreen(Screen::None);
        }
      }
      break;
    default: break;
  }
}


int Game::guiScaleFor(int w, int h) const {
  const int autoScale = Ui::autoScale(w, h);
  return settings_.guiScale > 0 ? std::min(settings_.guiScale, autoScale) : autoScale;
}







void Game::pickBlock() {
  const auto& t = session_->target();
  if (!t || !session_->player().creative()) return;
  const BlockState s = terrain_->world().block(t->block.x, t->block.y, t->block.z);
  const ItemStack pick = pickItem(s);
  if (pick.empty()) return;
  PlayerInventory& inv = session_->player().inventory;
  for (int i = 0; i < PlayerInventory::kHotbar; i++)
    if (inv.slot(i).id == pick.id && inv.slot(i).meta == pick.meta) { inv.select(i); return; }
  inv.selected() = ItemStack(pick.id, std::min(64, itemInfo(pick.id).stackSize), pick.meta);
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
      if (screen_ == Screen::Options) optionsDrag(mouseGui());
      if (creativeBarDrag_ && screen_ == Screen::Menu) dragCreativeBar(mouseGui().y);
      if (grabbed_ && screen_ == Screen::None) {
        // Solo se gira con el ratón capturado de verdad, y el primer movimiento tras capturarlo se
        // descarta: al entrar el navegador puede mandar un salto de toda la pantalla
        const bool locked = mouseLocked();
        const bool first = locked && !motionLocked_;
        motionLocked_ = locked;
        if (!locked || first) break;
        const float sens = 0.0022f * settings_.sensitivityScale();
        cam_.yaw -= e.motion.xrel * sens;
        cam_.pitch = std::clamp(cam_.pitch - e.motion.yrel * sens * (settings_.invertMouse ? -1.0f : 1.0f), -1.5607f, 1.5607f);
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.which == SDL_TOUCH_MOUSEID) break;
      touch_.setActive(false);  // un ratón de verdad: modo escritorio
      mouseX_ = e.button.x;
      mouseY_ = e.button.y;
      if (screen_ == Screen::Options) {
        if (waitingKey_ >= 0) waitingKey_ = -1;  // un clic cancela la espera de tecla
        else if (e.button.button == SDL_BUTTON_LEFT) optionsPress(mouseGui());
        break;
      }
      if (screen_ != Screen::None) {
        clickScreen(e.button.button == SDL_BUTTON_RIGHT ? 1 : 0, shift);
        break;
      }
      if (!mouseLocked()) { setMouseGrab(true); break; }  // el primer clic solo captura el ratón
      if (e.button.button == SDL_BUTTON_LEFT) leftHeld_ = attackPressed_ = true;
      if (e.button.button == SDL_BUTTON_RIGHT) rightHeld_ = usePressed_ = true;
      if (e.button.button == SDL_BUTTON_MIDDLE) pickBlock();
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_LEFT && screen_ == Screen::Options) optionsRelease();
      if (e.button.button == SDL_BUTTON_LEFT) creativeBarDrag_ = false;
      if (e.button.button == SDL_BUTTON_LEFT) leftHeld_ = false;
      if (e.button.button == SDL_BUTTON_RIGHT) rightHeld_ = false;
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      if (screen_ == Screen::Worlds) worldScroll_ -= e.wheel.y * 18.0f;
      if (screen_ == Screen::Multiplayer) serverScroll_ -= e.wheel.y * 18.0f;
      if (screen_ == Screen::Skins) skinScroll_ -= e.wheel.y * 18.0f;
      if (screen_ == Screen::Achievements) achScroll_.y -= e.wheel.y * 26.0f;
      else if (screen_ == Screen::Options) optionsScroll(-e.wheel.y * 24.0f);
      else if (screen_ == Screen::Menu && session_->menu()) session_->menu()->scroll(e.wheel.y > 0 ? -1 : 1);
      else if (screen_ == Screen::None && e.wheel.y != 0) {
        const int cur = session_->player().inventory.selectedIndex();
        selectSlot_ = (cur + (e.wheel.y > 0 ? -1 : 1) + 9) % 9;
      }
      break;
    case SDL_EVENT_TEXT_INPUT:
      // El carácter de la tecla que abrió el chat no se escribe
      if (e.text.timestamp <= suppressTextUntil_ && e.text.text &&
          (std::string_view(e.text.text) == "t" || std::string_view(e.text.text) == "T" || std::string_view(e.text.text) == "/")) {
        suppressTextUntil_ = 0;
        break;
      }
      if (inMenuScreen() || screen_ == Screen::Chat || creativeSearchActive()) menuText(e.text.text);
      break;
    case SDL_EVENT_KEY_DOWN: {
      const SDL_Scancode sc = e.key.scancode;
      if (screen_ == Screen::Options && waitingKey_ >= 0) {
        optionsKey(sc);
        break;
      }
      if (inMenuScreen()) {
        menuKey(sc);
        break;
      }
      if (screen_ == Screen::Chat) {
        // Chat: Intro envía, Esc cierra, flechas para repetir lo escrito antes
        if (sc == SDL_SCANCODE_ESCAPE) {
          openScreen(Screen::None);
        } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
          const std::string line = chatField_.text;
          if (!line.empty()) chatHistory_.push_back(line);
          openScreen(Screen::None);
          runCommand(line);
        } else if (sc == SDL_SCANCODE_BACKSPACE) {
          chatField_.backspace();
        } else if ((sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) && !chatHistory_.empty()) {
          const int n = static_cast<int>(chatHistory_.size());
          chatHistoryPos_ = std::clamp((chatHistoryPos_ < 0 ? n : chatHistoryPos_) + (sc == SDL_SCANCODE_UP ? -1 : 1), 0, n - 1);
          chatField_.text = chatHistory_[chatHistoryPos_];
        }
        break;
      }
      if (screen_ == Screen::None && (sc == SDL_SCANCODE_T || sc == SDL_SCANCODE_SLASH || sc == SDL_SCANCODE_KP_DIVIDE) && !e.key.repeat) {
        chatField_ = {};
        chatField_.maxLength = 100;
        chatHistoryPos_ = -1;
        openScreen(Screen::Chat);
        // La tecla que abre el chat también escribe una letra: la de "/" se queda, la "t" no
        if (sc != SDL_SCANCODE_T) chatField_.text = "/";
        suppressTextUntil_ = e.key.timestamp + 100'000'000ull;
        break;
      }
      if (creativeSearchActive() && sc == SDL_SCANCODE_BACKSPACE) {
        session_->menu()->eraseSearchChar();
        break;
      }
      if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 && screen_ == Screen::None) selectSlot_ = sc - SDL_SCANCODE_1;
      if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 && screen_ == Screen::Menu && !creativeSearchActive() && !e.key.repeat) {
        // Creativo: la tecla numérica sobre un objeto lo pone entero en esa casilla de la barra (o intercambia casillas)
        Menu* menu = session_->menu();
        if (menu && menu->kind() == MenuKind::Creative) {
          const glm::vec2 m = mouseGui();
          bool inside = false;
          const int slot = menuSlotAt(*ui_, *menu, m.x, m.y, inside);
          const InvSnapshot before = snapshotInventory();
          menu->hotkey(slot, sc - SDL_SCANCODE_1);
          if (net_) netCreativeSync(before);
        }
      }
      if (sc == SDL_SCANCODE_ESCAPE) {
        if (screen_ == Screen::None) setScreen(Screen::Pause);
        else if (screen_ == Screen::Options) optionsBack();
        else if (screen_ != Screen::Death) setScreen(Screen::None);
        break;
      }
      const bool playing = screen_ == Screen::None;
      if (isKey(sc, KeyAction::Inventory) && !creativeSearchActive()) {  // (buscando, la tecla de inventario escribe)
        if (playing && !session_->player().dead) {
          session_->openInventory();
          setScreen(Screen::Menu);
        } else if (screen_ == Screen::Menu) {
          setScreen(Screen::None);
        }
      }
      if (isKey(sc, KeyAction::Drop) && playing) {
        if (SDL_GetModState() & SDL_KMOD_CTRL) dropStackPressed_ = true;
        else dropPressed_ = true;
      }
      if (e.key.repeat) break;  // lo de abajo solo al pulsar, no al mantener
      if (isKey(sc, KeyAction::Jump)) jumpPressed_ = true;
      if (isKey(sc, KeyAction::Forward) && playing) {
        // Doble toque de adelante (en menos de 7 ticks): correr, como en el juego
        if (e.key.timestamp - lastForwardTap_ < 350'000'000ull) sprintLatched_ = true;
        lastForwardTap_ = e.key.timestamp;
      }
      if (isKey(sc, KeyAction::Sprint) && playing && settings_.toggleSprint) sprintToggled_ = !sprintToggled_;
      if (isKey(sc, KeyAction::Sneak) && playing && settings_.toggleSneak) sneakToggled_ = !sneakToggled_;
      if (isKey(sc, KeyAction::Perspective) && playing) {
        settings_.perspective = (settings_.perspective + 1) % 3;
        saveSettings();
      }
      if (isKey(sc, KeyAction::HideHud)) hideHud_ = !hideHud_;
      if (isKey(sc, KeyAction::Debug)) opt_.showDebug = !opt_.showDebug;
      if (isKey(sc, KeyAction::Screenshot)) wantScreenshot_ = true;
      if (isKey(sc, KeyAction::Fullscreen)) {
        const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(window_, !fs);
      }
      if (sc == SDL_SCANCODE_PAGEUP) {
        settings_.renderDistance = std::min(Settings::kMaxRenderDistance, settings_.renderDistance + 1);
        saveSettings();
      }
      if (sc == SDL_SCANCODE_PAGEDOWN) {
        settings_.renderDistance = std::max(Settings::kMinRenderDistance, settings_.renderDistance - 1);
        saveSettings();
      }
      break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (screen_ == Screen::None && !touch_.active() && spawned_) setScreen(Screen::Pause);
      break;    default: break;
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
      creativeStopTap_ = std::abs(creativeFling_) >= 8.0f;  // tocar frena el deslizamiento que quedara (y no cuenta como toque)
      creativeFling_ = creativeScrollAccum_ = creativeVel_ = 0;
      creativeLastMoveNs_ = e.tfinger.timestamp;
      if (screen_ == Screen::Menu && session_->menu() && creativeScrollbarAt(*ui_, *session_->menu(), gui.x, gui.y)) {
        creativeBarDrag_ = true;  // el dedo sobre la barra la lleva consigo
        dragCreativeBar(gui.y);
      }
      if (screen_ == Screen::Options) {
        // Los deslizadores responden al tocar; los botones, al levantar el dedo sin arrastrar
        const std::vector<OptionItem> items = optionItems();
        const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_);
        const int i = optionListHit(l, items, gui.x, gui.y);
        if (i >= 0 && items[i].type == OptionItem::Type::Slider) optionsPress(gui);
      }
      break;
    case SDL_EVENT_FINGER_MOTION:
      if (screenFinger_ != e.tfinger.fingerID) break;
      if (glm::length(gui - screenFingerStart_) > 8.0f) screenFingerMoved_ = true;
      if (screen_ == Screen::Options) {
        if (optDrag_ >= 0) {
          optionsDrag(gui);
        } else if (screenFingerMoved_) {  // arrastrar la lista la desplaza
          optionsScroll(screenFingerLast_.y - gui.y);
          screenFingerLast_ = gui;
        }
      }
      if (screenFingerMoved_ && (screen_ == Screen::Worlds || screen_ == Screen::Multiplayer || screen_ == Screen::Skins)) {
        (screen_ == Screen::Worlds ? worldScroll_ : screen_ == Screen::Skins ? skinScroll_ : serverScroll_) += screenFingerLast_.y - gui.y;
        screenFingerLast_ = gui;
      }
      if (screenFingerMoved_ && screen_ == Screen::Achievements) {
        achScroll_.y += screenFingerLast_.y - gui.y;
        screenFingerLast_ = gui;
      }
      if (screen_ == Screen::Menu && session_->menu()) {
        if (creativeBarDrag_) {
          dragCreativeBar(gui.y);
        } else if (screenFingerMoved_ && session_->menu()->kind() == MenuKind::Creative) {
          // Arrastrar desplaza la lista (sigue al dedo); se guarda la velocidad para que siga deslizándose al soltar
          const float dy = screenFingerLast_.y - gui.y;  // hacia arriba (+) baja la lista
          const u64 now = e.tfinger.timestamp;  // (la hora del toque, no la de procesarlo: con frames lentos no cambia la velocidad)
          const float dt = std::max(0.001f, static_cast<float>(now - creativeLastMoveNs_) / 1e9f);
          creativeVel_ = creativeVel_ * 0.6f + (dy / dt) * 0.4f;
          creativeLastMoveNs_ = now;
          creativeScrollAccum_ += dy;
          const int rows = static_cast<int>(creativeScrollAccum_ / 18.0f);
          if (rows != 0) {
            session_->menu()->scroll(rows);
            creativeScrollAccum_ -= static_cast<float>(rows) * 18.0f;
          }
          screenFingerLast_ = gui;
        }
      }
      break;
    case SDL_EVENT_FINGER_UP: {
      if (screenFinger_ != e.tfinger.fingerID) break;
      screenFinger_.reset();
      if (creativeBarDrag_) {  // se llevaba la barra: nada más que hacer
        creativeBarDrag_ = false;
        break;
      }
      if (screen_ == Screen::Menu && screenFingerMoved_ && session_->menu() && session_->menu()->kind() == MenuKind::Creative) {
        // Soltar con el dedo en movimiento: la lista sigue deslizándose y frena sola
        const bool moving = e.tfinger.timestamp - creativeLastMoveNs_ < 90'000'000ull;  // (si el dedo se paró antes de soltar, no hay empuje)
        creativeFling_ = moving && std::abs(creativeVel_) > 60.0f ? std::clamp(creativeVel_, -2500.0f, 2500.0f) : 0.0f;
      }
      if (screen_ == Screen::Options) {
        if (optDrag_ >= 0) optionsRelease();
        else if (!screenFingerMoved_) optionsPress(gui);
        break;
      }
      if (screenFingerMoved_) break;
      if (std::exchange(creativeStopTap_, false)) break;
      if (screen_ == Screen::Chat) {
        // Chat táctil: la X cierra; tocar en otro sitio envía lo escrito (o vuelve a sacar el teclado)
        if (touch_.closeHit(gui)) {
          openScreen(Screen::None);
        } else if (!chatField_.text.empty()) {
          const std::string line = chatField_.text;
          chatHistory_.push_back(line);
          openScreen(Screen::None);
          runCommand(line);
        } else {
          SDL_StartTextInput(window_);
        }
        break;
      }
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
  const TouchInput t = touch_.consume();
  if (t.haptic > 0) vibrate(t.haptic);
  const bool wasSprinting = session_->player().sprinting;
  if (screen_ == Screen::None) {
    const bool fwd = keyHeld(KeyAction::Forward);
    if (!fwd) sprintLatched_ = false;  // soltar adelante deja de correr
    in.move.forward = (fwd ? 1.0f : 0.0f) - (keyHeld(KeyAction::Back) ? 1.0f : 0.0f) + t.forward;
    in.move.strafe = (keyHeld(KeyAction::Right) ? 1.0f : 0.0f) - (keyHeld(KeyAction::Left) ? 1.0f : 0.0f) + t.strafe;
    in.move.forward = std::clamp(in.move.forward, -1.0f, 1.0f);
    in.move.strafe = std::clamp(in.move.strafe, -1.0f, 1.0f);
    in.move.jump = keyHeld(KeyAction::Jump) || t.jump;
    in.move.sneak = (settings_.toggleSneak ? sneakToggled_ : keyHeld(KeyAction::Sneak)) || t.sneak;
    in.move.sprint = (settings_.toggleSprint ? sprintToggled_ : keyHeld(KeyAction::Sprint)) || sprintLatched_ || t.sprint;
    in.move.autoJump = settings_.autoJump < 0 ? touch_.active() : settings_.autoJump == 1;
    in.attack = leftHeld_;
    in.use = rightHeld_ || t.use;
    // Demos del arco: tensado a tope, o tensado y soltado para ver las flechas clavadas
    if (opt_.demo == "arco") in.use = true;
    if (opt_.demo == "arco2") in.use = demoStep_ < 22;
    if (opt_.demo.rfind("arco", 0) == 0) demoStep_++;
    in.attackPressed = attackPressed_ || t.attackPressed;
    in.usePressed = usePressed_ || t.usePressed;
    in.drop = dropPressed_ || t.drop;
    in.dropStack = dropStackPressed_ || t.dropStack;
    in.jumpPressed = jumpPressed_ || t.jumpPressed;
    // Dedo mantenido sobre el mundo: rompe (o come, o tensa el arco, según lo que se lleve en la mano);
    // el botón Atacar siempre golpea
    const Player& tp = session_->player();
    const bool eatOrBow = (!tp.creative() && foodValue(tp.inventory.selected()) && tp.food < 20) || tp.inventory.selected().id == ItemId::bow;
    const bool holdUses = t.holdWorld && eatOrBow;
    if (holdUses) {
      in.use = true;  // (con el arco se mantiene el dedo y soltar dispara)
    } else if (t.attack) {
      in.attack = true;
      in.attackPressed = in.attackPressed || !touchAttacking_;
    }
    touchAttacking_ = t.attack && !holdUses;
    if (t.tap) in.usePressed = in.tapAttack = true;  // tocar usa o coloca; sobre una criatura, la golpea
    if (t.aim) in.aimDir = aimFromGui(*t.aim);
    if (t.perspective) {
      settings_.perspective = (settings_.perspective + 1) % 3;
      saveSettings();
    }
  } else {
    touchAttacking_ = false;
  }
  in.selectSlot = t.selectSlot >= 0 ? t.selectSlot : selectSlot_;
  in.yaw = cam_.yaw;
  in.pitch = cam_.pitch;
  in.worldTime = worldTime_;
  in.randomTickSpeed = std::max(0, std::atoi(level_.rule("randomTickSpeed", "3").c_str()));
  in.fromTouch = touch_.active();
  attackPressed_ = usePressed_ = jumpPressed_ = dropPressed_ = dropStackPressed_ = false;
  selectSlot_ = -1;

  // Animales en los chunks recién generados
  for (const ChunkPos& c : terrain_->takeNewChunks()) session_->populateChunk(c.x, c.z);
  session_->tick(in);
  tickNet();
  if (const auto wake = session_->takeSleepRequest()) {
    worldTime_ = *wake;
    chatMessage("Has dormido hasta la mañana.");
  }
  {
    // Balanceo al andar (por tick, así va igual a 60 que a 120 fps)
    const Player& p = session_->player();
    // Chocar con algo (o quedarse sin hambre) corta la carrera: hay que volver a pulsar dos veces
    if (sprintLatched_ && wasSprinting && !p.sprinting) sprintLatched_ = false;
    const float speed = static_cast<float>(glm::length(glm::dvec2(p.pos.x - p.prevPos.x, p.pos.z - p.prevPos.z)));
    prevWalked_ = walked_;
    prevBobAmp_ = bobAmp_;
    walked_ += speed * 0.6f;
    const float target = (p.onGround && !p.flying && !p.dead) ? std::min(0.1f, speed) : 0.0f;
    bobAmp_ += (target - bobAmp_) * 0.4f;
    // Cuerpo y piernas para la tercera persona: el cuerpo gira hacia la cabeza (como mucho 50 grados
    // de diferencia) y, al andar, la sigue del todo
    prevBodyYaw_ = bodyYaw_;
    prevLimbAmount_ = limbAmount_;
    limbAmount_ += (std::min(1.0f, speed * 4.0f) - limbAmount_) * 0.4f;
    limbSwing_ += limbAmount_;
    float diff = p.yaw - bodyYaw_;
    while (diff > 3.14159265f) diff -= 6.2831853f;
    while (diff < -3.14159265f) diff += 6.2831853f;
    if (speed > 0.01f) bodyYaw_ += diff * 0.3f;
    else if (std::abs(diff) > 0.87f) bodyYaw_ += diff - std::copysign(0.87f, diff);
  }

  // Animaciones y avisos
  if (in.attack && session_->target() && screen_ == Screen::None && swing_ <= 0) swing_ = 1.0f;
  const std::vector<SessionEvent> events = session_->takeEvents();
  tickEffects(events);
  for (const SessionEvent& ev : events) {
    switch (ev.type) {
      case SessionEvent::Type::BlockPlaced: swing_ = 1.0f; break;
      case SessionEvent::Type::PlayerHurt: hurtFlash_ = 1.0f; break;
      case SessionEvent::Type::Explosion: {
        const double d = glm::length(ev.where - session_->player().pos);
        shake_ = std::max(shake_, static_cast<float>(std::clamp(1.0 - d / 24.0, 0.0, 1.0)));
        break;
      }
      case SessionEvent::Type::PlayerDied: setScreen(Screen::Death); break;
      case SessionEvent::Type::Achievement: toasts_.push_back({ev.value, -1.0}); break;
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
  if (screen_ == Screen::None && t.chat) {
    chatField_ = {};
    chatField_.maxLength = 100;
    chatHistoryPos_ = -1;
    openScreen(Screen::Chat);  // abre también el teclado en pantalla
  }
}

void Game::syncQuality() {
  // Si en Ajustes se ha cambiado la distancia o la resolución, manda lo elegido; el control parte de ahí
  if (settings_.renderDistance == userDist_ && settings_.renderScale == userScale_) return;
  userDist_ = effDist_ = settings_.renderDistance;
  userScale_ = effScale_ = settings_.renderScale;
  quality_.setUser(userDist_, userScale_);
}

void Game::adaptQuality() {
  // Una vez por segundo (ver client/quality.h). La carga inicial del mundo no cuenta.
  if (settings_.adaptiveFps <= 0 || !inWorld_ || !spawned_ || opt_.benchSeconds > 0) {
    effDist_ = userDist_;
    effScale_ = userScale_;
    adaptWarm_ = 0;
    return;
  }
  if (!terrain_->settled() && adaptWarm_ < 6.0) {
    adaptWarm_ = 0;
    return;
  }
  adaptWarm_ += 1.0;
  if (adaptWarm_ < 6.0) return;
  if (quality_.update(fps_, cpuAvg_, gpuTimer_.supported() ? gpuTimer_.ms() : 0.0, settings_.adaptiveFps)) {
    effDist_ = quality_.distance();
    effScale_ = quality_.scale();
    log::info("rendimiento automático: {} fps (objetivo {}): distancia {}, resolución {:.0f} %", fps_, settings_.adaptiveFps, effDist_,
              effScale_ * 100.0f);
  }
}

void Game::logBench() {
  const TerrainStats st = terrain_->stats();
  std::string phases, json;
  for (std::size_t i = 0; i < FramePerf::kPhases; i++) {
    const auto p = static_cast<Phase>(i);
    phases += std::format(" {} {:.2f}", phaseName(p), perf_.benchAvg(p));
    json += std::format("{}\"{}\":{:.3f}", i ? "," : "", phaseName(p), perf_.benchAvg(p));
  }
  const double avg = perf_.periodAvg();
  const std::string gpu = gpuTimer_.supported() ? std::format("{:.2f} ms", gpuTimer_.ms()) : std::string("n/d");
  log::info("bench: {} frames, {:.1f} fps | ms por frame: media {:.2f}, p50 {:.2f}, p95 {:.2f}, p99 {:.2f}, peor {:.2f} | CPU ms:{} | GPU {} | llamadas {}, quads {}, secciones {}/{} | oclusión {} recorridas, {:.2f} ms | rd {}",
            perf_.recordedFrames(), avg > 0 ? 1000.0 / avg : 0.0, avg, perf_.percentile(0.5), perf_.percentile(0.95), perf_.percentile(0.99),
            perf_.periodMax(), phases, gpu, st.drawCalls, st.drawnQuads, st.drawnSections, st.sections, st.visited, st.cullMs, settings_.renderDistance);
  log::info("bench-json: {{\"frames\":{},\"fps\":{:.2f},\"ms\":{{\"avg\":{:.3f},\"p50\":{:.3f},\"p95\":{:.3f},\"p99\":{:.3f},\"max\":{:.3f}}},\"cpu\":{{{}}},\"gpuMs\":{:.3f},\"draws\":{},\"quads\":{},\"sections\":{},\"sectionsTotal\":{},\"visited\":{},\"cullMs\":{:.3f},\"rd\":{}}}",
            perf_.recordedFrames(), avg > 0 ? 1000.0 / avg : 0.0, avg, perf_.percentile(0.5), perf_.percentile(0.95), perf_.percentile(0.99),
            perf_.periodMax(), json, gpuTimer_.supported() ? gpuTimer_.ms() : -1.0, st.drawCalls, st.drawnQuads, st.drawnSections, st.sections,
            st.visited, st.cullMs, settings_.renderDistance);
}

double Game::debugValue(int what) const {
  // 100 + 2 b y 101 + 2 b: centro del botón b en puntos de la ventana; 130 a 132: joystick (x, y, radio)
  if (what >= 100 && what < 140 && ui_) {
    const float toWindow = static_cast<float>(ui_->scale()) / std::max(0.5f, SDL_GetWindowPixelDensity(window_));
    if (what >= 130) {
      const glm::vec2 c = touch_.stickCenter();
      return (what == 130 ? c.x : what == 131 ? c.y : touch_.stickRadius()) * toWindow;
    }
    const int b = (what - 100) / 2;
    if (b >= static_cast<int>(TouchButton::Count)) return 0.0;
    const glm::vec2 c = touch_.buttonCenter(static_cast<TouchButton>(b));
    return (what % 2 == 0 ? c.x : c.y) * toWindow;
  }
  // 140 a 147: inventario creativo (pestaña, fila, objetos en la lista, filas máximas, letras buscadas, objeto del cursor,
  // objeto y cantidad de la casilla 0 de la barra, velocidad de deslizamiento); 150 + 2 t y 151 + 2 t: centro de la pestaña t; 180 a 183: barra de
  // desplazamiento (x, y de arriba, y de abajo) y campo de búsqueda (x, y: 183 y 184); 200 + 2 s y 201 + 2 s: centro de la casilla s
  if (what >= 140 && what < 340 && ui_ && session_ && spawned_) {
    Menu* menu = session_->menu();
    const bool creative = menu && menu->kind() == MenuKind::Creative;
    const float toWindow = static_cast<float>(ui_->scale()) / std::max(0.5f, SDL_GetWindowPixelDensity(window_));
    const Player& pl = session_->player();
    switch (what) {
      case 140: return creative ? static_cast<double>(static_cast<int>(menu->creativeTab())) : -1.0;
      case 141: return creative ? menu->scrollRow() : 0.0;
      case 142: return creative ? menu->listSize() : 0.0;
      case 143: return creative ? menu->maxScroll() : 0.0;
      case 144: return creative ? static_cast<double>(menu->searchText().size()) : 0.0;
      case 145: return pl.cursor.empty() ? 0.0 : pl.cursor.id;
      case 146: return pl.inventory.slot(0).empty() ? 0.0 : pl.inventory.slot(0).id;
      case 147: return pl.inventory.slot(0).empty() ? 0.0 : pl.inventory.slot(0).count;
      case 148: return creativeFling_;
      case 149: return creativeVel_;
      default: break;
    }
    if (!menu) return 0.0;
    const glm::vec2 o = menuOriginGui(*ui_, *menu);
    if (what >= 150 && what < 174 && creative) {
      const glm::vec2 c = creativeTabCenterGui(*ui_, *menu, static_cast<CreativeTab>((what - 150) / 2));
      return (what % 2 == 0 ? c.x : c.y) * toWindow;
    }
    if (what >= 180 && what <= 184) {
      const float v[] = {o.x + 181.0f, o.y + 18.0f, o.y + 130.0f, o.x + 126.0f, o.y + 10.0f};
      return v[what - 180] * toWindow;
    }
    if (what >= 200) {
      const std::size_t s = static_cast<std::size_t>((what - 200) / 2);
      if (s >= menu->slots().size()) return 0.0;
      const MenuSlot& slot = menu->slots()[s];
      return ((what % 2 == 0 ? o.x + slot.x : o.y + slot.y) + 8.0f) * toWindow;
    }
    return 0.0;
  }
  if (!session_ || !spawned_) return what == 10 ? static_cast<double>(static_cast<int>(screen_)) : 0.0;
  const Player& p = session_->player();
  switch (what) {
    case 0: return cam_.yaw;
    case 1: return cam_.pitch;
    case 2: return p.pos.x;
    case 3: return p.pos.y;
    case 4: return p.pos.z;
    case 5: return p.sneaking ? 1.0 : 0.0;
    case 6: return p.sprinting ? 1.0 : 0.0;
    case 7: return p.flying ? 1.0 : 0.0;
    case 8: return p.inventory.selectedIndex();
    case 9: return p.onGround ? 1.0 : 0.0;
    case 10: return static_cast<double>(static_cast<int>(screen_));
    case 11: return terrain_->stats().drawCalls;
    case 12: return terrain_->stats().drawnQuads;
    case 13: return terrain_->stats().drawnSections;
    case 14: return terrain_->stats().sections;
    default: return 0.0;
  }
}

void Game::vibrate(int ms) {
#ifdef __EMSCRIPTEN__
  mcw_js_vibrate(ms);
#else
  (void)ms;  // (en escritorio no hay nada que vibre)
#endif
}

void Game::applyTouchLook(double dt) {
  // Lo arrastrado se convierte en giro (0,0045 rad por punto de pantalla, como en un móvil) y se aplica
  // poco a poco: con tau = 0 es inmediato; con el suavizado por defecto (unos 12 ms) quita el temblor
  // entre el ritmo de los toques y el de los frames sin que se note retraso
  const glm::vec2 look = touch_.takeLook();
  if (look.x != 0 || look.y != 0) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    const float perGuiPixel =
        0.0045f * settings_.touchSensitivityScale() * guiScaleFor(w, h) / std::max(0.5f, SDL_GetWindowPixelDensity(window_));
    lookPending_ += look * perGuiPixel;
  }
  if (lookPending_.x == 0 && lookPending_.y == 0) return;
  const float tau = settings_.touchSmoothing * 0.03f;
  const float k = tau <= 0.0f ? 1.0f : 1.0f - std::exp(-static_cast<float>(dt) / tau);
  glm::vec2 step = lookPending_ * k;
  if (std::abs(lookPending_.x - step.x) < 1e-5f && std::abs(lookPending_.y - step.y) < 1e-5f) step = lookPending_;
  lookPending_ -= step;
  cam_.yaw -= step.x;
  cam_.pitch = std::clamp(cam_.pitch - step.y, -1.5607f, 1.5607f);
}

bool Game::iterate() {
  u64 now = SDL_GetTicksNS();
  perf_.startFrame();
  pollNet();  // servidor: lo que haya llegado (también mientras se conecta)
  perf_.lap(Phase::Net);
  // Límite de FPS: en el navegador se salta el frame (el lienzo se queda como estaba); en nativo se espera
  rendered_ = true;
  if (settings_.fpsLimit > 0 && lastRender_ != 0) {
    const u64 interval = 1'000'000'000ull / static_cast<u64>(settings_.fpsLimit);
    const u64 since = now - lastRender_;
    if (since + 2'000'000ull < interval) {
#ifdef __EMSCRIPTEN__
      rendered_ = false;
      return !quit_;
#else
      SDL_DelayPrecise(interval - since);
      now = SDL_GetTicksNS();
#endif
    }
  }
  lastRender_ = now;
  // En el navegador, Esc suelta el puntero y la tecla no llega: si se pierde, al menú de pausa
  if (grabbed_ && !touch_.active()) {
    if (mouseLocked()) pointerLockSeen_ = true;
    else if (pointerLockSeen_ && screen_ == Screen::None && spawned_) setScreen(Screen::Pause);
  }
  applySettings();
  syncQuality();
  const double rawDt = static_cast<double>(now - lastTicks_) / 1e9;  // (la simulación se limita a 0,1 s; la medición no)
  const double dt = std::min(0.1, rawDt);
  lastTicks_ = now;
  runTime_ += dt;
  lastFrameDt_ = dt;

  // Presupuesto del hilo principal para recibir chunks y subir mallas: una parte del frame, para no
  // bajar de la frecuencia de la pantalla (8,3 ms a 120 Hz). Mientras carga no hay nada que mostrar.
  frameInterval_ += (std::clamp(dt, 1.0 / 240.0, 0.05) - frameInterval_) * 0.05;
  const double budget = spawned_ ? std::clamp(frameInterval_ * 1000.0 * 0.25, 1.0, 4.0) : 12.0;
  jobs_->pump(budget * 0.5);
  if (inWorld_) {
    const double used = (SDL_GetTicksNS() - now) / 1e6;
    const glm::dvec3 center = spawned_ ? session_->player().pos : spawn_;
    terrain_->update(center, effDist_, std::max(0.5, budget - used));
    trySpawn();
    // Guardado automático cada 45 s, repartido entre frames para no dar tirones
    autosaveTimer_ += dt;
    if (save_ && spawned_ && autosaveTimer_ >= 45.0) {
      autosaveTimer_ = 0;
      for (const Mob& m : session_->mobs())
        terrain_->markUnsaved({static_cast<int>(std::floor(m.pos.x)) >> 4, static_cast<int>(std::floor(m.pos.z)) >> 4});
      for (const ItemEntity& e : session_->items())
        terrain_->markUnsaved({static_cast<int>(std::floor(e.pos.x)) >> 4, static_cast<int>(std::floor(e.pos.z)) >> 4});
      level_.player = save::playerToNbt(session_->player(), spawn_, false);
      level_.dayTime = level_.time = static_cast<i64>(worldTime_);
      save_->saveLevel(level_);
      if (server_) server_->saveAll();
      pendingFlush_ = true;
    }
    if (save_ && terrain_->unsavedCount() > 0 && spawned_) terrain_->saveSome(1.0);
    if (pendingFlush_ && terrain_->unsavedCount() == 0) {
      pendingFlush_ = false;
      WorldSave::flush();
    }
  }
  if (quit_ && inWorld_) leaveWorld();  // cerrar la ventana guarda el mundo
  perf_.lap(Phase::Load);

  touch_.newFrame();
  updateMenuInertia(dt);
  // Mirar con el dedo, en cada frame (no en el tick: a 20 Hz la cámara iría a saltos)
  if (spawned_ && inWorld_ && screen_ == Screen::None) {
    applyTouchLook(dt);
  } else {
    touch_.takeLook();
    lookPending_ = {0, 0};
  }
  // Ticks del juego a 20 por segundo
  tickAccum_ += dt * 20.0;
  int ticks = 0;
  while (tickAccum_ >= 1.0 && ticks < 10) {
    tickAccum_ -= 1.0;
    ticks++;
    if (!opt_.freezeTime && inWorld_ && level_.ruleBool("doDaylightCycle", true)) worldTime_ += 1;
    const auto changed = opt_.fixedCam ? std::vector<int>{} : textures_->tick();  // (con cámara fija, agua y lava quietas)
    if (!changed.empty()) terrain_->refreshTextureLayers(*textures_, changed);
    if (spawned_ && inWorld_) gameTick();
    if (swing_ > 0) swing_ = std::max(0.0f, swing_ - 1.0f / 6.0f);
    if (nameTimer_ > 0) nameTimer_ -= 0.05f;
    if (hurtFlash_ > 0) hurtFlash_ = std::max(0.0f, hurtFlash_ - 0.1f);
    if (shake_ > 0) shake_ = std::max(0.0f, shake_ - 0.08f);
  }
  perf_.lap(Phase::Tick);
  const float partial = static_cast<float>(tickAccum_);
  if (spawned_) music_->update(dt, *audio_, settings_.volume * settings_.volMusic > 0.001f);
  // Subtítulos: lo que ha sonado, un texto por sonido (se refresca si se repite)
  for (const Audio::Heard& hd : audio_->takeHeard()) {
    const char* text = settings_.subtitles ? sfxSubtitle(hd.sfx) : nullptr;
    if (!text) continue;
    auto it = std::find_if(subtitles_.begin(), subtitles_.end(), [&](const Subtitle& st) { return st.text == text; });
    if (it == subtitles_.end()) subtitles_.push_back({text, hd.pos, hd.positional, runTime_});
    else *it = {text, hd.pos, hd.positional, runTime_};
  }
  std::erase_if(subtitles_, [&](const Subtitle& st) { return runTime_ - st.time > 3.0 || !settings_.subtitles; });

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  updateCamera(partial);
  audio_->setListener(cam_.pos, cam_.yaw);
  perf_.lap(Phase::Prep);
  render(w, h, partial);

  frames_++;
  fpsTimer_ += dt;
  const double cpuMs = (SDL_GetTicksNS() - now) / 1e6;
  cpuSum_ += cpuMs;
  cpuMaxAcc_ = std::max(cpuMaxAcc_, cpuMs);
  perf_.endFrame(rawDt * 1000.0);
  if (fpsTimer_ >= 1.0) {
    fps_ = frames_;
    cpuAvg_ = cpuSum_ / std::max(1, frames_);
    cpuMax_ = cpuMaxAcc_;
    perf_.roll();
    adaptQuality();
    if (opt_.logPerf) {
      const TerrainStats st = terrain_->stats();
      std::string phases;
      for (std::size_t i = 0; i < FramePerf::kPhases; i++)
        phases += std::format(" {} {:.2f}", phaseName(static_cast<Phase>(i)), perf_.avg(static_cast<Phase>(i)));
      log::info("perf: {} fps, CPU {:.2f} ms (peor {:.2f}) [{} ], GPU {}, chunks {}, gen {}, malla {}, llamadas {}, quads {}, secciones {}/{}, recorridas {} en {:.2f} ms",
                fps_, cpuAvg_, cpuMax_, phases.substr(1), gpuTimer_.supported() ? std::format("{:.2f} ms", gpuTimer_.ms()) : std::string("n/d"),
                st.chunks, st.pendingGen, st.pendingMesh, st.drawCalls, st.drawnQuads, st.drawnSections, st.sections, st.visited, st.cullMs);
    }
    frames_ = 0;
    cpuSum_ = cpuMaxAcc_ = 0;
    fpsTimer_ -= 1.0;
  }
  if (!loggedLoaded_ && spawned_ && terrain_->settled()) {
    loggedLoaded_ = true;
    log::info("mundo cargado en {:.2f} s (distancia {})", runTime_, settings_.renderDistance);
  }
  // --bench: con el mundo listo, un segundo de calentamiento, N segundos de medición y a salir
  if (opt_.benchSeconds > 0 && !quit_ && spawned_ && terrain_->settled()) {
    if (benchStart_ < 0) benchStart_ = runTime_;
    const double t = runTime_ - benchStart_;
    if (!perf_.recording() && t >= 1.0) perf_.startRecording();
    if (opt_.benchSpin && t >= 1.0) cam_.yaw += static_cast<float>(dt) * 1.5707963f;
    if (t >= 1.0 + opt_.benchSeconds) {
      perf_.stopRecording();
      logBench();
      quit_ = true;
    }
  }

  // Captura automática cuando el mundo está cargado (o tras 60 s como máximo)
  if (!opt_.screenshotPath.empty() && !screenshotDone_) {
    if (settledAt_ < 0 && spawned_ && terrain_->settled()) {
      settledAt_ = runTime_;
      // Demo de explosión: justo cuando el mundo está listo, para que salgan las partículas en la captura
      if (opt_.demo == "boom") {
        const Player& pl = session_->player();
        session_->explode(pl.pos + glm::dvec3(-std::sin(pl.yaw) * 7.0, 0.5, -std::cos(pl.yaw) * 7.0), 3.0f);
      }
    }
    if ((settledAt_ >= 0 && runTime_ - settledAt_ >= opt_.screenshotDelay) || runTime_ > 60 ||
        (!inWorld_ && runTime_ > 1.0 + opt_.screenshotDelay)) {
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
  if (quit_ && inWorld_) leaveWorld();  // salir del juego guarda el mundo
  return !quit_;
}

void Game::updateCamera(float partial) {
  const Player& p = session_->player();
  if (!spawned_) {
    cam_.pos = spawn_ + glm::dvec3(0, 20, 0);
    return;
  }
  if (opt_.fixedCam && opt_.startPos) {
    cam_.pos = *opt_.startPos + glm::dvec3(0, 1.62, 0);
    cam_.fovDeg = settings_.fov;
    return;
  }
  const glm::dvec3 pos = p.prevPos + (p.pos - p.prevPos) * static_cast<double>(partial);
  cam_.pos = pos + glm::dvec3(0, p.eyePos().y - p.pos.y, 0);
  // Balanceo de la vista al andar: un vaivén lateral y un pequeño bote hacia abajo
  if (settings_.viewBobbing) {
    const float phase = (prevWalked_ + (walked_ - prevWalked_) * partial) * 3.14159265f;
    const float amp = prevBobAmp_ + (bobAmp_ - prevBobAmp_) * partial;
    const glm::vec3 f = cam_.forward();
    const glm::dvec3 right = glm::normalize(glm::dvec3(-f.z, 0.0, f.x));
    cam_.pos += right * static_cast<double>(std::sin(phase) * amp * 0.5f) +
                glm::dvec3(0, -std::abs(std::cos(phase) * amp), 0);
  }
  if (shake_ > 0) {
    const float t = static_cast<float>(runTime_) * 40.0f;
    cam_.pos += glm::dvec3(std::sin(t) * 0.08 * shake_, std::sin(t * 1.3f) * 0.08 * shake_, std::cos(t * 0.9f) * 0.08 * shake_);
  }
  // FOV: se abre al correr y al volar (suavizado según el tiempo, no según los fps)
  float target = (p.sprinting ? 1.15f : 1.0f) * (p.flying ? 1.1f : 1.0f);
  if (const int bow = session_->bowTicks(); bow > 0) {  // el arco tensado cierra el encuadre hasta un 15 %
    const float f = std::min(1.0f, static_cast<float>(bow) / 20.0f);
    target *= 1.0f - f * f * 0.15f;
  }
  fovMod_ += (target - fovMod_) * (1.0f - std::exp(-static_cast<float>(lastFrameDt_) * 12.0f));
  cam_.fovDeg = settings_.fov * fovMod_;
}

Camera Game::viewCamera(int w, int h) const {
  Camera v = cam_;
  const Player& p = session_->player();
  if (settings_.perspective != 0 && spawned_ && !p.dead) {
    // Tercera persona: hasta 4 bloques detrás (o delante, mirando al jugador), sin atravesar paredes.
    // Se prueban varios rayos alrededor del ojo para que el plano cercano no se meta en un bloque.
    const glm::dvec3 f(cam_.forward());
    const glm::dvec3 dir = settings_.perspective == 1 ? -f : f;
    double dist = 4.0;
    for (int i = 0; i < 8; i++) {
      const glm::dvec3 o((i & 1) ? 0.1 : -0.1, (i & 2) ? 0.1 : -0.1, (i & 4) ? 0.1 : -0.1);
      if (auto hit = raycastBlocks(terrain_->world(), cam_.pos + o, dir, dist)) dist = std::max(0.0, std::min(dist, hit->distance - 0.1));
    }
    v.pos = cam_.pos + dir * dist;
    if (settings_.perspective == 2) {
      v.yaw += 3.14159265f;
      v.pitch = -v.pitch;
    }
  }
  v.update(w, h);
  return v;
}

bool Game::bindSceneTarget(int w, int h) {
  // Resolución 3D al 100 %: directamente en la pantalla
  const float scale = std::clamp(effScale_, 0.25f, 1.0f);
  if (scale >= 0.999f) return false;
  const int sw = std::max(1, static_cast<int>(w * scale)), sh = std::max(1, static_cast<int>(h * scale));
  if (!sceneFbo_) {
    glGenFramebuffers(1, &sceneFbo_);
    glGenTextures(1, &sceneColor_);
    glGenRenderbuffers(1, &sceneDepth_);
  }
  if (sw != sceneW_ || sh != sceneH_) {
    sceneW_ = sw;
    sceneH_ = sh;
    glBindTexture(GL_TEXTURE_2D, sceneColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sw, sh);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      log::warn("no se pudo crear el framebuffer de resolución reducida");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      settings_.renderScale = effScale_ = userScale_ = 1.0f;
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
  glViewport(0, 0, sw, sh);
  return true;
}

void Game::render(int w, int h, float partial) {
  if (!inWorld_) {
    // Fuera de la partida: solo menús (título, mundos, ajustes...)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.08f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ui_->begin(w, h, guiScaleFor(w, h));
    const glm::vec2 m = mouseGui();
    if (screen_ == Screen::Options) {
      drawMenuBackground(*ui_);
      const std::vector<OptionItem> items = optionItems();
      const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_);
      optScroll_ = l.scroll;
      drawOptionList(*ui_, optionTitle(), items, l, m.x, m.y, optPage_ == OptPage::Main ? "Listo" : "Volver");
      if (waitingKey_ >= 0) ui_->textCentered(ui_->guiWidth() / 2.0f, 22, "Pulsa una tecla (Esc: cancelar, Supr: ninguna)", 0xFFFF80);
    } else {
      drawMenuScreen(w, h);
    }
    ui_->end();
    return;
  }
  gpuTimer_.begin();
  const bool scaled = bindSceneTarget(w, h);
  const int vw = scaled ? sceneW_ : w, vh = scaled ? sceneH_ : h;
  if (!scaled) glViewport(0, 0, w, h);
  cam_.farPlane = effDist_ * 16.0f * 2.0f + 400.0f;
  cam_.update(w, h);  // la de los ojos: para apuntar con el dedo
  const Camera view = viewCamera(vw, vh);
  const bool thirdPerson = settings_.perspective != 0 && spawned_ && !session_->player().dead;
  env_->update(worldTime_, effDist_ * 16.0f, settings_.brightness, view.forward());

  FogParams fog = env_->fog();
  if (!settings_.fog) {
    // Sin niebla: solo un fundido corto en el borde para que los chunks no aparezcan de golpe
    const float edge = effDist_ * 16.0f;
    fog.start = edge - 6.0f;
    fog.end = edge + 10.0f;
  }
  glClearColor(fog.color.r, fog.color.g, fog.color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  env_->drawSky(view);
  perf_.lap(Phase::Sky);

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
  terrain_->drawOpaque(view, env_->lightmap(), fog);
  perf_.lap(Phase::Terrain);
  glDisable(GL_CULL_FACE);
  if (!opt_.fixedCam) {
    itemRenderer_->drawWorldItems(session_->items(), view, partial, worldTime_, lightAt);
    entityRenderer_->drawMobs(session_->mobs(), view, partial, lightAt, fog,
                              std::min(80.0f * settings_.entityDistance, effDist_ * 16.0f));
    entityRenderer_->drawArrows(session_->arrows(), view, partial, lightAt, fog);
    entityRenderer_->drawOrbs(session_->orbs(), view, partial, lightAt, fog);
    drawOtherPlayers(view, partial, fog, lightAt);
  }

  const Player& player = session_->player();
  if (thirdPerson) {
    PlayerPose pp;
    pp.pos = player.prevPos + (player.pos - player.prevPos) * static_cast<double>(partial);
    float by = bodyYaw_ - prevBodyYaw_;
    while (by > 3.14159265f) by -= 6.2831853f;
    while (by < -3.14159265f) by += 6.2831853f;
    pp.bodyYaw = prevBodyYaw_ + by * partial;
    pp.headYaw = cam_.yaw;
    pp.pitch = cam_.pitch;
    pp.limbAmount = prevLimbAmount_ + (limbAmount_ - prevLimbAmount_) * partial;
    pp.limbSwing = limbSwing_ - limbAmount_ * (1.0f - partial);
    pp.attack = swing_ > 0 ? 1.0f - swing_ : 0.0f;
    pp.sneaking = player.sneaking;
    pp.hurt = player.hurtTime > 0;
    pp.skin = localSkinRef();
    pp.armor = player.inventory.armorIds();
    entityRenderer_->drawPlayer(pp, view, lightAt(pp.pos + glm::dvec3(0, 1, 0)), fog);
  }
  if (spawned_ && screen_ == Screen::None && session_->target() && !player.dead && !hideHud_ && !opt_.fixedCam) {
    const RayHit& hit = *session_->target();
    std::vector<AABB> boxes;
    selectionBoxes(world, hit.block.x, hit.block.y, hit.block.z, boxes);
    itemRenderer_->drawSelection(boxes, hit.block, view);
  }
  if (auto br = session_->breaking())
    itemRenderer_->drawBreaking(world.block(br->pos.x, br->pos.y, br->pos.z), br->pos, br->progress, view);

  perf_.lap(Phase::Entities);
  if (settings_.clouds) env_->drawClouds(view, worldTime_);
  glEnable(GL_CULL_FACE);
  terrain_->drawTranslucent(view, env_->lightmap(), fog);
  glDisable(GL_CULL_FACE);
  if (!opt_.fixedCam) particles_->draw(view, partial, lightAt, fog);
  perf_.lap(Phase::Translucent);

  // Mano (u objeto) en primera persona
  const bool hand = spawned_ && !player.dead && !thirdPerson && settings_.showHand && !hideHud_;
  const float bob = settings_.viewBobbing ? (prevWalked_ + (walked_ - prevWalked_) * partial) : 0.0f;
  if (hand && player.inventory.selected().empty())
    entityRenderer_->drawFirstPersonArm(view, swing_ > 0 ? 1.0f - swing_ : 0.0f, bob, lightAt(player.eyePos()), localSkinRef());
  if (hand) {
    const int bowTicks = session_->bowTicks();
    itemRenderer_->drawHeld(player.inventory.selected(), view, swing_ > 0 ? 1.0f - swing_ : 0.0f, bob, lightAt(player.eyePos()),
                            bowTicks > 0 ? static_cast<float>(bowTicks) + partial : 0.0f);
  }

  perf_.lap(Phase::Hand);
  if (scaled) {
    // El mundo, ampliado a la pantalla (sin suavizar: píxeles nítidos como en el juego)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
  }

  // --- Interfaz ---
  const int scale = guiScaleFor(w, h);
  ui_->begin(w, h, scale);
  touch_.setScreen(ui_->guiWidth(), ui_->guiHeight(), scale, SDL_GetWindowPixelDensity(window_));
  if (hurtFlash_ > 0) ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()),
                                (static_cast<u32>(hurtFlash_ * 90) << 24) | 0xB00000);
  if (player.headInWater && !thirdPerson)
    ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()), 0x50102060);

  if (!spawned_) {
    drawMenuBackground(*ui_);
    const TerrainStats st = terrain_->stats();
    ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() / 2.0f - 14, asciiText(save_ && save_->exists() ? "Cargando el mundo" : "Generando el mundo"),
                      0xFFFFFF);
    ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() / 2.0f + 2, std::format("Preparando el terreno: {} chunks", st.chunks), 0xA0A0A0);
  } else {
    // En táctil con "tocar para apuntar" se apunta con el dedo: sin punto de mira
    if (screen_ == Screen::None && (!touch_.active() || touch_.options().scheme == TouchScheme::Crosshair) && settings_.showCrosshair &&
        !hideHud_ && settings_.perspective != 2)
      ui_->crosshair();
    if (!hideHud_ && screen_ != Screen::Options) drawHud(*ui_, *itemRenderer_, player, std::min(1.0f, nameTimer_));
    if (screen_ == Screen::None) {
      touch_.draw(*ui_, player.inventory.selectedIndex());
      touch_.drawHold(*ui_);
    }
    if (opt_.showDebug && screen_ == Screen::None) {
      drawDebug(w, h);
    } else if (!hideHud_) {
      float y = std::max(2.0f, touch_.topInset());
      auto line = [&](const std::string& text, u32 color) {
        ui_->rect(1, y - 1, ui_->textWidth(text) + 1, 9, 0x90505050);
        ui_->text(2, y, text, color, false);
        y += 10;
      };
      if (settings_.showFps) line(std::format("{} fps", fps_), fps_ >= 55 ? 0x80FF80 : (fps_ >= 30 ? 0xFFFF60 : 0xFF6060));
      if (settings_.showCoords) {
        line(std::format("XYZ: {:.1f} / {:.1f} / {:.1f}", player.pos.x, player.pos.y, player.pos.z), 0xE0E0E0);
        line("Mirando al " + cam_.facing(), 0xE0E0E0);
      }
    }
    if (settings_.subtitles && !hideHud_) drawSubtitles();
    if (!hideHud_ || screen_ == Screen::Chat) drawChat(screen_ == Screen::Chat);
    if (!hideHud_) drawNameTags(partial);
    drawToasts();
    const glm::vec2 m = mouseGui();
    switch (screen_) {
      case Screen::Menu:
        if (session_->menu()) {
          // En el inventario, el jugador en su recuadro mirando hacia el ratón
          auto preview = [&](float left, float top) {
            const float s = static_cast<float>(ui_->scale());
            const Menu& menu = *session_->menu();
            if (menu.kind() == MenuKind::Inventory) {
              entityRenderer_->drawPlayerPreview((left + 51) * s, (top + 75) * s, 30 * s, m.x - (left + 51), m.y - (top + 25), w, h,
                                                 glm::vec3(1.0f), localSkinRef(), 0.0f, player.inventory.armorIds());
            } else if (menu.kind() == MenuKind::Creative && menu.creativeTab() == CreativeTab::Inventory) {
              // En su recuadro, entre las casillas de armadura
              entityRenderer_->drawPlayerPreview((left + 45) * s, (top + 47) * s, 20 * s, m.x - (left + 45), m.y - (top + 22), w, h,
                                                 glm::vec3(1.0f), localSkinRef(), 0.0f, player.inventory.armorIds());
            }
          };
          glm::vec2 mm = m;
          if (opt_.demo == "mesa") {  // ratón fijo sobre la tercera opción (capturas de prueba)
            const float left = std::floor((ui_->guiWidth() - session_->menu()->width()) / 2.0f), top = std::floor((ui_->guiHeight() - session_->menu()->height()) / 2.0f);
            mm = {left + 110, top + 14 + 19 * 2 + 9};
          }
          if (opt_.demo == "libros" || opt_.demo == "libros-armadura") {  // ratón fijo sobre una casilla (capturas de prueba)
            const float left = std::floor((ui_->guiWidth() - session_->menu()->width()) / 2.0f), top = std::floor((ui_->guiHeight() - session_->menu()->height()) / 2.0f);
            const int slot = opt_.demo == "libros" ? 0 : static_cast<int>(session_->menu()->slots().size()) - 9;
            mm = {left + session_->menu()->slots()[static_cast<std::size_t>(slot)].x + 8, top + session_->menu()->slots()[static_cast<std::size_t>(slot)].y + 8};
          }
          drawMenu(*ui_, *itemRenderer_, *session_->menu(), player, mm.x, mm.y, preview);
        }
        touch_.drawClose(*ui_, screenFinger_ && touch_.closeHit(m));
        break;
      case Screen::Pause: drawPauseMenu(*ui_, pauseButtons(*ui_, false, level_.allowCommands || !save_), m.x, m.y); break;
      case Screen::Chat: touch_.drawClose(*ui_, screenFinger_ && touch_.closeHit(m)); break;
      case Screen::Achievements: drawAchievementScreen(m); break;
      case Screen::Message:
        ui_->rect(0, 0, static_cast<float>(ui_->guiWidth()), static_cast<float>(ui_->guiHeight()), 0xC0101010);
        ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() / 3.0f, asciiText(message_), 0xFFFFFF);
        {
          float y = ui_->guiHeight() / 3.0f + 14;
          for (const std::string& line : wrapText(*ui_, asciiText(messageDetail_), ui_->guiWidth() - 40.0f)) {
            ui_->textCentered(ui_->guiWidth() / 2.0f, y, line, 0xA0A0A0);
            y += 10;
          }
        }
        drawButtons(*ui_, menuButtons(), m.x, m.y);
        break;
      case Screen::Options: {
        const std::vector<OptionItem> items = optionItems();
        const OptionListLayout l = layoutOptionList(*ui_, items, optScroll_);
        optScroll_ = l.scroll;
        drawOptionList(*ui_, optionTitle(), items, l, m.x, m.y, optPage_ == OptPage::Main ? "Listo" : "Volver");
        if (waitingKey_ >= 0)
          ui_->textCentered(ui_->guiWidth() / 2.0f, 22, "Pulsa una tecla (Esc: cancelar, Supr: ninguna)", 0xFFFF80);
        break;
      }
      case Screen::Death: drawDeathScreen(*ui_, m.x, m.y); break;
      default:
        if (!mouseLocked() && !touch_.active()) {
          const char* msg = "Haz clic para jugar  -  E: inventario  -  ESC: menu";
          ui_->textCentered(ui_->guiWidth() / 2.0f, ui_->guiHeight() - 60.0f, msg, 0xFFFFFF);
        }
        break;
    }
  }
  ui_->end();
  perf_.lap(Phase::Ui);
  gpuTimer_.end();
}

void Game::drawSubtitles() {
  if (subtitles_.empty()) return;
  // Abajo a la derecha, como en las versiones nuevas: "<" o ">" según de qué lado viene
  const float right = static_cast<float>(ui_->guiWidth()) - 2.0f;
  float y = static_cast<float>(ui_->guiHeight()) - 34.0f - 10.0f * static_cast<float>(subtitles_.size());
  int widest = 0;
  for (const Subtitle& st : subtitles_) widest = std::max(widest, ui_->textWidth(st.text));
  const float boxW = static_cast<float>(widest) + 24.0f;
  const glm::vec3 f = cam_.forward();
  const glm::dvec3 rightDir = glm::normalize(glm::dvec3(-f.z, 0.0, f.x));
  for (const Subtitle& st : subtitles_) {
    const float age = static_cast<float>(runTime_ - st.time);
    const u32 a = static_cast<u32>(std::clamp(1.0f - (age - 2.0f), 0.3f, 1.0f) * 255.0f);
    ui_->rect(right - boxW, y - 1, boxW, 10, 0xC0000000);
    const u32 c = (a << 16) | (a << 8) | a;
    ui_->textCentered(right - boxW / 2, y, st.text, c);
    if (st.positional) {
      const glm::dvec3 rel = st.pos - cam_.pos;
      const double side = glm::dot(rel, rightDir);
      if (glm::length(rel) > 1.5 && std::abs(side) > 0.5) {
        if (side < 0) ui_->text(right - boxW + 2, y, "<", c, false);
        else ui_->text(right - 7, y, ">", c, false);
      }
    }
    y += 10;
  }
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
      std::format("{} fps, CPU {:.1f} ms (peor {:.1f}), GPU {}", fps_, cpuAvg_, cpuMax_,
                  gpuTimer_.supported() ? std::format("{:.1f} ms", gpuTimer_.ms()) : std::string("n/d")),
      std::format("red {:.1f} carga {:.1f} tick {:.1f} prep {:.1f} cielo {:.1f}", perf_.avg(Phase::Net), perf_.avg(Phase::Load),
                  perf_.avg(Phase::Tick), perf_.avg(Phase::Prep), perf_.avg(Phase::Sky)),
      std::format("terreno {:.1f} entid {:.1f} transl {:.1f} mano {:.1f} ui {:.1f} swap {:.1f}", perf_.avg(Phase::Terrain),
                  perf_.avg(Phase::Entities), perf_.avg(Phase::Translucent), perf_.avg(Phase::Hand), perf_.avg(Phase::Ui),
                  perf_.avg(Phase::Present)),
      std::format("Llamadas: {}  quads: {}  secciones: {} / {}", st.drawCalls, st.drawnQuads, st.drawnSections, st.sections),
      std::format("Oclusion: {} recorridas, {:.2f} ms", st.visited, st.cullMs),
      std::format("Chunks: {}  generando: {}  mallando: {}", st.chunks, st.pendingGen, st.pendingMesh),
      effDist_ != settings_.renderDistance || effScale_ < settings_.renderScale - 0.001f
          ? std::format("Distancia: {} chunks (elegida {}), resolucion {:.0f} %", effDist_, settings_.renderDistance, effScale_ * 100.0f)
          : std::format("Distancia de render: {} chunks", settings_.renderDistance),
      "",
      std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.pos.x, p.pos.y, p.pos.z),
      std::format("Bloque: {} {} {}", bx, by, bz),
      std::format("Chunk: {} {} {} en {} {} {}", bx & 15, by & 15, bz & 15, bx >> 4, by >> 4, bz >> 4),
      std::format("Mirando: {} ({:.1f} / {:.1f})", cam_.facing(), glm::degrees(cam_.yaw), glm::degrees(cam_.pitch)),
      std::format("Bioma: {}", biomeInfo(world.biome(bx, bz)).displayName),
      std::format("Luz: {} cielo, {} bloque", world.skyLight(bx, by + 1, bz), world.blockLight(bx, by + 1, bz)),
      std::format("Dia {}, {:02}:{:02}", day, hh, mm),
      "",
      std::format("Modo: {}{}{}  suelo: {}", p.creative() ? "creativo" : "supervivencia", p.flying ? " (volando)" : "",
                  p.sprinting ? " (corriendo)" : "", p.onGround ? "si" : "no"),
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
      std::format("Criaturas: {}  flechas: {}", session_->mobs().size(), session_->arrows().size()),
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

bool Game::particleLayer(BlockState s, const glm::ivec3& pos, u16& layer, glm::vec3& tint) const {
  const int id = stateId(s);
  tint = glm::vec3(1.0f);
  if (isWater(id)) { layer = models_->waterStill; return true; }
  if (isLava(id)) { layer = models_->lavaStill; return true; }
  const VariantList* vl = models_->forState(s);
  if (!vl || vl->models.empty() || vl->models[0].quads.empty()) return false;
  // Una cara lateral sin teñir (en la hierba, el lateral con tierra); si no, la primera
  const BakedModel& m = vl->models[0];
  const BakedQuad* q = &m.quads[0];
  for (const BakedQuad& c : m.quads)
    if (c.face >= 2 && c.tintIndex < 0) { q = &c; break; }
  layer = q->layer;
  if (q->tintIndex >= 0) {
    const int biome = terrain_->world().biome(pos.x, pos.z);
    u32 c = 0xFFFFFF;
    switch (tintTypeOf(s)) {
      case TintType::Grass: c = colors_->grass(biome); break;
      case TintType::Foliage: c = colors_->foliage(biome); break;
      case TintType::Birch: c = 0x80A755; break;
      case TintType::Spruce: c = 0x619961; break;
      case TintType::Constant: c = blockInfo(id).tintColor; break;
      default: break;
    }
    tint = glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0f;
  }
  return true;
}

void Game::tickEffects(const std::vector<SessionEvent>& events) {
  World& world = terrain_->world();
  const Player& p = session_->player();
  effectTick_++;
  auto mobSay = [](MobType t) {
    switch (t) {
      case MobType::Pig: return Sfx::PigSay;
      case MobType::Cow: return Sfx::CowSay;
      case MobType::Sheep: return Sfx::SheepSay;
      case MobType::Chicken: return Sfx::ChickenSay;
      case MobType::Zombie: return Sfx::ZombieSay;
      case MobType::Skeleton: return Sfx::SkeletonSay;
      case MobType::Spider: return Sfx::SpiderSay;
      default: return Sfx::Count;
    }
  };
  auto mobHurt = [](MobType t) {
    switch (t) {
      case MobType::Pig: return Sfx::PigHurt;
      case MobType::Cow: return Sfx::CowHurt;
      case MobType::Sheep: return Sfx::SheepSay;
      case MobType::Chicken: return Sfx::ChickenHurt;
      case MobType::Zombie: return Sfx::ZombieHurt;
      case MobType::Skeleton: return Sfx::SkeletonHurt;
      case MobType::Spider: return Sfx::SpiderHurt;
      default: return Sfx::CreeperHurt;
    }
  };
  for (const SessionEvent& ev : events) {
    const glm::dvec3 center = glm::dvec3(ev.pos) + 0.5;
    switch (ev.type) {
      case SessionEvent::Type::BlockBroken: {
        u16 layer = 0;
        glm::vec3 tint;
        if (particleLayer(ev.state, ev.pos, layer, tint)) particles_->blockBreak(ev.pos, layer, tint);
        audio_->play(blockSound(stateId(ev.state)), center, 1.0f, 0.8f);
        break;
      }
      case SessionEvent::Type::BlockPlaced: audio_->play(blockSound(stateId(ev.state)), center, 1.0f, 0.8f); break;
      case SessionEvent::Type::ItemPickedUp: audio_->playFlat(Sfx::Pop, 0.4f, 1.0f + (effectTick_ % 7) * 0.1f); break;
      case SessionEvent::Type::PlayerHurt: audio_->playFlat(Sfx::Hurt, 0.9f); break;
      case SessionEvent::Type::PlayerDied: audio_->playFlat(Sfx::Hurt, 1.0f, 0.8f); break;
      case SessionEvent::Type::MobHurt:
        audio_->play(mobHurt(ev.mob), ev.where, 1.0f, 0.9f + (effectTick_ % 5) * 0.05f + (ev.value ? 0.5f : 0.0f));
        break;
      case SessionEvent::Type::LoveHearts: particles_->hearts(ev.where, ev.value); break;
      case SessionEvent::Type::XpPickup:  // un "plin" agudo, más grave cuanto más valía
        audio_->playFlat(Sfx::Orb, 0.2f, 0.8f + static_cast<float>(effectTick_ % 7) * 0.07f);
        break;
      case SessionEvent::Type::LevelUp: audio_->playFlat(Sfx::LevelUp, std::min(1.0f, static_cast<float>(ev.value) / 30.0f) * 0.75f); break;
      case SessionEvent::Type::MobDied:
        particles_->smoke(ev.where + glm::dvec3(0, 0.4, 0), 20, 0.8f, false);
        break;
      case SessionEvent::Type::MobCrit: particles_->crit(ev.where); break;
      case SessionEvent::Type::Explosion:
        particles_->explosion(ev.where);
        audio_->play(Sfx::Explosion, ev.where, 4.0f, 0.85f + (effectTick_ % 4) * 0.05f);
        break;
      case SessionEvent::Type::ArrowShot: audio_->play(Sfx::Bow, ev.where, 1.0f); break;
      case SessionEvent::Type::BowShot:  // más agudo cuanto más tensado
        audio_->playFlat(Sfx::Bow, 1.0f, 1.0f / ((effectTick_ % 10) * 0.04f + 1.2f) + static_cast<float>(ev.value) / 200.0f);
        break;
      case SessionEvent::Type::ArrowHit: audio_->play(Sfx::ArrowHit, ev.where, 0.8f); break;
      case SessionEvent::Type::CreeperFuse: audio_->play(Sfx::Fuse, ev.where, 1.2f); break;
      case SessionEvent::Type::SheepSheared: audio_->play(Sfx::DigCloth, ev.where, 1.0f, 1.3f); break;
      case SessionEvent::Type::DoorOpened: audio_->play(Sfx::DigWood, center, 1.0f, 1.2f); break;
      case SessionEvent::Type::DoorClosed: audio_->play(Sfx::DigWood, center, 1.0f, 0.9f); break;
      case SessionEvent::Type::Click: audio_->play(Sfx::Click, center, 0.6f, 0.6f); break;
      case SessionEvent::Type::Ate: audio_->playFlat(Sfx::Eat, 0.8f); break;
      case SessionEvent::Type::Achievement:
        audio_->playFlat(Sfx::Note, 0.7f, 1.0f);
        audio_->playFlat(Sfx::Note, 0.5f, 1.5f);
        break;
      default: break;
    }
  }

  // Picar: esquirlas y golpecitos cada 4 ticks
  if (auto br = session_->breaking(); br && effectTick_ % 4 == 0) {
    const BlockState s = world.block(br->pos.x, br->pos.y, br->pos.z);
    u16 layer = 0;
    glm::vec3 tint;
    if (session_->target() && particleLayer(s, br->pos, layer, tint))
      particles_->blockHit(session_->target()->point, session_->target()->face, layer, tint);
    audio_->play(blockSound(stateId(s)), glm::dvec3(br->pos) + 0.5, 0.25f, 0.5f);
  }

  // Pasos: uno cada 1,67 bloques andados (como en el juego), según el bloque que se pisa
  const glm::ivec3 below(static_cast<int>(std::floor(p.pos.x)), static_cast<int>(std::floor(p.pos.y - 0.2)), static_cast<int>(std::floor(p.pos.z)));
  const BlockState ground = world.block(below.x, below.y, below.z);
  if (p.onGround && !p.flying && !p.inWater && walked_ > nextStep_ && ground != 0) {
    nextStep_ = walked_ + 1.0f;
    audio_->play(blockSound(stateId(ground)), p.pos, 0.3f, 1.0f);
  }
  if (nextStep_ > walked_ + 1.0f) nextStep_ = walked_ + 1.0f;
  // Polvo al correr
  if (p.sprinting && p.onGround && ground != 0) {
    u16 layer = 0;
    glm::vec3 tint;
    if (particleLayer(ground, below, layer, tint)) particles_->sprintDust(p.pos, layer, tint);
  }
  // Chapuzón
  if (p.inWater && !wasInWater_ && p.motion.y < -0.2) audio_->playFlat(Sfx::Splash, 0.6f);
  wasInWater_ = p.inWater;
  // Comer
  if (session_->eatProgress() > 0 && effectTick_ % 4 == 0) audio_->playFlat(Sfx::Eat, 0.5f, 0.9f + (effectTick_ % 3) * 0.1f);
  if (p.food > lastFood_) audio_->playFlat(Sfx::Burp, 0.5f);
  lastFood_ = p.food;

  // Criaturas: sonidos de vez en cuando, llamas y humo si arden
  for (const Mob& m : session_->mobs()) {
    if (m.dying()) continue;
    const double d = glm::length(m.pos - p.pos);
    if (d > 20) continue;
    if ((effectTick_ + m.id * 37) % 160 == 0 && (m.id + effectTick_ / 160) % 2 == 0) {
      const Sfx say = mobSay(m.type);
      if (say != Sfx::Count) audio_->play(say, m.eyePos(), 1.0f, 0.9f + (m.id % 5) * 0.05f);
    }
    if (m.fireTicks > 0) {
      particles_->flame(m.pos + glm::dvec3(0, m.info().height * 0.5, 0));
      if (effectTick_ % 3 == 0) particles_->smoke(m.pos + glm::dvec3(0, m.info().height, 0), 1, 0.4f, false);
    }
  }
  particles_->tick(world);
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
  } else if (opt_.demo.rfind("creativo", 0) == 0) {
    // creativo[:pestaña[:desplazamiento[:texto de búsqueda]]]
    session_->setMode(GameMode::Creative);
    session_->openInventory();
    setScreen(Screen::Menu);
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
      const std::size_t colon = opt_.demo.find(':', from);
      parts.push_back(opt_.demo.substr(from, colon == std::string::npos ? std::string::npos : colon - from));
      if (colon == std::string::npos) break;
      from = colon + 1;
    }
    if (parts.size() > 1 && !parts[1].empty()) selectCreativeTab(static_cast<CreativeTab>(std::clamp(std::atoi(parts[1].c_str()), 0, kCreativeTabCount - 1)));
    if (parts.size() > 3) session_->menu()->setSearchText(parts[3]);
    if (parts.size() > 2 && !parts[2].empty()) session_->menu()->setScrollRow(std::atoi(parts[2].c_str()));
  } else if (opt_.demo == "pausa") {
    setScreen(Screen::Pause);
  } else if (opt_.demo == "lan") {
    // Abrir en LAN y mirar al sur, por donde aparece el invitado de la demo "unirse"
    openToLan();
    setScreen(Screen::None);
    p.yaw = cam_.yaw = glm::pi<float>();
    p.pitch = cam_.pitch = 0.1f;
  } else if (opt_.demo.rfind("unirse:", 0) == 0) {
    // Invitado: 4 bloques al sur del anfitrión y mirando hacia él (al norte)
    p.pos.z += 4.0;
    p.prevPos = p.pos;
    p.yaw = cam_.yaw = 0.0f;
    p.pitch = cam_.pitch = 0.1f;
  } else if (opt_.demo == "nuevo") {
    // Una columna de oro delante del jugador (para comprobar que se guarda)
    const glm::ivec3 at = glm::ivec3(glm::floor(p.pos + glm::dvec3(-std::sin(p.yaw) * 4.0, 0.0, -std::cos(p.yaw) * 4.0)));
    for (int y = 0; y < 5; y++) terrain_->setBlock(at.x, at.y + y, at.z, makeState(B::gold_block));
    p.inventory.slot(0) = ItemStack(ItemId::diamond_sword, 1);
    chatMessage("Columna de oro colocada en " + std::to_string(at.x) + " " + std::to_string(at.y) + " " + std::to_string(at.z));
  } else if (opt_.demo == "logros" || opt_.demo == "aviso") {
    for (Ach a : {Ach::OpenInventory, Ach::MineWood, Ach::BuildWorkBench, Ach::BuildPickaxe, Ach::BuildSword, Ach::KillEnemy,
                  Ach::BuildFurnace, Ach::AcquireIron})
      session_->award(a);
    session_->achievements().addStat("stat.walkOneCm", 123456);
    session_->achievements().addStat("stat.jump", 42);
    if (opt_.demo == "logros") {
      achShowStats_ = false;
      openScreen(Screen::Achievements);
    }
  } else if (opt_.demo.rfind("bloques", 0) == 0) {
    // Muestrario de bloques con forma (para revisar modelos y conexiones)
    session_->setMode(GameMode::Creative);
    const glm::ivec3 o = glm::ivec3(glm::floor(p.pos));
    auto put = [&](int dx, int dy, int dz, int id, int meta = 0) { terrain_->setBlock(o.x + dx, o.y + dy, o.z + dz, makeState(id, meta)); };
    for (int dz = -16; dz <= 1; dz++)
      for (int dx = -12; dx <= 12; dx++) {
        put(dx, -1, dz, B::stone);
        for (int dy = 0; dy < 8; dy++) put(dx, dy, dz, B::air);
      }
    // Fila 1: vallas con puerta, muro, paneles y barrotes, puertas
    for (int dx = -10; dx <= -6; dx++) put(dx, 0, -5, dx == -8 ? 107 : 85);
    put(-10, 0, -4, 85);
    for (int dx = -4; dx <= -2; dx++) put(dx, 0, -5, 139);
    put(-2, 1, -5, 139);
    put(-3, 0, -6, 139, 1);
    for (int dx = 0; dx <= 2; dx++) put(dx, 0, -5, 102);
    put(3, 0, -5, 160, 14);
    put(4, 0, -5, 101);
    put(4, 0, -6, 101);
    put(6, 0, -5, 64, 1); put(6, 1, -5, 64, 8);
    put(7, 0, -5, 71, 1 | 4); put(7, 1, -5, 71, 9);
    put(9, 0, -5, 195, 3); put(9, 1, -5, 195, 8);
    // Fila 2: escaleras (esquinas), losas, redstone, raíles
    put(-10, 0, -8, 53, 3); put(-9, 0, -8, 53, 3); put(-8, 0, -8, 53, 0); put(-8, 0, -9, 53, 0);
    put(-10, 0, -9, 67, 3 | 4); put(-6, 0, -8, 109, 1); put(-6, 0, -9, 164, 2);
    put(-4, 0, -8, 44, 0); put(-3, 0, -8, 44, 8 | 3); put(-2, 0, -8, 126, 2); put(-2, 0, -9, 43, 0); put(-4, 0, -9, 182, 0);
    for (int dx = 0; dx <= 3; dx++) put(dx, 0, -8, 55, 15 - dx * 4);
    put(0, 0, -9, 55, 10); put(4, 0, -8, 76, 5); put(1, 0, -7, 93, 2); put(3, 0, -9, 69, 5 | 8); put(2, 0, -9, 77, 4 | 0);
    put(6, 0, -8, 66, 0); put(6, 0, -9, 66, 6); put(7, 0, -9, 66, 1); put(8, 0, -9, 27, 1 | 8); put(9, 0, -9, 28, 1);
    put(8, 0, -8, 157, 4); put(10, 0, -9, 66, 3);
    // Fila 3: utilidades
    int x = -11;
    auto next = [&](int id, int meta = 0) { put(x, 0, -12, id, meta); x += 2; };
    next(54); next(26, 0); put(x - 2, 0, -11, 26, 8);
    next(92, 2); next(145, 1); next(117, 3); next(118, 3); next(154, 0); next(116); next(120, 4 | 2); next(52); next(138);
    put(-11, 0, -14, 60, 7); put(-11, 1, -14, 59, 7); put(-10, 0, -14, 60, 7); put(-10, 1, -14, 141, 7);
    put(-9, 0, -14, 60, 0); put(-9, 1, -14, 142, 3); put(-8, 0, -14, B::soul_sand); put(-8, 1, -14, 115, 3);
    put(-7, 0, -14, 86, 0); put(-6, 0, -14, 104, 7); put(-5, 0, -14, 105, 3);
    put(-3, 0, -14, 33, 1 | 8); put(-3, 1, -14, 34, 1); put(-2, 0, -14, 29, 3); put(-1, 0, -14, 23, 3); put(0, 0, -14, 158, 1);
    put(2, 0, -14, B::planks); put(2, 1, -14, B::planks); put(2, 0, -13, 65, 3); put(2, 1, -13, 50, 3);
    put(3, 0, -14, 140); put(4, 0, -14, 96, 1); put(5, 0, -14, 96, 4 | 1); put(6, 0, -14, 72); put(7, 0, -14, 147, 5);
    put(8, 0, -14, 91, 2); put(9, 0, -14, 170, 0); put(10, 0, -14, 171, 11); put(11, 0, -14, 168, 1);
    put(4, 0, -12, 25); put(5, 0, -12, 84); put(6, 0, -12, 124); put(7, 0, -12, 123); put(8, 0, -12, 179, 1); put(9, 0, -12, 155, 2);
    put(10, 0, -12, 99, 14); put(11, 0, -12, 100, 5); put(12, 0, -12, 17, 3); put(12, 1, -12, 127, 8 | 3); put(12, 0, -13, 106, 1);
    put(-12, 0, -8, 51); put(-12, 0, -10, 90, 1); put(-12, 1, -10, 90, 1);
    // Vista desde arriba y detrás del muestrario (se puede cambiar el lado con --yaw)
    p.flying = true;
    // bloques:X:Y:Z coloca la cámara en ese desplazamiento
    double cx = 0.5, cy = 9.0, cz = 2.0;
    if (opt_.demo.size() > 8) std::sscanf(opt_.demo.c_str() + 8, "%lf:%lf:%lf", &cx, &cy, &cz);
    p.pos = glm::dvec3(o) + glm::dvec3(cx, cy, cz);
    p.pitch = -0.6f;
  } else if (opt_.demo.rfind("opciones", 0) == 0) {
    // opciones, opciones:graficos, :sonido, :controles, :teclas, :partida, :interfaz
    static const std::pair<const char*, OptPage> pages[] = {{"graficos", OptPage::Graphics}, {"sonido", OptPage::Sound},
                                                            {"controles", OptPage::Controls}, {"teclas", OptPage::Keys},
                                                            {"partida", OptPage::Game}, {"interfaz", OptPage::Interface}};
    openOptionPage(OptPage::Main);
    for (const auto& [name, page] : pages)
      if (opt_.demo.find(name) != std::string::npos) openOptionPage(page);
    setScreen(Screen::Options);
  } else if (opt_.demo == "detras" || opt_.demo == "delante") {
    settings_.perspective = opt_.demo == "detras" ? 1 : 2;
  } else if (opt_.demo.rfind("mob:", 0) == 0) {
    // Una sola criatura delante, de cerca (para revisar modelos): mob:0 .. mob:7
    session_->setMode(GameMode::Creative);
    const int type = std::clamp(std::atoi(opt_.demo.c_str() + 4), 0, static_cast<int>(MobType::Count) - 1);
    const glm::dvec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw));
    glm::dvec3 at = p.pos + f * 3.0;
    World& w = terrain_->world();
    const int x = static_cast<int>(std::floor(at.x)), z = static_cast<int>(std::floor(at.z));
    int y = static_cast<int>(p.pos.y) + 3;
    while (y > 1 && collisionBoxes(stateId(w.block(x, y - 1, z)), stateMeta(w.block(x, y - 1, z))).empty()) y--;
    at.y = y;
    if (Mob* m = session_->spawnMob(static_cast<MobType>(type), at)) {
      m->yaw = m->prevYaw = m->headYaw = m->prevHeadYaw = p.yaw + 3.14159f + (opt_.demo.size() >= 6 ? 1.5708f : 0.6f);  // de tres cuartos (o de lado: mob:NL)
      m->woolColor = 0;
    }
  } else if (opt_.demo == "boom") {
    session_->setMode(GameMode::Creative);
  } else if (opt_.demo == "arco" || opt_.demo == "arco2") {
    give(ItemId::bow, 1);
    give(ItemId::arrow, 16);
    inv.select(0);
    if (opt_.demo == "arco2") p.pitch = -0.12f;
  } else if (opt_.demo == "armadura" || opt_.demo == "armadura-inv" || opt_.demo.rfind("armadura:", 0) == 0) {
    // Armadura puesta: de cuero, malla, hierro, oro o diamante (armadura:N, 0..4); se ve desde delante o en el inventario
    const int mat = opt_.demo.rfind("armadura:", 0) == 0 ? std::clamp(std::atoi(opt_.demo.c_str() + 9), 0, 4) : 4;
    static const int firsts[5] = {ItemId::leather_helmet, ItemId::chainmail_helmet, ItemId::iron_helmet, ItemId::golden_helmet,
                                  ItemId::diamond_helmet};
    const int helmet = firsts[mat] == ItemId::golden_helmet ? ItemId::golden_helmet : firsts[mat];
    for (int i = 0; i < 4; i++) inv.armor(3 - i) = ItemStack(helmet + i);
    give(ItemId::iron_chestplate, 1);
    give(ItemId::diamond_boots, 1);
    inv.select(1);
    p.health = 16;
    if (opt_.demo == "armadura-inv") {
      session_->openInventory();
      setScreen(Screen::Menu);
    } else {
      settings_.perspective = 2;
    }
  } else if (opt_.demo == "libros" || opt_.demo == "libros-armadura") {
    // El inventario creativo al final (los libros encantados) con el ratón sobre uno, para ver el recuadro del nombre
    session_->setMode(GameMode::Creative);
    session_->openInventory();
    setScreen(Screen::Menu);
    session_->menu()->scroll(opt_.demo == "libros" ? 9999 : 0);
    if (opt_.demo == "libros-armadura") {
      ItemStack helmet(ItemId::diamond_helmet);
      ItemExtra extra;
      extra.ench = {{Ench::Protection, 4}, {Ench::Respiration, 3}, {Ench::Unbreaking, 3}};
      extra.name = "Casco del buzo";
      extra.lore = {"Forjado en el fondo", "del mar"};
      helmet.setExtra(std::move(extra));
      inv.add(helmet);
    }
  } else if (opt_.demo == "mesa") {
    // La mesa de encantamientos con 15 estanterías, una espada de hierro, 6 de lapislázuli y el nivel 30
    p.xpLevel = 30;
    p.xpTotal = 1395;
    p.xpProgress = 0;
    auto table = std::make_unique<Menu>(MenuKind::Enchant, p, nullptr, nullptr, 15);
    table->enchantSlot(0) = ItemStack(ItemId::iron_sword);
    table->enchantSlot(1) = ItemStack(ItemId::dye, 6, 4);
    table->refreshOffers();
    session_->openMenu(std::move(table));
    setScreen(Screen::Menu);
  } else if (opt_.demo == "xp") {
    // Un puñado de orbes de varios valores delante, y el nivel 17 con la barra a medias
    p.xpLevel = 17;
    p.xpProgress = 0.45f;
    p.xpTotal = 400;
    p.health = 18;
    const glm::dvec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw)), r(std::cos(p.yaw), 0, -std::sin(p.yaw));
    World& w = terrain_->world();
    static const int values[] = {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477};
    for (int i = 0; i < 11; i++) {
      glm::dvec3 at = p.pos + f * 4.5 + r * ((i - 5) * 0.55);
      const int x = static_cast<int>(std::floor(at.x)), z = static_cast<int>(std::floor(at.z));
      int y = static_cast<int>(p.pos.y) + 4;
      while (y > 1 && collisionBoxes(stateId(w.block(x, y - 1, z)), stateMeta(w.block(x, y - 1, z))).empty()) y--;
      XpOrb o;
      o.pos = o.prevPos = {at.x, static_cast<double>(y) + 0.1, at.z};
      o.value = values[i];
      o.pickupDelay = 1 << 30;
      session_->addOrb(o);
    }
  } else if (opt_.demo == "crias") {
    // Un adulto (en modo amor) y su cría de cada animal, en fila y mirando al jugador
    session_->setMode(GameMode::Creative);
    World& w = terrain_->world();
    const glm::dvec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw)), r(std::cos(p.yaw), 0, -std::sin(p.yaw));
    for (int i = 0; i < 8; i++) {
      glm::dvec3 at = p.pos + f * 5.0 + r * ((i - 3.5) * 1.3);
      const int x = static_cast<int>(std::floor(at.x)), z = static_cast<int>(std::floor(at.z));
      int y = static_cast<int>(p.pos.y) + 6;
      while (y > 1 && collisionBoxes(stateId(w.block(x, y - 1, z)), stateMeta(w.block(x, y - 1, z))).empty()) y--;
      at.y = y;
      if (Mob* m = session_->spawnMob(static_cast<MobType>(i / 2), at)) {
        m->yaw = m->prevYaw = m->headYaw = m->prevHeadYaw = p.yaw + 3.14159f;
        m->noAI = true;
        if (i % 2) m->growth = -kBabyTicks;
        else m->inLove = kLoveTicks;
        if (m->type == MobType::Sheep) m->woolColor = 0;
      }
    }
  } else if (opt_.demo == "mobs") {
    // Una fila con cada criatura delante del jugador, mirándole (en creativo: no atacan)
    session_->setMode(GameMode::Creative);
    World& w = terrain_->world();
    const glm::dvec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw)), r(std::cos(p.yaw), 0, -std::sin(p.yaw));
    for (int i = 0; i < static_cast<int>(MobType::Count); i++) {
      glm::dvec3 at = p.pos + f * 6.0 + r * ((i - 3.5) * 1.8);
      const int x = static_cast<int>(std::floor(at.x)), z = static_cast<int>(std::floor(at.z));
      int y = static_cast<int>(p.pos.y) + 6;
      while (y > 1 && collisionBoxes(stateId(w.block(x, y - 1, z)), stateMeta(w.block(x, y - 1, z))).empty()) y--;
      at.y = y;
      if (Mob* m = session_->spawnMob(static_cast<MobType>(i), at)) {
        m->yaw = m->prevYaw = m->headYaw = m->prevHeadYaw = p.yaw + 3.14159f;
        if (m->type == MobType::Sheep) m->woolColor = 0;
      }
    }
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
