#pragma once
#include <string_view>
#include <utility>

#include "core/types.h"

namespace mcw {

struct BiomeInfo {
  int id = 1;
  std::string_view name;
  std::string_view displayName;
  float temperature = 0.8f;
  float rainfall = 0.4f;
  u32 color = 0;
};

const BiomeInfo& biomeInfo(int id);  // bioma desconocido -> plains

/// Ids de bioma de 1.8 que usa nuestro generador.
namespace Biome {
inline constexpr int hell = 8, the_end = 9, ocean = 0, plains = 1, desert = 2, extreme_hills = 3, forest = 4, taiga = 5, swamp = 6, river = 7,
                     frozen_ocean = 10, ice_plains = 12, beach = 16, deep_ocean = 24, stone_beach = 25,
                     cold_beach = 26, birch_forest = 27, cold_taiga = 30, savanna = 35;
}

/// Coordenadas en los colormaps grass.png/foliage.png (256x256) de un bioma, según el método
/// documentado en minecraft.wiki ("Color#Biome colors").
std::pair<int, int> colormapCoords(int biomeId);

}  // namespace mcw
