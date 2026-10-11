#pragma once
// Metadatos de las criaturas de más (lobo, ocelote, conejo, caballo, aldeano, gólem, murciélago): lo que las distingue de otras de
// su clase y cambia con el tiempo (domada, sentada, pelaje...). Índices de 1.8.
#include <span>
#include <vector>

#include "game/mob.h"
#include "game/trading.h"
#include "net/protocol.h"

namespace mcw::net {

/// Mete en `m` lo que hay que contar de `mob` (nada si no es de las que tienen variantes).
void writeMobVariantMeta(Metadata& m, const Mob& mob);
/// Lo contrario: lee de `m` lo que haya.
void readMobVariantMeta(const Metadata& m, Mob& mob);
/// Una firma de lo contado: cuando cambia, hay que volver a mandarlo.
u32 mobVariantSig(const Mob& mob);


/// El mensaje de plugin `MC|TrList`: ofertas de un aldeano para la ventana `windowId` (formato de 1.8).
std::vector<u8> encodeTradeList(int windowId, const std::vector<TradeOffer>& offers);
/// Lo contrario; nullopt si el mensaje no vale.
bool decodeTradeList(std::span<const u8> body, int& windowId, std::vector<TradeOffer>& offers);

}  // namespace mcw::net
