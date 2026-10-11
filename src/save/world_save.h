#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "save/nbt.h"
#include "save/region.h"

namespace mcw {

/// Lo que va en level.dat (formato de 1.8).
struct LevelInfo {
  std::string name = "Mundo nuevo";
  u64 seed = 0;
  int gameType = 0;          // 0 supervivencia, 1 creativo, 2 aventura, 3 espectador
  bool hardcore = false;
  int difficulty = 2;
  bool difficultyLocked = false;
  bool allowCommands = false;
  bool mapFeatures = true;   // generar estructuras
  bool bonusChest = false;
  std::string generator = "default";  // default, flat, largeBiomes, amplified
  std::string generatorOptions;       // superplano: "3;minecraft:bedrock,2*minecraft:dirt,minecraft:grass;1;village"
  i64 time = 0, dayTime = 1000;
  glm::ivec3 spawn{0, 64, 0};
  bool spawnSet = false;
  bool dragonKilled = false;  // el dragón del End ya ha muerto
  float dragonHealth = 200.0f;  // su vida al salir del End
  i64 lastPlayed = 0;
  bool raining = false;
  std::map<std::string, std::string> gameRules;
  std::optional<nbt::Value> player;  // jugador del modo un jugador

  std::string rule(const std::string& name, const std::string& def) const {
    auto it = gameRules.find(name);
    return it == gameRules.end() ? def : it->second;
  }
  bool ruleBool(const std::string& name, bool def) const { return rule(name, def ? "true" : "false") == "true"; }
};

/// Resumen para la lista de mundos.
struct WorldSummary {
  std::string folder;   // nombre de la carpeta en saves/
  std::string name;     // nombre del mundo
  i64 lastPlayed = 0;   // milisegundos desde 1970
  int gameType = 0;
  bool hardcore = false, allowCommands = false;
  std::uintmax_t sizeBytes = 0;
};

/// Un mundo guardado en disco: saves/<carpeta>/{level.dat, region/*.mca}.
class WorldSave {
 public:
  explicit WorldSave(std::filesystem::path dir);

  static std::filesystem::path savesDir();
  /// Mundos guardados, del más reciente al más antiguo.
  static std::vector<WorldSummary> list();
  /// Carpeta libre a partir del nombre ("Mundo nuevo", "Mundo nuevo-1"...).
  static std::string freeFolderName(const std::string& name);
  static bool remove(const std::string& folder);
  static bool rename(const std::string& folder, const std::string& newName);
  /// Hace que lo escrito se guarde de verdad (en el navegador, pasa la memoria a IndexedDB).
  static void flush();
  /// El mundo entero en un .zip (con su carpeta dentro, como los que se comparten de 1.8).
  static std::vector<u8> exportZip(const std::string& folder);
  /// Importa un .zip con un mundo (level.dat en la raíz o dentro de una carpeta). Devuelve la
  /// carpeta creada, o vacío si el zip no tiene un mundo.
  static std::string importZip(const std::vector<u8>& zip, const std::string& fallbackName);

  const std::filesystem::path& dir() const { return dir_; }
  bool exists() const;
  bool loadLevel(LevelInfo& out) const;
  bool saveLevel(const LevelInfo& info);
  RegionStore& regions() { return regions_; }
  /// Las regiones de una dimensión (0 el mundo, -1 el Nether en DIM-1, 1 el End en DIM1).
  RegionStore& regions(int dimension);

 private:
  std::filesystem::path dir_;
  RegionStore regions_;
  std::unique_ptr<RegionStore> nether_, end_;
};

nbt::Value levelToNbt(const LevelInfo& info);
LevelInfo levelFromNbt(const nbt::Value& root);

}  // namespace mcw
