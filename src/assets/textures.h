#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "assets/image.h"
#include "core/types.h"

namespace mcw {

class PackStack;

/// Texturas de bloque organizadas como capas de un texture array (todas del mismo tamaño),
/// con sus mipmaps y las animaciones definidas en los `.mcmeta`.
class BlockTextures {
 public:
  BlockTextures();

  /// Capa de una textura ("blocks/stone" o "minecraft:blocks/stone"). La registra si es nueva.
  u16 layerFor(const std::string& name);
  static constexpr u16 kMissingLayer = 0;

  /// Carga todas las texturas registradas desde los packs.
  void load(const PackStack& packs);

  int tileSize() const { return tileSize_; }
  int mipLevels() const { return mipLevels_; }
  int layerCount() const { return static_cast<int>(layers_.size()); }
  /// Cadena de mipmaps de una capa (nivel 0 = tamaño completo).
  const std::vector<Image>& mips(int layer) const { return layers_[layer]; }
  const std::vector<std::string>& missing() const { return missing_; }

  /// Avanza un tick (1/20 s) las animaciones. Devuelve las capas que hay que volver a subir.
  std::vector<int> tick();
  int animationCount() const { return static_cast<int>(anims_.size()); }

 private:
  struct Animation {
    int layer = 0;
    std::vector<Image> frames;                  // ya escalados al tamaño de capa
    std::vector<std::pair<int, int>> sequence;  // (frame, duración en ticks)
    bool interpolate = false;
    int step = 0, ticks = 0;
  };
  std::vector<Image> buildMips(const Image& base) const;

  std::unordered_map<std::string, u16> byName_;
  std::vector<std::string> names_;
  std::vector<std::vector<Image>> layers_;
  std::vector<Animation> anims_;
  std::vector<std::string> missing_;
  int tileSize_ = 16;
  int mipLevels_ = 5;
};

/// Colores de bioma a partir de colormap/grass.png y colormap/foliage.png.
class Colormaps {
 public:
  Colormaps();
  void load(const PackStack& packs);
  u32 grass(int biome) const { return grassByBiome_[biome & 255]; }
  u32 foliage(int biome) const { return foliageByBiome_[biome & 255]; }

 private:
  std::array<u32, 256> grassByBiome_{}, foliageByBiome_{};  // precalculado: el mallador lo pide por bloque
};

std::string texturePath(const std::string& name);

}  // namespace mcw
