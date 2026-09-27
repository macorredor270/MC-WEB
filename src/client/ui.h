#pragma once
#include <string_view>
#include <vector>

#include "client/gl.h"
#include "core/types.h"

namespace mcw {

class PackStack;

/// Dibujo 2D: texto con la fuente del pack (textures/font/ascii.png) y sprites de la GUI.
/// Coordenadas en "píxeles de GUI" (se multiplican por la escala de GUI, como en el juego).
class Ui {
 public:
  ~Ui();
  void initGL(const PackStack& packs);

  void begin(int screenW, int screenH, int guiScale);
  /// Texto con sombra. Devuelve el ancho en píxeles de GUI.
  int text(float x, float y, std::string_view s, u32 rgb = 0xFFFFFF, bool shadow = true);
  int textWidth(std::string_view s) const;
  void rect(float x, float y, float w, float h, u32 argb);
  void crosshair();
  void end();

  int guiWidth() const { return screenW_ / scale_; }
  int guiHeight() const { return screenH_ / scale_; }
  static int autoScale(int w, int h);

 private:
  struct Vertex { float x, y, u, v; u8 r, g, b, a; };
  void quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 argb);
  void flush();

  GLuint program_ = 0, vao_ = 0, vbo_ = 0, ebo_ = 0;
  GLuint fontTex_ = 0, iconsTex_ = 0, whiteTex_ = 0;
  int fontCell_ = 8, iconsSize_ = 256;
  int glyphWidth_[256] = {};
  std::vector<Vertex> verts_;
  GLuint currentTex_ = 0;
  int screenW_ = 1, screenH_ = 1, scale_ = 2;
};

}  // namespace mcw
