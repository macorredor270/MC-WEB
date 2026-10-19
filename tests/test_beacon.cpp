#include <doctest/doctest.h>

#include <ostream>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/beacon.h"
#include "game/effects.h"
#include "game/menu.h"
#include "game/session.h"
#include "save/anvil.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {

constexpr int kY = 70;  // el faro está en (0, 70, 0), con aire por encima y la pirámide debajo

/// Pone `levels` capas de pirámide de `base` bajo el faro (la de nivel 1 en y-1 es de 3x3, la siguiente de 5x5...).
void buildPyramid(FlatTestWorld& fw, int levels, int base = B::iron_block) {
  for (int l = 1; l <= levels; l++)
    for (int dz = -l; dz <= l; dz++)
      for (int dx = -l; dx <= l; dx++) fw.setBlock(dx, kY - l, dz, makeState(base));
}

struct Lighthouse {
  FlatTestWorld fw;
  GameSession s{fw, 3};
  Lighthouse(int levels = 1) {
    s.setRules({2, true, true});
    s.setMode(GameMode::Survival);
    s.player().pos = s.player().prevPos = {0.5, 71, 3.5};
    fw.setBlock(0, kY, 0, makeState(B::beacon));
    buildPyramid(fw, levels);
  }
};

}  // namespace

TEST_CASE("Faro: la pirámide cuenta niveles completos y solo de bloques de metal") {
  FlatTestWorld fw;
  fw.setBlock(0, kY, 0, makeState(B::beacon));
  CHECK(beaconPyramidLevels(fw.world(), 0, kY, 0) == 0);
  for (int levels = 1; levels <= 4; levels++) {
    buildPyramid(fw, levels, levels % 2 ? B::iron_block : B::diamond_block);  // se pueden mezclar los metales
    CHECK(beaconPyramidLevels(fw.world(), 0, kY, 0) == levels);
  }
  fw.setBlock(2, kY - 2, -1, makeState(B::stone));  // un hueco en la capa 2: se queda en 1
  CHECK(beaconPyramidLevels(fw.world(), 0, kY, 0) == 1);
}

TEST_CASE("Faro: el cielo despejado deja pasar cristal y hojas, no piedra") {
  FlatTestWorld fw;
  fw.setBlock(0, kY, 0, makeState(B::beacon));
  CHECK(beaconSkyClear(fw.world(), 0, kY, 0));
  fw.setBlock(0, kY + 5, 0, makeState(B::glass));
  CHECK(beaconSkyClear(fw.world(), 0, kY, 0));
  fw.setBlock(0, kY + 9, 0, makeState(B::stone));
  CHECK_FALSE(beaconSkyClear(fw.world(), 0, kY, 0));
}

TEST_CASE("Faro: el rayo se tiñe con los cristales de colores de encima") {
  FlatTestWorld fw;
  fw.setBlock(0, kY, 0, makeState(B::beacon));
  CHECK(beaconBeamColors(fw.world(), 0, kY, 0).empty());
  fw.setBlock(0, kY + 1, 0, makeState(B::stained_glass, 14));  // rojo
  fw.setBlock(0, kY + 2, 0, makeState(B::stained_glass, 11));  // azul
  const auto colors = beaconBeamColors(fw.world(), 0, kY, 0);
  REQUIRE(colors.size() == 2);
  CHECK(colors[0] != colors[1]);
}

