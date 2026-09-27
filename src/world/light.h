#pragma once
#include <unordered_set>
#include <vector>

#include "world/chunk.h"

namespace mcw {

class World;

namespace light {

/// Luz inicial de un chunk recién generado, sin mirar a los vecinos: luz de cielo por columnas
/// más propagación dentro del chunk, y luz de los bloques que emiten.
void computeInitial(Chunk& chunk);

}  // namespace light

using ChunkSet = std::unordered_set<ChunkPos, ChunkPosHash>;

/// Propagación de luz entre chunks cargados (BFS). La luz de cielo baja sin perder intensidad en
/// vertical por bloques transparentes; en el resto de direcciones pierde 1 + opacidad.
class LightEngine {
 public:
  explicit LightEngine(World& world) : world_(world) {}

  /// Tras cargar un chunk: intercambia luz con sus vecinos por los bordes.
  void stitch(ChunkPos pos, ChunkSet& modified);
  /// Tras cambiar un bloque: recalcula la luz alrededor (quita la vieja y propaga la nueva).
  void blockChanged(int x, int y, int z, ChunkSet& modified);

 private:
  struct Node {
    int x, y, z;
    int level;  // solo se usa al apagar
  };
  void propagateIncrease(bool sky, std::vector<Node>& queue, ChunkSet& modified);
  void propagateDecrease(bool sky, std::vector<Node>& queue, std::vector<Node>& relight, ChunkSet& modified);

  World& world_;
};

}  // namespace mcw
