// Chat (tecla T) y comandos (/gamemode, /time, /tp, /give...). Los comandos siguen la sintaxis de 1.8.
#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include "client/game.h"
#include "client/terrain.h"
#include "client/ui.h"
#include "core/log.h"
#include "data/items.h"
#include "save/anvil.h"

namespace mcw {
namespace {

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

/// Coordenada con "~" relativa (como en el juego). Devuelve false si no es un número.
bool coord(const std::string& s, double base, double& out, bool blockCenter) {
  try {
    if (!s.empty() && s[0] == '~') {
      out = base + (s.size() > 1 ? std::stod(s.substr(1)) : 0.0);
      return true;
    }
    std::size_t n = 0;
    out = std::stod(s, &n);
    if (n != s.size()) return false;
    // Un entero sin decimales es el centro del bloque (x y z), como hace el juego
    if (blockCenter && s.find('.') == std::string::npos) out += 0.5;
    return true;
  } catch (...) {
    return false;
  }
}

bool integer(const std::string& s, int& out) {
  try {
    std::size_t n = 0;
    out = std::stoi(s, &n);
    return n == s.size();
  } catch (...) {
    return false;
  }
}

int itemFromName(std::string name) {
  if (name.starts_with("minecraft:")) name = name.substr(10);
  int id = itemIdByName(name);
  if (id < 0) integer(name, id);
  return id > 0 && itemInfo(id).exists ? id : -1;
}

}  // namespace

void Game::chatMessage(std::string text, u32 color) {
  log::info("[chat] {}", text);
  chat_.push_back({std::move(text), color, runTime_});
  if (chat_.size() > 100) chat_.erase(chat_.begin());
}

void Game::drawChat(bool open) {
  // Abajo a la izquierda: las 10 últimas líneas (se desvanecen a los 10 s si el chat está cerrado)
  const float gh = static_cast<float>(ui_->guiHeight());
  float y = gh - (open ? 36.0f : 50.0f);
  int shown = 0;
  for (auto it = chat_.rbegin(); it != chat_.rend() && shown < (open ? 20 : 10); ++it, shown++) {
    const double age = runTime_ - it->time;
    if (!open && age > 10.0) break;
    const float a = open ? 1.0f : static_cast<float>(std::clamp(10.0 - age, 0.0, 1.0));
    const std::string line = asciiText(it->text);
    ui_->rect(2, y - 1, 320, 9, static_cast<u32>(a * 128) << 24);
    ui_->text(4, y, line, it->color);
    y -= 9;
  }
  if (open) {
    chatField_.x = 2;
    chatField_.y = gh - 14;
    chatField_.w = static_cast<float>(ui_->guiWidth()) - 4;
    chatField_.h = 12;
    chatField_.focused = true;
    drawTextField(*ui_, chatField_, runTime_);
  }
}

void Game::runCommand(const std::string& line) {
  if (line.empty()) return;
  if (net_) {  // en un servidor, los mensajes y los comandos los procesa él
    net_->sendChat(line);
    return;
  }
  if (line[0] != '/') {
    const std::string msg = "<" + settings_.playerName + "> " + line;
    chatMessage(msg);
    if (server_) server_->broadcastChat(msg);
    return;
  }
  const std::vector<std::string> a = split(line.substr(1));
  if (a.empty()) return;
  const std::string& cmd = a[0];
  Player& p = session_->player();
  const bool cheats = level_.allowCommands || !save_;
  auto usage = [&](const char* u) { chatMessage(std::string("Uso: ") + u, 0xFF5555); };
  auto ok = [&](const std::string& m) {
    if (level_.ruleBool("sendCommandFeedback", true)) chatMessage(m, 0xAAAAAA);
  };

  if (cmd == "help" || cmd == "?") {
    chatMessage("Comandos: /gamemode /time /tp /give /seed /difficulty /gamerule /kill /spawnpoint", 0xFFFF55);
    chatMessage("/setworldspawn /summon /setblock /fill /clear /say /weather /xp /help", 0xFFFF55);
    return;
  }
  if (cmd == "seed") {
    chatMessage(std::format("Semilla: {}", static_cast<i64>(level_.seed)));
    return;
  }
  if (cmd == "say") {
    chatMessage("[Jugador] " + line.substr(std::min(line.size(), std::size_t{5})));
    return;
  }
  if (!cheats) {
    chatMessage("No tienes permiso para usar este comando (activa los trucos al crear el mundo)", 0xFF5555);
    return;
  }

  if (cmd == "gamemode" || cmd == "gm") {
    if (a.size() < 2) return usage("/gamemode <survival|creative|0|1>");
    const std::string& m = a[1];
    if (m == "0" || m == "s" || m == "survival" || m == "supervivencia") session_->setMode(GameMode::Survival);
    else if (m == "1" || m == "c" || m == "creative" || m == "creativo") session_->setMode(GameMode::Creative);
    else return usage("/gamemode <survival|creative>");
    level_.gameType = p.creative() ? 1 : 0;
    ok(std::string("Modo de juego cambiado a ") + (p.creative() ? "creativo" : "supervivencia"));
  } else if (cmd == "time") {
    if (a.size() < 2) return usage("/time <set|add|query> <valor>");
    if (a[1] == "query") {
      const double day = std::fmod(worldTime_, 24000.0);
      ok(std::format("La hora es {}", a.size() > 2 && a[2] == "gametime" ? static_cast<i64>(worldTime_) : static_cast<i64>(day)));
      return;
    }
    if (a.size() < 3) return usage("/time <set|add> <valor>");
    int v = 0;
    const std::string& s = a[2];
    if (s == "day") v = 1000;
    else if (s == "noon") v = 6000;
    else if (s == "night") v = 13000;
    else if (s == "midnight") v = 18000;
    else if (!integer(s, v)) return usage("/time set <day|night|noon|midnight|numero>");
    if (a[1] == "set") worldTime_ = std::floor(worldTime_ / 24000.0) * 24000.0 + v;
    else if (a[1] == "add") worldTime_ += v;
    else return usage("/time <set|add> <valor>");
    ok(std::format("Hora cambiada a {}", static_cast<i64>(std::fmod(worldTime_, 24000.0))));
  } else if (cmd == "tp" || cmd == "teleport") {
    if (a.size() < 4) return usage("/tp <x> <y> <z> [yaw] [pitch]");
    glm::dvec3 t;
    if (!coord(a[1], p.pos.x, t.x, true) || !coord(a[2], p.pos.y, t.y, false) || !coord(a[3], p.pos.z, t.z, true))
      return usage("/tp <x> <y> <z>");
    p.pos = p.prevPos = t;
    p.motion = glm::dvec3(0);
    p.fallDistance = 0;
    double yaw = 0, pitch = 0;
    if (a.size() >= 6 && coord(a[4], save::yawToSave(p.yaw), yaw, false) && coord(a[5], -glm::degrees(p.pitch), pitch, false)) {
      cam_.yaw = p.yaw = save::yawFromSave(static_cast<float>(yaw));
      cam_.pitch = p.pitch = -glm::radians(static_cast<float>(std::clamp(pitch, -90.0, 90.0)));
    }
    ok(std::format("Teletransportado a {:.1f}, {:.1f}, {:.1f}", t.x, t.y, t.z));
  } else if (cmd == "give") {
    // /give [jugador] <objeto> [cantidad] [datos]: el jugador se puede omitir (un jugador)
    std::size_t i = 1;
    if (a.size() > 2 && (a[1] == "@p" || a[1] == "@a" || a[1] == "@s" || itemFromName(a[1]) < 0)) i = 2;
    if (a.size() <= i) return usage("/give <objeto> [cantidad] [datos]");
    const int id = itemFromName(a[i]);
    if (id < 0) {
      chatMessage("No existe el objeto " + a[i], 0xFF5555);
      return;
    }
    int count = 1, meta = 0;
    if (a.size() > i + 1) integer(a[i + 1], count);
    if (a.size() > i + 2) integer(a[i + 2], meta);
    count = std::clamp(count, 1, 64 * 36);
    int left = count;
    while (left > 0) {
      const int n = std::min(left, std::max(1, itemInfo(id).stackSize));
      const ItemStack rest = p.inventory.add(ItemStack(id, n, meta));
      if (!rest.empty()) {
        session_->throwItem(rest);
      }
      left -= n;
    }
    ok(std::format("Dado {} x {}", count, itemDisplayNameEs(id, meta)));
  } else if (cmd == "difficulty") {
    if (a.size() < 2) return usage("/difficulty <peaceful|easy|normal|hard|0-3>");
    static const char* names[] = {"peaceful", "easy", "normal", "hard"};
    int d = -1;
    for (int i = 0; i < 4; i++)
      if (a[1] == names[i] || a[1] == std::string(1, names[i][0])) d = i;
    if (d < 0 && !integer(a[1], d)) d = -1;
    if (d < 0 || d > 3) return usage("/difficulty <peaceful|easy|normal|hard>");
    level_.difficulty = d;
    applyLevelRules();
    ok(std::format("Dificultad cambiada a {}", names[d]));
  } else if (cmd == "gamerule") {
    if (a.size() < 2) {
      std::string all;
      for (const auto& [k, v] : level_.gameRules) all += k + " ";
      chatMessage(all);
      return;
    }
    if (a.size() < 3) {
      ok(a[1] + " = " + level_.rule(a[1], "(sin definir)"));
      return;
    }
    level_.gameRules[a[1]] = a[2];
    applyLevelRules();
    ok("Regla " + a[1] + " cambiada a " + a[2]);
  } else if (cmd == "kill") {
    session_->killPlayer();
    ok("Jugador eliminado");
  } else if (cmd == "spawnpoint" || cmd == "setworldspawn") {
    glm::dvec3 t = glm::floor(p.pos);
    if (a.size() >= 4 && !(coord(a[1], p.pos.x, t.x, false) && coord(a[2], p.pos.y, t.y, false) && coord(a[3], p.pos.z, t.z, false)))
      return usage("/spawnpoint [x y z]");
    spawn_ = glm::floor(t) + glm::dvec3(0.5, 0.0, 0.5);
    session_->setSpawn(spawn_);
    level_.spawn = glm::ivec3(glm::floor(t));
    level_.spawnSet = true;
    ok(std::format("Punto de aparición en {}, {}, {}", level_.spawn.x, level_.spawn.y, level_.spawn.z));
  } else if (cmd == "summon") {
    if (a.size() < 2) return usage("/summon <Pig|Cow|Sheep|Chicken|Zombie|Skeleton|Creeper|Spider> [x y z]");
    auto type = save::mobTypeFromSaveId(a[1]);
    if (!type) {
      chatMessage("No se puede invocar " + a[1], 0xFF5555);
      return;
    }
    glm::dvec3 t = p.pos;
    if (a.size() >= 5 && !(coord(a[2], p.pos.x, t.x, true) && coord(a[3], p.pos.y, t.y, false) && coord(a[4], p.pos.z, t.z, true)))
      return usage("/summon <criatura> [x y z]");
    session_->spawnMob(*type, t);
    ok("Criatura invocada");
  } else if (cmd == "setblock" || cmd == "fill") {
    const bool fill = cmd == "fill";
    const std::size_t nc = fill ? 6 : 3;
    if (a.size() < nc + 2) return usage(fill ? "/fill <x1 y1 z1> <x2 y2 z2> <bloque> [datos]" : "/setblock <x y z> <bloque> [datos]");
    double c[6];
    const double base[3] = {p.pos.x, p.pos.y, p.pos.z};
    for (std::size_t i = 0; i < nc; i++)
      if (!coord(a[1 + i], base[i % 3], c[i], false)) return usage("coordenadas no validas");
    std::string name = a[1 + nc];
    if (name.starts_with("minecraft:")) name = name.substr(10);
    int id = blockIdByName(name);
    if (id < 0 && !integer(name, id)) id = -1;
    if (id < 0 || id > 255) {
      chatMessage("No existe el bloque " + a[1 + nc], 0xFF5555);
      return;
    }
    int meta = 0;
    if (a.size() > nc + 2) integer(a[nc + 2], meta);
    const glm::ivec3 p0 = glm::ivec3(glm::floor(glm::dvec3(c[0], c[1], c[2])));
    const glm::ivec3 p1 = fill ? glm::ivec3(glm::floor(glm::dvec3(c[3], c[4], c[5]))) : p0;
    const glm::ivec3 lo = glm::min(p0, p1), hi = glm::max(p0, p1);
    const long long volume = static_cast<long long>(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
    if (volume > 32768) {
      chatMessage(std::format("Demasiados bloques ({}; máximo 32768)", volume), 0xFF5555);
      return;
    }
    terrain_->beginBatch();
    for (int y = std::max(0, lo.y); y <= std::min(255, hi.y); y++)
      for (int z = lo.z; z <= hi.z; z++)
        for (int x = lo.x; x <= hi.x; x++) terrain_->setBlock(x, y, z, makeState(id, meta));
    terrain_->endBatch();
    ok(std::format("{} bloques cambiados", volume));
  } else if (cmd == "tree") {
    // Solo para pruebas: /tree <x y z> <tipo 0..5>: planta un brote (los gigantes, con 4) y lo hace crecer
    if (!cheats) {
      chatMessage("Los comandos no están permitidos en este mundo", 0xFF5555);
      return;
    }
    if (a.size() < 5) return usage("/tree <x y z> <tipo 0-5>");
    double c[3];
    const double base[3] = {p.pos.x, p.pos.y, p.pos.z};
    for (std::size_t i = 0; i < 3; i++)
      if (!coord(a[1 + i], base[i], c[i], false)) return usage("coordenadas no validas");
    int type = 0;
    integer(a[4], type);
    const glm::ivec3 at = glm::ivec3(glm::floor(glm::dvec3(c[0], c[1], c[2])));
    for (int dx = 0; dx <= ((type == 3 || type == 5) ? 1 : 0); dx++)
      for (int dz = 0; dz <= ((type == 3 || type == 5) ? 1 : 0); dz++) terrain_->setBlock(at.x + dx, at.y, at.z + dz, makeState(B::sapling, type | 8));
    for (int i = 0; i < 2000 && stateId(terrain_->world().block(at.x, at.y, at.z)) == B::sapling; i++) session_->growthTickAt(at);
    ok("árbol");
  } else if (cmd == "flow") {
    // Solo para pruebas: /flow <x y z> water|lava: pone una fuente y deja que fluya
    if (!cheats) {
      chatMessage("Los comandos no están permitidos en este mundo", 0xFF5555);
      return;
    }
    if (a.size() < 5) return usage("/flow <x y z> <water|lava>");
    double c[3];
    const double base[3] = {p.pos.x, p.pos.y, p.pos.z};
    for (std::size_t i = 0; i < 3; i++)
      if (!coord(a[1 + i], base[i], c[i], false)) return usage("coordenadas no validas");
    const glm::ivec3 at = glm::ivec3(glm::floor(glm::dvec3(c[0], c[1], c[2])));
    session_->placeBlock(at, makeState(a[4] == "lava" ? B::flowing_lava : B::flowing_water, 0));
    ok("fluyendo");
  } else if (cmd == "tile") {
    // Solo para pruebas: /tile sign x y z línea1|línea2|línea3|línea4, /tile banner x y z color [dibujo:color ...],
    // /tile skull x y z tipo rotación
    if (!cheats) {
      chatMessage("Los comandos no están permitidos en este mundo", 0xFF5555);
      return;
    }
    if (a.size() < 6) return usage("/tile <sign|banner|skull> <x y z> ...");
    double c[3];
    const double base[3] = {p.pos.x, p.pos.y, p.pos.z};
    for (std::size_t i = 0; i < 3; i++)
      if (!coord(a[2 + i], base[i], c[i], false)) return usage("coordenadas no validas");
    const glm::ivec3 at = glm::ivec3(glm::floor(glm::dvec3(c[0], c[1], c[2])));
    TileEntities& te = session_->tiles();
    const TilePos key{at.x, at.y, at.z};
    if (a[1] == "sign") {
      SignText t;
      std::string rest;
      for (std::size_t i = 5; i < a.size(); i++) rest += (i > 5 ? " " : "") + a[i];
      std::size_t from = 0;
      for (std::size_t i = 0; i < 4 && from <= rest.size(); i++) {
        const std::size_t bar = rest.find('|', from);
        t.lines[i] = rest.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
        if (bar == std::string::npos) break;
        from = bar + 1;
      }
      session_->setSignText(at, t);
    } else if (a[1] == "banner") {
      BannerData b;
      int base = 15;
      integer(a[5], base);
      b.base = static_cast<u8>(base & 15);
      for (std::size_t i = 6; i < a.size(); i++) {
        const std::size_t colon = a[i].find(':');
        int col = 0;
        if (colon != std::string::npos) integer(a[i].substr(colon + 1), col);
        b.patterns.push_back({a[i].substr(0, colon), static_cast<u8>(col & 15)});
      }
      te.banners[key] = b;
    } else if (a[1] == "skull") {
      SkullData sk;
      int type = 0, rot = 0;
      integer(a[5], type);
      if (a.size() > 6) integer(a[6], rot);
      sk.type = static_cast<u8>(std::clamp(type, 0, 4));
      sk.rot = static_cast<u8>(rot & 15);
      te.skulls[key] = sk;
    } else {
      return usage("/tile <sign|banner|skull> <x y z> ...");
    }
    ok("datos puestos");
  } else if (cmd == "clear") {
    p.inventory.clear();
    ok("Inventario vaciado");
  } else if (cmd == "weather") {
    ok("El tiempo cambiará cuando haya lluvia (próximamente)");
  } else if (cmd == "xp") {
    // /xp <puntos>  o  /xp <niveles>L  (con negativo en niveles se quitan)
    if (a.size() < 2) return usage("/xp <puntos> | /xp <niveles>L");
    std::string amount = a[1];
    const bool levels = !amount.empty() && (amount.back() == 'L' || amount.back() == 'l');
    if (levels) amount.pop_back();
    char* end = nullptr;
    const long n = std::strtol(amount.c_str(), &end, 10);
    if (amount.empty() || (end && *end != '\0')) return usage("/xp <puntos> | /xp <niveles>L");
    if (levels) {
      p.addXpLevels(static_cast<int>(n));
      ok(std::format("{} niveles de experiencia: ahora {}", n > 0 ? "+" + std::to_string(n) : std::to_string(n), p.xpLevel));
    } else if (n < 0) {
      usage("/xp <puntos> (positivo) | /xp <niveles>L");
    } else {
      session_->giveXp(p, static_cast<int>(n));
      ok(std::format("+{} puntos de experiencia", n));
    }
  } else {
    chatMessage("Comando desconocido. Escribe /help", 0xFF5555);
  }
}

}  // namespace mcw
