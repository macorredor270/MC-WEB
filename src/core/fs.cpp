#include "core/fs.h"

#include <cstdlib>
#include <fstream>

namespace mcw::fs {
namespace stdfs = std::filesystem;

namespace {
[[maybe_unused]] std::optional<stdfs::path> envPath(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !*v) return std::nullopt;
  return stdfs::path(v);
}
}  // namespace

std::optional<std::vector<u8>> readFile(const stdfs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const auto size = in.tellg();
  if (size < 0) return std::nullopt;
  std::vector<u8> data(static_cast<std::size_t>(size));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
  return data;
}

std::optional<std::string> readText(const stdfs::path& path) {
  auto data = readFile(path);
  if (!data) return std::nullopt;
  return std::string(data->begin(), data->end());
}

bool writeFile(const stdfs::path& path, const void* data, std::size_t size) {
  std::error_code ec;
  if (path.has_parent_path()) stdfs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return static_cast<bool>(out);
}

stdfs::path userDataDir() {
  stdfs::path dir;
#if defined(__EMSCRIPTEN__)
  dir = "/persist";
#elif defined(_WIN32)
  dir = envPath("APPDATA").value_or(stdfs::current_path()) / "mcweb";
#elif defined(__APPLE__)
  dir = envPath("HOME").value_or(stdfs::current_path()) / "Library/Application Support/mcweb";
#else
  if (auto xdg = envPath("XDG_DATA_HOME")) dir = *xdg / "mcweb";
  else dir = envPath("HOME").value_or(stdfs::current_path()) / ".local/share/mcweb";
#endif
  std::error_code ec;
  stdfs::create_directories(dir, ec);
  return dir;
}

std::optional<stdfs::path> minecraftDir() {
  stdfs::path dir;
#if defined(__EMSCRIPTEN__)
  return std::nullopt;
#elif defined(_WIN32)
  auto appdata = envPath("APPDATA");
  if (!appdata) return std::nullopt;
  dir = *appdata / ".minecraft";
#elif defined(__APPLE__)
  auto home = envPath("HOME");
  if (!home) return std::nullopt;
  dir = *home / "Library/Application Support/minecraft";
#else
  auto home = envPath("HOME");
  if (!home) return std::nullopt;
  dir = *home / ".minecraft";
#endif
  std::error_code ec;
  if (stdfs::is_directory(dir, ec)) return dir;
  return std::nullopt;
}

}  // namespace mcw::fs
