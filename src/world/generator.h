#pragma once
#include <memory>

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

/// Generador de terreno propio. Cada chunk se genera sin depender de otros chunks (los árboles
/// que cruzan bordes se reconstruyen a partir de la semilla del chunk vecino), así que se puede
/// paralelizar sin coordinación.
class TerrainGenerator {
 public:
  static constexpr int kSeaLevel = 63;  // el agua llena y <= 62

  explicit TerrainGenerator(u64 seed);
  u64 seed() const { return seed_; }

  ColumnInfo column(int x, int z) const;
  /// Genera el chunk completo, con decoración, biomas, heightmap y luz inicial (sin vecinos).
  std::unique_ptr<Chunk> generate(int cx, int cz) const;

 private:
  void fillColumn(Chunk& c, int lx, int lz, int wx, int wz, const ColumnInfo& col) const;
  void carveCaves(Chunk& c, const ColumnInfo* cols) const;
  void placeOres(Chunk& c) const;
  void placeTrees(Chunk& c) const;
  void placePlants(Chunk& c, const ColumnInfo* cols) const;
  void placeSnow(Chunk& c, const ColumnInfo* cols) const;

  u64 seed_;
  OctaveNoise continental_, detail_, rugged_, hills_, temperature_, humidity_, river_;
  OctaveNoise cave1_, cave2_, cavern_, surface_;
};

}  // namespace mcw
