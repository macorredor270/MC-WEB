#pragma once
#include "core/types.h"

namespace mcw {

/// De dónde viene un daño: decide qué encantamientos de armadura lo reducen (Protección contra el fuego, Caída de
/// pluma...) y si la armadura cuenta.
enum class DamageKind : u8 { Generic, Melee, Projectile, Explosion, Fall, Fire, Drowning, Starvation, Void };

}  // namespace mcw
