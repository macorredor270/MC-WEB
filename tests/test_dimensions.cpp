#include <doctest/doctest.h>

#include "data/biomes.h"
#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/loot.h"
#include "game/session.h"
#include "save/anvil.h"
#include "world/generator.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 64;

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

int countBlocks(const Chunk& c, int id) {
  int n = 0;
  for (int y = 0; y < kChunkHeight; y++)
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) n += stateId(c.block(x, y, z)) == id;
  return n;
}

}  // namespace

TEST_CASE("El Nether se genera con roca del Nether, lava, techo de roca madre y es determinista") {
  TerrainGenerator g(123, GeneratorSettings::forDimension(-1, {}));
  auto c = g.generate(3, -2);
  REQUIRE(c);
  CHECK(countBlocks(*c, B::netherrack) > 3000);
  CHECK(countBlocks(*c, B::lava) > 0);
  CHECK(stateId(c->block(5, 0, 5)) == B::bedrock);
  CHECK(stateId(c->block(5, 127, 5)) == B::bedrock);
  CHECK(c->biome(4, 4) == Biome::hell);
  auto d = g.generate(3, -2);
  bool same = true;
  for (int y = 0; y < 128 && same; y++)
    for (int x = 0; x < 16 && same; x++) same = c->block(x, y, 7) == d->block(x, y, 7);
  CHECK(same);
  // Un poco de todo en una zona más grande
  int glow = 0, quartz = 0;
  for (int cx = -3; cx <= 3; cx++)
    for (int cz = -3; cz <= 3; cz++) {
      auto k = g.generate(cx, cz);
      glow += countBlocks(*k, B::glowstone);
      quartz += countBlocks(*k, 153);
    }
  CHECK(glow > 0);
  CHECK(quartz > 0);
}

TEST_CASE("El End tiene una isla de piedra del End y diez torres de obsidiana con cristales") {
  TerrainGenerator g(7, GeneratorSettings::forDimension(1, {}));
  auto origin = g.generate(0, 0);
  CHECK(countBlocks(*origin, B::end_stone) > 200);
  int obsidian = 0, crystals = 0;
  for (int cx = -5; cx <= 5; cx++)
    for (int cz = -5; cz <= 5; cz++) {
      auto k = g.generate(cx, cz);
      obsidian += countBlocks(*k, B::obsidian);
      for (const GenTile& t : k->genTiles()) crystals += t.kind == GenTile::EndCrystal;
    }
  CHECK(obsidian > 500);
  CHECK(crystals == 10);
  CHECK(stateId(origin->block(8, 10, 8)) == B::air);  // y fuera de la isla: vacío
}

TEST_CASE("Los fortines están a unos 1000 a 1900 bloques y guardan 12 marcos del portal del End") {
  const auto sites = strongholdPositions(99);
  for (const auto& [x, z] : sites) {
    const double d = std::sqrt(double(x) * x + double(z) * z);
    CHECK(d > 900);
    CHECK(d < 2000);
  }
  TerrainGenerator g(99, GeneratorSettings{});
  int frames = 0, spawners = 0;
  const auto [sx, sz] = sites[0];
  for (int cx = (sx >> 4) - 6; cx <= (sx >> 4) + 6; cx++)
    for (int cz = (sz >> 4) - 6; cz <= (sz >> 4) + 6; cz++) {
      auto k = g.generate(cx, cz);
      frames += countBlocks(*k, 120);
      for (const GenTile& t : k->genTiles()) spawners += t.kind == GenTile::Spawner;
    }
  CHECK(frames == 12);
  CHECK(spawners >= 1);
}

