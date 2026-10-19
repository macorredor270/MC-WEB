#pragma once
// Mapas (1.8): el mapa vacío se convierte en un mapa lleno al usarlo, y mientras se lleva en la mano se va dibujando lo que hay
// alrededor de quien lo lleva (128x128 píxeles; cada píxel son 1 << escala bloques). Los colores son los de la tabla pública de
// "Map item format" de minecraft.wiki: un byte = color base * 4 + sombra.
#include <array>
#include <vector>

#include "core/types.h"
#include "world/chunk.h"

namespace mcw {

class World;

constexpr int kMapSize = 128;
/// Marca de un mapa de "alejar" (receta de ocho papeles alrededor de un mapa): al estrenarse se crea el mapa de la escala siguiente.
constexpr int kMapZoomFlag = 0x4000;

struct MapData {
  int id = 0;
  int scale = 0;      // 0 a 4: cada píxel mide 1 << scale bloques
  int dimension = 0;  // en cuál se hizo
  int centerX = 0, centerZ = 0;
  std::array<u8, kMapSize * kMapSize> colors{};  // 0 = sin explorar
  /// Marcas encima del mapa (en red: dónde está quien lo lleva). x, z en medios píxeles desde el centro (-128 a 127); rot de 0 a 15.
  struct Marker {
    i8 x = 0, z = 0;
    u8 type = 0, rot = 0;
  };
  std::vector<Marker> markers;
  u32 version = 1;    // sube con cada cambio (para redibujarlo y mandarlo)
  bool dirty = false; // sin guardar
  /// Cuántos bloques mide de lado.
  int blocks() const { return kMapSize << scale; }
};

namespace maps {
/// Color base (0 = ninguno) con el que se pinta un bloque.
int baseColor(BlockState s);
/// RGB (0xRRGGBB) de un byte de color de mapa; 0 = sin explorar (transparente).
u32 rgb(u8 color);
/// El byte de color de la columna del mundo (x, z) mirada desde arriba, con su sombra por la pendiente respecto a la columna del norte.
/// 0 si no hay nada que pintar (fuera de lo cargado).
u8 pixel(const World& w, int x, int z, int scale);
/// Centro (x, z) del mapa de esa escala en el que está el punto: las cuadrículas de 128 << escala bloques.
void centerFor(double x, double z, int scale, int& cx, int& cz);
}  // namespace maps

}  // namespace mcw
