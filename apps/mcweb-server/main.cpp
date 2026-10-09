// Servidor dedicado de MC-WEB: una partida de Minecraft 1.8 (protocolo 47, modo offline) sin
// ventana ni jugador local. Entran MC-WEB (escritorio, o web a través de mcweb-wsproxy) y el
// Minecraft 1.8 oficial. Se configura con server.properties, como el servidor de siempre.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "core/random.h"
#include "data/blocks.h"
#include "game/session.h"
#include "net/server.h"
#include "net/server_world.h"
#include "save/world_save.h"

using namespace mcw;
namespace stdfs = std::filesystem;

namespace {

std::atomic<bool> gStop{false};
void onSignal(int) { gStop = true; }

/// server.properties: "clave=valor" por línea; '#' comenta.
using Properties = std::map<std::string, std::string>;

const char* kDefaultProperties =
    "# Configuración del servidor de MC-WEB (compatible con la de Minecraft 1.8)\n"
    "server-port=25565\n"
    "motd=Un servidor de MC-WEB\n"
    "max-players=20\n"
    "# 0 supervivencia, 1 creativo\n"
    "gamemode=0\n"
    "# 0 pacífico, 1 fácil, 2 normal, 3 difícil\n"
    "difficulty=1\n"
    "level-name=world\n"
    "# Vacía = al azar; un número o cualquier texto\n"
    "level-seed=\n"
    "# DEFAULT, FLAT, LARGEBIOMES o AMPLIFIED\n"
    "level-type=DEFAULT\n"
    "generate-structures=true\n"
    "view-distance=8\n"
    "# Con white-list=true solo entran los nombres de whitelist.txt (uno por línea)\n"
    "white-list=false\n"
    "# Las cuentas premium (online-mode=true) aún no están soportadas: se juega en modo offline\n"
    "online-mode=false\n";

Properties readProperties(const stdfs::path& file) {
  Properties p;
  std::istringstream defaults(kDefaultProperties);
  std::ifstream in(file);
  if (!in) {
    std::ofstream(file) << kDefaultProperties;
    log::info("creado {} con la configuración por defecto", file.string());
  }
  std::istream& src = in ? static_cast<std::istream&>(in) : defaults;
  std::string line;
  while (std::getline(src, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq != std::string::npos) p[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return p;
}

int intProp(const Properties& p, const std::string& key, int def) {
  auto it = p.find(key);
  if (it == p.end() || it->second.empty()) return def;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return def;
  }
}
std::string strProp(const Properties& p, const std::string& key, const std::string& def) {
  auto it = p.find(key);
  return it == p.end() ? def : it->second;
}

std::vector<std::string> readLines(const stdfs::path& file) {
  std::vector<std::string> out;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty() && line[0] != '#') out.push_back(line);
  }
  return out;
}
void writeLines(const stdfs::path& file, const std::vector<std::string>& lines) {
  std::ofstream out(file);
  for (const std::string& l : lines) out << l << "\n";
}

/// La consola se lee en otro hilo (getline bloquea) y las órdenes se recogen en cada tick.
class Console {
 public:
  Console() {
    std::thread([this] {
      std::string line;
      while (std::getline(std::cin, line)) {
        std::lock_guard lock(mutex_);
        lines_.push_back(line);
      }
    }).detach();
  }
  std::vector<std::string> take() {
    std::lock_guard lock(mutex_);
    std::vector<std::string> out(lines_.begin(), lines_.end());
    lines_.clear();
    return out;
  }

 private:
  std::mutex mutex_;
  std::deque<std::string> lines_;
};

LevelInfo newLevel(const Properties& props) {
  LevelInfo info;
  info.name = strProp(props, "level-name", "world");
  const std::string s = strProp(props, "level-seed", "");
  if (s.empty()) {
    info.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count()) * 0x9E3779B97F4A7C15ull;
  } else {
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    info.seed = (end && *end == 0 && v != 0) ? static_cast<u64>(v) : seedFromString(s);
  }
  std::string type = strProp(props, "level-type", "DEFAULT");
  for (char& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  info.generator = type == "FLAT" ? "flat" : type == "LARGEBIOMES" ? "largeBiomes" : type == "AMPLIFIED" ? "amplified" : "default";
  if (info.generator == "flat") info.generatorOptions = GeneratorSettings::kDefaultFlat;
  info.mapFeatures = strProp(props, "generate-structures", "true") != "false";
  info.gameType = std::clamp(intProp(props, "gamemode", 0), 0, 1);
  info.difficulty = std::clamp(intProp(props, "difficulty", 1), 0, 3);
  info.dayTime = 0;
  info.gameRules = {{"doDaylightCycle", "true"}, {"doMobSpawning", "true"}, {"keepInventory", "false"},
                    {"doFireTick", "true"},      {"mobGriefing", "true"},   {"naturalRegeneration", "true"},
                    {"doTileDrops", "true"},     {"doMobLoot", "true"},     {"commandBlockOutput", "true"},
                    {"showDeathMessages", "true"}, {"sendCommandFeedback", "true"}, {"randomTickSpeed", "3"},
                    {"reducedDebugInfo", "false"}, {"logAdminCommands", "true"}};
  return info;
}

