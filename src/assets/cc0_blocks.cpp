// Pack libre (CC0): bloques con forma propia (escaleras, losas, puertas, vallas, raíles,
// redstone, cultivos...). Geometría y texturas dibujadas aquí; solo se usan los mismos nombres de
// archivo que el formato de resource packs de 1.8 para que cualquier pack encima los sustituya.
#include <algorithm>
#include <cmath>
#include <functional>

#include "assets/cc0_internal.h"
#include "core/random.h"

namespace mcw::cc0 {

std::string woodTex(const std::string& wood) { return wood == "dark_oak" ? "big_oak" : wood; }

namespace {

using V3 = std::array<float, 3>;
const char* kFaces[6] = {"down", "up", "north", "south", "west", "east"};

// ---------------------------------------------------------------------------
// Geometría
// ---------------------------------------------------------------------------

/// Cara de la caja que toca el borde del bloque (para ocultarla si el vecino es opaco).
const char* edgeCull(int f, const V3& a, const V3& b) {
  switch (f) {
    case 0: return a[1] <= 0 ? "down" : nullptr;
    case 1: return b[1] >= 16 ? "up" : nullptr;
    case 2: return a[2] <= 0 ? "north" : nullptr;
    case 3: return b[2] >= 16 ? "south" : nullptr;
    case 4: return a[0] <= 0 ? "west" : nullptr;
    default: return b[0] >= 16 ? "east" : nullptr;
  }
}

/// Caja con una textura por cara ("" = sin cara). Orden: abajo, arriba, norte, sur, oeste, este.
json boxF(V3 a, V3 b, const std::array<std::string, 6>& tex, int tint = -1) {
  json faces = json::object();
  for (int f = 0; f < 6; f++) {
    if (tex[f].empty()) continue;
    json j = {{"texture", tex[f]}};
    if (const char* c = edgeCull(f, a, b)) j["cullface"] = c;
    if (tint >= 0) j["tintindex"] = tint;
    faces[kFaces[f]] = j;
  }
  return {{"from", a}, {"to", b}, {"faces", faces}};
}

/// Caja con la misma textura en todas las caras.
json box(V3 a, V3 b, const std::string& t, int tint = -1) { return boxF(a, b, {t, t, t, t, t, t}, tint); }

/// Caja con arriba/abajo/lados.
json boxTBS(V3 a, V3 b, const std::string& top, const std::string& bottom, const std::string& side) {
  return boxF(a, b, {bottom, top, side, side, side, side});
}

/// Plano vertical o inclinado: una sola pareja de caras enfrentadas (sin ocultación).
json plane(V3 a, V3 b, const char* f1, const char* f2, const std::string& t, int tint = -1) {
  json faces = {{f1, {{"texture", t}}}, {f2, {{"texture", t}}}};
  if (tint >= 0) {
    faces[f1]["tintindex"] = tint;
    faces[f2]["tintindex"] = tint;
  }
  return {{"from", a}, {"to", b}, {"faces", faces}, {"shade", false}};
}

json withRot(json el, V3 origin, const char* axis, float angle, bool rescale = false) {
  el["rotation"] = {{"origin", origin}, {"axis", axis}, {"angle", angle}, {"rescale", rescale}};
  return el;
}

json mdl(const std::map<std::string, std::string>& textures, json elements, bool ao = true) {
  json t = json::object();
  for (auto& [k, v] : textures) t[k] = v;
  json m = {{"textures", t}, {"elements", std::move(elements)}};
  if (!ao) m["ambientocclusion"] = false;
  return m;
}

json var(const std::string& model, int x = 0, int y = 0, bool uvlock = false) {
  json v = {{"model", model}};
  if (x) v["x"] = x;
  if (y) v["y"] = ((y % 360) + 360) % 360;
  if (uvlock) v["uvlock"] = true;
  return v;
}

/// Giro (y) de un modelo hecho mirando al norte para una dirección horizontal.
int rotNorth(const std::string& facing) {
  if (facing == "east") return 90;
  if (facing == "south") return 180;
  if (facing == "west") return 270;
  return 0;
}

/// Valor de una propiedad en una clave de variante ("facing=east,half=top" -> prop("half") = "top").
std::string prop(const std::string& key, const std::string& name) {
  const std::string k = name + "=";
  std::size_t p = 0;
  while (p < key.size()) {
    const std::size_t e = key.find(',', p);
    const std::string part = key.substr(p, e == std::string::npos ? std::string::npos : e - p);
    if (part.rfind(k, 0) == 0) return part.substr(k.size());
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return {};
}

bool endsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// ---------------------------------------------------------------------------
// Texturas
// ---------------------------------------------------------------------------

void px(Image& img, int x, int y, u32 c) {
  if (x >= 0 && y >= 0 && x < img.width && y < img.height) img.set(x, y, c);
}

void border(Image& img, u32 c) {
  for (int i = 0; i < 16; i++) { img.set(i, 0, c); img.set(i, 15, c); img.set(0, i, c); img.set(15, i, c); }
}

u32 woodColor(const std::string& w) { return woods().at(w).first; }

Image plankOf(const std::string& w) { return planks(woodColor(w), seedOf("planks_" + w)); }

Image stoneTex(u32 seed = 0) { return noisy(baseColor("stone"), 10, seed ? seed : seedOf("stone")); }

Image cobbleTex() {
  Image img = noisy(rgb(120, 120, 120), 10, seedOf("cobblestone"));
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
      if ((hash3(x / 4, y / 4, 12, 1) + x + y) % 7 == 0) img.set(x, y, rgb(85, 85, 85));
  return img;
}

/// Planta de cultivo en `stage` de `stages` (tallos que crecen y, al final, fruto).
Image cropTex(u32 stem, u32 fruit, int stage, int stages, u32 seed, bool bulbs) {
  Image img(16, 16, 0);
  const int h = 3 + (12 * (stage + 1)) / stages;
  Random r(seed);
  for (int k = 0; k < 6; k++) {
    int x = 1 + k * 3 - (k > 2 ? 1 : 0);
    for (int y = 15; y > 15 - h + r.nextInt(3) && y >= 0; y--) {
      px(img, x, y, shade(stem, r.nextInt(30) - 15));
      if (r.nextInt(5) == 0) x += r.nextInt(3) - 1;
    }
    if (stage == stages - 1) {
      if (bulbs) disc(img, x + 0.5, 15.5 - h / 3.0, 1.6, fruit, shade(fruit, -40));
      else for (int y = 15 - h; y < 15 - h + 4 && y < 16; y++) if (y >= 0) px(img, x, y, shade(fruit, (y % 2) * 15));
    }
  }
  return img;
}

Image railTex(u32 metal, bool turned, u32 dot) {
  Image img(16, 16, 0);
  const u32 wood = rgb(110, 80, 45);
  if (!turned) {
    for (int y = 1; y < 16; y += 4) fillRect(img, 1, y, 14, 2, shade(wood, (y % 8) ? -8 : 4));
    for (int y = 0; y < 16; y++) { img.set(3, y, metal); img.set(4, y, shade(metal, -30)); img.set(11, y, metal); img.set(12, y, shade(metal, -30)); }
    if (dot) for (int y = 2; y < 16; y += 4) { img.set(7, y, dot); img.set(8, y, dot); }
  } else {
    for (int k = 0; k < 4; k++) fillRect(img, 1 + k * 4, 1 + k * 4, 4, 2, wood);
    for (int a = 0; a < 16; a++) {
      const double t = a / 15.0 * 1.5708;
      px(img, 3 + static_cast<int>(std::round(std::sin(t) * 13)), 16 - 1 - static_cast<int>(std::round((1 - std::cos(t)) * 12)) - 1, metal);
      px(img, 11 + static_cast<int>(std::round(std::sin(t) * 5)), 16 - 1 - static_cast<int>(std::round((1 - std::cos(t)) * 4)) - 1, metal);
    }
  }
  return img;
}

Image doorTex(const std::string& kind, bool upper) {
  const bool iron = kind == "iron";
  const u32 c = iron ? rgb(205, 205, 205) : woodColor(kind == "wood" ? "oak" : kind);
  Image img = iron ? noisy(c, 6, seedOf("door_iron")) : plankOf(kind == "wood" ? "oak" : kind);
  border(img, shade(c, -45));
  for (int y = 0; y < 16; y++) { img.set(1, y, shade(c, -25)); img.set(14, y, shade(c, -25)); }
  if (upper && kind != "acacia" && kind != "dark_oak") {
    // Ventanas (el roble oscuro y la acacia no tienen, como en 1.8)
    for (int y = 3; y < 13; y++)
      for (int x = 3; x < 13; x++)
        if (!(x == 7 || x == 8 || y == 7 || y == 8)) img.set(x, y, 0);
  } else if (upper) {
    for (int k = 3; k < 13; k += 3) fillRect(img, 3, k, 10, 1, shade(c, -35));
  } else {
    fillRect(img, 3, 3, 10, 1, shade(c, -30));
    fillRect(img, 3, 12, 10, 1, shade(c, -30));
    if (iron) { img.set(12, 1, rgb(90, 90, 90)); img.set(12, 14, rgb(90, 90, 90)); }
    else { img.set(12, 0, rgb(60, 60, 60)); img.set(12, 1, rgb(60, 60, 60)); }  // manilla
  }
  return img;
}

Image fireFrame(int f, int layer) {
  Image img(16, 16, 0);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double wave = std::sin((x + f * 0.7 + layer * 3) * 0.9) * 2.0 + std::cos((y + f) * 0.5) * 1.5;
      const double hgt = 11 + wave + (hash3(x, f, layer, 71) % 4);
      const int fromBottom = 15 - y;
      if (fromBottom < hgt) {
        const double t = fromBottom / hgt;
        img.set(x, y, t < 0.35 ? rgb(255, 240, 150) : (t < 0.7 ? rgb(250, 170, 40) : rgb(220, 90, 20)));
      }
    }
  return img;
}

Image portalFrame(int f) {
  Image img(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double a = std::atan2(y - 7.5, x - 7.5), d = std::hypot(x - 7.5, y - 7.5);
      const double v = std::sin(a * 3 + d * 0.8 - f * 0.4);
      img.set(x, y, rgb(90 + static_cast<int>(v * 40), 20, 170 + static_cast<int>(v * 50), 190));
    }
  return img;
}

Image repeaterTex(bool comparator, bool on) {
  Image img = noisy(rgb(160, 160, 160), 6, seedOf("repeater"));
  const u32 wire = on ? rgb(250, 30, 20) : rgb(100, 10, 10);
  for (int y = 2; y < 14; y++) { img.set(7, y, wire); img.set(8, y, wire); }
  if (comparator) {
    for (int x = 3; x < 13; x++) img.set(x, 11, wire);
    img.set(3, 10, wire); img.set(12, 10, wire);
  }
  // Flecha hacia el sur (la salida)
  for (int k = 0; k < 3; k++) { img.set(7 - k, 13 - k, wire); img.set(8 + k, 13 - k, wire); }
  return img;
}

Image mushroomSkin(bool red) {
  const u32 c = red ? rgb(200, 40, 40) : rgb(145, 110, 80);
  Image img = noisy(c, 8, seedOf(red ? "mred" : "mbrown"));
  if (red) speckle(img, rgb(235, 235, 225), 5, 4, 91);
  return img;
}

Image paneTop(u32 c) {
  Image img(16, 16, 0);
  for (int y = 0; y < 16; y++) { img.set(7, y, c); img.set(8, y, shade(c, -20)); }
  return img;
}

Image vineTex() {
  Image img(16, 16, 0);
  Random r(55);
  for (int k = 0; k < 6; k++) {
    int x = r.nextInt(16);
    for (int y = 0; y < 16; y++) {
      px(img, x, y, shade(rgb(150, 150, 150), r.nextInt(40) - 25));
      if (r.nextInt(3) == 0) { px(img, x + 1, y, rgb(120, 120, 120)); px(img, x - 1, y, rgb(165, 165, 165)); }
      if (r.nextInt(4) == 0) x = (x + r.nextInt(3) - 1) & 15;
    }
  }
  return img;
}

Image stemTex(bool connected) {
  Image img(16, 16, 0);
  const u32 c = rgb(170, 170, 170);
  if (!connected) {
    for (int y = 0; y < 16; y++) { img.set(7 + (y % 5 == 0 ? 1 : 0), y, c); if (y % 4 == 1) { img.set(6, y, c); img.set(9, y, c); } }
  } else {
    for (int x = 0; x < 16; x++) img.set(x, 8 + static_cast<int>(std::sin(x * 0.5) * 1.5), c);
    for (int y = 8; y < 16; y++) img.set(1, y, c);
  }
  return img;
}

Image redstoneDust(bool cross) {
  Image img(16, 16, 0);
  const u32 c = rgb(230, 230, 230);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const bool ns = x >= 6 && x <= 9, ew = y >= 6 && y <= 9;
      const bool on = cross ? (ns || ew) : ns;
      if (on && hash3(x, y, 3, 17) % 5) img.set(x, y, shade(c, static_cast<int>(hash3(x, y, 4, 17) % 40) - 20));
    }
  return img;
}

/// Dibuja una textura con forma propia; false si el nombre no es de estas.
bool special(const std::string& n, Image& img, std::vector<Image>* anim) {
  const u32 iron = rgb(210, 210, 210), gold = rgb(245, 205, 50), red = rgb(200, 30, 25);
  // Madera
  for (const auto& [w, c] : woods())
    if (n == "planks_" + woodTex(w)) { img = plankOf(w); return true; }
  if (n == "stone") { img = stoneTex(); return true; }
  if (n == "cobblestone") { img = cobbleTex(); return true; }
  if (n == "stone_slab_top") { img = stoneTex(seedOf(n)); border(img, rgb(105, 105, 105)); return true; }
  if (n == "stone_slab_side") {
    img = stoneTex(seedOf(n));
    for (int x = 0; x < 16; x++) { img.set(x, 0, rgb(105, 105, 105)); img.set(x, 7, rgb(95, 95, 95)); img.set(x, 8, rgb(150, 150, 150)); img.set(x, 15, rgb(95, 95, 95)); }
    return true;
  }
  if (n == "furnace_side" || n == "furnace_top") { img = stoneTex(seedOf(n)); border(img, rgb(90, 90, 90)); return true; }
  if (n == "furnace_front_off" || n == "furnace_front_on") {
    img = stoneTex(seedOf("furnace_front"));
    border(img, rgb(90, 90, 90));
    fillRect(img, 3, 8, 10, 5, n == "furnace_front_on" ? rgb(250, 150, 40) : rgb(25, 25, 25));
    if (n == "furnace_front_on") for (int x = 4; x < 12; x += 2) img.set(x, 9, rgb(255, 230, 120));
    fillRect(img, 3, 3, 10, 2, rgb(70, 70, 70));
    return true;
  }
  if (n.rfind("dispenser_front", 0) == 0 || n.rfind("dropper_front", 0) == 0) {
    img = stoneTex(seedOf(n));
    border(img, rgb(90, 90, 90));
    if (n.rfind("dispenser", 0) == 0) disc(img, 7.5, 7.5, 3.4, rgb(20, 20, 20), rgb(60, 60, 60));
    else fillRect(img, 5, 5, 6, 6, rgb(20, 20, 20));
    if (endsWith(n, "vertical")) for (int i = 2; i < 14; i += 11) fillRect(img, i, 2, 1, 12, rgb(70, 70, 70));
    return true;
  }
  if (n == "noteblock" || n == "jukebox_side") {
    img = noisy(rgb(100, 65, 45), 8, seedOf(n));
    border(img, rgb(70, 45, 30));
    if (n == "noteblock") { fillRect(img, 6, 4, 1, 7, rgb(40, 25, 15)); disc(img, 5, 11, 1.7, rgb(40, 25, 15), rgb(40, 25, 15)); fillRect(img, 6, 4, 4, 1, rgb(40, 25, 15)); }
    return true;
  }
  if (n == "jukebox_top") {
    img = noisy(rgb(100, 65, 45), 8, seedOf(n));
    border(img, rgb(70, 45, 30));
    fillRect(img, 3, 7, 10, 2, rgb(20, 20, 20));
    return true;
  }
  if (n.rfind("bed_", 0) == 0) {
    const bool head = n.find("head") != std::string::npos;
    const u32 blanket = rgb(170, 30, 30), wood = woodColor("oak");
    if (endsWith(n, "_top")) {
      img = noisy(blanket, 6, seedOf(n));
      if (head) fillRect(img, 2, 1, 12, 6, rgb(235, 235, 235));
      for (int y = 0; y < 16; y++) { img.set(0, y, shade(blanket, -40)); img.set(15, y, shade(blanket, -40)); }
    } else {
      img = Image(16, 16, 0);
      fillRect(img, 0, 7, 16, 3, blanket);
      fillRect(img, 0, 10, 16, 3, wood);
      fillRect(img, 0, 13, 2, 3, shade(wood, -30));
      if (endsWith(n, "_side")) fillRect(img, 14, 13, 2, 3, shade(wood, -30));
      else fillRect(img, 14, 13, 2, 3, shade(wood, -30));
      if (head && endsWith(n, "_side")) fillRect(img, 0, 7, 5, 3, rgb(235, 235, 235));
    }
    return true;
  }
  if (n == "rail_normal") { img = railTex(iron, false, 0); return true; }
  if (n == "rail_normal_turned") { img = railTex(iron, true, 0); return true; }
  if (n == "rail_golden") { img = railTex(gold, false, rgb(90, 20, 20)); return true; }
  if (n == "rail_golden_powered") { img = railTex(gold, false, rgb(255, 40, 30)); return true; }
  if (n == "rail_detector") { img = railTex(iron, false, rgb(90, 20, 20)); fillRect(img, 6, 6, 4, 4, rgb(90, 90, 90)); return true; }
  if (n == "rail_detector_powered") { img = railTex(iron, false, rgb(255, 40, 30)); fillRect(img, 6, 6, 4, 4, rgb(220, 40, 30)); return true; }
  if (n == "rail_activator") { img = railTex(iron, false, rgb(90, 20, 20)); for (int y = 0; y < 16; y += 4) fillRect(img, 6, y, 4, 1, rgb(120, 60, 40)); return true; }
  if (n == "rail_activator_powered") { img = railTex(iron, false, rgb(255, 40, 30)); for (int y = 0; y < 16; y += 4) fillRect(img, 6, y, 4, 1, rgb(230, 70, 40)); return true; }
  if (n == "piston_top_normal" || n == "piston_top_sticky") {
    img = plankOf("oak");
    border(img, rgb(90, 90, 90));
    if (n == "piston_top_sticky") { fillRect(img, 3, 3, 10, 10, rgb(110, 185, 90)); speckle(img, rgb(140, 210, 110), 6, 3, 9); }
    return true;
  }
  if (n == "piston_side") {
    img = stoneTex(seedOf(n));
    const Image p = plankOf("oak");
    for (int y = 0; y < 4; y++) for (int x = 0; x < 16; x++) img.set(x, y, p.get(x, y));
    fillRect(img, 6, 4, 4, 9, rgb(170, 170, 170));
    return true;
  }
  if (n == "piston_bottom") { img = stoneTex(seedOf(n)); border(img, rgb(90, 90, 90)); return true; }
  if (n == "piston_inner") { img = stoneTex(seedOf(n)); fillRect(img, 6, 6, 4, 4, rgb(180, 180, 180)); border(img, rgb(90, 90, 90)); return true; }
  if (n == "fire_layer_0" || n == "fire_layer_1") {
    const int layer = n.back() - '0';
    for (int f = 0; f < 16; f++) anim->push_back(fireFrame(f, layer));
    return true;
  }
  if (n == "portal") { for (int f = 0; f < 16; f++) anim->push_back(portalFrame(f)); return true; }
  if (n == "mob_spawner" || n == "iron_bars") {
    img = Image(16, 16, 0);
    const u32 c = n == "mob_spawner" ? rgb(40, 50, 60) : rgb(110, 110, 115);
    for (int i = 0; i < 16; i++)
      for (int k : {1, 5, 10, 14}) { img.set(k, i, shade(c, (i % 3) * 8)); if (n == "mob_spawner") img.set(i, k, shade(c, 12)); }
    if (n == "iron_bars") for (int x = 0; x < 16; x++) { img.set(x, 0, c); img.set(x, 15, c); }
    return true;
  }
  if (n.rfind("wheat_stage_", 0) == 0) { const int s = n.back() - '0'; img = cropTex(s < 7 ? rgb(60, 150, 40) : rgb(190, 170, 70), rgb(220, 190, 90), s, 8, 40 + s, false); return true; }
  if (n.rfind("carrots_stage_", 0) == 0) { const int s = n.back() - '0'; img = cropTex(rgb(60, 160, 45), rgb(240, 140, 30), s, 4, 50 + s, true); return true; }
  if (n.rfind("potatoes_stage_", 0) == 0) { const int s = n.back() - '0'; img = cropTex(rgb(70, 150, 50), rgb(210, 175, 100), s, 4, 60 + s, true); return true; }
  if (n.rfind("nether_wart_stage_", 0) == 0) { const int s = n.back() - '0'; img = cropTex(rgb(150, 30, 40), rgb(190, 40, 50), s, 3, 70 + s, true); return true; }
  if (n == "farmland_dry" || n == "farmland_wet") {
    img = noisy(n == "farmland_wet" ? rgb(80, 50, 30) : rgb(125, 88, 60), 8, seedOf(n));
    for (int y = 1; y < 16; y += 4) for (int x = 0; x < 16; x++) img.set(x, y, shade(img.get(x, y), -30));
    return true;
  }
  if (n.rfind("door_", 0) == 0) {
    const bool upper = endsWith(n, "_upper");
    std::string kind = n.substr(5, n.rfind('_') - 5);
    if (kind == "big_oak") kind = "dark_oak";
    img = doorTex(kind, upper);
    return true;
  }
  if (n == "trapdoor" || n == "iron_trapdoor") {
    const bool ironT = n == "iron_trapdoor";
    img = ironT ? noisy(iron, 6, seedOf(n)) : plankOf("oak");
    border(img, ironT ? rgb(150, 150, 150) : shade(woodColor("oak"), -45));
    for (int y = 3; y < 13; y++)
      for (int x = 3; x < 13; x++)
        if ((x % 5 != 2) && (y % 5 != 2) && !ironT) img.set(x, y, 0);
    if (ironT) for (int y = 3; y < 13; y += 3) fillRect(img, 3, y, 10, 1, rgb(160, 160, 160));
    return true;
  }
  if (n == "ladder") {
    img = Image(16, 16, 0);
    const u32 c = woodColor("oak");
    for (int y = 0; y < 16; y++) { img.set(2, y, shade(c, -20)); img.set(3, y, c); img.set(12, y, shade(c, -20)); img.set(13, y, c); }
    for (int y = 1; y < 16; y += 4) fillRect(img, 2, y, 12, 2, shade(c, 10));
    return true;
  }
  if (n == "lever") {
    img = Image(16, 16, 0);
    for (int y = 6; y < 16; y++) { img.set(7, y, rgb(115, 85, 50)); img.set(8, y, rgb(95, 70, 40)); }
    return true;
  }
  if (n == "redstone_torch_on" || n == "redstone_torch_off") {
    img = torchTex();
    const bool on = n == "redstone_torch_on";
    img.set(7, 6, on ? rgb(255, 60, 40) : rgb(110, 30, 30)); img.set(8, 6, on ? rgb(230, 30, 20) : rgb(90, 25, 25));
    img.set(7, 7, on ? rgb(200, 20, 15) : rgb(80, 20, 20)); img.set(8, 7, on ? rgb(180, 15, 10) : rgb(70, 18, 18));
    return true;
  }
  if (n == "pumpkin_side" || n == "pumpkin_top" || n.rfind("pumpkin_face", 0) == 0) {
    const u32 c = rgb(215, 125, 25);
    img = noisy(c, 7, seedOf("pumpkin"));
    if (n != "pumpkin_top") for (int x = 0; x < 16; x += 4) for (int y = 0; y < 16; y++) img.set(x, y, shade(c, -35));
    else { disc(img, 7.5, 7.5, 2.0, rgb(100, 90, 40), rgb(80, 70, 30)); }
    if (n.rfind("pumpkin_face", 0) == 0) {
      const u32 hole = n == "pumpkin_face_on" ? rgb(255, 220, 90) : rgb(40, 20, 5);
      fillRect(img, 3, 4, 3, 3, hole); fillRect(img, 10, 4, 3, 3, hole);
      fillRect(img, 3, 10, 10, 2, hole); fillRect(img, 5, 12, 2, 1, hole); fillRect(img, 9, 12, 2, 1, hole);
    }
    return true;
  }
  if (n == "melon_side" || n == "melon_top") {
    const u32 c = rgb(110, 150, 40);
    img = noisy(c, 8, seedOf(n));
    if (n == "melon_side") for (int x = 1; x < 16; x += 3) for (int y = 0; y < 16; y++) img.set(x, y, shade(c, 25));
    else disc(img, 7.5, 7.5, 2.0, rgb(95, 130, 35), rgb(80, 110, 30));
    return true;
  }
  if (n.rfind("cake_", 0) == 0) {
    const u32 cream = rgb(245, 240, 235), sponge = rgb(170, 100, 55);
    if (n == "cake_top") { img = noisy(cream, 4, seedOf(n)); speckle(img, rgb(210, 30, 30), 4, 1, 3); }
    else if (n == "cake_bottom") img = noisy(sponge, 6, seedOf(n));
    else {
      img = noisy(sponge, 6, seedOf(n));
      for (int y = 0; y < 4; y++) for (int x = 0; x < 16; x++) img.set(x, y, shade(cream, -static_cast<int>(hash3(x, y, 1, 9) % 12)));
      if (n == "cake_inner") for (int y = 9; y < 11; y++) for (int x = 0; x < 16; x++) img.set(x, y, rgb(230, 70, 70));
      else img.set(3, 4, cream), img.set(9, 5, cream), img.set(13, 4, cream);
    }
    return true;
  }
  if (n == "repeater_off" || n == "repeater_on") { img = repeaterTex(false, n == "repeater_on"); return true; }
  if (n == "comparator_off" || n == "comparator_on") { img = repeaterTex(true, n == "comparator_on"); return true; }
  if (n == "mushroom_block_skin_brown") { img = mushroomSkin(false); return true; }
  if (n == "mushroom_block_skin_red") { img = mushroomSkin(true); return true; }
  if (n == "mushroom_block_skin_stem") { img = noisy(rgb(215, 210, 190), 6, seedOf(n)); for (int x = 2; x < 16; x += 5) for (int y = 0; y < 16; y++) img.set(x, y, rgb(195, 190, 170)); return true; }
  if (n == "mushroom_block_inside") { img = noisy(rgb(215, 180, 140), 5, seedOf(n)); return true; }
  if (n == "glass_pane_top") { img = paneTop(rgb(200, 220, 230)); return true; }
  for (const auto& [color, c] : dyeColors())
    if (n == "glass_pane_top_" + color) { img = paneTop(c); return true; }
  if (n == "vine") { img = vineTex(); return true; }
  if (n == "pumpkin_stem_disconnected" || n == "melon_stem_disconnected") { img = stemTex(false); return true; }
  if (n == "pumpkin_stem_connected" || n == "melon_stem_connected") { img = stemTex(true); return true; }
  if (n.rfind("enchanting_table_", 0) == 0) {
    if (n == "enchanting_table_top") {
      img = noisy(rgb(40, 20, 30), 6, seedOf(n));
      fillRect(img, 3, 4, 10, 8, rgb(200, 30, 40));
      fillRect(img, 7, 4, 2, 8, rgb(240, 240, 230));
      border(img, rgb(50, 190, 170));
    } else if (n == "enchanting_table_side") {
      img = noisy(rgb(20, 18, 30), 6, seedOf(n));
      for (int x = 0; x < 16; x++) for (int y = 0; y < 4; y++) img.set(x, y, shade(rgb(200, 30, 40), -static_cast<int>(hash3(x, y, 1, 5) % 20)));
      fillRect(img, 0, 4, 16, 1, rgb(50, 190, 170));
    } else img = noisy(rgb(20, 18, 30), 6, seedOf(n));
    return true;
  }
  if (n == "brewing_stand") {
    img = Image(16, 16, 0);
    for (int y = 1; y < 16; y++) img.set(8, y, rgb(200, 180, 60));
    for (int k = 0; k < 3; k++) disc(img, 2.5 + k * 5.5, 12.5, 2.0, rgb(180, 200, 230), rgb(140, 160, 190));
    return true;
  }
  if (n == "brewing_stand_base") { img = noisy(rgb(110, 110, 110), 8, seedOf(n)); return true; }
  if (n.rfind("cauldron_", 0) == 0) {
    img = noisy(rgb(60, 60, 64), 6, seedOf(n));
    if (n == "cauldron_top") fillRect(img, 2, 2, 12, 12, 0);
    if (n == "cauldron_side") { border(img, rgb(40, 40, 44)); fillRect(img, 4, 13, 8, 3, 0); }
    if (n == "cauldron_bottom") fillRect(img, 4, 4, 8, 8, rgb(45, 45, 48));
    return true;
  }
  if (n.rfind("endframe_", 0) == 0) {
    if (n == "endframe_eye") {
      img = Image(16, 16, 0);
      disc(img, 7.5, 7.5, 4.2, rgb(40, 110, 90), rgb(20, 70, 60));
      disc(img, 7.5, 7.5, 1.6, rgb(10, 30, 25), rgb(10, 30, 25));
    } else {
      img = noisy(rgb(220, 222, 158), 8, seedOf(n));
      if (n == "endframe_top") { fillRect(img, 4, 4, 8, 8, rgb(40, 110, 100)); border(img, rgb(60, 90, 80)); }
      else for (int x = 0; x < 16; x++) for (int y = 0; y < 3; y++) img.set(x, y, rgb(60, 110, 100));
    }
    return true;
  }
  if (n == "dragon_egg") { img = noisy(rgb(20, 10, 30), 6, seedOf(n)); speckle(img, rgb(110, 40, 140), 8, 3, 7); return true; }
  if (n == "redstone_lamp_off" || n == "redstone_lamp_on") {
    const bool on = n == "redstone_lamp_on";
    img = noisy(on ? rgb(240, 200, 130) : rgb(110, 60, 35), 8, seedOf(n));
    for (int k = 2; k < 16; k += 5) { fillRect(img, k, 0, 1, 16, on ? rgb(200, 150, 90) : rgb(80, 45, 25)); fillRect(img, 0, k, 16, 1, on ? rgb(200, 150, 90) : rgb(80, 45, 25)); }
    return true;
  }
  if (n.rfind("cocoa_stage_", 0) == 0) {
    const int s = n.back() - '0';
    const u32 c = s == 0 ? rgb(120, 150, 50) : (s == 1 ? rgb(170, 120, 60) : rgb(150, 85, 40));
    img = noisy(c, 8, seedOf(n));
    return true;
  }
  if (n == "trip_wire") { img = Image(16, 16, 0); for (int x = 0; x < 16; x++) img.set(x, 7, rgb(220, 220, 220, 200)); return true; }
  if (n == "trip_wire_source") {
    img = Image(16, 16, 0);
    for (int y = 2; y < 14; y++) { img.set(7, y, rgb(115, 85, 50)); img.set(8, y, rgb(95, 70, 40)); }
    disc(img, 7.5, 4.5, 2.0, iron, rgb(150, 150, 150));
    return true;
  }
  if (n == "command_block") {
    img = noisy(rgb(180, 130, 90), 6, seedOf(n));
    fillRect(img, 3, 3, 10, 10, rgb(80, 80, 90));
    fillRect(img, 5, 6, 3, 1, rgb(230, 230, 230)); fillRect(img, 5, 8, 6, 1, rgb(230, 230, 230));
    return true;
  }
  if (n == "beacon") { img = noisy(rgb(120, 230, 220), 10, seedOf(n)); border(img, rgb(80, 200, 190)); return true; }
  if (n == "flower_pot") { img = noisy(rgb(150, 80, 55), 6, seedOf(n)); return true; }
  if (n == "anvil_base") { img = noisy(rgb(70, 70, 70), 6, seedOf(n)); return true; }
  if (n.rfind("anvil_top_damaged_", 0) == 0) {
    const int d = n.back() - '0';
    img = noisy(rgb(80, 80, 80), 6, seedOf(n));
    border(img, rgb(55, 55, 55));
    Random r(81 + d);
    for (int k = 0; k < d * 6; k++) px(img, r.nextInt(16), r.nextInt(16), rgb(40, 40, 40));
    return true;
  }
  if (n == "daylight_detector_top" || n == "daylight_detector_inverted_top") {
    const bool inv = n == "daylight_detector_inverted_top";
    img = noisy(inv ? rgb(80, 110, 150) : rgb(200, 190, 150), 6, seedOf(n));
    for (int k = 1; k < 16; k += 5) { fillRect(img, k, 0, 1, 16, rgb(90, 70, 50)); fillRect(img, 0, k, 16, 1, rgb(90, 70, 50)); }
    return true;
  }
  if (n == "daylight_detector_side") { img = plankOf("oak"); fillRect(img, 0, 0, 16, 10, 0); return true; }
  if (n == "redstone_block") { img = noisy(rgb(170, 25, 15), 12, seedOf(n)); border(img, rgb(120, 15, 10)); return true; }
  if (n == "hopper_outside" || n == "hopper_inside" || n == "hopper_top") {
    img = noisy(rgb(70, 70, 72), 6, seedOf(n));
    if (n == "hopper_top") { fillRect(img, 2, 2, 12, 12, rgb(40, 40, 42)); for (int k = 3; k < 13; k += 3) fillRect(img, k, 2, 1, 12, rgb(70, 70, 72)); }
    border(img, rgb(50, 50, 52));
    return true;
  }
  if (n == "quartz_block_lines" || n == "quartz_block_lines_top") {
    img = noisy(rgb(235, 230, 222), 4, seedOf(n));
    if (n == "quartz_block_lines") for (int x : {1, 14}) for (int y = 0; y < 16; y++) img.set(x, y, rgb(210, 205, 196));
    else border(img, rgb(210, 205, 196));
    return true;
  }
  if (n == "prismarine_rough") { img = noisy(rgb(100, 160, 145), 14, seedOf(n)); speckle(img, rgb(80, 130, 150), 8, 4, 5); return true; }
  if (n == "prismarine_bricks") {
    img = noisy(rgb(100, 170, 150), 8, seedOf(n));
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (y % 8 == 7 || (x + (y / 8) * 8) % 16 == 0) img.set(x, y, rgb(70, 120, 110));
    return true;
  }
  if (n == "prismarine_dark") {
    img = noisy(rgb(50, 90, 75), 8, seedOf(n));
    for (int k = 0; k < 16; k += 5) { fillRect(img, k, 0, 1, 16, rgb(35, 65, 55)); fillRect(img, 0, k, 16, 1, rgb(35, 65, 55)); }
    return true;
  }
  if (n == "hay_block_side" || n == "hay_block_top") {
    const u32 c = rgb(190, 165, 40);
    img = noisy(c, 10, seedOf(n));
    if (n == "hay_block_side") {
      for (int x = 0; x < 16; x++) for (int y = 0; y < 16; y++) if (hash3(x, 0, 3, 5) % 3 == 0) img.set(x, y, shade(c, -20));
      for (int y : {3, 12}) fillRect(img, 0, y, 16, 1, rgb(120, 90, 40));
    }
    return true;
  }
  if (n.rfind("red_sandstone_", 0) == 0) {
    const u32 c = rgb(185, 100, 35);
    img = noisy(c, 6, seedOf(n));
    if (n == "red_sandstone_normal") for (int x = 0; x < 16; x++) { img.set(x, 3, shade(c, -25)); img.set(x, 12, shade(c, -20)); }
    if (n == "red_sandstone_carved") { border(img, shade(c, -30)); disc(img, 7.5, 7.5, 3.0, shade(c, 15), shade(c, -30)); }
    return true;
  }
  if (n == "redstone_dust_cross") { img = redstoneDust(true); return true; }
  if (n == "redstone_dust_line") { img = redstoneDust(false); return true; }
  if (n == "crafting_table_top") {
    img = plankOf("oak");
    border(img, shade(woodColor("oak"), -40));
    for (int k = 4; k < 13; k += 4) { fillRect(img, k, 1, 1, 14, shade(woodColor("oak"), -30)); fillRect(img, 1, k, 14, 1, shade(woodColor("oak"), -30)); }
    return true;
  }
  if (n == "crafting_table_side" || n == "crafting_table_front") {
    img = plankOf("oak");
    for (int x = 0; x < 16; x++) for (int y = 0; y < 3; y++) img.set(x, y, shade(woodColor("oak"), -35));
    if (n == "crafting_table_front") { line(img, 3, 12, 8, 7, rgb(180, 180, 180)); line(img, 3, 7, 3, 12, rgb(120, 90, 50)); fillRect(img, 9, 6, 4, 2, rgb(150, 150, 150)); line(img, 11, 8, 11, 13, rgb(120, 90, 50)); }
    else { line(img, 4, 13, 10, 7, rgb(110, 80, 45)); fillRect(img, 9, 5, 4, 3, rgb(150, 150, 155)); }
    return true;
  }
  if (n == "tnt_side" || n == "tnt_top" || n == "tnt_bottom") {
    const u32 c = rgb(200, 50, 40);
    img = noisy(n == "tnt_side" ? c : rgb(185, 60, 50), 6, seedOf(n));
    if (n == "tnt_side") {
      fillRect(img, 0, 5, 16, 6, rgb(235, 235, 230));
      for (int x = 3; x < 13; x += 3) fillRect(img, x, 7, 2, 2, rgb(30, 30, 30));
      for (int x = 0; x < 16; x += 4) for (int y = 0; y < 16; y++) if (y < 5 || y > 10) img.set(x, y, shade(c, -30));
    } else if (n == "tnt_top") disc(img, 7.5, 7.5, 1.6, rgb(40, 40, 40), rgb(40, 40, 40));
    return true;
  }
  if (n == "bookshelf") {
    img = plankOf("oak");
    Random r(12);
    for (int row : {1, 9})
      for (int x = 0; x < 16;) {
        const int w = 1 + r.nextInt(2);
        const u32 c = shade(rgb(80 + r.nextInt(120), 40 + r.nextInt(80), 30 + r.nextInt(90)), -10);
        fillRect(img, x, row, w, 6, c);
        x += w + (r.nextInt(4) == 0 ? 1 : 0);
      }
    return true;
  }
  if (n == "chest_cc0_top" || n == "chest_cc0_side" || n == "chest_cc0_front" || n == "ender_chest_cc0" || n == "trapped_chest_cc0_front") {
    const bool ender = n == "ender_chest_cc0";
    const u32 c = ender ? rgb(30, 45, 45) : rgb(160, 110, 45);
    img = noisy(c, 7, seedOf(n));
    border(img, shade(c, -45));
    if (n != "chest_cc0_top") fillRect(img, 0, 5, 16, 1, shade(c, -50));
    if (n == "chest_cc0_front" || n == "trapped_chest_cc0_front" || ender) fillRect(img, 7, 4, 2, 4, ender ? rgb(60, 180, 160) : rgb(200, 200, 200));
    if (n == "trapped_chest_cc0_front") fillRect(img, 7, 4, 2, 1, rgb(200, 40, 30));
    return true;
  }
  if (n == "skull_cc0") { img = noisy(rgb(200, 195, 180), 6, seedOf(n)); fillRect(img, 3, 6, 3, 3, rgb(30, 30, 30)); fillRect(img, 10, 6, 3, 3, rgb(30, 30, 30)); return true; }
  if (n == "slab_wood_old") { img = plankOf("oak"); return true; }
  (void)red;
  return false;
}

/// Asegura que existe la textura `blocks/<n>` y devuelve su referencia.
std::string T(Builder& b, const std::string& n) {
  if (!b.hasTex(n)) {
    Image img;
    std::vector<Image> anim;
    if (special(n, img, &anim)) {
      if (!anim.empty()) {
        b.tex(n, strip(anim));
        b.pack().putJson(kTex + "blocks/" + n + ".png.mcmeta", {{"animation", {{"frametime", 2}}}});
      } else {
        b.tex(n, std::move(img));
      }
    } else {
      b.ensureSimple(n);
    }
  }
  return "blocks/" + n;
}

// ---------------------------------------------------------------------------
// Formas
// ---------------------------------------------------------------------------

/// Texturas (arriba, abajo, lados) de los materiales de escaleras y losas.
struct Mat {
  std::string top, bottom, side;
};

Mat materialOf(Builder& b, const std::string& base) {
  for (const auto& [w, c] : woods())
    if (base == w || base == "wood_" + w) { const std::string t = T(b, "planks_" + woodTex(w)); return {t, t, t}; }
  if (base == "stone") return {T(b, "stone_slab_top"), T(b, "stone_slab_top"), T(b, "stone_slab_side")};
  if (base == "sandstone") return {T(b, "sandstone_top"), T(b, "sandstone_bottom"), T(b, "sandstone_normal")};
  if (base == "red_sandstone") return {T(b, "red_sandstone_top"), T(b, "red_sandstone_bottom"), T(b, "red_sandstone_normal")};
  if (base == "quartz") return {T(b, "quartz_block_top"), T(b, "quartz_block_bottom"), T(b, "quartz_block_side")};
  if (base == "wood_old") { const std::string t = T(b, "planks_oak"); return {t, t, t}; }
  static const std::map<std::string, std::string> single = {{"cobblestone", "cobblestone"}, {"brick", "brick"},
                                                            {"stone_brick", "stonebrick"}, {"nether_brick", "nether_brick"}};
  const std::string t = T(b, single.count(base) ? single.at(base) : base);
  return {t, t, t};
}

void stairs(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  std::string base = file.substr(0, file.size() - 7);
  if (base == "stone") base = "cobblestone";  // las "escaleras de piedra" son de roca
  const Mat m = materialOf(b, base);
  const std::map<std::string, std::string> tx = {{"top", m.top}, {"bottom", m.bottom}, {"side", m.side}, {"particle", m.side}};
  auto slab = [&] { return boxTBS({0, 0, 0}, {16, 8, 16}, "#top", "#bottom", "#side"); };
  b.model(file, mdl(tx, json::array({slab(), boxTBS({8, 8, 0}, {16, 16, 16}, "#top", "#bottom", "#side")})));
  b.model(file + "_outer", mdl(tx, json::array({slab(), boxTBS({8, 8, 8}, {16, 16, 16}, "#top", "#bottom", "#side")})));
  b.model(file + "_inner", mdl(tx, json::array({slab(), boxTBS({8, 8, 0}, {16, 16, 16}, "#top", "#bottom", "#side"),
                                                boxTBS({0, 8, 8}, {8, 16, 16}, "#top", "#bottom", "#side")})));
  for (const auto& k : keys) {
    const std::string facing = prop(k, "facing"), half = prop(k, "half"), shape = prop(k, "shape");
    const int rot = rotNorth(facing) - 90;  // el modelo mira al este
    const bool top = half == "top";
    std::string model = file;
    int y = rot;
    if (shape != "straight") {
      model = file + (shape.rfind("outer", 0) == 0 ? "_outer" : "_inner");
      const bool left = endsWith(shape, "left");
      y = top ? (left ? rot : rot + 90) : (left ? rot + 270 : rot);
    }
    bs["variants"][k] = var(model, top ? 180 : 0, y, true);
  }
}

void slab(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  const std::string base = file.substr(0, file.size() - 5);
  const Mat m = materialOf(b, base);
  const std::map<std::string, std::string> tx = {{"top", m.top}, {"bottom", m.bottom}, {"side", m.side}, {"particle", m.side}};
  b.model(file + "_bottom", mdl(tx, json::array({boxTBS({0, 0, 0}, {16, 8, 16}, "#top", "#bottom", "#side")})));
  b.model(file + "_upper", mdl(tx, json::array({boxTBS({0, 8, 0}, {16, 16, 16}, "#top", "#bottom", "#side")})));
  for (const auto& k : keys) bs["variants"][k] = var(k == "half=top" ? file + "_upper" : file + "_bottom");
}

void doubleSlab(Builder& b, const std::string& file, const std::string& base, const std::set<std::string>& keys, json& bs) {
  const Mat m = materialOf(b, base);
  b.model(file, bottomTop(m.bottom.substr(7), m.top.substr(7), m.side.substr(7)));
  b.model(file + "_all", cubeAll(m.top.substr(7)));
  for (const auto& k : keys) bs["variants"][k] = var(k == "all" ? file + "_all" : file);
}

/// Caja en dirección norte (d=0), este, sur u oeste a partir de una caja definida hacia el norte.
json rotBox(V3 a, V3 b2, int d, const std::string& t, int tint = -1) {
  auto rot = [&](V3 p) {
    for (int i = 0; i < d; i++) { const float x = p[0]; p[0] = 16 - p[2]; p[2] = x; }
    return p;
  };
  V3 ra = rot(a), rb = rot(b2);
  for (int i = 0; i < 3; i++) if (ra[i] > rb[i]) std::swap(ra[i], rb[i]);
  return box(ra, rb, t, tint);
}

void fence(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  std::string t;
  if (file == "nether_brick_fence") t = T(b, "nether_brick");
  else t = T(b, "planks_" + woodTex(file == "fence" ? "oak" : file.substr(0, file.size() - 6)));
  for (int bits = 0; bits < 16; bits++) {
    json els = json::array({box({6, 0, 6}, {10, 16, 10}, "#t")});
    for (int d = 0; d < 4; d++)
      if (bits & (1 << d)) {
        els.push_back(rotBox({7, 12, 0}, {9, 15, 6}, d, "#t"));
        els.push_back(rotBox({7, 6, 0}, {9, 9, 6}, d, "#t"));
      }
    b.model(file + "_" + std::to_string(bits), mdl({{"t", t}, {"particle", t}}, els));
  }
  for (const auto& k : keys) {
    const int bits = (prop(k, "north") == "true" ? 1 : 0) | (prop(k, "east") == "true" ? 2 : 0) |
                     (prop(k, "south") == "true" ? 4 : 0) | (prop(k, "west") == "true" ? 8 : 0);
    bs["variants"][k] = var(file + "_" + std::to_string(bits), 0, 0, true);
  }
}

void wall(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  const std::string t = T(b, file == "cobblestone_wall" ? "cobblestone" : "cobblestone_mossy");
  for (const auto& k : keys) {
    const int bits = (prop(k, "north") == "true" ? 1 : 0) | (prop(k, "east") == "true" ? 2 : 0) |
                     (prop(k, "south") == "true" ? 4 : 0) | (prop(k, "west") == "true" ? 8 : 0);
    const bool up = prop(k, "up") == "true";
    const std::string name = file + "_" + std::to_string(bits) + (up ? "_up" : "");
    json els = json::array();
    if (up) els.push_back(box({4, 0, 4}, {12, 16, 12}, "#t"));
    for (int d = 0; d < 4; d++)
      if (bits & (1 << d)) els.push_back(rotBox({5, 0, 0}, {11, 13, up ? 4.0f : 8.0f}, d, "#t"));
    if (els.empty()) els.push_back(box({4, 0, 4}, {12, 16, 12}, "#t"));
    b.model(name, mdl({{"t", t}, {"particle", t}}, els));
    bs["variants"][k] = var(name, 0, 0, true);
  }
}

void pane(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  std::string pt, et;
  if (file == "iron_bars") { pt = T(b, "iron_bars"); et = pt; }
  else if (file == "glass_pane") { pt = T(b, "glass"); et = T(b, "glass_pane_top"); }
  else {
    const std::string color = file.substr(0, file.size() - std::string("_stained_glass_pane").size());
    pt = "blocks/glass_" + color;
    et = T(b, "glass_pane_top_" + color);
  }
  for (int bits = 0; bits < 16; bits++) {
    const int eff = bits ? bits : 15;
    json els = json::array();
    // Poste central y brazos: caras grandes con el cristal, cantos con la textura del borde
    auto arm = [&](int d) {
      V3 a{7, 0, 0}, c{9, 16, 8};
      json e = rotBox(a, c, d, "#pane");
      for (const char* f : {"up", "down"}) e["faces"][f]["texture"] = "#edge";
      // el canto exterior del brazo
      static const char* outer[4] = {"north", "east", "south", "west"};
      if (e["faces"].contains(outer[d])) e["faces"][outer[d]]["texture"] = "#edge";
      return e;
    };
    for (int d = 0; d < 4; d++) if (eff & (1 << d)) els.push_back(arm(d));
    b.model(file + "_" + std::to_string(bits), mdl({{"pane", pt}, {"edge", et}, {"particle", pt}}, els));
  }
  for (const auto& k : keys) {
    const int bits = (prop(k, "north") == "true" ? 1 : 0) | (prop(k, "east") == "true" ? 2 : 0) |
                     (prop(k, "south") == "true" ? 4 : 0) | (prop(k, "west") == "true" ? 8 : 0);
    bs["variants"][k] = var(file + "_" + std::to_string(bits));
  }
}

void door(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  std::string kind = file.substr(0, file.size() - 5);
  if (kind == "wooden") kind = "wood";
  const std::string lower = T(b, "door_" + kind + "_lower"), upper = T(b, "door_" + kind + "_upper");
  for (const char* half : {"bottom", "top"}) {
    const std::string t = std::string(half) == "bottom" ? lower : upper;
    json el = boxF({0, 0, 0}, {3, 16, 16}, {"#door", "#door", "#door", "#door", "#door", "#door"});
    el["faces"]["up"]["uv"] = {13, 0, 16, 16};
    el["faces"]["down"]["uv"] = {13, 0, 16, 16};
    el["faces"]["north"]["uv"] = {3, 0, 0, 16};
    el["faces"]["south"]["uv"] = {0, 0, 3, 16};
    el["faces"]["east"]["uv"] = {16, 0, 0, 16};  // espejo por fuera
    b.model(file + "_" + half, mdl({{"door", t}, {"particle", t}}, json::array({el})));
    json rh = el;
    rh["faces"]["west"]["uv"] = {16, 0, 0, 16};
    rh["faces"]["east"]["uv"] = {0, 0, 16, 16};
    b.model(file + "_" + half + "_rh", mdl({{"door", t}, {"particle", t}}, json::array({rh})));
  }
  for (const auto& k : keys) {
    const std::string facing = prop(k, "facing");
    const bool open = prop(k, "open") == "true", right = prop(k, "hinge") == "right";
    const std::string half = prop(k, "half") == "upper" ? "top" : "bottom";
    int y = rotNorth(facing) - 90;  // el modelo mira al este
    std::string model = file + "_" + half + (right ? "_rh" : "");
    if (open) {
      y += right ? 270 : 90;
      model = file + "_" + half + (right ? "" : "_rh");
    }
    bs["variants"][k] = var(model, 0, y);
  }
}

void fenceGate(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  const std::string w = file == "fence_gate" ? "oak" : file.substr(0, file.size() - 11);
  const std::string t = T(b, "planks_" + woodTex(w));
  for (int wallDrop : {0, 3})
    for (bool open : {false, true}) {
      const float o = static_cast<float>(wallDrop);
      json els = json::array({box({0, 5 - o, 7}, {2, 16 - o, 9}, "#t"), box({14, 5 - o, 7}, {16, 16 - o, 9}, "#t")});
      if (!open) {
        for (auto [x0, x1] : {std::pair{6.f, 8.f}, std::pair{8.f, 10.f}}) els.push_back(box({x0, 6 - o, 7}, {x1, 15 - o, 9}, "#t"));
        for (auto [x0, x1] : {std::pair{2.f, 6.f}, std::pair{10.f, 14.f}}) {
          els.push_back(box({x0, 6 - o, 7}, {x1, 9 - o, 9}, "#t"));
          els.push_back(box({x0, 12 - o, 7}, {x1, 15 - o, 9}, "#t"));
        }
      } else {
        for (float x : {0.f, 14.f}) {
          els.push_back(box({x, 6 - o, 13}, {x + 2, 15 - o, 15}, "#t"));
          els.push_back(box({x, 6 - o, 9}, {x + 2, 9 - o, 13}, "#t"));
          els.push_back(box({x, 12 - o, 9}, {x + 2, 15 - o, 13}, "#t"));
        }
      }
      b.model(file + (open ? "_open" : "_closed") + (wallDrop ? "_wall" : ""), mdl({{"t", t}, {"particle", t}}, els));
    }
  for (const auto& k : keys) {
    const bool open = prop(k, "open") == "true", inWall = prop(k, "in_wall") == "true";
    // El modelo se hace mirando al sur
    const int y = rotNorth(prop(k, "facing")) + 180;
    bs["variants"][k] = var(file + (open ? "_open" : "_closed") + (inWall ? "_wall" : ""), 0, y, true);
  }
}

/// Cultivos: 4 planos formando un "#".
json cropModel(const std::string& t) {
  json els = json::array();
  for (float x : {4.f, 12.f}) els.push_back(plane({x, -1, 0}, {x, 15, 16}, "west", "east", "#crop"));
  for (float z : {4.f, 12.f}) els.push_back(plane({0, -1, z}, {16, 15, z}, "north", "south", "#crop"));
  return mdl({{"crop", t}, {"particle", t}}, els, false);
}

void torchModels(Builder& b, const std::string& file, const std::string& t) {
  json faces = {{"up", face("#torch", nullptr, -1, {7, 6, 9, 8})}, {"down", face("#torch", nullptr, -1, {7, 13, 9, 15})}};
  for (const char* f : {"north", "south", "west", "east"}) faces[f] = face("#torch", nullptr, -1, {7, 6, 9, 16});
  json stick = {{"from", {7, 0, 7}}, {"to", {9, 10, 9}}, {"shade", false}, {"faces", faces}};
  b.model(file + "_up", mdl({{"torch", t}, {"particle", t}}, json::array({stick}), false));
  json wallStick = {{"from", {-1, 3.5, 7}}, {"to", {1, 13.5, 9}}, {"shade", false}, {"faces", faces},
                    {"rotation", {{"origin", {0, 3.5, 8}}, {"axis", "z"}, {"angle", -22.5}, {"rescale", false}}}};
  b.model(file + "_wall", mdl({{"torch", t}, {"particle", t}}, json::array({wallStick}), false));
}

void redstoneWire(Builder& b, const std::set<std::string>& keys, json& bs) {
  const std::string cross = T(b, "redstone_dust_cross"), lineT = T(b, "redstone_dust_line");
  for (const auto& k : keys) {
    const std::string n = prop(k, "north"), e = prop(k, "east"), s = prop(k, "south"), w = prop(k, "west");
    const bool cn = n != "none", ce = e != "none", cs = s != "none", cw = w != "none";
    const int count = cn + ce + cs + cw;
    json els = json::array();
    auto flat = [&](V3 a, V3 c, const std::string& t, int rotation) {
      json f = {{"texture", t}, {"tintindex", 0}, {"uv", {a[0], a[2], c[0], c[2]}}};
      if (rotation) f["rotation"] = rotation;
      return json{{"from", {a[0], 0.25f, a[2]}}, {"to", {c[0], 0.25f, c[2]}}, {"shade", false}, {"faces", {{"up", f}}}};
    };
    const bool nsOnly = (cn || cs) && !ce && !cw, ewOnly = (ce || cw) && !cn && !cs;
    if (count == 0) els.push_back(flat({0, 0, 0}, {16, 0, 16}, "#cross", 0));
    else if (nsOnly) els.push_back(flat({0, 0, 0}, {16, 0, 16}, "#line", 0));
    else if (ewOnly) els.push_back(flat({0, 0, 0}, {16, 0, 16}, "#line", 90));
    else els.push_back(flat({cw ? 0.f : 5.f, 0, cn ? 0.f : 5.f}, {ce ? 16.f : 11.f, 0, cs ? 16.f : 11.f}, "#cross", 0));
    // Subidas por la pared
    auto upPlane = [&](const char* side) {
      json f = {{"texture", "#line"}, {"tintindex", 0}};
      if (std::string(side) == "north") return json{{"from", {0, 0, 0.25}}, {"to", {16, 16, 0.25}}, {"shade", false}, {"faces", {{"south", f}}}};
      if (std::string(side) == "south") return json{{"from", {0, 0, 15.75}}, {"to", {16, 16, 15.75}}, {"shade", false}, {"faces", {{"north", f}}}};
      if (std::string(side) == "west") return json{{"from", {0.25, 0, 0}}, {"to", {0.25, 16, 16}}, {"shade", false}, {"faces", {{"east", f}}}};
      return json{{"from", {15.75, 0, 0}}, {"to", {15.75, 16, 16}}, {"shade", false}, {"faces", {{"west", f}}}};
    };
    if (n == "up") els.push_back(upPlane("north"));
    if (s == "up") els.push_back(upPlane("south"));
    if (w == "up") els.push_back(upPlane("west"));
    if (e == "up") els.push_back(upPlane("east"));
    std::string name = "redstone_" + std::string(1, n[0]) + e[0] + s[0] + w[0];
    b.model(name, mdl({{"cross", cross}, {"line", lineT}, {"particle", cross}}, els, false));
    bs["variants"][k] = var(name);
  }
}

}  // namespace

