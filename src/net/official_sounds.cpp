#include "net/official_sounds.h"

#include <nlohmann/json.hpp>

#include "core/fs.h"
#include "core/log.h"
#include "net/https.h"
#include "net/official_assets.h"

namespace mcw::net {
namespace {

std::filesystem::path objectPath(const std::string& hash) { return officialAssetsDir() / "objects" / hash.substr(0, 2) / hash; }

/// El objeto (por su huella) del servidor de recursos, o el guardado. nullopt si no se puede.
std::optional<std::vector<u8>> fetchObject(const std::string& hash, bool allowDownload) {
  const auto path = objectPath(hash);
  if (auto cached = fs::readFile(path)) return cached;
  if (!allowDownload) return std::nullopt;
  std::vector<u8> body;
  std::string err;
  if (!httpsGet("https://resources.download.minecraft.net/" + hash.substr(0, 2) + "/" + hash, body, &err, 10)) return std::nullopt;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  fs::writeFile(path, body.data(), body.size());
  return body;
}

}  // namespace

std::map<std::string, std::vector<std::vector<u8>>> loadOfficialSounds(const std::vector<std::string>& events, const std::atomic<bool>& cancel) {
  std::map<std::string, std::vector<std::vector<u8>>> out;
  // Índice de recursos (nombre -> huella): guardado la primera vez
  const auto indexPath = officialAssetsDir() / "indexes" / "1.8.json";
  std::vector<u8> indexData;
  if (auto cached = fs::readFile(indexPath)) {
    indexData = std::move(*cached);
  } else {
    std::string err;
    const std::string version = officialVersionJson(&err);
    if (version.empty()) { log::warn("sonidos oficiales: sin datos de la versión ({})", err); return out; }
    std::string url;
    try {
      url = nlohmann::json::parse(version).at("assetIndex").at("url");
    } catch (...) { return out; }
    if (!httpsGet(url, indexData, &err, 15)) { log::warn("sonidos oficiales: sin índice de recursos ({})", err); return out; }
    std::error_code ec;
    std::filesystem::create_directories(indexPath.parent_path(), ec);
    fs::writeFile(indexPath, indexData.data(), indexData.size());
  }
  nlohmann::json index;
  try {
    index = nlohmann::json::parse(indexData.begin(), indexData.end()).at("objects");
  } catch (...) { return out; }
  auto hashOf = [&](const std::string& name) -> std::string {
    const auto it = index.find(name);
    return it == index.end() ? std::string() : it->at("hash").get<std::string>();
  };
  // sounds.json: de evento a archivos
  const auto soundsHash = hashOf("minecraft/sounds.json");
  const auto soundsJson = soundsHash.empty() ? std::nullopt : fetchObject(soundsHash, true);
  if (!soundsJson) { log::warn("sonidos oficiales: sin sounds.json"); return out; }
  nlohmann::json events_;
  try {
    events_ = nlohmann::json::parse(soundsJson->begin(), soundsJson->end());
  } catch (...) { return out; }
  for (const std::string& event : events) {
    const auto ev = events_.find(event);
    if (ev == events_.end() || !ev->contains("sounds")) continue;
    for (const auto& entry : ev->at("sounds")) {
      if (cancel) return out;
      const std::string file = entry.is_string() ? entry.get<std::string>() : entry.value("name", "");
      const std::string hash = hashOf("minecraft/sounds/" + file + ".ogg");
      if (hash.empty()) continue;
      if (auto data = fetchObject(hash, true)) out[event].push_back(std::move(*data));
    }
  }
  log::info("sonidos oficiales: {} eventos listos", out.size());
  return out;
}

}  // namespace mcw::net
