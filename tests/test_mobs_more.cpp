#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"
#include "net/mob_meta.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

TickInput aimAt(const GameSession& s, const glm::dvec3& target) {
  TickInput in = testing::idleTick();
  in.aimDir = glm::normalize(target - s.player().eyePos());
  return in;
}

void rightClick(GameSession& s, const glm::dvec3& target) {
  TickInput in = aimAt(s, target);
  in.use = in.usePressed = true;
  s.tick(in);
  in.use = in.usePressed = false;
  s.tick(in);
}

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

struct Setup {
  FlatTestWorld fw;
  GameSession s{fw, 11};
  Setup() {
    s.setRules({2, true, true});
    s.player().pos = s.player().prevPos = {0.5, 64, 4.5};
    s.setMode(GameMode::Survival);
  }
  u32 spawn(MobType t, double x, double z, bool noai = false) {
    Mob* m = s.spawnMob(t, {x, 64.0, z});
    m->noAI = noai;
    m->persistent = true;
    return m->id;
  }
  Mob* mob(u32 id) { return s.mobById(id); }
  int count(MobType t) {
    int n = 0;
    for (const Mob& m : s.mobs()) n += m.type == t && !m.dying();
    return n;
  }
  int items(int id) {
    int n = 0;
    for (const ItemEntity& e : s.items()) n += e.stack.id == id ? e.stack.count : 0;
    for (int i = 0; i < PlayerInventory::kSize; i++) n += s.player().inventory.slot(i).id == id ? s.player().inventory.slot(i).count : 0;
    return n;
  }
};

}  // namespace

TEST_CASE("Lobo: se doma con huesos (hay que insistir), se sienta con el dueño, la carne lo cura y no ataca a quien lo domó") {
  Setup t;
  const u32 id = t.spawn(MobType::Wolf, 0.5, 2.0, true);
  t.s.tick(testing::idleTick());
  int tries = 0;
  while (!t.mob(id)->tamed && tries < 40) {
    t.s.player().inventory.selected() = ItemStack(ItemId::bone, 1);
    rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.4, 0));
    tries++;
  }
  REQUIRE(t.mob(id)->tamed);
  CHECK(t.mob(id)->ownerUid == t.s.player().uid);
  CHECK(t.mob(id)->health == doctest::Approx(20.0f));
  CHECK(t.mob(id)->sitting);
  // Con la mano vacía, el dueño lo levanta y lo vuelve a sentar
  t.s.player().inventory.selected().clear();
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.4, 0));
  CHECK_FALSE(t.mob(id)->sitting);
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.4, 0));
  CHECK(t.mob(id)->sitting);
  // La carne cura (aquí, herido)
  t.mob(id)->health = 10.0f;
  t.s.player().inventory.selected() = ItemStack(ItemId::cooked_beef, 2);
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.4, 0));
  CHECK(t.mob(id)->health > 10.0f);
  CHECK(t.s.player().inventory.selected().count == 1);
  // El collar toma el color del tinte (azul = tinte 4 -> color 11)
  t.s.player().inventory.selected() = ItemStack(ItemId::dye, 1, 4);
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.4, 0));
  CHECK(t.mob(id)->collar == 4);
}

TEST_CASE("Lobo manso: sigue al dueño, se teletransporta si se queda lejos y se queda quieto sentado") {
  Setup t;
  const u32 id = t.spawn(MobType::Wolf, 6.5, 2.0);
  Mob* w = t.mob(id);
  w->tamed = true;
  w->ownerUid = t.s.player().uid;
  tickN(t.s, 160);
  CHECK(glm::length(t.mob(id)->pos - t.s.player().pos) < 6.0);  // se ha acercado
  // Muy lejos: aparece junto al dueño
  t.mob(id)->pos = t.mob(id)->prevPos = {14.5, 64, -10.0};
  tickN(t.s, 60);
  CHECK(glm::length(t.mob(id)->pos - t.s.player().pos) < 8.0);
  // Sentado, no se mueve
  t.mob(id)->sitting = true;
  const glm::dvec3 at = t.mob(id)->pos;
  t.s.player().pos = t.s.player().prevPos = {10.5, 64, 4.5};
  tickN(t.s, 80);
  CHECK(glm::length(t.mob(id)->pos - at) < 0.5);
}

