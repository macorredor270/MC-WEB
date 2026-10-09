// Órdenes del servidor (/gamemode, /give, /tp, /kill...): las da la consola o un operador por el chat. Siguen la
// sintaxis de 1.8, con los nombres de jugador sin distinguir mayúsculas.
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>

#include "core/log.h"
#include "data/items.h"
#include "net/server.h"
#include "net/server_remote.h"

namespace mcw::net {
namespace {

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
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

/// Coordenada, con "~" relativa. Un entero sin decimales es el centro del bloque (x y z), como hace el juego.
bool coord(const std::string& s, double base, double& out, bool blockCenter) {
  try {
    if (!s.empty() && s[0] == '~') {
      out = base + (s.size() > 1 ? std::stod(s.substr(1)) : 0.0);
      return true;
    }
    std::size_t n = 0;
    out = std::stod(s, &n);
    if (n != s.size()) return false;
    if (blockCenter && s.find('.') == std::string::npos) out += 0.5;
    return true;
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

/// Modo de juego por su nombre o número (0 supervivencia, 1 creativo); -1 si no es uno que tengamos.
int parseGameMode(const std::string& s) {
  const std::string m = lower(s);
  if (m == "0" || m == "s" || m == "survival" || m == "supervivencia") return 0;
  if (m == "1" || m == "c" || m == "creative" || m == "creativo") return 1;
  return -1;
}

}  // namespace

std::vector<std::string> Server::runCommand(const std::string& rawLine, const std::string& fromName) {
  std::vector<std::string> out;
  auto say = [&](std::string s) { out.push_back(std::move(s)); };
  const std::string line = !rawLine.empty() && rawLine[0] == '/' ? rawLine.substr(1) : rawLine;
  const std::vector<std::string> a = split(line);
  if (a.empty()) return out;
  const std::string cmd = lower(a[0]);

  auto find = [&](const std::string& name) -> Remote* {
    for (auto& r : remotes_)
      if (r->joined && !r->closed && lower(r->name) == lower(name)) return r.get();
    return nullptr;
  };
  Remote* from = fromName.empty() ? nullptr : find(fromName);
  // Jugador al que va dirigida la orden: el que se nombra en el argumento `i`, o quien la da
  auto target = [&](std::size_t i) -> Remote* {
    if (a.size() > i) {
      Remote* r = find(a[i]);
      if (!r) say("No se encuentra al jugador " + a[i]);
      return r;
    }
    if (!from) say("Hay que decir de qué jugador se trata");
    return from;
  };
  auto usage = [&](const char* u) { say(std::string("Uso: ") + u); };

  if (cmd == "help" || cmd == "?") {
    say("Órdenes: /gamemode /give /tp /kill /xp /difficulty /time /say /list /kick /op /deop /whitelist /save-all /stop");
  } else if (cmd == "list") {
    std::string names;
    int n = 0;
    for (const auto& r : remotes_)
      if (r->joined && !r->closed) {
        names += (names.empty() ? "" : ", ") + r->name;
        n++;
      }
    say(std::to_string(n) + " de " + std::to_string(config_.maxPlayers) + " jugadores: " + (names.empty() ? "(nadie)" : names));
  } else if (cmd == "say") {
    const std::size_t at = line.find(' ');
    if (at != std::string::npos && at + 1 < line.size()) {
      const std::string text = line.substr(at + 1);
      broadcastChat(fromName.empty() ? "\xC2\xA7" "d[Servidor] " + text : "[" + fromName + "] " + text);
      log::info("[{}] {}", fromName.empty() ? "Servidor" : fromName, text);
    }
  } else if (cmd == "kick") {
    if (a.size() < 2) return usage("/kick <jugador> [motivo]"), out;
    std::string reason;
    for (std::size_t i = 2; i < a.size(); i++) reason += (reason.empty() ? "" : " ") + a[i];
    if (kickPlayer(a[1], reason.empty() ? "Expulsado por un administrador" : reason)) say("Se ha expulsado a " + a[1]);
    else say(a[1] + " no está conectado");
  } else if (cmd == "op" || cmd == "deop") {
    if (a.size() < 2) return usage("/op <jugador>"), out;
    if (!config_.setOp) {
      say("Este servidor no tiene lista de operadores");
    } else {
      config_.setOp(a[1], cmd == "op");
      say(a[1] + (cmd == "op" ? " es ahora operador" : " ya no es operador"));
    }
  } else if (cmd == "gamemode" || cmd == "gm") {
    if (a.size() < 2) return usage("/gamemode <supervivencia|creativo|0|1> [jugador]"), out;
    const int mode = parseGameMode(a[1]);
    if (mode < 0) return usage("/gamemode <supervivencia|creativo|0|1> [jugador]"), out;
    if (Remote* r = target(2)) {
      r->player.mode = mode == 1 ? GameMode::Creative : GameMode::Survival;
      if (mode == 0) r->player.flying = false;
      BufferWriter gs;
      gs.u8(3).f32(static_cast<float>(mode));  // Change Game State: 3 = cambio de modo
      send(*r, 0x2B, gs);
      BufferWriter ab;
      ab.i8(mode == 1 ? 0x0D : 0).f32(0.05f).f32(0.1f);
      send(*r, 0x39, ab);
      BufferWriter pl;
      pl.varInt(1).varInt(1);  // Player List Item: 1 = cambia el modo
      pl.bytes(uuidFromString(r->uuid));
      pl.varInt(mode);
      sendAll(0x38, pl);
      say(r->name + " está ahora en modo " + (mode == 1 ? "creativo" : "supervivencia"));
    }
  } else if (cmd == "give") {
    if (a.size() < 3) return usage("/give <jugador> <objeto> [cantidad] [variante]"), out;
    Remote* r = find(a[1]);
    if (!r) return say("No se encuentra al jugador " + a[1]), out;
    const int id = itemFromName(a[2]);
    int count = 1, meta = 0;
    if (id < 0) return say("No existe el objeto " + a[2]), out;
    if (a.size() > 3 && (!integer(a[3], count) || count < 1 || count > 64)) return say("La cantidad va de 1 a 64"), out;
    if (a.size() > 4 && (!integer(a[4], meta) || meta < 0)) return say("La variante no es válida"), out;
    ItemStack st(id, count, meta);
    const ItemStack rest = r->player.inventory.add(st);
    if (!rest.empty()) session_.dropItem(r->player.pos + glm::dvec3(0, 1.0, 0), rest, {0, 0.1, 0});
    sendInventory(*r);
    say("Se le ha dado " + std::to_string(count) + " de " + std::string(itemInfo(id).name) + " a " + r->name);
  } else if (cmd == "tp") {
    // /tp <x> <y> <z> | /tp <jugador> | /tp <jugador> <x> <y> <z> | /tp <jugador> <jugador>
    Remote* who = nullptr;
    glm::dvec3 dest{0};
    std::size_t coords = 0;
    bool haveDest = false;
    if (a.size() == 4 && from) { who = from; coords = 1; }
    else if (a.size() == 5) { who = find(a[1]); coords = 2; if (!who) return say("No se encuentra al jugador " + a[1]), out; }
    else if (a.size() == 3) {
      who = find(a[1]);
      Remote* to = find(a[2]);
      if (!who || !to) return say("No se encuentra a ese jugador"), out;
      dest = to->player.pos;
      haveDest = true;
    } else if (a.size() == 2 && from) {
      who = from;
      Remote* to = find(a[1]);
      if (!to) return say("No se encuentra al jugador " + a[1]), out;
      dest = to->player.pos;
      haveDest = true;
    } else {
      return usage("/tp [jugador] <x> <y> <z>  o  /tp [jugador] <jugador>"), out;
    }
    if (!haveDest) {
      if (!coord(a[coords], who->player.pos.x, dest.x, true) || !coord(a[coords + 1], who->player.pos.y, dest.y, false) ||
          !coord(a[coords + 2], who->player.pos.z, dest.z, true))
        return say("Esas coordenadas no son válidas"), out;
    }
    who->player.pos = who->player.prevPos = who->prevPos = dest;
    BufferWriter pos;
    pos.f64(dest.x).f64(dest.y).f64(dest.z).f32(yawToMc(who->player.yaw)).f32(pitchToMc(who->player.pitch)).i8(0);
    send(*who, 0x08, pos);
    char buf[96];
    std::snprintf(buf, sizeof buf, "Teletransportado a %s a %.1f, %.1f, %.1f", who->name.c_str(), dest.x, dest.y, dest.z);
    say(buf);
  } else if (cmd == "kill") {
    if (Remote* r = target(1)) {
      r->player.health = 0;  // (también a un creativo: es la orden para eso)
      r->player.dead = true;
      say("Se ha matado a " + r->name);
    }
  } else if (cmd == "xp") {
    if (a.size() < 2) return usage("/xp <cantidad> [jugador]"), out;
    int n = 0;
    std::string amount = a[1];
    const bool levels = !amount.empty() && (amount.back() == 'L' || amount.back() == 'l');
    if (levels) amount.pop_back();
    if (!integer(amount, n) || n <= 0) return usage("/xp <cantidad>[L] [jugador]"), out;
    if (Remote* r = target(2)) {
      if (levels) r->player.addXpLevels(n);
      else session_.giveXp(r->player, n);
      say("Se le ha dado experiencia a " + r->name);
    }
  } else if (cmd == "difficulty") {
    if (a.size() < 2) return usage("/difficulty <pacífico|fácil|normal|difícil|0-3>"), out;
    const std::string d = lower(a[1]);
    int v = d == "peaceful" || d == "p" ? 0 : d == "easy" || d == "e" ? 1 : d == "normal" || d == "n" ? 2 : d == "hard" || d == "h" ? 3 : -1;
    if (d.rfind("pac", 0) == 0) v = 0;
    else if (d.rfind("fac", 0) == 0 || d.rfind("fác", 0) == 0) v = 1;
    else if (d.rfind("dif", 0) == 0) v = 3;
    if (v < 0 && (!integer(d, v) || v < 0 || v > 3)) v = -1;
    if (v < 0) return usage("/difficulty <pacífico|fácil|normal|difícil|0-3>"), out;
    config_.difficulty = v;
    GameRules rules = session_.rules();
    rules.difficulty = v;
    session_.setRules(rules);
    BufferWriter w;
    w.u8(static_cast<u8>(v));
    sendAll(0x41, w);
    say("Dificultad cambiada");
  } else if (cmd == "setfood") {  // ayuda para pruebas: poner el hambre de un jugador (0 a 20)
    if (a.size() < 2) return usage("/setfood <0-20> [jugador]"), out;
    int n = 0;
    if (!integer(a[1], n) || n < 0 || n > 20) return usage("/setfood <0-20> [jugador]"), out;
    if (Remote* r = target(2)) {
      r->player.food = n;
      r->player.saturation = 0;
      say("Hambre de " + r->name + ": " + std::to_string(n));
    }
  } else if (config_.serverCommand) {
    if (auto res = config_.serverCommand(line)) return *res;
    say("Orden desconocida: " + a[0] + " (escribe \"help\")");
  } else {
    say("Orden desconocida: " + a[0] + " (escribe \"help\")");
  }
  return out;
}

}  // namespace mcw::net
