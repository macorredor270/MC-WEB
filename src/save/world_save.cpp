#include "save/world_save.h"

#include <algorithm>
#include <chrono>

#include "core/fs.h"
#include "core/log.h"
#include "core/zip.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {
namespace {

namespace stdfs = std::filesystem;
using nbt::Tag;
using nbt::Value;

i64 nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uintmax_t dirSize(const stdfs::path& p) {
  std::error_code ec;
  std::uintmax_t total = 0;
  for (auto it = stdfs::recursive_directory_iterator(p, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec))
    if (it->is_regular_file(ec)) total += it->file_size(ec);
  return total;
}

}  // namespace

nbt::Value levelToNbt(const LevelInfo& info) {
  Value d = Value::compound();
  d.set("LevelName", Value::string(info.name));
  d.set("RandomSeed", Value::longV(static_cast<i64>(info.seed)));
  d.set("GameType", Value::intV(info.gameType));
  d.set("hardcore", Value::boolean(info.hardcore));
  d.set("Difficulty", Value::byte(static_cast<i8>(info.difficulty)));
  d.set("DifficultyLocked", Value::boolean(info.difficultyLocked));
  d.set("allowCommands", Value::boolean(info.allowCommands));
  d.set("MapFeatures", Value::boolean(info.mapFeatures));
  d.set("generatorName", Value::string(info.generator));
  d.set("generatorVersion", Value::intV(info.generator == "default" ? 1 : 0));
  d.set("generatorOptions", Value::string(info.generatorOptions));
  d.set("Time", Value::longV(info.time));
  d.set("DayTime", Value::longV(info.dayTime));
  d.set("SpawnX", Value::intV(info.spawn.x));
  d.set("SpawnY", Value::intV(info.spawn.y));
  d.set("SpawnZ", Value::intV(info.spawn.z));
  d.set("LastPlayed", Value::longV(info.lastPlayed ? info.lastPlayed : nowMs()));
  d.set("version", Value::intV(19133));  // Anvil
  d.set("initialized", Value::byte(1));
  d.set("raining", Value::boolean(info.raining));
  d.set("rainTime", Value::intV(12000));
  d.set("thundering", Value::byte(0));
  d.set("thunderTime", Value::intV(12000));
  d.set("clearWeatherTime", Value::intV(0));
  d.set("BorderCenterX", Value::doubleV(0));
  d.set("BorderCenterZ", Value::doubleV(0));
  d.set("BorderSize", Value::doubleV(60000000));
  d.set("BorderSafeZone", Value::doubleV(5));
  d.set("BorderDamagePerBlock", Value::doubleV(0.2));
  d.set("BorderWarningBlocks", Value::doubleV(5));
  d.set("BorderWarningTime", Value::doubleV(15));
  Value rules = Value::compound();
  for (const auto& [k, v] : info.gameRules) rules.set(k, Value::string(v));
  d.set("GameRules", std::move(rules));
  if (info.player) d.set("Player", *info.player);
  // Datos propios de MC-WEB (el juego original los ignora)
  d.set("mcwebBonusChest", Value::boolean(info.bonusChest));
  d.set("mcwebSpawnSet", Value::boolean(info.spawnSet));
  d.set("mcwebDragonKilled", Value::boolean(info.dragonKilled));
  d.set("mcwebDragonHealth", Value::floatV(info.dragonHealth));
  Value root = Value::compound();
  root.set("Data", std::move(d));
  return root;
}

LevelInfo levelFromNbt(const nbt::Value& root) {
  LevelInfo info;
  const Value* d = root.getCompound("Data");
  if (!d) return info;
  info.name = d->getString("LevelName", "Mundo");
  info.seed = static_cast<u64>(d->getLong("RandomSeed"));
  info.gameType = d->getInt("GameType");
  info.hardcore = d->getBool("hardcore");
  info.difficulty = d->getInt("Difficulty", 2);
  info.difficultyLocked = d->getBool("DifficultyLocked");
  info.allowCommands = d->getBool("allowCommands");
  info.mapFeatures = d->getBool("MapFeatures", true);
  info.generator = d->getString("generatorName", "default");
  info.generatorOptions = d->getString("generatorOptions");
  info.time = d->getLong("Time");
  info.dayTime = d->getLong("DayTime", info.time);
  info.spawn = {d->getInt("SpawnX"), d->getInt("SpawnY", 64), d->getInt("SpawnZ")};
  info.spawnSet = d->getBool("mcwebSpawnSet", d->has("SpawnX"));
  info.dragonKilled = d->getBool("mcwebDragonKilled");
  info.dragonHealth = d->has("mcwebDragonHealth") ? static_cast<float>(d->getDouble("mcwebDragonHealth")) : 200.0f;
  info.lastPlayed = d->getLong("LastPlayed");
  info.raining = d->getBool("raining");
  info.bonusChest = d->getBool("mcwebBonusChest");
  if (const Value* rules = d->getCompound("GameRules"))
    for (std::size_t i = 0; i < rules->keys().size(); i++)
      if (rules->values()[i].type() == Tag::String) info.gameRules[rules->keys()[i]] = rules->values()[i].asString();
  if (const Value* p = d->getCompound("Player")) info.player = *p;
  return info;
}

WorldSave::WorldSave(stdfs::path dir) : dir_(std::move(dir)), regions_(dir_ / "region") {}

RegionStore& WorldSave::regions(int dimension) {
  if (dimension == 0) return regions_;
  auto& slot = dimension < 0 ? nether_ : end_;
  if (!slot) slot = std::make_unique<RegionStore>(dir_ / (dimension < 0 ? "DIM-1" : "DIM1") / "region");
  return *slot;
}

