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
std::vector<std::string> ZipPack::list(const std::string& prefix) const { return zip_->list(prefix); }

std::optional<std::vector<u8>> DirPack::read(const std::string& path) const { return fs::readFile(root_ / path); }
bool DirPack::exists(const std::string& path) const {
  std::error_code ec;
  return std::filesystem::is_regular_file(root_ / path, ec);
}

std::vector<std::string> DirPack::list(const std::string& prefix) const {
  std::vector<std::string> out;
  std::error_code ec;
  // El prefijo es una carpeta ("assets/minecraft/models/"): se recorre entera
  const std::filesystem::path base = root_ / prefix;
  for (auto it = std::filesystem::recursive_directory_iterator(base, ec); !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    out.push_back(std::filesystem::relative(it->path(), root_, ec).generic_string());
  }
  return out;
}

std::optional<std::vector<u8>> MemoryPack::read(const std::string& path) const {
  auto it = files_.find(path);
  if (it == files_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::string> MemoryPack::list(const std::string& prefix) const {
  std::vector<std::string> out;
  for (auto it = files_.lower_bound(prefix); it != files_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
    out.push_back(it->first);
  return out;
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

namespace {
constexpr u32 kBundleMagic = 0x4257434D;  // "MCWB"
void putU32(std::vector<u8>& out, u32 v) {
  for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(v >> (i * 8)));
}
bool getU32(const u8*& p, const u8* end, u32& v) {
  if (end - p < 4) return false;
  v = u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
  p += 4;
  return true;
}
}  // namespace

std::vector<u8> bundleModelFiles(const PackStack& packs, const Pack* skip) {
  // De abajo arriba: los packs de arriba sobrescriben
  std::map<std::string, std::vector<u8>> files;
  const auto& list = packs.packs();
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    if (it->get() == skip) continue;
    for (const char* prefix : {"assets/minecraft/blockstates/", "assets/minecraft/models/block/"})
      for (const std::string& path : (*it)->list(prefix))
        if (auto data = (*it)->read(path)) files[path] = std::move(*data);
  }
  std::vector<u8> out;
  putU32(out, kBundleMagic);
  putU32(out, static_cast<u32>(files.size()));
  for (const auto& [path, data] : files) {
    putU32(out, static_cast<u32>(path.size()));
    out.insert(out.end(), path.begin(), path.end());
    putU32(out, static_cast<u32>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
  }
  return out;
}

std::shared_ptr<MemoryPack> unbundlePack(const u8* data, std::size_t size, std::string name) {
  const u8* p = data;
  const u8* end = data + size;
  u32 magic = 0, count = 0;
  if (!getU32(p, end, magic) || magic != kBundleMagic || !getU32(p, end, count)) return nullptr;
  auto pack = std::make_shared<MemoryPack>(std::move(name));
  for (u32 i = 0; i < count; i++) {
    u32 n = 0;
    if (!getU32(p, end, n) || static_cast<std::size_t>(end - p) < n) return nullptr;
    std::string path(reinterpret_cast<const char*>(p), n);
    p += n;
    if (!getU32(p, end, n) || static_cast<std::size_t>(end - p) < n) return nullptr;
    pack->putBytes(path, std::vector<u8>(p, p + n));
    p += n;
  }
  return pack;
}

}  // namespace mcw
