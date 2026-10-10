#include "net/official_assets.h"

#include <nlohmann/json.hpp>

#include "core/fs.h"
#include "core/hash.h"
#include "core/log.h"
#include "net/https.h"

namespace mcw::net {
namespace {
constexpr const char* kManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr const char* kVersion = "1.8.8";

std::string hexOf(const std::array<u8, 20>& h) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (u8 b : h) {
    s += d[b >> 4];
    s += d[b & 15];
  }
  return s;
}
}  // namespace

std::string officialVersionJson(std::string* error) {
  const auto cache = officialAssetsDir() / "1.8.8.json";
  if (auto data = fs::readFile(cache)) return std::string(data->begin(), data->end());
  std::vector<u8> body;
  std::string err;
  if (!httpsGet(kManifest, body, &err, 15)) { if (error) *error = err; return {}; }
  std::string versionUrl;
  try {
    const auto manifest = nlohmann::json::parse(body.begin(), body.end());
    for (const auto& v : manifest.at("versions"))
      if (v.at("id") == kVersion) versionUrl = v.at("url");
  } catch (...) {}
  if (versionUrl.empty() || !httpsGet(versionUrl, body, &err, 15)) { if (error) *error = err.empty() ? "sin la versión 1.8.8" : err; return {}; }
  std::error_code ec;
  std::filesystem::create_directories(officialAssetsDir(), ec);
  fs::writeFile(cache, body.data(), body.size());
  return std::string(body.begin(), body.end());
}

std::filesystem::path officialAssetsDir() { return fs::userDataDir() / "assets"; }

std::optional<std::filesystem::path> downloadedJar() {
  const auto jar = officialAssetsDir() / (std::string(kVersion) + ".jar");
  std::error_code ec;
  if (std::filesystem::is_regular_file(jar, ec) && std::filesystem::file_size(jar, ec) > 1000000) return jar;
  return std::nullopt;
}

std::optional<std::filesystem::path> downloadOfficialJar(std::string* error, const std::function<bool(std::size_t, std::size_t)>& progress) {
  if (auto have = downloadedJar()) return have;
  auto fail = [&](const std::string& why) -> std::optional<std::filesystem::path> {
    if (error) *error = why;
    return std::nullopt;
  };
  if (!httpsAvailable()) return fail("esta versión no sabe descargar");
  std::vector<u8> body;
  std::string err;
  if (!httpsGet(kManifest, body, &err, 15)) return fail("lista de versiones: " + err);
  std::string versionUrl;
  try {
    const auto manifest = nlohmann::json::parse(body.begin(), body.end());
    for (const auto& v : manifest.at("versions"))
      if (v.at("id") == kVersion) versionUrl = v.at("url");
  } catch (const std::exception& e) {
    return fail(std::string("lista de versiones ilegible: ") + e.what());
  }
  if (versionUrl.empty()) return fail("Mojang ya no lista la 1.8.8");
  if (!httpsGet(versionUrl, body, &err, 15)) return fail("datos de la versión: " + err);
  std::string jarUrl, sha;
  try {
    const auto info = nlohmann::json::parse(body.begin(), body.end());
    jarUrl = info.at("downloads").at("client").at("url");
    sha = info.at("downloads").at("client").at("sha1");
  } catch (const std::exception& e) {
    return fail(std::string("datos de la versión ilegibles: ") + e.what());
  }
  log::info("descargando el cliente oficial 1.8.8 de Mojang...");
  if (!httpsGet(jarUrl, body, &err, 20, progress)) return fail("cliente: " + err);
  if (hexOf(sha1(body.data(), body.size())) != sha) return fail("la descarga no coincide con la huella SHA-1 que da Mojang");
  std::error_code ec;
  std::filesystem::create_directories(officialAssetsDir(), ec);
  const auto jar = officialAssetsDir() / (std::string(kVersion) + ".jar");
  const auto tmp = jar.string() + ".part";
  if (!fs::writeFile(tmp, body.data(), body.size())) return fail("no se pudo guardar en " + officialAssetsDir().string());
  std::filesystem::rename(tmp, jar, ec);
  if (ec) return fail("no se pudo guardar en " + officialAssetsDir().string());
  log::info("recursos oficiales guardados en {}", jar.string());
  return jar;
}

}  // namespace mcw::net