stdfs::path WorldSave::savesDir() {
  const stdfs::path p = fs::userDataDir() / "saves";
  std::error_code ec;
  stdfs::create_directories(p, ec);
  return p;
}

std::vector<WorldSummary> WorldSave::list() {
  std::vector<WorldSummary> out;
  std::error_code ec;
  for (const auto& e : stdfs::directory_iterator(savesDir(), ec)) {
    if (!e.is_directory(ec)) continue;
    WorldSave w(e.path());
    LevelInfo info;
    if (!w.loadLevel(info)) continue;
    WorldSummary s;
    s.folder = e.path().filename().string();
    s.name = info.name;
    s.lastPlayed = info.lastPlayed;
    s.gameType = info.gameType;
    s.hardcore = info.hardcore;
    s.allowCommands = info.allowCommands;
    s.sizeBytes = dirSize(e.path());
    out.push_back(std::move(s));
  }
  std::sort(out.begin(), out.end(), [](const WorldSummary& a, const WorldSummary& b) { return a.lastPlayed > b.lastPlayed; });
  return out;
}

std::string WorldSave::freeFolderName(const std::string& name) {
  // Solo caracteres seguros en cualquier sistema de archivos
  std::string base;
  for (char c : name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
  while (!base.empty() && (base.back() == ' ' || base.back() == '.')) base.pop_back();
  if (base.empty()) base = "Mundo";
  std::string folder = base;
  std::error_code ec;
  for (int i = 1; stdfs::exists(savesDir() / folder, ec); i++) folder = base + "-" + std::to_string(i);
  return folder;
}

bool WorldSave::remove(const std::string& folder) {
  std::error_code ec;
  stdfs::remove_all(savesDir() / folder, ec);
  flush();
  return !ec;
}

bool WorldSave::rename(const std::string& folder, const std::string& newName) {
  WorldSave w(savesDir() / folder);
  LevelInfo info;
  if (!w.loadLevel(info)) return false;
  info.name = newName;
  const bool ok = w.saveLevel(info);
  flush();
  return ok;
}

std::vector<u8> WorldSave::exportZip(const std::string& folder) {
  const stdfs::path root = savesDir() / folder;
  std::vector<std::pair<std::string, std::vector<u8>>> files;
  std::error_code ec;
  for (auto it = stdfs::recursive_directory_iterator(root, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    auto data = fs::readFile(it->path());
    if (!data) continue;
    std::string rel = stdfs::relative(it->path(), root, ec).generic_string();
    files.emplace_back(folder + "/" + rel, std::move(*data));
  }
  if (files.empty()) return {};
  return zipFiles(files);
}

std::string WorldSave::importZip(const std::vector<u8>& zipData, const std::string& fallbackName) {
  auto zip = ZipArchive::openMemory(zipData);
  if (!zip) return {};
  // Buscar level.dat (el de menos profundidad): lo que hay en su carpeta es el mundo
  std::string prefix;
  bool found = false;
  for (const std::string& name : zip->list()) {
    const auto slash = name.rfind('/');
    const std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
    if (file != "level.dat") continue;
    const std::string dir = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
    if (!found || dir.size() < prefix.size()) prefix = dir;
    found = true;
  }
  if (!found) return {};
  auto level = zip->read(prefix + "level.dat");
  if (!level) return {};
  auto root = nbt::read(*level);
  if (!root) return {};
  std::string name = levelFromNbt(*root).name;
  if (name.empty()) name = fallbackName;
  const std::string folder = freeFolderName(name);
  const stdfs::path dest = savesDir() / folder;
  for (const std::string& entry : zip->list(prefix)) {
    const std::string rel = entry.substr(prefix.size());
    if (rel.empty() || rel.find("..") != std::string::npos) continue;  // nada fuera de la carpeta
    auto data = zip->read(entry);
    if (!data) continue;
    const stdfs::path out = dest / stdfs::path(rel);
    std::error_code ec;
    stdfs::create_directories(out.parent_path(), ec);
    fs::writeFile(out, data->data(), data->size());
  }
  flush();
  return folder;
}

void WorldSave::flush() {
#ifdef __EMSCRIPTEN__
  EM_ASM({
    if (typeof FS !== 'undefined' && FS.syncfs && !Module.mcwSyncing) {
      Module.mcwSyncing = true;
      FS.syncfs(false, function(err) {
        Module.mcwSyncing = false;
        if (err) console.warn('no se pudo guardar en IndexedDB', err);
      });
    }
  });
#endif
}

bool WorldSave::exists() const {
  std::error_code ec;
  return stdfs::exists(dir_ / "level.dat", ec);
}

bool WorldSave::loadLevel(LevelInfo& out) const {
  auto data = fs::readFile(dir_ / "level.dat");
  if (!data) data = fs::readFile(dir_ / "level.dat_old");
  if (!data) return false;
  auto root = nbt::read(*data);
  if (!root) return false;
  out = levelFromNbt(*root);
  return true;
}

bool WorldSave::saveLevel(const LevelInfo& info) {
  std::error_code ec;
  stdfs::create_directories(dir_, ec);
  const std::vector<u8> raw = nbt::write(levelToNbt(info));
  const std::vector<u8> gz = gzipCompress(raw.data(), raw.size());
  // Como el juego: primero a un temporal, y la copia anterior queda en level.dat_old
  const stdfs::path tmp = dir_ / "level.dat_new", cur = dir_ / "level.dat", old = dir_ / "level.dat_old";
  if (!fs::writeFile(tmp, gz.data(), gz.size())) {
    log::warn("no se pudo guardar {}", cur.string());
    return false;
  }
  if (stdfs::exists(cur, ec)) {
    stdfs::remove(old, ec);
    stdfs::rename(cur, old, ec);
  }
  stdfs::rename(tmp, cur, ec);
  return !ec;
}

}  // namespace mcw
