#include <doctest/doctest.h>

#include <vector>

#include "core/buffer.h"
#include "core/random.h"
#include "core/zip.h"

using namespace mcw;

TEST_CASE("VarInt: valores de la documentación del protocolo") {
  const std::vector<std::pair<i32, std::vector<u8>>> cases = {
      {0, {0x00}}, {1, {0x01}}, {127, {0x7f}}, {128, {0x80, 0x01}}, {255, {0xff, 0x01}},
      {25565, {0xdd, 0xc7, 0x01}}, {2097151, {0xff, 0xff, 0x7f}}, {2147483647, {0xff, 0xff, 0xff, 0xff, 0x07}},
      {-1, {0xff, 0xff, 0xff, 0xff, 0x0f}}, {-2147483647 - 1, {0x80, 0x80, 0x80, 0x80, 0x08}},
  };
  for (const auto& [value, bytes] : cases) {
    CAPTURE(value);
    BufferWriter w;
    w.varInt(value);
    CHECK(w.data() == bytes);
    CHECK(varIntSize(value) == static_cast<int>(bytes.size()));
    BufferReader r(bytes);
    CHECK(r.varInt() == value);
    CHECK(r.remaining() == 0);
  }
}

TEST_CASE("VarInt de más de 5 bytes es un error") {
  const std::vector<u8> bad = {0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
  BufferReader r(bad);
  CHECK_THROWS_AS(r.varInt(), DecodeError);
}

TEST_CASE("VarLong ida y vuelta") {
  for (i64 v : {i64{0}, i64{1}, i64{127}, i64{128}, INT64_MAX, i64{-1}, INT64_MIN}) {
    BufferWriter w;
    w.varLong(v);
    BufferReader r(w.data());
    CHECK(r.varLong() == v);
  }
}

TEST_CASE("Tipos fijos y strings ida y vuelta") {
  BufferWriter w;
  w.u8(200).i8(-5).boolean(true).i16(-1234).u16(60000).i32(-123456789).i64(-5).f32(1.5f).f64(3.25).string("héllo ⛏");
  BufferReader r(w.data());
  CHECK(r.u8() == 200);
  CHECK(r.i8() == -5);
  CHECK(r.boolean());
  CHECK(r.i16() == -1234);
  CHECK(r.u16() == 60000);
  CHECK(r.i32() == -123456789);
  CHECK(r.i64() == -5);
  CHECK(r.f32() == 1.5f);
  CHECK(r.f64() == 3.25);
  CHECK(r.string() == "héllo ⛏");
  CHECK(r.remaining() == 0);
  CHECK_THROWS_AS(r.u8(), DecodeError);
}

TEST_CASE("zlib ida y vuelta") {
  std::vector<u8> data(10000);
  for (std::size_t i = 0; i < data.size(); i++) data[i] = static_cast<u8>(i * 7 % 13);
  auto c = zlibCompress(data.data(), data.size());
  REQUIRE(!c.empty());
  CHECK(c.size() < data.size());
  auto d1 = zlibDecompress(c.data(), c.size(), data.size());
  REQUIRE(d1);
  CHECK(*d1 == data);
  auto d2 = zlibDecompress(c.data(), c.size());
  REQUIRE(d2);
  CHECK(*d2 == data);
}

TEST_CASE("Random es determinista") {
  Random a(42), b(42), c(43);
  bool differs = false;
  for (int i = 0; i < 16; i++) {
    const u32 x = a.nextU32();
    CHECK(x == b.nextU32());
    differs |= x != c.nextU32();
  }
  CHECK(differs);
  Random r(7);
  for (int i = 0; i < 1000; i++) {
    const int v = r.nextInt(10);
    CHECK(v >= 0);
    CHECK(v < 10);
  }
}

#include "core/font_map.h"

TEST_CASE("Fuente: ñ, tildes, ¿ y ¡ tienen su celda de la fuente de 1.8") {
  using mcw::utf8ToFont;
  CHECK(utf8ToFont("Hola") == "Hola");
  CHECK(utf8ToFont("ñ") == "\xA4");
  CHECK(utf8ToFont("Ñ") == "\xA5");
  CHECK(utf8ToFont("áéíóú") == "\xA0\x82\xA1\xA2\xA3");
  CHECK(utf8ToFont("¿") == "\xA8");
  CHECK(utf8ToFont("¡") == "\xAD");
  CHECK(utf8ToFont("ÁÍÓÚ") == std::string("\x01\x06\x07\x0A"));
  CHECK(utf8ToFont("É") == "\x90");
  CHECK(utf8ToFont("€") == "?");  // sin celda
}
