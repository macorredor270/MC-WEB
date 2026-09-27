#pragma once
#include <memory>

namespace mcw {

class MemoryPack;

/// Pack integrado con texturas, modelos y fuente generados por código (CC0, sin nada de Mojang).
/// Se usa cuando no hay un jar de Minecraft y como respaldo de lo que falte en otros packs.
std::shared_ptr<MemoryPack> makeCC0Pack();

}  // namespace mcw
