#pragma once
// Generación de biomas y de relieve del Overworld de Minecraft 1.8 (pila de capas + ruido de superficie).
//
// El algoritmo es el mismo que reproduce Cubiomes (https://github.com/Cubitect/cubiomes, licencia MIT,
// © 2020 Cubitect): con la misma semilla da los mismos biomas que el juego original. Esta es una
// reescritura en C++ para la arquitectura del proyecto; el aviso de licencia está en docs/THIRD_PARTY.md.
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace mcw::legacy {

/// Pila de capas de biomas de 1.8. Una celda de la escala 1:4 son 4x4 bloques.
class BiomeLayers {
 public:
  BiomeLayers(u64 worldSeed, bool largeBiomes);
  /// Biomas del área (x, z, w, h) a escala 1:4. Índice: x + z * w.
  void area4(int x, int z, int w, int h, std::vector<int>& out) const;
  /// Biomas por bloque (escala 1:1, con el zoom Voronoi final). Índice: x + z * w.
  void area1(int x, int z, int w, int h, std::vector<int>& out) const;

 private:
  enum class Kind : u8 {
    Continent, ZoomFuzzy, Zoom, Land, Island, Snow, Cool, Heat, Special, Mushroom, DeepOcean, Biome, BiomeEdge, Noise, Hills,
    Sunflower, Shore, Smooth, River, RiverMix, Voronoi
  };
  struct Layer {
    Kind kind;
    int p = -1, p2 = -1;  // capas padre (índices)
    u64 salt = 0;         // la base de sal (0 = sin sembrar)
    u64 startSalt = 0, startSeed = 0;
  };
  int add(Kind kind, u64 saltBase, int p, int p2 = -1);
  void map(int idx, int x, int z, int w, int h, std::vector<int>& out) const;

  std::vector<Layer> layers_;
  int entry4_ = 0, entry1_ = 0;
};

/// Biomas y relieve de un chunk según el algoritmo de 1.8.
struct ChunkTerrain {
  std::array<i16, 256> top{};   // y del bloque sólido más alto de cada columna (índice: x + z * 16)
  std::array<u8, 256> biome{};  // id de bioma por columna (escala 1:1)
};

/// El generador del Overworld: la semilla decide los biomas y, con ellos, la altura del terreno.
/// Seguro entre hilos; guarda los chunks ya calculados.
class Terrain {
 public:
  Terrain(u64 worldSeed, bool largeBiomes);
  ~Terrain();
  std::shared_ptr<const ChunkTerrain> chunk(int cx, int cz) const;
  /// Bioma de un bloque sin calcular el relieve.
  int biomeAt(int x, int z) const;
  const BiomeLayers& layers() const { return layers_; }

 private:
  struct Noise;  // las octavas del ruido de superficie
  std::shared_ptr<const ChunkTerrain> compute(int cx, int cz) const;

  BiomeLayers layers_;
  std::unique_ptr<Noise> noise_;
  mutable std::mutex mutex_;
  mutable std::unordered_map<u64, std::shared_ptr<const ChunkTerrain>> cache_;
};

/// Altura base y variación de un bioma (los que usa el relieve de 1.8); false si no se conoce.
bool biomeDepthScale(int biome, double& depth, double& scale);

// --- Dónde caen las estructuras (las reglas de 1.8; el contenido de cada estructura es propio) -------------------

struct StructPos {
  int x = 0, z = 0;
};

/// El chunk donde se intenta una estructura en la región (rx, rz): `region` chunks de lado y una de las `range` primeras
/// posiciones en cada eje. Con `large` (monumentos) el reparto es triangular, más cerca del centro de la región.
/// Devuelve la esquina del chunk, en bloques.
StructPos regionAttempt(u64 seed, int rx, int rz, u32 salt, int region, int range, bool large);
/// Templos del desierto y de la jungla y cabañas de bruja (el tipo lo decide el bioma): una prueba por región de 32 chunks.
inline StructPos scatteredFeatureAttempt(u64 seed, int rx, int rz) { return regionAttempt(seed, rx, rz, 14357617, 32, 24, false); }
inline StructPos villageAttempt(u64 seed, int rx, int rz) { return regionAttempt(seed, rx, rz, 10387312, 32, 24, false); }
inline StructPos monumentAttempt(u64 seed, int rx, int rz) { return regionAttempt(seed, rx, rz, 10387313, 32, 27, true); }
/// ¿Este chunk es el origen de una mina abandonada? (más a menudo cuanto más lejos del origen, hasta 80 chunks)
bool mineshaftChunk(u64 seed, int cx, int cz);
/// Fortín del Nether de la región (rx, rz) de 16x16 chunks: su esquina de chunk en bloques, si esa región tiene uno.
bool netherFortressAttempt(u64 seed, int rx, int rz, StructPos& out);
/// Los tres fortines de un mundo (x, z de la escalera), con los biomas de `layers`.
std::array<StructPos, 3> strongholds(u64 seed, const BiomeLayers& layers);
/// El punto de aparición del mundo: un bioma de bosque, llanura, taiga o jungla cerca del origen y, desde ahí, paseos al azar hasta dar con
/// suelo de hierba. `surfaceTop(x, z)` da la y del bloque más alto del terreno.
StructPos worldSpawn(u64 seed, const BiomeLayers& layers, const std::function<int(int, int)>& surfaceTop);

}  // namespace mcw::legacy
