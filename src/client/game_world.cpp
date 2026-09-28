// Abrir, guardar y cerrar mundos: une el guardado (src/save) con el terreno y la partida.
#include <chrono>
#include <cmath>

#include "client/audio.h"
#include "client/game.h"
#include "client/particles.h"
#include "client/terrain.h"
#include "core/fs.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/random.h"
#include "save/anvil.h"
#include "save/chunk_io.h"

namespace mcw {

void Game::enterWorld(const std::string& folder, LevelInfo level) {
  if (inWorld_) leaveWorld();
  level_ = std::move(level);
  save_ = folder.empty() ? nullptr : std::make_unique<WorldSave>(WorldSave::savesDir() / folder);
  opt_.seed = level_.seed;
  log::info("mundo \"{}\" (semilla {}, {})", level_.name, static_cast<i64>(level_.seed), folder.empty() ? "sin guardar" : folder);

  terrain_->reset(level_.seed, GeneratorSettings::fromLevel(level_.generator, level_.generatorOptions, level_.mapFeatures));
  session_ = std::make_unique<GameSession>(*terrain_, level_.seed);
  particles_ = std::make_unique<ParticleSystem>();
  particles_->initGL(terrain_->textureArray());
  subtitles_.clear();
  chat_.clear();

  // Chunks: leerlos del disco si están guardados y guardarlos (con sus criaturas) al salir de memoria
  if (save_) {
    Terrain::Storage st;
    st.load = [this](ChunkPos p) { return save::loadChunk(save_->regions(), p, *session_); };
    st.save = [this](const Chunk& c, bool unloading) {
      save::storeChunk(save_->regions(), c, *session_, unloading, static_cast<i64>(worldTime_));
    };
    terrain_->setStorage(std::move(st));
  } else {
    terrain_->setStorage({});
  }

  // Modo, reglas, hora y jugador
  const GameMode mode = level_.gameType == 1 ? GameMode::Creative : GameMode::Survival;
  session_->setMode(mode);
  worldTime_ = static_cast<double>(level_.dayTime);
  keepPlayerPos_ = false;
  Player& p = session_->player();
  if (level_.spawnSet) spawn_ = glm::dvec3(level_.spawn) + glm::dvec3(0.5, 0.0, 0.5);
  else spawn_ = opt_.startPos && folder.empty() ? *opt_.startPos : findSpawn();
  session_->setSpawn(spawn_);
  p.pos = p.prevPos = spawn_;
  if (level_.player) {
    save::playerFromNbt(*level_.player, p);
    keepPlayerPos_ = !p.dead;
    if (p.dead) p.respawn(spawn_);
    session_->setMode(p.mode);
  } else {
    if (opt_.yawDeg && folder.empty()) p.yaw = glm::radians(*opt_.yawDeg);
    if (opt_.pitchDeg && folder.empty()) p.pitch = glm::radians(*opt_.pitchDeg);
  }
  // Logros y estadísticas del jugador en este mundo (stats/<uuid>.json, como en 1.8)
  session_->achievements().clear();
  if (save_)
    if (auto text = fs::readText(save_->dir() / "stats" / (offlineUuid(settings_.playerName) + ".json")))
      session_->achievements().fromJson(*text);
  applyLevelRules();
  cam_.yaw = p.yaw;
  cam_.pitch = p.pitch;
  cam_.pos = p.eyePos();
  bodyYaw_ = prevBodyYaw_ = p.yaw;
  spawned_ = false;
  settledAt_ = -1;
  loggedLoaded_ = false;
  autosaveTimer_ = 0;
  worldStartTicks_ = SDL_GetTicksNS();
  inWorld_ = true;
  // Mundo nuevo: se crea en disco al momento (level.dat) para que aparezca en la lista
  if (save_ && !save_->exists()) saveWorld();
  setScreen(Screen::None);
}

void Game::applyLevelRules() {
  if (!session_) return;
  session_->setRules({level_.difficulty, level_.ruleBool("keepInventory", false), level_.ruleBool("doMobSpawning", true)});
}

void Game::saveWorld() {
  if (!save_ || !inWorld_) return;
  const u64 t0 = SDL_GetTicksNS();
  // Criaturas y objetos se mueven: sus chunks se vuelven a guardar
  for (const Mob& m : session_->mobs()) terrain_->markUnsaved({static_cast<int>(std::floor(m.pos.x)) >> 4, static_cast<int>(std::floor(m.pos.z)) >> 4});
  for (const ItemEntity& e : session_->items())
    terrain_->markUnsaved({static_cast<int>(std::floor(e.pos.x)) >> 4, static_cast<int>(std::floor(e.pos.z)) >> 4});
  terrain_->saveAll();
  const Player& p = session_->player();
  level_.player = save::playerToNbt(p, spawn_, false);
  level_.dayTime = static_cast<i64>(worldTime_);
  level_.time = static_cast<i64>(worldTime_);
  level_.gameType = p.creative() ? 1 : 0;
  level_.spawn = glm::ivec3(glm::floor(spawn_));
  level_.spawnSet = true;
  level_.lastPlayed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  save_->saveLevel(level_);
  if (server_) server_->saveAll();
  {
    std::error_code ec;
    std::filesystem::create_directories(save_->dir() / "stats", ec);
    const std::string json = session_->achievements().toJson();
    fs::writeFile(save_->dir() / "stats" / (offlineUuid(settings_.playerName) + ".json"), json.data(), json.size());
  }
  WorldSave::flush();
  log::info("mundo guardado en {:.0f} ms", (SDL_GetTicksNS() - t0) / 1e6);
}

void Game::leaveWorld() {
  if (!inWorld_) return;
  if (net_) {
    leaveRemote("");
    openScreen(Screen::Title);
    return;
  }
  stopNet();
  saveWorld();
  terrain_->clear();
  terrain_->setStorage({});
  session_ = std::make_unique<GameSession>(*terrain_, 0);
  save_.reset();
  inWorld_ = false;
  spawned_ = false;
}

void Game::createWorldFromForm() {
  LevelInfo info;
  info.name = nameField_.text.empty() ? "Mundo nuevo" : nameField_.text;
  // Semilla: número tal cual; texto, con el mismo método que el juego (hash de la cadena); vacía, al azar
  const std::string& s = seedField_.text;
  if (s.empty()) {
    info.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count()) * 0x9E3779B97F4A7C15ull;
  } else {
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    info.seed = (end && *end == 0 && v != 0) ? static_cast<u64>(v) : seedFromString(s);
  }
  info.gameType = newMode_ == 2 ? 1 : 0;
  info.hardcore = newMode_ == 1;
  info.difficulty = info.hardcore ? 3 : newDifficulty_;
  info.difficultyLocked = info.hardcore;
  info.allowCommands = newCheats_ && !info.hardcore;
  info.mapFeatures = newStructures_;
  info.bonusChest = newBonusChest_;
  static const char* types[] = {"default", "flat", "largeBiomes", "amplified"};
  info.generator = types[std::clamp(newWorldType_, 0, 3)];
  if (newWorldType_ == 1) {
    static const char* presets[] = {GeneratorSettings::kDefaultFlat,
                                    "3;minecraft:bedrock,3*minecraft:stone,52*minecraft:sandstone,8*minecraft:sand;2;village",
                                    "3;minecraft:bedrock,230*minecraft:stone,5*minecraft:dirt,minecraft:grass;3;",
                                    "3;minecraft:bedrock,5*minecraft:stone,5*minecraft:dirt,5*minecraft:sand,90*minecraft:water;0;",
                                    "3;minecraft:bedrock,59*minecraft:stone,3*minecraft:dirt,minecraft:snow;12;village"};
    info.generatorOptions = presets[std::clamp(newFlatPreset_, 0, 4)];
  }
  info.gameRules = {{"doDaylightCycle", "true"}, {"doMobSpawning", "true"}, {"keepInventory", "false"},
                    {"doFireTick", "true"},      {"mobGriefing", "true"},   {"naturalRegeneration", "true"},
                    {"doTileDrops", "true"},     {"doMobLoot", "true"},     {"commandBlockOutput", "true"},
                    {"showDeathMessages", "true"}, {"sendCommandFeedback", "true"}, {"randomTickSpeed", "3"},
                    {"reducedDebugInfo", "false"}, {"logAdminCommands", "true"}};
  const std::string folder = WorldSave::freeFolderName(info.name);
  enterWorld(folder, std::move(info));
}

}  // namespace mcw
