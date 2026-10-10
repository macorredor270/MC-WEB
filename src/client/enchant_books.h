#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace mcw {

class World;

/// El libro que flota sobre cada mesa de encantamientos: sube y baja, se abre y mira hacia quien se acerca (a menos de 3
/// bloques) y pasa páginas; si no hay nadie, se cierra y gira despacio.
struct BookPose {
  glm::dvec3 pos{0};   // sobre el centro de la mesa (ya con lo que sube y baja)
  float yaw = 0;       // hacia dónde mira, en radianes (0 = su cara hacia +Z)
  float open = 0;      // 0 cerrado .. 1 abierto del todo
  float flip = 0;      // 0..1: la página que se está pasando
  glm::vec3 light{1};  // luz del sitio (la pone quien dibuja)
};

/// Busca las mesas de encantamientos cerca del jugador y lleva la animación de cada libro. Sin GL: solo cuentas.
class EnchantBooks {
 public:
  /// Un paso de `dt` segundos: de vez en cuando busca mesas alrededor de `player` y mueve los libros.
  void update(const World& world, const glm::dvec3& player, double dt);
  const std::vector<BookPose>& poses() const { return poses_; }
  std::size_t count() const { return poses_.size(); }
  /// Fuerza una búsqueda ya (en la próxima `update`).
  void rescan() { sinceScan_ = 1e9; }

 private:
  struct State {
    glm::ivec3 table{0};
    float open = 0, yaw = 0, flip = 0, flipSpeed = 1.0f, phase = 0;
    int page = 0;
  };
  static std::uint64_t key(const glm::ivec3& p) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x)) << 40) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.z)) << 12) ^
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.y + 128));
  }
  void scan(const World& world, const glm::dvec3& player);

  std::unordered_map<std::uint64_t, State> states_;
  std::vector<BookPose> poses_;
  double sinceScan_ = 1e9, time_ = 0;
  std::uint32_t seed_ = 0x2545F491u;
};

}  // namespace mcw
