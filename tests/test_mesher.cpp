#include <doctest/doctest.h>

#include <cstring>

#include "assets/cc0_pack.h"
#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "client/mesher.h"

using namespace mcw;

namespace {
struct Fixture {
  PackStack packs;
  BlockTextures textures;
  BlockModels models;
  Colormaps colors;
  Fixture() {
    packs.pushBottom(makeCC0Pack());
    models.bake(packs, textures);
    textures.load(packs);
    colors.load(packs);
  }
  MesherContext ctx() const { return {&models, &colors}; }
};

const Fixture& fx() {
  static const Fixture f;
  return f;
}

MeshInput emptyInput() {
  MeshInput in;
  in.blocks.fill(0);
  in.light.fill(0xF0);
  in.biomes.fill(1);
  return in;
}

void put(MeshInput& in, int x, int y, int z, BlockState s) {
  in.blocks[MeshInput::idx(x + 1, y + 1, z + 1)] = s;
  if (blockInfo(stateId(s)).opaqueCube) in.light[MeshInput::idx(x + 1, y + 1, z + 1)] = 0;
}
}  // namespace

TEST_CASE("Un bloque suelto tiene 6 caras") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::stone));
  const MeshOutput out = buildMesh(in, fx().ctx());
  CHECK(out.opaque.size() == 6 * 4);
  CHECK(out.translucent.empty());
}

TEST_CASE("Las caras entre bloques opacos se ocultan") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::stone));
  put(in, 6, 5, 5, makeState(B::dirt));
  CHECK(buildMesh(in, fx().ctx()).opaque.size() == 10 * 4);

  // Un bloque rodeado por completo no genera nada
  MeshInput full = emptyInput();
  for (int y = 4; y <= 6; y++)
    for (int z = 4; z <= 6; z++)
      for (int x = 4; x <= 6; x++) put(full, x, y, z, makeState(B::stone));
  const MeshOutput o = buildMesh(full, fx().ctx());
  CHECK(o.opaque.size() == 9 * 6 * 4);  // solo las 9 caras exteriores por cada lado del cubo 3x3x3
}

TEST_CASE("El cristal se oculta contra sí mismo pero no contra el aire") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::glass));
  put(in, 6, 5, 5, makeState(B::glass));
  CHECK(buildMesh(in, fx().ctx()).opaque.size() == 10 * 4);
}

TEST_CASE("Hojas rápidas: sin caras interiores y pintadas opacas; luz suave desactivable") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::leaves));
  put(in, 6, 5, 5, makeState(B::leaves));
  const MeshOutput fancy = buildMesh(in, fx().ctx());
  CHECK(fancy.opaque.size() == 12 * 4);  // detalladas: se ven las caras de dentro
  for (const ChunkVertex& v : fancy.opaque) CHECK(v.a == 255);
  in.flags = kMeshSmoothLight;  // hojas rápidas
  const MeshOutput fast = buildMesh(in, fx().ctx());
  CHECK(fast.opaque.size() == 10 * 4);
  for (const ChunkVertex& v : fast.opaque) CHECK(v.a == 0);  // el shader tapa los huecos

  // Sin luz suave no hay oclusión ambiental: la pared de al lado no oscurece nada
  MeshInput ao = emptyInput();
  put(ao, 5, 5, 5, makeState(B::stone));
  put(ao, 6, 6, 5, makeState(B::stone));
  ao.flags = kMeshFancyLeaves;
  const MeshOutput flat = buildMesh(ao, fx().ctx());
  for (std::size_t i = 0; i < flat.opaque.size(); i += 4) {
    bool top = true;
    for (int k = 0; k < 4; k++) top &= flat.opaque[i + k].y == 6 * 256 && flat.opaque[i + k].x <= 6 * 256;
    if (top)
      for (int k = 0; k < 4; k++) CHECK(flat.opaque[i + k].r == 255);
  }
}

