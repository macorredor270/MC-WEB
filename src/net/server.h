#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/menu.h"
#include "game/player.h"
#include "game/session.h"
#include "net/protocol.h"
#include "net/socket.h"

namespace mcw::net {

/// Servidor 1.8 (protocolo 47) sobre una partida: el anfitrión juega en local y los invitados
/// (MC-WEB o Minecraft 1.8 oficial) entran por TCP. Modo offline: sin cuentas ni cifrado.
class Server {
 public:
  struct Config {
    std::string motd = "Partida de MC-WEB";
    int maxPlayers = 8;
    int guestMode = 0;       // 0 supervivencia, 1 creativo, 2 aventura, 3 espectador
    int viewDistance = 6;    // chunks que se mandan alrededor de cada invitado
    int difficulty = 2;
    std::string hostName = "Anfitrion";  // "" = servidor dedicado (sin jugador local)
    /// Carpeta playerdata/ del mundo: cada invitado se guarda en <uuid>.dat (vacía = no se guarda).
    std::filesystem::path playerDataDir;
    /// Antes de dejar entrar a alguien (lista blanca): "" = puede entrar; si no, el motivo.
    std::function<std::string(const std::string& name)> checkLogin;
  };

  /// Un jugador conectado, visto desde fuera (para dibujarlo en el anfitrión).
  struct PlayerView {
    i32 eid;
    std::string name, uuid;
    glm::dvec3 pos, prevPos;
    float yaw, pitch;
    bool sneaking, swinging;
    ItemStack held;
    /// Su skin (PNG) si la ha mandado; `skinVersion` sube cada vez que cambia (así se sabe cuándo
    /// volver a cargarla). nullptr = ninguna: se ve la de serie que toque por su UUID.
    std::shared_ptr<const std::vector<u8>> skin;
    bool skinSlim = false;
    u32 skinVersion = 0;
    u8 skinParts = 0x7F;  // capas de su skin visibles
    std::array<i16, 4> armor{};  // armadura puesta (0 botas .. 3 casco), 0 = nada
  };

  Server(GameSession& session, Config config);
  ~Server();
  /// Empieza a escuchar. `port` 0 = cualquiera libre.
  bool start(int port, std::string* error);
  int port() const { return listener_ ? listener_->port() : 0; }

  /// Un tick de juego (20 por segundo): conexiones, paquetes, entidades, chunks.
  void tick(double now, double worldTime);
  /// Un bloque ha cambiado (llamar desde el aviso de la partida).
  void blockChanged(const glm::ivec3& p, BlockState s);
  /// Mensaje del anfitrión o del servidor para todos.
  void broadcastChat(const std::string& text);
  /// Mensajes que han llegado de los invitados (para mostrarlos en el anfitrión).
  std::vector<std::string> takeChat() { return std::exchange(chatForHost_, {}); }
  /// Chunks que hay que tener cargados (alrededor de cada invitado).
  std::vector<ChunkPos> wantedChunkCenters() const;
  std::vector<PlayerView> players() const;
  int playerCount() const;
  void setMotd(std::string m) { config_.motd = std::move(m); }
  const Config& config() const { return config_; }
  /// Guarda a los invitados conectados (playerdata), p. ej. en el guardado automático.
  void saveAll();
  /// Echa a un jugador por su nombre (false si no está).
  bool kickPlayer(const std::string& name, const std::string& reason);
  /// La skin del anfitrión (PNG de 64x64): se manda a cada invitado que entra.
  void setHostSkin(std::vector<u8> png, bool slim, u8 parts);
  void stop();

 private:
  struct Remote;
  void handle(Remote& r, const Packet& p, double now);
  void handleHandshake(Remote& r, BufferReader& in);
  void handleStatus(Remote& r, const Packet& p);
  void handleLogin(Remote& r, BufferReader& in, double now);
  void handlePlay(Remote& r, const Packet& p, double now);
  void send(Remote& r, i32 id, const BufferWriter& w);
  void sendAll(i32 id, const BufferWriter& w, const Remote* except = nullptr);
  void kick(Remote& r, const std::string& reason);
  void join(Remote& r, double now);
  void sendChunks(Remote& r, int budget);
  void trackEntities(Remote& r);
  void pickUpItems(Remote& r);
  void sendInventory(Remote& r);
  void sendWindow(Remote& r);
  void sendEnchantProps(Remote& r, bool all);
  void digBlock(Remote& r, int status, const glm::ivec3& pos, int face);
  void useOnBlock(Remote& r, const glm::ivec3& pos, int face, const glm::vec3& cursor);
  void clickWindow(Remote& r, int window, int slot, int button, int mode);
  void playerListAdd(Remote& to, i32 eid, const std::string& uuid, const std::string& name, int mode);
  bool loadPlayer(Remote& r);
  void savePlayer(const Remote& r);
  void guestDied(Remote& r);
  void handleSkin(Remote& r, std::span<const u8> data, double now);
  void sendSkin(Remote& to, const std::string& uuid, const std::vector<u8>& png, bool slim);

  GameSession& session_;
  Config config_;
  std::unique_ptr<Listener> listener_;
  std::vector<std::unique_ptr<Remote>> remotes_;
  std::vector<std::string> chatForHost_;
  double worldTime_ = 0, lastTime_ = 0;
  i32 nextEid_ = 2;  // 1 = el anfitrión
  std::string hostUuid_;
  std::shared_ptr<const std::vector<u8>> hostSkin_;
  bool hostSkinSlim_ = false;
  u8 hostParts_ = 0x7F;
};

/// Entidad del protocolo para nuestras criaturas y objetos.
inline constexpr i32 kHostEid = 1;
inline i32 mobEid(u32 id) { return static_cast<i32>(0x10000 + id); }
inline i32 itemEid(u32 id) { return static_cast<i32>(0x400000 + id); }
inline i32 orbEid(u32 id) { return static_cast<i32>(0x800000 + id); }
/// Tipo de criatura de 1.8 (Spawn Mob) de nuestras criaturas, y al revés (-1 si no la tenemos).
int mobNetType(MobType t);
int mobTypeFromNet(int netType);

}  // namespace mcw::net
