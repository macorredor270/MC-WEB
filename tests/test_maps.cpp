#include <doctest/doctest.h>

#include <ostream>

#include "data/blockstates.h"
#include "data/items.h"
#include "flat_world.h"
#include "game/crafting.h"
#include "game/maps.h"
#include "game/session.h"

using namespace mcw;
using testing::FlatTestWorld;

namespace {
void tickN(GameSession& s, int n) {
  for (int i = 0; i < n; i++) s.tick(testing::idleTick());
}
}  // namespace

TEST_CASE("Mapas: los colores base son los de la tabla de 1.8 y la sombra los oscurece o aclara") {
  CHECK(maps::rgb(1 * 4 + 2) == 0x7FB238);  // hierba, sombra normal
  CHECK(maps::rgb(1 * 4 + 1) == (0x7F * 220 / 255 * 0x10000u | (0xB2 * 220 / 255) * 0x100u | (0x38 * 220 / 255)));
  CHECK(maps::rgb(12 * 4 + 2) == 0x4040FF);  // agua
  CHECK(maps::rgb(0) == 0);                   // sin explorar
  CHECK((maps::rgb(1 * 4 + 3) >> 8 & 255) < (maps::rgb(1 * 4 + 0) >> 8 & 255));  // la 3 es la más oscura
  CHECK(maps::baseColor(makeState(B::grass)) == 1);
  CHECK(maps::baseColor(makeState(B::sand)) == 2);
  CHECK(maps::baseColor(makeState(B::stone)) == 11);
  CHECK(maps::baseColor(makeState(B::water)) == 12);
  CHECK(maps::baseColor(makeState(B::log)) == 13);
  CHECK(maps::baseColor(makeState(B::glass)) == 0);  // el cristal no se pinta
  CHECK(maps::baseColor(makeState(35, 0)) == 8);     // lana blanca
  CHECK(maps::baseColor(makeState(35, 14)) == 28);   // lana roja
  CHECK(maps::baseColor(makeState(B::dirt, 2)) == 34);  // podzol
}

TEST_CASE("Mapas: el centro es el de la cuadrícula de 128 << escala bloques donde está el jugador") {
  int cx, cz;
  maps::centerFor(0.5, 0.5, 0, cx, cz);
  CHECK(cx == 0);
  CHECK(cz == 0);
  maps::centerFor(70, -70, 0, cx, cz);  // pasado el medio mapa, el siguiente
  CHECK(cx == 128);
  CHECK(cz == -128);
  maps::centerFor(10, 10, 1, cx, cz);  // escala 1: 256 bloques de lado, centros en 64 + 256k
  CHECK(cx == 64);
  CHECK(cz == 64);
}

TEST_CASE("Mapas: un mapa vacío se llena al usarlo y se va pintando solo lo que hay cerca") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  // Con dos mapas vacíos en la mano, el lleno va a otra casilla y uno se queda vacío
  s.player().inventory.slot(0) = ItemStack(ItemId::map, 2);
  CHECK(s.useEmptyMap());
  CHECK(s.player().inventory.slot(0).id == ItemId::map);
  CHECK(s.player().inventory.slot(0).count == 1);
  CHECK(s.player().inventory.slot(1).id == ItemId::filled_map);
  // Con uno solo, se llena en la mano
  s.player().inventory.slot(0) = ItemStack(ItemId::map, 1);
  CHECK(s.useEmptyMap());
  CHECK(s.player().inventory.slot(0).id == ItemId::filled_map);
  CHECK(s.player().inventory.slot(0).count == 1);
  const int id = s.player().inventory.slot(0).meta;
  CHECK(id != s.player().inventory.slot(1).meta);  // (otro mapa)
  REQUIRE(s.mapById(id));
  CHECK(s.mapById(id)->centerX == 0);
  CHECK(s.mapById(id)->centerZ == 0);
  CHECK(s.mapById(id)->scale == 0);
  CHECK_FALSE(s.useEmptyMap());  // ya no lleva uno vacío en la mano
  // Dos columnas por tick: con 64 ticks (128 columnas) ya está pintado todo lo cargado que queda a menos de 64 bloques
  tickN(s, 70);
  const MapData& m = *s.mapById(id);
  auto at = [&](int wx, int wz) { return m.colors[static_cast<std::size_t>((wz - (m.centerZ - 64)) * kMapSize + (wx - (m.centerX - 64)))]; };
  CHECK((at(0, 0) >> 2) == 1);    // hierba bajo el jugador
  CHECK((at(10, 12) >> 2) == 1);
  CHECK(at(60, 60) == 0);         // lo no cargado (el mundo de prueba solo tiene 48x48) queda sin explorar
  CHECK(m.version > 1);
  CHECK(m.dirty);
}

