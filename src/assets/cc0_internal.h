#pragma once
// Piezas compartidas del pack libre (CC0): utilidades de dibujo y de modelos JSON.
#include <array>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "assets/image.h"
#include "assets/pack.h"
#include "core/types.h"

namespace mcw::cc0 {

using json = nlohmann::json;
extern const std::string kTex;

u32 rgb(int r, int g, int b, int a = 255);
int R(u32 c);
int G(u32 c);
int Bc(u32 c);
u32 shade(u32 c, int d);
u32 seedOf(const std::string& s);

Image noisy(u32 base, int amount, u32 seed);
void speckle(Image& img, u32 color, int count, int size, u32 seed);
Image planks(u32 base, u32 seed);
Image logSide(u32 bark, u32 seed);
Image logTop(u32 inner, u32 bark, u32 seed);
Image leaves(u32 seed);
Image grassBlades(u32 seed, bool fern);
Image flower(u32 petal, u32 center, u32 seed, int stemHeight = 8);
Image mushroom(u32 cap);
Image sapling(u32 leaf);
Image glassTex(u32 tint, int alpha);
Image torchTex();
Image fluid(u32 base, int amount, u32 seed, int frame);
Image strip(const std::vector<Image>& frames);
u32 baseColor(const std::string& n);
const std::map<std::string, u32>& dyeColors();
/// (tablones, corteza) por madera: oak, spruce, birch, jungle, acacia, dark_oak.
const std::map<std::string, std::pair<u32, u32>>& woods();

void fillRect(Image& img, int x, int y, int w, int h, u32 c);
void line(Image& img, int x0, int y0, int x1, int y1, u32 c);
void disc(Image& img, double cx, double cy, double r, u32 c, u32 edge);

/// Nombre de las texturas de madera en 1.8 ("big_oak" para el roble oscuro).
std::string woodTex(const std::string& wood);

class Builder {
 public:
  explicit Builder(MemoryPack& p) : pack_(p) {}

  void tex(const std::string& name, Image img) {
    textures_.insert(name);
    pack_.putImage(kTex + "blocks/" + name + ".png", std::move(img));
  }
  bool hasTex(const std::string& name) const { return textures_.count(name) > 0; }
  void model(const std::string& name, json j) { pack_.putJson("assets/minecraft/models/block/" + name + ".json", j); }
  /// Textura simple con ruido para un nombre si no está ya definida.
  void ensureSimple(const std::string& name);
  MemoryPack& pack() { return pack_; }

  std::set<std::string> textures_;

 private:
  MemoryPack& pack_;
};

json cubeAll(const std::string& t);
json column(const std::string& end, const std::string& side);
json bottomTop(const std::string& bottom, const std::string& top, const std::string& side);
json cross(const std::string& t, bool tinted);
json face(const std::string& tex, const char* cull, int tint = -1, std::array<int, 4> uv = {0, 0, 16, 16});

/// Bloques con forma propia (escaleras, puertas, raíles, redstone...). Rellena `bs` y devuelve
/// true si el fichero de blockstate es uno de ellos.
bool addShapedBlock(Builder& b, const std::string& file, const std::set<std::string>& keys, json& bs);

/// Nombre de la textura de 1.8 de un ítem por su nombre de registro ("wooden_sword" -> "wood_sword").
std::string itemTex18(const std::string& name);
/// Sprite `items/<textura de 1.8>.png` y modelo `item/<name>.json` que lo usa.
void putItem(MemoryPack& pack, const std::string& name, const Image& img);

/// Sprites y modelos de ítem de todo lo que no tenga uno ya.
void addAllItems(MemoryPack& pack);

}  // namespace mcw::cc0
