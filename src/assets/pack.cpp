#include "assets/pack.h"

#include <algorithm>

#include "core/fs.h"
#include "core/log.h"
#include "core/zip.h"

namespace mcw {

std::optional<Image> Pack::readImage(const std::string& path) const {
  auto data = read(path);
  if (!data) return std::nullopt;
  auto img = decodePng(*data);
  if (!img) log::warn("no se pudo decodificar {} del pack {}", path, name());
  return img;
}

ZipPack::ZipPack(std::string name, std::unique_ptr<ZipArchive> zip) : name_(std::move(name)), zip_(std::move(zip)) {}
ZipPack::~ZipPack() = default;

std::unique_ptr<ZipPack> ZipPack::open(const std::filesystem::path& path) {
  auto zip = ZipArchive::openFile(path.string());
  if (!zip) return nullptr;
  return std::unique_ptr<ZipPack>(new ZipPack(path.filename().string(), std::move(zip)));
}

std::unique_ptr<ZipPack> ZipPack::fromMemory(std::string name, std::vector<u8> data) {
  auto zip = ZipArchive::openMemory(std::move(data));
  if (!zip) return nullptr;
  return std::unique_ptr<ZipPack>(new ZipPack(std::move(name), std::move(zip)));
}

std::optional<std::vector<u8>> ZipPack::read(const std::string& path) const { return zip_->read(path); }
bool ZipPack::exists(const std::string& path) const { return zip_->contains(path); }

std::optional<std::vector<u8>> DirPack::read(const std::string& path) const { return fs::readFile(root_ / path); }
bool DirPack::exists(const std::string& path) const {
  std::error_code ec;
  return std::filesystem::is_regular_file(root_ / path, ec);
}

std::optional<std::vector<u8>> MemoryPack::read(const std::string& path) const {
  auto it = files_.find(path);
  if (it == files_.end()) return std::nullopt;
  return it->second;
}

std::optional<Image> MemoryPack::readImage(const std::string& path) const {
  auto it = images_.find(path);
  if (it != images_.end()) return it->second;
  return Pack::readImage(path);
}

std::optional<std::vector<u8>> PackStack::read(const std::string& path) const {
  for (const auto& p : packs_)
    if (auto d = p->read(path)) return d;
  return std::nullopt;
}

std::optional<Image> PackStack::readImage(const std::string& path) const {
  for (const auto& p : packs_)
    if (p->exists(path))
      if (auto img = p->readImage(path)) return img;
  return std::nullopt;
}

std::optional<nlohmann::json> PackStack::readJson(const std::string& path) const {
  auto data = read(path);
  if (!data) return std::nullopt;
  auto j = nlohmann::json::parse(data->begin(), data->end(), nullptr, false);
  if (j.is_discarded()) {
    log::warn("JSON inválido: {}", path);
    return std::nullopt;
  }
  return j;
}

bool PackStack::exists(const std::string& path) const {
  return std::any_of(packs_.begin(), packs_.end(), [&](const auto& p) { return p->exists(path); });
}

std::string PackStack::description() const { return packs_.empty() ? "(ninguno)" : packs_.front()->name(); }

std::optional<std::filesystem::path> findMinecraftJar() {
  auto dir = fs::minecraftDir();
  if (!dir) return std::nullopt;
  const char* versions[] = {"1.8.8", "1.8.9", "1.8.7", "1.8.6", "1.8.5", "1.8.4", "1.8.3", "1.8.2", "1.8.1", "1.8"};
  for (const char* v : versions) {
    auto jar = *dir / "versions" / v / (std::string(v) + ".jar");
    std::error_code ec;
    if (std::filesystem::is_regular_file(jar, ec)) return jar;
  }
  return std::nullopt;
}

}  // namespace mcw
