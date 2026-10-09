#include "client/skin_store.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

#include "assets/pack.h"
#include "core/fs.h"
#include "core/log.h"

namespace mcw {
namespace {

namespace stdfs = std::filesystem;

constexpr std::size_t kMaxUploadBytes = 256 * 1024;  // una skin de 64x64 pesa unos pocos KB
constexpr const char* kFilePrefix = "file:";

/// Qué skins subidas llevan brazos finos: skins.json = {"nombre": {"slim": true}, ...}
nlohmann::json readIndex() {
  auto text = fs::readText(SkinStore::dir() / "skins.json");
  if (!text) return nlohmann::json::object();
  auto j = nlohmann::json::parse(*text, nullptr, false);
  return j.is_object() ? j : nlohmann::json::object();
}

void writeIndex(const nlohmann::json& j) {
  const std::string text = j.dump(2);
  fs::writeFile(SkinStore::dir() / "skins.json", text.data(), text.size());
}

/// Nombre válido para un archivo: letras, números, espacio, guion y guion bajo, y no muy largo.
std::string cleanName(const std::string& in) {
  std::string out;
  for (unsigned char c : in) {
    if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out += static_cast<char>(c);
    else if (c >= 0x80) out += '_';  // letras con tilde y demás: nombres de archivo sin sorpresas
    if (out.size() >= 24) break;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  return out.empty() ? "skin" : out;
}

std::string lowerCase(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

stdfs::path SkinStore::dir() {
  const stdfs::path p = fs::userDataDir() / "skins";
  std::error_code ec;
  stdfs::create_directories(p, ec);
  return p;
}

std::vector<SkinEntry> SkinStore::list() const {
  std::vector<SkinEntry> out;
  out.push_back({"steve", "Steve", true, false});
  out.push_back({"alex", "Alex", true, true});
  const nlohmann::json index = readIndex();
  std::vector<SkinEntry> mine;
  std::error_code ec;
  for (const auto& e : stdfs::directory_iterator(dir(), ec)) {
    if (!e.is_regular_file(ec) || lowerCase(e.path().extension().string()) != ".png") continue;
    SkinEntry s;
    s.name = e.path().stem().string();
    s.id = kFilePrefix + s.name;
    if (index.contains(s.name) && index[s.name].is_object()) {
      s.slim = index[s.name].value("slim", false);
    } else if (auto data = fs::readFile(e.path())) {
      // Una skin copiada a mano a la carpeta: se mira si lleva brazos finos
      if (auto img = decodePng(*data)) s.slim = detectSlimSkin(*img);
    }
    mine.push_back(std::move(s));
  }
  std::sort(mine.begin(), mine.end(), [](const SkinEntry& a, const SkinEntry& b) { return lowerCase(a.name) < lowerCase(b.name); });
  out.insert(out.end(), mine.begin(), mine.end());
  return out;
}

std::optional<SkinEntry> SkinStore::find(const std::string& id) const {
  for (const SkinEntry& e : list())
    if (e.id == id) return e;
  return std::nullopt;
}

SkinStore::ImportResult SkinStore::import(std::span<const u8> png, const std::string& suggestedName) {
  ImportResult r;
  if (png.size() > kMaxUploadBytes) {
    r.error = "El archivo es demasiado grande para ser una skin";
    return r;
  }
  const auto img = decodePng(png);
  if (!img) {
    r.error = "No es un PNG válido";
    return r;
  }
  if (!validSkinSize(img->width, img->height)) {
    r.error = "La skin tiene que ser un PNG de 64x64 o de 64x32 píxeles (esta es de " + std::to_string(img->width) + "x" +
              std::to_string(img->height) + ")";
    return r;
  }
  // Nombre libre: "Mi skin", "Mi skin-2"...
  const std::string base = cleanName(suggestedName);
  std::string name = base;
  for (int n = 2; stdfs::exists(dir() / (name + ".png")); n++) name = base + "-" + std::to_string(n);
  if (!fs::writeFile(dir() / (name + ".png"), png.data(), png.size())) {
    r.error = "No se pudo guardar la skin";
    return r;
  }
  nlohmann::json index = readIndex();
  index[name] = {{"slim", detectSlimSkin(*img)}};
  writeIndex(index);
  r.id = kFilePrefix + name;
  log::info("skin importada: {} ({}x{})", name, img->width, img->height);
  return r;
}

bool SkinStore::remove(const std::string& id) {
  if (id.rfind(kFilePrefix, 0) != 0) return false;
  const std::string name = id.substr(std::string(kFilePrefix).size());
  std::error_code ec;
  const bool gone = stdfs::remove(dir() / (name + ".png"), ec);
  nlohmann::json index = readIndex();
  if (index.erase(name) > 0) writeIndex(index);
  return gone;
}

bool SkinStore::setSlim(const std::string& id, bool slim) {
  if (id.rfind(kFilePrefix, 0) != 0) return false;
  const std::string name = id.substr(std::string(kFilePrefix).size());
  if (!stdfs::exists(dir() / (name + ".png"))) return false;
  nlohmann::json index = readIndex();
  index[name] = {{"slim", slim}};
  writeIndex(index);
  return true;
}

std::optional<PreparedSkin> SkinStore::load(const SkinEntry& entry, const PackStack& packs) const {
  if (entry.builtin) {
    auto img = packs.readImage(std::string("assets/minecraft/textures/entity/") + (entry.slim ? "alex.png" : "steve.png"));
    return img ? prepareSkin(*img, entry.slim) : std::nullopt;
  }
  const std::string name = entry.id.substr(std::string(kFilePrefix).size());
  auto data = fs::readFile(dir() / (name + ".png"));
  if (!data) return std::nullopt;
  auto img = decodePng(*data);
  return img ? prepareSkin(*img, entry.slim) : std::nullopt;
}

std::vector<u8> skinToNetworkPng(const PreparedSkin& skin) {
  if (skin.image.width == 64) return encodePng(skin.image);
  return encodePng(skin.image.resized(64, 64));
}

}  // namespace mcw
