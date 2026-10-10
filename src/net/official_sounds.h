#pragma once
// Los sonidos oficiales de Minecraft 1.8 (archivos .ogg) tampoco vienen en el jar: están en el servidor de recursos de Mojang,
// uno por huella. Se bajan solo los de los eventos pedidos (unos pocos MB) y se guardan en la carpeta de datos del juego.
// Son de Mojang y están sujetos a su EULA; este proyecto no los incluye.
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "core/types.h"

namespace mcw::net {

/// Para cada evento de sonido de 1.8 ("dig.stone", "mob.pig.say"...), los .ogg de sus variantes. Los que faltan se descargan
/// (los que ya estaban no). Sin red, devuelve solo lo que haya guardado. `cancel` corta la descarga.
std::map<std::string, std::vector<std::vector<u8>>> loadOfficialSounds(const std::vector<std::string>& events, const std::atomic<bool>& cancel);

}  // namespace mcw::net
