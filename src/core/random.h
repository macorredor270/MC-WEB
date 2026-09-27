#pragma once
#include <cstdint>
#include <string_view>

namespace mcw {

/// Hash entero rápido de 3 coordenadas (variantes por posición, decoraciones...).
inline std::uint32_t hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t salt = 0) {
  std::uint32_t h = static_cast<std::uint32_t>(x) * 0x27d4eb2du ^ static_cast<std::uint32_t>(y) * 0x165667b1u ^
                    static_cast<std::uint32_t>(z) * 0x9e3779b1u ^ salt;
  h = (h ^ (h >> 15)) * 0x85ebca6bu;
  h = (h ^ (h >> 13)) * 0xc2b2ae35u;
  return h ^ (h >> 16);
}

inline std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

/// PRNG determinista (xoshiro128**), sembrado con splitmix64.
class Random {
 public:
  explicit Random(std::uint64_t seed) {
    std::uint64_t s = seed;
    const std::uint64_t a = splitmix64(s), b = splitmix64(s);
    s_[0] = static_cast<std::uint32_t>(a);
    s_[1] = static_cast<std::uint32_t>(a >> 32);
    s_[2] = static_cast<std::uint32_t>(b);
    s_[3] = static_cast<std::uint32_t>(b >> 32);
    if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) s_[0] = 1;
  }

  std::uint32_t nextU32() {
    const std::uint32_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint32_t t = s_[1] << 9;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 11);
    return result;
  }

  /// [0, 1)
  double next() { return nextU32() * (1.0 / 4294967296.0); }
  float nextFloat() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }
  /// Entero en [0, n)
  int nextInt(int n) { return n <= 0 ? 0 : static_cast<int>((static_cast<std::uint64_t>(nextU32()) * static_cast<std::uint64_t>(n)) >> 32); }
  int range(int lo, int hiInclusive) { return lo + nextInt(hiInclusive - lo + 1); }
  bool chance(double p) { return next() < p; }

 private:
  static std::uint32_t rotl(std::uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
  std::uint32_t s_[4];
};

/// Semilla derivada para una celda del mundo (un chunk, una región...).
inline std::uint64_t cellSeed(std::uint64_t worldSeed, std::int32_t x, std::int32_t z, std::uint32_t salt) {
  std::uint64_t s = worldSeed ^ (static_cast<std::uint64_t>(hash3(x, static_cast<std::int32_t>(salt), z)) << 32) ^ hash3(z, x, static_cast<std::int32_t>(salt), 0x5bd1e995u);
  return splitmix64(s);
}

/// Semilla numérica a partir de un texto, como hace el menú de "crear mundo".
inline std::uint64_t seedFromString(std::string_view s) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}

}  // namespace mcw
