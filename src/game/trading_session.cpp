// Comercio con aldeanos en la partida: oficio y ofertas, la ventana, contar los tratos y renovar las ofertas.
#include <algorithm>

#include "game/session.h"

namespace mcw {

void GameSession::ensureTrades(Mob& m) {
  if (m.type != MobType::Villager || m.career != 0xFF) return;
  const int profession = m.variant % 5;
  const int n = careerCount(profession);
  const Career c = careerOf(profession, n > 1 ? rng_.nextInt(n) : 0);
  m.career = static_cast<u8>(c);
  m.careerTier = 1;
  m.offers.clear();
  addCareerOffers(m.offers, c, 1, rng_);
}

bool GameSession::openTrading(Mob& m) {
  if (m.type != MobType::Villager || m.dying() || m.baby() || m.tradingWith != 0 || menu_) return false;
  ensureTrades(m);
  if (m.offers.empty()) return false;
  menu_ = std::make_unique<Menu>(MenuKind::Trade, player_);
  menu_->setTitle(careerNameEs(static_cast<Career>(m.career)));
  menu_->setTrades(m.offers);
  menu_->selectTrade(0);
  tradingMob_ = m.id;
  m.tradingWith = player_.uid;
  m.walkTarget.reset();
  return true;
}

void GameSession::applyTrades(Mob& m, const std::vector<int>& log) {
  for (const int i : log) {
    if (i < 0 || i >= static_cast<int>(m.offers.size())) continue;
    TradeOffer& o = m.offers[static_cast<std::size_t>(i)];
    const bool first = o.uses == 0;
    o.uses++;
    // 3 a 6 puntos de experiencia por trato
    spawnXp(m.pos + glm::dvec3(0, 0.5, 0), 3 + rng_.nextInt(4));
    SessionEvent e{SessionEvent::Type::Traded, glm::ivec3(glm::floor(m.pos)), 0};
    e.where = m.pos + glm::dvec3(0, 1.6, 0);
    e.mob = m.type;
    e.value = 1;
    events_.push_back(e);
    // La primera vez que se acepta una oferta (y una de cada cinco después) se renuevan las ofertas pasados dos segundos
    if (first || rng_.nextInt(5) == 0) {
      m.tradeReset = 40;
      m.tradeUnlock = true;
    }
  }
}

void GameSession::tickTrading() {
  if (remote_) return;  // en un servidor lo cuenta él
  // Renovación de ofertas de todos los aldeanos
  for (Mob& m : mobs_) {
    if (m.type == MobType::Villager && m.tradingWith != 0 && m.tradingWith != player_.uid && --m.tradeKeep <= 0) m.tradingWith = 0;  // (un invitado que se fue)
    if (m.type != MobType::Villager || m.tradeReset <= 0 || --m.tradeReset > 0) continue;
    if (m.tradeUnlock && m.career != 0xFF && m.careerTier < careerTiers(static_cast<Career>(m.career))) {
      m.careerTier++;
      addCareerOffers(m.offers, static_cast<Career>(m.career), m.careerTier, rng_);
    }
    for (TradeOffer& o : m.offers)  // las agotadas vuelven a ofrecerse con más usos
      if (o.disabled()) o.maxUses += 2 + rng_.nextInt(6) + rng_.nextInt(6);
    m.tradeUnlock = false;
  }
  // La ventana del jugador de esta partida
  if (!menu_ || menu_->kind() != MenuKind::Trade) return;
  Mob* m = mobById(tradingMob_);
  if (!m || m->dying() || glm::distance(m->pos, player_.pos) > 10.0) {
    closeMenu();
    return;
  }
  if (const std::vector<int> log = menu_->takeTradeLog(); !log.empty()) applyTrades(*m, log);
  if (!(m->offers == menu_->trades())) menu_->setTrades(m->offers);
}

}  // namespace mcw
