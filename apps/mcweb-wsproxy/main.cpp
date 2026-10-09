// mcweb-wsproxy: puente WebSocket <-> TCP para jugar desde el navegador en servidores de
// Minecraft 1.8 (el navegador no puede abrir conexiones TCP). La versión web se conecta a
// ws://<proxy>/<servidor>:<puerto> y el proxy abre la conexión TCP y pasa los bytes tal cual.
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "net/socket.h"
#include "net/websocket.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
constexpr SockT kBadSock = INVALID_SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SockT = int;
constexpr SockT kBadSock = -1;
#define CLOSESOCK ::close
#endif

using namespace mcw;

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

struct Options {
  std::string listenHost = "127.0.0.1";
  int listenPort = 25500;
  std::vector<std::string> allow;  // "host" o "host:puerto"; vacío = cualquiera
};

bool sendAll(SockT s, const void* data, std::size_t size) {
  const char* p = static_cast<const char*>(data);
  while (size > 0) {
    const int n = static_cast<int>(::send(s, p, static_cast<int>(std::min<std::size_t>(size, 1 << 20)), kSendFlags));
    if (n <= 0) return false;
    p += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}
bool sendAll(SockT s, const std::vector<u8>& v) { return sendAll(s, v.data(), v.size()); }
bool sendAll(SockT s, const std::string& v) { return sendAll(s, v.data(), v.size()); }

void setTimeout(SockT s, int seconds) {
#ifdef _WIN32
  DWORD ms = static_cast<DWORD>(seconds * 1000);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
  timeval tv{seconds, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

SockT connectTo(const std::string& host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return kBadSock;
  SockT s = kBadSock;
  for (addrinfo* a = res; a; a = a->ai_next) {
    s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == kBadSock) continue;
    if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
    CLOSESOCK(s);
    s = kBadSock;
  }
  freeaddrinfo(res);
  if (s != kBadSock) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
  }
  return s;
}

bool allowed(const Options& opt, const std::string& host, int port) {
  if (opt.allow.empty()) return true;
  for (const std::string& a : opt.allow)
    if (a == host || a == host + ":" + std::to_string(port)) return true;
  return false;
}

/// "%3A" y compañía (por si el navegador codifica la ruta).
std::string urlDecode(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

void closeWs(SockT ws, u16 code, const std::string& reason) {
  std::vector<u8> payload = {static_cast<u8>(code >> 8), static_cast<u8>(code)};
  payload.insert(payload.end(), reason.begin(), reason.end());
  sendAll(ws, net::encodeWsFrame(net::WsOpcode::Close, payload));
}

void handleClient(SockT ws, std::string peer, const Options& opt) {
  // 1) El saludo HTTP del navegador
  setTimeout(ws, 10);
  std::string req;
  char buf[65536];
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
    const int n = static_cast<int>(::recv(ws, buf, sizeof(buf), 0));
    if (n <= 0) {
      CLOSESOCK(ws);
      return;
    }
    req.append(buf, static_cast<std::size_t>(n));
  }
  const auto headerEnd = req.find("\r\n\r\n");
  const auto up = headerEnd == std::string::npos ? std::nullopt : net::parseUpgradeRequest(req.substr(0, headerEnd + 4));
  if (!up) {
    sendAll(ws, std::string("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"
                            "Esto es mcweb-wsproxy: conéctate con WebSocket a ws://<proxy>/<servidor>:<puerto>\n"));
    CLOSESOCK(ws);
    return;
  }
  std::string target = urlDecode(up->path);
  while (!target.empty() && target.front() == '/') target.erase(0, 1);
  std::string host;
  int port = 25565;
  net::splitAddress(target, host, port);
  if (host.empty() || !allowed(opt, host, port)) {
    log::warn("{}: destino no permitido: {}", peer, target);
    sendAll(ws, std::string("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n"));
    CLOSESOCK(ws);
    return;
  }
  sendAll(ws, net::upgradeResponse(up->key));
  std::vector<u8> fromWs(req.begin() + static_cast<std::ptrdiff_t>(headerEnd + 4), req.end());

  // 2) La conexión TCP al servidor de Minecraft
  const SockT tcp = connectTo(host, port);
  if (tcp == kBadSock) {
    log::warn("{}: no se pudo conectar a {}:{}", peer, host, port);
    closeWs(ws, 1011, "No se pudo conectar a " + host + ":" + std::to_string(port) + " (¿está encendido?)");
    CLOSESOCK(ws);
    return;
  }
  log::info("{} -> {}:{} abierto", peer, host, port);
  setTimeout(ws, 0);

  // 3) Pasar bytes en los dos sentidos hasta que uno cierre
  u64 upBytes = 0, down = 0;
  bool open = true;
  std::string why = "cerrado";
  while (open) {
    // Tramas que ya estén completas (del saludo o de antes)
    try {
      while (auto f = net::takeWsFrame(fromWs)) {
        switch (f->opcode) {
          case net::WsOpcode::Binary:
          case net::WsOpcode::Text:
          case net::WsOpcode::Continuation:
            upBytes += f->payload.size();
            if (!sendAll(tcp, f->payload)) open = false, why = "el servidor ha cerrado";
            break;
          case net::WsOpcode::Ping: sendAll(ws, net::encodeWsFrame(net::WsOpcode::Pong, f->payload)); break;
          case net::WsOpcode::Close:
            sendAll(ws, net::encodeWsFrame(net::WsOpcode::Close, f->payload));
            open = false;
            why = "el navegador ha cerrado";
            break;
          default: break;
        }
        if (!open) break;
      }
    } catch (const std::exception& e) {
      why = e.what();
      break;
    }
    if (!open) break;
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(ws, &rd);
    FD_SET(tcp, &rd);
    const int maxFd = static_cast<int>(std::max(ws, tcp)) + 1;
    timeval tv{60, 0};
    const int r = ::select(maxFd, &rd, nullptr, nullptr, &tv);
    if (r < 0) {
      why = "error de select";
      break;
    }
    if (FD_ISSET(ws, &rd)) {
      const int n = static_cast<int>(::recv(ws, buf, sizeof(buf), 0));
      if (n <= 0) {
        why = "el navegador se ha ido";
        break;
      }
      fromWs.insert(fromWs.end(), buf, buf + n);
    }
    if (FD_ISSET(tcp, &rd)) {
      const int n = static_cast<int>(::recv(tcp, buf, sizeof(buf), 0));
      if (n <= 0) {
        closeWs(ws, 1000, "El servidor ha cerrado la conexión");
        why = "el servidor ha cerrado";
        break;
      }
      down += static_cast<u64>(n);
      if (!sendAll(ws, net::encodeWsFrame(net::WsOpcode::Binary, std::span<const u8>(reinterpret_cast<const u8*>(buf), static_cast<std::size_t>(n))))) {
        why = "el navegador se ha ido";
        break;
      }
    }
  }
  log::info("{} -> {}:{} {} ({} KB subidos, {} KB bajados)", peer, host, port, why, upBytes / 1024, down / 1024);
  CLOSESOCK(tcp);
  CLOSESOCK(ws);
}

void printHelp() {
  std::cout << "mcweb-wsproxy: puente WebSocket <-> TCP para jugar en la versión web de MC-WEB a servidores de\n"
               "Minecraft 1.8 (el navegador no puede abrir conexiones TCP).\n"
               "  --listen <ip:puerto>  dónde escuchar (por defecto 127.0.0.1:25500, solo este equipo)\n"
               "  --allow <host[:puerto]>  solo deja conectar a ese servidor (se puede repetir). Sin --allow,\n"
               "                        a cualquiera: no lo abras a Internet sin limitarlo\n"
               "En la web, en Multijugador, pon como proxy ws://<esta máquina>:<puerto>.\n";
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--help" || a == "-h") {
      printHelp();
      return 0;
    } else if (a == "--listen") {
      net::splitAddress(next(), opt.listenHost, opt.listenPort);
      if (opt.listenPort == 25565) opt.listenPort = 25500;  // (sin puerto: el de siempre del proxy)
    } else if (a == "--allow") {
      opt.allow.push_back(next());
    } else {
      std::cerr << "opción desconocida: " << a << " (--help)\n";
      return 2;
    }
  }
