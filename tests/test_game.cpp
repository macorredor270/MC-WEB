#include <doctest/doctest.h>

#include <cmath>

#include "core/face.h"
#include "core/hash.h"
#include "data/items.h"
#include "game/crafting.h"
#include "game/menu.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/light.h"
#include "world/world.h"

using namespace mcw;

namespace {

/// Mundo plano de 5x5 chunks: piedra hasta y=60, tierra hasta 63 y hierba en y=63 (suelo a y=64).
struct FlatWorld : WorldAccess {
  World w;
  FlatWorld() {
    ChunkSet mod;
    for (int cz = -2; cz <= 2; cz++)
      for (int cx = -2; cx <= 2; cx++) {
        auto c = std::make_unique<Chunk>(cx, cz);
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            c->setBlock(x, 0, z, makeState(B::bedrock));
            for (int y = 1; y < 60; y++) c->setBlock(x, y, z, makeState(B::stone));
            for (int y = 60; y < 63; y++) c->setBlock(x, y, z, makeState(B::dirt));
            c->setBlock(x, 63, z, makeState(B::grass));
          }
        light::computeInitial(*c);
        w.insert(std::move(c), mod);
      }
  }
  World& world() override { return w; }
  void setBlock(int x, int y, int z, BlockState s) override {
    ChunkSet mod;
    w.setBlock(x, y, z, s, mod);
  }
};

int ticksToBreak(BlockState s, const ItemStack& tool) {
  float p = 0;
  int t = 0;
  while (p < 0.9999f && t < 10000) { p += digProgressPerTick(s, tool, true, false); t++; }
  return t;
}

}  // namespace

TEST_CASE("Física: caer, andar, correr y saltar como en 1.8") {
  FlatWorld fw;
  Player p;
  p.pos = p.prevPos = {0.5, 70, 0.5};
  for (int i = 0; i < 40; i++) p.tickMovement(fw.w, {}, false);
  CHECK(p.onGround);
  CHECK(p.pos.y == doctest::Approx(64.0));

  // Andar: tras acelerar, 4.317 bloques/s
  MoveInput walk;
  walk.forward = 1;
  for (int i = 0; i < 20; i++) p.tickMovement(fw.w, walk, false);
  const double z0 = p.pos.z;
  for (int i = 0; i < 20; i++) p.tickMovement(fw.w, walk, false);
  CHECK(std::abs(p.pos.z - z0) == doctest::Approx(4.317).epsilon(0.01));

  // Correr: 5.612 bloques/s
  walk.sprint = true;
  for (int i = 0; i < 20; i++) p.tickMovement(fw.w, walk, false);
  const double z1 = p.pos.z;
  for (int i = 0; i < 20; i++) p.tickMovement(fw.w, walk, false);
  CHECK(std::abs(p.pos.z - z1) == doctest::Approx(5.612).epsilon(0.01));

  // Saltar: altura máxima ~1.25 bloques
  Player j;
  j.pos = j.prevPos = {0.5, 64, 0.5};
  j.onGround = true;
  MoveInput jump;
  jump.jump = true;
  double maxY = 0;
  j.tickMovement(fw.w, jump, true);
  for (int i = 0; i < 30; i++) {
    j.tickMovement(fw.w, {}, false);
    maxY = std::max(maxY, j.pos.y - 64);
  }
  CHECK(maxY == doctest::Approx(1.2522).epsilon(0.005));
}

TEST_CASE("Colisiones: una pared para, un escalón bajo se sube") {
  FlatWorld fw;
  for (int y = 64; y < 66; y++) fw.setBlock(0, y, -3, makeState(B::stone));
  Player p;
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.onGround = true;
  MoveInput walk;
  walk.forward = 1;  // hacia el norte (-Z)
  for (int i = 0; i < 60; i++) p.tickMovement(fw.w, walk, false);
  CHECK(p.pos.z == doctest::Approx(-1.7).epsilon(0.001));  // la cara sur de la pared está en z=-2
  CHECK(p.pos.y == doctest::Approx(64.0));

  // Una capa de nieve de 4 niveles (media altura) se sube andando
  FlatWorld fw2;
  for (int z = -6; z <= -3; z++) fw2.setBlock(0, 64, z, makeState(B::snow_layer, 4));
  Player q;
  q.pos = q.prevPos = {0.5, 64, 0.5};
  q.onGround = true;
  double maxY = 0;
  for (int i = 0; i < 40; i++) {
    q.tickMovement(fw2.w, walk, false);
    maxY = std::max(maxY, q.pos.y);
  }
  CHECK(maxY == doctest::Approx(64.5));  // subió al escalón de medio bloque
  CHECK(q.pos.z < -6.0);                  // y siguió andando
}

TEST_CASE("Daño por caída: 10 bloques quitan 7 puntos") {
  FlatWorld fw;
  Player p;
  p.pos = p.prevPos = {0.5, 74, 0.5};
  for (int i = 0; i < 60; i++) p.tickMovement(fw.w, {}, false);
  CHECK(p.health == doctest::Approx(13.0f));
  Player c;
  c.mode = GameMode::Creative;
  c.pos = c.prevPos = {0.5, 90, 0.5};
  for (int i = 0; i < 80; i++) c.tickMovement(fw.w, {}, false);
  CHECK(c.health == doctest::Approx(20.0f));
}

TEST_CASE("Tiempos de rotura (minecraft.wiki)") {
  const ItemStack hand;
  CHECK(ticksToBreak(makeState(B::dirt), hand) == 15);                       // 0,75 s
  CHECK(ticksToBreak(makeState(B::stone), hand) == 150);                     // 7,5 s
  CHECK(ticksToBreak(makeState(B::stone), ItemStack(ItemId::wooden_pickaxe)) == 23);   // 1,15 s
  CHECK(ticksToBreak(makeState(B::stone), ItemStack(ItemId::diamond_pickaxe)) == 6);   // 0,3 s
  CHECK(ticksToBreak(makeState(B::log), ItemStack(ItemId::stone_axe)) == 15);          // 0,75 s
  CHECK(ticksToBreak(makeState(B::tallgrass, 1), hand) == 1);
  CHECK(digProgressPerTick(makeState(B::bedrock), hand, true, false) == 0.0f);
}

TEST_CASE("Drops") {
  Random rng(1);
  CHECK(blockDrops(makeState(B::stone), ItemStack(), rng).empty());
  auto d = blockDrops(makeState(B::stone), ItemStack(ItemId::wooden_pickaxe), rng);
  REQUIRE(d.size() == 1);
  CHECK(d[0] == ItemStack(B::cobblestone, 1, 0));
  d = blockDrops(makeState(B::log, 2 | 4), ItemStack(), rng);  // abedul en eje X
  REQUIRE(d.size() == 1);
  CHECK(d[0] == ItemStack(B::log, 1, 2));
  d = blockDrops(makeState(B::grass), ItemStack(), rng);
  REQUIRE(d.size() == 1);
  CHECK(d[0] == ItemStack(B::dirt, 1, 0));
  d = blockDrops(makeState(B::coal_ore), ItemStack(ItemId::stone_pickaxe), rng);
  REQUIRE(d.size() == 1);
  CHECK(d[0] == ItemStack(ItemId::coal, 1, 0));
  CHECK(blockDrops(makeState(B::iron_ore), ItemStack(ItemId::wooden_pickaxe), rng).empty());  // hace falta piedra
  CHECK(blockDrops(makeState(B::glass), ItemStack(), rng).empty());
}

