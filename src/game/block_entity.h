#pragma once
// Bloques con datos propios que se dibujan aparte (como las "tile entities" de 1.8): carteles con texto, estandartes con
// color y dibujos, y cabezas. Los cofres, hornos... tienen sus propios mapas en la partida.
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "core/types.h"

namespace mcw {

constexpr int kSignBlock = 63, kWallSignBlock = 68, kStandingBannerBlock = 176, kWallBannerBlock = 177, kSkullBlock = 144;

inline bool isSignBlock(int id) { return id == kSignBlock || id == kWallSignBlock; }
inline bool isBannerBlock(int id) { return id == kStandingBannerBlock || id == kWallBannerBlock; }

struct SignText {
  std::array<std::string, 4> lines;
  bool empty() const {
    for (const std::string& l : lines)
      if (!l.empty()) return false;
    return true;
  }
  bool operator==(const SignText&) const = default;
};

/// Un dibujo del estandarte: código de 1.8 ("bs", "cr"...) y color de tinte (0 negro ... 15 blanco, como los tintes).
struct BannerPattern {
  std::string code;
  u8 color = 0;
  bool operator==(const BannerPattern&) const = default;
};

struct BannerData {
  u8 base = 15;  // color de la tela (0 negro ... 15 blanco: el mismo número que el daño del objeto)
  std::vector<BannerPattern> patterns;
  bool operator==(const BannerData&) const = default;
};

/// Tipos de cabeza (el daño del objeto): 0 esqueleto, 1 esqueleto atrofiado, 2 zombi, 3 jugador, 4 creeper.
struct SkullData {
  u8 type = 0;
  u8 rot = 0;  // en el suelo: 0..15 pasos de 22,5º (como la rotación del cartel)
  std::string owner;
  bool operator==(const SkullData&) const = default;
};

using TilePos = std::tuple<int, int, int>;

struct TileEntities {
  std::map<TilePos, SignText> signs;
  std::map<TilePos, BannerData> banners;
  std::map<TilePos, SkullData> skulls;

  bool any() const { return !signs.empty() || !banners.empty() || !skulls.empty(); }
  void clear() { signs.clear(); banners.clear(); skulls.clear(); }
  /// Quita los datos de `p` que ya no corresponden al bloque que hay (por si cambió).
  void forget(const TilePos& p) {
    signs.erase(p);
    banners.erase(p);
    skulls.erase(p);
  }
  /// Tras poner el bloque `id` en `p`: quita lo que sobre.
  void blockSet(const TilePos& p, int id) {
    if (!isSignBlock(id)) signs.erase(p);
    if (!isBannerBlock(id)) banners.erase(p);
    if (id != kSkullBlock) skulls.erase(p);
  }
};

/// Los 38 dibujos de estandarte de 1.8: código, nombre de la textura (entity/banner/<nombre>.png) y si se saca con un objeto
/// especial (cabeza de wither, manzana de oro encantada...) en vez de con tinte y una forma.
struct BannerPatternInfo {
  const char* code;
  const char* texture;
};
const std::vector<BannerPatternInfo>& bannerPatterns();
/// Textura de un dibujo por su código ("" si no existe).
std::string bannerPatternTexture(const std::string& code);

/// Texto de una línea de cartel como lo guarda 1.8 (un texto de chat en JSON: {"text":"..."}) y al revés. Al leer acepta
/// un texto suelto, {"text":...} y las listas "extra" (se juntan sus trozos, sin colores).
std::string signLineToJson(const std::string& line);
std::string signLineFromJson(const std::string& json);

/// Rotación en el suelo (0..15) de algo que mira al jugador con ese yaw (radianes del juego: 0 = norte).
int signRotationFor(float yaw);

}  // namespace mcw
