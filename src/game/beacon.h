#pragma once
// Faro: la pirámide de bloques de metal que tiene debajo, si le llega el cielo y qué efectos puede dar (1.8, según minecraft.wiki).
#include <vector>

#include "core/types.h"

namespace mcw {

class World;

/// Niveles (0 a 4) de la pirámide de hierro, oro, esmeralda y diamante bajo el faro de (x, y, z).
int beaconPyramidLevels(const World& w, int x, int y, int z);
/// ¿Le llega el cielo? (ningún bloque opaco por encima; el cristal, el hielo y demás dejan pasar)
bool beaconSkyClear(const World& w, int x, int y, int z);
/// Colores (RGB) de los cristales de colores que tiñen el rayo, de abajo arriba.
std::vector<u32> beaconBeamColors(const World& w, int x, int y, int z);

/// ¿Puede elegirse ese efecto como poder principal con esos niveles? (velocidad y prisa; resistencia y salto; fuerza)
bool beaconPrimaryAllowed(int effect, int levels);
/// ¿Y como secundario? (regeneración, o repetir el principal, solo con 4 niveles)
bool beaconSecondaryAllowed(int effect, int primary, int levels);
/// ¿Vale ese objeto como pago? (lingote de hierro o de oro, esmeralda o diamante)
bool beaconPayment(int itemId);

}  // namespace mcw