TEST_CASE("Recetas: tablones, palos, mesa, pico, antorchas") {
  std::array<ItemStack, 4> g2{};
  g2[1] = ItemStack(B::log, 1, 2);  // abedul en cualquier casilla
  auto r = matchRecipe(g2, 2);
  REQUIRE(r);
  CHECK(*r == ItemStack(B::planks, 4, 2));

  g2 = {ItemStack(B::planks, 1, 0), ItemStack(B::planks, 1, 3), ItemStack(B::planks, 1, 1), ItemStack(B::planks, 1, 0)};
  r = matchRecipe(g2, 2);
  REQUIRE(r);
  CHECK(r->id == B::crafting_table);

  g2 = {ItemStack(), ItemStack(B::planks, 1, 0), ItemStack(), ItemStack(B::planks, 1, 0)};  // columna derecha
  r = matchRecipe(g2, 2);
  REQUIRE(r);
  CHECK(*r == ItemStack(ItemId::stick, 4, 0));

  std::array<ItemStack, 9> g3{};
  g3[0] = g3[1] = g3[2] = ItemStack(B::planks, 1, 0);
  g3[4] = g3[7] = ItemStack(ItemId::stick);
  r = matchRecipe(g3, 3);
  REQUIRE(r);
  CHECK(r->id == ItemId::wooden_pickaxe);

  g3 = {};
  g3[5] = ItemStack(ItemId::coal, 1, 1);  // carbón vegetal
  g3[8] = ItemStack(ItemId::stick);
  r = matchRecipe(g3, 3);
  REQUIRE(r);
  CHECK(*r == ItemStack(B::torch, 4, 0));

  g3 = {};
  g3[0] = ItemStack(B::dirt);
  CHECK_FALSE(matchRecipe(g3, 3).has_value());
}

TEST_CASE("Inventario y clics de ventana") {
  Player p;
  CHECK(p.inventory.add(ItemStack(B::dirt, 40)).empty());
  CHECK(p.inventory.add(ItemStack(B::dirt, 40)).empty());
  CHECK(p.inventory.slot(0).count == 64);
  CHECK(p.inventory.slot(1).count == 16);

  Menu m(MenuKind::Inventory, p);
  // Casilla 0 = resultado, 1..4 = rejilla 2x2, 5.. = inventario (principal y luego barra rápida)
  int hotbar0 = -1;
  for (int i = 0; i < static_cast<int>(m.slots().size()); i++)
    if (m.slots()[i].inventoryIndex == 0) hotbar0 = i;
  REQUIRE(hotbar0 > 0);
  m.click(hotbar0, 1, false);  // clic derecho: coge la mitad
  CHECK(p.cursor.count == 32);
  CHECK(p.inventory.slot(0).count == 32);
  m.click(1, 1, false);  // deja uno en la rejilla
  CHECK(m.slots()[1].stack->count == 1);
  CHECK(p.cursor.count == 31);
  m.click(hotbar0, 0, false);  // devuelve el resto
  CHECK(p.cursor.empty());
  CHECK(p.inventory.slot(0).count == 63);

  // Fabricar tablones con un tronco en la rejilla 2x2
  p.inventory.add(ItemStack(B::log, 2, 0));
  int logSlot = -1;
  for (int i = 0; i < static_cast<int>(m.slots().size()); i++)
    if (m.slots()[i].stack->id == B::log) logSlot = i;
  REQUIRE(logSlot > 0);
  m.click(1, 0, false);                 // coger la tierra que había en la rejilla
  m.click(hotbar0, 0, false);           // y guardarla
  m.click(logSlot, 0, false);           // coger los troncos
  m.click(2, 0, false);                 // ponerlos en la rejilla
  CHECK(m.slots()[0].stack->id == B::planks);
  m.click(0, 0, true);                  // mayús + clic en el resultado: fabrica todo
  int planks = 0;
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (p.inventory.slot(i).id == B::planks) planks += p.inventory.slot(i).count;
  CHECK(planks == 8);
  std::vector<ItemStack> dropped;
  m.close(dropped);
  CHECK(dropped.empty());
}

TEST_CASE("Horno: mineral de hierro + carbón = lingote") {
  FurnaceState f;
  f.input = ItemStack(B::iron_ore, 2);
  f.fuel = ItemStack(ItemId::coal, 1);
  for (int i = 0; i < 400; i++) f.tick();
  CHECK(f.output == ItemStack(ItemId::iron_ingot, 2, 0));
  CHECK(f.input.empty());
  CHECK(f.fuel.empty());
  CHECK(f.burning());  // el carbón dura 1600 ticks
}

TEST_CASE("Partida: romper, recoger, colocar y creativo") {
  FlatWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  // De pie al lado del bloque, mirando a su cara de arriba
  p.pos = p.prevPos = {0.5, 64, 2.5};
  TickInput in;
  in.aimDir = glm::normalize(glm::dvec3(0.5, 64.0, 0.5) - p.eyePos());
  for (int i = 0; i < 5; i++) s.tick(in);
  REQUIRE(s.target());
  CHECK(s.target()->block == glm::ivec3(0, 63, 0));
  CHECK(s.target()->face == Face::Up);

  // Romper la hierba a mano: 0,9 s (18 ticks) y cae tierra que se recoge
  in.attack = in.attackPressed = true;
  int t = 0;
  while (stateId(fw.w.block(0, 63, 0)) == B::grass && t < 100) { s.tick(in); in.attackPressed = false; t++; }
  CHECK(t == 18);
  in.attack = false;
  for (int i = 0; i < 20; i++) s.tick(in);
  CHECK(p.inventory.slot(0).empty());  // a 2 bloques todavía no se recoge
  p.pos = p.prevPos = {0.5, 63, 0.5};  // bajar al agujero donde ha caído
  for (int i = 0; i < 5; i++) s.tick(in);
  CHECK(p.inventory.slot(0) == ItemStack(B::dirt, 1, 0));
  CHECK(s.items().empty());
  p.pos = p.prevPos = {0.5, 64, 1.5};  // y salir, junto al borde

  // Colocarla otra vez en el agujero (apuntando a la tierra de debajo)
  in.aimDir = glm::normalize(glm::dvec3(0.5, 63.0, 0.5) - p.eyePos());
  in.use = in.usePressed = true;
  s.tick(in);
  CHECK(stateId(fw.w.block(0, 63, 0)) == B::dirt);
  CHECK(p.inventory.slot(0).empty());
  in.use = in.usePressed = false;

  // Creativo: rompe al momento y no suelta nada
  s.setMode(GameMode::Creative);
  in.aimDir = glm::normalize(glm::dvec3(0.5, 64.0, 0.5) - p.eyePos());
  for (int i = 0; i < 10; i++) s.tick(in);
  in.attack = in.attackPressed = true;
  s.tick(in);
  CHECK(fw.w.block(0, 63, 0) == 0);
  in.attack = in.attackPressed = false;
  for (int i = 0; i < 40; i++) s.tick(in);
  CHECK(s.items().empty());
}

