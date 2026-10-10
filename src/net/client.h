#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/protocol.h"
#include "net/socket.h"

namespace mcw::net {

/// Lo que pasa en el servidor, ya traducido a nuestros tipos.
struct ClientEvent {
  enum class Type {
    Joined, Disconnected, Chat, Chunk, UnloadChunk, BlockChange, PlayerPosition, Time, Health, Respawn,
    SpawnPlayer, SpawnMob, SpawnObject, EntityMove, EntityTeleport, EntityLook, EntityHeadLook, EntityVelocity,
    DestroyEntities, EntityMetadata, EntityStatus, EntityAnimation, EntityEquipment, CollectItem,
    SetSlot, WindowItems, OpenWindow, CloseWindow, HeldItem, GameState, Abilities, SpawnPosition,
    PlayerListAdd, PlayerListRemove, Sound, Explosion, Experience, SpawnXpOrb, WindowProperty,
    PlayerSkin  // skin de otro jugador de MC-WEB: uuid, flag = brazos finos, data = PNG
  } type;
  i32 eid = 0, a = 0, b = 0, c = 0;   // según el tipo (modo, ventana, casilla...)
  double x = 0, y = 0, z = 0;
  float yaw = 0, pitch = 0, f = 0;    // yaw/pitch en radianes con nuestra convención
  bool onGround = false, flag = false;
  glm::ivec3 pos{0};
  BlockState state = 0;
  std::string text, uuid;
  std::shared_ptr<Chunk> chunk;
  std::vector<i32> ids;
  std::vector<u8> data;
  std::vector<std::pair<glm::ivec3, BlockState>> blocks;
  ItemStack item;
  std::vector<ItemStack> items;
  Metadata meta;
};

/// Cliente del protocolo 1.8 (servidores en modo offline, sin cifrado).
class Client {
 public:
  Client(std::unique_ptr<Transport> t, std::string host, int port, std::string name);
  /// Llamar cada frame/tick: lee paquetes, contesta keep-alive y rellena los eventos.
  void poll();
  std::vector<ClientEvent> takeEvents() { return std::exchange(events_, {}); }
  bool playing() const { return state_ == State::Play; }
  bool closed() const { return closed_; }
  const std::string& error() const { return error_; }
  const std::string& name() const { return name_; }
  const std::string& uuid() const { return uuid_; }
  i32 entityId() const { return entityId_; }
  bool skyLight() const { return dimension_ == 0; }

  // --- Lo que manda el jugador (yaw/pitch en radianes, nuestra convención) ---
  void sendPosition(const glm::dvec3& feet, float yaw, float pitch, bool onGround);
  void sendChat(const std::string& text);
  /// status: 0 empezar a picar, 1 cancelar, 2 terminar, 3 soltar pila, 4 soltar uno, 5 usar (comer, arco)
  void sendDig(int status, const glm::ivec3& pos, int face);
  /// Clic derecho sobre un bloque (face -1 = usar el objeto en el aire).
  void sendPlace(const glm::ivec3& pos, int face, const ItemStack& held, const glm::vec3& cursor);
  void sendHeldItem(int slot);
  void sendSwing();
  void sendUseEntity(i32 target, bool attack);
  /// action: 0 agacharse, 1 levantarse, 3 empezar a correr, 4 dejar de correr
  void sendEntityAction(int action);
  void sendClickWindow(int window, int slot, int button, int mode, const ItemStack& clicked);
  void sendCloseWindow(int window);
  /// Pulsar una de las tres opciones de la mesa de encantamientos (0 a 2).
  void sendEnchantItem(int window, int button);
  /// Yunque: el nombre que se está escribiendo (mensaje de plugin MC|ItemName).
  void sendItemName(const std::string& name);
  void sendCreativeSlot(int slot, const ItemStack& item);
  void sendRespawn();
  /// Distancia de visión en chunks que se pide al servidor (Client Settings).
  void setViewDistance(int chunks);
  /// Capas de la skin que se ven (SkinPart; 0x7F = todas), también en Client Settings.
  void setSkinParts(u8 parts);
  /// Tu skin (PNG de 64x64) para los demás: solo se manda a servidores de MC-WEB (los que lo dicen
  /// en MC|Brand), así un servidor de 1.8 normal no recibe nada que no conozca.
  void setSkin(std::vector<u8> png, bool slim);
  void disconnect();

 private:
  void handle(const Packet& p);
  void sendSettings();
  void sendSkin();
  void handleLogin(const Packet& p);
  void handlePlay(const Packet& p);
  void send(i32 id, const BufferWriter& w);
  void fail(std::string why);

  std::unique_ptr<Transport> transport_;
  PacketCodec codec_;
  State state_ = State::Login;
  std::string host_, name_, uuid_, error_;
  int port_ = 25565;
  bool closed_ = false;
  i32 entityId_ = 0;
  int dimension_ = 0;
  i16 actionCounter_ = 1;
  int viewDistance_ = 8;
  u8 skinParts_ = 0x7F;
  std::vector<u8> skinPng_;
  bool skinSlim_ = false, mcwebServer_ = false;
  std::vector<ClientEvent> events_;
};

/// Información de un servidor para la lista de Multijugador (paquete de estado).
struct ServerStatus {
  std::string motd, version;
  int protocol = 0, online = 0, max = 0;
  int pingMs = -1;
  std::string faviconPng;  // bytes del PNG (base64 ya decodificado)
  std::vector<std::string> sample;  // nombres de algunos jugadores conectados
};

class StatusPinger {
 public:
  StatusPinger(std::unique_ptr<Transport> t, std::string host, int port);
  /// true cuando ha terminado (bien o mal).
  bool poll(double now);
  const ServerStatus& status() const { return status_; }
  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

 private:
  std::unique_ptr<Transport> transport_;
  PacketCodec codec_;
  std::string host_;
  int port_;
  bool sent_ = false, done_ = false, ok_ = false, gotInfo_ = false;
  double start_ = -1, pingSent_ = 0;
  ServerStatus status_;
  std::string error_;
};

/// Decodifica base64 (favicon de los servidores).
std::string base64Decode(std::string_view in);
std::string base64Encode(std::string_view in);

}  // namespace mcw::net
