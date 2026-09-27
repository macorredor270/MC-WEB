// Pack "libre" (CC0): todo se dibuja aquí con ruido y formas simples. No reproduce ninguna
// textura de Mojang; solo usa los mismos nombres de archivo para encajar con las tablas.
#include "assets/cc0_pack.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include <stb_easy_font.h>

#include "assets/pack.h"
#include "core/random.h"
#include "data/blockstates.h"

namespace mcw {
namespace {

using json = nlohmann::json;
const std::string kTex = "assets/minecraft/textures/";

u32 rgb(int r, int g, int b, int a = 255) {
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

Image flower(u32 petal, u32 center, u32 seed, int stemHeight = 8) {
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
      {"stone", rgb(125, 125, 125)}, {"granite", rgb(150, 105, 85)}, {"granite_smooth", rgb(160, 112, 92)},
      {"diorite", rgb(190, 190, 192)}, {"diorite_smooth", rgb(198, 198, 200)}, {"andesite", rgb(132, 134, 133)},
      {"andesite_smooth", rgb(138, 140, 139)}, {"dirt", rgb(134, 96, 67)}, {"coarse_dirt", rgb(119, 85, 59)},
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

class Builder {
 public:
  explicit Builder(MemoryPack& p) : pack_(p) {}

  void tex(const std::string& name, Image img) { pack_.putImage(kTex + "blocks/" + name + ".png", std::move(img)); }
  bool hasTex(const std::string& name) const { return textures_.count(name) > 0; }
  void model(const std::string& name, json j) { pack_.putJson("assets/minecraft/models/block/" + name + ".json", j); }

  /// Textura simple con ruido para un nombre si no está ya definida.
  void ensureSimple(const std::string& name) {
    if (textures_.count(name)) return;
    textures_.insert(name);
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

  std::set<std::string> textures_;

 private:
  MemoryPack& pack_;
};

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

json face(const std::string& tex, const char* cull, int tint = -1, std::array<int, 4> uv = {0, 0, 16, 16}) {
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

}  // namespace

std::shared_ptr<MemoryPack> makeCC0Pack() {
  auto pack = std::make_shared<MemoryPack>("Pack libre (CC0)");
  Builder b(*pack);
  baseModels(b);

  // --- Blockstates: agrupar las variantes que usa cada fichero ---
  std::map<std::string, std::set<std::string>> variants;
  for (const auto& [state, ref] : allMappedStates()) variants[ref.file].insert(ref.variant);

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
      const std::string side = "log_" + w, top = "log_" + w + "_top";
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
      b.model(file, {{"parent", "block/leaves"}, {"textures", {{"all", "blocks/leaves_" + w}}}});
      b.tex("leaves_" + w, leaves(seedOf(w)));
      simple(file);
    } else if (endsWith(file, "_planks")) {
      const std::string w = file.substr(0, file.size() - 7);
      b.model(file, cubeAll("planks_" + w));
      b.tex("planks_" + w, planks(wood.at(w).first, seedOf(file)));
      simple(file);
    } else if (endsWith(file, "_sapling")) {
      const std::string w = file.substr(0, file.size() - 8);
      b.model(file, cross("sapling_" + w, false));
      b.tex("sapling_" + w, sapling(w == "birch" ? rgb(120, 160, 80) : (w == "spruce" ? rgb(50, 90, 50) : rgb(70, 130, 40))));
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
      const std::string t = "flower_" + file;
      b.model(file, cross(t, false));
      b.tex(t, flower(fc.at(file).first, fc.at(file).second, seedOf(file)));
      simple(file);
    } else if (file == "sunflower" || file == "syringa" || file == "double_grass" || file == "double_fern" || file == "double_rose" ||
               file == "paeonia") {
      const bool tinted = file == "double_grass" || file == "double_fern";
      for (const char* half : {"bottom", "top"}) {
        const std::string t = "double_plant_" + file + "_" + half;
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
          {"smooth_granite", "granite_smooth"}, {"smooth_diorite", "diorite_smooth"}, {"smooth_andesite", "andesite_smooth"},
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
            const int py = y - 1;  // subir una fila: las mayúsculas quedan en 0..6
            if (x >= 0 && x < 7 && py >= 0 && py < 8) font.set(cellX + x, cellY + py, 0xFFFFFFFF);
          }
      }
    }
    pack->putImage(kTex + "font/ascii.png", font);
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

  // GUI: punto de mira en icons.png (0,0)-(15,15)
  {
    Image icons(256, 256, 0);
    for (int i = 0; i < 9; i++) {
      icons.set(7, 3 + i, 0xFFFFFFFF);
      icons.set(3 + i, 7, 0xFFFFFFFF);
    }
    pack->putImage(kTex + "gui/icons.png", icons);
  }
  return pack;
}

}  // namespace mcw
