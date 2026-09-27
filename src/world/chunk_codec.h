#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "world/chunk.h"

namespace mcw {

/// Chunk a bytes y vuelta (sin comprimir), para pasarlo entre el hilo principal y los Web Workers.
/// Solo sirve dentro del mismo build: usa el orden de bytes y la disposición en memoria nativos.
std::vector<u8> encodeChunk(const Chunk& c);
std::unique_ptr<Chunk> decodeChunk(const u8* data, std::size_t size);

}  // namespace mcw
