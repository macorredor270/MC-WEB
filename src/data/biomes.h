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
                     frozen_ocean = 10, frozen_river = 11, ice_plains = 12, ice_mountains = 13, mushroom_island = 14, mushroom_shore = 15, beach = 16,
                     desert_hills = 17, forest_hills = 18, taiga_hills = 19, jungle = 21, jungle_hills = 22, jungle_edge = 23, deep_ocean = 24,
                     stone_beach = 25, cold_beach = 26, birch_forest = 27, birch_forest_hills = 28, roofed_forest = 29, cold_taiga = 30,
                     cold_taiga_hills = 31, mega_taiga = 32, mega_taiga_hills = 33, extreme_hills_plus = 34, savanna = 35, savanna_plateau = 36,
                     mesa = 37, mesa_plateau_f = 38, mesa_plateau = 39, ice_spikes = 140;
}

/// El bioma "de base" de una variante de colinas (bosque -> colinas boscosas...): vale para decidir árboles, plantas y criaturas.
int baseBiome(int biomeId);
/// Los 39 biomas que hay que pisar para el logro Aventurero, en el orden de 1.8 (y si `id` cuenta, su posición; -1 si no).
int explorationIndex(int biomeId);
inline constexpr int kExplorationBiomes = 39;

/// Coordenadas en los colormaps grass.png/foliage.png (256x256) de un bioma, según el método
/// documentado en minecraft.wiki ("Color#Biome colors").
std::pair<int, int> colormapCoords(int biomeId);

}  // namespace mcw
