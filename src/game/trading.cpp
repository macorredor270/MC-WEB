#include "game/trading.h"

#include <algorithm>
#include <utility>

#include "data/blocks.h"
#include "data/items.h"
#include "game/enchanting.h"
#include "game/enchantments.h"

namespace mcw {

namespace {

ItemStack emeralds(int n) { return ItemStack(ItemId::emerald, n); }

/// El jugador vende `item` (de `lo` a `hi` unidades) por una esmeralda.
TradeOffer buys(const ItemStack& item, int lo, int hi, Random& rng) {
  TradeOffer o;
  o.buy = item;
  o.buy.count = static_cast<i16>(rng.range(lo, hi));
  o.sell = emeralds(1);
  return o;
}

/// El jugador compra `item` (de `lo` a `hi` esmeraldas) por `count` unidades.
TradeOffer sells(const ItemStack& item, int count, int lo, int hi, Random& rng) {
  TradeOffer o;
  o.buy = emeralds(rng.range(lo, hi));
  o.sell = item;
  o.sell.count = static_cast<i16>(count);
  return o;
}

/// Un objeto encantado como lo hacía el comerciante de 1.8: nivel de encantamiento de 5 a 19.
ItemStack enchanted(ItemStack item, Random& rng) {
  const int level = rng.range(5, 19);
  return applyEnchants(item, enchantList(item, 0, level, static_cast<i32>(rng.nextU32())));
}

/// Un libro con un único encantamiento al azar (todos con la misma probabilidad y luego todos sus niveles) y lo que cuesta.
TradeOffer enchantedBook(Random& rng) {
  const auto& all = allEnchantments();
  const EnchantInfo& e = all[static_cast<std::size_t>(rng.nextInt(static_cast<int>(all.size())))];
  const int level = rng.range(1, e.maxLevel);
  const int price = std::min(64, 2 + rng.nextInt(5 + level * 10) + 3 * level);
  TradeOffer o;
  o.buy = emeralds(price);
  o.buy2 = ItemStack(ItemId::book);
  o.sell = applyEnchants(ItemStack(ItemId::book), {{e.id, level}});
  return o;
}

}  // namespace

int careerCount(int profession) {
  switch (profession) {
    case 0: return 4;
    case 3: return 3;
    case 4: return 2;
    default: return 1;
  }
}

Career careerOf(int profession, int index) {
  switch (profession) {
    case 0: return static_cast<Career>(std::clamp(index, 0, 3));
    case 1: return Career::Librarian;
    case 2: return Career::Cleric;
    case 3: return static_cast<Career>(static_cast<int>(Career::Armorer) + std::clamp(index, 0, 2));
    default: return static_cast<Career>(static_cast<int>(Career::Butcher) + std::clamp(index, 0, 1));
  }
}

const char* careerNameEs(Career c) {
  switch (c) {
    case Career::Farmer: return "Granjero";
    case Career::Fisherman: return "Pescador";
    case Career::Shepherd: return "Pastor";
    case Career::Fletcher: return "Flechero";
    case Career::Librarian: return "Bibliotecario";
    case Career::Cleric: return "Clerigo";
    case Career::Armorer: return "Armero";
    case Career::Weaponsmith: return "Armero de armas";
    case Career::Toolsmith: return "Herrero de herramientas";
    case Career::Butcher: return "Carnicero";
    case Career::Leatherworker: return "Peletero";
  }
  return "Aldeano";
}

int careerTiers(Career c) {
  switch (c) {
    case Career::Farmer: return 4;
    case Career::Fisherman: return 2;
    case Career::Shepherd: return 2;
    case Career::Fletcher: return 2;
    case Career::Librarian: return 6;
    case Career::Cleric: return 4;
    case Career::Armorer: return 4;
    case Career::Weaponsmith: return 3;
    case Career::Toolsmith: return 3;
    case Career::Butcher: return 2;
    case Career::Leatherworker: return 3;
  }
  return 1;
}

void addCareerOffers(std::vector<TradeOffer>& out, Career c, int tier, Random& rng) {
  auto add = [&](TradeOffer o) { out.push_back(std::move(o)); };
  switch (c) {
    case Career::Farmer:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::wheat), 18, 22, rng));
        add(buys(ItemStack(ItemId::potato), 15, 19, rng));
        add(buys(ItemStack(ItemId::carrot), 15, 19, rng));
        TradeOffer b;
        b.buy = emeralds(1);
        b.sell = ItemStack(ItemId::bread, rng.range(2, 4));
        add(b);
      } else if (tier == 2) {
        add(buys(ItemStack(B::pumpkin), 8, 13, rng));
        TradeOffer p;
        p.buy = emeralds(1);
        p.sell = ItemStack(ItemId::pumpkin_pie, rng.range(2, 3));
        add(p);
      } else if (tier == 3) {
        add(buys(ItemStack(B::melon_block), 7, 12, rng));
        TradeOffer a;
        a.buy = emeralds(1);
        a.sell = ItemStack(ItemId::apple, rng.range(5, 7));
        add(a);
      } else if (tier == 4) {
        TradeOffer k;
        k.buy = emeralds(1);
        k.sell = ItemStack(ItemId::cookie, rng.range(6, 10));
        add(k);
        TradeOffer cake;
        cake.buy = emeralds(1);
        cake.sell = ItemStack(ItemId::cake);
        add(cake);
      }
      break;
    case Career::Fisherman:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::string), 15, 20, rng));
        add(buys(ItemStack(ItemId::coal), 16, 24, rng));
        TradeOffer f;  // seis pescados crudos y una esmeralda por seis cocinados
        f.buy = emeralds(1);
        f.buy2 = ItemStack(ItemId::fish, 6);
        f.sell = ItemStack(ItemId::cooked_fish, 6);
        add(f);
      } else if (tier == 2) {
        TradeOffer r;
        r.buy = emeralds(rng.range(7, 8));
        r.sell = enchanted(ItemStack(ItemId::fishing_rod), rng);
        add(r);
      }
      break;
    case Career::Shepherd:
      if (tier == 1) {
        add(buys(ItemStack(B::wool, 1, 0), 16, 22, rng));
        add(sells(ItemStack(ItemId::shears), 1, 3, 4, rng));
      } else if (tier == 2) {
        const int price = rng.range(1, 2);  // (el mismo precio para todos los colores)
        for (int color = 0; color < 16; color++) {
          TradeOffer w;
          w.buy = emeralds(price);
          w.sell = ItemStack(B::wool, 1, color);
          add(w);
        }
      }
      break;
    case Career::Fletcher:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::string), 15, 20, rng));
        TradeOffer a;
        a.buy = emeralds(1);
        a.sell = ItemStack(ItemId::arrow, rng.range(8, 12));
        add(a);
      } else if (tier == 2) {
        add(sells(ItemStack(ItemId::bow), 1, 2, 3, rng));
        TradeOffer f;
        f.buy = emeralds(1);
        f.buy2 = ItemStack(B::gravel, 10);
        f.sell = ItemStack(ItemId::flint, rng.range(6, 10));
        add(f);
      }
      break;
    case Career::Librarian:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::paper), 24, 36, rng));
        add(enchantedBook(rng));
      } else if (tier == 2) {
        add(buys(ItemStack(ItemId::book), 8, 10, rng));
        add(sells(ItemStack(ItemId::compass), 1, 10, 12, rng));
        add(sells(ItemStack(B::bookshelf), 1, 3, 4, rng));
      } else if (tier == 3) {
        add(buys(ItemStack(ItemId::written_book), 2, 2, rng));
        add(sells(ItemStack(ItemId::clock), 1, 10, 12, rng));
        TradeOffer g;
        g.buy = emeralds(1);
        g.sell = ItemStack(B::glass, rng.range(3, 5));
        add(g);
      } else if (tier == 4 || tier == 5) {
        add(enchantedBook(rng));
      } else if (tier == 6) {
        add(sells(ItemStack(ItemId::name_tag), 1, 20, 22, rng));
      }
      break;
    case Career::Cleric:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::rotten_flesh), 36, 40, rng));
        add(buys(ItemStack(ItemId::gold_ingot), 8, 10, rng));
      } else if (tier == 2) {
        TradeOffer r;
        r.buy = emeralds(1);
        r.sell = ItemStack(ItemId::redstone, rng.range(1, 4));
        add(r);
        TradeOffer l;
        l.buy = emeralds(1);
        l.sell = ItemStack(ItemId::dye, rng.range(1, 2), 4);
        add(l);
      } else if (tier == 3) {
        add(sells(ItemStack(ItemId::ender_pearl), 1, 4, 7, rng));
        TradeOffer g;
        g.buy = emeralds(1);
        g.sell = ItemStack(B::glowstone, rng.range(1, 3));
        add(g);
      } else if (tier == 4) {
        add(sells(ItemStack(ItemId::experience_bottle), 1, 3, 11, rng));
      }
      break;
    case Career::Armorer:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::coal), 16, 24, rng));
        add(sells(ItemStack(ItemId::iron_helmet), 1, 4, 6, rng));
      } else if (tier == 2) {
        add(buys(ItemStack(ItemId::iron_ingot), 7, 9, rng));
        add(sells(ItemStack(ItemId::iron_chestplate), 1, 10, 14, rng));
      } else if (tier == 3) {
        add(buys(ItemStack(ItemId::diamond), 3, 4, rng));
        TradeOffer d;
        d.buy = emeralds(rng.range(16, 19));
        d.sell = enchanted(ItemStack(ItemId::diamond_chestplate), rng);
        add(d);
      } else if (tier == 4) {
        add(sells(ItemStack(ItemId::chainmail_boots), 1, 5, 7, rng));
        add(sells(ItemStack(ItemId::chainmail_leggings), 1, 9, 11, rng));
        add(sells(ItemStack(ItemId::chainmail_helmet), 1, 5, 7, rng));
        add(sells(ItemStack(ItemId::chainmail_chestplate), 1, 11, 15, rng));
      }
      break;
    case Career::Weaponsmith:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::coal), 16, 24, rng));
        add(sells(ItemStack(ItemId::iron_axe), 1, 6, 8, rng));
      } else if (tier == 2) {
        add(buys(ItemStack(ItemId::iron_ingot), 7, 9, rng));
        TradeOffer s;
        s.buy = emeralds(rng.range(9, 10));
        s.sell = enchanted(ItemStack(ItemId::iron_sword), rng);
        add(s);
      } else if (tier == 3) {
        add(buys(ItemStack(ItemId::diamond), 3, 4, rng));
        TradeOffer s;
        s.buy = emeralds(rng.range(12, 15));
        s.sell = enchanted(ItemStack(ItemId::diamond_sword), rng);
        add(s);
        TradeOffer a;
        a.buy = emeralds(rng.range(9, 12));
        a.sell = enchanted(ItemStack(ItemId::diamond_axe), rng);
        add(a);
      }
      break;
    case Career::Toolsmith:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::coal), 16, 24, rng));
        TradeOffer s;
        s.buy = emeralds(rng.range(5, 7));
        s.sell = enchanted(ItemStack(ItemId::iron_shovel), rng);
        add(s);
      } else if (tier == 2) {
        add(buys(ItemStack(ItemId::iron_ingot), 7, 9, rng));
        TradeOffer p;
        p.buy = emeralds(rng.range(9, 11));
        p.sell = enchanted(ItemStack(ItemId::iron_pickaxe), rng);
        add(p);
      } else if (tier == 3) {
        add(buys(ItemStack(ItemId::diamond), 3, 4, rng));
        TradeOffer p;
        p.buy = emeralds(rng.range(12, 15));
        p.sell = enchanted(ItemStack(ItemId::diamond_pickaxe), rng);
        add(p);
      }
      break;
    case Career::Butcher:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::porkchop), 14, 18, rng));
        add(buys(ItemStack(ItemId::chicken), 14, 18, rng));
      } else if (tier == 2) {
        add(buys(ItemStack(ItemId::coal), 16, 24, rng));
        TradeOffer p;
        p.buy = emeralds(1);
        p.sell = ItemStack(ItemId::cooked_porkchop, rng.range(5, 7));
        add(p);
        TradeOffer c2;
        c2.buy = emeralds(1);
        c2.sell = ItemStack(ItemId::cooked_chicken, rng.range(6, 8));
        add(c2);
      }
      break;
    case Career::Leatherworker:
      if (tier == 1) {
        add(buys(ItemStack(ItemId::leather), 9, 12, rng));
        add(sells(ItemStack(ItemId::leather_leggings), 1, 2, 4, rng));
      } else if (tier == 2) {
        TradeOffer t;
        t.buy = emeralds(rng.range(7, 12));
        t.sell = enchanted(ItemStack(ItemId::leather_chestplate), rng);
        add(t);
      } else if (tier == 3) {
        add(sells(ItemStack(ItemId::saddle), 1, 8, 10, rng));
      }
      break;
  }
}

bool offerSatisfied(const TradeOffer& o, const ItemStack& slotA, const ItemStack& slotB) {
  auto covers = [](const ItemStack& need, const ItemStack& have) {
    return need.empty() || (have.id == need.id && have.meta == need.meta && have.count >= need.count);
  };
  if (o.buy.empty()) return false;
  // Las dos casillas valen en cualquier orden (como en 1.8)
  return (covers(o.buy, slotA) && covers(o.buy2, slotB)) || (covers(o.buy, slotB) && covers(o.buy2, slotA));
}

}  // namespace mcw
