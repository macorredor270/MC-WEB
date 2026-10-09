#pragma once
// Skins de jugador en el formato de 1.8: PNG de 64x64 (o 64x32 en las antiguas) con la piel de
// cada parte y una segunda capa (sombrero, chaqueta, mangas y perneras) que se puede ocultar.
#include <optional>
#include <string_view>

#include "assets/image.h"

namespace mcw {

/// Partes de la skin que se pueden ocultar: los mismos bits que el byte "partes visibles" de 1.8
/// (Client Settings y metadatos de jugador).
enum SkinPart : u8 {
  kSkinCape = 0x01,
  kSkinJacket = 0x02,
  kSkinLeftSleeve = 0x04,
  kSkinRightSleeve = 0x08,
  kSkinLeftPants = 0x10,
  kSkinRightPants = 0x20,
  kSkinHat = 0x40,
};
inline constexpr u8 kAllSkinParts = 0x7F;

/// Lo que se acepta al subir una skin: 64x64 o 64x32.
bool validSkinSize(int width, int height);

/// Una skin lista para dibujar: siempre en la disposición moderna (cuadrada) y con las capas de
/// base opacas, y si lleva brazos finos (modelo "Alex") o anchos ("Steve").
struct PreparedSkin {
  Image image;
  bool slim = false;
};

/// Prepara una skin leída de un PNG. Acepta 64x64 y 64x32 y también esas medidas ampliadas
/// (múltiplos de 64, de los paquetes de recursos en alta resolución). `slim` fuerza el modelo; sin
/// él se detecta mirando las columnas que una skin de brazos finos deja transparentes.
/// nullopt si el tamaño no es el de una skin.
std::optional<PreparedSkin> prepareSkin(const Image& raw, std::optional<bool> slim = std::nullopt);

/// ¿Parece una skin de brazos finos? (solo en la disposición moderna; las antiguas nunca lo son).
bool detectSlimSkin(const Image& raw);

/// Skin que se ve por defecto según el UUID, como en 1.8: la mitad de los UUID son Steve y la otra
/// mitad Alex. Es lo que ve cualquiera de un jugador sin skin (por ejemplo en servidores offline).
bool defaultSkinSlim(std::string_view uuid);

}  // namespace mcw
