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

/// nullopt si el estado no tiene modelo (aire, fluidos, bloques aún no soportados).
std::optional<BlockstateRef> blockstateOf(BlockState state);

/// Todos los estados que tienen blockstate, con su referencia.
std::vector<std::pair<BlockState, BlockstateRef>> allMappedStates();

}  // namespace mcw
