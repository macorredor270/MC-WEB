#pragma once
#include <map>
#include <string>
#include <tuple>

#include "core/types.h"
#include "data/blocks.h"

namespace mcw {

class PackStack;
class BlockTextures;
class BlockModels;
class Colormaps;

/// Cómo se dibuja un ítem: sprite plano (herramientas, flores...) o cubo 3D (bloques).
struct ItemIcon {
  enum class Kind { None, Flat, Block } kind = Kind::None;
  u16 layer = 0;        // capa del texture array (sprites planos)
  BlockState state = 0; // estado de bloque (iconos 3D)
  u32 tint = 0xFFFFFF;  // color para las texturas en gris (hierba, hojas)
  bool hasLayer2 = false;  // segunda capa (el líquido de la poción, las manchas del huevo de criatura): su color lo pone quien dibuja
  u16 layer2 = 0;
};

/// Resuelve el icono de cada ítem leyendo los modelos JSON de ítem del pack
/// (`models/item/<nombre>.json`): si heredan de `builtin/generated` son un sprite con su `layer0`,
/// si heredan de un modelo de bloque se dibujan como bloque.
class ItemModels {
 public:
  /// Hay que llamarlo antes de BlockTextures::load (registra las texturas de los ítems).
  void prepare(const PackStack& packs, BlockTextures& textures, const BlockModels& blocks, const Colormaps& colors);
  /// `variant` 0 = el normal; el arco tiene 1..3 (`bow_pulling_0..2`) según lo tensado que esté.
  const ItemIcon& icon(int id, int meta, int variant = 0) const;

 private:
  ItemIcon resolve(int id, int meta, int variant) const;
  std::string modelName(int id, int meta, int variant) const;

  const PackStack* packs_ = nullptr;
  BlockTextures* textures_ = nullptr;
  const BlockModels* blocks_ = nullptr;
  const Colormaps* colors_ = nullptr;
  mutable std::map<std::tuple<int, int, int>, ItemIcon> cache_;
};

}  // namespace mcw
