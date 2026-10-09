#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
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
  /// Dibuja un trozo de una textura de GUI del pack ("gui/widgets.png"...). `u,v,uw,vh` en píxeles
  /// de una textura de 256x256 (se escala solo si el pack es HD). `w,h` en píxeles de GUI.
  void sprite(const std::string& texture, float x, float y, float w, float h, float u, float v, float uw, float vh,
              u32 argb = 0xFFFFFFFF);
  void sprite(const std::string& texture, float x, float y, float w, float h, float u, float v) {
    sprite(texture, x, y, w, h, u, v, w, h);
  }
  /// Botón de 20 px de alto con las texturas de widgets.png. Devuelve si el ratón está encima.
  bool button(float x, float y, float w, std::string_view label, float mouseX, float mouseY, bool enabled = true);
  int textCentered(float cx, float y, std::string_view s, u32 rgb = 0xFFFFFF, bool shadow = true) {
    return text(cx - textWidth(s) / 2.0f, y, s, rgb, shadow);
  }
  /// Dibuja lo pendiente (antes de pintar iconos 3D encima, por ejemplo).
  void flush();
  /// Texto con sombra. Devuelve el ancho en píxeles de GUI.
  int text(float x, float y, std::string_view s, u32 rgb = 0xFFFFFF, bool shadow = true);
  int textWidth(std::string_view s) const;
  /// Escritura rúnica de la mesa de encantamientos (textures/font/ascii_sga.png del pack, en minúsculas), sin
  /// sombra. Si el pack no la trae se escribe con la letra normal. Devuelve el ancho.
  int runeText(float x, float y, std::string_view s, u32 rgb);
  int runeWidth(std::string_view s) const;
  /// Texto a otra escala (títulos grandes), con el origen arriba a la izquierda.
  void textScaled(float x, float y, std::string_view s, float scale, u32 rgb = 0xFFFFFF, bool shadow = true);
  /// Una textura del pack repetida en mosaico (fondo de tierra de los menús). `tile` = tamaño de cada copia.
  void tiled(const std::string& texture, float x, float y, float w, float h, float tile, u32 argb = 0xFFFFFFFF);
  /// ¿Existe esta textura en los packs cargados?
  bool hasTexture(const std::string& texture) const;
  void rect(float x, float y, float w, float h, u32 argb);
  void crosshair();
  void end();

  int guiWidth() const { return screenW_ / scale_; }
  int screenWidth() const { return screenW_; }
  int screenHeight() const { return screenH_; }
  int scale() const { return scale_; }
  int guiHeight() const { return screenH_ / scale_; }
  static int autoScale(int w, int h);

 private:
  struct Vertex { float x, y, u, v; u8 r, g, b, a; };
  void quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 argb);
  struct Tex { GLuint id = 0; int w = 256, h = 256; };
  const Tex& texture(const std::string& path);

  GLuint program_ = 0, vao_ = 0, vbo_ = 0, ebo_ = 0;
  GLuint fontTex_ = 0, iconsTex_ = 0, whiteTex_ = 0, runeTex_ = 0;
  int fontCell_ = 8, iconsSize_ = 256;
  int glyphWidth_[256] = {};
  int runeGlyphWidth_[256] = {};
  std::vector<Vertex> verts_;
  GLuint currentTex_ = 0;
  int screenW_ = 1, screenH_ = 1, scale_ = 2;
  bool inverted_ = false;
  const PackStack* packs_ = nullptr;
  std::unordered_map<std::string, Tex> textures_;
};

}  // namespace mcw