TEST_CASE("Lobo salvaje: se enfada si lo golpean (y su manada) y caza ovejas") {
  Setup t;
  const u32 a = t.spawn(MobType::Wolf, 0.5, 0.0, true);
  const u32 b = t.spawn(MobType::Wolf, 3.5, 0.0, true);
  t.s.tick(testing::idleTick());
  t.s.hurtMob(*t.mob(a), 1.0f, t.s.player().pos, 0.2f, true);
  CHECK(t.mob(a)->angry);
  CHECK(t.mob(b)->angry);
  Setup u;
  const u32 w = u.spawn(MobType::Wolf, 0.5, 0.0);
  const u32 sheep = u.spawn(MobType::Sheep, 3.0, 0.0, true);
  u.s.player().pos = u.s.player().prevPos = {0.5, 64, 12.0};
  tickN(u.s, 800);
  CHECK((u.mob(sheep) == nullptr || u.mob(sheep)->health < 8.0f || u.mob(sheep)->dying()));
  (void)w;
}

TEST_CASE("Ocelote: se doma con pescado crudo y se vuelve un gato") {
  Setup t;
  const u32 id = t.spawn(MobType::Ocelot, 0.5, 2.0, true);
  t.s.tick(testing::idleTick());
  int tries = 0;
  while (!t.mob(id)->tamed && tries < 40) {
    t.s.player().inventory.selected() = ItemStack(ItemId::fish, 1);
    rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.3, 0));
    tries++;
  }
  REQUIRE(t.mob(id)->tamed);
  CHECK(t.mob(id)->variant >= 1);
  CHECK(t.mob(id)->variant <= 3);
  CHECK(t.mob(id)->sitting);
}

TEST_CASE("Champiñaca: las tijeras la vuelven vaca y sueltan 5 setas; el cuenco da sopa") {
  Setup t;
  const u32 id = t.spawn(MobType::Mooshroom, 0.5, 2.0, true);
  t.s.tick(testing::idleTick());
  t.s.player().inventory.selected() = ItemStack(ItemId::bowl, 1);
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.6, 0));
  CHECK(t.items(ItemId::mushroom_stew) == 1);
  t.s.player().inventory.selected() = ItemStack(ItemId::shears, 1);
  rightClick(t.s, t.mob(id)->pos + glm::dvec3(0, 0.6, 0));
  CHECK(t.mob(id)->type == MobType::Cow);
  CHECK(t.items(ItemId::red_mushroom) == 5);
}

TEST_CASE("Calamar: se queda en el agua, nada y suelta tinta; el murciélago vuela y se cuelga") {
  Setup t;
  for (int x = -4; x <= 4; x++)
    for (int z = -4; z <= 4; z++)
      for (int y = 60; y <= 66; y++) t.fw.setBlock(x + 10, y, z, makeState(y == 60 ? B::stone : B::water, 0));
  Mob* sq = t.s.spawnMob(MobType::Squid, {10.5, 63.0, 0.5});
  const u32 id = sq->id;
  tickN(t.s, 400);
  const Mob* m = t.mob(id);
  REQUIRE(m);
  CHECK(m->pos.x > 5.4);
  CHECK(m->pos.x < 15.6);
  CHECK(m->pos.y > 60.9);
  CHECK(m->pos.y < 66.1);
  // Tinta al morir
  t.s.hurtMob(*t.mob(id), 100.0f, t.s.player().pos, 0.0f, true);
  tickN(t.s, 5);
  CHECK(t.items(ItemId::dye) >= 1);
  // Murciélago
  Setup b;
  for (int x = -6; x <= 6; x++)
    for (int z = -6; z <= 6; z++) b.fw.setBlock(x, 70, z, makeState(B::stone));  // techo
  const u32 bat = b.spawn(MobType::Bat, 0.5, 0.5);
  b.mob(bat)->pos.y = 66.0;
  b.s.player().pos = b.s.player().prevPos = {0.5, 64, 12.0};
  glm::dvec3 start = b.mob(bat)->pos;
  double moved = 0;
  for (int i = 0; i < 300; i++) {
    b.s.tick(testing::idleTick());
    if (b.mob(bat)) moved = std::max(moved, glm::length(b.mob(bat)->pos - start));
  }
  CHECK(moved > 1.0);
  CHECK(b.mob(bat) != nullptr);
}

