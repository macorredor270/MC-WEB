#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace mcw {

/// Ruido de gradiente (Perlin "mejorado") en 2D/3D con permutación derivada de una semilla.
/// Implementación propia del algoritmo público de Ken Perlin (2002).
class PerlinNoise {
 public:
  explicit PerlinNoise(std::uint64_t seed);
  /// Aproximadamente en [-1, 1].
  double noise3(double x, double y, double z) const;
  double noise2(double x, double z) const { return noise3(x, 0.0, z); }

 private:
  std::array<std::uint8_t, 512> p_{};
  double ox_, oy_, oz_;
};

/// Suma de octavas (fBm), normalizada para quedar aproximadamente en [-1, 1].
class OctaveNoise {
 public:
  OctaveNoise(std::uint64_t seed, int octaves, double persistence = 0.5, double lacunarity = 2.0);
  double noise2(double x, double z) const;
  double noise3(double x, double y, double z) const;

 private:
  std::vector<PerlinNoise> layers_;
  double persistence_, lacunarity_, norm_;
};

}  // namespace mcw
