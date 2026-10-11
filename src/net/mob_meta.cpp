#include "net/mob_meta.h"

namespace mcw::net {

void writeMobVariantMeta(Metadata& m, const Mob& mob) {
  switch (mob.type) {
    case MobType::Wolf:
      m.byte(16, static_cast<i8>((mob.sitting ? 1 : 0) | (mob.angry ? 2 : 0) | (mob.tamed ? 4 : 0)));
      m.string(17, mob.tamed ? "MC-WEB" : "");
      m.floatV(18, mob.health);
      m.byte(20, static_cast<i8>(mob.collar));
      break;
    case MobType::Ocelot:
      m.byte(16, static_cast<i8>((mob.sitting ? 1 : 0) | (mob.tamed ? 4 : 0)));
      m.string(17, mob.tamed ? "MC-WEB" : "");
      m.byte(18, static_cast<i8>(mob.variant));
      break;
    case MobType::Rabbit: m.byte(18, static_cast<i8>(mob.variant)); break;
    case MobType::Horse:
      m.intV(16, (mob.tamed ? 2 : 0) | (mob.saddled ? 4 : 0));
      m.byte(19, static_cast<i8>(mob.variant));
      m.intV(20, static_cast<i32>(mob.coat & 0xFFFF));
      break;
    case MobType::Villager: m.intV(16, mob.variant); break;
    case MobType::IronGolem: m.byte(16, mob.playerMade ? 1 : 0); break;
    case MobType::Bat: m.byte(16, mob.hanging ? 1 : 0); break;
    default: break;
  }
}

void readMobVariantMeta(const Metadata& m, Mob& mob) {
  auto at = [&](u8 i) { return m.find(i); };
  switch (mob.type) {
    case MobType::Wolf:
      if (const auto* f = at(16)) {
        mob.sitting = (f->i & 1) != 0;
        mob.angry = (f->i & 2) != 0;
        mob.tamed = (f->i & 4) != 0;
      }
      if (const auto* h = at(18); h && h->type == 3) mob.health = h->f;
      if (const auto* c = at(20)) mob.collar = static_cast<u8>(c->i & 15);
      break;
    case MobType::Ocelot:
      if (const auto* f = at(16)) {
        mob.sitting = (f->i & 1) != 0;
        mob.tamed = (f->i & 4) != 0;
      }
      if (const auto* t = at(18)) mob.variant = static_cast<u8>(t->i & 3);
      break;
    case MobType::Rabbit:
      if (const auto* t = at(18)) mob.variant = static_cast<u8>(t->i);
      break;
    case MobType::Horse:
      if (const auto* f = at(16)) {
        mob.tamed = (f->i & 2) != 0;
        mob.saddled = (f->i & 4) != 0;
      }
      if (const auto* t = at(19)) mob.variant = static_cast<u8>(t->i % 3);
      if (const auto* v = at(20)) mob.coat = static_cast<u32>(v->i) & 0xFFFF;
      break;
    case MobType::Villager:
      if (const auto* p = at(16)) mob.variant = static_cast<u8>(p->i % 5);
      break;
    case MobType::IronGolem:
      if (const auto* p = at(16)) mob.playerMade = (p->i & 1) != 0;
      break;
    case MobType::Bat:
      if (const auto* p = at(16)) mob.hanging = (p->i & 1) != 0;
      break;
    default: break;
  }
}

u32 mobVariantSig(const Mob& mob) {
  u32 s = (mob.sitting ? 1u : 0u) | (mob.angry ? 2u : 0u) | (mob.tamed ? 4u : 0u) | (mob.saddled ? 8u : 0u) | (mob.hanging ? 16u : 0u) |
          (mob.playerMade ? 32u : 0u);
  s |= static_cast<u32>(mob.variant) << 8;
  s |= static_cast<u32>(mob.collar & 15) << 16;
  s |= (mob.coat & 0xFF) << 20;
  s ^= static_cast<u32>(mob.health) << 24;  // (la vida del lobo)
  return s;
}

}  // namespace mcw::net