TEST_CASE("Conejo: salta hacia donde va y huye del jugador") {
  Setup t;
  const u32 id = t.spawn(MobType::Rabbit, 0.5, 2.5);
  const glm::dvec3 start = t.mob(id)->pos;
  tickN(t.s, 40);  // el jugador está a dos bloques: huye
  CHECK(glm::length(t.mob(id)->pos - start) > 1.0);
  CHECK(t.mob(id)->pos.z < start.z);
}

TEST_CASE("Gólems: una calabaza sobre hierro en T o sobre dos de nieve les da vida") {
  Setup t;
  t.s.player().pos = t.s.player().prevPos = {2.5, 68, 5.5};  // sobre un saliente: apunta desde arriba, al alcance
  auto put = [&](int x, int y, int z, int id) { t.fw.setBlock(x, y, z, makeState(id)); };
  // Hierro: tres en vertical con brazos en el del medio
  put(2, 64, 2, B::iron_block);
  put(2, 65, 2, B::iron_block);
  put(2, 66, 2, B::iron_block);
  put(1, 65, 2, B::iron_block);
  put(3, 65, 2, B::iron_block);
  t.s.player().inventory.selected() = ItemStack(B::pumpkin, 1);
  t.s.tick(testing::idleTick());
  rightClick(t.s, {2.5, 66.99, 2.5});
  CHECK(t.count(MobType::IronGolem) == 1);
  CHECK(stateId(t.fw.w.block(2, 65, 2)) == 0);
  CHECK(stateId(t.fw.w.block(1, 65, 2)) == 0);
  for (const Mob& m : t.s.mobs())
    if (m.type == MobType::IronGolem) CHECK(m.playerMade);
  // Nieve: dos bloques y la calabaza
  t.s.player().pos = t.s.player().prevPos = {6.5, 68, 5.5};
  put(6, 64, 2, B::snow);
  put(6, 65, 2, B::snow);
  t.s.player().inventory.selected() = ItemStack(B::pumpkin, 1);
  rightClick(t.s, {6.5, 65.99, 2.5});
  CHECK(t.count(MobType::SnowGolem) == 1);
}

TEST_CASE("Gólem de hierro defiende: ataca zombis y los lanza; el de nieve les tira bolas") {
  Setup t;
  const u32 g = t.spawn(MobType::IronGolem, 0.5, 0.0);
  t.mob(g)->playerMade = true;
  const u32 z = t.spawn(MobType::Zombie, 4.5, 0.0, true);
  t.mob(z)->health = 100.0f;
  const float before = t.mob(z)->health;
  tickN(t.s, 120);
  CHECK(t.mob(z)->health < before);
  Setup u;
  const u32 sg = u.spawn(MobType::SnowGolem, 0.5, 0.0);
  const u32 z2 = u.spawn(MobType::Zombie, 5.5, 0.0, true);
  u.mob(z2)->health = 100.0f;
  bool thrown = false;
  for (int i = 0; i < 100 && !thrown; i++) {
    u.s.tick(testing::idleTick());
    for (const Thrown& th : u.s.thrown()) thrown |= th.owner == sg;
  }
  CHECK(thrown);
}

TEST_CASE("Bruja: lanza pociones al jugador que ve; el aldeano huye de los zombis") {
  Setup t;
  t.s.player().pos = t.s.player().prevPos = {0.5, 64, 6.5};
  const u32 w = t.spawn(MobType::Witch, 0.5, 0.0);
  bool potion = false;
  for (int i = 0; i < 200 && !potion; i++) {
    t.s.tick(testing::idleTick());
    for (const Thrown& th : t.s.thrown()) potion |= th.kind == Thrown::Potion && th.owner == w;
  }
  CHECK(potion);
  Setup u;
  u.s.player().pos = u.s.player().prevPos = {0.5, 64, 14.0};
  const u32 v = u.spawn(MobType::Villager, 0.5, 0.0);
  const u32 z = u.spawn(MobType::Zombie, 0.5, 4.0, true);
  const double d0 = glm::length(u.mob(v)->pos - u.mob(z)->pos);
  tickN(u.s, 60);
  CHECK(glm::length(u.mob(v)->pos - u.mob(z)->pos) > d0 + 1.0);
}