/// Suelo en (x, z): el primer bloque sólido desde arriba, sin contar hojas.
int groundY(const World& w, int x, int z) {
  for (int y = kChunkHeight - 2; y > 1; y--) {
    const int id = stateId(w.block(x, y, z));
    if (id == 0 || id == B::leaves || id == B::leaves2) continue;
    if (blockInfo(id).fullBox) return y;
  }
  return 64;
}

void printHelp() {
  std::cout << "mcweb-server: servidor de Minecraft 1.8 (modo offline) de MC-WEB\n"
               "  --dir <carpeta>   carpeta del servidor (server.properties, el mundo...). Por defecto, la actual\n"
               "  --port <puerto>   puerto (manda sobre server-port)\n"
               "  --threads <n>     hilos para generar el mundo\n"
               "Órdenes (de la consola, o por el chat para los operadores de ops.txt):\n"
               "  list, say <texto>, kick <jugador> [motivo], op <jugador>, deop <jugador>,\n"
               "  gamemode <supervivencia|creativo> [jugador], give <jugador> <objeto> [cantidad] [variante],\n"
               "  tp [jugador] <x y z | jugador>, kill [jugador], xp <puntos|niveles L> [jugador], difficulty <0-3>,\n"
               "  time <set|add> <día|noche|n>, whitelist <on|off|add|remove|list> [jugador], save-all, stop\n";
}

}  // namespace

