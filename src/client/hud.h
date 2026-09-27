#pragma once
#include <functional>
#include <string>
#include <vector>

#include "client/settings.h"
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
/// `preview` (opcional) dibuja algo sobre el fondo antes que los objetos (el jugador en el inventario);
/// recibe la esquina de la ventana en píxeles de GUI.
int drawMenu(Ui& ui, ItemRenderer& items, const Menu& menu, const Player& player, float mouseX, float mouseY,
             const std::function<void(float left, float top)>& preview = {});
/// Casilla bajo un punto (en píxeles de GUI), -1 si ninguna; `inside` = el punto cae dentro de la ventana.
int menuSlotAt(const Ui& ui, const Menu& menu, float x, float y, bool& inside);

/// Menú de pausa. Devuelve el botón bajo el ratón: 0 volver, 1 modo, 2 opciones, 3 hora, 4 salir (o -1).
int drawPauseMenu(Ui& ui, float mouseX, float mouseY, bool creative, bool canQuit);
int pauseButtonAt(const Ui& ui, float x, float y, bool canQuit);

/// Elemento de una lista de ajustes: botón, deslizador o título de sección.
struct OptionItem {
  enum class Type { Button, Slider, Header };
  Type type = Type::Button;
  std::function<std::string()> text;  // lo que pone (p. ej. "Distancia: 12 chunks")
  std::function<float()> get;         // deslizador: valor 0..1
  std::function<void(float)> set;     // deslizador: nuevo valor 0..1
  std::function<void()> press;        // botón
  bool wide = false;                  // ocupa la fila entera
  bool enabled = true;
};

/// Dónde cae cada elemento con el desplazamiento actual (solo los que se ven enteros).
struct OptionListLayout {
  struct Rect {
    int item;
    float x, y, w;
  };
  std::vector<Rect> rects;
  float top = 0, bottom = 0, maxScroll = 0, scroll = 0, doneX = 0, doneY = 0;
};
OptionListLayout layoutOptionList(const Ui& ui, const std::vector<OptionItem>& items, float scroll);
/// Elemento bajo un punto: índice, -2 = botón de abajo ("Listo"), -1 = nada.
int optionListHit(const OptionListLayout& l, const std::vector<OptionItem>& items, float x, float y);
/// Valor 0..1 de un deslizador para una x de GUI.
float optionSliderAt(const OptionListLayout& l, int item, float x);
void drawOptionList(Ui& ui, const std::string& title, const std::vector<OptionItem>& items, const OptionListLayout& l, float mouseX,
                    float mouseY, const std::string& doneLabel);

/// Pantalla de muerte. Devuelve true si el ratón está sobre "Reaparecer".
bool drawDeathScreen(Ui& ui, float mouseX, float mouseY);
bool deathButtonAt(const Ui& ui, float x, float y);

}  // namespace mcw
