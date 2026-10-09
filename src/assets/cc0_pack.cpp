// Pack "libre" (CC0): todo se dibuja aquí con ruido y formas simples. No reproduce ninguna
// textura de Mojang; solo usa los mismos nombres de archivo para encajar con las tablas.
#include "assets/cc0_pack.h"
#include "assets/cc0_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include <stb_easy_font.h>

#include "assets/pack.h"
#include "core/random.h"
#include "data/blockstates.h"

namespace mcw {
namespace cc0 {

using json = nlohmann::json;
const std::string kTex = "assets/minecraft/textures/";

u32 rgb(int r, int g, int b, int a) {
  return (u32(std::clamp(a, 0, 255)) << 24) | (u32(std::clamp(r, 0, 255)) << 16) | (u32(std::clamp(g, 0, 255)) << 8) |
         u32(std::clamp(b, 0, 255));
}
int R(u32 c) { return (c >> 16) & 255; }
int G(u32 c) { return (c >> 8) & 255; }
int Bc(u32 c) { return c & 255; }
u32 shade(u32 c, int d) { return rgb(R(c) + d, G(c) + d, Bc(c) + d, c >> 24); }

u32 seedOf(const std::string& s) { return static_cast<u32>(seedFromString(s)); }

/// Ruido por píxel alrededor de un color base.
Image noisy(u32 base, int amount, u32 seed) {
  Image img(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const int d = static_cast<int>(hash3(x, y, 0, seed) % (2 * amount + 1)) - amount;
      img.set(x, y, shade(base, d));
    }
  return img;
}

/// Manchas de otro color (menas, grava...).
void speckle(Image& img, u32 color, int count, int size, u32 seed) {
  Random r(seed);
  for (int i = 0; i < count; i++) {
    const int cx = r.nextInt(16), cy = r.nextInt(16);
    for (int k = 0; k < size; k++) {
      const int x = (cx + r.nextInt(3) - 1) & 15, y = (cy + r.nextInt(3) - 1) & 15;
      img.set(x, y, shade(color, r.nextInt(30) - 15));
    }
  }
}

Image planks(u32 base, u32 seed) {
  Image img = noisy(base, 6, seed);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      if (y % 4 == 3) img.set(x, y, shade(base, -45));
      else if ((x + (y / 4) * 5) % 8 == 0 && y % 4 == 0) img.set(x, y, shade(base, -35));
      else if (hash3(x, y / 4, 1, seed) % 5 == 0) img.set(x, y, shade(base, -12));
    }
  return img;
}

Image logSide(u32 bark, u32 seed) {
  Image img = noisy(bark, 8, seed);
  for (int x = 0; x < 16; x++) {
    const int stripe = static_cast<int>(hash3(x, 0, 2, seed) % 3);
    for (int y = 0; y < 16; y++)
      if (stripe == 0 || (hash3(x, y, 3, seed) % 7 == 0)) img.set(x, y, shade(bark, -28));
  }
  return img;
}

Image logTop(u32 inner, u32 bark, u32 seed) {
  Image img(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double d = std::hypot(x - 7.5, y - 7.5);
      if (d > 6.8 || x == 0 || x == 15 || y == 0 || y == 15) img.set(x, y, shade(bark, static_cast<int>(hash3(x, y, 4, seed) % 12) - 6));
      else img.set(x, y, shade(inner, (static_cast<int>(d) % 2 == 0 ? -18 : 0) + static_cast<int>(hash3(x, y, 5, seed) % 6)));
    }
  return img;
}

/// Hojas en gris (se tiñen con el color del bioma), con huecos para el modo cutout.
Image leaves(u32 seed) {
  Image img(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const u32 h = hash3(x, y, 6, seed);
      if (h % 9 == 0) img.set(x, y, 0);
      else img.set(x, y, shade(rgb(150, 150, 150), static_cast<int>(h % 70) - 40));
    }
  return img;
}

/// Hojas de hierba en gris para teñir.
Image grassBlades(u32 seed, bool fern) {
  Image img(16, 16, 0);
  Random r(seed);
  for (int i = 0; i < (fern ? 5 : 9); i++) {
    const int x0 = 1 + r.nextInt(14), h = 6 + r.nextInt(fern ? 9 : 10);
    int x = x0;
    for (int y = 15; y > 15 - h && y >= 0; y--) {
      img.set(std::clamp(x, 0, 15), y, shade(rgb(160, 160, 160), r.nextInt(50) - 25));
      if (fern && y % 3 == 0) {
        img.set(std::clamp(x - 1, 0, 15), y, shade(rgb(140, 140, 140), -10));
        img.set(std::clamp(x + 1, 0, 15), y, shade(rgb(140, 140, 140), -10));
      }
      if (r.nextInt(4) == 0) x += (x < 8) ? -1 : 1;
    }
  }
  return img;
}

Image flower(u32 petal, u32 center, u32 seed, int stemHeight) {
  Image img(16, 16, 0);
  const u32 stem = rgb(58, 120, 40);
  for (int y = 15; y > 15 - stemHeight; y--) img.set(8, y, stem);
  img.set(9, 12, stem);
  img.set(7, 13, stem);
  const int cy = 15 - stemHeight - 1;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      if (std::abs(dx) + std::abs(dy) > 3) continue;
      const int x = 8 + dx, y = cy + dy;
      if (y < 0) continue;
      img.set(x, y, (dx == 0 && dy == 0) ? center : shade(petal, static_cast<int>(hash3(x, y, 7, seed) % 30) - 15));
    }
  return img;
}

Image mushroom(u32 cap) {
  Image img(16, 16, 0);
  for (int y = 10; y < 16; y++) { img.set(7, y, rgb(215, 205, 180)); img.set(8, y, rgb(200, 190, 165)); }
  for (int y = 6; y < 10; y++)
    for (int x = 4; x < 12; x++)
      if (!(y == 6 && (x == 4 || x == 11))) img.set(x, y, shade(cap, (x + y) % 3 == 0 ? 20 : 0));
  return img;
}

Image sapling(u32 leaf) {
  Image img(16, 16, 0);
  for (int y = 9; y < 16; y++) img.set(8, y, rgb(100, 72, 40));
  for (int y = 1; y < 10; y++)
    for (int x = 3; x < 13; x++)
      if (std::hypot(x - 8, y - 5) < 4.5 && hash3(x, y, 8, leaf) % 4) img.set(x, y, shade(leaf, static_cast<int>(hash3(x, y, 9, leaf) % 40) - 20));
  return img;
}

Image deadBush() {
  Image img(16, 16, 0);
  const u32 c = rgb(125, 85, 40);
  for (int y = 6; y < 16; y++) img.set(8, y, c);
  for (int i = 0; i < 5; i++) { img.set(8 - i, 10 - i, c); img.set(8 + i, 11 - i, c); img.set(8 + i / 2, 7 - i, c); }
  return img;
}

Image reeds() {
  Image img(16, 16, 0);
  for (int x : {3, 8, 12})
    for (int y = 0; y < 16; y++) img.set(x, y, (y % 5 == 0) ? rgb(200, 200, 200) : rgb(170, 170, 170));
  return img;
}

Image glassTex(u32 tint, int alpha) {
  Image img(16, 16, 0);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      if (x == 0 || y == 0 || x == 15 || y == 15) img.set(x, y, (tint & 0xFFFFFF) | 0xFF000000);
      else if (x == y + 3 || x == y + 4) img.set(x, y, rgb(255, 255, 255, 170));
      else if (alpha > 0) img.set(x, y, (tint & 0xFFFFFF) | (u32(alpha) << 24));
    }
  return img;
}

Image torchTex() {
  Image img(16, 16, 0);
  for (int y = 8; y < 16; y++) { img.set(7, y, rgb(110, 80, 45)); img.set(8, y, rgb(90, 65, 35)); }
  img.set(7, 6, rgb(255, 230, 120)); img.set(8, 6, rgb(255, 200, 80));
  img.set(7, 7, rgb(255, 170, 60)); img.set(8, 7, rgb(240, 140, 40));
  return img;
}

Image cactusSide() {
  Image img = noisy(rgb(40, 125, 45), 10, 11);
  for (int y = 0; y < 16; y++) {
    img.set(0, y, 0); img.set(15, y, 0);
    img.set(1, y, rgb(25, 90, 30)); img.set(14, y, rgb(25, 90, 30));
    if (y % 4 == 1) { img.set(3, y, rgb(220, 220, 190)); img.set(11, y + 2 < 16 ? y + 2 : y, rgb(220, 220, 190)); }
  }
  return img;
}

Image fluid(u32 base, int amount, u32 seed, int frame) {
  Image img(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double w = std::sin((x + frame) * 0.8) + std::cos((y + frame * 0.5) * 0.6);
      img.set(x, y, shade(base, static_cast<int>(w * amount) + static_cast<int>(hash3(x, y, frame, seed) % 5)));
    }
  return img;
}

/// Tira vertical de frames de animación.
Image strip(const std::vector<Image>& frames) {
  Image img(16, 16 * static_cast<int>(frames.size()));
  for (std::size_t f = 0; f < frames.size(); f++)
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) img.set(x, static_cast<int>(f) * 16 + y, frames[f].get(x, y));
  return img;
}

/// Colores base por nombre de textura para los bloques "simples".
u32 baseColor(const std::string& n) {
  static const std::map<std::string, u32> colors = {
      {"stone", rgb(125, 125, 125)}, {"stone_granite", rgb(150, 105, 85)}, {"stone_granite_smooth", rgb(160, 112, 92)},
      {"stone_diorite", rgb(190, 190, 192)}, {"stone_diorite_smooth", rgb(198, 198, 200)}, {"stone_andesite", rgb(132, 134, 133)},
      {"stone_andesite_smooth", rgb(138, 140, 139)}, {"dirt", rgb(134, 96, 67)}, {"coarse_dirt", rgb(119, 85, 59)},
      {"cobblestone", rgb(120, 120, 120)}, {"bedrock", rgb(80, 80, 80)}, {"sand", rgb(219, 208, 160)},
      {"red_sand", rgb(190, 102, 33)}, {"gravel", rgb(135, 128, 126)}, {"clay", rgb(160, 166, 179)},
      {"snow", rgb(240, 250, 250)}, {"ice", rgb(145, 185, 250)}, {"ice_packed", rgb(160, 188, 240)},
      {"obsidian", rgb(20, 18, 30)}, {"netherrack", rgb(110, 50, 50)}, {"soul_sand", rgb(84, 64, 51)},
      {"glowstone", rgb(220, 180, 100)}, {"end_stone", rgb(220, 222, 158)}, {"brick", rgb(150, 90, 75)},
      {"nether_brick", rgb(45, 22, 26)}, {"hardened_clay", rgb(150, 92, 66)}, {"sponge", rgb(195, 192, 74)},
      {"sponge_wet", rgb(160, 160, 60)}, {"cobblestone_mossy", rgb(105, 125, 100)}, {"stonebrick", rgb(122, 121, 122)},
      {"stonebrick_mossy", rgb(110, 120, 100)}, {"stonebrick_cracked", rgb(118, 117, 118)}, {"stonebrick_carved", rgb(118, 118, 118)},
      {"quartz_block_side", rgb(235, 230, 222)}, {"quartz_block_top", rgb(238, 233, 226)}, {"quartz_block_chiseled", rgb(230, 225, 216)},
      {"quartz_block_chiseled_top", rgb(233, 228, 220)}, {"quartz_block_bottom", rgb(232, 227, 218)},
      {"gold_block", rgb(250, 215, 60)}, {"iron_block", rgb(220, 220, 220)}, {"diamond_block", rgb(100, 225, 215)},
      {"emerald_block", rgb(80, 210, 110)}, {"lapis_block", rgb(38, 67, 138)}, {"coal_block", rgb(25, 25, 25)},
      {"sea_lantern", rgb(172, 199, 190)}, {"slime", rgb(110, 190, 90)}, {"web", rgb(220, 220, 220)},
      {"mycelium_top", rgb(111, 99, 105)}, {"dirt_podzol_top", rgb(92, 63, 25)},
  };
  auto it = colors.find(n);
  return it == colors.end() ? rgb(128, 128, 128) : it->second;
}

const std::map<std::string, u32>& dyeColors() {
  static const std::map<std::string, u32> c = {
      {"white", rgb(222, 222, 222)}, {"orange", rgb(219, 125, 62)}, {"magenta", rgb(179, 80, 188)},
      {"light_blue", rgb(107, 138, 201)}, {"yellow", rgb(177, 166, 39)}, {"lime", rgb(65, 174, 56)},
      {"pink", rgb(208, 132, 153)}, {"gray", rgb(64, 64, 64)}, {"silver", rgb(154, 161, 161)},
      {"cyan", rgb(46, 110, 137)}, {"purple", rgb(126, 61, 181)}, {"blue", rgb(46, 56, 141)},
      {"brown", rgb(79, 50, 31)}, {"green", rgb(53, 70, 27)}, {"red", rgb(150, 52, 48)}, {"black", rgb(25, 22, 22)},
  };
  return c;
}

