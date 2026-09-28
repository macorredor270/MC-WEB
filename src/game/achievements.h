#pragma once
#include <array>
#include <map>
#include <string>
#include <string_view>

#include "core/types.h"

namespace mcw {

/// Los 34 logros de 1.8, en el orden de la lista del juego.
enum class Ach : int {
  OpenInventory, MineWood, BuildWorkBench, BuildPickaxe, BuildFurnace, AcquireIron, BuildHoe, MakeBread, BakeCake,
  BuildBetterPickaxe, CookFish, OnARail, BuildSword, KillEnemy, KillCow, FlyPig, SnipeSkeleton, Diamonds,
  DiamondsToYou, Portal, Ghast, BlazeRod, Potion, TheEnd, TheEnd2, Enchantments, Overkill, Bookcase, BreedCow,
  SpawnWither, KillWither, FullBeacon, ExploreAllBiomes, Overpowered,
  Count
};

struct AchievementInfo {
  std::string_view id;        // "achievement.openInventory" (el nombre de las estadísticas de 1.8)
  std::string_view name;      // nombre en español (traducción propia)
  std::string_view desc;      // cómo se consigue
  int parent;                 // índice del logro previo, -1 si no tiene
  int x, y;                   // posición en el mapa de logros
  int iconId, iconMeta;       // objeto que lo representa
  bool special;               // marco especial (los difíciles)
};

const AchievementInfo& achievementInfo(Ach a);
inline const AchievementInfo& achievementInfo(int i) { return achievementInfo(static_cast<Ach>(i)); }
constexpr int kAchievementCount = static_cast<int>(Ach::Count);

/// Logros y estadísticas de un jugador en un mundo (stats/<jugador>.json en el formato de 1.8).
class Achievements {
 public:
  bool has(Ach a) const { return got_[static_cast<int>(a)]; }
  /// ¿Se puede conseguir ya? (su logro previo está conseguido)
  bool canTake(Ach a) const;
  /// Lo marca si se puede; devuelve true si es nuevo.
  bool award(Ach a);
  int count() const;

  /// Estadísticas ("stat.walkOneCm"...): se suman y se guardan con los logros.
  void addStat(const std::string& name, i64 amount = 1) { stats_[name] += amount; }
  i64 stat(const std::string& name) const {
    auto it = stats_.find(name);
    return it == stats_.end() ? 0 : it->second;
  }
  const std::map<std::string, i64>& stats() const { return stats_; }

  std::string toJson() const;
  void fromJson(const std::string& text);
  void clear();

 private:
  std::array<bool, kAchievementCount> got_{};
  std::map<std::string, i64> stats_;
};

}  // namespace mcw