TEST_CASE("Faro: qué poderes se pueden elegir según los niveles") {
  CHECK(beaconPrimaryAllowed(fx::Speed, 1));
  CHECK(beaconPrimaryAllowed(fx::Haste, 1));
  CHECK_FALSE(beaconPrimaryAllowed(fx::Resistance, 1));
  CHECK(beaconPrimaryAllowed(fx::Resistance, 2));
  CHECK(beaconPrimaryAllowed(fx::JumpBoost, 2));
  CHECK_FALSE(beaconPrimaryAllowed(fx::Strength, 2));
  CHECK(beaconPrimaryAllowed(fx::Strength, 3));
  CHECK_FALSE(beaconPrimaryAllowed(fx::Regeneration, 4));  // la regeneración solo es secundaria
  CHECK_FALSE(beaconPrimaryAllowed(0, 4));
  CHECK_FALSE(beaconSecondaryAllowed(fx::Regeneration, fx::Speed, 3));  // el secundario pide los 4 niveles
  CHECK(beaconSecondaryAllowed(fx::Regeneration, fx::Speed, 4));
  CHECK(beaconSecondaryAllowed(fx::Speed, fx::Speed, 4));  // repetir el principal
  CHECK_FALSE(beaconSecondaryAllowed(fx::Haste, fx::Speed, 4));
}

TEST_CASE("Faro: solo valen como pago lingotes de hierro y de oro, esmeralda y diamante") {
  CHECK(beaconPayment(ItemId::iron_ingot));
  CHECK(beaconPayment(ItemId::gold_ingot));
  CHECK(beaconPayment(ItemId::emerald));
  CHECK(beaconPayment(ItemId::diamond));
  CHECK_FALSE(beaconPayment(ItemId::stick));
}

TEST_CASE("Faro: confirmar valida, cobra un objeto y guarda los poderes") {
  Lighthouse l(1);
  const glm::ivec3 pos(0, kY, 0);
  ItemStack pay(ItemId::iron_ingot, 3);
  CHECK_FALSE(l.s.confirmBeacon(pos, fx::Resistance, 0, pay, false));  // con 1 nivel no hay resistencia
  CHECK(pay.count == 3);
  ItemStack nothing;
  CHECK_FALSE(l.s.confirmBeacon(pos, fx::Speed, 0, nothing, false));  // sin pago
  ItemStack junk(ItemId::stick, 1);
  CHECK_FALSE(l.s.confirmBeacon(pos, fx::Speed, 0, junk, false));
  CHECK(l.s.confirmBeacon(pos, fx::Speed, 0, pay, false));
  CHECK(pay.count == 2);
  REQUIRE(l.s.tiles().beacons.count({0, kY, 0}) == 1);
  CHECK(l.s.tiles().beacons.at({0, kY, 0}) == BeaconData{fx::Speed, 0});
  // En creativo no cuesta nada
  ItemStack empty;
  CHECK(l.s.confirmBeacon(pos, fx::Haste, 0, empty, true));
  CHECK(l.s.tiles().beacons.at({0, kY, 0}).primary == fx::Haste);
}

TEST_CASE("Faro: sin cielo despejado no se puede confirmar") {
  Lighthouse l(1);
  l.fw.setBlock(0, kY + 4, 0, makeState(B::stone));
  ItemStack pay(ItemId::iron_ingot, 1);
  CHECK_FALSE(l.s.confirmBeacon({0, kY, 0}, fx::Speed, 0, pay, false));
  CHECK(pay.count == 1);
}

TEST_CASE("Faro: cada 80 ticks da sus efectos a quien esté dentro del radio y no a quien esté lejos") {
  Lighthouse l(2);  // radio 30
  ItemStack pay(ItemId::emerald, 1);
  REQUIRE(l.s.confirmBeacon({0, kY, 0}, fx::Resistance, 0, pay, false));
  CHECK_FALSE(l.s.player().effects.has(fx::Resistance));
  for (int i = 0; i < 80; i++) l.s.tick(testing::idleTick());
  CHECK(l.s.player().effects.has(fx::Resistance));
  // Fuera del radio (31 bloques) no llega
  l.s.player().effects.remove(fx::Resistance);
  l.s.player().pos = l.s.player().prevPos = {0.5, 71, 31.5};
  for (int i = 0; i < 80; i++) l.s.tick(testing::idleTick());
  CHECK_FALSE(l.s.player().effects.has(fx::Resistance));
}