const std::map<std::string, std::pair<u32, u32>>& woods() {  // (tablones, corteza)
  static const std::map<std::string, std::pair<u32, u32>> w = {
      {"oak", {rgb(162, 130, 78), rgb(102, 81, 50)}},     {"spruce", {rgb(115, 85, 49), rgb(58, 37, 16)}},
      {"birch", {rgb(196, 179, 123), rgb(216, 215, 210)}}, {"jungle", {rgb(160, 115, 80), rgb(87, 67, 26)}},
      {"acacia", {rgb(168, 90, 50), rgb(103, 96, 86)}},    {"dark_oak", {rgb(66, 43, 20), rgb(60, 46, 26)}},
  };
  return w;
}

void Builder::ensureSimple(const std::string& name) {
    if (textures_.count(name)) return;
    Image img = noisy(baseColor(name), 10, seedOf(name));
    if (name == "gravel") speckle(img, rgb(95, 90, 88), 14, 4, 3);
    if (name == "cobblestone" || name == "cobblestone_mossy") {
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
          if ((hash3(x / 4, y / 4, 12, 1) + x + y) % 7 == 0) img.set(x, y, shade(baseColor(name), -35));
      if (name == "cobblestone_mossy") speckle(img, rgb(70, 110, 55), 10, 5, 13);
    }
    if (name.rfind("stonebrick", 0) == 0 || name == "brick" || name == "nether_brick") {
      const u32 mortar = name == "brick" ? rgb(170, 160, 150) : shade(baseColor(name), -30);
      const int bh = name == "brick" ? 4 : 8;
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
          if (y % bh == bh - 1 || ((x + ((y / bh) % 2) * (bh == 4 ? 4 : 8)) % (bh == 4 ? 8 : 16)) == 0) img.set(x, y, mortar);
      if (name == "stonebrick_mossy") speckle(img, rgb(70, 110, 55), 8, 5, 14);
    }
    if (name == "bedrock") speckle(img, rgb(40, 40, 40), 18, 5, 15);
    if (name == "obsidian") speckle(img, rgb(60, 40, 90), 8, 3, 16);
    if (name == "glowstone") speckle(img, rgb(255, 235, 170), 12, 3, 17);
    tex(name, std::move(img));
}

json cubeAll(const std::string& t) { return {{"parent", "block/cube_all"}, {"textures", {{"all", "blocks/" + t}}}}; }
json column(const std::string& end, const std::string& side) {
  return {{"parent", "block/cube_column"}, {"textures", {{"end", "blocks/" + end}, {"side", "blocks/" + side}}}};
}
json bottomTop(const std::string& bottom, const std::string& top, const std::string& side) {
  return {{"parent", "block/cube_bottom_top"},
          {"textures", {{"bottom", "blocks/" + bottom}, {"top", "blocks/" + top}, {"side", "blocks/" + side}}}};
}
json cross(const std::string& t, bool tinted) {
  return {{"parent", tinted ? "block/tinted_cross" : "block/cross"}, {"textures", {{"cross", "blocks/" + t}}}};
}

json face(const std::string& tex, const char* cull, int tint, std::array<int, 4> uv) {
  json f = {{"texture", tex}, {"uv", uv}};
  if (cull) f["cullface"] = cull;
  if (tint >= 0) f["tintindex"] = tint;
  return f;
}

void baseModels(Builder& b) {
  json cubeFaces;
  for (const char* f : {"down", "up", "north", "south", "west", "east"}) cubeFaces[f] = face(std::string("#") + f, f);
  b.model("cube", {{"elements", json::array({{{"from", {0, 0, 0}}, {"to", {16, 16, 16}}, {"faces", cubeFaces}}})}});
  b.model("cube_all", {{"parent", "block/cube"},
                       {"textures", {{"particle", "#all"}, {"down", "#all"}, {"up", "#all"}, {"north", "#all"}, {"south", "#all"}, {"west", "#all"}, {"east", "#all"}}}});
  b.model("cube_column", {{"parent", "block/cube"},
                          {"textures", {{"particle", "#side"}, {"down", "#end"}, {"up", "#end"}, {"north", "#side"}, {"south", "#side"}, {"west", "#side"}, {"east", "#side"}}}});
  b.model("cube_bottom_top", {{"parent", "block/cube"},
                              {"textures", {{"particle", "#side"}, {"down", "#bottom"}, {"up", "#top"}, {"north", "#side"}, {"south", "#side"}, {"west", "#side"}, {"east", "#side"}}}});
  json leafFaces;
  for (const char* f : {"down", "up", "north", "south", "west", "east"}) leafFaces[f] = face("#all", nullptr, 0);
  b.model("leaves", {{"elements", json::array({{{"from", {0, 0, 0}}, {"to", {16, 16, 16}}, {"faces", leafFaces}}})}});
  auto crossModel = [&](int tint) {
    json el1 = {{"from", {0.8, 0, 8}}, {"to", {15.2, 16, 8}}, {"shade", false},
                {"rotation", {{"origin", {8, 8, 8}}, {"axis", "y"}, {"angle", 45}, {"rescale", true}}},
                {"faces", {{"north", face("#cross", nullptr, tint)}, {"south", face("#cross", nullptr, tint)}}}};
    json el2 = {{"from", {8, 0, 0.8}}, {"to", {8, 16, 15.2}}, {"shade", false},
                {"rotation", {{"origin", {8, 8, 8}}, {"axis", "y"}, {"angle", 45}, {"rescale", true}}},
                {"faces", {{"west", face("#cross", nullptr, tint)}, {"east", face("#cross", nullptr, tint)}}}};
    return json{{"ambientocclusion", false}, {"elements", json::array({el1, el2})}};
  };
  b.model("cross", crossModel(-1));
  b.model("tinted_cross", crossModel(0));
}

}  // namespace cc0


// ---------------------------------------------------------------------------
// Interfaz: paneles con bisel, casillas, barra rápida, botones, corazones...
// Mismas posiciones que el formato de las texturas de GUI (256x256) para que el código de
// interfaz funcione igual con este pack y con cualquier resource pack.
// ---------------------------------------------------------------------------
namespace cc0 {

void fillRect(Image& img, int x, int y, int w, int h, u32 c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      if (xx >= 0 && yy >= 0 && xx < img.width && yy < img.height) img.set(xx, yy, c);
}

void panel(Image& img, int w, int h) {
  fillRect(img, 1, 1, w - 2, h - 2, rgb(198, 198, 198));
  fillRect(img, 3, 0, w - 6, 1, rgb(0, 0, 0));
  fillRect(img, 3, h - 1, w - 6, 1, rgb(0, 0, 0));
  fillRect(img, 0, 3, 1, h - 6, rgb(0, 0, 0));
  fillRect(img, w - 1, 3, 1, h - 6, rgb(0, 0, 0));
  img.set(1, 2, rgb(0, 0, 0)); img.set(2, 1, rgb(0, 0, 0));
  img.set(w - 2, 2, rgb(0, 0, 0)); img.set(w - 3, 1, rgb(0, 0, 0));
  img.set(1, h - 3, rgb(0, 0, 0)); img.set(2, h - 2, rgb(0, 0, 0));
  img.set(w - 2, h - 3, rgb(0, 0, 0)); img.set(w - 3, h - 2, rgb(0, 0, 0));
  fillRect(img, 3, 1, w - 6, 2, rgb(255, 255, 255));
  fillRect(img, 1, 3, 2, h - 6, rgb(255, 255, 255));
  fillRect(img, 3, h - 3, w - 6, 2, rgb(85, 85, 85));
  fillRect(img, w - 3, 3, 2, h - 6, rgb(85, 85, 85));
}

/// Casilla de inventario: el ítem va en (x, y); el marco ocupa (x-1, y-1) 18x18.
void slotFrame(Image& img, int x, int y, int size = 18) {
  const int x0 = x - 1 - (size - 18) / 2, y0 = y - 1 - (size - 18) / 2;
  fillRect(img, x0, y0, size, size, rgb(139, 139, 139));
  fillRect(img, x0, y0, size - 1, 1, rgb(55, 55, 55));
  fillRect(img, x0, y0, 1, size - 1, rgb(55, 55, 55));
  fillRect(img, x0 + 1, y0 + size - 1, size - 1, 1, rgb(255, 255, 255));
  fillRect(img, x0 + size - 1, y0 + 1, 1, size - 1, rgb(255, 255, 255));
}

void playerSlots(Image& img, int invY, int hotbarY) {
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 9; c++) slotFrame(img, 8 + c * 18, invY + r * 18);
  for (int c = 0; c < 9; c++) slotFrame(img, 8 + c * 18, hotbarY);
}

/// Hueco hundido (la barra de desplazamiento, el campo de búsqueda): oscuro arriba y a la izquierda, claro abajo y a la derecha.
void groove(Image& img, int x, int y, int w, int h, u32 face = 0xFF8B8B8B) {
  fillRect(img, x, y, w, h, face);
  fillRect(img, x, y, w - 1, 1, rgb(55, 55, 55));
  fillRect(img, x, y, 1, h - 1, rgb(55, 55, 55));
  fillRect(img, x + 1, y + h - 1, w - 1, 1, rgb(255, 255, 255));
  fillRect(img, x + w - 1, y + 1, 1, h - 1, rgb(255, 255, 255));
}

/// Pestaña del inventario creativo (28x32). Las de arriba tienen las esquinas redondeadas arriba y las de abajo, abajo;
/// los últimos 4 píxeles se meten bajo la ventana: la pestaña elegida los pinta del color de la ventana para unirse a ella
/// (dejando ver el borde de la ventana en las columnas de los lados, salvo en las de la derecha del todo, que asoman 2
/// píxeles por fuera de la ventana y cierran su borde).
void creativeTabSprite(Image& img, int x, int y, bool onTop, bool selected, bool leftmost, bool rightmost) {
  const u32 outline = rgb(0, 0, 0);
  const u32 face = selected ? rgb(198, 198, 198) : rgb(139, 139, 139);
  const u32 light = selected ? rgb(255, 255, 255) : rgb(190, 190, 190);
  const u32 shade = selected ? rgb(85, 85, 85) : rgb(72, 72, 72);
  // Se dibuja como una pestaña de arriba y, si es de abajo, se refleja en vertical al final
  Image t(28, 32, 0);
  fillRect(t, 1, 1, 26, 31, face);
  fillRect(t, 2, 0, 24, 1, outline);                              // arriba
  fillRect(t, 0, 2, 1, 30, outline);                              // izquierda
  fillRect(t, 27, 2, 1, 30, outline);                             // derecha
  t.set(1, 1, outline);
  t.set(26, 1, outline);
  fillRect(t, 2, 1, 24, 2, light);                                // luz arriba
  fillRect(t, 1, 2, 2, 28, light);                                // y a la izquierda
  fillRect(t, 24, 3, 2, 28, shade);                               // sombra a la derecha
  t.set(2, 1, light);
  t.set(25, 1, shade);
  if (selected) {
    // Abierta por abajo: se une a la ventana
    fillRect(t, 0, 28, 28, 4, 0);
    fillRect(t, 1, 28, 26, 4, face);
    if (leftmost) fillRect(t, 0, 28, 1, 3, outline);  // (la esquina de la ventana es redonda: el borde de la pestaña baja hasta el suyo)
    if (rightmost) {
      fillRect(t, 27, 28, 1, 4, outline);   // el borde de la derecha sigue hasta abajo, por fuera de la ventana
      fillRect(t, 26, 28, 1, 4, face);
    }
  } else {
    fillRect(t, 1, 31, 26, 1, outline);                           // cerrada por abajo
    fillRect(t, 1, 30, 25, 1, shade);
  }
  for (int yy = 0; yy < 32; yy++)
    for (int xx = 0; xx < 28; xx++) img.set(x + xx, y + (onTop ? yy : 31 - yy), t.get(xx, yy));
}

/// Tirador de la barra de desplazamiento del creativo (12x15), con tres rayas de agarre.
void scrollHandle(Image& img, int x, int y, bool enabled) {
  const u32 face = enabled ? rgb(198, 198, 198) : rgb(139, 139, 139);
  fillRect(img, x, y, 12, 15, rgb(0, 0, 0));
  fillRect(img, x + 1, y + 1, 10, 13, face);
  fillRect(img, x + 1, y + 1, 10, 1, enabled ? rgb(255, 255, 255) : rgb(170, 170, 170));
  fillRect(img, x + 1, y + 1, 1, 13, enabled ? rgb(255, 255, 255) : rgb(170, 170, 170));
  fillRect(img, x + 2, y + 13, 9, 1, rgb(85, 85, 85));
  fillRect(img, x + 10, y + 2, 1, 12, rgb(85, 85, 85));
  if (enabled)
    for (int i = 0; i < 3; i++) fillRect(img, x + 3, y + 4 + i * 3, 6, 1, rgb(110, 110, 110));
}

