#pragma once
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.h"
#include "data/blocks.h"

namespace mcw {

class PackStack;
class BlockTextures;

/// Un cuadrilátero listo para mallar, en coordenadas de bloque (0..1).
/// Los vértices van en el orden: arriba-izq, abajo-izq, abajo-der, arriba-der vistos desde fuera.
struct BakedQuad {
  std::array<std::array<float, 3>, 4> pos{};
  std::array<std::array<float, 2>, 4> uv{};
  u16 layer = 0;
  i8 cullface = -1;   // cara del bloque que la oculta si el vecino es opaco (-1 = nunca)
  i8 tintIndex = -1;
  i8 face = 1;        // dirección de la normal (la más cercana a un eje)
  bool shade = true;  // aplicar sombreado por cara
  bool onFace = false;  // alineada con un eje y pegada a la cara del bloque (usa luz del vecino y AO)
};

struct BakedModel {
  std::vector<BakedQuad> quads;
  bool ambientOcclusion = true;
};

/// Variantes de un estado (listas con peso en el blockstate), elegidas por posición.
struct VariantList {
  std::vector<BakedModel> models;
  std::vector<int> weights;
  int totalWeight = 0;

  const BakedModel& pick(u32 hash) const {
    if (models.size() == 1) return models[0];
    int r = static_cast<int>(hash % static_cast<u32>(totalWeight));
    for (std::size_t i = 0; i < models.size(); i++) {
      r -= weights[i];
      if (r < 0) return models[i];
    }
    return models.back();
  }
};

/// Tabla de modelos horneados para todos los estados de bloque conocidos.
class BlockModels {
 public:
  BlockModels();
  void bake(const PackStack& packs, BlockTextures& textures);

  /// nullptr si el estado no tiene modelo (aire, fluidos).
  const VariantList* forState(BlockState s) const { return table_[s & 0xFFF].get(); }
  const VariantList& missing() const { return *missing_; }
  /// Modelo de un bloque que depende de sus vecinos (vallas, escaleras...) con sus bits extra.
  /// Si no hay, el del estado sin más.
  const VariantList* forExtended(BlockState s, int ext) const {
    auto it = extended_.find(extKey(s, ext));
    return it != extended_.end() ? it->second.get() : forState(s);
  }
  static u32 extKey(BlockState s, int ext) { return (u32(s) << 8) | u32(ext & 255); }

  u16 waterStill = 0, waterFlow = 0, lavaStill = 0, lavaFlow = 0;
  int bakedStates() const { return baked_; }
  const std::vector<std::string>& errors() const { return errors_; }

 private:
  std::array<std::shared_ptr<const VariantList>, 4096> table_{};
  std::unordered_map<u32, std::shared_ptr<const VariantList>> extended_;
  std::shared_ptr<const VariantList> missing_;
  std::vector<std::string> errors_;
  int baked_ = 0;
};

}  // namespace mcw
