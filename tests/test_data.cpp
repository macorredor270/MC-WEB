#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "core/fs.h"
#include "data/items.h"
#include "data/biomes.h"
#include "data/blocks.h"
#include "data/blockstates.h"

using namespace mcw;

TEST_CASE("Tabla de bloques") {
  CHECK(blockInfo(B::stone).name == "stone");
  CHECK(blockInfo(B::stone).opaqueCube);
  CHECK_FALSE(blockInfo(B::glass).opaqueCube);
  CHECK_FALSE(blockInfo(B::air).opaqueCube);
  CHECK(blockInfo(B::torch).emitLight == 14);
  CHECK(blockInfo(B::water).opacity == 3);
  CHECK(blockInfo(B::red_flower).opacity == 0);
  CHECK(blockInfo(B::leaves).layer == RenderLayer::CutoutMipped);
  CHECK(blockIdByName("diamond_ore") == B::diamond_ore);
  CHECK(blockIdByName("no_existe") == -1);
  CHECK(tintTypeOf(makeState(B::leaves, 2)) == TintType::Birch);
  CHECK(makeState(B::grass) == 32);
}

TEST_CASE("Biomas y colormap") {
  CHECK(biomeInfo(Biome::desert).name == "desert");
  CHECK(biomeInfo(999).id == Biome::plains);
  // Llanuras: temperatura 0.8, lluvia 0.4
  auto [x, y] = colormapCoords(Biome::plains);
  CHECK(x == 50);
  CHECK(y == 173);
}

TEST_CASE("Blockstates de 1.8") {
  CHECK(blockstateOf(makeState(B::stone, 0)) == BlockstateRef{"stone", "normal"});
  CHECK(blockstateOf(makeState(B::stone, 1)) == BlockstateRef{"granite", "normal"});
  CHECK(blockstateOf(makeState(B::log, 2 | 4)) == BlockstateRef{"birch_log", "axis=x"});
  CHECK(blockstateOf(makeState(B::tallgrass, 1)) == BlockstateRef{"tall_grass", "normal"});
  CHECK_FALSE(blockstateOf(makeState(B::water, 0)).has_value());
  CHECK_FALSE(blockstateOf(makeState(B::stone, 9)).has_value());
  for (const auto& [state, ref] : allMappedStates()) CHECK(blockInfo(stateId(state)).exists);
}

// Validación contra un jar real, solo si se indica MCWEB_ASSETS_DIR (carpeta con assets/ descomprimido).
TEST_CASE("Blockstates existen en los assets de 1.8.8 (opcional)") {
  const char* dir = std::getenv("MCWEB_ASSETS_DIR");
  if (!dir) return;
  for (const auto& [state, ref] : allMappedStates()) {
    CAPTURE(state);
    CAPTURE(ref.file);
    CAPTURE(ref.variant);
    auto text = fs::readText(std::filesystem::path(dir) / "assets/minecraft/blockstates" / (ref.file + ".json"));
    REQUIRE(text.has_value());
    auto j = nlohmann::json::parse(*text);
    CHECK(j["variants"].contains(ref.variant));
  }
}

TEST_CASE("Nombres en español para todos los objetos") {
  int missing = 0;
  for (int id = 1; id < 3000; id++) {
    const ItemInfo& info = itemInfo(id);
    if (!info.exists) continue;
    if (itemDisplayNameEs(id, 0) == info.displayName && info.displayName != "Netherrack" && info.displayName != "Cactus" &&
        info.displayName != "Redstone" && info.displayName != "Slime") {
      MESSAGE("sin traducir: " << info.name);
      missing++;
    }
  }
  CHECK(missing == 0);
  CHECK(itemDisplayNameEs(35, 14) == "Lana roja");
  CHECK(itemDisplayNameEs(5, 5) == "Tablones de roble oscuro");
  CHECK(itemDisplayNameEs(ItemId::diamond_pickaxe, 0) == "Pico de diamante");
  CHECK(itemDisplayNameEs(351, 15) == "Polvo de hueso");
}
