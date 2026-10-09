#pragma once
#include <optional>
#include <span>
#include <utility>

#include "core/types.h"

namespace mcw {

/// Ancho y alto de un PNG leyendo solo su cabecera (sin descomprimir nada). nullopt si no empieza
/// como un PNG. Sirve para rechazar de entrada lo que llega por la red (skins de otros jugadores).
std::optional<std::pair<int, int>> pngSize(std::span<const u8> data);

}  // namespace mcw
