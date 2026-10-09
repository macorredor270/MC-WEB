#include "assets/skin.h"

#include <algorithm>

namespace mcw {
namespace {

/// Rectángulo en unidades de una skin de 64 píxeles de ancho.
struct Rect {
  int x, y, w, h;
};

/// Copia un rectángulo girándolo de izquierda a derecha (el espejo de un brazo o una pierna).
/// Todo está en unidades de 64 y se multiplica por `s` para las skins ampliadas.
void copyFlipped(Image& img, int s, const Rect& from, int dx, int dy) {
  for (int y = 0; y < from.h * s; y++)
    for (int x = 0; x < from.w * s; x++) {
      const int sx = from.x * s + (from.w * s - 1 - x), sy = from.y * s + y;
      img.set(dx * s + x, dy * s + y, img.get(sx, sy));
    }
}

/// Pasa la pierna o el brazo derecho (disposición antigua) a su sitio de la izquierda en la
/// disposición moderna. Cada cara se voltea, y la derecha y la izquierda cambian de sitio.
void mirrorLimb(Image& img, int s, int ox, int oy, int dx, int dy) {
  // ox, oy: esquina de la caja origen; dx, dy: la del destino. Caja de 4x12x4.
  copyFlipped(img, s, {ox + 4, oy, 4, 4}, dx + 4, dy);          // arriba
  copyFlipped(img, s, {ox + 8, oy, 4, 4}, dx + 8, dy);          // abajo
  copyFlipped(img, s, {ox, oy + 4, 4, 12}, dx + 8, dy + 4);     // derecha -> izquierda
  copyFlipped(img, s, {ox + 4, oy + 4, 4, 12}, dx + 4, dy + 4); // delante
  copyFlipped(img, s, {ox + 8, oy + 4, 4, 12}, dx, dy + 4);     // izquierda -> derecha
  copyFlipped(img, s, {ox + 12, oy + 4, 4, 12}, dx + 12, dy + 4);  // detrás
}

bool allTransparent(const Image& img, int s, const Rect& r) {
  for (int y = r.y * s; y < (r.y + r.h) * s; y++)
    for (int x = r.x * s; x < (r.x + r.w) * s; x++)
      if ((img.get(x, y) >> 24) != 0) return false;
  return true;
}

bool anyTransparent(const Image& img, int s, const Rect& r) {
  for (int y = r.y * s; y < (r.y + r.h) * s; y++)
    for (int x = r.x * s; x < (r.x + r.w) * s; x++)
      if ((img.get(x, y) >> 24) != 255) return true;
  return false;
}

void makeOpaque(Image& img, int s, const Rect& r) {
  for (int y = r.y * s; y < (r.y + r.h) * s; y++)
    for (int x = r.x * s; x < (r.x + r.w) * s; x++) img.set(x, y, img.get(x, y) | 0xFF000000u);
}

void clearRect(Image& img, int s, const Rect& r) {
  for (int y = r.y * s; y < (r.y + r.h) * s; y++)
    for (int x = r.x * s; x < (r.x + r.w) * s; x++) img.set(x, y, 0);
}

}  // namespace

bool validSkinSize(int w, int h) { return w == 64 && (h == 64 || h == 32); }

bool detectSlimSkin(const Image& raw) {
  if (raw.empty() || raw.width % 64 != 0) return false;
  const int s = raw.width / 64;
  if (raw.height != 64 * s) return false;  // las antiguas (64x32) no tienen brazo izquierdo propio
  // Con brazos de 3 píxeles, la columna 4 del brazo (la última de la cara de atrás) no se usa y
  // se deja transparente: x 54..55 del brazo derecho y x 46..47 del izquierdo, filas de la
  // cara lateral (20..31 y 52..63)
  return allTransparent(raw, s, {54, 20, 2, 12}) && allTransparent(raw, s, {46, 52, 2, 12});
}

std::optional<PreparedSkin> prepareSkin(const Image& raw, std::optional<bool> slim) {
  if (raw.empty() || raw.width % 64 != 0) return std::nullopt;
  const int s = raw.width / 64;
  const bool legacy = raw.height == 32 * s;
  if (!legacy && raw.height != 64 * s) return std::nullopt;

  PreparedSkin out;
  out.slim = slim ? *slim : detectSlimSkin(raw);
  // Disposición moderna: lo de arriba tal cual y, si era antigua, abajo las piernas y brazos izquierdos
  out.image = Image(64 * s, 64 * s, 0);
  for (int y = 0; y < raw.height; y++)
    for (int x = 0; x < raw.width; x++) out.image.set(x, y, raw.get(x, y));
  Image& img = out.image;
  if (legacy) {
    // Una capa de sombrero sin ningún píxel transparente es el fondo de una skin antigua que no
    // tenía sombrero: no se dibuja
    if (!anyTransparent(img, s, {32, 0, 32, 16})) clearRect(img, s, {32, 0, 32, 16});
    mirrorLimb(img, s, 0, 16, 16, 48);   // pierna izquierda
    mirrorLimb(img, s, 40, 16, 32, 48);  // brazo izquierdo
  }
  // Las capas de base no pueden ser transparentes (lo transparente se vería como un agujero)
  for (const Rect& r : {Rect{0, 0, 32, 16}, Rect{16, 16, 24, 16}, Rect{0, 16, 16, 16}, Rect{40, 16, 16, 16}, Rect{16, 48, 16, 16},
                        Rect{32, 48, 16, 16}})
    makeOpaque(img, s, r);
  return out;
}

bool defaultSkinSlim(std::string_view uuid) {
  // UUID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" -> 128 bits. Se hace el hash de siempre de un UUID
  // (las dos mitades de 64 bits con O exclusivo y, de eso, las dos mitades de 32 bits también) y
  // su paridad elige el modelo: par = Steve, impar = Alex
  u64 hi = 0, lo = 0;
  int digits = 0;
  for (char c : uuid) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;
    if (digits < 16) hi = hi << 4 | static_cast<u64>(v);
    else if (digits < 32) lo = lo << 4 | static_cast<u64>(v);
    digits++;
  }
  if (digits != 32) return false;
  const u64 x = hi ^ lo;
  const u32 hash = static_cast<u32>(x >> 32) ^ static_cast<u32>(x);
  return (hash & 1) != 0;
}

}  // namespace mcw
