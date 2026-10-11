// Tiempo atmosférico: lluvia, tormenta y nieve según el bioma, rayos y lo que hace el agua del cielo con el mundo
// (apagar fuego, llenar calderos, nieve y hielo). Comportamiento observable de 1.8; implementación propia.
#include <algorithm>
#include <cmath>

#include "data/biomes.h"
#include "data/blockstates.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

namespace {

/// Temperatura de un punto: la del bioma, más fría cuanto más alto (por encima del nivel del mar).
float temperatureAt(const World& w, int x, int y, int z) {
  const float t = biomeInfo(w.biome(x, z)).temperature;
  return y > 64 ? t - static_cast<float>(y - 64) * 0.05f / 30.0f : t;
}

/// El primer hueco sobre lo que está en pie en la columna: el mapa de alturas de la luz no cuenta lo que deja pasar la luz
/// (calderos, plantas, nieve...), así que se sube por encima de eso.
int precipTop(const World& w, const Chunk& c, int x, int z) {
  int top = c.height(x & 15, z & 15);
  while (top < 255 && w.block(x, top, z) != 0 && !isFluid(stateId(w.block(x, top, z)))) top++;
  return top;
}

}  // namespace

void GameSession::setWeather(bool raining, bool thundering, int ticks) {
  weather_.raining = raining || thundering;
  weather_.thundering = thundering;
  const int dur = ticks > 0 ? ticks : 12000 + tickRng_.nextInt(168000);
  weather_.rainTime = dur;
  weather_.thunderTime = dur;
}

void GameSession::tickWeather() {
  Weather& wt = weather_;
  wt.prevRain = wt.rain;
  wt.prevThunder = wt.thunder;
  if (remote_) return;  // (el servidor manda el estado y las intensidades)
  if (dimension_ == 0) {
    // Temporizadores (como en 1.8): al llegar a cero, cambia el estado y se sortea la duración siguiente
    if (--wt.thunderTime <= 0) {
      wt.thundering = !wt.thundering;
      wt.thunderTime = wt.thundering ? 3600 + tickRng_.nextInt(12000) : 12000 + tickRng_.nextInt(168000);
    }
    if (--wt.rainTime <= 0) {
      wt.raining = !wt.raining;
      wt.rainTime = wt.raining ? 12000 + tickRng_.nextInt(12000) : 12000 + tickRng_.nextInt(168000);
    }
    if (!wt.raining) wt.thundering = false;
  }
  // La intensidad sube y baja despacio
  const float rainTarget = wt.raining ? 1.0f : 0.0f, thunderTarget = wt.thundering && wt.raining ? 1.0f : 0.0f;
  if (wt.rain < rainTarget) wt.rain = std::min(rainTarget, wt.rain + 0.01f);
  else if (wt.rain > rainTarget) wt.rain = std::max(rainTarget, wt.rain - 0.01f);
  if (wt.thunder < thunderTarget) wt.thunder = std::min(thunderTarget, wt.thunder + 0.01f);
  else if (wt.thunder > thunderTarget) wt.thunder = std::max(thunderTarget, wt.thunder - 0.01f);
  if (dimension_ != 0) return;
  if (wt.rain > 0.0f) weatherChunkTicks();
}

int GameSession::precipitationColumn(int x, int z, int* ground) const {
  if (weather_.rain <= 0.0f || dimension_ != 0) return 0;
  const World& w = access_.world();
  const Chunk* c = w.chunkAt(x, z);
  if (!c) return 0;
  const BiomeInfo& b = biomeInfo(c->biome(x & 15, z & 15));
  if (b.rainfall <= 0.0f) return 0;  // desierto y sabana: no llueve
  const int top = precipTop(w, *c, x, z);
  if (ground) *ground = top;
  return temperatureAt(w, x, top, z) < 0.15f ? 2 : 1;
}

int GameSession::precipitationAt(int x, int y, int z) const {
  if (!weather_.raining) return 0;
  int ground = 0;
  const int type = precipitationColumn(x, z, &ground);
  return type != 0 && y >= ground ? type : 0;  // (por debajo del primer hueco hay techo)
}

