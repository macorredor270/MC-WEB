#include "assets/derived_pack.h"

#include <algorithm>

namespace mcw {
namespace {

void paste(Image& dst, const Image& src, int x0, int y0) {
  for (int y = 0; y < src.height; y++)
    for (int x = 0; x < src.width; x++)
      if (x0 + x >= 0 && y0 + y >= 0 && x0 + x < dst.width && y0 + y < dst.height) dst.set(x0 + x, y0 + y, src.get(x, y));
}

}  // namespace

std::shared_ptr<MemoryPack> makeChestIconPack(const PackStack& packs) {
  auto out = std::make_shared<MemoryPack>("derivadas");
  const struct { const char* file; const char* prefix; } kTypes[] = {{"normal", "chest"}, {"trapped", "trapped_chest"}, {"ender", "ender_chest"}};
  for (const auto& t : kTypes) {
    const auto src = packs.readImage(std::string("assets/minecraft/textures/entity/chest/") + t.file + ".png");
    if (!src || src->width < 64) continue;
    const int k = src->width / 64;  // 1 en las texturas de 64x64; más en las de alta resolución
    // La tapa (14x5 de alto por cara) encima de la base (14x10: se salta su primera fila, que queda tapada por la tapa): 14x14
    auto side = [&](int u) {
      Image img(14 * k, 14 * k, 0);
      paste(img, src->crop(u * k, 14 * k, 14 * k, 5 * k), 0, 0);
      paste(img, src->crop(u * k, 33 * k + k, 14 * k, 9 * k), 0, 5 * k);
      return img;
    };
    Image top = src->crop(14 * k, 0, 14 * k, 14 * k);
    Image sideImg = side(0);
    Image front = side(14);
    paste(front, src->crop(k, k, 2 * k, 4 * k), 6 * k, 3 * k);  // el pestillo
    const std::string base = std::string("assets/minecraft/textures/blocks/") + t.prefix + "_cc0_";
    out->putImage(base + "top.png", top.resized(16 * k, 16 * k));
    out->putImage(base + "side.png", sideImg.resized(16 * k, 16 * k));
    out->putImage(base + "front.png", front.resized(16 * k, 16 * k));
  }
  return out;
}

}  // namespace mcw
