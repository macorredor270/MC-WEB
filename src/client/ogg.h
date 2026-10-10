#pragma once
#include <span>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Decodifica un .ogg (Vorbis) a muestras mono en [-1, 1] a `sampleRate` Hz (mezcla los canales y remuestrea). Vacío si no vale.
std::vector<float> decodeOggMono(std::span<const u8> data, int sampleRate);

}  // namespace mcw
