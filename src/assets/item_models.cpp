#include "assets/item_models.h"

#include <nlohmann/json.hpp>

#include "assets/models.h"
#include "assets/pack.h"
#include "assets/textures.h"
#include "data/biomes.h"
#include "data/blockstates.h"
#include "data/items.h"

namespace mcw {
namespace {

const char* kDyeNames[16] = {"black", "red", "green", "brown", "blue", "purple", "cyan", "silver",
                             "gray", "pink", "lime", "yellow", "light_blue", "magenta", "orange", "white"};

/// Estado de bloque con el que se dibuja un ítem de bloque (troncos en vertical, etc.).
BlockState itemBlockState(int id, int meta) {
  switch (id) {
    case B::log: case B::log2: case B::leaves: case B::leaves2: return makeState(id, meta & 3);
    case B::furnace: return makeState(id, 3);  // de frente
    case B::torch: case 75: case 76: return makeState(id, 5);
    case 65: return makeState(id, 2);  // escalera de mano
    default: return makeState(id, meta);
  }
}

}  // namespace

void ItemModels::prepare(const PackStack& packs, BlockTextures& textures, const BlockModels& blocks, const Colormaps& colors) {
  packs_ = &packs;
  textures_ = &textures;
  blocks_ = &blocks;
  colors_ = &colors;
  cache_.clear();
  // Las texturas de ítems tienen que estar registradas antes de cargar el texture array:
  // resolvemos ya todos los ítems que se pueden conseguir o están en el creativo.
  for (int id = 1; id < 512; id++) {
    if (!itemInfo(id).exists) continue;
    const int metas = id == 397 ? 5 : id == 425 ? 16 : id == ItemId::dye ? 16 : (id == ItemId::coal ? 2 : (isBlockItem(id) ? 16 : 1));
    for (int m = 0; m < metas; m++) icon(id, m);
  }
  for (int v = 1; v <= 3; v++) icon(ItemId::bow, 0, v);  // el arco tensado
  textures.layerFor("items/barrier");  // por si algo no tiene modelo
}

std::string ItemModels::modelName(int id, int meta, int variant) const {
  if (isBlockItem(id)) {
    if (id == B::sponge) return meta == 1 ? "sponge_wet" : "sponge";
    const auto ref = blockstateOf(itemBlockState(id, meta));
    if (!ref) return {};
    if (ref->file == "smooth_granite") return "granite_smooth";
    if (ref->file == "smooth_diorite") return "diorite_smooth";
    if (ref->file == "smooth_andesite") return "andesite_smooth";
    return ref->file;
  }
  if (id == ItemId::bow && variant >= 1 && variant <= 3) return "bow_pulling_" + std::to_string(variant - 1);
  if (id == ItemId::coal && meta == 1) return "charcoal";
  // Cabezas y estandartes se dibujan en el juego con su propio modelo de entidad: aquí llevan un sprite (del pack libre)
  if (id == 397) {
    static const char* heads[5] = {"mcweb_skull_skeleton", "mcweb_skull_wither", "mcweb_skull_zombie", "mcweb_skull_char", "mcweb_skull_creeper"};
    return heads[meta >= 0 && meta < 5 ? meta : 0];
  }
  if (id == 425) return "mcweb_banner";
  if (id == ItemId::dye) return std::string("dye_") + kDyeNames[meta & 15];
  return std::string(itemInfo(id).name);
}

const ItemIcon& ItemModels::icon(int id, int meta, int variant) const {
  // Las herramientas guardan el desgaste en meta: no cambia el dibujo
  if (itemInfo(id).maxDurability > 0) meta = 0;
  if (id != ItemId::bow) variant = 0;
  auto key = std::tuple{id, meta, variant};
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  return cache_.emplace(key, resolve(id, meta, variant)).first->second;
}

ItemIcon ItemModels::resolve(int id, int meta, int variant) const {
  ItemIcon out;
  if (!packs_) return out;
  const std::string name = modelName(id, meta, variant);
  u32 tint = 0xFFFFFF;
  if (isBlockItem(id)) {
    const BlockState st = itemBlockState(id, meta);
    switch (tintTypeOf(st)) {
      case TintType::Grass: tint = colors_->grass(Biome::plains); break;
      case TintType::Foliage: tint = colors_->foliage(Biome::plains); break;
      case TintType::Birch: tint = 0x80A755; break;
      case TintType::Spruce: tint = 0x619961; break;
      case TintType::Constant: tint = blockInfo(id).tintColor; break;
      default: break;
    }
  }

  // Seguir la cadena de padres del modelo de ítem
  std::string current = name.empty() ? std::string() : "item/" + name;
  std::string layer0;
  for (int depth = 0; depth < 8 && !current.empty(); depth++) {
    if (current.rfind("minecraft:", 0) == 0) current = current.substr(10);
    if (current == "builtin/generated") {
      if (!layer0.empty()) {
        out.kind = ItemIcon::Kind::Flat;
        out.layer = textures_->layerFor(layer0);
        // Hierba alta, helechos y plantas dobles de hierba usan el color de la hierba
        if (id == 425) {  // estandarte: el color base está en meta (como los tintes: 0 negro .. 15 blanco)
          static const u32 dye[16] = {0x1E1B1B, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8, 0x169C9C, 0x9D9D97,
                                      0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D, 0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE};
          out.tint = dye[meta & 15];
          return out;
        }
        out.tint = (isBlockItem(id) && (id == B::tallgrass || id == B::double_plant || id == B::vine || id == B::waterlily)) ? tint : 0xFFFFFF;
        return out;
      }
      break;
    }
    if (current.rfind("block/", 0) == 0) break;  // modelo de bloque: se dibuja como bloque
    auto j = packs_->readJson("assets/minecraft/models/" + current + ".json");
    if (!j) break;
    if (j->contains("textures") && (*j)["textures"].contains("layer0") && layer0.empty())
      layer0 = (*j)["textures"]["layer0"].get<std::string>();
    current = j->value("parent", "");
  }

  if (isBlockItem(id)) {
    const BlockState st = itemBlockState(id, meta);
    const VariantList* vl = blocks_->forState(st);
    if (vl && !vl->models.empty()) {
      // Sin modelo de ítem: si el modelo del bloque no es un cubo (plantas, antorchas) se usa su
      // textura como sprite, como en el juego.
      const BakedModel& m = vl->models[0];
      bool anyFace = false;
      for (const BakedQuad& q : m.quads) anyFace |= q.onFace;
      if (!anyFace && !m.quads.empty()) {
        out.kind = ItemIcon::Kind::Flat;
        out.layer = m.quads[0].layer;
        out.tint = m.quads[0].tintIndex >= 0 ? tint : 0xFFFFFF;
      } else {
        out.kind = ItemIcon::Kind::Block;
        out.state = st;
        out.tint = tint;
      }
      return out;
    }
  }
  if (!layer0.empty()) {
    out.kind = ItemIcon::Kind::Flat;
    out.layer = textures_->layerFor(layer0);
  }
  return out;
}

}  // namespace mcw
