#include "client/ui.h"

#include <algorithm>

#include "assets/pack.h"
#include "client/shaders.h"

namespace mcw {

GLuint uploadUiTexture(const Image& img);

Ui::~Ui() {
  GLuint tex[] = {fontTex_, iconsTex_, whiteTex_};
  glDeleteTextures(3, tex);
  for (auto& [k, t] : textures_) glDeleteTextures(1, &t.id);
  GLuint bufs[] = {vbo_, ebo_};
  glDeleteBuffers(2, bufs);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
}

GLuint uploadUiTexture(const Image& img) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

void Ui::initGL(const PackStack& packs) {
  packs_ = &packs;
  program_ = gl::makeProgram(shaders::kUiVS, shaders::kUiFS, "ui");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  std::vector<u32> idx;
  for (u32 q = 0; q < 16384; q++) {
    const u32 b = q * 4;
    idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);

  // Fuente: rejilla de 16x16 celdas; el ancho de cada glifo es su última columna con píxeles + 1
  Image font = packs.readImage("assets/minecraft/textures/font/ascii.png").value_or(Image(128, 128, 0));
  fontCell_ = std::max(1, font.width / 16);
  for (int ch = 0; ch < 256; ch++) {
    const int cx = (ch % 16) * fontCell_, cy = (ch / 16) * fontCell_;
    int last = -1;
    for (int x = 0; x < fontCell_; x++)
      for (int y = 0; y < fontCell_; y++)
        if ((font.get(cx + x, cy + y) >> 24) > 0) last = std::max(last, x);
    const int w = last + 1;
    glyphWidth_[ch] = ch == ' ' ? 4 : (w * 8 + fontCell_ - 1) / fontCell_ + 1;
  }
  fontTex_ = uploadUiTexture(font);
  Image icons = packs.readImage("assets/minecraft/textures/gui/icons.png").value_or(Image(256, 256, 0));
  iconsSize_ = icons.width;
  iconsTex_ = uploadUiTexture(icons);
  whiteTex_ = uploadUiTexture(Image(1, 1, 0xFFFFFFFF));
}

int Ui::autoScale(int w, int h) {
  int s = 1;
  while (s < 4 && w / (s + 1) >= 320 && h / (s + 1) >= 240) s++;
  return s;
}

void Ui::begin(int screenW, int screenH, int guiScale) {
  screenW_ = std::max(1, screenW);
  screenH_ = std::max(1, screenH);
  scale_ = std::max(1, guiScale);
  verts_.clear();
  currentTex_ = 0;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(program_);
  glUniform2f(glGetUniformLocation(program_, "uScreen"), static_cast<float>(screenW_), static_cast<float>(screenH_));
  glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
}

void Ui::quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 argb) {
  if (tex != currentTex_) {
    flush();
    currentTex_ = tex;
  }
  const u8 r = static_cast<u8>(argb >> 16), g = static_cast<u8>(argb >> 8), b = static_cast<u8>(argb), a = static_cast<u8>(argb >> 24);
  const float s = static_cast<float>(scale_);
  verts_.push_back({x0 * s, y0 * s, u0, v0, r, g, b, a});
  verts_.push_back({x0 * s, y1 * s, u0, v1, r, g, b, a});
  verts_.push_back({x1 * s, y1 * s, u1, v1, r, g, b, a});
  verts_.push_back({x1 * s, y0 * s, u1, v0, r, g, b, a});
  if (verts_.size() >= 4 * 16000) flush();
}

int Ui::textWidth(std::string_view s) const {
  int w = 0;
  for (unsigned char c : s) w += glyphWidth_[c];
  return w;
}

int Ui::text(float x, float y, std::string_view s, u32 rgb, bool shadow) {
  auto draw = [&](float ox, float oy, u32 color) {
    float cx = x + ox;
    for (unsigned char c : s) {
      if (c != ' ') {
        const float u = (c % 16) / 16.0f, v = (c / 16) / 16.0f;
        quad(fontTex_, cx, y + oy, cx + 8, y + oy + 8, u, v, u + 1 / 16.0f, v + 1 / 16.0f, 0xFF000000 | color);
      }
      cx += glyphWidth_[c];
    }
  };
  if (shadow) {
    // La sombra es el mismo color a un cuarto de brillo, desplazada 1 píxel
    const u32 sh = ((rgb & 0xFCFCFC) >> 2);
    draw(1, 1, sh);
  }
  draw(0, 0, rgb);
  return textWidth(s);
}

void Ui::rect(float x, float y, float w, float h, u32 argb) { quad(whiteTex_, x, y, x + w, y + h, 0, 0, 1, 1, argb); }

void Ui::crosshair() {
  flush();
  // Mezcla invertida: el punto de mira siempre contrasta con el fondo
  inverted_ = true;
  glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
  const float cx = guiWidth() / 2.0f, cy = guiHeight() / 2.0f;
  const float t = 15.0f / iconsSize_;
  quad(iconsTex_, cx - 7.5f, cy - 7.5f, cx + 7.5f, cy + 7.5f, 0, 0, t, t, 0xFFFFFFFF);
  flush();
  inverted_ = false;
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Ui::flush() {
  if (verts_.empty()) return;
  // Otros renderers (iconos 3D) pueden haber cambiado el estado entre tandas: restaurarlo
  glUseProgram(program_);
  glUniform2f(glGetUniformLocation(program_, "uScreen"), static_cast<float>(screenW_), static_cast<float>(screenH_));
  glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  if (!inverted_) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, currentTex_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(verts_.size() / 4 * 6), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  verts_.clear();
}

void Ui::end() {
  flush();
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

}  // namespace mcw

namespace mcw {

const Ui::Tex& Ui::texture(const std::string& path) {
  auto it = textures_.find(path);
  if (it != textures_.end()) return it->second;
  Tex t;
  Image img = packs_ ? packs_->readImage("assets/minecraft/textures/" + path).value_or(Image(256, 256, 0)) : Image(256, 256, 0);
  t.id = uploadUiTexture(img);
  t.w = img.width;
  t.h = img.height;
  return textures_.emplace(path, t).first->second;
}

void Ui::sprite(const std::string& path, float x, float y, float w, float h, float u, float v, float uw, float vh, u32 argb) {
  const Tex& t = texture(path);
  quad(t.id, x, y, x + w, y + h, u / 256.0f, v / 256.0f, (u + uw) / 256.0f, (v + vh) / 256.0f, argb);
}

bool Ui::button(float x, float y, float w, std::string_view label, float mx, float my, bool enabled) {
  const bool hover = enabled && mx >= x && mx < x + w && my >= y && my < y + 20;
  const float v = !enabled ? 46.0f : (hover ? 86.0f : 66.0f);
  // Mitad izquierda y derecha de la textura de 200 px, para cualquier ancho
  sprite("gui/widgets.png", x, y, w / 2, 20, 0, v, w / 2, 20);
  sprite("gui/widgets.png", x + w / 2, y, w / 2, 20, 200 - w / 2, v, w / 2, 20);
  textCentered(x + w / 2, y + 6, label, !enabled ? 0xA0A0A0 : (hover ? 0xFFFFA0 : 0xE0E0E0));
  return hover;
}

}  // namespace mcw
