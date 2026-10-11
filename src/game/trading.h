#pragma once
// Comercio con aldeanos: las ofertas de cada oficio, por niveles (como en 1.8: un oficio solo desbloquea el siguiente nivel de
// ofertas cuando alguien le acepta una). Los números salen de minecraft.wiki como referencia de comportamiento; las listas son propias.
#include <vector>

#include "core/random.h"
#include "core/types.h"
#include "game/item_stack.h"

namespace mcw {

/// Una oferta: lo que pide (una o dos pilas) y lo que da.
struct TradeOffer {
  ItemStack buy, buy2, sell;
  int uses = 0, maxUses = 7;
  bool disabled() const { return uses >= maxUses; }
  bool operator==(const TradeOffer&) const = default;
};

/// Los oficios con ofertas (el aldeano tiene un oficio de 0 a 4 y dentro de él uno de estos).
enum class Career : u8 { Farmer, Fisherman, Shepherd, Fletcher, Librarian, Cleric, Armorer, Weaponsmith, Toolsmith, Butcher, Leatherworker };

/// Oficios de un aldeano de la profesión 0 (granjero), 1 (bibliotecario), 2 (clérigo), 3 (herrero) o 4 (carnicero).
int careerCount(int profession);
Career careerOf(int profession, int index);
const char* careerNameEs(Career c);
/// Cuántos niveles de ofertas tiene.
int careerTiers(Career c);
/// Añade las ofertas del nivel `tier` (1 es el primero) con cantidades y precios al azar dentro de sus márgenes.
void addCareerOffers(std::vector<TradeOffer>& out, Career c, int tier, Random& rng);

/// ¿Cubre lo puesto en las casillas lo que pide la oferta? (misma clase de objeto y tinte, y al menos las unidades)
bool offerSatisfied(const TradeOffer& o, const ItemStack& slotA, const ItemStack& slotB);

}  // namespace mcw