bool addShapedBlock(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs) {
  auto all = [&](const std::string& model) { for (const auto& k : keys) bs["variants"][k] = var(model); };
  auto facingKeys = [&](const std::string& model, int base, bool uvlock = false) {
    for (const auto& k : keys) bs["variants"][k] = var(model, 0, rotNorth(prop(k, "facing")) + base, uvlock);
  };

  if (endsWith(file, "_stairs")) { stairs(b, file, keys, bs); return true; }
  if (file.rfind("double_", 0) == 0 && endsWith(file, "_slab")) {
    doubleSlab(b, file, file.substr(7, file.size() - 12), keys, bs);
    return true;
  }
  if (endsWith(file, "_double_slab")) {
    doubleSlab(b, file, file.substr(0, file.size() - 12), keys, bs);
    return true;
  }
  if (endsWith(file, "_slab")) { slab(b, file, keys, bs); return true; }
  if (file == "fence" || endsWith(file, "_fence")) { fence(b, file, keys, bs); return true; }
  if (endsWith(file, "_wall")) { wall(b, file, keys, bs); return true; }
  if (file == "glass_pane" || file == "iron_bars" || endsWith(file, "_stained_glass_pane")) { pane(b, file, keys, bs); return true; }
  if (endsWith(file, "_door")) { door(b, file, keys, bs); return true; }
  if (file == "fence_gate" || endsWith(file, "_fence_gate")) { fenceGate(b, file, keys, bs); return true; }
  if (file == "redstone_wire") { redstoneWire(b, keys, bs); return true; }

  if (file == "furnace" || file == "lit_furnace") {
    const std::string front = T(b, file == "furnace" ? "furnace_front_off" : "furnace_front_on");
    const std::string side = T(b, "furnace_side"), top = T(b, "furnace_top");
    b.model(file, mdl({{"front", front}, {"side", side}, {"top", top}, {"particle", front}},
                      json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#top", "#top", "#front", "#side", "#side", "#side"})})));
    facingKeys(file, 0);
    return true;
  }
  if (file == "dispenser" || file == "dropper") {
    const std::string fh = T(b, file + "_front_horizontal"), fv = T(b, file + "_front_vertical");
    const std::string side = T(b, "furnace_side"), top = T(b, "furnace_top");
    b.model(file, mdl({{"front", fh}, {"side", side}, {"top", top}, {"particle", side}},
                      json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#top", "#top", "#front", "#side", "#side", "#side"})})));
    b.model(file + "_vertical", mdl({{"front", fv}, {"side", top}, {"particle", top}},
                                    json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#side", "#front", "#side", "#side", "#side", "#side"})})));
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing");
      if (f == "up") bs["variants"][k] = var(file + "_vertical");
      else if (f == "down") bs["variants"][k] = var(file + "_vertical", 180);
      else bs["variants"][k] = var(file, 0, rotNorth(f));
    }
    return true;
  }
  if (file == "pumpkin" || file == "lit_pumpkin") {
    const std::string front = T(b, file == "pumpkin" ? "pumpkin_face_off" : "pumpkin_face_on");
    const std::string side = T(b, "pumpkin_side"), top = T(b, "pumpkin_top");
    b.model(file, mdl({{"front", front}, {"side", side}, {"top", top}, {"particle", side}},
                      json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#top", "#top", "#front", "#side", "#side", "#side"})})));
    // 1.8: facing=south es la cara mirando al sur
    facingKeys(file, 0);
    return true;
  }
  if (file == "crafting_table") {
    b.model(file, mdl({{"top", T(b, "crafting_table_top")}, {"front", T(b, "crafting_table_front")}, {"side", T(b, "crafting_table_side")},
                       {"bottom", T(b, "planks_oak")}, {"particle", T(b, "crafting_table_front")}},
                      json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#bottom", "#top", "#front", "#side", "#side", "#front"})})));
    all(file);
    return true;
  }
  if (file == "tnt") { b.model(file, bottomTop(T(b, "tnt_bottom").substr(7), T(b, "tnt_top").substr(7), T(b, "tnt_side").substr(7))); all(file); return true; }
  if (file == "bookshelf") { b.model(file, column(T(b, "planks_oak").substr(7), T(b, "bookshelf").substr(7))); all(file); return true; }
  if (file == "melon_block") { b.model(file, column(T(b, "melon_top").substr(7), T(b, "melon_side").substr(7))); all(file); return true; }
  if (file == "jukebox") { b.model(file, column(T(b, "jukebox_top").substr(7), T(b, "jukebox_side").substr(7))); all(file); return true; }
  if (file == "noteblock" || file == "mob_spawner" || file == "redstone_block" || file == "command_block" || file == "redstone_lamp" ||
      file == "lit_redstone_lamp") {
    const std::string t = file == "redstone_lamp" ? "redstone_lamp_off" : (file == "lit_redstone_lamp" ? "redstone_lamp_on" : file);
    T(b, t);
    b.model(file, cubeAll(t));
    all(file);
    return true;
  }
  if (endsWith(file, "_monster_egg")) {
    static const std::map<std::string, std::string> t = {
        {"stone_monster_egg", "stone"}, {"cobblestone_monster_egg", "cobblestone"}, {"stone_brick_monster_egg", "stonebrick"},
        {"mossy_brick_monster_egg", "stonebrick_mossy"}, {"cracked_brick_monster_egg", "stonebrick_cracked"},
        {"chiseled_brick_monster_egg", "stonebrick_carved"}};
    T(b, t.at(file));
    b.model(file, cubeAll(t.at(file)));
    all(file);
    return true;
  }
  if (file == "prismarine" || file == "prismarine_bricks" || file == "dark_prismarine") {
    const std::string t = file == "prismarine" ? "prismarine_rough" : (file == "dark_prismarine" ? "prismarine_dark" : "prismarine_bricks");
    T(b, t);
    b.model(file, cubeAll(t));
    all(file);
    return true;
  }
  if (file == "red_sandstone" || file == "chiseled_red_sandstone" || file == "smooth_red_sandstone") {
    const std::string side = file == "red_sandstone" ? "red_sandstone_normal" : (file == "chiseled_red_sandstone" ? "red_sandstone_carved" : "red_sandstone_smooth");
    b.model(file, bottomTop(T(b, "red_sandstone_bottom").substr(7), T(b, "red_sandstone_top").substr(7), T(b, side).substr(7)));
    all(file);
    return true;
  }
  if (file == "quartz_column") {
    b.model(file, column(T(b, "quartz_block_lines_top").substr(7), T(b, "quartz_block_lines").substr(7)));
    for (const auto& k : keys) bs["variants"][k] = k == "axis=y" ? var(file) : (k == "axis=z" ? var(file, 90) : var(file, 90, 90));
    return true;
  }
  if (file == "hay_block") {
    b.model(file, column(T(b, "hay_block_top").substr(7), T(b, "hay_block_side").substr(7)));
    for (const auto& k : keys) bs["variants"][k] = k == "axis=y" ? var(file) : (k == "axis=z" ? var(file, 90) : var(file, 90, 90));
    return true;
  }
  if (endsWith(file, "_carpet")) {
    const std::string color = file.substr(0, file.size() - 7);
    const std::string t = "blocks/wool_colored_" + color;
    b.model(file, mdl({{"wool", t}, {"particle", t}}, json::array({boxF({0, 0, 0}, {16, 1, 16}, {"#wool", "#wool", "#wool", "#wool", "#wool", "#wool"})})));
    all(file);
    return true;
  }
  if (file == "torch" || file == "redstone_torch" || file == "unlit_redstone_torch") {
    const std::string t = T(b, file == "torch" ? "torch_on" : (file == "redstone_torch" ? "redstone_torch_on" : "redstone_torch_off"));
    torchModels(b, file, t);
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing");
      bs["variants"][k] = f == "up" ? var(file + "_up") : var(file + "_wall", 0, rotNorth(f) - 90);
    }
    return true;
  }
  if (file == "fire") {
    const std::string t0 = T(b, "fire_layer_0"), t1 = T(b, "fire_layer_1");
    json els = json::array();
    for (float z : {3.f, 13.f}) els.push_back(plane({0, 0, z}, {16, 16, z}, "north", "south", "#fire"));
    for (float x : {3.f, 13.f}) els.push_back(plane({x, 0, 0}, {x, 16, 16}, "west", "east", "#fire"));
    b.model(file, mdl({{"fire", t0}, {"particle", t0}}, els, false));
    (void)t1;
    all(file);
    return true;
  }
  if (file == "portal") {
    const std::string t = T(b, "portal");
    b.model(file, mdl({{"p", t}, {"particle", t}}, json::array({boxF({0, 0, 6}, {16, 16, 10}, {"", "", "#p", "#p", "", ""})}), false));
    for (const auto& k : keys) bs["variants"][k] = k == "axis=z" ? var(file, 0, 90) : var(file);
    return true;
  }
  if (file == "bed") {
    for (const char* part : {"foot", "head"}) {
      const std::string p = part == std::string("foot") ? "feet" : "head";
      const std::string top = T(b, "bed_" + p + "_top"), side = T(b, "bed_" + p + "_side"), end = T(b, "bed_" + p + "_end");
      // Hecha con la cabecera al norte: el extremo abierto de cada mitad mira a la otra
      const bool head = p == "head";
      json el = boxF({0, 0, 0}, {16, 9, 16}, {"#bottom", "#top", head ? "#end" : "", head ? "" : "#end", "#side", "#side"});
      el["faces"]["east"]["uv"] = {16, 0, 0, 16};
      b.model(std::string("bed_") + part, mdl({{"top", top}, {"side", side}, {"end", end}, {"bottom", T(b, "planks_oak")}, {"particle", side}},
                                              json::array({el})));
    }
    for (const auto& k : keys) bs["variants"][k] = var("bed_" + prop(k, "part"), 0, rotNorth(prop(k, "facing")));
    return true;
  }
  if (file == "rail" || file == "golden_rail" || file == "detector_rail" || file == "activator_rail") {
    const std::string base = file == "rail" ? "rail_normal" : (file == "golden_rail" ? "rail_golden" : (file == "detector_rail" ? "rail_detector" : "rail_activator"));
    for (bool powered : {false, true}) {
      if (file == "rail" && powered) continue;
      const std::string t = T(b, base + (powered ? "_powered" : ""));
      const std::string sfx = powered ? "_powered" : "";
      json flat = {{"from", {0, 1, 0}}, {"to", {16, 1, 16}}, {"faces", {{"up", {{"texture", "#rail"}}}, {"down", {{"texture", "#rail"}}}}}};
      b.model(file + sfx + "_flat", mdl({{"rail", t}, {"particle", t}}, json::array({flat}), false));
      json raised = withRot({{"from", {0, 9, 0}}, {"to", {16, 9, 16}}, {"faces", {{"up", {{"texture", "#rail"}}}, {"down", {{"texture", "#rail"}}}}}},
                            {8, 9, 8}, "x", 45, true);
      b.model(file + sfx + "_raised", mdl({{"rail", t}, {"particle", t}}, json::array({raised}), false));
    }
    if (file == "rail") {
      const std::string t = T(b, "rail_normal_turned");
      json flat = {{"from", {0, 1, 0}}, {"to", {16, 1, 16}}, {"faces", {{"up", {{"texture", "#rail"}}}, {"down", {{"texture", "#rail"}}}}}};
      b.model("rail_curved", mdl({{"rail", t}, {"particle", t}}, json::array({flat}), false));
    }
    for (const auto& k : keys) {
      const std::string shape = prop(k, "shape");
      const std::string sfx = prop(k, "powered") == "true" ? "_powered" : "";
      if (shape == "north_south") bs["variants"][k] = var(file + sfx + "_flat");
      else if (shape == "east_west") bs["variants"][k] = var(file + sfx + "_flat", 0, 90);
      else if (shape.rfind("ascending_", 0) == 0) bs["variants"][k] = var(file + sfx + "_raised", 0, rotNorth(shape.substr(10)));
      else {
        static const std::map<std::string, int> curve = {{"south_east", 0}, {"south_west", 90}, {"north_west", 180}, {"north_east", 270}};
        bs["variants"][k] = var("rail_curved", 0, curve.at(shape));
      }
    }
    return true;
  }
  if (file == "piston" || file == "sticky_piston") {
    const std::string top = T(b, file == "piston" ? "piston_top_normal" : "piston_top_sticky");
    const std::string side = T(b, "piston_side"), bottom = T(b, "piston_bottom"), inner = T(b, "piston_inner");
    b.model(file + "_normal", mdl({{"top", top}, {"side", side}, {"bottom", bottom}, {"particle", side}},
                                  json::array({boxF({0, 0, 0}, {16, 16, 16}, {"#bottom", "#top", "#side", "#side", "#side", "#side"})})));
    json base = boxF({0, 0, 0}, {16, 12, 16}, {"#bottom", "#inner", "#side", "#side", "#side", "#side"});
    for (const char* f : {"north", "south", "west", "east"}) base["faces"][f]["uv"] = {0, 4, 16, 16};
    b.model(file + "_extended", mdl({{"inner", inner}, {"side", side}, {"bottom", bottom}, {"particle", side}}, json::array({base})));
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing");
      const std::string m = file + (prop(k, "extended") == "true" ? "_extended" : "_normal");
      if (f == "up") bs["variants"][k] = var(m);
      else if (f == "down") bs["variants"][k] = var(m, 180);
      else bs["variants"][k] = var(m, 90, rotNorth(f));
    }
    return true;
  }
  if (file == "piston_head") {
    const std::string side = T(b, "piston_side");
    for (const char* type : {"normal", "sticky"}) {
      const std::string top = T(b, std::string("piston_top_") + type);
      json plate = boxF({0, 12, 0}, {16, 16, 16}, {"#top", "#top", "#side", "#side", "#side", "#side"});
      for (const char* f : {"north", "south", "west", "east"}) plate["faces"][f]["uv"] = {0, 0, 16, 4};
      json arm = boxF({6, -4, 6}, {10, 12, 10}, {"", "", "#side", "#side", "#side", "#side"});
      for (const char* f : {"north", "south", "west", "east"}) arm["faces"][f]["uv"] = {6, 0, 10, 16};
      b.model(std::string("piston_head_") + type, mdl({{"top", top}, {"side", side}, {"particle", side}}, json::array({plate, arm})));
    }
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing"), m = "piston_head_" + prop(k, "type");
      if (f == "up") bs["variants"][k] = var(m);
      else if (f == "down") bs["variants"][k] = var(m, 180);
      else bs["variants"][k] = var(m, 90, rotNorth(f));
    }
    return true;
  }
  if (file == "wheat" || file == "carrots" || file == "potatoes" || file == "nether_wart") {
    for (const auto& k : keys) {
      const int age = std::stoi(prop(k, "age"));
      int stage = age;
      if (file == "carrots" || file == "potatoes") stage = age < 2 ? 0 : (age < 4 ? 1 : (age < 7 ? 2 : 3));
      if (file == "nether_wart") stage = age == 0 ? 0 : (age < 3 ? 1 : 2);
      const std::string t = file + "_stage_" + std::to_string(stage);
      T(b, t);
      b.model(t, cropModel("blocks/" + t));
      bs["variants"][k] = var(t);
    }
    return true;
  }
  if (file == "farmland") {
    const std::string dirt = T(b, "dirt");
    for (bool wet : {false, true}) {
      const std::string top = T(b, wet ? "farmland_wet" : "farmland_dry");
      b.model(wet ? "farmland_moist" : "farmland_dry",
              mdl({{"top", top}, {"dirt", dirt}, {"particle", dirt}}, json::array({boxTBS({0, 0, 0}, {16, 15, 16}, "#top", "#dirt", "#dirt")})));
    }
    for (const auto& k : keys) bs["variants"][k] = var(k == "moisture=7" ? "farmland_moist" : "farmland_dry");
    return true;
  }
  if (file == "ladder") {
    const std::string t = T(b, "ladder");
    b.model(file, mdl({{"t", t}, {"particle", t}}, json::array({plane({0, 0, 15.2f}, {16, 16, 15.2f}, "north", "south", "#t")}), false));
    facingKeys(file, 0);
    return true;
  }
  if (file == "vine") {
    const std::string t = T(b, "vine");
    for (const auto& k : keys) {
      json els = json::array();
      if (prop(k, "north") == "true") els.push_back(plane({0, 0, 0.8f}, {16, 16, 0.8f}, "north", "south", "#t", 0));
      if (prop(k, "south") == "true") els.push_back(plane({0, 0, 15.2f}, {16, 16, 15.2f}, "north", "south", "#t", 0));
      if (prop(k, "west") == "true") els.push_back(plane({0.8f, 0, 0}, {0.8f, 16, 16}, "west", "east", "#t", 0));
      if (prop(k, "east") == "true") els.push_back(plane({15.2f, 0, 0}, {15.2f, 16, 16}, "west", "east", "#t", 0));
      if (prop(k, "up") == "true") els.push_back(plane({0, 15.2f, 0}, {16, 15.2f, 16}, "up", "down", "#t", 0));
      if (els.empty()) els.push_back(plane({0, 15.2f, 0}, {16, 15.2f, 16}, "up", "down", "#t", 0));
      std::string name = "vine";
      for (const char* p : {"north", "east", "south", "west", "up"}) name += prop(k, p) == "true" ? "1" : "0";
      b.model(name, mdl({{"t", t}, {"particle", t}}, els, false));
      bs["variants"][k] = var(name);
    }
    return true;
  }
  if (file == "lever") {
    const std::string lev = T(b, "lever"), base = T(b, "cobblestone");
    for (bool on : {false, true}) {
      json handle = withRot(boxF({7, 1, 7}, {9, 11, 9}, {"", "#lever", "#lever", "#lever", "#lever", "#lever"}), {8, 1, 8}, "x", on ? -45.f : 45.f);
      for (const char* f : {"north", "south", "west", "east"}) handle["faces"][f]["uv"] = {7, 6, 9, 16};
      handle["faces"]["up"]["uv"] = {7, 6, 9, 8};
      b.model(on ? "lever_on" : "lever_off", mdl({{"lever", lev}, {"base", base}, {"particle", base}},
                                                 json::array({box({5, 0, 4}, {11, 3, 12}, "#base"), handle})));
    }
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing"), m = prop(k, "powered") == "true" ? "lever_on" : "lever_off";
      if (f == "up_z") bs["variants"][k] = var(m);
      else if (f == "up_x") bs["variants"][k] = var(m, 0, 90);
      else if (f == "down_z") bs["variants"][k] = var(m, 180, 180);
      else if (f == "down_x") bs["variants"][k] = var(m, 180, 90);
      else bs["variants"][k] = var(m, 90, rotNorth(f));
    }
    return true;
  }
  if (endsWith(file, "_pressure_plate")) {
    static const std::map<std::string, std::string> t = {{"stone_pressure_plate", "stone"}, {"wooden_pressure_plate", "planks_oak"},
                                                         {"light_weighted_pressure_plate", "gold_block"},
                                                         {"heavy_weighted_pressure_plate", "iron_block"}};
    const std::string tx = T(b, t.at(file));
    b.model(file + "_up", mdl({{"t", tx}, {"particle", tx}}, json::array({box({1, 0, 1}, {15, 1, 15}, "#t")})));
    b.model(file + "_down", mdl({{"t", tx}, {"particle", tx}}, json::array({box({1, 0, 1}, {15, 0.5f, 15}, "#t")})));
    for (const auto& k : keys) {
      const bool down = k == "powered=true" || (k.rfind("power=", 0) == 0 && k != "power=0");
      bs["variants"][k] = var(file + (down ? "_down" : "_up"));
    }
    return true;
  }
  if (endsWith(file, "_button")) {
    const std::string tx = T(b, file == "stone_button" ? "stone" : "planks_oak");
    b.model(file, mdl({{"t", tx}, {"particle", tx}}, json::array({box({5, 0, 6}, {11, 2, 10}, "#t")})));
    b.model(file + "_pressed", mdl({{"t", tx}, {"particle", tx}}, json::array({box({5, 0, 6}, {11, 1, 10}, "#t")})));
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing"), m = file + (prop(k, "powered") == "true" ? "_pressed" : "");
      if (f == "up") bs["variants"][k] = var(m);
      else if (f == "down") bs["variants"][k] = var(m, 180);
      else bs["variants"][k] = var(m, 90, rotNorth(f));
    }
    return true;
  }
  if (file == "cake") {
    const std::string top = T(b, "cake_top"), side = T(b, "cake_side"), bottom = T(b, "cake_bottom"), inner = T(b, "cake_inner");
    for (const auto& k : keys) {
      const int bites = std::stoi(prop(k, "bites"));
      const float x0 = 1.f + bites * 2;
      json el = boxF({x0, 0, 1}, {15, 8, 15}, {"#bottom", "#top", "#side", "#side", bites ? "#inner" : "#side", "#side"});
      for (const char* f : {"north", "south", "west", "east"}) el["faces"][f]["uv"] = {1, 8, 15, 16};
      b.model("cake_" + std::to_string(bites), mdl({{"top", top}, {"side", side}, {"bottom", bottom}, {"inner", inner}, {"particle", side}},
                                                   json::array({el})));
      bs["variants"][k] = var("cake_" + std::to_string(bites));
    }
    return true;
  }
  if (endsWith(file, "_repeater") || endsWith(file, "_comparator")) {
    const bool comparator = endsWith(file, "_comparator");
    const bool powered = file.rfind("powered", 0) == 0;
    const std::string top = T(b, std::string(comparator ? "comparator_" : "repeater_") + (powered ? "on" : "off"));
    const std::string slabT = T(b, "stone_slab_top");
    const std::string torchT = T(b, powered ? "redstone_torch_on" : "redstone_torch_off");
    auto torch = [&](float x, float z) {
      json f = {{"texture", "#torch"}, {"uv", {7, 6, 9, 11}}};
      return json{{"from", {x - 1, 2, z - 1}}, {"to", {x + 1, 7, z + 1}}, {"shade", false},
                  {"faces", {{"north", f}, {"south", f}, {"west", f}, {"east", f}, {"up", {{"texture", "#torch"}, {"uv", {7, 6, 9, 8}}}}}}};
    };
    for (const auto& k : keys) {
      json els = json::array({boxF({0, 0, 0}, {16, 2, 16}, {"#slab", "#top", "#slab", "#slab", "#slab", "#slab"})});
      std::string name = file;
      if (comparator) {
        els.push_back(torch(4, 3));
        els.push_back(torch(12, 3));
        name += prop(k, "mode") == "subtract" ? "_sub" : "";
      } else {
        const int delay = std::stoi(prop(k, "delay"));
        els.push_back(torch(8, 3));
        els.push_back(torch(8, 5 + delay * 2));
        name += "_" + std::to_string(delay);
      }
      b.model(name, mdl({{"top", top}, {"slab", slabT}, {"torch", torchT}, {"particle", top}}, els));
      bs["variants"][k] = var(name, 0, rotNorth(prop(k, "facing")));
    }
    return true;
  }
  if (file == "trapdoor" || file == "iron_trapdoor") {
    const std::string t = T(b, file);
    b.model(file + "_bottom", mdl({{"t", t}, {"particle", t}}, json::array({box({0, 0, 0}, {16, 3, 16}, "#t")})));
    b.model(file + "_top", mdl({{"t", t}, {"particle", t}}, json::array({box({0, 13, 0}, {16, 16, 16}, "#t")})));
    b.model(file + "_open", mdl({{"t", t}, {"particle", t}}, json::array({box({0, 0, 13}, {16, 16, 16}, "#t")})));
    for (const auto& k : keys) {
      if (prop(k, "open") == "true") bs["variants"][k] = var(file + "_open", 0, rotNorth(prop(k, "facing")));
      else bs["variants"][k] = var(file + (prop(k, "half") == "top" ? "_top" : "_bottom"));
    }
    return true;
  }
  if (endsWith(file, "_mushroom_block")) {
    const std::string skin = T(b, file == "red_mushroom_block" ? "mushroom_block_skin_red" : "mushroom_block_skin_brown");
    const std::string inside = T(b, "mushroom_block_inside"), stem = T(b, "mushroom_block_skin_stem");
    for (const auto& k : keys) {
      const std::string v = prop(k, "variant");
      // Caras con sombrero: abajo, arriba, norte, sur, oeste, este
      std::array<std::string, 6> f = {"#in", "#skin", "#in", "#in", "#in", "#in"};
      if (v == "all_inside") f[1] = "#in";
      if (v == "all_outside") f = {"#skin", "#skin", "#skin", "#skin", "#skin", "#skin"};
      if (v == "all_stem") f = {"#stem", "#stem", "#stem", "#stem", "#stem", "#stem"};
      if (v == "stem") f = {"#in", "#in", "#stem", "#stem", "#stem", "#stem"};
      if (v.find("north") != std::string::npos) f[2] = "#skin";
      if (v.find("south") != std::string::npos) f[3] = "#skin";
      if (v.find("west") != std::string::npos) f[4] = "#skin";
      if (v.find("east") != std::string::npos) f[5] = "#skin";
      const std::string name = file + "_" + v;
      b.model(name, mdl({{"skin", skin}, {"in", inside}, {"stem", stem}, {"particle", skin}}, json::array({boxF({0, 0, 0}, {16, 16, 16}, f)})));
      bs["variants"][k] = var(name);
    }
    return true;
  }
  if (file == "pumpkin_stem" || file == "melon_stem") {
    const std::string dis = T(b, file + "_disconnected"), con = T(b, file + "_connected");
    for (const auto& k : keys) {
      const std::string ageText = prop(k, "age");  // (el tallo doblado hacia su fruto no lleva edad)
      const int age = ageText.empty() ? 7 : std::stoi(ageText);
      const std::string f = prop(k, "facing");
      std::string name;
      if (f == "up") {
        name = file + "_growth" + std::to_string(age);
        const float h = (age + 1) * 2.f;
        json els = json::array();
        json e1 = {{"from", {0, -1, 8}}, {"to", {16, h - 1, 8}}, {"shade", false},
                   {"faces", {{"north", {{"texture", "#stem"}, {"tintindex", 0}, {"uv", {0, 16 - h, 16, 16}}}},
                              {"south", {{"texture", "#stem"}, {"tintindex", 0}, {"uv", {0, 16 - h, 16, 16}}}}}}};
        json e2 = {{"from", {8, -1, 0}}, {"to", {8, h - 1, 16}}, {"shade", false},
                   {"faces", {{"west", {{"texture", "#stem"}, {"tintindex", 0}, {"uv", {0, 16 - h, 16, 16}}}},
                              {"east", {{"texture", "#stem"}, {"tintindex", 0}, {"uv", {0, 16 - h, 16, 16}}}}}}};
        els.push_back(withRot(e1, {8, 8, 8}, "y", 45, true));
        els.push_back(withRot(e2, {8, 8, 8}, "y", 45, true));
        b.model(name, mdl({{"stem", dis}, {"particle", dis}}, els, false));
        bs["variants"][k] = var(name);
      } else {
        name = file + "_bent";
        json els = json::array({plane({0, -1, 8}, {16, 15, 8}, "north", "south", "#stem", 0)});
        b.model(name, mdl({{"stem", con}, {"particle", con}}, els, false));
        // El modelo apunta al este (el tallo sale en x = 1 y se curva hacia x = 16)
        bs["variants"][k] = var(name, 0, rotNorth(f) - 90);
      }
    }
    return true;
  }
  if (file == "enchanting_table") {
    b.model(file, mdl({{"top", T(b, "enchanting_table_top")}, {"side", T(b, "enchanting_table_side")}, {"bottom", T(b, "enchanting_table_bottom")},
                       {"particle", T(b, "enchanting_table_bottom")}},
                      json::array({boxTBS({0, 0, 0}, {16, 12, 16}, "#top", "#bottom", "#side")})));
    all(file);
    return true;
  }
  if (file == "end_portal_frame") {
    const std::string top = T(b, "endframe_top"), side = T(b, "endframe_side"), eye = T(b, "endframe_eye"), bottom = T(b, "end_stone");
    const std::map<std::string, std::string> tx = {{"top", top}, {"side", side}, {"eye", eye}, {"bottom", bottom}, {"particle", side}};
    b.model("end_portal_frame_empty", mdl(tx, json::array({boxTBS({0, 0, 0}, {16, 13, 16}, "#top", "#bottom", "#side")})));
    b.model("end_portal_frame_filled", mdl(tx, json::array({boxTBS({0, 0, 0}, {16, 13, 16}, "#top", "#bottom", "#side"),
                                                             box({4, 13, 4}, {12, 16, 12}, "#eye")})));
    for (const auto& k : keys)
      bs["variants"][k] = var(prop(k, "eye") == "true" ? "end_portal_frame_filled" : "end_portal_frame_empty", 0, rotNorth(prop(k, "facing")));
    return true;
  }
  if (file == "dragon_egg") {
    const std::string t = T(b, "dragon_egg");
    json els = json::array();
    const float r[8] = {3, 5, 6, 7, 7, 6, 5, 3};
    for (int i = 0; i < 8; i++) els.push_back(box({8 - r[i], i * 2.f, 8 - r[i]}, {8 + r[i], i * 2.f + 2, 8 + r[i]}, "#t"));
    b.model(file, mdl({{"t", t}, {"particle", t}}, els));
    all(file);
    return true;
  }
  if (file == "brewing_stand") {
    const std::string t = T(b, "brewing_stand"), base = T(b, "brewing_stand_base");
    for (const auto& k : keys) {
      json els = json::array({box({7, 0, 7}, {9, 14, 9}, "#stand"), box({9, 0, 5}, {15, 2, 11}, "#base"), box({2, 0, 1}, {8, 2, 7}, "#base"),
                              box({2, 0, 9}, {8, 2, 15}, "#base")});
      const V3 spots[3] = {{12, 2, 8}, {5, 2, 4}, {5, 2, 12}};
      for (int i = 0; i < 3; i++)
        if (prop(k, "has_bottle_" + std::to_string(i)) == "true")
          els.push_back(box({spots[i][0] - 1.5f, 2, spots[i][2] - 1.5f}, {spots[i][0] + 1.5f, 8, spots[i][2] + 1.5f}, "#bottle"));
      std::string name = "brewing_stand";
      for (int i = 0; i < 3; i++) name += prop(k, "has_bottle_" + std::to_string(i)) == "true" ? "1" : "0";
      b.model(name, mdl({{"stand", t}, {"base", base}, {"bottle", "blocks/glass"}, {"particle", t}}, els));
      bs["variants"][k] = var(name);
    }
    return true;
  }
  if (file == "cauldron") {
    const std::string top = T(b, "cauldron_top"), side = T(b, "cauldron_side"), bottom = T(b, "cauldron_bottom"), inner = T(b, "cauldron_inner");
    for (const auto& k : keys) {
      const int level = std::stoi(prop(k, "level"));
      json els = json::array({boxTBS({0, 3, 0}, {16, 4, 16}, "#inner", "#bottom", "#side"),
                              boxTBS({0, 3, 0}, {2, 16, 16}, "#top", "#bottom", "#side"), boxTBS({14, 3, 0}, {16, 16, 16}, "#top", "#bottom", "#side"),
                              boxTBS({2, 3, 0}, {14, 16, 2}, "#top", "#bottom", "#side"), boxTBS({2, 3, 14}, {14, 16, 16}, "#top", "#bottom", "#side"),
                              box({0, 0, 0}, {4, 3, 2}, "#side"), box({12, 0, 0}, {16, 3, 2}, "#side"), box({0, 0, 14}, {4, 3, 16}, "#side"),
                              box({12, 0, 14}, {16, 3, 16}, "#side")});
      if (level > 0) {
        const float h = 4.f + level * 3.f;
        els.push_back({{"from", {2, h, 2}}, {"to", {14, h, 14}}, {"faces", {{"up", {{"texture", "#water"}}}}}});
      }
      const std::string name = "cauldron_level" + std::to_string(level);
      b.model(name, mdl({{"top", top}, {"side", side}, {"bottom", bottom}, {"inner", inner}, {"water", "blocks/water_still"}, {"particle", side}}, els));
      bs["variants"][k] = var(name);
    }
    return true;
  }
  if (file == "cocoa") {
    for (const auto& k : keys) {
      const int age = std::stoi(prop(k, "age"));
      const std::string t = T(b, "cocoa_stage_" + std::to_string(age));
      const float w = 4.f + age * 2, h = 5.f + age * 2;
      // Pegado a la pared norte (el tronco al norte) y girado según su dirección
      json els = json::array({box({8 - w / 2, 12 - h, 1}, {8 + w / 2, 12, 1 + w}, "#t")});
      const std::string name = "cocoa_age" + std::to_string(age);
      b.model(name, mdl({{"t", t}, {"particle", t}}, els));
      bs["variants"][k] = var(name, 0, rotNorth(prop(k, "facing")));
    }
    return true;
  }
  if (file == "tripwire_hook") {
    const std::string t = T(b, "trip_wire_source"), wood = T(b, "planks_oak");
    for (const auto& k : keys) {
      const bool attached = prop(k, "attached") == "true";
      json els = json::array({box({6.2f, 3.8f, 14}, {9.8f, 12.2f, 16}, "#wood"), box({7.5f, attached ? 3.f : 4.f, 9}, {8.5f, attached ? 5.f : 6.f, 14}, "#hook")});
      const std::string name = std::string("tripwire_hook") + (attached ? "_attached" : "");
      b.model(name, mdl({{"wood", wood}, {"hook", t}, {"particle", wood}}, els));
      bs["variants"][k] = var(name, 0, rotNorth(prop(k, "facing")));
    }
    return true;
  }
  if (file == "tripwire") {
    const std::string t = T(b, "trip_wire");
    for (const auto& k : keys) {
      const bool ns = prop(k, "north") == "true" || prop(k, "south") == "true";
      const bool ew = prop(k, "east") == "true" || prop(k, "west") == "true";
      const float y = prop(k, "attached") == "true" ? 1.5f : 2.5f;
      json els = json::array();
      if (ns || !ew) els.push_back({{"from", {7.5f, y, 0}}, {"to", {8.5f, y, 16}}, {"shade", false}, {"faces", {{"up", {{"texture", "#t"}, {"rotation", 90}}}, {"down", {{"texture", "#t"}, {"rotation", 90}}}}}});
      if (ew) els.push_back({{"from", {0, y, 7.5f}}, {"to", {16, y, 8.5f}}, {"shade", false}, {"faces", {{"up", {{"texture", "#t"}}}, {"down", {{"texture", "#t"}}}}}});
      const std::string name = std::string("tripwire_") + (ns ? "n" : "") + (ew ? "e" : "") + (y < 2 ? "_a" : "");
      b.model(name, mdl({{"t", t}, {"particle", t}}, els, false));
      bs["variants"][k] = var(name);
    }
    return true;
  }
  if (file == "beacon") {
    const std::string glass = T(b, "glass"), obs = T(b, "obsidian"), core = T(b, "beacon");
    b.model(file, mdl({{"glass", glass}, {"obsidian", obs}, {"beacon", core}, {"particle", glass}},
                      json::array({box({0, 0, 0}, {16, 16, 16}, "#glass"), box({2, 0.1f, 2}, {14, 3, 14}, "#obsidian"), box({3, 3, 3}, {13, 14, 13}, "#beacon")})));
    all(file);
    return true;
  }
  if (file == "flower_pot") {
    const std::string t = T(b, "flower_pot"), dirt = T(b, "dirt");
    b.model(file, mdl({{"pot", t}, {"dirt", dirt}, {"particle", t}},
                      json::array({boxF({5, 0, 5}, {11, 6, 11}, {"#pot", "", "#pot", "#pot", "#pot", "#pot"}),
                                   {{"from", {6, 4, 6}}, {"to", {10, 4, 10}}, {"faces", {{"up", {{"texture", "#dirt"}}}}}}})));
    all(file);
    return true;
  }
  if (file == "anvil") {
    const std::string base = T(b, "anvil_base");
    for (int d = 0; d < 3; d++) {
      const std::string top = T(b, "anvil_top_damaged_" + std::to_string(d));
      b.model("anvil_" + std::to_string(d),
              mdl({{"base", base}, {"top", top}, {"particle", base}},
                  json::array({box({2, 0, 2}, {14, 4, 14}, "#base"), box({4, 4, 3}, {12, 5, 13}, "#base"), box({6, 5, 4}, {10, 10, 12}, "#base"),
                               boxTBS({3, 10, 0}, {13, 16, 16}, "#top", "#base", "#base")})));
    }
    // El modelo tiene el yunque a lo largo de z (mira al este/oeste en 1.8 con facing=south)
    for (const auto& k : keys) bs["variants"][k] = var("anvil_" + prop(k, "damage"), 0, rotNorth(prop(k, "facing")) + 90);
    return true;
  }
  if (file == "daylight_detector" || file == "daylight_detector_inverted") {
    const std::string top = T(b, file + "_top"), side = T(b, "daylight_detector_side");
    b.model(file, mdl({{"top", top}, {"side", side}, {"particle", top}}, json::array({boxTBS({0, 0, 0}, {16, 6, 16}, "#top", "#side", "#side")})));
    all(file);
    return true;
  }
  if (file == "hopper") {
    const std::string out = T(b, "hopper_outside"), in = T(b, "hopper_inside"), top = T(b, "hopper_top");
    const std::map<std::string, std::string> tx = {{"out", out}, {"in", in}, {"top", top}, {"particle", out}};
    json bowl = json::array({boxTBS({0, 10, 0}, {16, 11, 16}, "#in", "#out", "#out"), boxTBS({0, 11, 0}, {2, 16, 16}, "#top", "#out", "#out"),
                             boxTBS({14, 11, 0}, {16, 16, 16}, "#top", "#out", "#out"), boxTBS({2, 11, 0}, {14, 16, 2}, "#top", "#out", "#out"),
                             boxTBS({2, 11, 14}, {14, 16, 16}, "#top", "#out", "#out"), box({4, 4, 4}, {12, 10, 12}, "#out")});
    json down = bowl;
    down.push_back(box({6, 0, 6}, {10, 4, 10}, "#out"));
    b.model("hopper_down", mdl(tx, down));
    json side = bowl;
    side.push_back(box({6, 4, 0}, {10, 8, 4}, "#out"));
    b.model("hopper_side", mdl(tx, side));
    for (const auto& k : keys) {
      const std::string f = prop(k, "facing");
      bs["variants"][k] = f == "down" ? var("hopper_down") : var("hopper_side", 0, rotNorth(f));
    }
    return true;
  }
  if (file == "chest" || file == "trapped_chest" || file == "ender_chest") {
    const bool ender = file == "ender_chest";
    const std::string top = T(b, ender ? "ender_chest_cc0" : "chest_cc0_top"), side = T(b, ender ? "ender_chest_cc0" : "chest_cc0_side");
    const std::string front = T(b, ender ? "ender_chest_cc0" : (file == "trapped_chest" ? "trapped_chest_cc0_front" : "chest_cc0_front"));
    b.model(file, mdl({{"top", top}, {"side", side}, {"front", front}, {"particle", side}},
                      json::array({boxF({1, 0, 1}, {15, 14, 15}, {"#top", "#top", "#side", "#front", "#side", "#side"})})));
    all(file);
    return true;
  }
  if (file == "standing_sign" || file == "wall_sign") {
    const std::string t = T(b, "planks_oak");
    json els = file == "standing_sign" ? json::array({box({0, 7, 7.25f}, {16, 15, 8.75f}, "#t"), box({7.25f, 0, 7.25f}, {8.75f, 7, 8.75f}, "#t")})
                                       : json::array({box({0, 4.5f, 14}, {16, 12.5f, 15.5f}, "#t")});
    b.model(file, mdl({{"t", t}, {"particle", t}}, els));
    all(file);
    return true;
  }
  if (file == "standing_banner" || file == "wall_banner") {
    const std::string wood = T(b, "planks_oak"), cloth = "blocks/wool_colored_white";
    json els = file == "standing_banner"
                   ? json::array({box({7, 0, 7}, {9, 28, 9}, "#wood"), box({1, 28, 7}, {15, 30, 9}, "#wood"), box({1, 4, 8.5f}, {15, 28, 9.5f}, "#cloth")})
                   : json::array({box({1, 29, 13}, {15, 31, 15}, "#wood"), box({1, 5, 14.5f}, {15, 29, 15.5f}, "#cloth")});
    b.model(file, mdl({{"wood", wood}, {"cloth", cloth}, {"particle", wood}}, els));
    all(file);
    return true;
  }
  if (file == "skull") {
    const std::string t = T(b, "skull_cc0");
    b.model(file, mdl({{"t", t}, {"particle", t}}, json::array({box({4, 0, 4}, {12, 8, 12}, "#t")})));
    all(file);
    return true;
  }
  if (file == "barrier") {
    // Invisible en el mundo, como en 1.8 (el ítem tiene su propio dibujo)
    b.model(file, {{"textures", {{"particle", "items/barrier"}}}});
    all(file);
    return true;
  }
  return false;
}

}  // namespace mcw::cc0
