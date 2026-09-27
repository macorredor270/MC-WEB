#include "world/noise.h"

#include <cmath>
#include <numeric>

#include "core/random.h"

namespace mcw {
namespace {

inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline double lerp(double t, double a, double b) { return a + t * (b - a); }
inline double grad(int hash, double x, double y, double z) {
  const int h = hash & 15;
  const double u = h < 8 ? x : y;
  const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

}  // namespace

PerlinNoise::PerlinNoise(std::uint64_t seed) {
  Random rng(seed);
  std::array<std::uint8_t, 256> perm;
  std::iota(perm.begin(), perm.end(), 0);
  for (int i = 255; i > 0; i--) std::swap(perm[i], perm[rng.nextInt(i + 1)]);
  for (int i = 0; i < 512; i++) p_[i] = perm[i & 255];
  ox_ = rng.next() * 256.0;
  oy_ = rng.next() * 256.0;
  oz_ = rng.next() * 256.0;
}

double PerlinNoise::noise3(double x, double y, double z) const {
  x += ox_;
  y += oy_;
  z += oz_;
  const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const int X = static_cast<int>(fx) & 255, Y = static_cast<int>(fy) & 255, Z = static_cast<int>(fz) & 255;
  x -= fx;
  y -= fy;
  z -= fz;
  const double u = fade(x), v = fade(y), w = fade(z);
  const int A = p_[X] + Y, AA = p_[A] + Z, AB = p_[A + 1] + Z;
  const int B = p_[X + 1] + Y, BA = p_[B] + Z, BB = p_[B + 1] + Z;
  return lerp(w,
              lerp(v, lerp(u, grad(p_[AA], x, y, z), grad(p_[BA], x - 1, y, z)),
                   lerp(u, grad(p_[AB], x, y - 1, z), grad(p_[BB], x - 1, y - 1, z))),
              lerp(v, lerp(u, grad(p_[AA + 1], x, y, z - 1), grad(p_[BA + 1], x - 1, y, z - 1)),
                   lerp(u, grad(p_[AB + 1], x, y - 1, z - 1), grad(p_[BB + 1], x - 1, y - 1, z - 1))));
}

OctaveNoise::OctaveNoise(std::uint64_t seed, int octaves, double persistence, double lacunarity)
    : persistence_(persistence), lacunarity_(lacunarity) {
  std::uint64_t s = seed;
  double amp = 1, total = 0;
  for (int i = 0; i < octaves; i++) {
    layers_.emplace_back(splitmix64(s));
    total += amp;
    amp *= persistence;
  }
  norm_ = 1.0 / total;
}

double OctaveNoise::noise2(double x, double z) const {
  double sum = 0, amp = 1, freq = 1;
  for (const auto& l : layers_) {
    sum += l.noise2(x * freq, z * freq) * amp;
    amp *= persistence_;
    freq *= lacunarity_;
  }
  return sum * norm_;
}

double OctaveNoise::noise3(double x, double y, double z) const {
  double sum = 0, amp = 1, freq = 1;
  for (const auto& l : layers_) {
    sum += l.noise3(x * freq, y * freq, z * freq) * amp;
    amp *= persistence_;
    freq *= lacunarity_;
  }
  return sum * norm_;
}

}  // namespace mcw
