#include "net/socket.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "core/log.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/websocket.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
using SockT = SOCKET;
constexpr SockT kBadSock = INVALID_SOCKET;
#define MCW_CLOSESOCK closesocket
#define MCW_WOULDBLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using SockT = int;
constexpr SockT kBadSock = -1;
#define MCW_CLOSESOCK ::close
#define MCW_WOULDBLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
// Que un envío a una conexión cerrada no mate el programa con SIGPIPE
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#elif !defined(__EMSCRIPTEN__)
constexpr int kSendFlags = 0;
#endif

namespace mcw::net {

void splitAddress(const std::string& address, std::string& host, int& port) {
  host = address;
  port = 25565;
  const auto colon = address.rfind(':');
  if (colon != std::string::npos && address.find(':') == colon) {  // (no IPv6 sin corchetes)
    host = address.substr(0, colon);
    try {
      port = std::stoi(address.substr(colon + 1));
    } catch (...) {
      port = 25565;
    }
  }
  while (!host.empty() && host.back() == ' ') host.pop_back();
  while (!host.empty() && host.front() == ' ') host.erase(host.begin());
}

#if !defined(__EMSCRIPTEN__)

namespace {

bool initSockets() {
#ifdef _WIN32
  static const bool ok = [] {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  return ok;
#else
  return true;
#endif
}

/// Último error del sistema de sockets (para el log).
std::string lastSocketError() {
#ifdef _WIN32
  return "WSA " + std::to_string(WSAGetLastError());
#else
  return std::strerror(errno);
#endif
}

void setBlocking(SockT s) {
#ifdef _WIN32
  u_long mode = 0;
  ioctlsocket(s, FIONBIO, &mode);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
#endif
}

/// Cierre ordenado en segundo plano: manda lo que quede, avisa del fin (FIN) y lee lo que siga
/// llegando hasta que el otro lado cierre (o 3 s). Si se cerrara con datos sin leer, el sistema
/// mandaría un RST y el otro lado perdería lo último (p. ej. el motivo de una expulsión).
void closeGracefully(SockT s, std::vector<u8> pending) {
  std::thread([s, pending = std::move(pending)] {
    setBlocking(s);
#ifdef _WIN32
    DWORD ms = 500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{0, 500000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    std::size_t off = 0;
    for (int tries = 0; off < pending.size() && tries < 6; tries++) {
      const int n = static_cast<int>(::send(s, reinterpret_cast<const char*>(pending.data() + off), static_cast<int>(pending.size() - off), kSendFlags));
      if (n <= 0) continue;
      off += static_cast<std::size_t>(n);
      tries = 0;
    }
#ifdef _WIN32
    ::shutdown(s, SD_SEND);
#else
    ::shutdown(s, SHUT_WR);
#endif
    char buf[4096];
    for (int i = 0; i < 6; i++)
      if (::recv(s, buf, sizeof(buf), 0) == 0) break;
    MCW_CLOSESOCK(s);
  }).detach();
}

void setNonBlocking(SockT s) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_NOSIGPIPE
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

class TcpTransport final : public Transport {
 public:
  /// Conexión ya abierta (la acepta un Listener).
  explicit TcpTransport(SockT s) : sock_(s), accepted_(true) {
    setNonBlocking(s);
    status_ = Status::Open;
  }
  /// Conectar en segundo plano.
  TcpTransport(std::string host, int port) {
    state_ = std::make_shared<Shared>();
    std::thread([st = state_, host = std::move(host), port] {
      addrinfo hints{};
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      addrinfo* res = nullptr;
      if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        std::lock_guard lock(st->mutex);
        st->error = "No se encuentra el servidor " + host;
        st->done = true;
        return;
      }
      SockT s = kBadSock;
      for (addrinfo* a = res; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kBadSock) continue;
        if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
        MCW_CLOSESOCK(s);
        s = kBadSock;
      }
      freeaddrinfo(res);
      std::lock_guard lock(st->mutex);
      if (s == kBadSock) st->error = "No se pudo conectar a " + host + ":" + std::to_string(port) + " (¿está encendido?)";
      st->sock = s;
      st->done = true;
    }).detach();
  }
  ~TcpTransport() override { close(); }

  Status status() const override { return status_; }
  std::string error() const override { return error_; }
  void send(std::span<const u8> data) override { out_.insert(out_.end(), data.begin(), data.end()); }

  std::vector<u8> poll() override {
    std::vector<u8> in;
    if (status_ == Status::Connecting) {
      std::lock_guard lock(state_->mutex);
      if (!state_->done) return in;
      if (state_->sock == kBadSock) {
        error_ = state_->error;
        status_ = Status::Closed;
        return in;
      }
      sock_ = state_->sock;
      state_->sock = kBadSock;
      setNonBlocking(sock_);
      status_ = Status::Open;
    }
    if (status_ != Status::Open) return in;
    // Enviar lo pendiente
    while (!out_.empty()) {
      const int n = static_cast<int>(::send(sock_, reinterpret_cast<const char*>(out_.data()), static_cast<int>(std::min<std::size_t>(out_.size(), 1 << 20)), kSendFlags));
      if (n > 0) {
        out_.erase(out_.begin(), out_.begin() + n);
        continue;
      }
      if (n < 0 && MCW_WOULDBLOCK) break;
      fail("Se ha perdido la conexión");
      return in;
    }
    // Leer lo que haya
    u8 buf[65536];
    for (int guard = 0; guard < 64; guard++) {
      const int n = static_cast<int>(::recv(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0));
      if (n > 0) {
        in.insert(in.end(), buf, buf + n);
        continue;
      }
      if (n < 0 && MCW_WOULDBLOCK) break;
      if (n == 0) fail(accepted_ ? "El jugador ha cerrado la conexión" : "El servidor ha cerrado la conexión", false);
      else fail("Se ha perdido la conexión");
      break;
    }
    return in;
  }

  void close() override {
    if (sock_ != kBadSock) {
      // Lo que quede (p. ej. el paquete de desconexión) se manda antes de cerrar
      closeGracefully(sock_, std::move(out_));
      out_.clear();
      sock_ = kBadSock;
    }
    if (status_ != Status::Closed && error_.empty()) error_ = "Desconectado";
    status_ = Status::Closed;
  }

 private:
  void fail(std::string why, bool systemError = true) {
    if (systemError) log::warn("red: {} ({})", why, lastSocketError());
    error_ = std::move(why);
    if (sock_ != kBadSock) MCW_CLOSESOCK(sock_);
    sock_ = kBadSock;
    status_ = Status::Closed;
  }
  struct Shared {
    std::mutex mutex;
    bool done = false;
    SockT sock = kBadSock;
    std::string error;
  };
  std::shared_ptr<Shared> state_;
  SockT sock_ = kBadSock;
  bool accepted_ = false;  // lado del servidor (conexión aceptada por un Listener)
  Status status_ = Status::Connecting;
  std::string error_;
  std::vector<u8> out_;
};

class TcpListener final : public Listener {
 public:
  TcpListener(SockT s, int port) : sock_(s), port_(port) {}
  ~TcpListener() override { MCW_CLOSESOCK(sock_); }
  std::vector<std::unique_ptr<Transport>> accept() override {
    std::vector<std::unique_ptr<Transport>> out;
    for (int guard = 0; guard < 16; guard++) {
      const SockT c = ::accept(sock_, nullptr, nullptr);
      if (c == kBadSock) break;
      out.push_back(std::make_unique<TcpTransport>(c));
    }
    return out;
  }
  int port() const override { return port_; }

