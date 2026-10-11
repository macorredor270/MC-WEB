#pragma once
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/types.h"
#include "world/chunk.h"
#include "world/noise.h"

namespace mcw {

/// Información de una columna del mundo, calculada sin estado a partir de la semilla.
struct ColumnInfo {
  int height = 64;  // y del primer bloque de aire sobre el terreno
  int biome = 1;
  bool river = false;
  float mountain = 0;
};

/// Tipos de mundo de 1.8 que admite el generador.
enum class WorldType : u8 { Default, Flat, LargeBiomes, Amplified, Nether, End };

/// Cómo se genera un mundo (level.dat: generatorName + generatorOptions + MapFeatures).
struct GeneratorSettings {
  WorldType type = WorldType::Default;
  std::vector<std::pair<BlockState, int>> flatLayers;  // de abajo arriba: bloque y grosor
  int flatBiome = 1;                                   // llanura
  bool structures = true;

  /// Desde los valores de level.dat ("default", "flat", "largeBiomes", "amplified").
  static GeneratorSettings fromLevel(std::string_view generatorName, std::string_view options, bool structures);
  std::string generatorName() const;
  /// Preajuste de superplano en el formato de 1.8: "3;minecraft:bedrock,2*minecraft:dirt,minecraft:grass;1;village".
  std::string flatOptions() const;
  /// Los ajustes de una dimensión: 0 el mundo (los de `base`), -1 el Nether, 1 el End.
  static GeneratorSettings forDimension(int dimension, const GeneratorSettings& base);
  static constexpr const char* kDefaultFlat = "3;minecraft:bedrock,2*minecraft:dirt,minecraft:grass;1;village";
};

/// Dónde están (x, z) los tres primeros fortines (strongholds) de un mundo; ahí hay un portal del End bajo tierra.
std::array<std::pair<int, int>, 3> strongholdPositions(u64 seed);

/// Generador de terreno propio. Cada chunk se genera sin depender de otros chunks (los árboles
/// que cruzan bordes se reconstruyen a partir de la semilla del chunk vecino), así que se puede
/// paralelizar sin coordinación.
class TerrainGenerator {
 public:
  static constexpr int kSeaLevel = 63;  // el agua llena y <= 62

  explicit TerrainGenerator(u64 seed, GeneratorSettings settings = {});
  u64 seed() const { return seed_; }
  const GeneratorSettings& settings() const { return settings_; }

  ColumnInfo column(int x, int z) const;
  /// Punto de aparición: en espiral desde el origen, la primera tierra firme (ni océano, ni río
  /// ni playa). Devuelve x, altura del terreno y z (sin generar chunks).
  std::array<int, 3> findSpawn() const;
  /// Genera el chunk completo, con decoración, biomas, heightmap y luz inicial (sin vecinos).
  std::unique_ptr<Chunk> generate(int cx, int cz) const;

 private:
  void fillColumn(Chunk& c, int lx, int lz, int wx, int wz, const ColumnInfo& col) const;
  void carveCaves(Chunk& c, const ColumnInfo* cols) const;
  void placeOres(Chunk& c) const;
  void placeTrees(Chunk& c) const;
  void placePlants(Chunk& c, const ColumnInfo* cols) const;
  void placeSnow(Chunk& c, const ColumnInfo* cols) const;

  std::unique_ptr<Chunk> generateFlat(int cx, int cz) const;
  std::unique_ptr<Chunk> generateNether(int cx, int cz) const;
  std::unique_ptr<Chunk> generateEnd(int cx, int cz) const;
  void placeFortress(Chunk& c) const;
  void placeStrongholds(Chunk& c) const;
  void placeDungeons(Chunk& c, const ColumnInfo* cols) const;

  u64 seed_;
  GeneratorSettings settings_;
  int flatHeight_ = 4;
  OctaveNoise continental_, detail_, rugged_, hills_, temperature_, humidity_, river_;
  OctaveNoise cave1_, cave2_, cavern_, surface_;
};

}  // namespace mcw