/// Ventana del inventario creativo (195x136): 0 = objetos, 1 = búsqueda, 2 = inventario de supervivencia.
void creativeWindow(Image& img, int kind) {
  panel(img, 195, 136);
  for (int c = 0; c < 9; c++) slotFrame(img, 9 + c * 18, 112);  // la barra rápida
  if (kind == 2) {
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 9; c++) slotFrame(img, 9 + c * 18, 54 + r * 18);
    slotFrame(img, 9, 6);   // armadura a los lados del jugador
    slotFrame(img, 9, 33);
    slotFrame(img, 63, 6);
    slotFrame(img, 63, 33);
    groove(img, 28, 5, 34, 47, rgb(0, 0, 0));  // el recuadro del jugador
    slotFrame(img, 173, 112);                   // la papelera
    const u32 lid = rgb(70, 70, 70), body = rgb(112, 112, 112), slit = rgb(60, 60, 60);
    fillRect(img, 176, 114, 4, 1, lid);
    fillRect(img, 174, 115, 8, 1, lid);
    fillRect(img, 175, 117, 6, 8, body);
    for (int i = 0; i < 3; i++) fillRect(img, 176 + i * 2, 118, 1, 6, slit);
    return;
  }
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 9; c++) slotFrame(img, 9 + c * 18, 18 + r * 18);
  groove(img, 174, 17, 14, 114);  // el carril de la barra de desplazamiento
  if (kind == 1) {
    groove(img, 80, 4, 93, 13, rgb(0, 0, 0));  // el campo de búsqueda
  }
}

void arrowShape(Image& img, int x, int y, u32 c) {
  fillRect(img, x, y + 6, 16, 4, c);
  for (int i = 0; i < 8; i++) fillRect(img, x + 15 + i, y + i, 1, 16 - 2 * i, c);
}

void flameShape(Image& img, int x, int y, u32 c, u32 core) {
  for (int yy = 0; yy < 14; yy++)
    for (int xx = 0; xx < 14; xx++) {
      const double dx = (xx - 6.5) / 6.5, dy = (yy - 9.0) / 9.0;
      if (dx * dx + dy * dy * (yy < 9 ? 0.45 : 1.0) < 1.0 - (yy < 5 ? (5 - yy) * 0.12 : 0)) img.set(x + xx, y + yy, c);
      if (std::abs(xx - 6.5) < 3 && yy > 6 && yy < 13) img.set(x + xx, y + yy, core);
    }
}

void heart(Image& img, int x, int y, u32 fill, u32 outline, bool half) {
  static const char* kShape[9] = {" XX XX  ", "XooXooX ", "XoooooX ", "XoooooX ", " XoooX  ", "  XoX   ", "   X    ", "        ", "        "};
  for (int yy = 0; yy < 9; yy++)
    for (int xx = 0; xx < 8; xx++) {
      const char ch = kShape[yy][xx];
      if (ch == 'X') img.set(x + xx + 1, y + yy + 1, outline);
      else if (ch == 'o' && (!half || xx < 4)) img.set(x + xx + 1, y + yy + 1, fill);
    }
}

/// Icono de armadura de 9x9 (una pechera): contorno, y relleno entero o solo la mitad izquierda.
void armorIcon(Image& img, int x, int y, u32 fill, u32 outline, bool half) {
  static const char* kShape[9] = {"XX.....XX", "XooX.XooX", "XoooXoooX", ".XooooooX", ".XooooooX", ".XooooooX", ".XooooooX", "..XoooooX", "...XXXXX."};
  for (int yy = 0; yy < 9; yy++)
    for (int xx = 0; xx < 9; xx++) {
      const char ch = kShape[yy][xx];
      if (ch == 'X') img.set(x + xx, y + yy, outline);
      else if (ch == 'o' && (!half || xx < 5)) img.set(x + xx, y + yy, fill);
    }
}

void drumstick(Image& img, int x, int y, u32 fill, u32 outline, bool half) {
  for (int yy = 0; yy < 9; yy++)
    for (int xx = 0; xx < 9; xx++) {
      const double d = std::hypot(xx - 5.5, yy - 3.5);
      const bool bone = (xx + yy == 9 || xx + yy == 10) && xx < 4;
      if (half && xx < 4 && !bone) continue;
      if (d < 3.2) img.set(x + xx, y + yy, d > 2.3 ? outline : fill);
      else if (bone) img.set(x + xx, y + yy, rgb(235, 235, 220));
    }
}

/// Caja de una opción de la mesa de encantamientos (108x19): borde oscuro, relleno y luz arriba a la izquierda.
void optionBox(Image& img, int x, int y, u32 face, u32 hi, u32 lo) {
  fillRect(img, x, y, 108, 19, rgb(8, 7, 6));
  fillRect(img, x + 1, y + 1, 106, 17, face);
  fillRect(img, x + 1, y + 1, 106, 1, hi);
  fillRect(img, x + 1, y + 1, 1, 17, hi);
  fillRect(img, x + 1, y + 17, 106, 1, lo);
  fillRect(img, x + 106, y + 2, 1, 16, lo);
}

/// Icono de nivel de una opción: una gema redonda con 1, 2 o 3 (verde si se puede pagar, gris si no).
void enchantLevelIcon(Image& img, int x, int y, int n, bool on) {
  static const char* kDigit[3][5] = {{" X ", "XX ", " X ", " X ", "XXX"}, {"XXX", "  X", "XXX", "X  ", "XXX"}, {"XXX", "  X", "XXX", "  X", "XXX"}};
  for (int yy = 0; yy < 16; yy++)
    for (int xx = 0; xx < 16; xx++) {
      const double d = std::hypot(xx - 7.5, yy - 7.5);
      if (d > 7.4) continue;
      const bool rim = d > 6.2;
      const bool lit = (xx + yy) < 13;
      u32 c = on ? (rim ? rgb(24, 90, 30) : lit ? rgb(96, 214, 84) : rgb(52, 160, 56)) : (rim ? rgb(40, 40, 40) : lit ? rgb(98, 98, 98) : rgb(70, 70, 70));
      img.set(x + xx, y + yy, c);
    }
  const u32 ink = on ? rgb(255, 255, 255) : rgb(160, 160, 160);
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 3; c++)
      if (kDigit[n - 1][r][c] == 'X') fillRect(img, x + 5 + c * 2, y + 3 + r * 2, 2, 2, ink);
}

void addGuiTextures(MemoryPack& pack) {
  // --- widgets.png: barra rápida, selección y botones ---
  Image w(256, 256, 0);
  fillRect(w, 0, 0, 182, 22, rgb(40, 40, 40, 200));
  for (int i = 0; i < 9; i++) {
    fillRect(w, 1 + i * 20, 1, 20, 20, rgb(110, 110, 110, 150));
    fillRect(w, 3 + i * 20, 3, 16, 16, rgb(60, 60, 60, 150));
  }
  for (int i = 0; i < 24; i++) {
    w.set(i, 22, 0xFFFFFFFF); w.set(i, 45, 0xFFFFFFFF); w.set(0, 22 + i, 0xFFFFFFFF); w.set(23, 22 + i, 0xFFFFFFFF);
    w.set(i, 23, rgb(200, 200, 200)); w.set(i, 44, rgb(200, 200, 200)); w.set(1, 22 + i, rgb(200, 200, 200)); w.set(22, 22 + i, rgb(200, 200, 200));
  }
  auto buttonTex = [&](int y, u32 face, u32 hi, u32 lo) {
    fillRect(w, 0, y, 200, 20, rgb(0, 0, 0));
    fillRect(w, 1, y + 1, 198, 18, face);
    fillRect(w, 1, y + 1, 198, 1, hi);
    fillRect(w, 1, y + 1, 1, 17, hi);
    fillRect(w, 1, y + 17, 198, 2, lo);
    fillRect(w, 198, y + 2, 1, 16, lo);
  };
  buttonTex(46, rgb(44, 44, 44), rgb(60, 60, 60), rgb(30, 30, 30));      // desactivado
  buttonTex(66, rgb(111, 111, 111), rgb(170, 170, 170), rgb(70, 70, 70));  // normal
  buttonTex(86, rgb(126, 136, 191), rgb(190, 200, 250), rgb(80, 90, 140)); // encima
  pack.putImage(kTex + "gui/widgets.png", w);

  // --- icons.png: punto de mira, corazones, comida, aire, experiencia ---
  Image ic(256, 256, 0);
  for (int i = 0; i < 9; i++) { ic.set(7, 3 + i, 0xFFFFFFFF); ic.set(3 + i, 7, 0xFFFFFFFF); }
  heart(ic, 16, 0, rgb(0, 0, 0, 0), rgb(0, 0, 0), false);          // contenedor
  heart(ic, 25, 0, rgb(0, 0, 0, 0), rgb(255, 255, 255), false);    // contenedor al recibir daño
  heart(ic, 52, 0, rgb(220, 20, 20), rgb(0, 0, 0, 0), false);      // lleno
  heart(ic, 61, 0, rgb(220, 20, 20), rgb(0, 0, 0, 0), true);       // medio
  armorIcon(ic, 16, 9, rgb(0, 0, 0, 0), rgb(0, 0, 0), false);       // armadura vacía
  armorIcon(ic, 25, 9, rgb(235, 235, 240), rgb(0, 0, 0), true);       // media
  armorIcon(ic, 34, 9, rgb(235, 235, 240), rgb(0, 0, 0), false);      // llena
  drumstick(ic, 16, 27, rgb(0, 0, 0, 0), rgb(0, 0, 0), false);
  drumstick(ic, 52, 27, rgb(200, 130, 70), rgb(120, 60, 20), false);
  drumstick(ic, 61, 27, rgb(200, 130, 70), rgb(120, 60, 20), true);
  for (int yy = 0; yy < 9; yy++)
    for (int xx = 0; xx < 9; xx++) {
      const double d = std::hypot(xx - 4, yy - 4);
      if (d < 4) ic.set(16 + xx, 18 + yy, d > 3 ? rgb(30, 60, 160) : rgb(120, 170, 255));
      if (d < 4 && d > 3) ic.set(25 + xx, 18 + yy, rgb(120, 170, 255));
    }
  fillRect(ic, 0, 64, 182, 5, rgb(30, 30, 30));
  fillRect(ic, 1, 65, 180, 3, rgb(60, 60, 60));
  fillRect(ic, 0, 69, 182, 5, rgb(30, 60, 10));
  fillRect(ic, 1, 70, 180, 3, rgb(128, 255, 32));
  pack.putImage(kTex + "gui/icons.png", ic);

  // --- Ventanas ---
  Image inv(256, 256, 0);
  panel(inv, 176, 166);
  for (int i = 0; i < 4; i++) slotFrame(inv, 8, 8 + i * 18);
  fillRect(inv, 26, 8, 49, 70, rgb(0, 0, 0));
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 2; c++) slotFrame(inv, 98 + c * 18, 18 + r * 18);
  arrowShape(inv, 134, 28, rgb(139, 139, 139));
  slotFrame(inv, 154, 28);
  playerSlots(inv, 84, 142);
  pack.putImage(kTex + "gui/container/inventory.png", inv);

  Image ct(256, 256, 0);
  panel(ct, 176, 166);
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) slotFrame(ct, 30 + c * 18, 17 + r * 18);
  arrowShape(ct, 90, 35, rgb(139, 139, 139));
  slotFrame(ct, 124, 35, 26);
  playerSlots(ct, 84, 142);
  pack.putImage(kTex + "gui/container/crafting_table.png", ct);

  Image fu(256, 256, 0);
  panel(fu, 176, 166);
  slotFrame(fu, 56, 17);
  slotFrame(fu, 56, 53);
  slotFrame(fu, 116, 35, 26);
  arrowShape(fu, 79, 34, rgb(139, 139, 139));
  flameShape(fu, 56, 36, rgb(139, 139, 139), rgb(139, 139, 139));
  flameShape(fu, 176, 0, rgb(255, 150, 20), rgb(255, 230, 90));  // llama encendida (sprite)
  arrowShape(fu, 176, 14, rgb(255, 255, 255));                   // flecha de progreso (sprite)
  playerSlots(fu, 84, 142);
  pack.putImage(kTex + "gui/container/furnace.png", fu);

  // Mesa de encantamientos. Abajo a la izquierda, como en la ventana de 1.8: las tres opciones (normal en
  // y=166, apagada en y=185 y con el ratón encima en y=204, de 108x19) y los iconos de nivel (encendidos en
  // y=223 y apagados en y=239, de 16x16, uno por opción)
  Image en(256, 256, 0);
  panel(en, 176, 166);
  fillRect(en, 12, 14, 40, 30, rgb(30, 24, 44));  // la ventanita donde flota el libro
  fillRect(en, 12, 14, 40, 1, rgb(10, 8, 16));
  fillRect(en, 12, 14, 1, 30, rgb(10, 8, 16));
  fillRect(en, 12, 43, 40, 1, rgb(255, 255, 255));
  fillRect(en, 51, 14, 1, 30, rgb(255, 255, 255));
  slotFrame(en, 15, 47);
  slotFrame(en, 35, 47);
  for (int i = 0; i < 3; i++) optionBox(en, 60, 14 + 19 * i, rgb(48, 44, 40), rgb(40, 37, 34), rgb(30, 28, 26));  // hueco de fondo
  playerSlots(en, 84, 142);
  optionBox(en, 0, 166, rgb(96, 86, 72), rgb(140, 126, 104), rgb(58, 52, 44));
  optionBox(en, 0, 185, rgb(52, 48, 44), rgb(70, 65, 60), rgb(34, 31, 28));
  optionBox(en, 0, 204, rgb(112, 100, 138), rgb(170, 160, 204), rgb(72, 64, 96));
  for (int i = 0; i < 3; i++) {
    enchantLevelIcon(en, 16 * i, 223, i + 1, true);
    enchantLevelIcon(en, 16 * i, 239, i + 1, false);
  }
  pack.putImage(kTex + "gui/container/enchanting_table.png", en);

  Image ch(256, 256, 0);
  panel(ch, 176, 222);
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < 9; c++) slotFrame(ch, 8 + c * 18, 18 + r * 18);
  playerSlots(ch, 140, 198);
  pack.putImage(kTex + "gui/container/generic_54.png", ch);

  // Inventario creativo: la ventana de cada tipo de pestaña, y las pestañas con la barra de desplazamiento (tabs.png:
  // arriba sin elegir y elegida a y=0 y y=32, abajo a y=64 y y=96, de 28x32 y en columnas de 28; el tirador a x=232 y x=244)
  const char* const kCreativeWindows[3] = {"tab_items", "tab_item_search", "tab_inventory"};
  for (int k = 0; k < 3; k++) {
    Image win(256, 256, 0);
    creativeWindow(win, k);
    pack.putImage(kTex + "gui/container/creative_inventory/" + kCreativeWindows[k] + ".png", win);
  }
  Image tabs(256, 256, 0);
  for (int col = 0; col < 6; col++)
    for (int row = 0; row < 4; row++) creativeTabSprite(tabs, col * 28, row * 32, row < 2, row % 2 == 1, col == 0, col == 5);
  scrollHandle(tabs, 232, 0, true);
  scrollHandle(tabs, 244, 0, false);
  pack.putImage(kTex + "gui/container/creative_inventory/tabs.png", tabs);
}

