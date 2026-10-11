#pragma once
// Botín de los cofres que nacen con el terreno (mazmorras, fortines del Nether y del End...).
#include "core/random.h"
#include "game/menu.h"

namespace mcw {

/// Tablas de botín: 1 mazmorra, 2 fortín del Nether, 3 pasillo de fortín, 4 biblioteca, 5 cruce de fortín.
void fillLoot(ChestState& chest, int table, Random& rng);

}  // namespace mcw
