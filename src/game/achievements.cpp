// Logros de 1.8: lista, árbol (cada uno necesita el anterior) y posiciones en el mapa según el
// formato público de estadísticas. Nombres y descripciones: traducción propia.
#include "game/achievements.h"

#include <nlohmann/json.hpp>

namespace mcw {
namespace {

const std::array<AchievementInfo, kAchievementCount> kList = {{
    {"achievement.openInventory", "Hacer inventario", "Pulsa E para abrir el inventario", -1, 0, 0, 340, 0, false},
    {"achievement.mineWood", "Consiguiendo madera", "Golpea un árbol hasta que suelte un tronco", 0, 2, 1, 17, 0, false},
    {"achievement.buildWorkBench", "¡A currar!", "Fabrica una mesa de trabajo con cuatro tablones", 1, 4, -1, 58, 0, false},
    {"achievement.buildPickaxe", "¡A la mina!", "Usa tablones y palos para hacer un pico", 2, 4, 2, 270, 0, false},
    {"achievement.buildFurnace", "Tema candente", "Fabrica un horno con ocho bloques de roca", 3, 3, 4, 61, 0, false},
    {"achievement.acquireIron", "Hierro en mano", "Funde un lingote de hierro", 4, 1, 4, 265, 0, false},
    {"achievement.buildHoe", "Hora de cultivar", "Usa tablones y palos para hacer una azada", 2, 2, -3, 290, 0, false},
    {"achievement.makeBread", "Hornear pan", "Convierte trigo en pan", 6, -1, -3, 297, 0, false},
    {"achievement.bakeCake", "La tarta no es mentira", "Trigo, azúcar, leche y huevo", 6, 0, -5, 354, 0, false},
    {"achievement.buildBetterPickaxe", "Una mejora", "Fabrica un pico mejor", 3, 6, 2, 274, 0, false},
    {"achievement.cookFish", "Pescado frito", "Pesca y cocina un pez", 4, 2, 6, 350, 0, false},
    {"achievement.onARail", "Sobre raíles", "Recorre 1 km en vagoneta desde donde empezaste", 5, 2, 3, 66, 0, true},
    {"achievement.buildSword", "¡Al ataque!", "Usa tablones y palos para hacer una espada", 2, 6, -1, 268, 0, false},
    {"achievement.killEnemy", "Cazador de monstruos", "Ataca y derrota a un monstruo", 12, 8, -1, 352, 0, false},
    {"achievement.killCow", "Ganadero", "Consigue cuero", 12, 7, -3, 334, 0, false},
    {"achievement.flyPig", "Cuando los cerdos vuelen", "Cae por un precipicio montado en un cerdo", 14, 9, -3, 329, 0, true},
    {"achievement.snipeSkeleton", "Francotirador", "Mata a un esqueleto con una flecha a más de 50 bloques", 13, 7, 0, 261, 0, true},
    {"achievement.diamonds", "¡Diamantes!", "Consigue diamantes con tus herramientas de hierro", 5, -1, 5, 264, 0, false},
    {"achievement.diamondsToYou", "¡Diamantes para ti!", "Tira diamantes a otro jugador", 17, -1, 2, 264, 0, false},
    {"achievement.portal", "Más hondo todavía", "Construye un portal al Nether", 17, -1, 7, 49, 0, false},
    {"achievement.ghast", "Devolución al remitente", "Mata a un ghast con su propia bola de fuego", 19, -4, 8, 370, 0, true},
    {"achievement.blazeRod", "Jugando con fuego", "Quítale la vara a un blaze", 19, 0, 9, 369, 0, false},
    {"achievement.potion", "Destilería casera", "Prepara una poción", 21, 2, 8, 373, 0, false},
    {"achievement.theEnd", "¿El fin?", "Encuentra el End", 21, 3, 10, 381, 0, true},
    {"achievement.theEnd2", "El fin.", "Mata al dragón del End", 23, 4, 13, 122, 0, true},
    {"achievement.enchantments", "Encantador", "Usa un libro, obsidiana y diamantes para una mesa de encantamientos", 17, -4, 4, 116, 0, false},
    {"achievement.overkill", "Exagerado", "Quita nueve corazones de un solo golpe", 25, -4, 1, 276, 0, true},
    {"achievement.bookcase", "Bibliotecario", "Fabrica librerías para mejorar la mesa de encantamientos", 25, -3, 6, 47, 0, false},
    {"achievement.breedCow", "Repoblación", "Cría dos vacas con trigo", 14, 7, -5, 296, 0, false},
    {"achievement.spawnWither", "El principio", "Invoca al Wither", 24, 7, 12, 397, 1, false},
    {"achievement.killWither", "El principio.", "Mata al Wither", 29, 7, 10, 399, 0, false},
    {"achievement.fullBeacon", "Faro", "Crea un faro completo", 30, 7, 8, 138, 0, true},
    {"achievement.exploreAllBiomes", "Aventurero", "Descubre todos los biomas", 23, 4, 8, 313, 0, true},
    {"achievement.overpowered", "Todopoderoso", "Fabrica una manzana de oro encantada", 9, 6, 4, 322, 1, true},
}};

}  // namespace

const AchievementInfo& achievementInfo(Ach a) { return kList[static_cast<std::size_t>(a)]; }

bool Achievements::canTake(Ach a) const {
  const int p = achievementInfo(a).parent;
  return p < 0 || got_[p];
}

bool Achievements::award(Ach a) {
  const int i = static_cast<int>(a);
  if (got_[i] || !canTake(a)) return false;
  got_[i] = true;
  return true;
}

int Achievements::count() const {
  int n = 0;
  for (bool g : got_) n += g;
  return n;
}

std::string Achievements::toJson() const {
  nlohmann::json j = nlohmann::json::object();
  for (int i = 0; i < kAchievementCount; i++)
    if (got_[i]) j[std::string(kList[i].id)] = 1;
  for (const auto& [k, v] : stats_) j[k] = v;
  return j.dump();
}

void Achievements::fromJson(const std::string& text) {
  clear();
  nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  if (!j.is_object()) return;
  for (auto& [k, v] : j.items()) {
    bool isAch = false;
    for (int i = 0; i < kAchievementCount; i++)
      if (k == kList[i].id) {
        // En 1.8 un logro vale 1 (o un objeto con "value" en los que tienen progreso)
        got_[i] = v.is_number() ? v.get<i64>() > 0 : (v.is_object() && v.value("value", 0) > 0);
        isAch = true;
      }
    if (!isAch && v.is_number_integer()) stats_[k] = v.get<i64>();
  }
}

void Achievements::clear() {
  got_.fill(false);
  stats_.clear();
}

}  // namespace mcw
