#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "data/blocks.h"

namespace mcw {

/// Fichero de blockstate del resource pack (`assets/minecraft/blockstates/<file>.json`) y la
/// clave de variante dentro de él para un estado de bloque de 1.8.
struct BlockstateRef {
  std::string file;
  std::string variant;
  bool operator==(const BlockstateRef&) const = default;
};

/// nullopt si el estado no tiene modelo (aire, fluidos, bloques que se dibujan aparte).
/// En los bloques que dependen de sus vecinos devuelve la variante "sin vecinos".
std::optional<BlockstateRef> blockstateOf(BlockState state);

/// Todos los estados que tienen blockstate, con su referencia (variante por defecto).
std::vector<std::pair<BlockState, BlockstateRef>> allMappedStates();

// --- Bloques cuyo dibujo depende de los vecinos (como el "estado real" de 1.8) ---
// Vallas, paneles, muros, escaleras, puertas, redstone, enredaderas, puertas de valla, tallos y
// cable trampa. `ext` son unos bits extra (conexiones, forma...) que calcula `neighborBits`.

/// ¿El dibujo del bloque depende de los vecinos?
bool dependsOnNeighbors(int id);

/// Para estos bloques el modelo solo depende de `ext` y no de la metadata (puertas, redstone):
/// se busca siempre con meta 0.
bool extReplacesMeta(int id);

/// Número de valores de `ext` posibles para el bloque (0 si no depende de los vecinos).
int extCount(int id);

/// Lectura de un vecino relativo a la posición del bloque.
using NeighborFn = BlockState (*)(const void* ctx, int dx, int dy, int dz);

/// Calcula los bits extra del bloque en su sitio. Solo mira vecinos a distancia 1.
int neighborBits(BlockState s, const void* ctx, NeighborFn at);

/// Referencia para un estado con sus bits extra.
std::optional<BlockstateRef> blockstateOf(BlockState state, int ext);

struct ExtendedStateRef {
  BlockState state;
  int ext;
  BlockstateRef ref;
};
/// Todas las combinaciones (estado, ext) de los bloques que dependen de los vecinos.
std::vector<ExtendedStateRef> allExtendedStates();

/// Color del polvo de redstone según su potencia (0..15).
u32 redstoneWireColor(int power);
/// Color de los tallos de calabaza y sandía según su edad (0..7).
u32 stemColor(int age);

}  // namespace mcw
