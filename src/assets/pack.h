#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "assets/image.h"
#include "core/types.h"

namespace mcw {

class ZipArchive;

/// Origen de assets con la estructura de un resource pack (`assets/minecraft/...`).
class Pack {
 public:
  virtual ~Pack() = default;
  virtual std::string name() const = 0;
  virtual std::optional<std::vector<u8>> read(const std::string& path) const = 0;
  virtual bool exists(const std::string& path) const { return read(path).has_value(); }
  virtual std::optional<Image> readImage(const std::string& path) const;
};

/// Un .jar del juego o un resource pack en .zip.
class ZipPack final : public Pack {
 public:
  static std::unique_ptr<ZipPack> open(const std::filesystem::path& path);
  static std::unique_ptr<ZipPack> fromMemory(std::string name, std::vector<u8> data);
  ~ZipPack() override;
  std::string name() const override { return name_; }
  std::optional<std::vector<u8>> read(const std::string& path) const override;
  bool exists(const std::string& path) const override;

 private:
  ZipPack(std::string name, std::unique_ptr<ZipArchive> zip);
  std::string name_;
  std::unique_ptr<ZipArchive> zip_;
};

/// Un resource pack descomprimido en una carpeta.
class DirPack final : public Pack {
 public:
  explicit DirPack(std::filesystem::path root) : root_(std::move(root)) {}
  std::string name() const override { return root_.filename().string(); }
  std::optional<std::vector<u8>> read(const std::string& path) const override;
  bool exists(const std::string& path) const override;

 private:
  std::filesystem::path root_;
};

/// Pack en memoria (el pack CC0 generado por código). Las imágenes se guardan sin codificar.
class MemoryPack final : public Pack {
 public:
  explicit MemoryPack(std::string name) : name_(std::move(name)) {}
  std::string name() const override { return name_; }
  std::optional<std::vector<u8>> read(const std::string& path) const override;
  bool exists(const std::string& path) const override { return files_.count(path) || images_.count(path); }
  std::optional<Image> readImage(const std::string& path) const override;

  void putText(const std::string& path, const std::string& text) { files_[path] = std::vector<u8>(text.begin(), text.end()); }
  void putJson(const std::string& path, const nlohmann::json& j) { putText(path, j.dump()); }
  void putImage(const std::string& path, Image img) { images_[path] = std::move(img); }

 private:
  std::string name_;
  std::map<std::string, std::vector<u8>> files_;
  std::map<std::string, Image> images_;
};

/// Pila de packs: el primero que tenga el archivo gana (como la lista de resource packs del juego).
class PackStack {
 public:
  void pushTop(std::shared_ptr<const Pack> p) { packs_.insert(packs_.begin(), std::move(p)); }
  void pushBottom(std::shared_ptr<const Pack> p) { packs_.push_back(std::move(p)); }
  const std::vector<std::shared_ptr<const Pack>>& packs() const { return packs_; }

  std::optional<std::vector<u8>> read(const std::string& path) const;
  std::optional<Image> readImage(const std::string& path) const;
  std::optional<nlohmann::json> readJson(const std::string& path) const;
  bool exists(const std::string& path) const;
  /// Nombre del pack principal (el de arriba) para mostrarlo en pantalla.
  std::string description() const;

 private:
  std::vector<std::shared_ptr<const Pack>> packs_;
};

/// Busca un cliente 1.8.x instalado (`.minecraft/versions/1.8.*/1.8.*.jar`), prefiriendo 1.8.8.
std::optional<std::filesystem::path> findMinecraftJar();

}  // namespace mcw
