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

/// La semilla que da un texto que no es un número, igual que en el juego: el `String.hashCode()` de Java (31*h + unidad UTF-16).
inline std::uint64_t javaStringSeed(std::string_view s) {
  std::uint32_t h = 0;
  auto unit = [&h](std::uint32_t u) { h = 31u * h + u; };
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
    std::uint32_t cp = extra == 0 ? c : (c & (0x3Fu >> extra));
    for (int k = 1; k <= extra && i + static_cast<std::size_t>(k) < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3Fu);
    i += static_cast<std::size_t>(extra) + 1;
    if (cp >= 0x10000) {  // par sustituto
      cp -= 0x10000;
      unit(0xD800u + (cp >> 10));
      unit(0xDC00u + (cp & 0x3FFu));
    } else {
      unit(cp);
    }
  }
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(h)));
}

/// Semilla numérica a partir de un texto (un hash propio; para texturas, pruebas...). Los mundos usan `javaStringSeed`.
inline std::uint64_t seedFromString(std::string_view s) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}

}  // namespace mcw