 private:
  SockT sock_;
  int port_;
};

}  // namespace

std::unique_ptr<Transport> connectTcp(const std::string& host, int port) {
  initSockets();
  return std::make_unique<TcpTransport>(host, port);
}

bool tcpAvailable() { return true; }

std::unique_ptr<Listener> listenTcp(int port, std::string* error) {
  initSockets();
  const SockT s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kBadSock) {
    if (error) *error = "No se pudo crear el socket";
    return nullptr;
  }
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<u16>(port));
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, 16) != 0) {
    if (error) *error = "El puerto " + std::to_string(port) + " está ocupado";
    MCW_CLOSESOCK(s);
    return nullptr;
  }
  socklen_t len = sizeof(addr);
  getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  return std::make_unique<TcpListener>(s, ntohs(addr.sin_port));
}

// --- LAN ---

LanBroadcaster::LanBroadcaster(std::string motd, int port) {
  initSockets();
  message_ = "[MOTD]" + motd + "[/MOTD][AD]" + std::to_string(port) + "[/AD]";
  const SockT s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kBadSock) return;
  const unsigned char ttl = 1;
  setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
  sock_ = static_cast<intptr_t>(s);
}

LanBroadcaster::~LanBroadcaster() {
  if (sock_ != -1) MCW_CLOSESOCK(static_cast<SockT>(sock_));
}

