#include "game/firework.h"

#include <vector>

#include "data/items.h"

namespace mcw::firework {

i32 dyeColor(int meta) {
  // Los colores de 1.8 para fuegos artificiales (el orden es el de los tintes: 0 tinta negra, 1 rojo, 2 verde cactus, 3 cacao...)
  static constexpr i32 kColors[16] = {0x1E1B1B, 0xB3312C, 0x3B511A, 0x51301A, 0x253192, 0x7B2FBE, 0x287697, 0xABABAB,
                                      0x434343, 0xD88198, 0x41CD34, 0xDECF2A, 0x6689D3, 0xC354CD, 0xEB8844, 0xF0F0F0};
  return kColors[meta & 15];
}

namespace {

bool isSkull(const ItemStack& s, int type) { return s.id == 397 && s.meta == type; }

std::shared_ptr<const ItemExtra> withExplosions(int flight, std::vector<FireworkExplosion> ex) {
  ItemExtra e;
  e.flight = flight;
  e.explosions = std::move(ex);
  return std::make_shared<const ItemExtra>(std::move(e));
}

}  // namespace

std::optional<ItemStack> craft(std::span<const ItemStack> grid) {
  int gunpowder = 0, paper = 0, stars = 0, glowstone = 0, diamond = 0, other = 0, total = 0;
  std::vector<i32> dyes;
  int shape = -1, shapes = 0;
  std::vector<const ItemStack*> starItems;
  for (const ItemStack& s : grid) {
    if (s.empty()) continue;
    total++;
    if (s.id == ItemId::gunpowder) gunpowder++;
    else if (s.id == ItemId::paper) paper++;
    else if (s.id == ItemId::firework_charge) {
      stars++;
      starItems.push_back(&s);
    } else if (s.id == ItemId::dye) dyes.push_back(dyeColor(s.meta));
    else if (s.id == ItemId::glowstone_dust) glowstone++;
    else if (s.id == ItemId::diamond) diamond++;
    else if (s.id == ItemId::fire_charge) { shape = 1; shapes++; }
    else if (s.id == ItemId::gold_nugget) { shape = 2; shapes++; }
    else if (isSkull(s, 4)) { shape = 3; shapes++; }
    else if (s.id == ItemId::feather) { shape = 4; shapes++; }
    else other++;
  }
  if (total == 0 || other > 0) return std::nullopt;

  // Estrella nueva: una pólvora, al menos un tinte y, como mucho, una forma, estela y centelleo (sin papel ni estrellas)
  if (gunpowder == 1 && paper == 0 && stars == 0 && !dyes.empty() && shapes <= 1 && glowstone <= 1 && diamond <= 1) {
    FireworkExplosion x;
    x.type = static_cast<u8>(shape < 0 ? 0 : shape);
    x.flicker = glowstone == 1;
    x.trail = diamond == 1;
    x.colors = dyes;
    ItemStack out(ItemId::firework_charge, 1, 0);
    out.extra = withExplosions(0, {x});
    return out;
  }
  // Desvanecer: una estrella y tintes (sustituyen los colores a los que se apaga)
  if (stars == 1 && total == stars + static_cast<int>(dyes.size()) && !dyes.empty()) {
    const ItemStack& star = *starItems[0];
    FireworkExplosion x;
    if (star.extra && !star.extra->explosions.empty()) x = star.extra->explosions[0];
    x.fade = dyes;
    ItemStack out(ItemId::firework_charge, 1, 0);
    out.extra = withExplosions(0, {x});
    return out;
  }
  // Cohete: un papel, de 1 a 3 pólvoras y hasta 7 estrellas (nada más)
  if (paper == 1 && gunpowder >= 1 && gunpowder <= 3 && total == paper + gunpowder + stars && stars <= 7) {
    std::vector<FireworkExplosion> ex;
    for (const ItemStack* s : starItems)
      if (s->extra && !s->extra->explosions.empty()) ex.push_back(s->extra->explosions[0]);
    ItemStack out(ItemId::fireworks, 3, 0);
    out.extra = withExplosions(gunpowder, std::move(ex));
    return out;
  }
  return std::nullopt;
}

}  // namespace mcw::firework
