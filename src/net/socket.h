#pragma once
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/types.h"

namespace mcw::net {

/// Conexión de bytes con un servidor o un cliente (TCP, o WebSocket en el navegador).
/// Todo es sin bloquear: se llama a poll() cada frame.
class Transport {
 public:
  virtual ~Transport() = default;
  enum class Status { Connecting, Open, Closed };
  virtual Status status() const = 0;
  /// Por qué se cerró (texto para el jugador).
  virtual std::string error() const = 0;
  /// Encola bytes para enviar.
  virtual void send(std::span<const u8> data) = 0;
  /// Envía lo pendiente y lee lo que haya llegado (lo devuelve).
  virtual std::vector<u8> poll() = 0;
  virtual void close() = 0;
};

/// Conexión TCP a host:puerto (en escritorio). Resuelve y conecta en segundo plano.
std::unique_ptr<Transport> connectTcp(const std::string& host, int port);
/// Conexión a un servidor a través del proxy WebSocket (mcweb-wsproxy): `proxyUrl` p. ej.
/// "ws://localhost:25500"; el proxy abre el TCP a host:puerto.
std::unique_ptr<Transport> connectViaProxy(const std::string& proxyUrl, const std::string& host, int port);
/// En el navegador solo existe connectViaProxy; en escritorio, connectTcp.
bool tcpAvailable();

/// Servidor TCP que acepta conexiones (hostear / abrir en LAN). Solo escritorio.
class Listener {
 public:
  virtual ~Listener() = default;
  /// Nuevas conexiones desde la última llamada.
  virtual std::vector<std::unique_ptr<Transport>> accept() = 0;
  virtual int port() const = 0;
};
/// `port` 0 = uno libre cualquiera. nullptr si no se puede (puerto ocupado, navegador...).
std::unique_ptr<Listener> listenTcp(int port, std::string* error = nullptr);

/// Anuncio y descubrimiento de partidas en la LAN como 1.8 (UDP multicast 224.0.2.60:4445,
/// mensaje "[MOTD]texto[/MOTD][AD]puerto[/AD]").
class LanBroadcaster {
 public:
  LanBroadcaster(std::string motd, int port);
  ~LanBroadcaster();
  /// Enviar el anuncio (cada 1,5 s).
  void tick(double now);

 private:
  std::string message_;
  [[maybe_unused]] double last_ = -10;  // (en el navegador no hay LAN)
  [[maybe_unused]] intptr_t sock_ = -1;
};

struct LanGame {
  std::string motd;
  std::string address;  // ip:puerto
  double seen = 0;
};

class LanListener {
 public:
  LanListener();
  ~LanListener();
  /// Lee anuncios y quita los que llevan más de 5 s sin oírse.
  void poll(double now);
  const std::vector<LanGame>& games() const { return games_; }

 private:
  [[maybe_unused]] intptr_t sock_ = -1;
  std::vector<LanGame> games_;
};

/// Direcciones IP locales de este equipo (para decir a los amigos a qué conectarse).
std::vector<std::string> localAddresses();

/// "host:puerto" -> host y puerto (25565 por defecto).
void splitAddress(const std::string& address, std::string& host, int& port);

}  // namespace mcw::net