TEST_CASE("Faro: con cuatro niveles y los dos poderes se logra el Faro completo") {
  Lighthouse l(4);
  for (int a = 0; a < static_cast<int>(Ach::FullBeacon); a++) l.s.achievements().award(static_cast<Ach>(a));  // (el logro pide antes matar al Wither)
  ItemStack pay(ItemId::diamond, 1);
  REQUIRE(l.s.confirmBeacon({0, kY, 0}, fx::Speed, fx::Regeneration, pay, false));
  CHECK_FALSE(l.s.achievements().has(Ach::FullBeacon));
  for (int i = 0; i < 80; i++) l.s.tick(testing::idleTick());
  CHECK(l.s.achievements().has(Ach::FullBeacon));
  CHECK(l.s.player().effects.has(fx::Speed));
  CHECK(l.s.player().effects.has(fx::Regeneration));
}

TEST_CASE("Faro: si se rompe el bloque, el faro deja de dar efectos y se olvida su dato") {
  Lighthouse l(1);
  ItemStack pay(ItemId::iron_ingot, 1);
  REQUIRE(l.s.confirmBeacon({0, kY, 0}, fx::Speed, 0, pay, false));
  TileEntities& te = l.s.tiles();
  te.blockSet({0, kY, 0}, B::stone);
  CHECK(te.beacons.empty());
}

TEST_CASE("Faro: ida y vuelta por NBT") {
  const BeaconData b{fx::Haste, fx::Regeneration};
  const auto back = save::beaconFromNbt(save::beaconToNbt(4, 70, -9, b));
  REQUIRE(back);
  CHECK(back->first == glm::ivec3(4, 70, -9));
  CHECK(back->second == b);
  CHECK_FALSE(save::beaconFromNbt(save::skullToNbt(0, 0, 0, SkullData{})));  // no confunde otras entidades
}

TEST_CASE("Faro: la ventana acepta un objeto de pago, lo devuelve al cerrar y no deja meter otros") {
  Lighthouse l(1);
  Player& p = l.s.player();
  p.inventory.slot(0) = ItemStack(ItemId::iron_ingot, 5);
  p.inventory.slot(1) = ItemStack(ItemId::stick, 5);
  Menu m(MenuKind::Beacon, p);
  REQUIRE(m.slots()[0].role == SlotRole::BeaconPay);
  const int hotbar = static_cast<int>(m.slots().size()) - 9;  // la barra rápida son las últimas 9 casillas
  m.click(hotbar + 1, 0, true);  // el palo, con shift, no va al pago
  CHECK(m.beaconPay().empty());
  m.click(hotbar + 0, 0, true);  // el lingote sí, de uno en uno
  CHECK(m.beaconPay().id == ItemId::iron_ingot);
  CHECK(m.beaconPay().count == 1);
  CHECK(p.inventory.slot(0).count == 4);
  std::vector<ItemStack> dropped;
  m.close(dropped);
  CHECK(m.beaconPay().empty());
  CHECK(dropped.empty());
  CHECK(p.inventory.slot(0).count == 5);  // volvió al inventario
}

TEST_CASE("Faro: la ventana sin poder elegido no se confirma y se cierra sin más") {
  Lighthouse l(3);
  l.s.player().inventory.slot(0) = ItemStack(ItemId::gold_ingot, 2);
  l.s.openMenu(std::make_unique<Menu>(MenuKind::Beacon, l.s.player()));
  REQUIRE(l.s.menu());
  CHECK(l.s.menu()->kind() == MenuKind::Beacon);
  CHECK(l.s.menu()->width() == 230);
  CHECK(l.s.menu()->beaconPay().empty());
  CHECK_FALSE(l.s.confirmOpenBeacon());  // sin poder elegido ni pago
  l.s.closeMenu();
  CHECK_FALSE(l.s.menu());
}