TEST_CASE("Agua: superficie de dos caras, lados y fondo") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::water));
  const MeshOutput out = buildMesh(in, fx().ctx());
  // arriba (2) + abajo (1) + 4 lados x 2 caras
  CHECK(out.translucent.size() == 11 * 4);
  // La superficie de una fuente queda por debajo del bloque entero
  int maxY = 0;
  for (const ChunkVertex& v : out.translucent) maxY = std::max<int>(maxY, v.y);
  CHECK(maxY < 6 * 256);
  CHECK(maxY > 5 * 256 + 128);  // más de medio bloque
}

TEST_CASE("Oclusión ambiental: una pared oscurece los vértices de al lado") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::stone));
  put(in, 6, 6, 5, makeState(B::stone));  // encima y al este: tapa el borde este de la cara superior
  const MeshOutput out = buildMesh(in, fx().ctx());
  int darkEast = 0, brightWest = 0;
  for (std::size_t i = 0; i < out.opaque.size(); i += 4) {
    bool top = true;
    for (int k = 0; k < 4; k++) top &= out.opaque[i + k].y == 6 * 256;
    if (!top) continue;
    // Solo la cara superior del bloque de abajo (x de 5 a 6)
    bool lower = true;
    for (int k = 0; k < 4; k++) lower &= out.opaque[i + k].x <= 6 * 256;
    if (!lower) continue;
    for (int k = 0; k < 4; k++) {
      const ChunkVertex& v = out.opaque[i + k];
      if (v.x == 6 * 256) darkEast += v.r < 215;
      else brightWest += v.r == 255;
    }
  }
  CHECK(darkEast == 2);
  CHECK(brightWest == 2);
}

TEST_CASE("Hierba alta: cruz de 4 caras, teñida y sin AO") {
  MeshInput in = emptyInput();
  put(in, 5, 5, 5, makeState(B::tallgrass, 1));
  const MeshOutput out = buildMesh(in, fx().ctx());
  CHECK(out.opaque.size() == 4 * 4);
  // Teñida con el color de la hierba (no blanca)
  CHECK(out.opaque[0].r != out.opaque[0].g);
}

TEST_CASE("Mallas a bytes y vuelta; cada vértice sabe en qué sección está") {
  MeshInput in;
  in.sx = 2;
  in.sy = 5;
  in.sz = -1;
  in.blocks[MeshInput::idx(5, 5, 5)] = makeState(B::stone);
  in.blocks[MeshInput::idx(8, 8, 8)] = makeState(B::water);
  in.light.fill(0xF0);
  const MeshOutput a = buildMesh(in, fx().ctx());
  REQUIRE(!a.opaque.empty());
  REQUIRE(!a.translucent.empty());
  for (const ChunkVertex& v : a.opaque) CHECK(v.slot == 0);  // (el hueco en la GPU lo da Terrain al subir la malla)
  const std::vector<u8> bytes = encodeMeshOutput(a);
  MeshOutput b;
  REQUIRE(decodeMeshOutput(bytes.data(), bytes.size(), b));
  CHECK(b.sx == 2);
  CHECK(b.sy == 5);
  CHECK(b.sz == -1);
  REQUIRE(b.opaque.size() == a.opaque.size());
  REQUIRE(b.translucent.size() == a.translucent.size());
  CHECK(std::memcmp(a.opaque.data(), b.opaque.data(), a.opaque.size() * sizeof(ChunkVertex)) == 0);
  CHECK_FALSE(decodeMeshOutput(bytes.data(), bytes.size() - 3, b));
}

TEST_CASE("Paquete de modelos para los workers: mismas capas al hornear") {
  // Un pack "de usuario" con un blockstate propio encima del CC0
  auto user = std::make_shared<MemoryPack>("usuario");
  user->putText("assets/minecraft/blockstates/stone.json", R"({"variants":{"variant=stone":{"model":"mi_piedra"}}})");
  user->putText("assets/minecraft/models/block/mi_piedra.json",
                R"({"parent":"block/cube_all","textures":{"all":"blocks/mi_piedra"}})");
  auto cc0 = makeCC0Pack();
  PackStack page;
  page.pushBottom(cc0);
  page.pushTop(user);
  BlockTextures t1;
  BlockModels m1;
  m1.bake(page, t1);

  const std::vector<u8> bundle = bundleModelFiles(page, cc0.get());
  auto unpacked = unbundlePack(bundle.data(), bundle.size(), "worker");
  REQUIRE(unpacked);
  CHECK(unpacked->exists("assets/minecraft/models/block/mi_piedra.json"));
  PackStack worker;
  worker.pushBottom(makeCC0Pack());
  worker.pushTop(unpacked);
  BlockTextures t2;
  BlockModels m2;
  m2.bake(worker, t2);
  CHECK(t1.layerCount() == t2.layerCount());
  CHECK(t2.layerFor("blocks/mi_piedra") == t1.layerFor("blocks/mi_piedra"));
}

