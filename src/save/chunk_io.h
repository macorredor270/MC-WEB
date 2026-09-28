#pragma once
// Chunks guardados con lo que llevan dentro (criaturas, objetos, hornos y cofres): lo usan el
// juego y el servidor dedicado.
#include <memory>

#include "game/session.h"
#include "save/region.h"
#include "world/chunk.h"

namespace mcw::save {

/// Lee un chunk de las regiones y mete en la partida sus criaturas, objetos, hornos y cofres.
/// nullptr si no está guardado (o no se puede leer).
std::unique_ptr<Chunk> loadChunk(RegionStore& regions, ChunkPos p, GameSession& session);

/// Guarda un chunk con lo que hay en él. Si `unloading`, las criaturas, objetos, hornos y cofres
/// salen de la partida (el chunk sale de memoria).
void storeChunk(RegionStore& regions, const Chunk& c, GameSession& session, bool unloading, i64 worldTime);

}  // namespace mcw::save
