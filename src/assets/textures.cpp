#include "assets/textures.h"

#include <algorithm>
#include <bit>
#include <map>
#include <nlohmann/json.hpp>

#include "assets/pack.h"
#include "core/log.h"
#include "data/biomes.h"

namespace mcw {
namespace {

std::string normalize(std::string name) {
  if (name.rfind("minecraft:", 0) == 0) name = name.substr(10);
  return name;
}

Image missingTexture(int size) {
  Image img(size, size);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) img.set(x, y, ((x < size / 2) ^ (y < size / 2)) ? 0xFFF800F8 : 0xFF000000);
  return img;
}

Image lerpImage(const Image& a, const Image& b, float t) {
  Image out = a;
  for (std::size_t i = 0; i < out.rgba.size(); i++)
    out.rgba[i] = static_cast<u8>(a.rgba[i] + (b.rgba[i] - a.rgba[i]) * t);
  return out;
}

}  // namespace

std::string texturePath(const std::string& name) { return "assets/minecraft/textures/" + normalize(name) + ".png"; }

BlockTextures::BlockTextures() {
  names_.push_back("missingno");
  byName_["missingno"] = kMissingLayer;
}

u16 BlockTextures::layerFor(const std::string& rawName) {
  const std::string name = normalize(rawName);
  auto it = byName_.find(name);
  if (it != byName_.end()) return it->second;
  const u16 layer = static_cast<u16>(names_.size());
  names_.push_back(name);
  byName_[name] = layer;
  return layer;
}

std::vector<Image> BlockTextures::buildMips(const Image& base) const {
  std::vector<Image> chain{base};
  for (int i = 1; i < mipLevels_; i++) chain.push_back(chain.back().halfMip());
  return chain;
}

void BlockTextures::load(const PackStack& packs) {
  struct Loaded {
    Image image;
    nlohmann::json anim;
    bool ok = false;
  };
  std::vector<Loaded> loaded(names_.size());
  std::map<int, int> widths;
  for (std::size_t i = 1; i < names_.size(); i++) {
    auto img = packs.readImage(texturePath(names_[i]));
    if (!img || img->width == 0 || img->height < img->width) {
      missing_.push_back(names_[i]);
      continue;
    }
    loaded[i].image = std::move(*img);
    loaded[i].ok = true;
    if (auto meta = packs.readJson(texturePath(names_[i]) + ".mcmeta"); meta && meta->contains("animation"))
      loaded[i].anim = (*meta)["animation"];
    widths[loaded[i].image.width]++;
  }
  // El tamaño de capa es el ancho mayor (como hace el juego: las texturas más pequeñas se amplían),
  // con un tope por memoria: 128 en el navegador y 256 en escritorio.
#ifdef __EMSCRIPTEN__
  constexpr int kMaxTile = 128;
#else
  constexpr int kMaxTile = 256;
#endif
  tileSize_ = 16;
  for (auto [w, n] : widths) tileSize_ = std::max(tileSize_, w);
  tileSize_ = std::clamp(static_cast<int>(std::bit_floor(static_cast<unsigned>(tileSize_))), 4, kMaxTile);
  mipLevels_ = std::countr_zero(static_cast<unsigned>(tileSize_)) + 1;

  layers_.assign(names_.size(), {});
  anims_.clear();
  const Image missing = missingTexture(tileSize_);
  for (std::size_t i = 0; i < names_.size(); i++) {
    if (!loaded[i].ok) {
      layers_[i] = buildMips(missing);
      continue;
    }
    const Image& img = loaded[i].image;
    const int fw = img.width, frames = img.height / img.width;
    Image first = img.crop(0, 0, fw, fw).resized(tileSize_, tileSize_);
    layers_[i] = buildMips(first);
    if (frames > 1 && !loaded[i].anim.is_null()) {
      Animation a;
      a.layer = static_cast<int>(i);
      for (int f = 0; f < frames; f++) a.frames.push_back(img.crop(0, f * fw, fw, fw).resized(tileSize_, tileSize_));
      const auto& an = loaded[i].anim;
      const int frametime = std::max(1, an.value("frametime", 1));
      a.interpolate = an.value("interpolate", false);
      if (an.contains("frames") && an["frames"].is_array()) {
        for (const auto& f : an["frames"]) {
          if (f.is_number_integer()) a.sequence.emplace_back(f.get<int>(), frametime);
          else if (f.is_object()) a.sequence.emplace_back(f.value("index", 0), std::max(1, f.value("time", frametime)));
        }
      } else {
        for (int f = 0; f < frames; f++) a.sequence.emplace_back(f, frametime);
      }
      std::erase_if(a.sequence, [&](auto& s) { return s.first < 0 || s.first >= frames; });
      if (!a.sequence.empty()) anims_.push_back(std::move(a));
    }
  }
  log::info("texturas de bloque: {} capas de {}x{}, {} animadas, {} sin encontrar", names_.size(), tileSize_, tileSize_,
            anims_.size(), missing_.size());
}

std::vector<int> BlockTextures::tick() {
  std::vector<int> changed;
  for (Animation& a : anims_) {
    a.ticks++;
    const auto [frame, duration] = a.sequence[a.step];
    bool advanced = false;
    if (a.ticks >= duration) {
      a.ticks = 0;
      a.step = (a.step + 1) % static_cast<int>(a.sequence.size());
      advanced = true;
    }
    if (!advanced && !a.interpolate) continue;
    const auto [cur, curDur] = a.sequence[a.step];
    Image img = a.frames[cur];
    if (a.interpolate) {
      const int next = a.sequence[(a.step + 1) % a.sequence.size()].first;
      img = lerpImage(a.frames[cur], a.frames[next], static_cast<float>(a.ticks) / curDur);
    }
    layers_[a.layer] = buildMips(img);
    changed.push_back(a.layer);
  }
  return changed;
}

Colormaps::Colormaps() {
  grassByBiome_.fill(0x7FB238);
  foliageByBiome_.fill(0x59AE30);
}

void Colormaps::load(const PackStack& packs) {
  auto g = packs.readImage("assets/minecraft/textures/colormap/grass.png");
  auto f = packs.readImage("assets/minecraft/textures/colormap/foliage.png");
  const bool hasGrass = g && g->width == 256 && g->height == 256;
  const bool hasFoliage = f && f->width == 256 && f->height == 256;
  for (int b = 0; b < 256; b++) {
    auto [x, y] = colormapCoords(b);
    grassByBiome_[b] = hasGrass ? (g->get(x, y) & 0xFFFFFF) : 0x7FB238;
    foliageByBiome_[b] = hasFoliage ? (f->get(x, y) & 0xFFFFFF) : 0x59AE30;
  }
}

}  // namespace mcw
