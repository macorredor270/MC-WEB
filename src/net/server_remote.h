#pragma once
// Lo que el servidor guarda de cada jugador conectado. Es interno: lo usan server.cpp y server_commands.cpp.
#include <array>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "net/server.h"

namespace mcw::net {

struct Server::Remote {
  std::unique_ptr<Transport> t;
  PacketCodec codec;
  State state = State::Handshake;
  int protocol = 0;
  std::string name, uuid;
  i32 eid = 0;
  Player player;
  bool joined = false, closed = false;
  std::set<std::pair<int, int>> chunks;
  std::set<i32> tracked;
  std::map<i32, std::array<i32, 5>> lastSent;  // x, y, z, yaw, pitch (en unidades del protocolo)
  std::map<i32, u8> mobFlags;                  // animales: bit 0 = cría, bit 1 = en modo amor (para avisar de los cambios)
  std::map<i32, std::array<ItemStack, 5>> equipSent;  // jugadores: lo último que se le dijo (mano, botas, pantalones, pechera, casco)
  double lastKeepAlive = 0, lastReply = 0;
  i32 keepAliveId = 0;
  std::unique_ptr<Menu> invMenu;  // ventana 0 (inventario)
  std::unique_ptr<Menu> window;   // mesa, cofre u horno abiertos
  int windowId = 0;
  glm::ivec3 windowPos{0};  // dónde está el yunque abierto (para desgastarlo)
  int sentAnvilCost = -1;   // y el coste que se le dijo (propiedad 0 de la ventana)
  std::vector<ItemStack> windowSent;                    // lo último que se le dijo de las casillas de la ventana abierta
  std::array<int, 4> sentFurnace{-1, -1, -1, -1};       // y de las propiedades del horno (llama, llama máxima, progreso, progreso máximo)
  bool sneaking = false, sprinting = false;
  int swingTicks = 0;
  int viewDistance = -1;  // la que pide el cliente (Client Settings); -1 = la del servidor
  std::shared_ptr<const std::vector<u8>> skin;  // la que ha mandado (PNG), si la ha mandado
  bool skinSlim = false;
  u32 skinVersion = 0;
  double lastSkin = -100;  // cuándo mandó la última (para que no inunde a los demás)
  u8 skinParts = 0x7F;     // capas visibles que dice su cliente (Client Settings)
  float sentHealth = -1;  // lo último que se le mandó (Update Health)
  std::array<int, 10> sentEnchant{};  // propiedades de la ventana de la mesa de encantamientos (0-2 costes, 3 semilla, 4-6 pistas, 7-9 niveles)
  int sentXpLevel = -1, sentXpTotal = -1;  // y de experiencia (Set Experience)
  float sentXpProgress = -1;
  int sentFood = -1;
  bool deathHandled = false;
  double lastCreativeDrop = -100;  // cuándo tiró algo desde el inventario creativo (para limitar cuántos)
  GameSession::ActionState act;    // lo que está haciendo (para usar y golpear con las mismas reglas que el anfitrión)
  enum class Use { None, Eat, Bow } use = Use::None;  // lo que mantiene pulsado con el botón derecho
  int useTicks = 0, useSlot = 0;
  glm::dvec3 prevPos{0};
};

}  // namespace mcw::net