void GameSession::weatherChunkTicks() {
  World& w = access_.world();
  std::set<std::pair<int, int>> done;
  for (const Player* pl : activePlayers()) {
    const int pcx = static_cast<int>(std::floor(pl->pos.x)) >> 4, pcz = static_cast<int>(std::floor(pl->pos.z)) >> 4;
    for (int cz = pcz - 6; cz <= pcz + 6; cz++)
      for (int cx = pcx - 6; cx <= pcx + 6; cx++) {
        if (!done.insert({cx, cz}).second) continue;
        const Chunk* c = w.chunk(cx, cz);
        if (!c) continue;
        // Rayos: en tormenta, uno por chunk cada 100 000 ticks (a esta escala, algo más a menudo para que se noten)
        if (weather_.thunder > 0.9f && tickRng_.nextInt(20000) == 0) {
          const u32 r = tickRng_.nextInt(256);
          strikeLightning(cx * 16 + (r & 15) + 0.5, cz * 16 + ((r >> 4) & 15) + 0.5);
        }
        // Una columna al azar de cada chunk, 1 de cada 16 ticks: nieve, hielo y calderos
        if (tickRng_.nextInt(16) != 0) continue;
        const u32 r = tickRng_.nextInt(256);
        const int x = cx * 16 + (r & 15), z = cz * 16 + ((r >> 4) & 15);
        const int top = precipTop(w, *c, x, z);  // el primer hueco libre sobre el terreno
        if (top <= 0 || top >= 255) continue;
        const BiomeInfo& b = biomeInfo(c->biome(x & 15, z & 15));
        if (b.rainfall <= 0.0f) continue;
        const float temp = temperatureAt(w, x, top, z);
        const BlockState below = w.block(x, top - 1, z);
        const int belowId = stateId(below);
        if (temp < 0.15f) {
          // Agua quieta que se hiela y nieve que cubre lo que esté al descubierto
          if (belowId == B::water && stateMeta(below) == 0 && w.blockLight(x, top, z) < 10) {
            setWorldBlock(x, top - 1, z, makeState(B::ice, 0));
          } else if (w.block(x, top, z) == 0 && w.blockLight(x, top, z) < 10 && belowId != B::ice && belowId != B::packed_ice &&
                     (blockInfo(belowId).opaqueCube || belowId == 18 || belowId == 161)) {
            setWorldBlock(x, top, z, makeState(B::snow_layer, 0));
          }
        } else if (belowId == 118 && stateMeta(below) < 3 && tickRng_.nextInt(20) == 0) {
          setWorldBlock(x, top - 1, z, makeState(118, stateMeta(below) + 1));  // el caldero se llena
        }
      }
  }
}

void GameSession::strikeLightning(double x, double z) {
  World& w = access_.world();
  const int bx = static_cast<int>(std::floor(x)), bz = static_cast<int>(std::floor(z));
  const Chunk* c = w.chunkAt(bx, bz);
  if (!c) return;
  const int top = precipTop(w, *c, bx, bz);
  const glm::dvec3 at(x, top, z);
  SessionEvent ev{SessionEvent::Type::Lightning, glm::ivec3(bx, top, bz), 0};
  ev.where = at;
  events_.push_back(ev);
  if (lightning_.size() < 16) lightning_.push_back(at);
  // Fuego donde cae
  if (rules_.fireTick && w.block(bx, top, bz) == 0) fireIgnite({bx, top, bz}, 0);
  // Daño y llamas a lo que esté a menos de 3 bloques (5 puntos de daño)
  for (Player* p : activePlayers())
    if (!p->dead && glm::length(p->pos - at) < 3.0 + 2.0) {
      damagePlayer(*p, 10.0f, at, 0.0f, DamageKind::Fire);
      p->fireTicks = std::max(p->fireTicks, 160);
    }
  // Un cerdo alcanzado se vuelve cerdo zombi
  std::vector<std::pair<glm::dvec3, float>> pigs;
  for (const Mob& m : mobs_)
    if (m.type == MobType::Pig && !m.dying() && glm::length(m.pos - at) <= 5.0) pigs.emplace_back(m.pos, m.yaw);
  for (const auto& [pos, yaw] : pigs) {
    for (const Mob& m : mobs_)
      if (m.type == MobType::Pig && m.pos == pos) {
        removeMob(m.id);
        break;
      }
    if (Mob* z = spawnMob(MobType::PigZombie, pos)) z->yaw = yaw;
  }
  for (Mob& m : mobs_) {
    if (m.dying() || glm::length(m.pos - at) > 5.0) continue;
    hurtMob(m, 10.0f, at, 0.0f, false);
    m.fireTicks = std::max(m.fireTicks, 160);
  }
}

}  // namespace mcw
