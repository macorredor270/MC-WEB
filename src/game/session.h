#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/random.h"
#include "game/menu.h"
#include "game/player.h"

namespace mcw {

class World;

/// Acceso al mundo desde la lógica de juego. El cliente lo implementa para que cada cambio
/// actualice la luz y vuelva a mallar lo necesario.
class WorldAccess {
 public:
  virtual ~WorldAccess() = default;
  virtual World& world() = 0;
  virtual void setBlock(int x, int y, int z, BlockState s) = 0;
};

struct TickInput {
  MoveInput move;
  bool jumpPressed = false;
  float yaw = 0, pitch = 0;
  std::optional<glm::dvec3> aimDir;  // p. ej. hacia donde se toca la pantalla; si no, hacia donde se mira
  bool attack = false, attackPressed = false;
  bool use = false, usePressed = false;
  int selectSlot = -1;
  bool drop = false, dropStack = false;
};

struct ItemEntity {
  ItemStack stack;
  glm::dvec3 pos{0}, prevPos{0}, motion{0};
  int age = 0, pickupDelay = 10;
  bool onGround = false;
  float bobOffset = 0;
};

struct BreakState {
  glm::ivec3 pos{0};
  float progress = 0;  // 0..1
};

struct SessionEvent {
  enum class Type { BlockBroken, BlockPlaced, ItemPickedUp, PlayerHurt, PlayerDied } type;
  glm::ivec3 pos{0};
  BlockState state = 0;
};

/// Partida en marcha: jugador, ítems en el suelo, hornos y las reglas para romper/colocar/usar.
/// No sabe nada de gráficos; el cliente le pasa la entrada y lee el estado para dibujar.
class GameSession {
 public:
  GameSession(WorldAccess& access, u64 seed);

  Player& player() { return player_; }
  const Player& player() const { return player_; }
  const std::vector<ItemEntity>& items() const { return items_; }
  const std::optional<RayHit>& target() const { return target_; }
  std::optional<BreakState> breaking() const;
  float eatProgress() const { return eatTicks_ > 0 ? eatTicks_ / 32.0f : 0.0f; }
  double reach() const { return player_.creative() ? 5.0 : 4.5; }

  void setMode(GameMode m);
  void setSpawn(const glm::dvec3& p) { spawn_ = p; }
  void respawn();
  void tick(const TickInput& in);

  Menu* menu() { return menu_.get(); }
  void openInventory();
  void closeMenu();
  void menuClickOutside(int button);

  std::vector<SessionEvent> takeEvents() { return std::exchange(events_, {}); }
  /// Suelta un ítem delante del jugador (tecla Q o clic fuera del inventario).
  void throwItem(const ItemStack& s);

 private:
  void updateTarget(const TickInput& in);
  void handleAttack(const TickInput& in);
  void handleUse(const TickInput& in);
  void breakBlock(const glm::ivec3& p, bool byPlayer);
  void setAndUpdate(int x, int y, int z, BlockState s);
  void neighborUpdates(const glm::ivec3& p);
  void spawnItem(const glm::dvec3& at, const ItemStack& s, const glm::dvec3& motion, int pickupDelay);
  void tickItems();
  void tickFurnaces();
  void damageTool(int amount);

  WorldAccess& access_;
  Player player_;
  Random rng_;
  glm::dvec3 spawn_{0.5, 80, 0.5};
  std::vector<ItemEntity> items_;
  std::map<std::tuple<int, int, int>, FurnaceState> furnaces_;
  std::unique_ptr<Menu> menu_;
  std::optional<RayHit> target_;
  std::optional<glm::ivec3> breakPos_;
  float breakProgress_ = 0;
  int breakDelay_ = 0, useDelay_ = 0, eatTicks_ = 0;
  std::vector<SessionEvent> events_;
};

}  // namespace mcw