TEST_CASE("Actualizaciones: la flor se rompe sin tierra y la arena cae") {
  FlatWorld fw;
  GameSession s(fw, 3);
  fw.setBlock(2, 64, 2, makeState(B::red_flower));
  s.setMode(GameMode::Creative);
  Player& p = s.player();
  p.pos = p.prevPos = {2.5, 64, 5.5};
  TickInput in;
  // Apuntar a la cara norte... mejor: a la tierra bajo la flor desde un lado
  fw.setBlock(2, 63, 3, 0);  // hueco para ver la cara sur del bloque de tierra
  in.aimDir = glm::normalize(glm::dvec3(2.5, 63.5, 3.0) - p.eyePos());
  for (int i = 0; i < 3; i++) s.tick(in);
  REQUIRE(s.target());
  CHECK(s.target()->block == glm::ivec3(2, 63, 2));
  in.attack = in.attackPressed = true;
  s.tick(in);
  CHECK(fw.w.block(2, 63, 2) == 0);
  CHECK(fw.w.block(2, 64, 2) == 0);  // la flor se ha roto al quedarse sin tierra
  in.attack = in.attackPressed = false;

  // Arena colgando: al romper lo que la sujeta, cae hasta el suelo
  fw.setBlock(6, 64, 6, makeState(B::stone));
  fw.setBlock(6, 65, 6, makeState(B::sand));
  p.pos = p.prevPos = {6.5, 64, 9.5};
  in.aimDir = glm::normalize(glm::dvec3(6.5, 64.5, 7.0) - p.eyePos());
  for (int i = 0; i < 6; i++) s.tick(in);
  REQUIRE(s.target());
  CHECK(s.target()->block == glm::ivec3(6, 64, 6));
  in.attack = in.attackPressed = true;
  s.tick(in);
  CHECK(stateId(fw.w.block(6, 64, 6)) == B::sand);
  CHECK(fw.w.block(6, 65, 6) == 0);
}

TEST_CASE("Día y noche: oscuridad del cielo") {
  CHECK(skyDarkness(6000) == 0);    // mediodía
  CHECK(skyDarkness(18000) == 11);  // medianoche
  CHECK(skyDarkness(1000) <= 1);
  CHECK(skyDarkness(13500) >= 4);   // anochecer: ya salen monstruos
  CHECK(effectiveLight(15, 0, 11) == 4);
  CHECK(effectiveLight(15, 12, 11) == 12);
}

namespace {
TickInput idle(double time = 6000) {
  TickInput in;
  in.worldTime = time;
  return in;
}
}  // namespace

TEST_CASE("Criaturas: caen, se quedan en el suelo y pasean sin salirse") {
  FlatWorld fw;
  GameSession s(fw, 1);
  s.player().pos = s.player().prevPos = {20.5, 64, 20.5};
  s.spawnMob(MobType::Pig, {0.5, 67, 0.5});      // cae 3: sin daño
  s.spawnMob(MobType::Chicken, {3.5, 75, 0.5});  // la gallina planea: nunca se hace daño
  for (int i = 0; i < 400; i++) s.tick(idle());
  REQUIRE(s.mobs().size() == 2);
  // Y un cerdo que cae 6 bloques pierde 3 (como en 1.8: lo que pase de 3)
  s.spawnMob(MobType::Pig, {10.5, 70, 10.5});
  for (int i = 0; i < 60; i++) s.tick(idle());
  CHECK(s.mobs().back().health == doctest::Approx(7.0f));
  s.explode({10.5, 64.5, 10.5}, 0.0f);  // (potencia 0: no hace nada)
  for (const Mob& m : std::vector<Mob>(s.mobs().begin(), s.mobs().begin() + 2)) {
    CHECK(m.onGround);
    CHECK(m.pos.y == doctest::Approx(64.0));
    CHECK(m.health == m.info().maxHealth);
  }
}

TEST_CASE("Combate: dos espadazos de diamante matan un cerdo; suelta chuletas") {
  FlatWorld fw;
  GameSession s(fw, 2);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::diamond_sword);
  s.spawnMob(MobType::Pig, {0.5, 64, -1.5});
  s.tick(idle());
  TickInput hit = idle();
  hit.yaw = 0;  // mirando al norte (-Z), donde está el cerdo
  hit.pitch = -0.3f;
  hit.attack = hit.attackPressed = true;
  s.tick(hit);
  REQUIRE(s.mobs().size() == 1);
  CHECK(s.mobs()[0].health == doctest::Approx(2.0f));  // 10 - 8
  CHECK(s.mobs()[0].hurtTime > 0);
  // Justo después está en su medio segundo de invulnerabilidad: no cuenta
  s.tick(hit);
  CHECK(s.mobs()[0].health == doctest::Approx(2.0f));
  // Esperar y volver a golpear (el cerdo huye: hay que seguirlo con la mirada)
  for (int i = 0; i < 10; i++) s.tick(idle());
  const glm::dvec3 d = s.mobs()[0].pos - p.eyePos();
  TickInput hit2 = idle();
  hit2.aimDir = glm::normalize(d + glm::dvec3(0, 0.6, 0));
  hit2.attack = hit2.attackPressed = true;
  if (glm::length(d) < 3.0) {
    s.tick(hit2);
    CHECK(s.mobs()[0].dying());
    for (int i = 0; i < 25; i++) s.tick(idle());
    CHECK(s.mobs().empty());
    bool pork = false;
    for (const ItemEntity& e : s.items()) pork = pork || e.stack.id == ItemId::porkchop;
    for (int i = 0; i < PlayerInventory::kSize; i++) pork = pork || p.inventory.slot(i).id == ItemId::porkchop;
    CHECK(pork);
  }
}

TEST_CASE("Zombi: de noche persigue y pega; en creativo no") {
  FlatWorld fw;
  GameSession s(fw, 3);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  s.spawnMob(MobType::Zombie, {0.5, 64, 8.5});
  for (int i = 0; i < 200 && p.health == Player::kMaxHealth; i++) s.tick(idle(18000));
  CHECK(p.health < Player::kMaxHealth);
  CHECK(p.health >= Player::kMaxHealth - 6);  // 3 por golpe, uno cada segundo como mucho

  FlatWorld fw2;
  GameSession c(fw2, 3);
  c.setMode(GameMode::Creative);
  c.player().pos = c.player().prevPos = {0.5, 64, 0.5};
  c.spawnMob(MobType::Zombie, {0.5, 64, 4.5});
  for (int i = 0; i < 200; i++) c.tick(idle(18000));
  CHECK(c.player().health == Player::kMaxHealth);
  REQUIRE(!c.mobs().empty());
  CHECK_FALSE(c.mobs()[0].chasing);
}

TEST_CASE("Zombi al sol: arde y acaba muriendo") {
  FlatWorld fw;
  GameSession s(fw, 4);
  s.player().pos = s.player().prevPos = {40.5, 64, 40.5};
  s.setMode(GameMode::Creative);
  s.spawnMob(MobType::Zombie, {0.5, 64, 0.5});
  bool burned = false;
  for (int i = 0; i < 20 * 40 && !s.mobs().empty(); i++) {
    s.tick(idle(6000));
    if (!s.mobs().empty() && s.mobs()[0].fireTicks > 0) burned = true;
  }
  CHECK(burned);
  CHECK(s.mobs().empty());
}