int run(int argc, char** argv) {
  stdfs::path dir = ".";
  int portOverride = -1, threads = std::clamp(JobSystem::defaultThreadCount(), 1, 4);
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--help" || a == "-h") {
      printHelp();
      return 0;
    } else if (a == "--dir") {
      dir = next();
    } else if (a == "--port") {
      portOverride = std::atoi(next().c_str());
    } else if (a == "--threads") {
      threads = std::clamp(std::atoi(next().c_str()), 1, 16);
    } else if (a != "--nogui" && a != "nogui") {
      std::cerr << "opción desconocida: " << a << "\n";
    }
  }
  std::error_code ec;
  stdfs::create_directories(dir, ec);
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  const Properties props = readProperties(dir / "server.properties");
  if (strProp(props, "online-mode", "false") == "true")
    log::warn("online-mode=true no está soportado todavía: el servidor funciona en modo offline");
  const int port = portOverride >= 0 ? portOverride : intProp(props, "server-port", 25565);
  const int viewDistance = std::clamp(intProp(props, "view-distance", 8), 2, 12);

  // El mundo: el que haya en la carpeta o uno nuevo con lo de server.properties
  const std::string levelName = strProp(props, "level-name", "world");
  WorldSave save(dir / levelName);
  LevelInfo level;
  const bool existing = save.loadLevel(level);
  if (!existing) level = newLevel(props);
  log::info("mundo \"{}\" ({}, semilla {})", levelName, existing ? "guardado" : "nuevo", static_cast<i64>(level.seed));

  net::ServerWorld world(save, level.seed, GeneratorSettings::fromLevel(level.generator, level.generatorOptions, level.mapFeatures),
                         threads);
  GameSession session(world, level.seed);
  world.attach(session);
  session.setLocalPlayerActive(false);
  session.setMode(level.gameType == 1 ? GameMode::Creative : GameMode::Survival);
  auto applyRules = [&] { session.setRules({level.difficulty, level.ruleBool("keepInventory", false), level.ruleBool("doMobSpawning", true)}); };
  applyRules();

  // Punto de aparición: se carga antes de abrir (y, en un mundo nuevo, se busca el suelo)
  const auto t0 = std::chrono::steady_clock::now();
  if (!level.spawnSet) {
    // Tierra firme (ni océano, ni río ni playa) cerca del origen, como en el juego
    const auto [sx, sh, sz] = world.generator().findSpawn();
    level.spawn = {sx, sh, sz};
  }
  world.loadNow({level.spawn.x >> 4, level.spawn.z >> 4}, 2, level.dayTime);
  if (!level.spawnSet) {
    level.spawn.y = groundY(world.world(), level.spawn.x, level.spawn.z) + 1;  // (encima de los árboles no)
    level.spawnSet = true;
  }
  const glm::dvec3 spawn = glm::dvec3(level.spawn) + glm::dvec3(0.5, 0.0, 0.5);
  session.setSpawn(spawn);
  for (const ChunkPos& c : world.takeNewChunks()) session.populateChunk(c.x, c.z);
  log::info("punto de aparición listo en {:.1f} s ({} {} {})",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), level.spawn.x, level.spawn.y, level.spawn.z);

  // Lista blanca
  bool whitelistOn = strProp(props, "white-list", "false") == "true";
  std::vector<std::string> whitelist = readLines(dir / "whitelist.txt");

  // Operadores (ops.txt, un nombre por línea): pueden dar órdenes por el chat
  std::vector<std::string> ops = readLines(dir / "ops.txt");
  auto lowerName = [](std::string n) {
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n;
  };
  std::function<std::optional<std::vector<std::string>>(const std::string&)> serverCommandImpl;

  net::Server::Config cfg;
  cfg.isOp = [&](const std::string& name) {
    return std::any_of(ops.begin(), ops.end(), [&](const std::string& o) { return lowerName(o) == lowerName(name); });
  };
  cfg.setOp = [&](const std::string& name, bool op) {
    std::erase_if(ops, [&](const std::string& o) { return lowerName(o) == lowerName(name); });
    if (op) ops.push_back(name);
    writeLines(dir / "ops.txt", ops);
  };
  cfg.serverCommand = [&](const std::string& line) { return serverCommandImpl ? serverCommandImpl(line) : std::nullopt; };
  cfg.motd = strProp(props, "motd", "Un servidor de MC-WEB");
  cfg.levelType = level.generator;  // ("default", "flat", "largeBiomes" o "amplified": los nombres del protocolo)
  cfg.maxPlayers = std::clamp(intProp(props, "max-players", 20), 1, 1000);
  cfg.guestMode = level.gameType == 1 ? 1 : 0;
  cfg.viewDistance = viewDistance;
  cfg.difficulty = level.difficulty;
  cfg.hostName = "";
  cfg.playerDataDir = save.dir() / "playerdata";
  cfg.checkLogin = [&](const std::string& name) -> std::string {
    if (!whitelistOn) return "";
    for (const std::string& w : whitelist)
      if (w == name) return "";
    return "No estás en la lista blanca de este servidor";
  };
  net::Server server(session, cfg);
  session.setBlockListener([&server](const glm::ivec3& p, BlockState s) { server.blockChanged(p, s); });
  std::string err;
  if (!server.start(port, &err)) {
    log::error("no se pudo abrir el puerto {}: {}", port, err);
    return 1;
  }
  log::info("listo: MC-WEB y Minecraft 1.8 pueden entrar en el puerto {} (escribe \"help\" para ver las órdenes)", server.port());

  double worldTime = static_cast<double>(level.dayTime);
  auto saveEverything = [&](bool all) {
    world.saveAll(static_cast<i64>(worldTime), all);
    server.saveAll();
    level.dayTime = level.time = static_cast<i64>(worldTime);
    level.lastPlayed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    save.saveLevel(level);
    WorldSave::flush();
  };
  saveEverything(false);

  Console console;
  // Órdenes que no son de jugadores (las de jugadores las atiende el servidor: /gamemode, /give, /tp...)
  serverCommandImpl = [&](const std::string& line) -> std::optional<std::vector<std::string>> {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::string rest;
    std::getline(in >> std::ws, rest);
    if (cmd == "stop") {
      gStop = true;
      return std::vector<std::string>{"Cerrando el servidor..."};
    }
    if (cmd == "save-all") {
      saveEverything(true);
      return std::vector<std::string>{"Mundo guardado"};
    }
    if (cmd == "time") {
      std::istringstream r(rest);
      std::string sub, value;
      r >> sub >> value;
      if ((sub == "set" || sub == "add") && !value.empty()) {
        const double day = std::floor(worldTime / 24000.0) * 24000.0;
        if (sub == "add") {
          worldTime = std::max(0.0, worldTime + std::atof(value.c_str()));
        } else {
          worldTime = value == "day" || value == "dia" || value == "día" ? day + 1000
                      : value == "night" || value == "noche"              ? day + 13000
                                                                         : std::max(0.0, std::atof(value.c_str()));
        }
        return std::vector<std::string>{"Hora: " + std::to_string(static_cast<i64>(worldTime))};
      }
      return std::vector<std::string>{"Hora: " + std::to_string(static_cast<i64>(worldTime) % 24000) + " (día " + std::to_string(static_cast<i64>(worldTime) / 24000) + ")"};
    }
    if (cmd == "whitelist") {
      std::istringstream r(rest);
      std::string sub, who;
      r >> sub >> who;
      if (sub == "on" || sub == "off") {
        whitelistOn = sub == "on";
        return std::vector<std::string>{std::string("Lista blanca ") + (whitelistOn ? "activada" : "desactivada")};
      }
      if (sub == "add" && !who.empty()) {
        if (std::find(whitelist.begin(), whitelist.end(), who) == whitelist.end()) whitelist.push_back(who);
        writeLines(dir / "whitelist.txt", whitelist);
        return std::vector<std::string>{who + " añadido a la lista blanca"};
      }
      if (sub == "remove" && !who.empty()) {
        std::erase(whitelist, who);
        writeLines(dir / "whitelist.txt", whitelist);
        return std::vector<std::string>{who + " quitado de la lista blanca"};
      }
      std::string names;
      for (const std::string& w : whitelist) names += (names.empty() ? "" : ", ") + w;
      return std::vector<std::string>{std::string("Lista blanca (") + (whitelistOn ? "activada" : "desactivada") + "): " + (names.empty() ? "(vacía)" : names)};
    }
    return std::nullopt;
  };
  auto command = [&](const std::string& line) {
    std::string t = line;
    while (!t.empty() && (t.front() == ' ' || t.front() == '/')) t.erase(t.begin());
    if (t.empty()) return;
    if (t == "help" || t == "?") {
      printHelp();
      return;
    }
    for (const std::string& l : server.runCommand(t)) log::info("{}", l);
  };

  // Bucle a 20 ticks por segundo
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  auto next = start;
  double lastSave = 0;
  while (!gStop) {
    const double now = std::chrono::duration<double>(Clock::now() - start).count();
    for (const std::string& line : console.take()) command(line);
    if (level.ruleBool("doDaylightCycle", true)) worldTime += 1;
    for (const ChunkPos& c : world.takeNewChunks()) session.populateChunk(c.x, c.z);
    TickInput in;
    in.worldTime = worldTime;
    in.randomTickSpeed = std::max(0, std::atoi(level.rule("randomTickSpeed", "3").c_str()));
    session.tick(in);
    session.takeEvents();  // (sonidos y partículas: sin nadie mirando en el servidor)
    server.tick(now, worldTime);
    for (const std::string& line : server.takeChat()) log::info("[chat] {}", line);
    std::vector<ChunkPos> centers = server.wantedChunkCenters();
    centers.push_back({level.spawn.x >> 4, level.spawn.z >> 4});
    world.update(centers, viewDistance + 1, 20.0, static_cast<i64>(worldTime));
    if (now - lastSave >= 45.0) {  // guardado automático, como en el juego
      lastSave = now;
      saveEverything(false);
    }
    next += std::chrono::milliseconds(50);
    const auto behind = Clock::now() - next;
    if (behind > std::chrono::seconds(2)) {
      log::warn("el servidor va con {:.1f} s de retraso (¿demasiado para esta máquina?)", std::chrono::duration<double>(behind).count());
      next = Clock::now();
    }
    std::this_thread::sleep_until(next);
  }

  log::info("cerrando el servidor...");
  server.stop();
  saveEverything(true);
  log::info("mundo guardado. ¡Hasta luego!");
  return 0;
}

int main(int argc, char** argv) {
  const int code = run(argc, argv);  // (al volver ya se ha guardado y cerrado todo)
  // Sin pasar por exit(): el hilo de la consola sigue bloqueado leyendo stdin y la limpieza de
  // stdio se quedaría esperando su cerrojo
  // (y fflush(nullptr) también: incluye stdin)
  std::cout.flush();
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
}
