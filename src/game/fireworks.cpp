// Cohetes de fuegos artificiales en la partida: se lanzan sobre un bloque, suben acelerando y estallan al acabarse el vuelo, y hieren a lo que
// tengan a menos de 5 bloques si llevan estrellas (comportamiento según minecraft.wiki).
#include <algorithm>
#include <cmath>

#include "data/blocks.h"
#include "data/items.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {

void GameSession::launchRocket(const glm::dvec3& at, const std::shared_ptr<const ItemExtra>& extra) {
  Rocket r;
  r.id = nextRocketId_++;
  r.pos = r.prevPos = at;
  r.motion = {(rng_.nextFloat() - 0.5) * 0.002, 0.05, (rng_.nextFloat() - 0.5) * 0.002};
  const int flight = extra && extra->flight > 0 ? std::clamp(extra->flight, 1, 3) : 1;
  r.lifetime = 10 * flight + rng_.nextInt(6) + rng_.nextInt(7);
  r.extra = extra;
  r.owner = player_.uid;
  rockets_.push_back(std::move(r));
}

void GameSession::tickRockets() {
  const World& w = access_.world();
  for (Rocket& r : rockets_) {
    if (r.exploded) {
      r.linger++;
      continue;
    }
    if (r.netDriven) {  // (cliente de red: lo mueve el servidor)
      r.prevPos = r.pos;
      continue;
    }
    r.prevPos = r.pos;
    r.life++;
    r.motion.x *= 1.15;
    r.motion.z *= 1.15;
    r.motion.y += 0.04;
    r.pos += r.motion;
    const bool blocked = blockInfo(stateId(w.block(static_cast<int>(std::floor(r.pos.x)), static_cast<int>(std::floor(r.pos.y)), static_cast<int>(std::floor(r.pos.z))))).opaqueCube;
    if (r.life < r.lifetime && !blocked) continue;
    r.exploded = true;
    const int n = r.extra ? static_cast<int>(r.extra->explosions.size()) : 0;
    if (n == 0) continue;
    bursts_.push_back({r.pos, r.extra});
    const float damage = 5.0f + 2.0f * static_cast<float>(n);  // 5 más 2 por cada explosión, a lo que esté a menos de 5 bloques
    for (Mob& m : mobs_)
      if (!m.dying() && glm::distance(m.pos + glm::dvec3(0, m.info().height * 0.5, 0), r.pos) < 5.0) hurtMob(m, damage, r.pos, 0.0f, false);
    for (Player* p : activePlayers())
      if (!p->dead && glm::distance(p->pos + glm::dvec3(0, 0.9, 0), r.pos) < 5.0) damagePlayer(*p, damage, r.pos, 0.0f, DamageKind::Explosion);
  }
  // Un tick después de estallar se quita (así el servidor tiene tiempo de contarlo a los clientes)
  std::erase_if(rockets_, [](const Rocket& r) { return r.exploded && r.linger > 1; });
  if (!localActive_) bursts_.clear();  // (servidor dedicado: nadie dibuja las partículas)
}

}  // namespace mcw
