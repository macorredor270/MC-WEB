#include <doctest/doctest.h>

#include <filesystem>

#include "core/zip.h"
#include "save/anvil.h"
#include "save/nbt.h"
#include "save/region.h"
#include "save/world_save.h"
#include "world/generator.h"

using namespace mcw;
namespace stdfs = std::filesystem;

namespace {
stdfs::path tempDir(const char* name) {
  const stdfs::path p = stdfs::temp_directory_path() / ("mcweb_test_" + std::string(name));
  std::error_code ec;
  stdfs::remove_all(p, ec);
  stdfs::create_directories(p);
  return p;
}
}  // namespace

TEST_CASE("NBT: ida y vuelta con todos los tipos, y con gzip") {
  nbt::Value root = nbt::Value::compound();
  root.set("b", nbt::Value::byte(-5));
  root.set("s", nbt::Value::shortV(1234));
  root.set("i", nbt::Value::intV(-70000));
  root.set("l", nbt::Value::longV(-1234567890123LL));
  root.set("f", nbt::Value::floatV(1.5f));
  root.set("d", nbt::Value::doubleV(-2.25));
  root.set("str", nbt::Value::string("Hola, mundo"));
  root.set("ba", nbt::Value::byteArray({1, 2, 255}));
  root.set("ia", nbt::Value::intArray({7, -8, 9}));
  nbt::Value list = nbt::Value::list(nbt::Tag::Compound);
  nbt::Value inner = nbt::Value::compound();
  inner.set("x", nbt::Value::intV(42));
  list.push(inner);
  root.set("list", list);
  root.set("empty", nbt::Value::list(nbt::Tag::End));

  const std::vector<u8> raw = nbt::write(root, "raiz");
  std::string name;
  auto back = nbt::read(raw, &name);
  REQUIRE(back);
  CHECK(name == "raiz");
  CHECK(back->getInt("b") == -5);
  CHECK(back->getInt("s") == 1234);
  CHECK(back->getInt("i") == -70000);
  CHECK(back->getLong("l") == -1234567890123LL);
  CHECK(back->getDouble("f") == doctest::Approx(1.5));
  CHECK(back->getDouble("d") == doctest::Approx(-2.25));
  CHECK(back->getString("str") == "Hola, mundo");
  CHECK(back->get("ba")->bytes() == std::vector<u8>{1, 2, 255});
  CHECK(back->get("ia")->ints() == std::vector<i32>{7, -8, 9});
  REQUIRE(back->getList("list"));
  CHECK(back->getList("list")->items()[0].getInt("x") == 42);
  CHECK(back->getList("empty")->items().empty());
  CHECK(nbt::write(*back, "raiz") == raw);  // se reescribe igual

  const std::vector<u8> gz = gzipCompress(raw.data(), raw.size());
  CHECK(isGzip(gz.data(), gz.size()));
  auto fromGz = nbt::read(gz);
  REQUIRE(fromGz);
  CHECK(fromGz->getString("str") == "Hola, mundo");
  // Datos rotos: no revienta
  std::vector<u8> broken(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(raw.size() / 2));
  CHECK_FALSE(nbt::read(broken));
}

TEST_CASE("Región Anvil: guardar y leer chunks, y reescribir más grandes") {
  const stdfs::path dir = tempDir("region");
  TerrainGenerator gen(12345);
  {
    RegionStore store(dir);
    for (int cz = -1; cz <= 1; cz++)
      for (int cx = -1; cx <= 1; cx++) {
        auto c = gen.generate(cx, cz);
        CHECK(store.writeChunk(cx, cz, nbt::write(save::chunkToNbt(*c, 100))));
      }
  }
  {
  RegionStore store(dir);  // abrir de nuevo desde disco
  for (int cz = -1; cz <= 1; cz++)
    for (int cx = -1; cx <= 1; cx++) {
      auto data = store.readChunk(cx, cz);
      REQUIRE(data);
      auto root = nbt::read(*data);
      REQUIRE(root);
      auto loaded = save::chunkFromNbt(*root);
      REQUIRE(loaded);
      auto orig = gen.generate(cx, cz);
      CHECK(loaded->pos() == orig->pos());
      int diffs = 0;
      for (int y = 0; y < kChunkHeight; y++)
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            diffs += loaded->block(x, y, z) != orig->block(x, y, z);
            diffs += loaded->packedLight(x, y, z) != orig->packedLight(x, y, z);
          }
      CHECK(diffs == 0);
      CHECK(loaded->biome(3, 7) == orig->biome(3, 7));
    }
  CHECK_FALSE(store.readChunk(5, 5));
  // Reescribir un chunk con otro contenido
  auto c = gen.generate(0, 0);
  c->setBlock(1, 200, 1, makeState(B::stone));
  CHECK(store.writeChunk(0, 0, nbt::write(save::chunkToNbt(*c, 200))));
  auto again = save::chunkFromNbt(*nbt::read(*store.readChunk(0, 0)));
  CHECK(again->block(1, 200, 1) == makeState(B::stone));
  }  // cerrar los archivos antes de borrar (en Windows no se puede borrar un archivo abierto)
  std::error_code ec;
  stdfs::remove_all(dir, ec);
}

