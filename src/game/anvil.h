#pragma once
#include <string>
#include <utility>
#include <vector>

#include "game/item_stack.h"

namespace mcw {

/// Lo que sale de poner dos objetos en el yunque (y un nombre), con las reglas de 1.8.
struct AnvilResult {
  ItemStack output;      // vacío si no se puede (o es demasiado caro)
  int cost = 0;          // niveles de experiencia que cuesta sacarlo
  int materialUsed = 0;  // unidades que se gastan de la casilla de la derecha al reparar con material (0 = se gasta entera)
  bool tooExpensive = false;  // "¡Demasiado caro!": 40 niveles o más (salvo en creativo)
};

/// Nombre que se ve de un objeto: el que se le puso en el yunque o el de siempre (en español).
std::string anvilDisplayName(const ItemStack& s);
/// ¿Ese material repara ese objeto? (tablones a la madera, adoquín a la piedra, lingote de hierro al hierro y a la cota de
/// mallas, de oro al oro, diamante al diamante y cuero al cuero)
bool anvilRepairsWith(int itemId, int materialId);
/// Encantamientos que lleva un objeto (los guardados, si es un libro).
std::vector<std::pair<int, int>> anvilEnchants(const ItemStack& s);
/// Calcula el resultado de `left` + `right` con el nombre `newName` (vacío = sin nombre puesto). Reparar con material, juntar
/// dos objetos iguales o añadirles los encantamientos de un libro, y renombrar; cada uso encarece el objeto (RepairCost).
AnvilResult anvilCompute(const ItemStack& left, const ItemStack& right, const std::string& newName, bool creative);
/// Desgaste del yunque al usarlo: 0 sin daño, 1 algo dañado, 2 muy dañado; -1 si se rompe del todo (el valor que se guarda
/// en el bloque es `facing | (daño << 2)`).
int anvilNextDamage(int currentDamage);

}  // namespace mcw
