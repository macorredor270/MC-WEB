#pragma once
// La fuente del juego sobre imágenes en memoria (el texto de los carteles va dentro de su textura).
#include <string>
#include <string_view>

#include "assets/image.h"

namespace mcw {

class BitmapFont {
 public:
  /// `ascii.png` (rejilla de 16x16 celdas, como en 1.8).
  void load(const Image& font);
  bool loaded() const { return cell_ > 0; }
  /// Ancho en píxeles de la fuente (como `Ui::textWidth`) de un texto UTF-8.
  int width(std::string_view utf8) const;
  /// Pinta el texto con su esquina de arriba a la izquierda en (x, y), un píxel de la fuente = `scale` píxeles de la imagen.
  void blit(Image& dst, int x, int y, std::string_view utf8, u32 rgb, int scale = 1) const;

 private:
  Image font_;
  int cell_ = 0;
  int widths_[256]{};
};

}  // namespace mcw
