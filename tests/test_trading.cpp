#include <doctest/doctest.h>

#include <cmath>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"
#include "game/trading.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}

struct Shop {
  FlatTestWorld fw;
  GameSession s{fw, 17};
  Mob* villager = nullptr;
  Shop() {
    s.setRules({2, true, true});
    s.setMode(GameMode::Survival);
    s.player().pos = s.player().prevPos = {0.5, 64, 4.5};
    villager = s.spawnMob(MobType::Villager, {0.5, 64.0, 2.0});
    villager->noAI = true;
    villager->persistent = true;
    villager->variant = 0;
    villager->career = static_cast<u8>(Career::Farmer);
    villager->careerTier = 1;
    Random rng(5);
    addCareerOffers(villager->offers, Career::Farmer, 1, rng);
  }
  void click(int slot, int button = 0, bool shift = false) { s.menu()->click(slot, button, shift); }
};

}  // namespace

TEST_CASE("Ofertas: cada oficio tiene todos sus niveles, con cantidades y precios dentro de su margen") {
  Random rng(1);
  for (int c = 0; c <= static_cast<int>(Career::Leatherworker); c++)
    for (int tier = 1; tier <= careerTiers(static_cast<Career>(c)); tier++) {
      std::vector<TradeOffer> offers;
      addCareerOffers(offers, static_cast<Career>(c), tier, rng);
      CAPTURE(c);
      CAPTURE(tier);
      CHECK_FALSE(offers.empty());
      for (const TradeOffer& o : offers) {
        CHECK_FALSE(o.buy.empty());
        CHECK_FALSE(o.sell.empty());
        CHECK(o.buy.count >= 1);
        CHECK(o.sell.count >= 1);
        CHECK(o.uses == 0);
      }
    }
  // Granjero, nivel 1: el trigo de 18 a 22 por una esmeralda
  bool wheat = false;
  for (int i = 0; i < 200; i++) {
    std::vector<TradeOffer> o;
    addCareerOffers(o, Career::Farmer, 1, rng);
    for (const TradeOffer& t : o)
      if (t.buy.id == ItemId::wheat) {
        wheat = true;
        CHECK(t.buy.count >= 18);
        CHECK(t.buy.count <= 22);
        CHECK(t.sell.id == ItemId::emerald);
        CHECK(t.sell.count == 1);
      }
  }
  CHECK(wheat);
}

TEST_CASE("Ofertas: el libro del bibliotecario lleva un solo encantamiento y cuesta entre 5 y 64 esmeraldas; los objetos de herrero, encantados") {
  Random rng(2);
  for (int i = 0; i < 100; i++) {
    std::vector<TradeOffer> o;
    addCareerOffers(o, Career::Librarian, 1, rng);
    const TradeOffer& book = o.back();
    CHECK(book.sell.id == ItemId::enchanted_book);
    CHECK(book.buy2.id == ItemId::book);
    CHECK(book.buy.id == ItemId::emerald);
    CHECK(book.buy.count >= 5);
    CHECK(book.buy.count <= 64);
    CHECK(book.sell.extra->stored.size() == 1);
  }
  std::vector<TradeOffer> o;
  addCareerOffers(o, Career::Weaponsmith, 3, rng);
  for (const TradeOffer& t : o)
    if (t.sell.id != ItemId::emerald) CHECK(t.sell.hasEnchants());
}

TEST_CASE("Ofertas: pagar con las dos casillas en cualquier orden, y sin cubrir la oferta no hay resultado") {
  TradeOffer o;
  o.buy = ItemStack(ItemId::emerald, 1);
  o.buy2 = ItemStack(ItemId::fish, 6);
  o.sell = ItemStack(ItemId::cooked_fish, 6);
  CHECK(offerSatisfied(o, ItemStack(ItemId::emerald, 1), ItemStack(ItemId::fish, 6)));
  CHECK(offerSatisfied(o, ItemStack(ItemId::fish, 7), ItemStack(ItemId::emerald, 3)));
  CHECK_FALSE(offerSatisfied(o, ItemStack(ItemId::emerald, 1), ItemStack(ItemId::fish, 5)));
  CHECK_FALSE(offerSatisfied(o, ItemStack(ItemId::emerald, 1), ItemStack(ItemId::fish, 6, 1)));  // otro pescado
  CHECK_FALSE(offerSatisfied(o, ItemStack(), ItemStack()));
}

