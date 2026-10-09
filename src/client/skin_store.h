#pragma once
// Skins del jugador guardadas en el equipo: las dos de serie (Steve y Alex, del paquete de
// texturas) y las que ha subido el jugador (carpeta skins/ de los datos de usuario).
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/skin.h"

namespace mcw {

class PackStack;

struct SkinEntry {
  std::string id;    // "steve", "alex" o "file:<nombre>"
  std::string name;  // como sale en la lista
  bool builtin = false;
  bool slim = false;
};

class SkinStore {
 public:
  /// Carpeta de las skins subidas (se crea si no existe).
  static std::filesystem::path dir();

  /// Steve, Alex y las subidas (por nombre).
  std::vector<SkinEntry> list() const;
  std::optional<SkinEntry> find(const std::string& id) const;

  struct ImportResult {
    std::string id;     // la entrada creada (vacío si falla)
    std::string error;  // por qué ha fallado (en español, para enseñar tal cual)
  };
  /// Guarda un PNG subido por el jugador. Solo vale 64x64 o 64x32; el modelo (brazos finos o
  /// anchos) se detecta y se puede cambiar luego.
  ImportResult import(std::span<const u8> png, const std::string& suggestedName);
  /// Borra una skin subida (las de serie no se pueden borrar).
  bool remove(const std::string& id);
  /// Cambia a brazos finos o anchos una skin subida.
  bool setSlim(const std::string& id, bool slim);

  /// La skin lista para dibujar. Las de serie salen del paquete de texturas.
  std::optional<PreparedSkin> load(const SkinEntry& entry, const PackStack& packs) const;
};

/// La skin como PNG de 64x64 para mandarla a otros jugadores (las ampliadas se reducen).
std::vector<u8> skinToNetworkPng(const PreparedSkin& skin);

}  // namespace mcw
