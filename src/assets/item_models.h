#pragma once
#include <map>
#include <string>

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
};

/// Resuelve el icono de cada ítem leyendo los modelos JSON de ítem del pack
/// (`models/item/<nombre>.json`): si heredan de `builtin/generated` son un sprite con su `layer0`,
/// si heredan de un modelo de bloque se dibujan como bloque.
class ItemModels {
 public:
  /// Hay que llamarlo antes de BlockTextures::load (registra las texturas de los ítems).
  void prepare(const PackStack& packs, BlockTextures& textures, const BlockModels& blocks, const Colormaps& colors);
  const ItemIcon& icon(int id, int meta) const;

 private:
  ItemIcon resolve(int id, int meta) const;
  std::string modelName(int id, int meta) const;

  const PackStack* packs_ = nullptr;
  BlockTextures* textures_ = nullptr;
  const BlockModels* blocks_ = nullptr;
  const Colormaps* colors_ = nullptr;
  mutable std::map<std::pair<int, int>, ItemIcon> cache_;
};

}  // namespace mcw
