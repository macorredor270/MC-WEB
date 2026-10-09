#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <set>
#include <vector>

#include "core/random.h"
#include "game/achievements.h"
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
  int randomTickSpeed = 3;  // regla randomTickSpeed
};

/// Reglas de la partida (Ajustes > Juego).
struct GameRules {
  int difficulty = 2;  // 0 pacífico, 1 fácil, 2 normal, 3 difícil
  bool keepInventory = false;
  bool mobSpawning = true;
};

struct ItemEntity {
  u32 id = 0;  // para el multijugador
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
    MobHurt, MobDied, MobCrit, Explosion, ArrowShot, ArrowHit, CreeperFuse, SheepSheared,
    DoorOpened, DoorClosed, Click, Ate, Slept, Achievement,
    BowShot,    // el jugador suelta el arco: `value` = potencia (0..100)
    LoveHearts  // corazones sobre un animal en modo amor (`value` = cuántos)
  } type;
  glm::ivec3 pos{0};
  BlockState state = 0;
  glm::dvec3 where{0};  // posición exacta (criaturas, explosiones)
  MobType mob = MobType::Pig;
  int value = 0;        // logro conseguido (Achievement)
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
  /// Ticks que lleva tensado el arco (0 = no se está tensando).
  int bowTicks() const { return bowTicks_; }
  /// Cuántas flechas lleva el jugador.
  int arrowCount() const;
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
  /// Abrir una ventana que ha pedido el servidor (cofre, mesa, horno).
  void openMenu(std::unique_ptr<Menu> m) { menu_ = std::move(m); }
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
  std::vector<std::pair<glm::ivec3, ChestState>> chestsInChunk(int cx, int cz, bool take);
  void setChest(const glm::ivec3& p, const ChestState& c) { chests_[{p.x, p.y, p.z}] = c; }
  u32 addMob(Mob m);
  u32 addItem(ItemEntity e) {
    e.id = nextItemId_++;
    items_.push_back(std::move(e));
    return items_.back().id;
  }
  // --- Para los jugadores invitados (servidor integrado) ---
  /// Romper un bloque como otro jugador (con o sin lo que suelta).
  void breakBlockAt(const glm::ivec3& p, bool drops) {
    suppressDrops_ = !drops;
    breakBlock(p, false);
    suppressDrops_ = false;
  }
  /// Quitar un objeto del suelo (lo ha recogido otro jugador).
  std::optional<ItemStack> takeItem(u32 id);
  /// Soltar un objeto en el mundo.
  void dropItem(const glm::dvec3& at, const ItemStack& s, const glm::dvec3& motion) { spawnItem(at, s, motion, 40); }
  /// Golpear a una criatura (otro jugador).
  void hurtMobById(u32 id, float amount, const glm::dvec3& from);
  /// Casillas del cofre en esa posición (se crea vacío si no existe).
  ItemStack* chestItems(const glm::ivec3& p) { return chests_[{p.x, p.y, p.z}].items.data(); }
  FurnaceState* furnaceAt(const glm::ivec3& p) { return &furnaces_[{p.x, p.y, p.z}]; }
  void setFurnace(const glm::ivec3& p, const FurnaceState& f) { furnaces_[{p.x, p.y, p.z}] = f; }
  /// Quita todo (criaturas, objetos, hornos): al cambiar de mundo.
  void clearWorldState();
  /// Muerte inmediata (comando /kill o el vacío): suelta el inventario y avisa.
  void killPlayer();
  WorldAccess& access() { return access_; }
  const glm::dvec3& spawn() const { return spawn_; }

  /// Logros y estadísticas del jugador en este mundo.
  Achievements& achievements() { return achievements_; }
  /// Da un logro (si su logro previo ya está); avisa con un evento Achievement si es nuevo.
  void award(Ach a);

  /// Jugando en un servidor: las acciones se mandan (y el mundo, las criaturas y los objetos los
  /// decide el servidor). Sin ganchos, la partida es local.
  struct RemoteHooks {
    std::function<void(int status, const glm::ivec3& pos, int face)> dig;  // 0 empezar, 2 terminar
    std::function<void(const glm::ivec3& pos, int face, const glm::vec3& cursor, const ItemStack& held)> use;
    std::function<void(u32 mobId, bool attack)> useEntity;
    std::function<void(bool wholeStack)> drop;
    std::function<void()> swing;
  };
  void setRemote(std::shared_ptr<RemoteHooks> hooks) { remote_ = std::move(hooks); }
  bool remote() const { return remote_ != nullptr; }
  /// Criatura por id (para moverla con lo que dice el servidor).
  Mob* mobById(u32 id) {
    for (Mob& m : mobs_)
      if (m.id == id) return &m;
    return nullptr;
  }
  ItemEntity* itemById(u32 id) {
    for (ItemEntity& e : items_)
      if (e.id == id) return &e;
    return nullptr;
  }
  void removeMob(u32 id) { std::erase_if(mobs_, [id](const Mob& m) { return m.id == id; }); }
  void removeItem(u32 id) { std::erase_if(items_, [id](const ItemEntity& e) { return e.id == id; }); }

  /// Aviso de cada bloque que cambia la partida (el servidor lo manda a los demás jugadores).
  void setBlockListener(std::function<void(const glm::ivec3&, BlockState)> fn) { blockListener_ = std::move(fn); }
  /// Cambiar un bloque sin avisar a los vecinos (lo usan la redstone, el servidor, etc.).
  void setWorldBlock(int x, int y, int z, BlockState s) {
    access_.setBlock(x, y, z, s);
    if (blockListener_) blockListener_({x, y, z}, s);
  }

  /// Poner un bloque avisando a los vecinos (comandos, tests).
  void placeBlock(const glm::ivec3& p, BlockState s) { setAndUpdate(p.x, p.y, p.z, s); }
  /// Usar el bloque de `p` como con el clic derecho (tests, comandos).
  bool interact(const glm::ivec3& p) { return useBlock(p); }

  /// Suelta un ítem delante del jugador (tecla Q o clic fuera del inventario).
  void throwItem(const ItemStack& s);

  /// Si el jugador ha dormido en una cama: la hora a la que hay que saltar (el cliente la aplica).
  std::optional<double> takeSleepRequest() { return std::exchange(sleepRequest_, std::nullopt); }

  /// Los demás jugadores de la partida (los invitados, que son del servidor): las criaturas los
  /// persiguen y les atacan, y el mundo (monstruos, cultivos) se mueve también a su alrededor.
  void setOtherPlayers(std::vector<Player*> players) { others_ = std::move(players); }
  /// Servidor dedicado: no hay jugador local (el de la sesión no cuenta para nada).
  void setLocalPlayerActive(bool active) { localActive_ = active; }
  bool localPlayerActive() const { return localActive_; }
  /// Los jugadores que cuentan: el local (si lo hay) y los demás.
  std::vector<Player*> activePlayers();

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
  /// Usar un bloque (abrir puertas, palancas, botones, tarta, cama...). true si se ha usado.
  bool useBlock(const glm::ivec3& p);
  void tickScheduled();
  void trackAchievements();
  void onPickup(const ItemStack& s);
  /// Crecimiento de cultivos y plantas alrededor del jugador (los "random ticks" de 1.8).
  void randomTicks();
  /// Lo que se mueve solo: objetos, hornos, redstone, cultivos, criaturas, flechas.
  void tickWorld(const TickInput& in);

  // Criaturas (mobs.cpp)
  void tickMobs();
  void mobAI(Mob& m);
  void moveMob(Mob& m);
  void walkTowards(Mob& m, const glm::dvec3& target, float speedMul);
  void wander(Mob& m, int chance, float speedMul);
  bool canSeePlayer(const Mob& m, const Player& p) const;
  /// El jugador vivo más cercano (nullptr si no hay ninguno). `attackable`: sin contar creativos.
  Player* nearestPlayer(const glm::dvec3& at, bool attackable);
  double nearestPlayerDistance(const glm::dvec3& at);
  void attackMob(Mob& m);
  void hurtMob(Mob& m, float amount, const glm::dvec3& from, float knockback, bool byPlayer);
  void mobDrops(const Mob& m);
  void damagePlayer(Player& p, float amount, const glm::dvec3& from, float knockback);
  void spawnHostiles();
  void shootArrow(const Mob& from, const Player& target);
  void tickArrows();
  void arrowHitsMob(Arrow& a, Mob& m, double speed, const glm::dvec3& dir);
  // Cría de animales
  bool feedAnimal(Mob& m);
  Mob* findMate(const Mob& m);
  void breedMobs(Mob& a, Mob& b);
  void loveHearts(const Mob& m, int count);
  void shootBow(int ticks);
  bool takeArrow();
  void pushEntities();
  std::optional<std::pair<std::size_t, double>> raycastMobs(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist) const;

  WorldAccess& access_;
  GameRules rules_;
  Player player_;
  std::vector<Player*> others_;
  bool localActive_ = true;
  Random rng_;
  glm::dvec3 spawn_{0.5, 80, 0.5};
  std::vector<ItemEntity> items_;
  std::vector<Mob> mobs_;
  std::vector<Mob> newMobs_;  // crías de este tick (se añaden al acabar, para no mover `mobs_` en plena IA)
  std::vector<Arrow> arrows_;
  std::optional<u32> targetMob_;
  u32 nextMobId_ = 1;
  u32 nextItemId_ = 1;
  bool suppressDrops_ = false;
  u64 seed_ = 0;
  double worldTime_ = 1000;
  int randomTickSpeed_ = 3;
  std::function<void(const glm::ivec3&, BlockState)> blockListener_;
  std::shared_ptr<RemoteHooks> remote_;
  Achievements achievements_;
  Random tickRng_{0x5EED};  // aparte, para no cambiar la secuencia de las criaturas
  std::optional<double> sleepRequest_;
  int hostileSpawnTimer_ = 0, touchAttackTimer_ = 0;
  std::map<std::tuple<int, int, int>, FurnaceState> furnaces_;
  std::map<std::tuple<int, int, int>, ChestState> chests_;
  // Redstone (redstone.cpp)
  enum class TickKind : u8 { ButtonRelease, Torch, Repeater, Comparator, Lamp, Tnt };
  struct Scheduled {
    glm::ivec3 pos;
    int ticks;
    TickKind kind = TickKind::ButtonRelease;
  };
  /// Potencia que el bloque en `from` da a su vecino en la dirección `dir` (0..15).
  /// `strongOnly`: solo la fuerte (la que atraviesa bloques sólidos). `forWire`: la pide el polvo
  /// (el polvo no recibe de bloques cargados solo débilmente).
  int emittedPower(const glm::ivec3& from, const glm::ivec3& dir, bool strongOnly);
  /// Carga de un bloque sólido (conductor) por lo que tiene alrededor.
  int conductorPower(const glm::ivec3& c, bool strongOnly);
  /// Potencia que recibe un mecanismo en `p` (lámpara, puerta, pistón...). `skip` = dirección que no cuenta.
  int powerInto(const glm::ivec3& p, int skip = -1);
  /// Algo ha cambiado en `p`: recalcular redstone alrededor.
  void redstoneNotify(const glm::ivec3& p);
  void redstoneUpdate(const glm::ivec3& p, std::vector<glm::ivec3>& changed);
  void updateWireNetwork(const glm::ivec3& start, std::vector<glm::ivec3>& changed);
  bool pistonMove(const glm::ivec3& p, bool extend);
  void schedule(const glm::ivec3& p, int ticks, TickKind kind);
  void tickPlates();
  std::vector<Scheduled> scheduled_;
  std::set<std::tuple<int, int, int>> pressedPlates_, poweredTrapdoors_;
  bool inRedstone_ = false;
  std::vector<glm::ivec3> pendingRedstone_;
  std::unique_ptr<Menu> menu_;
  std::optional<RayHit> target_;
  std::optional<glm::ivec3> breakPos_;
  float breakProgress_ = 0;
  int breakDelay_ = 0, useDelay_ = 0, eatTicks_ = 0, bowTicks_ = 0;
  std::vector<SessionEvent> events_;
};

}  // namespace mcw