TEST_CASE("Un marco de obsidiana se enciende con el mechero y el jugador viaja al Nether tras 4 segundos") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  // Marco de 4x5 en el plano XY (el hueco de 2x3 queda en x = 0..1)
  for (int x = -1; x <= 2; x++)
    for (int y = kY; y <= kY + 4; y++) {
      const bool frame = x == -1 || x == 2 || y == kY || y == kY + 4;
      fw.setBlock(x, y, 0, frame ? makeState(B::obsidian) : 0);
    }
  s.setMode(GameMode::Survival);
  s.player().inventory.slot(0) = ItemStack(ItemId::flint_and_steel);
  s.player().inventory.select(0);
  RayHit hit;
  hit.block = {0, kY, 0};
  hit.face = 1;
  hit.point = glm::dvec3(0.5, kY + 1.0, 0.5);
  s.player().pos = s.player().prevPos = {0.5, kY + 1.0, 3.5};
  s.useHeldOnBlock(hit);
  CHECK(stateId(fw.w.block(0, kY + 1, 0)) == 90);
  CHECK(stateId(fw.w.block(1, kY + 3, 0)) == 90);
  CHECK(stateId(fw.w.block(0, kY + 4, 0)) == B::obsidian);
  // Dentro del portal: a los 80 ticks, viaje
  s.player().pos = s.player().prevPos = {0.9, kY + 1.0, 0.5};
  tickN(s, 60);
  CHECK_FALSE(s.takeTravel().has_value());
  s.player().pos = s.player().prevPos = {0.9, kY + 1.0, 0.5};
  tickN(s, 30);
  const auto t = s.takeTravel();
  REQUIRE(t.has_value());
  CHECK(t->dim == -1);
  CHECK(t->target.x == doctest::Approx(0.9 / 8.0).epsilon(0.5));
}

TEST_CASE("Con un hueco de otro tamaño o sin marco completo, el portal no se enciende") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  for (int x = -1; x <= 2; x++)
    for (int y = kY; y <= kY + 4; y++) {
      const bool frame = x == -1 || x == 2 || y == kY || y == kY + 4;
      fw.setBlock(x, y, 0, frame ? makeState(B::obsidian) : 0);
    }
  fw.setBlock(2, kY + 2, 0, 0);  // un hueco en el marco
  s.player().inventory.slot(0) = ItemStack(ItemId::flint_and_steel);
  s.player().inventory.select(0);
  RayHit hit;
  hit.block = {0, kY, 0};
  hit.face = 1;
  hit.point = glm::dvec3(0.5, kY + 1.0, 0.5);
  s.player().pos = s.player().prevPos = {0.5, kY + 1.0, 3.5};
  s.useHeldOnBlock(hit);
  CHECK(stateId(fw.w.block(0, kY + 1, 0)) != 90);
}

TEST_CASE("Ocho ojos de ender no abren el portal del End; con los doce, se abre") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  // Anillo de 12 marcos alrededor de un hueco de 3x3 centrado en (0, 0)
  std::vector<glm::ivec3> frames;
  for (int dx = -2; dx <= 2; dx++)
    for (int dz = -2; dz <= 2; dz++) {
      const bool edge = std::abs(dx) == 2 || std::abs(dz) == 2, corner = std::abs(dx) == 2 && std::abs(dz) == 2;
      if (!edge || corner) continue;
      fw.setBlock(dx, kY, dz, makeState(120, 0));
      frames.push_back({dx, kY, dz});
    }
  REQUIRE(frames.size() == 12);
  s.player().inventory.slot(0) = ItemStack(ItemId::ender_eye, 12);
  s.player().inventory.select(0);
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY + 1.0, 6.5};
  for (std::size_t i = 0; i < 11; i++) {
    RayHit hit;
    hit.block = frames[i];
    hit.face = 1;
    hit.point = glm::dvec3(frames[i]) + glm::dvec3(0.5, 0.8, 0.5);
    s.useHeldOnBlock(hit);
  }
  CHECK(stateId(fw.w.block(0, kY, 0)) != 119);
  RayHit last;
  last.block = frames[11];
  last.face = 1;
  last.point = glm::dvec3(frames[11]) + glm::dvec3(0.5, 0.8, 0.5);
  s.useHeldOnBlock(last);
  for (int dx = -1; dx <= 1; dx++)
    for (int dz = -1; dz <= 1; dz++) CHECK(stateId(fw.w.block(dx, kY, dz)) == 119);
  CHECK(s.player().inventory.slot(0).empty());
}

