#include <doctest/doctest.h>

#include "net/protocol.h"
#include "world/generator.h"

using namespace mcw;
using namespace mcw::net;

TEST_CASE("Protocolo 47: tramas con y sin compresión") {
  for (int threshold : {-1, 64}) {
    PacketCodec out, in;
    out.setCompression(threshold);
    in.setCompression(threshold);
    std::vector<u8> small = {1, 2, 3};
    std::vector<u8> big(5000);
    for (std::size_t i = 0; i < big.size(); i++) big[i] = static_cast<u8>(i * 7);
    std::vector<u8> stream = out.encode(0x21, big);
    const auto s2 = out.encode(0x00, small);
    stream.insert(stream.end(), s2.begin(), s2.end());
    // Llegan de trozo en trozo
    for (std::size_t i = 0; i < stream.size(); i += 333) in.feed(std::span(stream).subspan(i, std::min<std::size_t>(333, stream.size() - i)));
    auto p1 = in.next();
    REQUIRE(p1);
    CHECK(p1->id == 0x21);
    CHECK(p1->data == big);
    auto p2 = in.next();
    REQUIRE(p2);
    CHECK(p2->id == 0);
    CHECK(p2->data == small);
    CHECK_FALSE(in.next());
  }
}

TEST_CASE("Protocolo 47: posición, objetos, metadatos, chat") {
  BufferWriter w;
  writePosition(w, {-123, 64, 4567});
  writePosition(w, {33554431, 255, -33554432});
  writeSlot(w, ItemStack(276, 1, 12));
  writeSlot(w, ItemStack());
  Metadata m;
  m.byte(0, 2);
  m.floatV(6, 20.0f);
  m.string(2, "Paco");
  writeMetadata(w, m);
  BufferReader r(w.data());
  CHECK(readPosition(r) == glm::ivec3(-123, 64, 4567));
  CHECK(readPosition(r) == glm::ivec3(33554431, 255, -33554432));
  CHECK(readSlot(r) == ItemStack(276, 1, 12));
  CHECK(readSlot(r).empty());
  Metadata back = readMetadata(r);
  REQUIRE(back.find(6));
  CHECK(back.find(6)->f == doctest::Approx(20.0f));
  CHECK(back.find(2)->s == "Paco");
  CHECK(r.remaining() == 0);

  CHECK(chatToText(R"({"translate":"chat.type.text","with":["Ana",{"text":"hola"}]})") == "<Ana> hola");
  CHECK(chatToText(R"({"text":"","extra":[{"text":"rojo","color":"red"}]})") == "\xC2\xA7" "crojo");
  CHECK(chatToText(textToChat("¡Hola!")) == "¡Hola!");
  CHECK(toAngle(90.0f) == 64);
}

TEST_CASE("Protocolo 47: una columna de chunk de ida y vuelta") {
  TerrainGenerator gen(99);
  auto c = gen.generate(3, -2);
  u16 mask = 0;
  const std::vector<u8> data = encodeChunkColumn(*c, true, mask);
  CHECK(mask != 0);
  Chunk back(3, -2);
  CHECK(decodeChunkColumn(back, data, mask, true, true) == data.size());
  int diffs = 0;
  for (int y = 0; y < 256; y++)
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) {
        diffs += back.block(x, y, z) != c->block(x, y, z);
        diffs += back.packedLight(x, y, z) != c->packedLight(x, y, z);
      }
  CHECK(diffs == 0);
  CHECK(back.biome(5, 9) == c->biome(5, 9));
}
