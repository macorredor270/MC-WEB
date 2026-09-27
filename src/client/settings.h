#pragma once
#include <string>
#include <string_view>

namespace mcw {

/// Opciones del jugador (pantalla "Opciones"). Se guardan entre partidas: en options.txt de la
/// carpeta de datos (nativo) o en el almacenamiento del navegador (web).
struct Settings {
  int renderDistance = 12;   // chunks
  float fov = 70.0f;         // grados (vertical, como en el juego)
  float brightness = 0.5f;   // 0 = oscuro, 1 = brillante
  float sensitivity = 0.5f;  // 0..1 (0,5 = normal)
  bool clouds = true;
  bool viewBobbing = true;
  bool showFps = false;
  int guiScale = 0;          // 0 = automático
  float volume = 1.0f;       // 0..1

  std::string serialize() const;
  void parse(std::string_view text);  // clave:valor por línea; lo desconocido se ignora

  /// Multiplicador de la sensibilidad (0,25x .. 2x).
  float sensitivityScale() const;

  static constexpr int kMinRenderDistance = 2, kMaxRenderDistance = 32;
};

/// Lee y guarda el texto de las opciones donde toque según la plataforma.
std::string loadSettingsText();
void saveSettingsText(const std::string& text);

}  // namespace mcw
