#include "client/font_blit.h"

#include <algorithm>

#include "core/font_map.h"

namespace mcw {

void BitmapFont::load(const Image& font) {
  font_ = font;
  cell_ = std::max(1, font.width / 16);
  for (int ch = 0; ch < 256; ch++) {
    const int cx = (ch % 16) * cell_, cy = (ch / 16) * cell_;
    int last = -1;
    for (int x = 0; x < cell_; x++)
      for (int y = 0; y < cell_; y++)
        if (cx + x < font.width && cy + y < font.height && (font.get(cx + x, cy + y) >> 24) > 0) last = std::max(last, x);
    widths_[ch] = ch == ' ' ? 4 : ((last + 1) * 8 + cell_ - 1) / cell_ + 1;
  }
}

int BitmapFont::width(std::string_view utf8) const {
  int w = 0;
  for (unsigned char c : utf8ToFont(utf8)) w += widths_[c];
  return w;
}

void BitmapFont::blit(Image& dst, int x, int y, std::string_view utf8, u32 rgb, int scale) const {
  if (!loaded()) return;
  int cx = x;
  for (unsigned char c : utf8ToFont(utf8)) {
    if (c != ' ') {
      const int gx = (c % 16) * cell_, gy = (c / 16) * cell_;
      for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
          // Un píxel de la fuente (8x8 por celda) puede ser varios si la fuente es de más resolución
          const int sx = gx + px * cell_ / 8, sy = gy + py * cell_ / 8;
          if (sx >= font_.width || sy >= font_.height || (font_.get(sx, sy) >> 24) < 128) continue;
          for (int oy = 0; oy < scale; oy++)
            for (int ox = 0; ox < scale; ox++) {
              const int dx = cx + px * scale + ox, dy = y + py * scale + oy;
              if (dx >= 0 && dy >= 0 && dx < dst.width && dy < dst.height) dst.set(dx, dy, 0xFF000000 | (rgb & 0xFFFFFF));
            }
        }
    }
    cx += widths_[c] * scale;
  }
}

}  // namespace mcw