TEST_CASE("Creeper: se enciende cerca, explota, hace un cráter y daña") {
  FlatWorld fw;
  GameSession s(fw, 5);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  s.spawnMob(MobType::Creeper, {0.5, 64, 2.5});
  bool fused = false, exploded = false;
  for (int i = 0; i < 80 && !exploded; i++) {
    s.tick(idle(18000));
    for (const SessionEvent& e : s.takeEvents()) {
      fused = fused || e.type == SessionEvent::Type::CreeperFuse;
      exploded = exploded || e.type == SessionEvent::Type::Explosion;
    }
  }
  CHECK(fused);
  REQUIRE(exploded);
  CHECK(p.health < Player::kMaxHealth - 4);
  // La hierba y la tierra de alrededor han volado
  int holes = 0;
  for (int x = -2; x <= 3; x++)
    for (int z = 0; z <= 5; z++) holes += fw.w.block(x, 63, z) == 0 ? 1 : 0;
  CHECK(holes > 6);
  CHECK(fw.w.block(0, 0, 2) == makeState(B::bedrock));  // la roca madre aguanta
}

TEST_CASE("Explosión: la obsidiana resiste, la tierra no") {
  FlatWorld fw;
  GameSession s(fw, 6);
  s.player().pos = s.player().prevPos = {30.5, 64, 30.5};
  fw.setBlock(1, 64, 0, makeState(B::obsidian));
  s.explode({0.5, 64.5, 0.5}, 3.0f);
  CHECK(fw.w.block(1, 64, 0) == makeState(B::obsidian));
  CHECK(fw.w.block(0, 63, 0) == 0);
}

TEST_CASE("Oveja: se esquila con tijeras y suelta lana") {
  FlatWorld fw;
  GameSession s(fw, 7);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::shears);
  Mob* sheep = s.spawnMob(MobType::Sheep, {0.5, 64, -1.5});
  sheep->woolColor = 14;
  TickInput use = idle();
  use.pitch = -0.4f;
  use.use = use.usePressed = true;
  s.tick(use);
  REQUIRE(!s.mobs().empty());
  CHECK(s.mobs()[0].sheared);
  int wool = 0;
  for (const ItemEntity& e : s.items())
    if (e.stack.id == B::wool && e.stack.meta == 14) wool += e.stack.count;
  CHECK(wool >= 1);
  CHECK(p.inventory.slot(0).meta == 1);  // desgaste de las tijeras
}

/// Mantiene el botón derecho `ticks` ticks y lo suelta (devuelve los eventos de ese rato).
std::vector<SessionEvent> drawBow(GameSession& s, int ticks, float pitch = 0.0f, double time = 6000) {
  TickInput in = idle(time);
  in.yaw = 0;
  in.pitch = pitch;
  std::vector<SessionEvent> out;
  for (int i = 0; i < ticks; i++) {
    in.use = true;
    in.usePressed = i == 0;
    s.tick(in);
    for (const SessionEvent& e : s.takeEvents()) out.push_back(e);
  }
  in.use = in.usePressed = false;
  s.tick(in);  // al soltar sale la flecha
  for (const SessionEvent& e : s.takeEvents()) out.push_back(e);
  return out;
}

TEST_CASE("Arco: la potencia depende de lo tensado, gasta flecha y desgasta el arco") {
  FlatWorld fw;
  GameSession s(fw, 11);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::bow);
  p.inventory.slot(1) = ItemStack(ItemId::arrow, 5);
  s.tick(idle());
  CHECK(s.arrowCount() == 5);

  // Un toque (2 ticks) no llega a la potencia mínima: no sale nada y no se gasta
  drawBow(s, 2);
  CHECK(s.arrows().empty());
  CHECK(s.arrowCount() == 5);
  CHECK(p.inventory.slot(0).meta == 0);

  // A tope (1 s): velocidad 3 por tick, flecha crítica, y gasta una flecha y 1 de durabilidad
  const auto events = drawBow(s, 20);
  REQUIRE(s.arrows().size() == 1);
  const Arrow& full = s.arrows()[0];
  CHECK(full.fromPlayer);
  CHECK(full.crit);
  CHECK(glm::length(full.motion) == doctest::Approx(3.0 * 0.99).epsilon(0.03));  // (ya ha avanzado un tick)
  CHECK(full.motion.z < -2.5);  // hacia el norte
  CHECK(s.arrowCount() == 4);
  CHECK(p.inventory.slot(0).meta == 1);
  int power = -1;
  for (const SessionEvent& e : events)
    if (e.type == SessionEvent::Type::BowShot) power = e.value;
  CHECK(power == 100);

  // A medias (10 ticks): (0,5² + 2·0,5) / 3 = 0,4167 de potencia -> velocidad ~1,25, sin crítico
  drawBow(s, 10);
  REQUIRE(s.arrows().size() == 2);
  const Arrow& half = s.arrows()[1];
  CHECK_FALSE(half.crit);
  CHECK(glm::length(half.motion) == doctest::Approx(1.25 * 0.99).epsilon(0.05));
  CHECK(s.arrowCount() == 3);

  // Más de un segundo tensado no da más potencia
  drawBow(s, 60);
  REQUIRE(s.arrows().size() == 3);
  CHECK(glm::length(s.arrows()[2].motion) < 3.1);
}

TEST_CASE("Arco: sin flechas no se tensa; en creativo no se gastan") {
  FlatWorld fw;
  GameSession s(fw, 12);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::bow);
  TickInput in = idle();
  in.use = in.usePressed = true;
  s.tick(in);
  CHECK(s.bowTicks() == 0);
  drawBow(s, 20);
  CHECK(s.arrows().empty());

  s.setMode(GameMode::Creative);
  drawBow(s, 20);
  CHECK(s.arrows().size() == 1);
  CHECK_FALSE(s.arrows()[0].pickup);
  CHECK(p.inventory.slot(0).meta == 0);  // el arco no se desgasta en creativo
  CHECK(s.arrowCount() == 0);
}

TEST_CASE("Arco: cambiar de ranura lo cancela y tensarlo frena al andar") {
  FlatWorld fw;
  GameSession s(fw, 13);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 20.5};
  p.inventory.slot(0) = ItemStack(ItemId::bow);
  p.inventory.slot(1) = ItemStack(ItemId::arrow, 3);
  for (int i = 0; i < 10; i++) s.tick(idle());

  // Tensar y cambiar a otra ranura: no dispara
  TickInput in = idle();
  in.use = in.usePressed = true;
  for (int i = 0; i < 15; i++) {
    s.tick(in);
    in.usePressed = false;
  }
  CHECK(s.bowTicks() == 15);
  in.selectSlot = 2;
  s.tick(in);
  in.use = false;
  in.selectSlot = -1;
  s.tick(in);
  CHECK(s.arrows().empty());
  CHECK(s.arrowCount() == 3);
  CHECK(s.bowTicks() == 0);

  // Andar con el arco tensado avanza mucho menos
  p.inventory.select(0);
  s.tick(idle());
  TickInput walk = idle();
  walk.move.forward = 1;
  const double z0 = p.pos.z;
  for (int i = 0; i < 20; i++) s.tick(walk);
  const double free = z0 - p.pos.z;
  p.pos = p.prevPos = {0.5, 64, 20.5};
  p.motion = {0, 0, 0};
  for (int i = 0; i < 5; i++) s.tick(idle());
  walk.use = walk.usePressed = true;
  const double z1 = p.pos.z;
  for (int i = 0; i < 20; i++) {
    s.tick(walk);
    walk.usePressed = false;
  }
  const double drawn = z1 - p.pos.z;
  CHECK(free > 3.0);
  CHECK(drawn < free * 0.4);
}

