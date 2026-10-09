#include <doctest/doctest.h>

#include "assets/skin.h"
#include "core/png.h"

using namespace mcw;

namespace {

/// Color único por píxel para seguir adónde va cada uno al convertir.
u32 mark(int x, int y) { return 0xFF000000u | static_cast<u32>((x * 3 + 1) & 255) << 16 | static_cast<u32>((y * 5 + 2) & 255) << 8 | 0x7Fu; }

u32 alpha(u32 argb) { return argb >> 24; }

Image legacySkin() {
  Image img(64, 32, 0);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 64; x++) img.set(x, y, mark(x, y));
  return img;
}

}  // namespace

TEST_CASE("Skins: solo valen 64x64 y 64x32") {
  CHECK(validSkinSize(64, 64));
  CHECK(validSkinSize(64, 32));
  CHECK_FALSE(validSkinSize(32, 32));
  CHECK_FALSE(validSkinSize(128, 128));
  CHECK_FALSE(validSkinSize(64, 48));
  CHECK_FALSE(prepareSkin(Image(100, 100, 0xFFFFFFFF)).has_value());
  CHECK_FALSE(prepareSkin(Image(64, 40, 0xFFFFFFFF)).has_value());
  CHECK_FALSE(prepareSkin(Image()).has_value());
}

TEST_CASE("Skins: una antigua (64x32) pasa a la disposición moderna con las extremidades izquierdas en espejo") {
  const Image raw = legacySkin();
  const auto p = prepareSkin(raw);
  REQUIRE(p.has_value());
  const Image& m = p->image;
  CHECK(m.width == 64);
  CHECK(m.height == 64);
  CHECK_FALSE(p->slim);
  // Lo de arriba queda igual
  CHECK(m.get(10, 10) == raw.get(10, 10));
  CHECK(m.get(45, 25) == raw.get(45, 25));
  // Pierna izquierda: la cara de delante de la derecha (4..7, 20..31) volteada, en (20..23, 52..63)
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 4; x++) CHECK(m.get(20 + x, 52 + y) == raw.get(4 + (3 - x), 20 + y));
  // La derecha de la pierna izquierda es la izquierda de la derecha, volteada
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 4; x++) CHECK(m.get(16 + x, 52 + y) == raw.get(8 + (3 - x), 20 + y));
  // Brazo izquierdo: delante (36..39, 52..63) <- delante del derecho (44..47, 20..31)
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 4; x++) CHECK(m.get(36 + x, 52 + y) == raw.get(44 + (3 - x), 20 + y));
  // Parte de arriba del brazo izquierdo (36..39, 48..51) <- (44..47, 16..19) volteada
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) CHECK(m.get(36 + x, 48 + y) == raw.get(44 + (3 - x), 16 + y));
}

TEST_CASE("Skins: la base se vuelve opaca y la capa exterior conserva su transparencia") {
  Image raw(64, 64, 0);  // todo transparente
  raw.set(8, 8, 0x80FF0000);   // cara (base): medio transparente
  raw.set(40, 8, 0x00112233);  // sombrero (capa exterior): transparente
  raw.set(44, 8, 0x80445566);  // sombrero: medio transparente
  const auto p = prepareSkin(raw);
  REQUIRE(p.has_value());
  CHECK(p->image.get(8, 8) == 0xFFFF0000u);   // opaco, mismo color
  CHECK(alpha(p->image.get(0, 0)) == 255);
  CHECK(alpha(p->image.get(40, 8)) == 0);
  CHECK(p->image.get(44, 8) == 0x80445566u);
  // Las capas de arriba de cada parte (chaqueta, mangas, perneras) siguen transparentes
  CHECK(alpha(p->image.get(20, 40)) == 0);
  CHECK(alpha(p->image.get(44, 40)) == 0);
  CHECK(alpha(p->image.get(4, 40)) == 0);
}

TEST_CASE("Skins: un sombrero sin ningún píxel transparente en una skin antigua no se dibuja") {
  Image raw(64, 32, 0xFF336699);  // todo opaco, también la zona del sombrero
  auto p = prepareSkin(raw);
  REQUIRE(p.has_value());
  CHECK(alpha(p->image.get(40, 8)) == 0);  // sombrero: fuera
  CHECK(alpha(p->image.get(10, 10)) == 255);  // cabeza: sigue
  // Pero si ya traía transparencia, es un sombrero de verdad y se respeta
  raw.set(33, 1, 0);
  p = prepareSkin(raw);
  REQUIRE(p.has_value());
  CHECK(p->image.get(40, 8) == 0xFF336699u);
  CHECK(alpha(p->image.get(33, 1)) == 0);
}