#ifdef _WIN32
  WSADATA d;
  WSAStartup(MAKEWORD(2, 2), &d);
#else
  std::signal(SIGPIPE, SIG_IGN);
#endif

  const SockT listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u16>(opt.listenPort));
  if (opt.listenHost.empty() || opt.listenHost == "*" || opt.listenHost == "0.0.0.0") addr.sin_addr.s_addr = htonl(INADDR_ANY);
  else if (inet_pton(AF_INET, opt.listenHost.c_str(), &addr.sin_addr) != 1) addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listener, 64) != 0) {
    log::error("no se puede escuchar en {}:{} (¿ya está abierto?)", opt.listenHost, opt.listenPort);
    return 1;
  }
  const bool loopback = ntohl(addr.sin_addr.s_addr) >> 24 == 127;
  if (!loopback && opt.allow.empty())
    log::warn("escuchando fuera de este equipo sin --allow: cualquiera podría usar el proxy para conectarse a cualquier sitio");
  log::info("mcweb-wsproxy escuchando en ws://{}:{} {}", opt.listenHost, opt.listenPort,
            opt.allow.empty() ? std::string("(cualquier servidor)") : "(solo " + std::to_string(opt.allow.size()) + " servidores)");

  for (;;) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    const SockT c = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
    if (c == kBadSock) continue;
    char ip[64] = "?";
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    int nodelay = 1;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    std::thread(handleClient, c, std::string(ip), std::cref(opt)).detach();
  }
}