TEST_CASE("Arco: con una mesa de trabajo delante el clic es de la mesa; agachado, tensa") {
  FlatWorld fw;
  GameSession s(fw, 14);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::bow);
  p.inventory.slot(1) = ItemStack(ItemId::arrow, 3);
  s.placeBlock({0, 65, -2}, makeState(B::crafting_table));  // a la altura de los ojos
  TickInput in = idle();
  in.pitch = 0.0f;
  in.use = in.usePressed = true;
  s.tick(in);
  s.tick(in);
  CHECK(s.menu() != nullptr);
  CHECK(s.bowTicks() == 0);
  s.closeMenu();
  in.use = in.usePressed = false;
  s.tick(in);

  in.move.sneak = true;
  p.sneaking = true;
  in.use = in.usePressed = true;
  s.tick(in);
  p.sneaking = true;
  CHECK(s.menu() == nullptr);
  CHECK(s.bowTicks() >= 1);
}

TEST_CASE("Arco: la flecha hiere a las criaturas y Arquero pide un esqueleto a 50 bloques o más") {
  // Cerca: cerdo a bocajarro; la flecha le quita vida y se rompe
  {
    FlatWorld fw;
    GameSession s(fw, 15);
    Player& p = s.player();
    p.pos = p.prevPos = {0.5, 64, 0.5};
    p.inventory.slot(0) = ItemStack(ItemId::bow);
    p.inventory.slot(1) = ItemStack(ItemId::arrow, 8);
    s.spawnMob(MobType::Pig, {0.5, 64, -8.5});
    s.tick(idle());
    const auto events = drawBow(s, 20, -0.02f);
    for (int i = 0; i < 15; i++) {
      s.tick(idle());
      for (const SessionEvent& e : s.takeEvents()) (void)e;
    }
    (void)events;
    REQUIRE_FALSE(s.mobs().empty());
    CHECK(s.mobs()[0].health < s.mobs()[0].info().maxHealth);
    bool spent = true;
    for (const Arrow& a : s.arrows()) spent = spent && a.inGround;
    CHECK(spent);
  }
  // Lejos y cerca: matar un esqueleto con la flecha da "Cazar"; solo a 50+ bloques da "Arquero"
  auto snipe = [](double distance) {
    FlatWorld fw;
    GameSession s(fw, 16);
    s.setMode(GameMode::Creative);  // (flechas de sobra)
    Player& p = s.player();
    p.pos = p.prevPos = {0.5, 64, 40.5};
    p.inventory.slot(0) = ItemStack(ItemId::bow);
    // Hace falta tener el camino de logros hasta "Cazar monstruos"
    for (Ach a : {Ach::OpenInventory, Ach::MineWood, Ach::BuildWorkBench, Ach::BuildSword}) s.achievements().award(a);
    Mob* sk = s.spawnMob(MobType::Skeleton, {0.5, 64, 40.5 - distance});
    sk->health = 1;
    sk->noAI = true;  // quieto, para poder apuntarle
    sk->persistent = true;
    const u32 id = sk->id;
    s.tick(idle(18000));
    // Se prueba con distintas elevaciones hasta acertar (la flecha cae por el camino)
    for (int step = 0; step < 100 && !s.achievements().has(Ach::KillEnemy); step++) {
      drawBow(s, 20, -0.02f + 0.004f * static_cast<float>(step), 18000);
      for (int i = 0; i < 25; i++) s.tick(idle(18000));
      s.takeEvents();
      if (!s.mobById(id) || s.mobById(id)->dying()) break;
    }
    return std::pair{s.achievements().has(Ach::KillEnemy), s.achievements().has(Ach::SnipeSkeleton)};
  };
  const auto far = snipe(52.0);
  CHECK(far.first);
  CHECK(far.second);
  const auto near = snipe(20.0);
  CHECK(near.first);
  CHECK_FALSE(near.second);
}

TEST_CASE("Arco: las flechas clavadas se recogen y una disparada hacia arriba vuelve") {
  FlatWorld fw;
  GameSession s(fw, 17);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::bow);
  p.inventory.slot(1) = ItemStack(ItemId::arrow, 5);
  s.tick(idle());
  // Al suelo, a unos metros: se clava y, al acercarse, se recoge
  drawBow(s, 20, -0.4f);
  CHECK(s.arrowCount() == 4);
  for (int i = 0; i < 40; i++) s.tick(idle());
  REQUIRE(s.arrows().size() == 1);
  REQUIRE(s.arrows()[0].inGround);
  const glm::dvec3 where = s.arrows()[0].pos;
  p.pos = p.prevPos = {where.x, 64, where.z};
  for (int i = 0; i < 4; i++) s.tick(idle());
  CHECK(s.arrowCount() == 5);

  // Recto hacia arriba: o vuelve y le da (se gasta), o se clava al lado y se recupera
  drawBow(s, 20, 1.5707f);
  CHECK(s.arrowCount() == 4);
  for (int i = 0; i < 400; i++) s.tick(idle());
  const bool hurt = p.health < Player::kMaxHealth;
  CHECK(((hurt && s.arrowCount() == 4) || s.arrowCount() == 5));
}

/// Da de comer a una criatura (clic derecho apuntándole).
void feed(GameSession& s, const Mob& m) {
  TickInput in = idle();
  in.aimDir = glm::normalize(m.pos + glm::dvec3(0, m.info().height * m.scale() * 0.5, 0) - s.player().eyePos());
  in.use = in.usePressed = true;
  s.tick(in);
}

/// Da un logro con todos los que hacen falta antes.
void awardChain(GameSession& s, Ach a) {
  if (const int parent = achievementInfo(a).parent; parent >= 0) awardChain(s, static_cast<Ach>(parent));
  s.achievements().award(a);
}

int count(const GameSession& s, bool babies) {
  int n = 0;
  for (const Mob& m : s.mobs()) n += m.baby() == babies ? 1 : 0;
  return n;
}

TEST_CASE("Cría: dos vacas con trigo tienen una cría, esperan 5 minutos y la cría crece") {
  FlatWorld fw;
  GameSession s(fw, 21);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(ItemId::wheat, 5);
  awardChain(s, Ach::KillCow);
  s.spawnMob(MobType::Cow, {-0.5, 64, -1.0});
  s.spawnMob(MobType::Cow, {1.5, 64, -1.0});
  s.tick(idle());
  feed(s, s.mobs()[0]);
  CHECK(s.mobs()[0].inLove >= kLoveTicks - 1);  // (ya ha pasado un tick)
  CHECK(p.inventory.slot(0).count == 4);
  feed(s, s.mobs()[1]);
  CHECK(s.mobs()[1].inLove > 0);
  CHECK(p.inventory.slot(0).count == 3);
  // Ya en modo amor no gasta más trigo
  feed(s, s.mobs()[1]);
  CHECK(p.inventory.slot(0).count == 3);
  bool hearts = false;
  for (const SessionEvent& e : s.takeEvents()) hearts |= e.type == SessionEvent::Type::LoveHearts && e.value == 7;
  CHECK(hearts);

  // Se buscan y, juntas 60 ticks a menos de 3 bloques, aparece una cría
  for (int i = 0; i < 400 && s.mobs().size() < 3; i++) s.tick(idle());
  REQUIRE(s.mobs().size() == 3);
  CHECK(count(s, true) == 1);
  const Mob* baby = nullptr;
  for (const Mob& m : s.mobs()) {
    if (m.baby()) baby = &m;
    else {
      CHECK(m.growth > 0);  // esperan para volver a criar
      CHECK(m.inLove == 0);
    }
  }
  REQUIRE(baby);
  CHECK(baby->type == MobType::Cow);
  CHECK(baby->growth <= -kBabyTicks + 5);
  CHECK(baby->box().max.y - baby->box().min.y == doctest::Approx(0.65));  // la mitad que una vaca
  CHECK(s.achievements().has(Ach::BreedCow));
  CHECK(s.achievements().stat("stat.animalsBred") == 1);

  // Con la espera, otro trigo no hace nada (ni se gasta)
  const Mob adult = s.mobs()[0].baby() ? s.mobs()[1] : s.mobs()[0];
  const int wheat = p.inventory.slot(0).count;
  p.pos = p.prevPos = {adult.pos.x, 64, adult.pos.z + 1.2};
  s.tick(idle());
  for (const Mob& m : s.mobs())
    if (!m.baby()) feed(s, m);
  CHECK(p.inventory.slot(0).count == wheat);

  // A los 20 minutos la cría es adulta
  Mob* b = nullptr;
  for (Mob& m : const_cast<std::vector<Mob>&>(s.mobs()))
    if (m.baby()) b = &m;
  REQUIRE(b);
  b->growth = -3;
  for (int i = 0; i < 5; i++) s.tick(idle());
  CHECK(count(s, true) == 0);
  CHECK(s.mobs().size() == 3);
}