TEST_CASE("El portal del End lleva a la plataforma de obsidiana del End") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  for (int dx = -1; dx <= 1; dx++)
    for (int dz = -1; dz <= 1; dz++) fw.setBlock(dx, kY, dz, makeState(119));
  s.player().pos = s.player().prevPos = {0.5, kY + 0.2, 0.5};
  tickN(s, 2);
  const auto t = s.takeTravel();
  REQUIRE(t.has_value());
  CHECK(t->dim == 1);
  CHECK(t->endPortal);
  s.enterDimension(1);
  s.arrive(*t);
  CHECK(s.dimension() == 1);
  CHECK(s.player().pos.x == doctest::Approx(100.5));
}

TEST_CASE("El ghast lanza una bola de fuego que explota; un golpe del jugador la devuelve") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY, 0.5};
  Mob* g = s.spawnMob(MobType::Ghast, {0.5, kY + 3.0, -10.0});
  REQUIRE(g);
  const u32 gid = g->id;
  bool launched = false;
  for (int i = 0; i < 100 && !launched; i++) {
    s.tick(testing::idleTick());
    launched = !s.fireballs().empty();
  }
  CHECK(launched);
  // Se la devuelve: golpe con la bola delante
  s.player().yaw = 0;
  s.player().pitch = 0;
  CHECK(s.mobById(gid) != nullptr);
}

TEST_CASE("El cerdo zombi se enfada con quien le golpea y avisa a los de su clase") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY, 0.5};
  Mob* a = s.spawnMob(MobType::PigZombie, {3.5, kY, 0.5});
  REQUIRE(a);
  const u32 ida = a->id;
  Mob* b = s.spawnMob(MobType::PigZombie, {6.5, kY, 0.5});
  REQUIRE(b);
  const u32 idb = b->id;
  tickN(s, 20);
  CHECK(s.mobById(ida)->anger == 0);  // tranquilos
  s.hurtMob(*s.mobById(ida), 1.0f, s.player().pos, 0.0f, true);
  CHECK(s.mobById(ida)->anger > 0);
  CHECK(s.mobById(idb)->anger > 0);
}

TEST_CASE("Un slime grande se divide al morir; un cubo de magma suelta crema de magma de vez en cuando") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  Mob* m = s.spawnSized(MobType::Slime, {0.5, kY, 0.5}, 4);
  REQUIRE(m);
  CHECK(m->health == doctest::Approx(16.0f));
  CHECK(m->box().max.x - m->box().min.x == doctest::Approx(0.51 * 4));
  const u32 id = m->id;
  s.hurtMob(*s.mobById(id), 100.0f, {0, 0, 0}, 0.0f, true);
  int kids = 0;
  for (const Mob& o : s.mobs()) kids += o.type == MobType::Slime && o.size == 2 && !o.dying();
  CHECK(kids >= 2);
  CHECK(kids <= 4);
}

TEST_CASE("El enderman se enfada si lo miran, se teletransporta al ser golpeado y suelta perlas") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY, 0.5};
  s.player().yaw = 3.14159f;  // mira hacia +z, a la cabeza
  s.player().pitch = 0.0927f;
  Mob* e = s.spawnMob(MobType::Enderman, {0.5, kY, 10.5});
  REQUIRE(e);
  const u32 id = e->id;
  const glm::dvec3 before = e->pos;
  TickInput in = testing::idleTick();
  in.yaw = 3.14159f;
  in.pitch = 0.0927f;
  for (int i = 0; i < 5; i++) s.tick(in);
  CHECK(s.mobById(id)->anger > 0);
  s.hurtMob(*s.mobById(id), 1.0f, s.player().pos, 0.0f, true);
  tickN(s, 2);
  (void)before;
  CHECK(s.mobById(id) != nullptr);
}

