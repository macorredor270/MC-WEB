#pragma once
// Árboles que crecen de los brotes (los del mundo generado tienen su propio código en el generador).
#include <functional>

#include "core/random.h"
#include "data/blocks.h"

namespace mcw {

/// Hace crecer un árbol del tipo de brote `type` (0 roble, 1 abeto, 2 abedul, 3 jungla, 4 acacia, 5 roble oscuro) con el
/// brote en (x, y, z). `get` lee un bloque y `put` pone uno (con `log` = si es un tronco). Los de roble oscuro y los
/// gigantes de jungla necesitan 4 brotes juntos: `x`, `z` es la esquina de menor coordenada. Devuelve false (sin tocar nada)
/// si no hay sitio.
bool growTree(const std::function<BlockState(int, int, int)>& get, const std::function<void(int, int, int, BlockState, bool)>& put,
              int type, int x, int y, int z, Random& rng, bool big = false);

}  // namespace mcw
