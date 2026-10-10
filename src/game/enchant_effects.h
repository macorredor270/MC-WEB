#pragma once
#include "core/random.h"
#include "game/damage_kind.h"
#include "game/inventory.h"
#include "game/item_stack.h"
#include "game/mob.h"

namespace mcw {

/// Lo que hacen los encantamientos de 1.8 (las cuentas, sin tocar el estado de la partida: la partida las usa y las
/// pruebas las comprueban sueltas).
namespace enchfx {

// --- Armadura ---------------------------------------------------------------------------------------

/// Puntos de protección de un encantamiento de armadura contra ese daño: floor((6 + nivel²) / 3 × factor), con factor
/// 0,75 la Protección (todo menos vacío), 1,25 contra el fuego, 2,5 la Caída de pluma, 1,5 contra explosiones y proyectiles.
int protectionPoints(int enchId, int level, DamageKind kind);
/// Suma de las cuatro piezas, con tope 25 y el azar de 1.8 (de la mitad a todo), y recortada a 20.
int protectionModifier(const PlayerInventory& inv, DamageKind kind, Random& rng);
/// Daño que queda tras la protección: daño × (25 - modificador) / 25.
inline float afterProtection(float damage, int modifier) { return modifier > 0 ? damage * static_cast<float>(25 - modifier) / 25.0f : damage; }
/// ¿Este daño se salta la armadura (puntos de defensa)? Caídas, ahogo, hambre, vacío y fuego de estar ardiendo, sí.
bool bypassesArmor(DamageKind kind);
/// ¿Se salta también los encantamientos? Solo el hambre y el vacío.
bool bypassesEnchantments(DamageKind kind);
/// Espinas: nivel que devuelve el golpe y cuánto daño hace (1 a 4; con nivel mayor de 10, nivel - 10), o 0 si esta vez no salta.
int thornsDamage(int level, Random& rng);

// --- Armas ------------------------------------------------------------------------------------------

/// Daño de más del arma contra esa criatura: Filo 1,25 por nivel; Pesadez 2,5 por nivel contra no muertos y
/// Perdición de los artrópodos 2,5 por nivel contra arañas.
float weaponBonus(const ItemStack& weapon, MobType target);
bool isUndead(MobType t);
bool isArthropod(MobType t);

// --- Herramientas -----------------------------------------------------------------------------------

/// Velocidad de rotura con Eficiencia: si la herramienta ya acelera (base > 1), suma nivel² + 1.
float efficiencySpeed(float baseSpeed, const ItemStack& tool);
/// Desgaste que se aplica de verdad tras Irrompibilidad: cada punto se salva con probabilidad nivel / (nivel + 1)
/// (en armaduras, solo el 40 % de las veces cuenta ese intento).
int wearAfterUnbreaking(const ItemStack& item, int amount, Random& rng);
/// Desgasta el objeto (con Irrompibilidad); true si se ha roto (queda vacío).
bool wearItem(ItemStack& item, int amount, Random& rng);

/// Lo que suelta con Toque de seda un bloque (vacío si ese bloque no se puede recoger así).
ItemStack silkTouchDrop(BlockState s);
/// Cantidad que suelta un mineral con Fortuna: la normal × (1 + hasta nivel), o + hasta nivel en los que sueltan varios.
int fortuneCount(int blockId, int baseCount, int level, Random& rng);

}  // namespace enchfx
}  // namespace mcw