void LanBroadcaster::tick(double now) {
  if (sock_ == -1 || now - last_ < 1.5) return;
  last_ = now;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(4445);
  inet_pton(AF_INET, "224.0.2.60", &addr.sin_addr);
  ::sendto(static_cast<SockT>(sock_), message_.data(), static_cast<int>(message_.size()), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

LanListener::LanListener() {
  initSockets();
  const SockT s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kBadSock) return;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_REUSEPORT
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(4445);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    MCW_CLOSESOCK(s);
    return;
  }
  ip_mreq mreq{};
  inet_pton(AF_INET, "224.0.2.60", &mreq.imr_multiaddr);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq));
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  sock_ = static_cast<intptr_t>(s);
}

LanListener::~LanListener() {
  if (sock_ != -1) MCW_CLOSESOCK(static_cast<SockT>(sock_));
}

void LanListener::poll(double now) {
  std::erase_if(games_, [&](const LanGame& g) { return now - g.seen > 5.0; });
  if (sock_ == -1) return;
  char buf[1024];
  for (int guard = 0; guard < 32; guard++) {
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    const int n = static_cast<int>(::recvfrom(static_cast<SockT>(sock_), buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &len));
    if (n <= 0) break;
    const std::string msg(buf, static_cast<std::size_t>(n));
    auto between = [&](const std::string& a, const std::string& b) -> std::string {
      const auto i = msg.find(a), j = msg.find(b);
      return (i == std::string::npos || j == std::string::npos || j < i) ? std::string() : msg.substr(i + a.size(), j - i - a.size());
    };
    const std::string motd = between("[MOTD]", "[/MOTD]"), port = between("[AD]", "[/AD]");
    if (port.empty()) continue;
    char ip[64] = {};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    const std::string address = std::string(ip) + ":" + port;
    bool found = false;
    for (LanGame& g : games_)
      if (g.address == address) {
        g.motd = motd;
        g.seen = now;
        found = true;
      }
    if (!found) games_.push_back({motd, address, now});
  }
}

std::vector<std::string> localAddresses() {
  std::vector<std::string> out;
#ifdef _WIN32
  initSockets();
  char name[256];
  if (gethostname(name, sizeof(name)) == 0) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
      for (addrinfo* a = res; a; a = a->ai_next) {
        char ip[64];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr, ip, sizeof(ip));
        if (std::string(ip).rfind("127.", 0) != 0) out.push_back(ip);
      }
      freeaddrinfo(res);
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) == 0) {
    for (ifaddrs* a = list; a; a = a->ifa_next) {
      if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
      char ip[64];
      inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, ip, sizeof(ip));
      if (std::string(ip).rfind("127.", 0) != 0) out.push_back(ip);
    }
    freeifaddrs(list);
  }
#endif
  return out;
}

std::unique_ptr<Transport> connectViaProxy(const std::string&, const std::string&, int) { return nullptr; }

#else  // __EMSCRIPTEN__