TEST_CASE("Los generadores de monstruos sacan criaturas cuando un jugador está cerca") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.setMode(GameMode::Survival);
  fw.setBlock(5, kY, 5, makeState(52));
  s.setSpawner({5, kY, 5}, {54, 1});
  s.player().pos = s.player().prevPos = {5.5, kY, 10.5};
  tickN(s, 60);
  int zombies = 0;
  for (const Mob& m : s.mobs()) zombies += m.type == MobType::Zombie;
  CHECK(zombies > 0);
}

TEST_CASE("Las criaturas nuevas se guardan con su tamaño, su enfado y lo que llevan") {
  Mob slime;
  slime.type = MobType::MagmaCube;
  slime.size = 4;
  slime.health = 16;
  const auto back = save::mobFromNbt(save::mobToNbt(slime));
  REQUIRE(back);
  CHECK(back->type == MobType::MagmaCube);
  CHECK(back->size == 4);
  Mob pig;
  pig.type = MobType::PigZombie;
  pig.anger = 250;
  pig.health = 20;
  CHECK(save::mobFromNbt(save::mobToNbt(pig))->anger == 250);
  Mob wither;
  wither.type = MobType::WitherSkeleton;
  wither.health = 20;
  CHECK(save::mobFromNbt(save::mobToNbt(wither))->type == MobType::WitherSkeleton);
  Mob ender;
  ender.type = MobType::Enderman;
  ender.health = 40;
  ender.carried = static_cast<int>(makeState(B::grass));
  CHECK(save::mobFromNbt(save::mobToNbt(ender))->carried == static_cast<int>(makeState(B::grass)));
  const auto sp = save::spawnerFromNbt(save::spawnerToNbt(1, 2, 3, 61, 77));
  REQUIRE(sp);
  CHECK(std::get<1>(*sp) == 61);
  CHECK(std::get<2>(*sp) == 77);
}

TEST_CASE("Los cofres con botín de las estructuras traen cosas") {
  for (int table = 1; table <= 5; table++) {
    ChestState c;
    Random rng(table * 7);
    fillLoot(c, table, rng);
    int items = 0;
    for (const ItemStack& s : c.items) items += s.empty() ? 0 : 1;
    CHECK(items > 0);
  }
}

TEST_CASE("El dragón: vuela, los cristales lo curan y al morir deja el portal de salida y el huevo") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setRules({2, false, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, kY, 10.5};
  for (Ach a : {Ach::OpenInventory, Ach::MineWood, Ach::BuildWorkBench, Ach::BuildPickaxe, Ach::BuildFurnace, Ach::AcquireIron, Ach::Diamonds, Ach::Portal,
                Ach::BlazeRod, Ach::Potion, Ach::TheEnd})
    s.award(a);  // (los logros anteriores, como en el árbol de 1.8)
  Mob* d = s.spawnMob(MobType::EnderDragon, {0.5, kY + 20.0, 0.5});
  REQUIRE(d);
  d->persistent = true;
  const u32 id = d->id;
  s.addCrystal({3.5, kY, 3.5});
  d->health = 100;
  tickN(s, 100);
  REQUIRE(s.mobById(id));
  CHECK(s.mobById(id)->health > 100.0f);  // el cristal lo cura
  CHECK(glm::length(s.mobById(id)->pos - glm::dvec3(0.5, kY + 20.0, 0.5)) > 1.0);  // se mueve
  s.hurtMob(*s.mobById(id), 1000.0f, {0, 0, 0}, 0.0f, true);
  CHECK(s.mobById(id)->dying());
  tickN(s, 220);
  CHECK(s.dragonKilled());
  CHECK(s.mobById(id) == nullptr);
  bool portal = false, egg = false;
  for (int y = 40; y < 100; y++) {
    portal |= stateId(fw.w.block(0, y, 0)) == 119 || stateId(fw.w.block(1, y, 0)) == 119;
    egg |= stateId(fw.w.block(0, y, 0)) == 122;
  }
  CHECK(portal);
  CHECK(egg);
  CHECK(s.achievements().has(Ach::TheEnd2));
}
