#include <doctest/doctest.h>

#include <ostream>

#include "data/items.h"
#include "flat_world.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

TEST_CASE("Logros: el de un invitado va a sus logros y avisa al servidor, sin tocar los del anfitrión ni sacar su ventanita") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  Player guest;
  guest.uid = 7;
  GameSession::ActionState st;
  std::vector<std::pair<u32, Ach>> told;
  s.setGuestAchievementListener([&](const Player& p, Ach a) { told.emplace_back(p.uid, a); });
  s.actAs(guest, st, [&] { s.award(Ach::OpenInventory); });
  CHECK(guest.achievements.has(Ach::OpenInventory));
  CHECK_FALSE(s.achievements().has(Ach::OpenInventory));  // (los del anfitrión, aparte)
  REQUIRE(told.size() == 1);
  CHECK(told[0] == std::pair<u32, Ach>(7, Ach::OpenInventory));
  // Un logro del invitado no saca la ventanita del anfitrión; uno del anfitrión, sí
  auto toasted = [&] {
    bool toast = false;
    for (const SessionEvent& e : s.takeEvents()) toast |= e.type == SessionEvent::Type::Achievement;
    return toast;
  };
  CHECK_FALSE(toasted());
  s.award(Ach::OpenInventory);
  CHECK(toasted());
  // Si ya lo tenía no se repite
  s.actAs(guest, st, [&] { s.award(Ach::OpenInventory); });
  CHECK(told.size() == 1);
}

TEST_CASE("Logros: awardTo se lo da a quien toque (el anfitrión o un invitado) y respeta el logro previo") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  Player guest;
  guest.uid = 9;
  s.setOtherPlayers({&guest});
  std::vector<std::pair<u32, Ach>> told;
  s.setGuestAchievementListener([&](const Player& p, Ach a) { told.emplace_back(p.uid, a); });
  s.awardTo(9, Ach::DiamondsToYou);  // sin "Diamantes" antes no se puede
  CHECK_FALSE(guest.achievements.has(Ach::DiamondsToYou));
  s.awardTo(9, Ach::OpenInventory);
  s.awardTo(9, Ach::MineWood);
  CHECK(guest.achievements.has(Ach::MineWood));
  CHECK(told.size() == 2);
  s.awardTo(s.player().uid, Ach::OpenInventory);  // el anfitrión (uid 1)
  CHECK(s.achievements().has(Ach::OpenInventory));
  CHECK(told.size() == 2);  // al anfitrión no se le cuenta por el aviso de invitados
  s.awardTo(1234, Ach::MineWood);  // alguien que no está: nada
  CHECK(told.size() == 2);
}

TEST_CASE("Logros: un cliente de red no concede los suyos (llegan del servidor) y sí los acepta con grantAchievement") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.setRemote(std::make_shared<GameSession::RemoteHooks>());
  s.award(Ach::OpenInventory);
  CHECK_FALSE(s.achievements().has(Ach::OpenInventory));
  s.grantAchievement(Ach::OpenInventory);
  CHECK(s.achievements().has(Ach::OpenInventory));
}

TEST_CASE("Logros: lo que se lanza lleva el nombre de quien lo lanzó") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  s.throwItem(ItemStack(ItemId::diamond, 1));
  s.dropItem({3.5, 64, 3.5}, ItemStack(ItemId::diamond, 1), {0, 0, 0}, 42);
  s.dropItem({6.5, 64, 6.5}, ItemStack(ItemId::diamond, 1), {0, 0, 0});
  REQUIRE(s.items().size() == 3);
  CHECK(s.items()[0].thrower == s.player().uid);
  CHECK(s.items()[1].thrower == 42);
  CHECK(s.items()[2].thrower == 0);
}
