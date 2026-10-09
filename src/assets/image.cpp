#include "assets/image.h"

#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace mcw {

std::optional<Image> decodePng(std::span<const u8> data) {
  int w = 0, h = 0, n = 0;
  stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &n, 4);
  if (!pixels) return std::nullopt;
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
  stbi_image_free(pixels);
  return img;
}

bool writePng(const char* path, const Image& img) {
  return stbi_write_png(path, img.width, img.height, 4, img.rgba.data(), img.width * 4) != 0;
}

std::vector<u8> encodePng(const Image& img) {
  std::vector<u8> out;
  if (img.empty()) return out;
  int len = 0;
  unsigned char* png = stbi_write_png_to_mem(img.rgba.data(), img.width * 4, img.width, img.height, 4, &len);
  if (!png) return out;
  out.assign(png, png + len);
  STBIW_FREE(png);
  return out;
}

Image Image::crop(int x, int y, int w, int h) const {
  Image out;
  out.width = w;
  out.height = h;
  out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
  for (int row = 0; row < h; row++) {
    const int sy = std::clamp(y + row, 0, height - 1);
    for (int col = 0; col < w; col++) {
      const int sx = std::clamp(x + col, 0, width - 1);
      std::memcpy(&out.rgba[(static_cast<std::size_t>(row) * w + col) * 4], &rgba[(static_cast<std::size_t>(sy) * width + sx) * 4], 4);
    }
  }
  return out;
}

Image Image::resized(int w, int h) const {
  if (w == width && h == height) return *this;
  Image out;
  out.width = w;
  out.height = h;
  out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
  if (w >= width && h >= height) {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        std::memcpy(&out.rgba[(static_cast<std::size_t>(y) * w + x) * 4],
                    &rgba[(static_cast<std::size_t>(y * height / h) * width + x * width / w) * 4], 4);
    return out;
  }
  // Reducción por caja con pesos de alfa (evita bordes negros en hojas y plantas)
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const int x0 = x * width / w, x1 = std::max(x0 + 1, (x + 1) * width / w);
      const int y0 = y * height / h, y1 = std::max(y0 + 1, (y + 1) * height / h);
      double r = 0, g = 0, b = 0, a = 0, cr = 0, cg = 0, cb = 0;
      int n = 0;
      for (int sy = y0; sy < y1; sy++)
        for (int sx = x0; sx < x1; sx++) {
          const u8* p = &rgba[(static_cast<std::size_t>(sy) * width + sx) * 4];
          const double al = p[3] / 255.0;
          r += p[0] * al; g += p[1] * al; b += p[2] * al; a += al;
          cr += p[0]; cg += p[1]; cb += p[2];
          n++;
        }
      u8* o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
      if (a > 0) { o[0] = static_cast<u8>(r / a); o[1] = static_cast<u8>(g / a); o[2] = static_cast<u8>(b / a); }
      else { o[0] = static_cast<u8>(cr / n); o[1] = static_cast<u8>(cg / n); o[2] = static_cast<u8>(cb / n); }
      o[3] = static_cast<u8>(std::clamp(a / n * 255.0 + 0.5, 0.0, 255.0));
    }
  }
  return out;
}

Image Image::halfMip() const { return resized(std::max(1, width / 2), std::max(1, height / 2)); }

}  // namespace mcw