TEST_CASE("level.dat, jugador, criaturas y objetos en el formato de 1.8") {
  const stdfs::path dir = tempDir("level");
  WorldSave w(dir);
  LevelInfo info;
  info.name = "Mi mundo";
  info.seed = 0xDEADBEEFCAFEull;
  info.gameType = 1;
  info.difficulty = 3;
  info.dayTime = 13000;
  info.spawn = {10, 70, -20};
  info.gameRules["keepInventory"] = "true";
  Player p;
  p.pos = {1.5, 70, -3.25};
  p.yaw = 1.0f;
  p.pitch = -0.3f;
  p.health = 13;
  p.food = 9;
  p.inventory.slot(0) = ItemStack(ItemId::diamond_pickaxe, 1, 17);
  p.inventory.slot(20) = ItemStack(B::planks, 33, 2);
  p.inventory.select(4);
  p.mode = GameMode::Creative;
  info.player = save::playerToNbt(p, {}, false);
  REQUIRE(w.saveLevel(info));

  LevelInfo back;
  REQUIRE(w.loadLevel(back));
  CHECK(back.name == "Mi mundo");
  CHECK(back.seed == 0xDEADBEEFCAFEull);
  CHECK(back.gameType == 1);
  CHECK(back.difficulty == 3);
  CHECK(back.dayTime == 13000);
  CHECK(back.spawn == glm::ivec3(10, 70, -20));
  CHECK(back.ruleBool("keepInventory", false));
  REQUIRE(back.player);
  Player q;
  save::playerFromNbt(*back.player, q);
  CHECK(q.pos.x == doctest::Approx(1.5));
  CHECK(q.pos.z == doctest::Approx(-3.25));
  CHECK(q.yaw == doctest::Approx(1.0f).epsilon(1e-4));
  CHECK(q.pitch == doctest::Approx(-0.3f).epsilon(1e-4));
  CHECK(q.health == doctest::Approx(13.0f));
  CHECK(q.food == 9);
  CHECK(q.inventory.slot(0) == ItemStack(ItemId::diamond_pickaxe, 1, 17));
  CHECK(q.inventory.slot(20) == ItemStack(B::planks, 33, 2));
  CHECK(q.inventory.selectedIndex() == 4);
  CHECK(q.creative());

  // Rotación como la guarda el juego: mirando al sur = 0 grados, al oeste = 90
  CHECK(save::yawToSave(3.14159265f) == doctest::Approx(0.0f).epsilon(1e-3));
  CHECK(save::yawToSave(1.5707963f) == doctest::Approx(90.0f).epsilon(1e-3));

  Mob sheep;
  sheep.type = MobType::Sheep;
  sheep.pos = {3, 64, 4};
  sheep.health = 6;
  sheep.woolColor = 14;
  sheep.sheared = true;
  auto m = save::mobFromNbt(save::mobToNbt(sheep));
  REQUIRE(m);
  CHECK(m->type == MobType::Sheep);
  CHECK(m->health == doctest::Approx(6.0f));
  CHECK(m->woolColor == 14);
  CHECK(m->sheared);
  ItemEntity it;
  it.stack = ItemStack(ItemId::apple, 3);
  it.pos = {1, 2, 3};
  auto ie = save::itemEntityFromNbt(save::itemEntityToNbt(it));
  REQUIRE(ie);
  CHECK(ie->stack == ItemStack(ItemId::apple, 3));
  CHECK(save::itemName(B::stone) == "minecraft:stone");
  ChestState chest;
  chest.items[5] = ItemStack(ItemId::diamond, 7);
  chest.items[26] = ItemStack(B::wool, 3, 14);
  auto ch = save::chestFromNbt(save::chestToNbt(10, 64, -3, chest));
  REQUIRE(ch);
  CHECK(ch->first == glm::ivec3(10, 64, -3));
  CHECK(ch->second.items[5] == ItemStack(ItemId::diamond, 7));
  CHECK(ch->second.items[26] == ItemStack(B::wool, 3, 14));
  CHECK(ch->second.items[0].empty());

  auto list = WorldSave::list();
  (void)list;
  std::error_code ec;
  stdfs::remove_all(dir, ec);
}

TEST_CASE("Exportar e importar un mundo en .zip") {
  const std::string folder = WorldSave::freeFolderName("Prueba zip mcweb");
  {
    WorldSave w(WorldSave::savesDir() / folder);
    LevelInfo info;
    info.name = "Prueba zip mcweb";
    info.seed = 4242;
    REQUIRE(w.saveLevel(info));
    TerrainGenerator gen(4242);
    auto c = gen.generate(0, 0);
    REQUIRE(w.regions().writeChunk(0, 0, nbt::write(save::chunkToNbt(*c, 0))));
  }
  const std::vector<u8> zip = WorldSave::exportZip(folder);
  REQUIRE(!zip.empty());
  auto archive = ZipArchive::openMemory(zip);
  REQUIRE(archive);
  CHECK(archive->contains(folder + "/level.dat"));
  CHECK(archive->contains(folder + "/region/r.0.0.mca"));
  WorldSave::remove(folder);
  const std::string back = WorldSave::importZip(zip, "otro");
  REQUIRE(!back.empty());
  {
    WorldSave w(WorldSave::savesDir() / back);
    LevelInfo info;
    REQUIRE(w.loadLevel(info));
    CHECK(info.name == "Prueba zip mcweb");
    CHECK(info.seed == 4242);
    CHECK(w.regions().readChunk(0, 0).has_value());
  }
  // Un zip sin mundo no importa nada
  CHECK(WorldSave::importZip(zipFiles({{"hola.txt", {'h', 'o', 'l', 'a'}}}), "x").empty());
  WorldSave::remove(back);
}