TEST_CASE("Comercio: el clic derecho abre la ventana, rellena las casillas y el trato da la esmeralda") {
  Shop t;
  t.s.player().inventory.slot(0) = ItemStack(ItemId::wheat, 64);
  t.s.player().inventory.select(4);
  REQUIRE(t.s.openTrading(*t.villager));
  Menu* m = t.s.menu();
  REQUIRE(m);
  CHECK(m->kind() == MenuKind::Trade);
  CHECK(t.villager->tradingWith == t.s.player().uid);
  // La primera oferta es la de trigo: se rellena sola
  const TradeOffer& first = t.villager->offers[0];
  REQUIRE(first.buy.id == ItemId::wheat);
  CHECK(m->tradeSlot(0).id == ItemId::wheat);
  CHECK(m->tradeSlot(0).count == first.buy.count);
  CHECK(m->tradeOutput().id == ItemId::emerald);
  const int wheatBefore = t.s.player().inventory.slot(0).count;
  CHECK(wheatBefore == 64 - first.buy.count);
  // Se coge el resultado (casilla 2)
  t.click(2);
  CHECK(t.s.player().cursor.id == ItemId::emerald);
  CHECK(t.s.player().cursor.count == 1);
  CHECK(m->tradeSlot(0).empty());
  tickN(t.s, 1);
  CHECK(t.villager->offers[0].uses == 1);
  CHECK_FALSE(t.s.orbs().empty());  // experiencia por el trato
}

TEST_CASE("Comercio: cerrar la ventana devuelve lo que se había puesto y libera al aldeano") {
  Shop t;
  t.s.player().inventory.slot(0) = ItemStack(ItemId::wheat, 64);
  t.s.player().inventory.select(4);
  REQUIRE(t.s.openTrading(*t.villager));
  t.s.closeMenu();
  CHECK(t.villager->tradingWith == 0);
  CHECK(t.s.menu() == nullptr);
  CHECK(t.s.player().inventory.slot(0).count == 64);
}

TEST_CASE("Comercio: las ofertas gastadas no dan resultado y se renuevan pasado un rato; la primera vez abre el siguiente nivel") {
  Shop t;
  t.s.player().inventory.slot(0) = ItemStack(ItemId::wheat, 64);
  t.s.player().inventory.select(4);
  REQUIRE(t.s.openTrading(*t.villager));
  const std::size_t before = t.villager->offers.size();
  t.click(2);  // primer trato: abre el siguiente nivel
  t.s.player().cursor.clear();
  tickN(t.s, 1);
  CHECK(t.villager->tradeReset > 0);
  tickN(t.s, 45);
  CHECK(t.villager->careerTier == 2);
  CHECK(t.villager->offers.size() > before);
  // Agotar la oferta: sin resultado
  t.villager->offers[0].uses = t.villager->offers[0].maxUses;
  tickN(t.s, 1);
  t.s.menu()->selectTrade(0);
  CHECK(t.s.menu()->tradeOutput().empty());
}

TEST_CASE("Comercio: una cría o un aldeano ocupado no comercian, y el aldeano se queda quieto mientras") {
  Shop t;
  const u32 id = t.villager->id;
  Mob* baby = t.s.spawnMob(MobType::Villager, {3.5, 64.0, 2.0});
  baby->growth = -kBabyTicks;
  CHECK_FALSE(t.s.openTrading(*baby));
  t.villager = t.s.mobById(id);  // (la lista pudo moverse al añadir otro)
  t.s.player().inventory.slot(0) = ItemStack(ItemId::wheat, 64);
  REQUIRE(t.s.openTrading(*t.villager));
  t.villager->noAI = false;
  const glm::dvec3 at = t.villager->pos;
  tickN(t.s, 60);
  CHECK(glm::length(t.villager->pos - at) < 0.2);
}

TEST_CASE("Guardado: el aldeano conserva su oficio, su nivel y sus ofertas con los usos") {
  Shop t;
  t.villager->offers[1].uses = 3;
  t.villager->offers[1].maxUses = 9;
  t.villager->careerTier = 1;
  t.villager->health = 20;
  const auto back = save::mobFromNbt(save::mobToNbt(*t.villager));
  REQUIRE(back);
  CHECK(back->career == static_cast<u8>(Career::Farmer));
  CHECK(back->careerTier == 1);
  REQUIRE(back->offers.size() == t.villager->offers.size());
  CHECK(back->offers == t.villager->offers);
  CHECK(back->offers[1].uses == 3);
  CHECK(back->offers[1].maxUses == 9);
  // Un herrero (profesión 3) con su oficio de armero
  Mob smith;
  smith.type = MobType::Villager;
  smith.variant = 3;
  smith.health = 20;
  smith.career = static_cast<u8>(Career::Toolsmith);
  smith.careerTier = 2;
  Random rng(1);
  addCareerOffers(smith.offers, Career::Toolsmith, 1, rng);
  const auto s2 = save::mobFromNbt(save::mobToNbt(smith));
  REQUIRE(s2);
  CHECK(s2->career == static_cast<u8>(Career::Toolsmith));
  CHECK(s2->offers == smith.offers);
}
