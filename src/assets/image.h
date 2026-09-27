#pragma once
#include <optional>
#include <span>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Imagen RGBA8 en memoria.
struct Image {
  int width = 0, height = 0;
  std::vector<u8> rgba;

  Image() = default;
  Image(int w, int h, u32 fill = 0) : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4) {
    for (int i = 0; i < w * h; i++) set(i % w, i / w, fill);
  }
  bool empty() const { return width == 0 || height == 0; }

  /// Color en formato 0xAARRGGBB
  u32 get(int x, int y) const {
    const u8* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
    return (u32(p[3]) << 24) | (u32(p[0]) << 16) | (u32(p[1]) << 8) | p[2];
  }
  void set(int x, int y, u32 argb) {
    u8* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
    p[0] = static_cast<u8>(argb >> 16);
    p[1] = static_cast<u8>(argb >> 8);
    p[2] = static_cast<u8>(argb);
    p[3] = static_cast<u8>(argb >> 24);
  }

  Image crop(int x, int y, int w, int h) const;
  /// Escala al tamaño pedido: vecino más cercano para ampliar, media (con alfa) para reducir.
  Image resized(int w, int h) const;
  /// Siguiente nivel de mipmap (mitad de tamaño), promediando el color de los píxeles no transparentes.
  Image halfMip() const;
};

std::optional<Image> decodePng(std::span<const u8> data);
bool writePng(const char* path, const Image& img);

}  // namespace mcw
