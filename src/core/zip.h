#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Lector de archivos .zip/.jar (miniz). Se puede abrir desde disco o desde memoria.
class ZipArchive {
 public:
  static std::unique_ptr<ZipArchive> openFile(const std::string& path);
  static std::unique_ptr<ZipArchive> openMemory(std::vector<u8> data);
  ~ZipArchive();

  ZipArchive(const ZipArchive&) = delete;
  ZipArchive& operator=(const ZipArchive&) = delete;

  bool contains(const std::string& name) const;
  std::optional<std::vector<u8>> read(const std::string& name) const;
  /// Nombres de todos los archivos (no carpetas) que empiezan por `prefix`.
  std::vector<std::string> list(const std::string& prefix = "") const;

 private:
  ZipArchive();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Crea un .zip en memoria con esos archivos (nombre dentro del zip, contenido).
std::vector<u8> zipFiles(const std::vector<std::pair<std::string, std::vector<u8>>>& files);

/// zlib (compresión de chunks y del protocolo).
std::vector<u8> zlibCompress(const u8* data, std::size_t size, int level = 6);
std::optional<std::vector<u8>> zlibDecompress(const u8* data, std::size_t size, std::size_t expectedSize = 0);

/// gzip (level.dat, playerdata y otros .dat de los mundos de 1.8).
std::vector<u8> gzipCompress(const u8* data, std::size_t size, int level = 6);
std::optional<std::vector<u8>> gzipDecompress(const u8* data, std::size_t size);
bool isGzip(const u8* data, std::size_t size);

}  // namespace mcw