TEST_CASE("Cría: cada animal quiere su comida, en creativo no se gasta y las crías crecen antes") {
  FlatWorld fw;
  GameSession s(fw, 22);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  for (int i = 0; i < 4; i++) {
    Mob* m = s.spawnMob(i == 0 ? MobType::Pig : i == 1 ? MobType::Sheep : i == 2 ? MobType::Chicken : MobType::Cow, {0.5, 64, -1.2});
    m->noAI = true;  // (se quedan donde están para apuntarles)
  }
  s.tick(idle());
  auto tryFeed = [&](int index, int item) {
    p.inventory.slot(0) = ItemStack(item, 2);
    // Una por una: las demás se apartan
    for (std::size_t j = 0; j < s.mobs().size(); j++) {
      Mob* m = s.mobById(s.mobs()[j].id);
      m->pos = m->prevPos = {static_cast<double>(j == static_cast<std::size_t>(index) ? 0.5 : 20.5 + j * 2), 64, -1.2};
    }
    s.tick(idle());
    feed(s, s.mobs()[static_cast<std::size_t>(index)]);
    return p.inventory.slot(0).count < 2;
  };
  CHECK_FALSE(tryFeed(0, ItemId::wheat));        // el cerdo no quiere trigo
  CHECK(tryFeed(0, ItemId::carrot));              // sino zanahorias
  CHECK_FALSE(tryFeed(1, ItemId::carrot));        // la oveja, trigo
  CHECK(tryFeed(1, ItemId::wheat));
  CHECK_FALSE(tryFeed(2, ItemId::wheat));         // la gallina, semillas
  CHECK(tryFeed(2, ItemId::wheat_seeds));
  CHECK_FALSE(tryFeed(3, ItemId::wheat_seeds));   // la vaca, trigo
  CHECK(tryFeed(3, ItemId::wheat));
  CHECK(s.mobs()[3].lovedByPlayer);

  // En creativo no se gasta
  s.setMode(GameMode::Creative);
  s.mobs();
  Mob* cow = s.mobById(s.mobs()[3].id);
  cow->inLove = 0;
  cow->growth = 0;
  CHECK_FALSE(tryFeed(3, ItemId::wheat));
  CHECK(cow->inLove > 0);
  s.setMode(GameMode::Survival);

  // Una cría: cada comida le quita el 10 % de lo que le falta (y no entra en modo amor)
  cow->inLove = 0;
  cow->growth = -20000;
  p.inventory.slot(0) = ItemStack(ItemId::wheat, 3);
  feed(s, *cow);
  CHECK(cow->growth == -18000 + 1);  // (un tick de crecimiento)
  CHECK(cow->inLove == 0);
  CHECK(p.inventory.slot(0).count == 2);
}

TEST_CASE("Cría: la cría sigue al adulto y se queda a su lado") {
  FlatWorld fw;
  GameSession s(fw, 23);
  s.player().pos = s.player().prevPos = {30.5, 64, 30.5};
  s.setMode(GameMode::Creative);  // (los animales no huyen de él)
  Mob* mother = s.spawnMob(MobType::Pig, {0.5, 64, 0.5});
  mother->noAI = true;
  Mob* calf = s.spawnMob(MobType::Pig, {0.5, 64, 7.0});
  calf->growth = -kBabyTicks;
  const u32 id = calf->id;
  const double before = glm::length(calf->pos - mother->pos);
  for (int i = 0; i < 160; i++) s.tick(idle());
  const double after = glm::length(s.mobById(id)->pos - s.mobs()[0].pos);
  CHECK(before > 6.5);
  CHECK(after < 4.0);
}

TEST_CASE("Animales al generar chunks y monstruos en la oscuridad") {
  FlatWorld fw;
  GameSession s(fw, 8);
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  for (int cz = -2; cz <= 2; cz++)
    for (int cx = -2; cx <= 2; cx++) s.populateChunk(cx, cz);  // el mundo plano es "plains"
  for (const Mob& m : s.mobs()) CHECK_FALSE(m.info().hostile);
  // Una noche en el mundo plano de 5x5 chunks: salen monstruos a más de 24 bloques
  s.setMode(GameMode::Creative);
  for (int i = 0; i < 20 * 60; i++) s.tick(idle(18000));
  int hostiles = 0;
  for (const Mob& m : s.mobs()) hostiles += m.info().hostile ? 1 : 0;
  CHECK(hostiles > 0);
  // De día no aparecen en la superficie
  FlatWorld fw2;
  GameSession d(fw2, 8);
  d.player().pos = d.player().prevPos = {0.5, 64, 0.5};
  for (int i = 0; i < 20 * 60; i++) d.tick(idle(6000));
  CHECK(d.mobs().empty());
}

TEST_CASE("Táctil: tocar una criatura la golpea (en vez de usar/colocar)") {
  FlatWorld fw;
  GameSession s(fw, 9);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  p.inventory.slot(0) = ItemStack(B::dirt, 10);
  s.spawnMob(MobType::Cow, {0.5, 64, -1.5});
  TickInput tap = idle();
  tap.pitch = -0.3f;
  tap.fromTouch = true;
  tap.use = tap.usePressed = true;
  s.tick(tap);
  REQUIRE(!s.mobs().empty());
  CHECK(s.mobs()[0].health < 10.0f);
  CHECK(p.inventory.slot(0).count == 10);  // no se ha colocado tierra
  // Con el ratón (clic derecho) sobre la vaca no se la golpea
  FlatWorld fw2;
  GameSession d(fw2, 9);
  d.player().pos = d.player().prevPos = {0.5, 64, 0.5};
  d.spawnMob(MobType::Cow, {0.5, 64, -1.5});
  TickInput right = idle();
  right.pitch = -0.3f;
  right.use = right.usePressed = true;
  d.tick(right);
  CHECK(d.mobs()[0].health == 10.0f);
}

