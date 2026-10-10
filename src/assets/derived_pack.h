#pragma once
// Texturas de bloque que salen de texturas de entidad del paquete activo (los cofres de 1.8 solo existen como entidad): así
// el icono del cofre en el inventario usa la textura de verdad, sea la oficial, la de un pack de recursos o la libre.
#include <memory>

#include "assets/pack.h"

namespace mcw {

/// Un pack en memoria con `blocks/chest_cc0_top|side|front` (y `trapped_chest_` y `ender_chest_`) sacados de
/// `entity/chest/normal|trapped|ender.png`. Se pone encima de la pila.
std::shared_ptr<MemoryPack> makeChestIconPack(const PackStack& packs);

}  // namespace mcw
