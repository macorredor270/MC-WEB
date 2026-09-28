#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Archivo de región Anvil (r.X.Z.mca): 32x32 chunks, cada uno comprimido con zlib en sectores de 4 KiB.
/// Formato público de los mundos de Minecraft; los mundos se pueden abrir en el juego original.
class RegionFile {
 public:
  explicit RegionFile(std::filesystem::path path);

  /// NBT del chunk (sin comprimir), o nada si no está guardado. `lx, lz` en 0..31.
  std::optional<std::vector<u8>> read(int lx, int lz);
  /// Guarda el NBT (sin comprimir) del chunk.
  bool write(int lx, int lz, const std::vector<u8>& nbt);
  bool has(int lx, int lz) const { return offsets_[lx + lz * 32] != 0; }

 private:
  bool open(bool create);
  void writeHeader(int index);

  std::filesystem::path path_;
  std::fstream file_;
  bool opened_ = false;
  std::array<u32, 1024> offsets_{};     // sector << 8 | número de sectores
  std::array<u32, 1024> timestamps_{};
  std::vector<bool> used_;              // sectores ocupados
};

/// Todas las regiones de una carpeta "region" (se abren al usarlas).
class RegionStore {
 public:
  explicit RegionStore(std::filesystem::path dir) : dir_(std::move(dir)) {}
  std::optional<std::vector<u8>> readChunk(int cx, int cz);
  bool writeChunk(int cx, int cz, const std::vector<u8>& nbt);
  bool hasChunk(int cx, int cz);

 private:
  RegionFile& region(int rx, int rz);
  std::filesystem::path dir_;
  std::unordered_map<i64, std::unique_ptr<RegionFile>> regions_;
};

}  // namespace mcw