TEST_CASE("Skins: detectar los brazos finos por las columnas que dejan transparentes") {
  Image wide(64, 64, 0xFFAABBCC);
  CHECK_FALSE(detectSlimSkin(wide));
  Image slim = wide;
  for (int y = 20; y < 32; y++)
    for (int x = 54; x < 56; x++) slim.set(x, y, 0);
  CHECK_FALSE(detectSlimSkin(slim));  // falta el brazo izquierdo
  for (int y = 52; y < 64; y++)
    for (int x = 46; x < 48; x++) slim.set(x, y, 0);
  CHECK(detectSlimSkin(slim));
  CHECK(prepareSkin(slim)->slim);
  CHECK_FALSE(prepareSkin(wide)->slim);
  // Se puede forzar el modelo (lo que manda el usuario o el que ya venía decidido)
  CHECK(prepareSkin(wide, true)->slim);
  CHECK_FALSE(prepareSkin(slim, false)->slim);
  // Las antiguas no son nunca de brazos finos
  CHECK_FALSE(detectSlimSkin(Image(64, 32, 0)));
}

TEST_CASE("Skins: las ampliadas (128x128) también se preparan") {
  Image hd(128, 64, 0xFF102030);  // antigua ampliada x2
  const auto p = prepareSkin(hd);
  REQUIRE(p.has_value());
  CHECK(p->image.width == 128);
  CHECK(p->image.height == 128);
  // Pierna izquierda ampliada: (16..31, 48..63) x2
  CHECK(p->image.get(32 + 3, 96 + 10) == 0xFF102030u);
}

TEST_CASE("Skins: la que se ve por defecto sale del UUID (par = Steve, impar = Alex)") {
  CHECK_FALSE(defaultSkinSlim("00000000-0000-0000-0000-000000000000"));
  CHECK(defaultSkinSlim("00000000-0000-0000-0000-000000000001"));
  CHECK_FALSE(defaultSkinSlim("00000000-0000-0000-0000-000000000002"));
  CHECK(defaultSkinSlim("00000001-0000-0000-0000-000000000000"));  // el bit 32 de una mitad
  CHECK_FALSE(defaultSkinSlim("00000001-0000-0000-0000-000100000000"));  // los dos se anulan
  CHECK_FALSE(defaultSkinSlim("no es un uuid"));
  // Con y sin guiones es lo mismo
  CHECK(defaultSkinSlim("00000000000000000000000000000001"));
  // Y es estable: el mismo jugador siempre lleva lo mismo
  CHECK(defaultSkinSlim("25a6e036-1424-3aad-8eb7-dac5960d29b6") == defaultSkinSlim("25a6e036-1424-3aad-8eb7-dac5960d29b6"));
}

TEST_CASE("PNG: tamaño leído de la cabecera, y PNG en memoria de ida y vuelta") {
  Image img(64, 64, 0xFF405060);
  img.set(3, 4, 0x80112233);
  const std::vector<u8> png = encodePng(img);
  REQUIRE(png.size() > 24);
  const auto size = pngSize(png);
  REQUIRE(size.has_value());
  CHECK(size->first == 64);
  CHECK(size->second == 64);
  const auto back = decodePng(png);
  REQUIRE(back.has_value());
  CHECK(back->get(3, 4) == 0x80112233u);
  CHECK(back->get(0, 0) == 0xFF405060u);
  // Lo que no es un PNG se rechaza sin descomprimir nada
  CHECK_FALSE(pngSize(std::vector<u8>{1, 2, 3}).has_value());
  std::vector<u8> bad = png;
  bad[1] = 'X';
  CHECK_FALSE(pngSize(bad).has_value());
  std::vector<u8> huge = png;
  huge[16] = 0xFF;  // ancho absurdo
  CHECK_FALSE(pngSize(huge).has_value());
  CHECK(encodePng(Image()).empty());
}
