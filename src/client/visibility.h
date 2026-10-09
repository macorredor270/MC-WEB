#pragma once
// Oclusión del terreno por grafo de visibilidad: qué secciones (cubos de 16) se ven desde la cámara pasando por
// los huecos entre bloques. Sin GL ni SDL: se prueba sola.
//
// Cada sección guarda 15 bits, uno por cada par de caras distintas del cubo: el bit está puesto si dentro de la
// sección hay un camino por bloques que no tapan la vista entre esas dos caras. Desde la sección de la cámara se
// recorre a lo ancho (por capas de distancia): se pasa a la vecina solo si la sección actual conecta alguna cara
// por la que se entró con la de salida, y nunca se da un paso hacia la cámara (un rayo no vuelve atrás).
#include <array>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "core/face.h"
#include "core/types.h"

namespace mcw {

constexpr u16 kVisAll = 0x7FFF;  // todas las caras conectadas (aire, o sección que aún no se conoce)

/// Número del par de caras (a != b) entre 0 y 14.
constexpr int visPairIndex(int a, int b) {
  if (a > b) {
    const int t = a;
    a = b;
    b = t;
  }
  return a * (11 - a) / 2 + (b - a - 1);
}
constexpr u16 visPairBit(int a, int b) { return static_cast<u16>(1u << visPairIndex(a, b)); }

/// ¿Alguna de las caras de entrada (máscara de 6 bits) está conectada con la de salida `exit`?
constexpr bool visConnected(u16 vis, u8 incoming, int exit) {
  for (int f = 0; f < 6; f++)
    if ((incoming & (1u << f)) && f != exit && (vis & visPairBit(f, exit))) return true;
  return false;
}

/// Recorrido por capas desde la sección de la cámara. Reutiliza sus tablas entre frames.
class SectionTraversal {
 public:
  static constexpr int kSections = 16;  // secciones por columna (de abajo arriba)
  struct Info {
    bool exists = false;  // la columna está cargada
    u16 vis = kVisAll;
  };

  /// `cam`: sección de la cámara (x, y, z; la y se ajusta a 0..15). `radius`: chunks a cada lado.
  /// `info(cx, sy, cz)` da la sección; `inView(cx, sy, cz)` dice si entra en la pirámide de visión y en la distancia;
  /// `emit(cx, sy, cz)` se llama una vez por cada sección visible (la de la cámara, la primera).
  template <class InfoFn, class InViewFn, class EmitFn>
  void run(int camX, int camY, int camZ, int radius, InfoFn&& info, InViewFn&& inView, EmitFn&& emit) {
    const int d = 2 * radius + 1;
    const std::size_t total = static_cast<std::size_t>(d) * static_cast<std::size_t>(d) * kSections;
    if (stamp_.size() < total) {
      stamp_.assign(total, 0);
      incoming_.assign(total, 0);
      gen_ = 0;
    }
    if (++gen_ == 0) {  // (da la vuelta cada 4000 millones de frames: se empieza de cero)
      std::fill(stamp_.begin(), stamp_.end(), 0u);
      gen_ = 1;
    }
    const int cy = camY < 0 ? 0 : (camY >= kSections ? kSections - 1 : camY);
    auto index = [&](int x, int y, int z) {
      return (static_cast<std::size_t>(x - camX + radius) * static_cast<std::size_t>(d) + static_cast<std::size_t>(z - camZ + radius)) *
                 kSections +
             static_cast<std::size_t>(y);
    };
    queue_.clear();
    queue_.push_back({camX, cy, camZ});
    stamp_[index(camX, cy, camZ)] = gen_;
    incoming_[index(camX, cy, camZ)] = 0x3F;
    for (std::size_t head = 0; head < queue_.size(); head++) {
      const Node n = queue_[head];
      const bool start = head == 0;
      const u8 inc = incoming_[index(n.x, n.y, n.z)];
      const Info here = info(n.x, n.y, n.z);
      emit(n.x, n.y, n.z);
      for (int f = 0; f < 6; f++) {
        const auto& v = kFaceNormals[static_cast<std::size_t>(f)];
        // Nunca un paso hacia la cámara en ningún eje (en el eje de la cámara se puede ir a los dos lados)
        if ((n.x - camX) * v[0] < 0 || (n.y - cy) * v[1] < 0 || (n.z - camZ) * v[2] < 0) continue;
        const int nx = n.x + v[0], ny = n.y + v[1], nz = n.z + v[2];
        if (ny < 0 || ny >= kSections || std::abs(nx - camX) > radius || std::abs(nz - camZ) > radius) continue;
        if (!start && !visConnected(here.vis, inc, f)) continue;
        const std::size_t ni = index(nx, ny, nz);
        const u8 entry = static_cast<u8>(1u << (f ^ 1));  // se entra por la cara opuesta al paso
        if (stamp_[ni] == gen_) {
          incoming_[ni] = static_cast<u8>(incoming_[ni] | entry);
          continue;
        }
        if (!info(nx, ny, nz).exists || !inView(nx, ny, nz)) continue;
        stamp_[ni] = gen_;
        incoming_[ni] = entry;
        queue_.push_back({nx, ny, nz});
      }
    }
  }

 private:
  struct Node {
    int x, y, z;
  };
  std::vector<u32> stamp_;
  std::vector<u8> incoming_;
  std::vector<Node> queue_;
  u32 gen_ = 0;
};

}  // namespace mcw
