#pragma once
// Fuente del juego: correspondencia entre caracteres Unicode y las celdas de font/ascii.png (rejilla de 16x16, como en 1.8).
// Sirve para pintar ñ, tildes, ¿ y ¡ con la fuente oficial y con la libre.
#include <string>
#include <string_view>

namespace mcw {

/// Celda de la fuente para un carácter Unicode, o -1 si no tiene (se pinta como '?').
inline int fontCellOf(char32_t cp) {
  if (cp < 0x80) return cp >= 0x20 ? static_cast<int>(cp) : -1;
  // 0x80..0xAF: Ç ü é â ä à å ç ê ë è ï î ì Ä Å  É æ Æ ô ö ò û ù ÿ Ö Ü ø £ Ø × ƒ  á í ó ú ñ Ñ ª º ¿ ® ¬ ½ ¼ ¡ « »
  static constexpr char32_t kHigh[48] = {
      0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
      0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xF8, 0xA3, 0xD8, 0xD7, 0x192,
      0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0xAE, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB};
  for (int i = 0; i < 48; i++)
    if (kHigh[i] == cp) return 0x80 + i;
  // Mayúsculas acentuadas que viven en las primeras celdas: À Á Â È Ê Ë Í Ó Ô Õ Ú
  static constexpr char32_t kLow[11] = {0xC0, 0xC1, 0xC2, 0xC8, 0xCA, 0xCB, 0xCD, 0xD3, 0xD4, 0xD5, 0xDA};
  for (int i = 0; i < 11; i++)
    if (kLow[i] == cp) return i;
  if (cp == 0xDF) return 11;  // ß
  return -1;
}

/// Pasa un texto UTF-8 a bytes que son directamente la celda de la fuente. Lo que no tiene celda sale como '?'.
inline std::string utf8ToFont(std::string_view s) {
  bool plain = true;
  for (unsigned char c : s) plain &= c < 0x80;
  if (plain) return std::string(s);
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = c;
    int extra = 0;
    if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
    else if (c >= 0x80) { cp = 0xFFFD; }
    i++;
    for (int k = 0; k < extra; k++) {
      if (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
      else { cp = 0xFFFD; break; }
    }
    const int cell = fontCellOf(cp);
    out += cell >= 0 ? static_cast<char>(cell) : '?';
  }
  return out;
}

}  // namespace mcw
