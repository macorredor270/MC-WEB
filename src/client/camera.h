#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace mcw {

/// Cámara en primera persona. Se renderiza con la cámara en el origen (coordenadas relativas),
/// así no hay problemas de precisión lejos del spawn.
struct Camera {
  glm::dvec3 pos{0, 80, 0};
  float yaw = 0;    // radianes; 0 = mirando al norte (-Z), positivo gira a la izquierda
  float pitch = 0;  // radianes; positivo mira arriba
  float fovDeg = 70;
  float nearPlane = 0.05f;
  float farPlane = 1000;

  glm::mat4 proj{1}, view{1}, viewProj{1};

  glm::vec3 forward() const {
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
  }

  void update(int width, int height) {
    proj = glm::perspective(glm::radians(fovDeg), static_cast<float>(width) / std::max(1, height), nearPlane, farPlane);
    view = glm::lookAt(glm::vec3(0), forward(), glm::vec3(0, 1, 0));
    viewProj = proj * view;
  }

  /// Dirección cardinal como en la pantalla de depuración de Minecraft.
  std::string facing() const {
    const glm::vec3 f = forward();
    if (std::abs(f.x) > std::abs(f.z)) return f.x > 0 ? "este (+X)" : "oeste (-X)";
    return f.z > 0 ? "sur (+Z)" : "norte (-Z)";
  }
};

}  // namespace mcw
