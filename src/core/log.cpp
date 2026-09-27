#include "core/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace mcw::log {
namespace {
std::atomic<Level> gMin{Level::Info};
std::mutex gMutex;
const auto gStart = std::chrono::steady_clock::now();
}  // namespace

void setMinLevel(Level level) { gMin = level; }

void write(Level level, std::string_view msg) {
  if (level < gMin.load()) return;
  static constexpr const char* kNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count();
  std::lock_guard lock(gMutex);
  std::FILE* out = level >= Level::Warn ? stderr : stdout;
  std::fprintf(out, "[%8.3f] %-5s %.*s\n", t, kNames[static_cast<int>(level)], static_cast<int>(msg.size()), msg.data());
  std::fflush(out);
}

}  // namespace mcw::log
