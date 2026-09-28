#include <doctest/doctest.h>

#include <cstdlib>

#include "assets/cc0_pack.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "core/face.h"

using namespace mcw;

namespace {
void checkPack(const PackStack& packs) {
  BlockTextures tex;
  BlockModels models;
  models.bake(packs, tex);
  tex.load(packs);
  for (const auto& e : models.errors()) MESSAGE(e);
  CHECK(models.errors().empty());
  CHECK(tex.missing().empty());
  CHECK(models.bakedStates() > 900);

  // Piedra: 6 caras de cubo completo con cullface
  const VariantList* stone = models.forState(makeState(B::stone));
  REQUIRE(stone);
  const BakedModel& m = stone->pick(0);
  CHECK(m.quads.size() == 6);
  for (const BakedQuad& q : m.quads) {
    CHECK(q.onFace);
    CHECK(q.cullface == q.face);
  }
  // Tronco en eje X: la cara este/oeste es la de los anillos
  const VariantList* logX = models.forState(makeState(B::log, 4));
  REQUIRE(logX);
  const u16 top = tex.layerFor("blocks/log_oak_top");
  bool eastIsTop = false;
  for (const BakedQuad& q : logX->pick(0).quads)
    if (q.face == Face::East) eastIsTop = q.layer == top;
  CHECK(eastIsTop);
  // Hierba alta: cruz sin AO y tintada
  const VariantList* grass = models.forState(makeState(B::tallgrass, 1));
  REQUIRE(grass);
  CHECK_FALSE(grass->pick(0).ambientOcclusion);
  CHECK(grass->pick(0).quads.front().tintIndex == 0);
  CHECK_FALSE(grass->pick(0).quads.front().onFace);
  CHECK(tex.tileSize() == 16);
  CHECK(tex.animationCount() >= 2);  // agua y lava
}
}  // namespace

TEST_CASE("El pack CC0 tiene todos los modelos y texturas") {
  PackStack packs;
  packs.pushBottom(makeCC0Pack());
  checkPack(packs);
  auto font = packs.readImage("assets/minecraft/textures/font/ascii.png");
  REQUIRE(font);
  CHECK(font->width == 128);
}

TEST_CASE("Los assets del jar real se hornean sin errores (opcional)") {
  const char* dir = std::getenv("MCWEB_ASSETS_DIR");
  if (!dir) return;
  PackStack packs;
  packs.pushBottom(std::make_shared<DirPack>(dir));
  checkPack(packs);
}

#include "assets/item_models.h"
#include "data/items.h"
#include "game/rules.h"

TEST_CASE("Pack libre: todo lo del modo creativo tiene dibujo") {
  PackStack packs;
  packs.pushBottom(makeCC0Pack());
  BlockTextures tex;
  BlockModels models;
  Colormaps colors;
  ItemModels items;
  models.bake(packs, tex);
  colors.load(packs);
  items.prepare(packs, tex, models, colors);
  tex.load(packs);
  std::vector<std::string> noIcon;
  for (const ItemStack& s : creativeItems()) {
    const ItemIcon& icon = items.icon(s.id, s.meta);
    if (icon.kind == ItemIcon::Kind::None) noIcon.push_back(std::string(itemInfo(s.id).name) + ":" + std::to_string(s.meta));
  }
  // Todos los objetos de 1.8 (no solo los del creativo)
  std::vector<std::string> allNoIcon;
  for (int id = 1; id < 512; id++) {
    if (!itemInfo(id).exists) continue;
    if (items.icon(id, 0).kind == ItemIcon::Kind::None) allNoIcon.push_back(std::string(itemInfo(id).name));
  }
  std::string all;
  for (const auto& n : allNoIcon) all += n + " ";
  MESSAGE("objetos sin dibujo (" << allNoIcon.size() << "): " << all);
  // Estados de bloque sin modelo
  std::vector<std::string> noModel;
  for (int id = 1; id < 256; id++) {
    if (!blockInfo(id).exists || blockInfo(id).id != id || id == B::air || isFluid(id) || id == 36 /* pistón moviéndose */ || id == 119 /* portal del End */) continue;
    bool any = false;
    for (int m = 0; m < 16; m++) any |= models.forState(makeState(id, m)) != nullptr;
    if (!any) noModel.push_back(std::string(blockInfo(id).name));
  }
  std::string nm;
  for (const auto& n : noModel) nm += n + " ";
  MESSAGE("bloques sin modelo (" << noModel.size() << "): " << nm);
  std::string list;
  for (const auto& n : noIcon) list += n + " ";
  MESSAGE("sin icono (" << noIcon.size() << "): " << list);
  std::string miss;
  for (const auto& m : tex.missing()) miss += m + " ";
  MESSAGE("texturas que faltan (" << tex.missing().size() << "): " << miss);
  CHECK(noIcon.empty());
  CHECK(allNoIcon.empty());
  CHECK(noModel.empty());
  CHECK(tex.missing().empty());
  CHECK(models.errors().empty());
}
