#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace mcw::fs {

std::optional<std::vector<u8>> readFile(const std::filesystem::path& path);
std::optional<std::string> readText(const std::filesystem::path& path);
bool writeFile(const std::filesystem::path& path, const void* data, std::size_t size);

/// Carpeta de datos de usuario de MC-WEB (ajustes, mundos). Se crea si no existe.
std::filesystem::path userDataDir();

/// Carpeta de instalación de Minecraft Java (.minecraft) si existe en esta máquina.
std::optional<std::filesystem::path> minecraftDir();

}  // namespace mcw::fs