// ---------------------------------------------------------------------------
// Ítems: herramientas y materiales en píxel art propio
// ---------------------------------------------------------------------------

void line(Image& img, int x0, int y0, int x1, int y1, u32 c) {
  const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
  for (int i = 0; i <= n; i++) {
    const int x = x0 + (x1 - x0) * i / std::max(1, n), y = y0 + (y1 - y0) * i / std::max(1, n);
    if (x >= 0 && y >= 0 && x < 16 && y < 16) img.set(x, y, c);
  }
}

void disc(Image& img, double cx, double cy, double r, u32 c, u32 edge) {
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double d = std::hypot(x - cx, y - cy);
      if (d < r) img.set(x, y, d > r - 1.1 ? edge : shade(c, static_cast<int>(hash3(x, y, 5, c) % 20) - 10));
    }
}

Image tool(const std::string& type, u32 c) {
  Image img(16, 16, 0);
  const u32 handle = rgb(105, 75, 40), dark = shade(c, -60);
  if (type == "sword") {
    line(img, 2, 13, 4, 11, handle);
    line(img, 3, 10, 6, 13, dark);
    line(img, 5, 10, 13, 2, c);
    line(img, 5, 11, 13, 3, shade(c, -30));
    return img;
  }
  line(img, 2, 13, 10, 5, handle);
  line(img, 3, 13, 11, 5, shade(handle, -25));
  if (type == "pickaxe") {
    line(img, 5, 2, 9, 2, c); line(img, 9, 2, 13, 6, c); line(img, 13, 6, 13, 10, c);
    line(img, 5, 3, 9, 3, dark); line(img, 9, 3, 12, 6, dark); line(img, 12, 6, 12, 10, dark);
  } else if (type == "axe") {
    for (int y = 2; y < 8; y++) line(img, 8, y, 12 - (y > 5 ? y - 5 : 0), y, y < 4 ? c : dark);
  } else if (type == "shovel") {
    disc(img, 11.5, 4.5, 2.6, c, dark);
  } else if (type == "hoe") {
    line(img, 7, 3, 12, 3, c); line(img, 7, 4, 12, 4, dark); line(img, 7, 3, 7, 5, dark);
  }
  return img;
}

void addItems(MemoryPack& pack) {
  auto item = [&](const std::string& name, const Image& img) { putItem(pack, name, img); };
  const std::pair<const char*, u32> tiers[] = {{"wooden", rgb(150, 115, 65)}, {"stone", rgb(135, 135, 135)},
                                               {"iron", rgb(225, 225, 225)}, {"golden", rgb(250, 215, 60)},
                                               {"diamond", rgb(90, 230, 220)}};
  for (auto [tier, color] : tiers)
    for (const char* type : {"sword", "shovel", "pickaxe", "axe", "hoe"}) item(std::string(tier) + "_" + type, tool(type, color));

  Image stick(16, 16, 0);
  line(stick, 4, 12, 11, 5, rgb(120, 85, 45));
  line(stick, 5, 12, 12, 5, rgb(90, 62, 30));
  item("stick", stick);
  Image shears(16, 16, 0);
  line(shears, 3, 12, 10, 5, rgb(200, 200, 200)); line(shears, 3, 5, 10, 12, rgb(170, 170, 170));
  disc(shears, 3.5, 12.5, 1.8, rgb(180, 30, 30), rgb(120, 20, 20)); disc(shears, 3.5, 4.5, 1.8, rgb(180, 30, 30), rgb(120, 20, 20));
  item("shears", shears);

  auto lump = [&](const char* name, u32 c) { Image i(16, 16, 0); disc(i, 7.5, 8.5, 5.0, c, shade(c, -40)); item(name, i); };
  lump("coal", rgb(35, 35, 35));
  lump("charcoal", rgb(70, 50, 35));
  lump("clay_ball", rgb(165, 170, 185));
  lump("snowball", rgb(245, 250, 255));
  lump("apple", rgb(215, 30, 30));
  lump("golden_apple", rgb(250, 210, 50));
  lump("flint", rgb(60, 60, 65));
  auto ingot = [&](const char* name, u32 c) {
    Image i(16, 16, 0);
    for (int y = 6; y < 11; y++) line(i, 3 + (10 - y) / 2, y, 12 - (10 - y) / 2, y, y == 6 ? shade(c, 30) : (y == 10 ? shade(c, -50) : c));
    item(name, i);
  };
  ingot("iron_ingot", rgb(215, 215, 215));
  ingot("gold_ingot", rgb(250, 210, 50));
  ingot("brick", rgb(170, 85, 60));
  auto gem = [&](const char* name, u32 c) {
    Image i(16, 16, 0);
    for (int y = 3; y < 13; y++) {
      const int half = y < 7 ? (y - 2) : (12 - y);
      line(i, 8 - half, y, 7 + half, y, shade(c, (y < 7 ? 25 : -15) - (y % 2) * 10));
    }
    item(name, i);
  };
  gem("diamond", rgb(90, 230, 220));
  gem("emerald", rgb(40, 200, 90));
  auto dust = [&](const char* name, u32 c) {
    Image i(16, 16, 0);
    for (int k = 0; k < 30; k++) i.set(3 + static_cast<int>(hash3(k, 1, 2, c) % 10), 5 + static_cast<int>(hash3(k, 3, 4, c) % 8), shade(c, static_cast<int>(hash3(k, 5, 6) % 40) - 20));
    item(name, i);
  };
  dust("redstone", rgb(200, 20, 20));
  dust("dye_blue", rgb(40, 70, 200));  // lapislázuli
  dust("wheat_seeds", rgb(90, 160, 60));
  Image string(16, 16, 0);
  for (int x = 2; x < 14; x++) string.set(x, 8 + static_cast<int>(std::sin(x * 0.9) * 2.5), rgb(240, 240, 240));
  item("string", string);
  Image reedsItem(16, 16, 0);
  for (int x : {5, 8, 11}) line(reedsItem, x, 2, x - 1, 14, rgb(130, 190, 80));
  item("reeds", reedsItem);
  Image bread(16, 16, 0);
  for (int y = 6; y < 12; y++) line(bread, 2, y, 13, y, shade(rgb(200, 150, 80), (y - 9) * 12));
  item("bread", bread);
}

void addDestroyStages(MemoryPack& pack) {
  // Grietas que crecen: cada fase añade segmentos a partir del centro
  Random r(99);
  std::vector<std::pair<int, int>> path;
  int x = 8, y = 8;
  for (int i = 0; i < 120; i++) {
    path.push_back({x, y});
    x = std::clamp(x + r.nextInt(3) - 1, 0, 15);
    y = std::clamp(y + r.nextInt(3) - 1, 0, 15);
    if (i % 12 == 11) { x = 8 + r.nextInt(5) - 2; y = 8 + r.nextInt(5) - 2; }
  }
  for (int stage = 0; stage < 10; stage++) {
    Image img(16, 16, 0);
    const std::size_t n = path.size() * (stage + 1) / 10;
    for (std::size_t i = 0; i < n; i++) img.set(path[i].first, path[i].second, rgb(30, 30, 30, 255));
    pack.putImage(kTex + "blocks/destroy_stage_" + std::to_string(stage) + ".png", img);
  }
}

/// Textura de entidad con la disposición estándar de cajas: para una caja de w×h×d en (u, v),
/// arriba y abajo en la primera fila; derecha, delante, izquierda y detrás en la segunda.
struct Skin {
  Image img;
  u32 seed;
  Skin(int w, int h, u32 s) : img(w, h, 0), seed(s) {}
  void rect(int x, int y, int w, int h, u32 c, int noise = 0) {
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) {
        if (xx < 0 || yy < 0 || xx >= img.width || yy >= img.height) continue;
        const int d = noise ? static_cast<int>(hash3(xx, yy, 7, seed) % (2 * noise + 1)) - noise : 0;
        img.set(xx, yy, shade(c, d));
      }
  }
  void px(int x, int y, u32 c) {
    if (x >= 0 && y >= 0 && x < img.width && y < img.height) img.set(x, y, c);
  }
  /// Las 6 caras de una caja, con algo de sombra en los lados y abajo.
  void box(int u, int v, int w, int h, int d, u32 c, int noise = 6) {
    rect(u + d, v, w, d, shade(c, 8), noise);             // arriba
    rect(u + d + w, v, w, d, shade(c, -18), noise);       // abajo
    rect(u, v + d, d, h, shade(c, -8), noise);            // derecha
    rect(u + d, v + d, w, h, c, noise);                   // delante
    rect(u + d + w, v + d, d, h, shade(c, -8), noise);    // izquierda
    rect(u + 2 * d + w, v + d, w, h, shade(c, -4), noise);  // detrás
  }
  /// Franja horizontal alrededor de los 4 lados de una caja (filas `y0`..`y0+n` desde arriba del lado).
  void band(int u, int v, int w, int h, int d, int y0, int n, u32 c, int noise = 4) {
    (void)h;
    rect(u, v + d + y0, 2 * d + 2 * w, n, c, noise);
  }
  /// Origen de la cara delantera de una caja.
  static std::pair<int, int> front(int u, int v, int d) { return {u + d, v + d}; }
};