namespace {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const std::string& url) {
    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes(&attrs);
    attrs.url = url.c_str();
    attrs.createOnMainThread = true;
    ws_ = emscripten_websocket_new(&attrs);
    if (ws_ <= 0) {
      status_ = Status::Closed;
      error_ = "El navegador no puede abrir " + url;
      return;
    }
    emscripten_websocket_set_onopen_callback(ws_, this, [](int, const EmscriptenWebSocketOpenEvent*, void* self) -> EM_BOOL {
      auto* t = static_cast<WebSocketTransport*>(self);
      t->status_ = Status::Open;
      if (!t->out_.empty()) {
        emscripten_websocket_send_binary(t->ws_, t->out_.data(), static_cast<uint32_t>(t->out_.size()));
        t->out_.clear();
      }
      return EM_TRUE;
    });
    emscripten_websocket_set_onmessage_callback(ws_, this, [](int, const EmscriptenWebSocketMessageEvent* e, void* self) -> EM_BOOL {
      auto* t = static_cast<WebSocketTransport*>(self);
      if (!e->isText) t->in_.insert(t->in_.end(), e->data, e->data + e->numBytes);
      return EM_TRUE;
    });
    emscripten_websocket_set_onerror_callback(ws_, this, [](int, const EmscriptenWebSocketErrorEvent*, void* self) -> EM_BOOL {
      auto* t = static_cast<WebSocketTransport*>(self);
      if (t->error_.empty())
        t->error_ = t->status_ == Status::Connecting ? "No se pudo conectar con el proxy (¿está abierto mcweb-wsproxy?)" : "Error de conexión";
      t->status_ = Status::Closed;
      return EM_TRUE;
    });
    emscripten_websocket_set_onclose_callback(ws_, this, [](int, const EmscriptenWebSocketCloseEvent* e, void* self) -> EM_BOOL {
      auto* t = static_cast<WebSocketTransport*>(self);
      if (t->error_.empty()) t->error_ = e->reason[0] ? e->reason : "El servidor ha cerrado la conexión";
      t->status_ = Status::Closed;
      return EM_TRUE;
    });
  }
  ~WebSocketTransport() override {
    if (ws_ > 0) {
      emscripten_websocket_close(ws_, 1000, "");
      emscripten_websocket_delete(ws_);
    }
  }
  Status status() const override { return status_; }
  std::string error() const override { return error_; }
  void send(std::span<const u8> data) override {
    if (status_ == Status::Open) emscripten_websocket_send_binary(ws_, const_cast<u8*>(data.data()), static_cast<uint32_t>(data.size()));
    else out_.insert(out_.end(), data.begin(), data.end());
  }
  std::vector<u8> poll() override { return std::exchange(in_, {}); }
  void close() override {
    if (ws_ > 0 && status_ != Status::Closed) emscripten_websocket_close(ws_, 1000, "");
    status_ = Status::Closed;
  }

 private:
  EMSCRIPTEN_WEBSOCKET_T ws_ = 0;
  Status status_ = Status::Connecting;
  std::string error_;
  std::vector<u8> in_, out_;
};

}  // namespace

std::unique_ptr<Transport> connectTcp(const std::string&, int) { return nullptr; }
bool tcpAvailable() { return false; }
std::unique_ptr<Transport> connectViaProxy(const std::string& proxyUrl, const std::string& host, int port) {
  std::string url = proxyUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  return std::make_unique<WebSocketTransport>(url + "/" + host + ":" + std::to_string(port));
}
std::unique_ptr<Listener> listenTcp(int, std::string* error) {
  if (error) *error = "En el navegador no se puede hostear: usa la versión de escritorio";
  return nullptr;
}
LanBroadcaster::LanBroadcaster(std::string, int) {}
LanBroadcaster::~LanBroadcaster() = default;
void LanBroadcaster::tick(double) {}
LanListener::LanListener() = default;
LanListener::~LanListener() = default;
void LanListener::poll(double) {}
std::vector<std::string> localAddresses() { return {}; }

#endif

}  // namespace mcw::net
