#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/random.h"
#include "game/menu.h"
#include "game/mob.h"
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
  /// Muchos cambios seguidos (explosiones): el cliente puede esperar al final para volver a mallar.
  virtual void beginBatch() {}
  virtual void endBatch() {}
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
  bool fromTouch = false;  // usePressed es un toque en la pantalla: sobre una criatura, la golpea
  double worldTime = 1000;  // hora del día en ticks (para la aparición de monstruos y el sol)
};

/// Reglas de la partida (Ajustes > Juego).
struct GameRules {
  int difficulty = 2;  // 0 pacífico, 1 fácil, 2 normal, 3 difícil
  bool keepInventory = false;
  bool mobSpawning = true;
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
  enum class Type {
    BlockBroken, BlockPlaced, ItemPickedUp, PlayerHurt, PlayerDied,
    MobHurt, MobDied, MobCrit, Explosion, ArrowShot, ArrowHit, CreeperFuse, SheepSheared
  } type;
  glm::ivec3 pos{0};
  BlockState state = 0;
  glm::dvec3 where{0};  // posición exacta (criaturas, explosiones)
  MobType mob = MobType::Pig;
};

/// Partida en marcha: jugador, ítems en el suelo, hornos y las reglas para romper/colocar/usar.
/// No sabe nada de gráficos; el cliente le pasa la entrada y lee el estado para dibujar.
class GameSession {
 public:
  GameSession(WorldAccess& access, u64 seed);

  Player& player() { return player_; }
  const Player& player() const { return player_; }
  const std::vector<ItemEntity>& items() const { return items_; }
  const std::vector<Mob>& mobs() const { return mobs_; }
  const std::vector<Arrow>& arrows() const { return arrows_; }
  /// Criatura a la que se apunta (si está más cerca que el bloque apuntado).
  const Mob* targetedMob() const;
  const std::optional<RayHit>& target() const { return target_; }
  std::optional<BreakState> breaking() const;
  float eatProgress() const { return eatTicks_ > 0 ? eatTicks_ / 32.0f : 0.0f; }
  double reach() const { return player_.creative() ? 5.0 : 4.5; }

  void setMode(GameMode m);
  /// Dificultad y reglas. En pacífico desaparecen los monstruos.
  void setRules(const GameRules& r);
  const GameRules& rules() const { return rules_; }
  void setSpawn(const glm::dvec3& p) { spawn_ = p; }
  void respawn();
  void tick(const TickInput& in);

  Menu* menu() { return menu_.get(); }
  void openInventory();
  void closeMenu();
  void menuClickOutside(int button);

  std::vector<SessionEvent> takeEvents() { return std::exchange(events_, {}); }

  /// Animales al generar un chunk (como en 1.8: a veces un grupo de cerdos, ovejas, gallinas o vacas).
  void populateChunk(int cx, int cz);
  Mob* spawnMob(MobType type, const glm::dvec3& pos);
  /// Explosión (creeper): rompe bloques según su resistencia y hace daño alrededor.
  void explode(const glm::dvec3& center, float power);
  double entityReach() const { return player_.creative() ? 5.0 : 3.0; }
  // --- Guardado: lo que hay en cada chunk ---
  /// Criaturas, objetos y hornos dentro del chunk (cx, cz). `take` los quita de la partida.
  std::vector<Mob> mobsInChunk(int cx, int cz, bool take);
  std::vector<ItemEntity> itemsInChunk(int cx, int cz, bool take);
  std::vector<std::pair<glm::ivec3, FurnaceState>> furnacesInChunk(int cx, int cz, bool take);
  void addMob(Mob m);
  void addItem(ItemEntity e) { items_.push_back(std::move(e)); }
  void setFurnace(const glm::ivec3& p, const FurnaceState& f) { furnaces_[{p.x, p.y, p.z}] = f; }
  /// Quita todo (criaturas, objetos, hornos): al cambiar de mundo.
  void clearWorldState();
  /// Muerte inmediata (comando /kill o el vacío): suelta el inventario y avisa.
  void killPlayer();
  WorldAccess& access() { return access_; }
  const glm::dvec3& spawn() const { return spawn_; }

  /// Suelta un ítem delante del jugador (tecla Q o clic fuera del inventario).
  void throwItem(const ItemStack& s);

 private:
  void onPlayerDeath();
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

  // Criaturas (mobs.cpp)
  void tickMobs();
  void mobAI(Mob& m);
  void moveMob(Mob& m);
  void walkTowards(Mob& m, const glm::dvec3& target, float speedMul);
  void wander(Mob& m, int chance, float speedMul);
  bool canSeePlayer(const Mob& m) const;
  void attackMob(Mob& m);
  void hurtMob(Mob& m, float amount, const glm::dvec3& from, float knockback, bool byPlayer);
  void mobDrops(const Mob& m);
  void damagePlayer(float amount, const glm::dvec3& from, float knockback);
  void spawnHostiles();
  void shootArrow(const Mob& from);
  void tickArrows();
  void pushEntities();
  std::optional<std::pair<std::size_t, double>> raycastMobs(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist) const;

  WorldAccess& access_;
  GameRules rules_;
  Player player_;
  Random rng_;
  glm::dvec3 spawn_{0.5, 80, 0.5};
  std::vector<ItemEntity> items_;
  std::vector<Mob> mobs_;
  std::vector<Arrow> arrows_;
  std::optional<u32> targetMob_;
  u32 nextMobId_ = 1;
  u64 seed_ = 0;
  double worldTime_ = 1000;
  int hostileSpawnTimer_ = 0, touchAttackTimer_ = 0;
  std::map<std::tuple<int, int, int>, FurnaceState> furnaces_;
  std::unique_ptr<Menu> menu_;
  std::optional<RayHit> target_;
  std::optional<glm::ivec3> breakPos_;
  float breakProgress_ = 0;
  int breakDelay_ = 0, useDelay_ = 0, eatTicks_ = 0;
  std::vector<SessionEvent> events_;
};

}  // namespace mcw