void addEntityTextures(MemoryPack& pack) {
  auto put = [&](const std::string& name, const Skin& s) { pack.putImage(kTex + "entity/" + name, s.img); };
  const u32 black = rgb(20, 20, 20), white = rgb(240, 240, 240);

  // Cerdo: rosa, hocico más claro, pezuñas oscuras
  {
    Skin s(64, 32, 1);
    const u32 pink = rgb(236, 160, 158);
    s.box(0, 0, 8, 8, 8, pink);
    auto [fx, fy] = Skin::front(0, 0, 8);
    s.px(fx + 1, fy + 3, white); s.px(fx + 2, fy + 3, black);
    s.px(fx + 5, fy + 3, black); s.px(fx + 6, fy + 3, white);
    s.box(16, 16, 4, 3, 1, rgb(245, 185, 180), 3);
    s.px(17, 18, rgb(150, 80, 80)); s.px(20, 18, rgb(150, 80, 80));
    s.box(28, 8, 10, 16, 8, pink);
    s.box(0, 16, 4, 6, 4, pink);
    s.band(0, 16, 4, 6, 4, 5, 1, rgb(110, 70, 60));
    put("pig/pig.png", s);
  }
  // Vaca: negra con manchas blancas, hocico rosado, cuernos grises
  {
    Skin s(64, 32, 2);
    const u32 dark = rgb(55, 40, 32);
    s.box(0, 0, 8, 8, 6, dark);
    auto [fx, fy] = Skin::front(0, 0, 6);
    s.rect(fx + 3, fy, 2, 5, white, 3);
    s.rect(fx + 1, fy + 5, 6, 3, rgb(220, 170, 160), 4);
    s.px(fx + 1, fy + 3, black); s.px(fx + 6, fy + 3, black);
    s.px(fx + 1, fy + 2, white); s.px(fx + 6, fy + 2, white);
    s.box(22, 0, 1, 3, 1, rgb(200, 200, 190), 2);
    s.box(18, 4, 12, 18, 10, dark);
    Random r(22);
    for (int i = 0; i < 9; i++) s.rect(18 + r.nextInt(40), 4 + r.nextInt(24), 3 + r.nextInt(5), 2 + r.nextInt(5), white, 4);
    s.box(52, 0, 4, 6, 1, rgb(230, 160, 170), 3);
    s.box(0, 16, 4, 12, 4, dark);
    s.band(0, 16, 4, 12, 4, 6, 4, white);
    s.band(0, 16, 4, 12, 4, 11, 1, rgb(40, 35, 30));
    put("cow/cow.png", s);
  }
  // Oveja: piel rosada bajo la lana y cara beis; la lana (blanca) va aparte y se tiñe
  {
    Skin s(64, 32, 3);
    const u32 face = rgb(225, 200, 175);
    s.box(0, 0, 6, 6, 8, face);
    auto [fx, fy] = Skin::front(0, 0, 8);
    s.px(fx + 1, fy + 2, black); s.px(fx + 4, fy + 2, black);
    s.px(fx + 1, fy + 1, white); s.px(fx + 4, fy + 1, white);
    s.rect(fx + 2, fy + 4, 2, 1, rgb(200, 140, 140));
    s.box(28, 8, 8, 16, 6, rgb(230, 190, 185));
    s.box(0, 16, 4, 12, 4, face);
    s.band(0, 16, 4, 12, 4, 11, 1, rgb(90, 70, 60));
    put("sheep/sheep.png", s);

    Skin f(64, 32, 4);
    const u32 wool = rgb(238, 238, 238);
    f.box(0, 0, 6, 6, 6, wool, 10);
    auto [wx, wy] = Skin::front(0, 0, 6);
    f.rect(wx + 1, wy + 1, 4, 5, 0);  // la cara asoma por la lana
    f.box(28, 8, 8, 16, 6, wool, 10);
    f.box(0, 16, 4, 6, 4, wool, 10);
    put("sheep/sheep_fur.png", f);
  }
  // Gallina: blanca, pico amarillo, barba roja, patas naranjas
  {
    Skin s(64, 32, 5);
    s.box(0, 0, 4, 6, 3, white, 5);
    auto [fx, fy] = Skin::front(0, 0, 3);
    s.px(fx, fy + 2, black); s.px(fx + 3, fy + 2, black);
    s.box(14, 0, 4, 2, 2, rgb(240, 190, 60), 4);
    s.box(14, 4, 2, 2, 2, rgb(210, 40, 40), 4);
    s.box(0, 9, 6, 8, 6, rgb(235, 235, 230), 6);
    s.box(26, 0, 3, 5, 3, rgb(235, 160, 50), 4);
    s.box(24, 13, 1, 4, 6, rgb(225, 225, 220), 6);
    put("chicken.png", s);
  }
  // Zombi: piel verde, camisa turquesa, pantalón morado
  {
    Skin s(64, 64, 6);
    const u32 skin = rgb(85, 140, 75);
    s.box(0, 0, 8, 8, 8, skin);
    auto [fx, fy] = Skin::front(0, 0, 8);
    s.rect(fx + 1, fy + 3, 2, 1, black); s.rect(fx + 5, fy + 3, 2, 1, black);
    s.rect(fx + 2, fy + 6, 4, 1, rgb(45, 80, 40));
    s.rect(8, 0, 8, 8, rgb(55, 95, 50), 5);  // pelo ralo arriba
    s.box(16, 16, 8, 12, 4, rgb(40, 165, 170));
    s.box(40, 16, 4, 12, 4, skin);
    s.band(40, 16, 4, 12, 4, 0, 4, rgb(40, 165, 170));
    s.box(0, 16, 4, 12, 4, rgb(70, 60, 150));
    s.band(0, 16, 4, 12, 4, 10, 2, rgb(80, 80, 80));
    put("zombie/zombie.png", s);
  }
  // Esqueleto: huesos grises, cuencas oscuras y costillas
  {
    Skin s(64, 32, 7);
    const u32 bone = rgb(200, 200, 195);
    s.box(0, 0, 8, 8, 8, bone);
    auto [fx, fy] = Skin::front(0, 0, 8);
    s.rect(fx + 1, fy + 3, 2, 2, rgb(40, 40, 40)); s.rect(fx + 5, fy + 3, 2, 2, rgb(40, 40, 40));
    s.px(fx + 3, fy + 5, rgb(70, 70, 70)); s.px(fx + 4, fy + 5, rgb(70, 70, 70));
    s.rect(fx + 1, fy + 6, 6, 1, rgb(90, 90, 90));
    s.box(16, 16, 8, 12, 4, rgb(170, 170, 165));
    for (int y = 21; y < 30; y += 2) s.rect(16, y, 24, 1, rgb(110, 110, 105));
    s.box(40, 16, 2, 12, 2, bone);
    s.box(0, 16, 2, 12, 2, bone);
    put("skeleton/skeleton.png", s);
  }
  // Creeper: verde moteado, con una cara oscura propia
  {
    Skin s(64, 32, 8);
    Random r(88);
    auto mottled = [&](int x, int y, int w, int h) {
      for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
          const int k = r.nextInt(10);
          s.px(xx, yy, k < 6 ? rgb(75, 170, 70) : k < 9 ? rgb(50, 125, 45) : rgb(190, 215, 185));
        }
    };
    auto mottledBox = [&](int u, int v, int w, int h, int d) {
      mottled(u + d, v, 2 * w, d);
      mottled(u, v + d, 2 * d + 2 * w, h);
    };
    mottledBox(0, 0, 8, 8, 8);
    mottledBox(16, 16, 8, 12, 4);
    mottledBox(0, 16, 4, 6, 4);
    auto [fx, fy] = Skin::front(0, 0, 8);
    const u32 k = rgb(15, 25, 15);
    s.rect(fx + 1, fy + 2, 2, 2, k); s.rect(fx + 5, fy + 2, 2, 2, k);
    s.rect(fx + 3, fy + 4, 2, 2, k); s.rect(fx + 2, fy + 5, 1, 2, k); s.rect(fx + 5, fy + 5, 1, 2, k);
    put("creeper/creeper.png", s);
  }
  // Araña: oscura, con ojos rojos (y una capa aparte con los ojos que brillan de noche)
  {
    Skin s(64, 32, 9);
    const u32 dark = rgb(50, 42, 38);
    s.box(32, 4, 8, 8, 8, dark);
    s.box(0, 0, 6, 6, 6, rgb(40, 34, 30));
    s.box(0, 12, 10, 8, 12, dark);
    s.rect(12 + 3, 24, 4, 4, rgb(85, 65, 55), 4);  // marca en el lomo
    s.box(18, 0, 16, 2, 2, rgb(45, 38, 35), 4);
    Skin e(64, 32, 10);
    auto [fx, fy] = Skin::front(32, 4, 8);
    const u32 red = rgb(220, 30, 30);
    for (auto [dx, dy] : {std::pair{1, 3}, {2, 3}, {5, 3}, {6, 3}, {3, 2}, {4, 2}, {2, 5}, {5, 5}}) {
      s.px(fx + dx, fy + dy, red);
      e.px(fx + dx, fy + dy, red);
    }
    put("spider/spider.png", s);
    put("spider_eyes.png", e);
  }
  // Las dos skins de serie del jugador, en la disposición de 1.8 (64x64): cada brazo y pierna con su
  // sitio y la segunda capa vacía. Steve lleva brazos de 4 píxeles y Alex de 3.
  {
    auto person = [&](u32 seed, bool slim, u32 skin, u32 hair, u32 shirt, u32 pants, u32 shoes, u32 eyes, bool longHair) {
      Skin s(64, 64, seed);
      const int aw = slim ? 3 : 4;  // ancho de los brazos
      // Cabeza: piel, pelo arriba, por detrás y en los lados (más abajo si lo lleva largo)
      s.box(0, 0, 8, 8, 8, skin);
      s.rect(8, 0, 8, 8, hair, 5);               // arriba
      s.rect(24, 8, 8, longHair ? 8 : 5, hair, 5);  // nuca
      s.band(0, 0, 8, 8, 8, 0, longHair ? 6 : 2, hair);
      s.rect(8, 8, 8, 2, hair, 5);               // flequillo (delante)
      auto [fx, fy] = Skin::front(0, 0, 8);
      s.px(fx + 1, fy + 4, white); s.px(fx + 2, fy + 4, eyes);
      s.px(fx + 5, fy + 4, eyes); s.px(fx + 6, fy + 4, white);
      s.rect(fx + 3, fy + 6, 2, 1, rgb(150, 90, 70));
      // Cuerpo, brazos (con la manga arriba) y piernas (con los zapatos abajo)
      s.box(16, 16, 8, 12, 4, shirt);
      s.box(40, 16, aw, 12, 4, skin);   // brazo derecho
      s.band(40, 16, aw, 12, 4, 0, 5, shirt);
      s.box(32, 48, aw, 12, 4, skin);   // brazo izquierdo
      s.band(32, 48, aw, 12, 4, 0, 5, shirt);
      s.box(0, 16, 4, 12, 4, pants);    // pierna derecha
      s.band(0, 16, 4, 12, 4, 10, 2, shoes);
      s.box(16, 48, 4, 12, 4, pants);   // pierna izquierda
      s.band(16, 48, 4, 12, 4, 10, 2, shoes);
      return s;
    };
    put("steve.png", person(11, false, rgb(200, 150, 110), rgb(70, 45, 25), rgb(40, 170, 175), rgb(55, 70, 150), rgb(90, 90, 90),
                            rgb(60, 80, 170), false));
    put("alex.png", person(13, true, rgb(232, 188, 152), rgb(196, 98, 40), rgb(90, 160, 70), rgb(110, 80, 55), rgb(60, 45, 35),
                           rgb(50, 130, 80), true));
  }
  // Flecha (vista lateral en 16x5 y las plumas en 5x5)
  {
    Skin s(32, 32, 12);
    for (int x = 3; x < 13; x++) s.px(x, 2, rgb(125, 95, 60));
    for (int dy = -2; dy <= 2; dy++) s.px(13 + (2 - std::abs(dy)) / 2, 2 + dy, rgb(160, 160, 165));
    s.px(15, 2, rgb(200, 200, 205));
    for (int x = 0; x < 4; x++) { s.px(x, 1, white); s.px(x, 3, white); }
    for (int i = 0; i < 5; i++) { s.px(i, 5 + i, white); s.px(4 - i, 5 + i, white); }
    put("arrow.png", s);
  }
}

/// Arco de 16x16 en diagonal (la madera combada hacia arriba a la izquierda). `pull` 0 = en reposo;
/// 1..3 = cada vez más tensado y con la flecha puesta (los tres sprites `bow_pulling_N` de 1.8).
Image bowSprite(int pull) {
  Image img(16, 16, 0);
  const u32 wood = rgb(124, 86, 45), edge = rgb(74, 50, 25), grip = rgb(158, 114, 62), cord = rgb(232, 232, 226);
  std::set<std::pair<int, int>> body;
  for (int i = 0; i <= 48; i++) {
    const double t = i / 48.0, u = 1 - t;
    const double x = u * u * 2.5 + 2 * u * t * 0.6 + t * t * 13.5, y = u * u * 13.5 + 2 * u * t * 0.6 + t * t * 2.5;
    const std::pair<int, int> c{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
    body.insert(c);
    if (t > 0.3 && t < 0.7) body.insert({c.first + 1, c.second});  // el agarre es más grueso
  }
  for (const auto& [x, y] : body) img.set(x, y, wood);
  for (const auto& [x, y] : body) {  // contorno por fuera (arriba y a la izquierda)
    for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{0, -1}}) {
      const int nx = x + dx, ny = y + dy;
      if (nx >= 0 && ny >= 0 && !body.count({nx, ny})) img.set(nx, ny, edge);
    }
  }
  for (auto [x, y] : {std::pair{5, 6}, {6, 5}}) if (body.count({x, y})) img.set(x, y, grip);
  const double pulled = pull * 1.45;  // cuánto se echa atrás la cuerda por la diagonal
  const int nx = static_cast<int>(std::round(8.0 + pulled)), ny = nx;
  line(img, 2, 13, nx, ny, cord);
  line(img, nx, ny, 13, 2, cord);
  if (pull > 0) {  // la flecha, apoyada en la cuerda y apuntando hacia fuera del arco
    const int tip = nx - 6;
    line(img, nx, ny, tip, tip, rgb(158, 124, 78));
    for (auto [x, y] : {std::pair{tip, tip}, {tip + 1, tip}, {tip, tip + 1}}) img.set(x, y, rgb(188, 188, 194));
    for (auto [x, y] : {std::pair{nx, ny - 1}, {nx - 1, ny}, {nx + 1, ny}, {nx, ny + 1}}) img.set(x, y, rgb(245, 245, 245));
  }
  return img;
}

