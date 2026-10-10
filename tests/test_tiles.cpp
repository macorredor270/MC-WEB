#include <doctest/doctest.h>

#include <filesystem>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/block_entity.h"
#include "game/session.h"
#include "save/anvil.h"
#include "save/chunk_io.h"
#include "save/nbt.h"
#include "save/region.h"

using namespace mcw;
using testing::FlatTestWorld;

TEST_CASE("Carteles, estandartes y cabezas: ida y vuelta por NBT, con acentos y los 38 dibujos") {
  SignText t;
  t.lines = {"Hola", "¡Ñandú!", "áéíóú ¿?", ""};
  const auto sg = save::signFromNbt(save::signToNbt(3, 70, -4, t));
  REQUIRE(sg);
  CHECK(sg->first == glm::ivec3(3, 70, -4));
  CHECK(sg->second == t);
  // Como lo guarda 1.8: cada línea es un texto de chat en JSON; también se leen las variantes con "extra" y el texto suelto
  const std::string quoted = signLineToJson("a\"b");
  CHECK(quoted == "{\"text\":\"a\\\"b\"}");
  const std::string withExtra = "{\"text\":\"\",\"extra\":[{\"text\":\"uno \"},{\"text\":\"dos\"}]}";
  CHECK(signLineFromJson(withExtra) == "uno dos");
  CHECK(signLineFromJson("\"suelto\"") == "suelto");
  CHECK(signLineFromJson("sin comillas") == "sin comillas");

  BannerData b;
  b.base = 14;
  for (const BannerPatternInfo& p : bannerPatterns()) b.patterns.push_back({p.code, static_cast<u8>(b.patterns.size() % 16)});
  CHECK(bannerPatterns().size() == 38);
  const auto bn = save::bannerFromNbt(save::bannerToNbt(1, 2, 3, b));
  REQUIRE(bn);
  CHECK(bn->second == b);
  CHECK(bannerPatternTexture("bs") == "stripe_bottom");
  CHECK(bannerPatternTexture("nope").empty());

  SkullData s;
  s.type = 3;
  s.rot = 11;
  s.owner = "Notch";
  const auto sk = save::skullFromNbt(save::skullToNbt(0, 64, 0, s));
  REQUIRE(sk);
  CHECK(sk->second == s);

  // Los dibujos de un estandarte viajan en la etiqueta del objeto
  ItemStack item(ItemId::banner, 1, 5);
  ItemExtra e;
  e.patterns = {{"cr", 3}, {"bo", 0}};
  item.setExtra(e);
  const ItemStack back = save::stackFromNbt(save::stackToNbt(item));
  CHECK(back.meta == 5);
  REQUIRE(back.extra);
  CHECK(back.extra->patterns == e.patterns);
  CHECK(back == item);
}

TEST_CASE("Carteles, estandartes y cabezas se guardan con su chunk y vuelven al cargarlo") {
  const auto dir = std::filesystem::temp_directory_path() / "mcweb-tiles-test";
  std::filesystem::remove_all(dir);
  FlatTestWorld fw;
  GameSession session(fw, 1);
  session.placeBlock({2, 64, 2}, makeState(kSignBlock, 4));
  session.placeBlock({3, 64, 2}, makeState(kStandingBannerBlock, 0));
  session.placeBlock({4, 64, 2}, makeState(kSkullBlock, 1));
  session.tiles().signs[{2, 64, 2}] = SignText{{"uno", "dos", "tres", "cuatro"}};
  session.tiles().banners[{3, 64, 2}] = BannerData{9, {{"ms", 4}}};
  session.tiles().skulls[{4, 64, 2}] = SkullData{2, 7, ""};
  {
    RegionStore store(dir);
    save::storeChunk(store, *fw.w.chunk(0, 0), session, true, 0);  // al descargarlo salen de la memoria
    CHECK(!session.tiles().any());
  }
  GameSession other(fw, 1);
  RegionStore store(dir);
  auto loaded = save::loadChunk(store, {0, 0}, other);
  REQUIRE(loaded);
  CHECK(other.tiles().signs.at({2, 64, 2}).lines[3] == "cuatro");
  CHECK(other.tiles().banners.at({3, 64, 2}).base == 9);
  CHECK(other.tiles().skulls.at({4, 64, 2}).rot == 7);
  std::filesystem::remove_all(dir);
}

TEST_CASE("Al poner un cartel se abre el editor; el texto se recorta a 15 letras; romper el estandarte suelta el suyo con color y dibujos") {
  FlatTestWorld fw;
  GameSession s(fw, 1);
  s.setSpawn({0.5, 64, 0.5});
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  s.player().inventory.slot(0) = ItemStack(ItemId::sign);
  s.player().inventory.select(0);
  RayHit hit;
  hit.block = {3, 63, 0};
  hit.face = Face::Up;
  hit.point = {3.5, 64.0, 0.5};
  const auto r = s.useHeldOnBlock(hit);
  CHECK(r.kind == GameSession::UseResult::Kind::Sign);
  CHECK(isSignBlock(stateId(fw.w.block(3, 64, 0))));
  CHECK(s.tiles().signs.count({3, 64, 0}) == 1);
  SignText t;
  t.lines = {"1234567890123456789", "ñandúñandúñandúñandú", "", ""};
  CHECK(s.setSignText({3, 64, 0}, t));
  CHECK(s.tiles().signs.at({3, 64, 0}).lines[0] == "123456789012345");
  CHECK(s.tiles().signs.at({3, 64, 0}).lines[1] == "ñandúñandúñandú");
  CHECK(!s.setSignText({9, 64, 9}, t));  // ahí no hay cartel

  // Estandarte: rojo (daño 1) con un dibujo; al romperlo suelta el mismo
  ItemStack banner(ItemId::banner, 1, 1);
  ItemExtra e;
  e.patterns = {{"gru", 15}};
  banner.setExtra(e);
  s.player().inventory.slot(0) = banner;
  hit.block = {5, 63, 0};
  hit.point = {5.5, 64.0, 0.5};
  CHECK(s.useHeldOnBlock(hit).kind == GameSession::UseResult::Kind::Used);
  REQUIRE(s.tiles().banners.count({5, 64, 0}) == 1);
  CHECK(s.tiles().banners.at({5, 64, 0}).base == 1);
  s.breakBlockAt({5, 64, 0}, true);
  CHECK(s.tiles().banners.empty());
  bool dropped = false;
  for (const ItemEntity& it : s.items())
    if (it.stack.id == ItemId::banner) {
      dropped = true;
      CHECK(it.stack.meta == 1);
      REQUIRE(it.stack.extra);
      CHECK(it.stack.extra->patterns.size() == 1);
    }
  CHECK(dropped);

  // Quitar el cartel borra sus datos
  fw.setBlock(3, 64, 0, 0);
  s.setWorldBlock(3, 64, 0, 0);
  CHECK(s.tiles().signs.empty());
}
