#include "core/png.h"

namespace mcw {

std::optional<std::pair<int, int>> pngSize(std::span<const u8> d) {
  // Firma (8 bytes) + trozo IHDR: longitud 13, "IHDR", ancho y alto en 4 bytes cada uno (big endian)
  static constexpr u8 kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (d.size() < 24) return std::nullopt;
  for (int i = 0; i < 8; i++)
    if (d[static_cast<std::size_t>(i)] != kSignature[i]) return std::nullopt;
  auto be32 = [&](std::size_t at) {
    return static_cast<u32>(d[at]) << 24 | static_cast<u32>(d[at + 1]) << 16 | static_cast<u32>(d[at + 2]) << 8 | d[at + 3];
  };
  if (be32(8) != 13 || d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R') return std::nullopt;
  const u32 w = be32(16), h = be32(20);
  if (w == 0 || h == 0 || w > 65535 || h > 65535) return std::nullopt;
  return std::pair<int, int>{static_cast<int>(w), static_cast<int>(h)};
}

}  // namespace mcw
