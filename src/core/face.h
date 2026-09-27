#pragma once
#include <array>
#include <string_view>

namespace mcw {

/// Caras de un bloque. Norte = -Z, Oeste = -X, como en Minecraft.
enum Face : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };

inline constexpr std::array<std::string_view, 6> kFaceNames = {"down", "up", "north", "south", "west", "east"};
inline constexpr std::array<std::array<int, 3>, 6> kFaceNormals = {{
    {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
}};
inline constexpr std::array<int, 6> kOppositeFace = {1, 0, 3, 2, 5, 4};

/// -1 si el nombre no es una cara.
inline int faceFromName(std::string_view name) {
  for (int i = 0; i < 6; i++)
    if (kFaceNames[i] == name) return i;
  return -1;
}

}  // namespace mcw