/// Texturas de la armadura puesta (64x32, la disposición de una skin antigua): la capa 1 (casco, pechera
/// y botas) y la 2 (pantalones) de cada material, y la capa sin teñir del cuero.
void addArmorTextures(MemoryPack& pack) {
  struct Mat {
    const char* name;
    u32 base;
  };
  const Mat mats[] = {{"leather", rgb(226, 226, 226)}, {"chainmail", rgb(150, 150, 158)}, {"iron", rgb(214, 214, 220)},
                      {"gold", rgb(242, 208, 70)},     {"diamond", rgb(72, 218, 196)}};
  for (const Mat& m : mats) {
    const std::string name = m.name;
    for (int layer = 1; layer <= 2; layer++) {
      Skin s(64, 32, seedOf(name + std::to_string(layer)));
      const u32 c = layer == 2 ? shade(m.base, -24) : m.base;
      s.box(0, 0, 8, 8, 8, c, 7);     // casco
      s.box(16, 16, 8, 12, 4, c, 7);  // cuerpo / pantalones
      s.box(40, 16, 4, 12, 4, c, 7);  // brazos
      s.box(0, 16, 4, 12, 4, c, 7);   // piernas
      const u32 dark = shade(c, -58), light = shade(c, 38);
      // Cada cara es una placa: borde oscuro por abajo y a la derecha, brillo por arriba y a la izquierda
      auto plate = [&](int x, int y, int w, int h) {
        for (int i = 0; i < w; i++) {
          s.px(x + i, y, shade(c, 26));
          s.px(x + i, y + h - 1, shade(c, -44));
        }
        for (int j = 0; j < h; j++) {
          s.px(x, y + j, shade(c, 18));
          s.px(x + w - 1, y + j, shade(c, -34));
        }
      };
      auto plates = [&](int u, int v, int w, int h, int d) {
        plate(u + d, v, w, d);
        plate(u + d + w, v, w, d);
        plate(u, v + d, d, h);
        plate(u + d, v + d, w, h);
        plate(u + d + w, v + d, d, h);
        plate(u + 2 * d + w, v + d, w, h);
      };
      plates(0, 0, 8, 8, 8);
      plates(16, 16, 8, 12, 4);
      plates(40, 16, 4, 12, 4);
      plates(0, 16, 4, 12, 4);
      if (name == "chainmail") {  // malla: un tablero fino oscuro y claro
        for (int y = 0; y < 32; y++)
          for (int x = 0; x < 64; x++)
            if (s.img.get(x, y) >> 24) s.px(x, y, ((x + y) & 1) ? shade(s.img.get(x, y), -34) : shade(s.img.get(x, y), 14));
      }
      if (layer == 1) {
        s.rect(8, 12, 8, 2, dark);      // visera del casco (cara de delante)
        s.rect(8, 8, 8, 1, light);      // brillo en la frente
        s.rect(20, 20, 8, 1, light);    // hombros
        s.rect(23, 21, 2, 8, shade(c, 18));  // costura del pecho
        s.rect(20, 27, 8, 1, dark);     // faja
        s.rect(4, 29, 4, 3, dark);      // punteras de las botas
        s.rect(0, 29, 16, 3, shade(c, -20));
      } else {
        s.rect(20, 20, 8, 2, dark);     // cinturón
        s.rect(23, 20, 2, 2, light);    // hebilla
        s.rect(4, 22, 4, 1, dark);      // rodillas
      }
      pack.putImage(kTex + "models/armor/" + name + "_layer_" + std::to_string(layer) + ".png", s.img);
    }
  }
  for (int layer = 1; layer <= 2; layer++) {
    Skin o(64, 32, 99);
    const u32 stitch = rgb(70, 45, 28);
    for (int y = 21; y < 32; y += 2) o.px(layer == 1 ? 24 : 24, y, stitch);  // costuras del pecho
    o.rect(20, layer == 1 ? 27 : 21, 8, 1, stitch);                         // faja o cinturón
    o.rect(8, 11, 8, 1, stitch);                                            // borde del casco
    for (int x = 4; x < 8; x += 2) o.px(x, 30, stitch);                     // botas
    pack.putImage(kTex + "models/armor/leather_layer_" + std::to_string(layer) + "_overlay.png", o.img);
  }
}

/// Hoja de los orbes de experiencia (64x64: 11 tamaños en una cuadrícula de 4x4 de 16 px). Casi blancos: el
/// juego los tiñe de verde a amarillo.
void addXpOrbTexture(MemoryPack& pack) {
  Image img(64, 64, 0);
  for (int i = 0; i < 11; i++) {
    const double radius = 2.2 + i * 0.5;
    const int ox = (i % 4) * 16, oy = (i / 4) * 16;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        const double d = std::hypot(x - 7.5, y - 7.5);
        if (d > radius) continue;
        const double k = d / radius;  // 0 en el centro, 1 en el borde
        const int v = static_cast<int>(255 - 120 * k * k);
        const int a = d > radius - 0.9 ? 190 : 255;
        img.set(ox + x, oy + y, rgb(v, v, v, a));
      }
    // un brillo pequeño arriba a la izquierda
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        if (std::hypot(x - 6.0 + radius * 0.15, y - 6.0 + radius * 0.15) < radius * 0.28) img.set(ox + x, oy + y, rgb(255, 255, 255));
  }
  pack.putImage(kTex + "entity/experience_orb.png", img);
}

void addMobItems(MemoryPack& pack) {
  auto item = [&](const std::string& name, const Image& img) { putItem(pack, name, img); };
  // Carne: una tajada con su veta de grasa
  auto meat = [&](const char* name, u32 flesh, u32 fat) {
    Image i(16, 16, 0);
    disc(i, 7.0, 8.0, 5.2, flesh, shade(flesh, -45));
    for (int x = 4; x < 11; x++) i.set(x, 6 + (x % 3 == 0 ? 1 : 0), fat);
    disc(i, 11.5, 11.5, 1.8, rgb(235, 230, 215), rgb(190, 185, 170));  // hueso
    item(name, i);
  };
  meat("porkchop", rgb(235, 140, 140), rgb(250, 215, 210));
  meat("cooked_porkchop", rgb(195, 125, 85), rgb(235, 200, 150));
  meat("beef", rgb(200, 50, 45), rgb(240, 200, 190));
  meat("cooked_beef", rgb(120, 70, 40), rgb(170, 120, 80));
  meat("mutton", rgb(210, 70, 70), rgb(245, 225, 225));
  meat("cooked_mutton", rgb(150, 85, 55), rgb(200, 160, 120));
  meat("rotten_flesh", rgb(140, 110, 70), rgb(90, 130, 70));
  auto drumstick = [&](const char* name, u32 c) {
    Image i(16, 16, 0);
    disc(i, 6.5, 6.5, 4.2, c, shade(c, -40));
    line(i, 9, 9, 13, 13, rgb(235, 230, 215));
    line(i, 10, 9, 13, 12, rgb(210, 205, 190));
    item(name, i);
  };
  drumstick("chicken", rgb(240, 190, 170));
  drumstick("cooked_chicken", rgb(200, 140, 80));
  Image leather(16, 16, 0);
  for (int y = 3; y < 13; y++) line(leather, 3 + (y % 4 == 0 ? 1 : 0), y, 12 - (y % 5 == 0 ? 1 : 0), y, shade(rgb(150, 90, 50), (y % 3) * 6 - 6));
  item("leather", leather);
  Image feather(16, 16, 0);
  line(feather, 3, 13, 12, 3, rgb(200, 200, 200));
  for (int k = 0; k < 7; k++) { line(feather, 5 + k, 11 - k, 7 + k, 12 - k, rgb(245, 245, 245)); line(feather, 4 + k, 10 - k, 5 + k, 8 - k, rgb(235, 235, 235)); }
  item("feather", feather);
  Image bone(16, 16, 0);
  line(bone, 4, 12, 11, 5, rgb(235, 232, 220));
  line(bone, 5, 12, 12, 5, rgb(215, 212, 200));
  for (auto [x, y] : {std::pair{3, 12}, {4, 13}, {12, 4}, {11, 3}}) disc(bone, x + 0.5, y + 0.5, 1.2, rgb(240, 238, 228), rgb(200, 198, 188));
  item("bone", bone);
  Image arrow(16, 16, 0);
  line(arrow, 4, 12, 11, 5, rgb(125, 95, 60));
  for (auto [x, y] : {std::pair{11, 4}, {12, 4}, {12, 5}, {13, 3}, {10, 4}, {12, 6}}) arrow.set(x, y, rgb(170, 170, 175));
  for (auto [x, y] : {std::pair{3, 12}, {4, 13}, {2, 11}, {3, 13}, {2, 13}}) arrow.set(x, y, rgb(240, 240, 240));
  item("arrow", arrow);
  Image powder(16, 16, 0);
  for (int k = 0; k < 40; k++) powder.set(3 + static_cast<int>(hash3(k, 7, 1) % 10), 6 + static_cast<int>(hash3(k, 8, 2) % 7), shade(rgb(80, 80, 80), static_cast<int>(hash3(k, 9, 3) % 50) - 25));
  item("gunpowder", powder);
  Image eye(16, 16, 0);
  disc(eye, 7.5, 8.5, 4.5, rgb(170, 40, 60), rgb(110, 20, 35));
  disc(eye, 6.5, 7.5, 1.5, rgb(240, 200, 200), rgb(200, 150, 150));
  item("spider_eye", eye);
  Image egg(16, 16, 0);
  for (int y = 3; y < 14; y++) {
    const double half = std::sqrt(std::max(0.0, 1.0 - std::pow((y - 8.5) / 5.6, 2))) * (y < 8 ? 3.4 : 4.2);
    line(egg, static_cast<int>(7.5 - half), y, static_cast<int>(7.5 + half), y, shade(rgb(235, 220, 180), (8 - y) * 3));
  }
  item("egg", egg);
  item("bow", bowSprite(0));
  for (int n = 0; n < 3; n++) item("bow_pulling_" + std::to_string(n), bowSprite(n + 1));
  auto root = [&](const char* name, u32 c, bool leaves) {
    Image i(16, 16, 0);
    for (int y = 4; y < 14; y++) line(i, 8 - (14 - y) / 3, y, 8 + (14 - y) / 3 - (y > 11 ? 1 : 0), y, shade(c, (y % 2) * 12));
    if (leaves) for (int k = 0; k < 4; k++) line(i, 6 + k, 1, 7 + k / 2, 4, rgb(70, 160, 50));
    item(name, i);
  };
  root("carrot", rgb(240, 140, 30), true);
  root("potato", rgb(210, 175, 100), false);
  root("baked_potato", rgb(200, 150, 70), false);
  Image wheat(16, 16, 0);
  for (int x : {5, 8, 11}) { line(wheat, x, 14, x - 1, 3, rgb(210, 180, 80)); for (int y = 3; y < 8; y++) wheat.set(x - 1 + (y % 2), y, rgb(230, 200, 90)); }
  item("wheat", wheat);
}

}  // namespace cc0

using namespace cc0;