TEST_CASE("Salto automático: sube un escalón de un bloque, pero no una pared de dos") {
  FlatWorld fw;
  for (int x = -2; x <= 2; x++) fw.setBlock(x, 64, -3, makeState(B::stone));
  MoveInput walk;
  walk.forward = 1;
  Player p;
  p.pos = p.prevPos = {0.5, 64, 0.5};
  for (int i = 0; i < 60; i++) p.tickMovement(fw.w, walk, false);
  CHECK(p.pos.y == doctest::Approx(64.0));  // sin salto automático se queda delante
  CHECK(p.pos.z > -2.0);
  walk.autoJump = true;
  Player q;
  q.pos = q.prevPos = {0.5, 64, 0.5};
  for (int i = 0; i < 60; i++) q.tickMovement(fw.w, walk, false);
  CHECK(q.pos.y == doctest::Approx(64.0));  // ya ha bajado del escalón
  CHECK(q.pos.z < -4.0);                    // y lo ha pasado

  FlatWorld wall;
  for (int y = 64; y < 66; y++) wall.setBlock(0, y, -3, makeState(B::stone));
  Player r;
  r.pos = r.prevPos = {0.5, 64, 0.5};
  double maxY = 0;
  for (int i = 0; i < 60; i++) {
    r.tickMovement(wall.w, walk, false);
    maxY = std::max(maxY, r.pos.y);
  }
  CHECK(maxY == doctest::Approx(64.0));  // contra una pared de dos no salta
}

TEST_CASE("Dificultad: pacífico quita monstruos y cura; difícil pega más; conservar inventario") {
  FlatWorld fw;
  GameSession s(fw, 4);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 0.5};
  s.spawnMob(MobType::Zombie, {0.5, 64, 8.5});
  s.spawnMob(MobType::Pig, {4.5, 64, 4.5});
  s.setRules({0, false, true});
  REQUIRE(s.mobs().size() == 1);
  CHECK(s.mobs()[0].type == MobType::Pig);
  p.health = 10;
  p.food = 10;
  for (int i = 0; i < 20 * 60; i++) s.tick(idle(18000));
  for (const Mob& m : s.mobs()) CHECK_FALSE(m.info().hostile);  // de noche no aparece ninguno
  CHECK(p.health == doctest::Approx(Player::kMaxHealth));
  CHECK(p.food == 20);

  // Difícil: el zombi quita 4,5 en vez de 3
  FlatWorld fw2;
  GameSession h(fw2, 4);
  h.setRules({3, false, true});
  h.player().pos = h.player().prevPos = {0.5, 64, 0.5};
  h.spawnMob(MobType::Zombie, {0.5, 64, 4.5});
  for (int i = 0; i < 200 && h.player().health == Player::kMaxHealth; i++) h.tick(idle(18000));
  CHECK(h.player().health == doctest::Approx(Player::kMaxHealth - 4.5f));

  // Conservar inventario: al morir no se suelta nada
  FlatWorld fw3;
  GameSession k(fw3, 4);
  k.setRules({2, true, true});
  k.player().pos = k.player().prevPos = {0.5, 64, 0.5};
  k.player().inventory.slot(3) = ItemStack(B::cobblestone, 12);
  k.player().damage(100);
  k.tick(idle());
  CHECK(k.player().dead);
  CHECK(k.player().inventory.slot(3).count == 12);
  CHECK(k.items().empty());
}

TEST_CASE("Hambre: en fácil la inanición se para en 5 corazones; en difícil mata") {
  FlatWorld fw;
  Player e;
  e.pos = e.prevPos = {0.5, 64, 0.5};
  e.difficulty = 1;
  e.food = 0;
  e.saturation = 0;
  for (int i = 0; i < 80 * 30; i++) {
    e.tickMovement(fw.w, {}, false);
    e.tickStatus(fw.w);
  }
  CHECK(e.health == doctest::Approx(10.0f));
  Player h = e;
  h.health = 20;
  h.dead = false;
  h.difficulty = 3;
  for (int i = 0; i < 80 * 30; i++) {
    h.tickMovement(fw.w, {}, false);
    h.tickStatus(fw.w);
  }
  CHECK(h.dead);
}

TEST_CASE("Velocidades de 1.8: el cerdo pasea despacio y el zombi persigue a unos 2,3 m/s") {
  FlatWorld fw;
  GameSession s(fw, 5);
  s.setMode(GameMode::Creative);
  s.player().pos = s.player().prevPos = {0.5, 64, 30.5};
  Mob* pig = s.spawnMob(MobType::Pig, {0.5, 64, 0.5});
  REQUIRE(pig);
  const u32 pigId = pig->id;
  double fastest = 0;
  for (int i = 0; i < 20 * 60; i++) {
    s.tick(idle());
    for (const Mob& m : s.mobs())
      if (m.id == pigId) fastest = std::max(fastest, std::hypot(m.pos.x - m.prevPos.x, m.pos.z - m.prevPos.z) * 20.0);
  }
  CHECK(fastest > 0.5);  // se ha movido
  CHECK(fastest < 3.2);  // y sin pasarse (antes iba a ~10 m/s)

  FlatWorld fw2;
  GameSession z(fw2, 5);
  z.player().pos = z.player().prevPos = {0.5, 64, 0.5};
  z.spawnMob(MobType::Zombie, {0.5, 64, 14.5});
  for (int i = 0; i < 20; i++) z.tick(idle(18000));  // arranca
  REQUIRE(!z.mobs().empty());
  const double z0 = z.mobs()[0].pos.z;
  for (int i = 0; i < 40; i++) z.tick(idle(18000));
  const double speed = (z0 - z.mobs()[0].pos.z) / 2.0;
  CHECK(speed > 1.5);
  CHECK(speed < 3.0);
}

TEST_CASE("Bloques de 1.8: colocar con orientación, puertas, losas y cultivos") {
  FlatWorld fw;
  World& w = fw.w;
  auto hitTop = [](int x, int y, int z) {
    RayHit h;
    h.block = {x, y, z};
    h.face = Face::Up;
    h.point = {x + 0.5, y + 1.0, z + 0.5};
    return h;
  };
  // Mirando al norte (yaw 0): escaleras hacia el norte (metadata 3), puerta de dos bloques
  auto st = placementFor(w, ItemStack(53), hitTop(0, 63, 0), 0.0f, 0.0f);
  REQUIRE(st);
  CHECK(st->state == makeState(53, 3));
  CHECK(st->pos == glm::ivec3(0, 64, 0));
  auto door = placementFor(w, ItemStack(ItemId::wooden_door), hitTop(2, 63, 0), 0.0f, 0.0f);
  REQUIRE(door);
  CHECK(door->state == makeState(64, 3));
  REQUIRE(door->hasSecond);
  CHECK(door->secondPos == glm::ivec3(2, 65, 0));
  CHECK((stateMeta(door->secondState) & 8) != 0);
  // Una losa encima de otra igual forma una losa doble
  fw.setBlock(4, 64, 0, makeState(44, 3));
  auto slab = placementFor(w, ItemStack(44, 1, 3), hitTop(4, 64, 0), 0.0f, 0.0f);
  REQUIRE(slab);
  CHECK(slab->state == makeState(43, 3));
  // El ítem de una puerta y de una losa doble
  CHECK(pickItem(makeState(64, 8)).id == ItemId::wooden_door);
  Random rng(1);
  auto drops = blockDrops(makeState(43, 3), ItemStack(ItemId::stone_pickaxe), rng);
  REQUIRE(drops.size() == 1);
  CHECK(drops[0] == ItemStack(44, 2, 3));
  // Semillas solo sobre tierra de cultivo
  CHECK_FALSE(placementFor(w, ItemStack(ItemId::wheat_seeds), hitTop(6, 63, 0), 0.0f, 0.0f));
  fw.setBlock(6, 63, 0, makeState(60, 7));
  auto seeds = placementFor(w, ItemStack(ItemId::wheat_seeds), hitTop(6, 63, 0), 0.0f, 0.0f);
  REQUIRE(seeds);
  CHECK(seeds->state == makeState(59, 0));
  // Todo lo del modo creativo que es un bloque se puede colocar en algún sitio
  for (const ItemStack& s : creativeItems()) {
    const bool ok = blockForItem(s) < 0 || isPlaceableItem(s);
    CHECK(ok);
  }
}

