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
