#include <doctest/doctest.h>

#include <cmath>

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