TEST_CASE("Mapas: el agua lleva su color y se oscurece con la profundidad; la pendiente aclara o oscurece la tierra") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.setMode(GameMode::Survival);
  // Un lago de 12 de profundidad en x 4..8 y un montículo de 6 bloques en x -9..-5
  for (int z = -6; z <= 6; z++) {
    for (int x = 4; x <= 8; x++)
      for (int y = 52; y <= 63; y++) fw.setBlock(x, y, z, makeState(B::water));
    for (int x = -9; x <= -5; x++)
      for (int y = 64; y <= 69; y++) fw.setBlock(x, y, z, makeState(B::stone));
  }
  const u8 deep = maps::pixel(fw.world(), 6, 0, 0);
  CHECK((deep >> 2) == 12);
  CHECK((deep & 3) == 0);  // profundo: la sombra más oscura
  const u8 stone = maps::pixel(fw.world(), -7, 0, 0);
  CHECK((stone >> 2) == 11);
  // Sin chunk cargado no hay nada
  CHECK(maps::pixel(fw.world(), 5000, 5000, 0) == 0);
}

TEST_CASE("Mapas: copiar un mapa da ejemplares con los mismos datos; ocho papeles alrededor dan uno de alejar, que al estrenarse tiene una escala más") {
  FlatTestWorld fw;
  GameSession s(fw, 3);
  s.setRules({2, true, true});
  s.setMode(GameMode::Survival);
  s.player().pos = s.player().prevPos = {0.5, 64, 0.5};
  const int id = s.createMap(0, 0, 0);
  // Copiar: un mapa lleno y un mapa vacío dan dos mapas con el mismo número
  std::array<ItemStack, 4> grid{ItemStack(ItemId::filled_map, 1, id), ItemStack(ItemId::map, 1), ItemStack(), ItemStack()};
  const auto copy = matchRecipe(grid, 2);
  REQUIRE(copy);
  CHECK(copy->id == ItemId::filled_map);
  CHECK(copy->count == 2);
  CHECK(copy->meta == id);
  // Alejar: papel alrededor del mapa
  std::array<ItemStack, 9> g9;
  for (ItemStack& it : g9) it = ItemStack(ItemId::paper, 1);
  g9[4] = ItemStack(ItemId::filled_map, 1, id);
  const auto zoom = matchRecipe(g9, 3);
  REQUIRE(zoom);
  CHECK(zoom->id == ItemId::filled_map);
  CHECK((zoom->meta & kMapZoomFlag) != 0);
  CHECK((zoom->meta & ~kMapZoomFlag) == id);
  // Al llevarlo en la mano se crea el mapa de la escala siguiente
  s.player().inventory.slot(0) = *zoom;
  tickN(s, 2);
  const int newId = s.player().inventory.slot(0).meta;
  CHECK(newId != id);
  CHECK((newId & kMapZoomFlag) == 0);
  REQUIRE(s.mapById(newId));
  CHECK(s.mapById(newId)->scale == 1);
}

#include "save/anvil.h"

TEST_CASE("Mapas: ida y vuelta por NBT (el formato de data/map_<id>.dat de 1.8)") {
  MapData m;
  m.id = 7;
  m.scale = 2;
  m.dimension = -1;
  m.centerX = 384;
  m.centerZ = -128;
  for (std::size_t i = 0; i < m.colors.size(); i++) m.colors[i] = static_cast<u8>((i * 7) & 0xFF);
  const nbt::Value root = save::mapToNbt(m);
  const nbt::Value* d = root.getCompound("data");
  REQUIRE(d);
  CHECK(d->getInt("width") == 128);  // como lo escribe 1.8
  CHECK(d->getInt("height") == 128);
  const auto back = save::mapFromNbt(root, 7);
  REQUIRE(back);
  CHECK(back->id == 7);
  CHECK(back->scale == 2);
  CHECK(back->dimension == -1);
  CHECK(back->centerX == 384);
  CHECK(back->centerZ == -128);
  CHECK(back->colors == m.colors);
  // Un archivo con los colores truncados no vale
  nbt::Value bad = save::mapToNbt(m);
  bad.get("data")->get("colors")->bytes().resize(100);
  CHECK_FALSE(save::mapFromNbt(bad, 7));
}