TEST_CASE("Usar bloques: abrir puertas, azada y el trigo crece") {
  FlatWorld fw;
  GameSession s(fw, 3);
  Player& p = s.player();
  p.pos = p.prevPos = {0.5, 64, 2.5};
  // Puerta delante del jugador
  fw.setBlock(0, 64, 0, makeState(64, 3));
  fw.setBlock(0, 65, 0, makeState(64, 8));
  TickInput use = idle();
  use.pitch = 0.0f;
  use.use = use.usePressed = true;
  s.tick(use);
  CHECK((stateMeta(fw.w.block(0, 64, 0)) & 4) != 0);
  // Azada sobre la hierba
  fw.setBlock(0, 64, 0, 0);
  fw.setBlock(0, 65, 0, 0);
  p.inventory.slot(0) = ItemStack(ItemId::iron_hoe);
  TickInput hoe = idle();
  hoe.pitch = -0.6f;
  hoe.use = hoe.usePressed = true;
  for (int i = 0; i < 6; i++) s.tick(i == 0 ? hoe : idle());
  bool tilled = false;
  for (int z = -1; z <= 2; z++) tilled |= stateId(fw.w.block(0, 63, z)) == 60;
  CHECK(tilled);
  // Trigo con randomTickSpeed alto: madura
  fw.setBlock(3, 63, 3, makeState(60, 7));
  fw.setBlock(3, 64, 3, makeState(59, 0));
  TickInput fast = idle();
  fast.randomTickSpeed = 2000;
  for (int i = 0; i < 200 && stateMeta(fw.w.block(3, 64, 3)) < 7; i++) s.tick(fast);
  CHECK(stateMeta(fw.w.block(3, 64, 3)) == 7);
}

TEST_CASE("Redstone: palanca, polvo, lámpara, antorcha, repetidor y pistón") {
  FlatWorld fw;
  GameSession s(fw, 5);
  s.player().pos = s.player().prevPos = {0.5, 64, 20.5};
  auto at = [&](int x, int y, int z) { return fw.w.block(x, y, z); };
  // Palanca en el suelo, 6 de polvo y una lámpara al final
  s.placeBlock({0, 64, 0}, makeState(69, 5));
  for (int x = 1; x <= 6; x++) s.placeBlock({x, 64, 0}, makeState(55));
  s.placeBlock({7, 64, 0}, makeState(123));
  CHECK(stateId(at(7, 64, 0)) == 123);
  s.interact({0, 64, 0});
  CHECK(stateMeta(at(1, 64, 0)) == 15);
  CHECK(stateMeta(at(6, 64, 0)) == 10);
  CHECK(stateId(at(7, 64, 0)) == 124);
  // Apagar: el polvo queda a 0 y la lámpara se apaga un poco después
  s.interact({0, 64, 0});
  CHECK(stateMeta(at(3, 64, 0)) == 0);
  for (int i = 0; i < 6; i++) s.tick(idle());
  CHECK(stateId(at(7, 64, 0)) == 123);

  // Antorcha en un lado de un bloque: se apaga cuando el bloque recibe carga
  s.placeBlock({0, 64, 4}, makeState(B::stone));
  s.placeBlock({1, 64, 4}, makeState(76, 1));  // apoyada en el bloque del oeste
  s.placeBlock({-1, 64, 4}, makeState(69, 2));  // palanca en la cara oeste del bloque
  s.interact({-1, 64, 4});
  for (int i = 0; i < 4; i++) s.tick(idle());
  CHECK(stateId(at(1, 64, 4)) == 75);

  // Pistón mirando al este empuja un bloque
  s.placeBlock({0, 64, 8}, makeState(33, 5));
  s.placeBlock({1, 64, 8}, makeState(B::cobblestone));
  s.placeBlock({-1, 64, 8}, makeState(152));  // bloque de redstone detrás
  CHECK((stateMeta(at(0, 64, 8)) & 8) != 0);
  CHECK(stateId(at(1, 64, 8)) == 34);
  CHECK(stateId(at(2, 64, 8)) == B::cobblestone);
  s.placeBlock({-1, 64, 8}, 0);
  CHECK(stateId(at(1, 64, 8)) == B::air);

  // Repetidor (mirando al sur = sale hacia el norte) con retardo
  s.placeBlock({5, 64, 12}, makeState(93, 0));
  s.placeBlock({5, 64, 11}, makeState(55));
  s.placeBlock({5, 64, 13}, makeState(152));
  CHECK(stateId(at(5, 64, 12)) == 93);
  for (int i = 0; i < 3; i++) s.tick(idle());
  CHECK(stateId(at(5, 64, 12)) == 94);
  CHECK(stateMeta(at(5, 64, 11)) == 15);
}

TEST_CASE("Logros: árbol de 1.8, disparadores y guardado en JSON") {
  Achievements a;
  CHECK_FALSE(a.award(Ach::MineWood));  // necesita "Hacer inventario"
  CHECK(a.award(Ach::OpenInventory));
  CHECK(a.award(Ach::MineWood));
  CHECK_FALSE(a.award(Ach::MineWood));  // ya lo tiene
  a.addStat("stat.jump", 5);
  Achievements b;
  b.fromJson(a.toJson());
  CHECK(b.has(Ach::MineWood));
  CHECK(b.stat("stat.jump") == 5);
  CHECK(b.count() == 2);
  // Cada logro tiene un previo que existe (o ninguno)
  for (int i = 0; i < kAchievementCount; i++) CHECK(achievementInfo(i).parent < kAchievementCount);
  CHECK(std::string(achievementInfo(Ach::Overpowered).name) == "Todopoderoso");

  // En la partida: abrir el inventario y fabricar una mesa de trabajo
  FlatWorld fw;
  GameSession s(fw, 1);
  s.openInventory();
  s.closeMenu();
  CHECK(s.achievements().has(Ach::OpenInventory));
  s.achievements().award(Ach::MineWood);
  s.player().crafted.push_back(ItemStack(B::crafting_table));
  s.tick(idle());
  CHECK(s.achievements().has(Ach::BuildWorkBench));
  bool event = false;
  for (const SessionEvent& e : s.takeEvents()) event |= e.type == SessionEvent::Type::Achievement && e.value == static_cast<int>(Ach::BuildWorkBench);
  CHECK(event);
}

TEST_CASE("UUID offline de 1.8") {
  // MD5 de "OfflinePlayer:Notch" con versión 3 (valor conocido de los servidores offline)
  CHECK(offlineUuid("Notch") == "b50ad385-829d-3141-a216-7e7d7539ba7f");
  CHECK(uuidToString(md5("")) == "d41d8cd9-8f00-b204-e980-0998ecf8427e");
}
