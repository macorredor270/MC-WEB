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

/// Pantalla de opciones: deslizadores y botones en dos columnas, como la de vídeo de 1.8.
enum class OptionId { RenderDistance, Fov, Brightness, Sensitivity, Clouds, ViewBobbing, ShowFps, GuiScale, Done };
struct OptionWidget {
  OptionId id;
  float x, y, w;
  bool slider;
};
std::vector<OptionWidget> optionsLayout(const Ui& ui);
/// Widget bajo un punto (índice en optionsLayout) o -1.
int optionAt(const std::vector<OptionWidget>& layout, float x, float y);
/// Posición 0..1 de un deslizador para una x de GUI.
float sliderValueAt(const OptionWidget& w, float x);
/// Valor 0..1 del deslizador de una opción.
float optionSliderValue(const Settings& s, OptionId id);
void drawOptions(Ui& ui, const Settings& s, float mouseX, float mouseY);

/// Pantalla de muerte. Devuelve true si el ratón está sobre "Reaparecer".
bool drawDeathScreen(Ui& ui, float mouseX, float mouseY);
bool deathButtonAt(const Ui& ui, float x, float y);

}  // namespace mcw
