#pragma once
#include <string>

#include "game/menu.h"

namespace mcw {

class Ui;
class ItemRenderer;
class Player;

/// Número y barra de desgaste encima de un icono ya dibujado (en píxeles de GUI).
void drawStackOverlay(Ui& ui, const ItemStack& s, float x, float y);

/// Barra rápida, corazones, comida, burbujas, experiencia y el nombre del objeto seleccionado.
void drawHud(Ui& ui, ItemRenderer& items, const Player& player, float selectedNameAlpha);

/// Ventana de inventario/mesa/horno/creativo. Devuelve el índice de la casilla bajo el ratón (o -1).
int drawMenu(Ui& ui, ItemRenderer& items, const Menu& menu, const Player& player, float mouseX, float mouseY);
/// Casilla bajo un punto (en píxeles de GUI), -1 si ninguna; `inside` = el punto cae dentro de la ventana.
int menuSlotAt(const Ui& ui, const Menu& menu, float x, float y, bool& inside);

/// Menú de pausa. Devuelve el botón bajo el ratón: 0 volver, 1 modo, 2 distancia, 3 hora, 4 salir (o -1).
int drawPauseMenu(Ui& ui, float mouseX, float mouseY, bool creative, int renderDistance, bool canQuit);
int pauseButtonAt(const Ui& ui, float x, float y, bool canQuit);

/// Pantalla de muerte. Devuelve true si el ratón está sobre "Reaparecer".
bool drawDeathScreen(Ui& ui, float mouseX, float mouseY);
bool deathButtonAt(const Ui& ui, float x, float y);

}  // namespace mcw