TEST_CASE("Recortes: las texturas con huecos (hojas, hierba alta, cristal) se separan de los sólidos") {
  const BlockTextures& t = fx().textures;
  BlockTextures& names = const_cast<BlockTextures&>(t);  // (layerFor solo mira una tabla: no registra nada nuevo si ya está)
  CHECK_FALSE(t.needsCutout(names.layerFor("blocks/stone")));
  CHECK_FALSE(t.needsCutout(names.layerFor("blocks/dirt")));
  CHECK(t.needsCutout(names.layerFor("blocks/leaves_oak")));
  CHECK(t.needsCutout(names.layerFor("blocks/tallgrass")));
  CHECK(t.needsCutout(names.layerFor("blocks/glass")));
  CHECK(t.needsCutout(names.layerFor("blocks/fire_layer_0")));  // animada: cuenta cualquier fotograma
}

TEST_CASE("Recortes: splitCutout reparte los quads por su textura y deja las hojas rápidas en los sólidos") {
  const BlockTextures& t = fx().textures;
  BlockTextures& names = const_cast<BlockTextures&>(t);
  std::vector<u8> flags(static_cast<std::size_t>(t.layerCount()), 0);
  for (int i = 0; i < t.layerCount(); i++) flags[static_cast<std::size_t>(i)] = t.needsCutout(i) ? 1 : 0;

  auto quad = [](u16 layer, u8 alpha) {
    std::vector<ChunkVertex> q(4);
    for (ChunkVertex& v : q) {
      v = ChunkVertex{};
      v.layer = layer;
      v.a = alpha;
    }
    return q;
  };
  std::vector<ChunkVertex> in;
  auto add = [&](const std::vector<ChunkVertex>& q) { in.insert(in.end(), q.begin(), q.end()); };
  add(quad(names.layerFor("blocks/stone"), 255));
  add(quad(names.layerFor("blocks/leaves_oak"), 255));
  add(quad(names.layerFor("blocks/leaves_oak"), 0));  // hojas rápidas: el shader las pinta opacas
  add(quad(names.layerFor("blocks/dirt"), 255));
  std::vector<ChunkVertex> solid, cutout;
  splitCutout(in, flags, solid, cutout);
  CHECK(solid.size() == 3 * 4);
  CHECK(cutout.size() == 1 * 4);
  CHECK(cutout[0].layer == names.layerFor("blocks/leaves_oak"));
  // Nada se pierde ni se repite
  CHECK(solid.size() + cutout.size() == in.size());
}

TEST_CASE("Recortes: una malla con hojas y piedra deja las hojas en la lista de recortes") {
  MeshInput in = emptyInput();
  put(in, 4, 4, 4, makeState(B::stone));
  put(in, 8, 4, 8, makeState(B::leaves));
  const MeshOutput out = buildMesh(in, fx().ctx());
  const BlockTextures& t = fx().textures;
  std::vector<u8> flags(static_cast<std::size_t>(t.layerCount()), 0);
  for (int i = 0; i < t.layerCount(); i++) flags[static_cast<std::size_t>(i)] = t.needsCutout(i) ? 1 : 0;
  std::vector<ChunkVertex> solid, cutout;
  splitCutout(out.opaque, flags, solid, cutout);
  CHECK(solid.size() == 6 * 4);   // la piedra
  CHECK(cutout.size() == 6 * 4);  // las hojas (con hojas elegantes: caras con hueco)
}
