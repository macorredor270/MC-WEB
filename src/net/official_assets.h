#pragma once
// Los recursos oficiales de Minecraft 1.8.8 (texturas, modelos, idiomas) se descargan de los servidores de Mojang al
// equipo de quien juega; este proyecto no los incluye ni los redistribuye. Son de Mojang y están sujetos a su EULA.
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace mcw::net {

/// Dónde se guarda lo descargado (`<datos del juego>/assets`).
std::filesystem::path officialAssetsDir();

/// El jar 1.8.8 ya descargado, si está.
std::optional<std::filesystem::path> downloadedJar();

/// Baja el cliente 1.8.8 si aún no está, comprueba su SHA-1 contra el que da Mojang y lo guarda. nullopt si no hay red
/// o algo falla (`error` dice qué). `progress(recibidos, total)` devuelve false para cancelar.
std::optional<std::filesystem::path> downloadOfficialJar(std::string* error, const std::function<bool(std::size_t, std::size_t)>& progress = {});

/// Los datos de la versión 1.8.8 (JSON de Mojang: cliente, índice de recursos...), guardados la primera vez. Texto vacío si no hay.
std::string officialVersionJson(std::string* error);

}  // namespace mcw::net