std::shared_ptr<MemoryPack> makeCC0Pack() {
  auto pack = std::make_shared<MemoryPack>("Pack libre (CC0)");
  Builder b(*pack);
  baseModels(b);

  // --- Blockstates: agrupar las variantes que usa cada fichero ---
  std::map<std::string, std::set<std::string>> variants;
  for (const auto& [state, ref] : allMappedStates()) variants[ref.file].insert(ref.variant);
  for (const auto& e : allExtendedStates()) variants[e.ref.file].insert(e.ref.variant);

  const auto& wood = woods();
  const auto& dyes = dyeColors();
  auto endsWith = [](const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
  };

  for (const auto& [file, keys] : variants) {
    json bs = {{"variants", json::object()}};
    auto simple = [&](const std::string& model) {
      for (const auto& k : keys) bs["variants"][k] = {{"model", model}};
    };

    if (addShapedBlock(b, file, keys, bs)) {
      pack->putJson("assets/minecraft/blockstates/" + file + ".json", bs);
      continue;
    }
    if (file == "grass" || file == "mycelium" || file == "podzol") {
      const std::string top = file == "grass" ? "grass_top" : (file == "mycelium" ? "mycelium_top" : "dirt_podzol_top");
      const std::string side = file == "grass" ? "grass_side" : (file == "mycelium" ? "mycelium_side" : "dirt_podzol_side");
      json faces = {{"down", face("#bottom", "down")}, {"up", face("#top", "up", file == "grass" ? 0 : -1)}};
      for (const char* f : {"north", "south", "west", "east"}) faces[f] = face("#side", f);
      b.model(file, {{"textures", {{"particle", "#bottom"}, {"bottom", "blocks/dirt"}, {"top", "blocks/" + top}, {"side", "blocks/" + side}}},
                     {"elements", json::array({{{"from", {0, 0, 0}}, {"to", {16, 16, 16}}, {"faces", faces}}})}});
      b.model(file + "_snowed", bottomTop("dirt", "snow", "grass_side_snowed"));
      for (const auto& k : keys) bs["variants"][k] = {{"model", k == "snowy=true" ? file + "_snowed" : file}};
    } else if (endsWith(file, "_log")) {
      const std::string w = file.substr(0, file.size() - 4);
      const std::string side = "log_" + woodTex(w), top = "log_" + woodTex(w) + "_top";
      b.model(file, column(top, side));
      b.model(w + "_bark", cubeAll(side));
      const auto [plank, bark] = wood.at(w);
      b.tex(side, logSide(bark, seedOf(side)));
      b.tex(top, logTop(plank, bark, seedOf(top)));
      bs["variants"]["axis=y"] = {{"model", file}};
      bs["variants"]["axis=z"] = {{"model", file}, {"x", 90}};
      bs["variants"]["axis=x"] = {{"model", file}, {"x", 90}, {"y", 90}};
      bs["variants"]["axis=none"] = {{"model", w + "_bark"}};
    } else if (endsWith(file, "_leaves")) {
      const std::string w = file.substr(0, file.size() - 7);
      b.model(file, {{"parent", "block/leaves"}, {"textures", {{"all", "blocks/leaves_" + woodTex(w)}}}});
      b.tex("leaves_" + woodTex(w), leaves(seedOf(w)));
      simple(file);
    } else if (endsWith(file, "_planks")) {
      const std::string w = file.substr(0, file.size() - 7);
      b.model(file, cubeAll("planks_" + woodTex(w)));
      b.tex("planks_" + woodTex(w), planks(wood.at(w).first, seedOf(file)));
      simple(file);
    } else if (endsWith(file, "_sapling")) {
      const std::string w = file.substr(0, file.size() - 8);
      const std::string st = "sapling_" + (w == "dark_oak" ? std::string("roofed_oak") : w);
      b.model(file, cross(st, false));
      b.tex(st, sapling(w == "birch" ? rgb(120, 160, 80) : (w == "spruce" ? rgb(50, 90, 50) : rgb(70, 130, 40))));
      simple(file);
    } else if (endsWith(file, "_wool") || endsWith(file, "_stained_hardened_clay") || endsWith(file, "_stained_glass")) {
      const auto pos = file.find(endsWith(file, "_wool") ? "_wool" : (endsWith(file, "_stained_glass") ? "_stained_glass" : "_stained_hardened_clay"));
      const std::string color = file.substr(0, pos);
      const u32 c = dyes.at(color);
      std::string t;
      if (endsWith(file, "_wool")) { t = "wool_colored_" + color; b.tex(t, noisy(c, 7, seedOf(t))); }
      else if (endsWith(file, "_stained_glass")) { t = "glass_" + color; b.tex(t, glassTex(c, 110)); }
      else { t = "hardened_clay_stained_" + color; b.tex(t, noisy(rgb(R(c) * 3 / 4 + 40, G(c) * 3 / 4 + 30, Bc(c) * 3 / 4 + 25), 5, seedOf(t))); }
      b.model(file, cubeAll(t));
      simple(file);
    } else if (file == "tall_grass" || file == "fern") {
      b.model(file, cross(file == "fern" ? "fern" : "tallgrass", true));
      b.tex(file == "fern" ? "fern" : "tallgrass", grassBlades(seedOf(file), file == "fern"));
      simple(file);
    } else if (file == "dead_bush") {
      b.model(file, cross("deadbush", false));
      b.tex("deadbush", deadBush());
      simple(file);
    } else if (file == "reeds") {
      b.model(file, cross("reeds", true));
      b.tex("reeds", reeds());
      simple(file);
    } else if (file == "web") {
      b.model(file, cross("web", false));
      Image w(16, 16, 0);
      for (int i = 0; i < 16; i++) { w.set(i, i, rgb(230, 230, 230)); w.set(15 - i, i, rgb(230, 230, 230)); w.set(8, i, rgb(220, 220, 220)); w.set(i, 8, rgb(220, 220, 220)); }
      b.tex("web", w);
      simple(file);
    } else if (file == "brown_mushroom" || file == "red_mushroom") {
      b.model(file, cross("mushroom_" + file.substr(0, file.find('_')), false));
      b.tex("mushroom_" + file.substr(0, file.find('_')), mushroom(file == "red_mushroom" ? rgb(200, 30, 30) : rgb(150, 110, 80)));
      simple(file);
    } else if (file == "dandelion" || file == "poppy" || file == "blue_orchid" || file == "allium" || file == "houstonia" ||
               endsWith(file, "_tulip") || file == "oxeye_daisy") {
      static const std::map<std::string, std::pair<u32, u32>> fc = {
          {"dandelion", {rgb(245, 215, 30), rgb(230, 170, 20)}},    {"poppy", {rgb(200, 25, 25), rgb(40, 30, 20)}},
          {"blue_orchid", {rgb(40, 160, 220), rgb(160, 220, 250)}}, {"allium", {rgb(185, 110, 230), rgb(220, 170, 250)}},
          {"houstonia", {rgb(225, 230, 240), rgb(240, 220, 90)}},   {"red_tulip", {rgb(210, 40, 30), rgb(160, 20, 20)}},
          {"orange_tulip", {rgb(235, 120, 30), rgb(200, 90, 20)}},  {"white_tulip", {rgb(235, 235, 235), rgb(210, 210, 210)}},
          {"pink_tulip", {rgb(240, 170, 200), rgb(220, 140, 180)}}, {"oxeye_daisy", {rgb(240, 240, 240), rgb(240, 200, 40)}},
      };
      static const std::map<std::string, std::string> tn = {
          {"poppy", "flower_rose"}, {"red_tulip", "flower_tulip_red"}, {"orange_tulip", "flower_tulip_orange"},
          {"white_tulip", "flower_tulip_white"}, {"pink_tulip", "flower_tulip_pink"}};
      const std::string t = tn.count(file) ? tn.at(file) : "flower_" + file;
      b.model(file, cross(t, false));
      b.tex(t, flower(fc.at(file).first, fc.at(file).second, seedOf(file)));
      simple(file);
    } else if (file == "sunflower" || file == "syringa" || file == "double_grass" || file == "double_fern" || file == "double_rose" ||
               file == "paeonia") {
      const bool tinted = file == "double_grass" || file == "double_fern";
      for (const char* half : {"bottom", "top"}) {
        static const std::map<std::string, std::string> dn = {{"double_grass", "grass"}, {"double_fern", "fern"}, {"double_rose", "rose"}};
        const std::string t = "double_plant_" + (dn.count(file) ? dn.at(file) : file) + "_" + half;
        b.model(file + "_" + half, cross(t, tinted));
        if (tinted) b.tex(t, grassBlades(seedOf(t), file == "double_fern"));
        else if (std::string(half) == "top") b.tex(t, flower(file == "sunflower" ? rgb(250, 210, 30) : (file == "double_rose" ? rgb(200, 20, 30) : rgb(220, 150, 220)), rgb(90, 60, 20), seedOf(t), 6));
        else { Image s(16, 16, 0); for (int y = 0; y < 16; y++) { s.set(8, y, rgb(58, 120, 40)); if (y % 4 == 2) { s.set(7, y, rgb(58, 120, 40)); s.set(9, y + 1 < 16 ? y + 1 : y, rgb(58, 120, 40)); } } b.tex(t, s); }
      }
      bs["variants"]["half=lower"] = {{"model", file + "_bottom"}};
      bs["variants"]["half=upper"] = {{"model", file + "_top"}};
    } else if (file == "snow_layer") {
      for (int n = 1; n <= 8; n++) {
        const int h = n * 2;
        json faces = {{"down", face("#texture", "down")}, {"up", face("#texture", h == 16 ? "up" : nullptr)}};
        for (const char* f : {"north", "south", "west", "east"}) faces[f] = face("#texture", f, -1, {0, 16 - h, 16, 16});
        b.model("snow_height" + std::to_string(h), {{"textures", {{"texture", "blocks/snow"}}},
                                                    {"elements", json::array({{{"from", {0, 0, 0}}, {"to", {16, h, 16}}, {"faces", faces}}})}});
        bs["variants"]["layers=" + std::to_string(n)] = {{"model", "snow_height" + std::to_string(h)}};
      }
      b.ensureSimple("snow");
    } else if (file == "torch") {
      json faces = {{"up", face("#torch", nullptr, -1, {7, 6, 9, 8})}, {"down", face("#torch", nullptr, -1, {7, 13, 9, 15})}};
      for (const char* f : {"north", "south", "west", "east"}) faces[f] = face("#torch", nullptr, -1, {7, 6, 9, 16});
      b.model("normal_torch", {{"ambientocclusion", false},
                               {"textures", {{"torch", "blocks/torch_on"}}},
                               {"elements", json::array({{{"from", {7, 0, 7}}, {"to", {9, 10, 9}}, {"shade", false}, {"faces", faces}}})}});
      b.tex("torch_on", torchTex());
      simple("normal_torch");
    } else if (file == "cactus") {
      json side = {{"from", {1, 0, 1}}, {"to", {15, 16, 15}},
                   {"faces", {{"north", face("#side", nullptr)}, {"south", face("#side", nullptr)}, {"west", face("#side", nullptr)}, {"east", face("#side", nullptr)}}}};
      json caps = {{"from", {0, 0, 0}}, {"to", {16, 16, 16}}, {"faces", {{"up", face("#top", "up")}, {"down", face("#bottom", "down")}}}};
      b.model(file, {{"ambientocclusion", false},
                     {"textures", {{"top", "blocks/cactus_top"}, {"bottom", "blocks/cactus_bottom"}, {"side", "blocks/cactus_side"}}},
                     {"elements", json::array({caps, side})}});
      b.tex("cactus_side", cactusSide());
      Image top = noisy(rgb(60, 140, 60), 8, 21);
      for (int i = 0; i < 16; i++) { top.set(i, 0, 0); top.set(i, 15, 0); top.set(0, i, 0); top.set(15, i, 0); }
      b.tex("cactus_top", top);
      b.tex("cactus_bottom", top);
      simple(file);
    } else if (file == "waterlily") {
      json f = {{"from", {0, 0.25, 0}}, {"to", {16, 0.25, 16}}, {"faces", {{"up", face("#texture", nullptr, 0)}, {"down", face("#texture", nullptr, 0)}}}};
      b.model(file, {{"ambientocclusion", false}, {"textures", {{"texture", "blocks/waterlily"}}}, {"elements", json::array({f})}});
      Image lily(16, 16, 0);
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
          if (std::hypot(x - 7.5, y - 7.5) < 7.5 && !(x > 7 && std::abs(y - 7.5) < 1.5)) lily.set(x, y, shade(rgb(150, 150, 150), static_cast<int>(hash3(x, y, 1, 2) % 30) - 15));
      b.tex("waterlily", lily);
      simple(file);
    } else if (file == "sandstone" || file == "chiseled_sandstone" || file == "smooth_sandstone") {
      const std::string side = file == "sandstone" ? "sandstone_normal" : (file == "chiseled_sandstone" ? "sandstone_carved" : "sandstone_smooth");
      b.model(file, bottomTop("sandstone_bottom", "sandstone_top", side));
      for (const auto& t : {std::string("sandstone_bottom"), std::string("sandstone_top"), side})
        if (!b.textures_.count(t)) {
          b.textures_.insert(t);
          Image img = noisy(rgb(216, 203, 155), 6, seedOf(t));
          if (t == "sandstone_normal") for (int x = 0; x < 16; x++) { img.set(x, 3, rgb(190, 176, 128)); img.set(x, 12, rgb(196, 182, 134)); }
          if (t == "sandstone_carved") for (int x = 2; x < 14; x++) { img.set(x, 2, rgb(180, 166, 118)); img.set(x, 13, rgb(180, 166, 118)); }
          b.tex(t, img);
        }
      simple(file);
    } else if (file == "crafting_table" || file == "tnt" || file == "bookshelf" || file == "melon_block") {
      const u32 c = file == "crafting_table" ? rgb(150, 110, 70) : (file == "tnt" ? rgb(200, 50, 40) : (file == "bookshelf" ? rgb(150, 115, 70) : rgb(110, 150, 40)));
      b.model(file, bottomTop(file + "_bottom", file + "_top", file + "_side"));
      b.tex(file + "_bottom", planks(wood.at("oak").first, seedOf(file)));
      Image top = noisy(shade(c, 20), 6, seedOf(file + "t"));
      Image side = noisy(c, 8, seedOf(file + "s"));
      for (int x = 0; x < 16; x++) { side.set(x, 0, shade(c, -40)); side.set(x, 15, shade(c, -40)); }
      if (file == "tnt") for (int x = 3; x < 13; x++) for (int y = 6; y < 10; y++) side.set(x, y, rgb(230, 230, 230));
      if (file == "bookshelf") for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (y % 8 != 0 && y % 8 != 7) side.set(x, y, shade(dyes.begin()->second, -static_cast<int>(hash3(x / 2, y / 8, 1, 3) % 150)));
      b.tex(file + "_top", top);
      b.tex(file + "_side", side);
      simple(file);
    } else if (endsWith(file, "_ore") && file != "lit_redstone_ore") {
      static const std::map<std::string, u32> ore = {
          {"coal_ore", rgb(30, 30, 30)},     {"iron_ore", rgb(215, 175, 145)}, {"gold_ore", rgb(250, 220, 70)},
          {"diamond_ore", rgb(95, 230, 220)}, {"lapis_ore", rgb(30, 70, 190)},  {"redstone_ore", rgb(200, 10, 10)},
          {"emerald_ore", rgb(40, 200, 90)},  {"quartz_ore", rgb(230, 225, 215)},
      };
      b.model(file, cubeAll(file));
      Image img = noisy(file == "quartz_ore" ? baseColor("netherrack") : baseColor("stone"), 10, seedOf("stone"));
      speckle(img, ore.at(file), 6, 4, seedOf(file));
      b.tex(file, img);
      simple(file);
    } else if (file == "lit_redstone_ore") {
      b.model(file, cubeAll("redstone_ore"));
      simple(file);
    } else if (file == "glass" || file == "ice" || file == "slime") {
      b.model(file, cubeAll(file));
      b.tex(file, file == "glass" ? glassTex(rgb(200, 220, 230), 0) : (file == "ice" ? glassTex(rgb(150, 190, 250), 170) : glassTex(rgb(110, 190, 90), 150)));
      simple(file);
    } else if (file == "quartz_block" || file == "chiseled_quartz_block") {
      const bool ch = file == "chiseled_quartz_block";
      b.model(file, bottomTop(ch ? "quartz_block_chiseled_top" : "quartz_block_bottom", ch ? "quartz_block_chiseled_top" : "quartz_block_top",
                              ch ? "quartz_block_chiseled" : "quartz_block_side"));
      for (const char* t : {"quartz_block_chiseled_top", "quartz_block_bottom", "quartz_block_top", "quartz_block_chiseled", "quartz_block_side"}) b.ensureSimple(t);
      simple(file);
    } else if (file == "sponge") {
      b.model("sponge", cubeAll("sponge"));
      b.model("sponge_wet", cubeAll("sponge_wet"));
      b.ensureSimple("sponge");
      b.ensureSimple("sponge_wet");
      bs["variants"]["wet=false"] = {{"model", "sponge"}};
      bs["variants"]["wet=true"] = {{"model", "sponge_wet"}};
    } else {
      // Bloque de textura única: nombre de textura = nombre de fichero salvo excepciones
      static const std::map<std::string, std::string> texName = {
          {"granite", "stone_granite"}, {"diorite", "stone_diorite"}, {"andesite", "stone_andesite"},
          {"smooth_granite", "stone_granite_smooth"}, {"smooth_diorite", "stone_diorite_smooth"},
          {"smooth_andesite", "stone_andesite_smooth"}, {"quartz_ore", "quartz_ore"},
          {"brick_block", "brick"}, {"mossy_cobblestone", "cobblestone_mossy"}, {"mossy_stonebrick", "stonebrick_mossy"},
          {"cracked_stonebrick", "stonebrick_cracked"}, {"chiseled_stonebrick", "stonebrick_carved"}, {"packed_ice", "ice_packed"},
          {"snow", "snow"},
      };
      auto it = texName.find(file);
      const std::string t = it == texName.end() ? file : it->second;
      b.model(file, cubeAll(t));
      b.ensureSimple(t);
      simple(file);
    }
    pack->putJson("assets/minecraft/blockstates/" + file + ".json", bs);
  }

  // Texturas compartidas que usan varios modelos
  for (const char* t : {"dirt", "snow", "stone"}) b.ensureSimple(t);
  {
    Image top = noisy(rgb(150, 150, 150), 14, 31);  // se tiñe con el color del bioma
    b.tex("grass_top", top);
    Image side = noisy(baseColor("dirt"), 10, seedOf("dirt"));
    for (int x = 0; x < 16; x++) {
      const int depth = 3 + static_cast<int>(hash3(x, 0, 0, 33) % 3);
      for (int y = 0; y < depth; y++) side.set(x, y, shade(rgb(95, 150, 55), static_cast<int>(hash3(x, y, 1, 34) % 20) - 10));
    }
    b.tex("grass_side", side);
    Image snowed = side;
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 4; y++) snowed.set(x, y, shade(rgb(240, 250, 250), -static_cast<int>(hash3(x, y, 2, 35) % 12)));
    b.tex("grass_side_snowed", snowed);
    Image myc = side;
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 4; y++) myc.set(x, y, shade(baseColor("mycelium_top"), static_cast<int>(hash3(x, y, 3, 36) % 16) - 8));
    b.tex("mycelium_side", myc);
    b.ensureSimple("mycelium_top");
    Image pod = side;
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 4; y++) pod.set(x, y, shade(baseColor("dirt_podzol_top"), static_cast<int>(hash3(x, y, 4, 37) % 16) - 8));
    b.tex("dirt_podzol_side", pod);
    b.ensureSimple("dirt_podzol_top");
  }

  // Fluidos animados
  {
    std::vector<Image> water, lava;
    for (int f = 0; f < 16; f++) {
      water.push_back(fluid(rgb(45, 85, 220, 170), 10, 41, f));
      lava.push_back(fluid(rgb(215, 95, 20), 25, 42, f));
    }
    for (const char* n : {"water_still", "water_flow"}) {
      b.tex(n, strip(water));
      pack->putJson(kTex + "blocks/" + n + ".png.mcmeta", {{"animation", {{"frametime", 2}}}});
    }
    for (const char* n : {"lava_still", "lava_flow"}) {
      b.tex(n, strip(lava));
      pack->putJson(kTex + "blocks/" + n + ".png.mcmeta", {{"animation", {{"frametime", 3}}}});
    }
  }

  // Colormaps: degradado propio (cálido/seco abajo-izquierda como en el formato)
  {
    Image grass(256, 256), foliage(256, 256);
    for (int y = 0; y < 256; y++)
      for (int x = 0; x < 256; x++) {
        const double t = 1.0 - x / 255.0, r = 1.0 - y / 255.0;  // temperatura, humedad
        grass.set(x, y, rgb(static_cast<int>(80 + 110 * t * (1 - r) + 20 * (1 - t)), static_cast<int>(120 + 70 * r + 20 * t),
                            static_cast<int>(60 + 40 * (1 - t))));
        foliage.set(x, y, rgb(static_cast<int>(60 + 100 * t * (1 - r)), static_cast<int>(110 + 60 * r + 10 * t),
                              static_cast<int>(40 + 30 * (1 - t))));
      }
    pack->putImage(kTex + "colormap/grass.png", grass);
    pack->putImage(kTex + "colormap/foliage.png", foliage);
  }

  // Fuente: se rasteriza stb_easy_font (dominio público) en la rejilla de 16x16 celdas de 8x8
  {
    Image font(128, 128, 0);
    static char buf[16 * 1024];
    for (int ch = 33; ch < 127; ch++) {
      char text[2] = {static_cast<char>(ch), 0};
      const int quads = stb_easy_font_print(0, 0, text, nullptr, buf, sizeof(buf));
      const int cellX = (ch % 16) * 8, cellY = (ch / 16) * 8;
      for (int q = 0; q < quads; q++) {
        const float* v = reinterpret_cast<const float*>(buf + q * 64);
        const int x0 = static_cast<int>(v[0]), y0 = static_cast<int>(v[1]);
        const int x1 = static_cast<int>(v[8]), y1 = static_cast<int>(v[9]);
        for (int y = y0; y < y1; y++)
          for (int x = x0; x < x1; x++) {
            // Mayúsculas en las filas 0..6 (como la fuente de 1.8); los rabos de g, j, p, q, y
            // bajan hasta la fila 8 y se juntan en la 7, la última de la celda
            const int py = std::min(y, 7);
            if (x >= 0 && x < 7 && py >= 0) font.set(cellX + x, cellY + py, 0xFFFFFFFF);
          }
      }
    }
    pack->putImage(kTex + "font/ascii.png", font);
  }

  // Escritura rúnica de la mesa de encantamientos: 26 letras inventadas (5x7 en celdas de 8x8), unas rayas
  // que unen puntos de una rejilla; las mayúsculas se dibujan igual que las minúsculas
  {
    Image sga(128, 128, 0);
    std::vector<u64> seen;
    u32 state = 0x9E3779B9u;
    auto rnd = [&state](u32 n) {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return state % n;
    };
    for (int letter = 0; letter < 26; letter++) {
      u64 bits = 0;
      bool ok = false;
      for (int attempt = 0; attempt < 200 && !ok; attempt++) {
        std::array<bool, 35> px{};
        int prevX = static_cast<int>(rnd(3)) * 2, prevY = static_cast<int>(rnd(4)) * 2;
        const int strokes = 3 + static_cast<int>(rnd(2));
        for (int s = 0; s < strokes; s++) {
          const int nx = static_cast<int>(rnd(3)) * 2, ny = static_cast<int>(rnd(4)) * 2;
          const int steps = std::max(std::abs(nx - prevX), std::abs(ny - prevY));
          for (int i = 0; i <= steps; i++) {
            const int x = prevX + (nx - prevX) * i / std::max(1, steps), y = prevY + (ny - prevY) * i / std::max(1, steps);
            px[static_cast<std::size_t>(y * 5 + x)] = true;
          }
          prevX = nx;
          prevY = ny;
        }
        bits = 0;
        int count = 0;
        for (std::size_t i = 0; i < px.size(); i++)
          if (px[i]) { bits |= u64{1} << i; count++; }
        ok = count >= 9 && std::find(seen.begin(), seen.end(), bits) == seen.end();
      }
      seen.push_back(bits);
      for (int cell : {'a' + letter, 'A' + letter}) {
        const int cx = (cell % 16) * 8, cy = (cell / 16) * 8;
        for (int i = 0; i < 35; i++)
          if (bits >> i & 1) sga.set(cx + i % 5, cy + i / 5, 0xFFFFFFFF);
      }
    }
    pack->putImage(kTex + "font/ascii_sga.png", sga);
  }

  // Entorno: sol, luna, nubes
  {
    Image sun(32, 32, 0);
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++) {
        const int d = std::max(std::abs(x - 16), std::abs(y - 16));
        if (d < 8) sun.set(x, y, rgb(255, 255, 210));
        else if (d < 11) sun.set(x, y, rgb(255, 230, 120, 255));
      }
    pack->putImage(kTex + "environment/sun.png", sun);
    Image moon(128, 64, 0);
    for (int p = 0; p < 8; p++) {
      const int ox = (p % 4) * 32, oy = (p / 4) * 32;
      const double shadow = std::cos(p * 3.14159 / 4.0);  // fase
      for (int y = 10; y < 22; y++)
        for (int x = 10; x < 22; x++) {
          const double u = (x - 15.5) / 6.0;
          const bool lit = p == 0 || (p < 4 ? u > shadow - 0.001 * p : u < -shadow);
          if (p == 4) continue;
          if (lit) moon.set(ox + x, oy + y, shade(rgb(220, 225, 235), static_cast<int>(hash3(x, y, p, 5) % 30) - 20));
          else moon.set(ox + x, oy + y, rgb(40, 45, 60, 255));
        }
    }
    pack->putImage(kTex + "environment/moon_phases.png", moon);
    Image clouds(256, 256, 0);
    for (int y = 0; y < 256; y++)
      for (int x = 0; x < 256; x++) {
        double v = 0;
        for (int o = 0; o < 3; o++) {
          const int s = 32 >> o;
          v += (hash3(x / s, y / s, o, 77) % 1000) / 1000.0 / (1 << o);
        }
        if (v > 1.12) clouds.set(x, y, 0xFFFFFFFF);
      }
    pack->putImage(kTex + "environment/clouds.png", clouds);
  }

  addGuiTextures(*pack);
  addItems(*pack);
  addDestroyStages(*pack);
  addEntityTextures(*pack);
  addArmorTextures(*pack);
  addXpOrbTexture(*pack);
  addMobItems(*pack);
  addAllItems(*pack);
  return pack;
}

}  // namespace mcw