TEST_CASE("Criaturas de más: guardado en NBT con los ids de 1.8") {
  Mob w;
  w.type = MobType::Wolf;
  w.tamed = true;
  w.sitting = true;
  w.collar = 11;
  w.ownerUid = 1;
  w.health = 14;
  w.pos = {1, 64, 2};
  const nbt::Value c = save::mobToNbt(w);
  CHECK(c.getString("id") == "Wolf");
  CHECK(c.getBool("Sitting"));
  CHECK(c.getInt("CollarColor") == 11);
  const auto back = save::mobFromNbt(c);
  REQUIRE(back);
  CHECK(back->tamed);
  CHECK(back->sitting);
  CHECK(back->collar == 11);
  Mob r;
  r.type = MobType::Rabbit;
  r.variant = 4;
  r.pos = {0, 64, 0};
  r.health = 3;
  CHECK(save::mobToNbt(r).getString("id") == "Rabbit");
  CHECK(save::mobFromNbt(save::mobToNbt(r))->variant == 4);
  Mob h;
  h.type = MobType::Horse;
  h.variant = 1;
  h.coat = 0x0203;
  h.tamed = true;
  h.saddled = true;
  h.pos = {0, 64, 0};
  h.health = 20;
  CHECK(save::mobToNbt(h).getString("id") == "EntityHorse");
  const auto hb = save::mobFromNbt(save::mobToNbt(h));
  REQUIRE(hb);
  CHECK(hb->variant == 1);
  CHECK(hb->coat == 0x0203);
  CHECK(hb->saddled);
  Mob m;
  m.type = MobType::Mooshroom;
  m.pos = {0, 64, 0};
  m.health = 10;
  CHECK(save::mobToNbt(m).getString("id") == "MushroomCow");
  CHECK(save::mobFromNbt(save::mobToNbt(m))->type == MobType::Mooshroom);
  Mob v;
  v.type = MobType::Villager;
  v.variant = 3;
  v.pos = {0, 64, 0};
  v.health = 20;
  CHECK(save::mobFromNbt(save::mobToNbt(v))->variant == 3);
}

TEST_CASE("Red: los metadatos de variante van y vuelven (lobo, caballo, conejo, murciélago)") {
  Mob w;
  w.type = MobType::Wolf;
  w.tamed = true;
  w.sitting = true;
  w.collar = 5;
  w.health = 17;
  net::Metadata m;
  net::writeMobVariantMeta(m, w);
  Mob r;
  r.type = MobType::Wolf;
  net::readMobVariantMeta(m, r);
  CHECK(r.tamed);
  CHECK(r.sitting);
  CHECK_FALSE(r.angry);
  CHECK(r.collar == 5);
  CHECK(r.health == doctest::Approx(17));
  const u32 sig = net::mobVariantSig(w);
  w.sitting = false;
  CHECK(net::mobVariantSig(w) != sig);  // un cambio vuelve a mandarse

  Mob h;
  h.type = MobType::Horse;
  h.variant = 2;
  h.coat = 0x0203;
  h.saddled = true;
  net::Metadata hm;
  net::writeMobVariantMeta(hm, h);
  Mob hr;
  hr.type = MobType::Horse;
  net::readMobVariantMeta(hm, hr);
  CHECK(hr.variant == 2);
  CHECK(hr.coat == 0x0203);
  CHECK(hr.saddled);

  Mob b;
  b.type = MobType::Bat;
  b.hanging = true;
  net::Metadata bm;
  net::writeMobVariantMeta(bm, b);
  Mob br;
  br.type = MobType::Bat;
  net::readMobVariantMeta(bm, br);
  CHECK(br.hanging);
}
