#pragma once
#include <memory>
#include <set>
#include <unordered_set>
#include <vector>

#include "core/jobs.h"
#include "game/session.h"
#include "save/world_save.h"
#include "world/generator.h"
#include "world/world.h"

namespace mcw::net {

/// Mundo del servidor dedicado, sin gráficos: lee del disco o genera (en hilos) los chunks que
/// rodean a los jugadores y al punto de aparición, y guarda y suelta los que ya no hacen falta.
class ServerWorld final : public WorldAccess {
 public:
  ServerWorld(WorldSave& save, u64 seed, GeneratorSettings settings, int threads);
  ~ServerWorld() override;

  /// La partida (para meter y sacar las criaturas, objetos, hornos y cofres de cada chunk).
  void attach(GameSession& session) { session_ = &session; }

  World& world() override { return world_; }
  void setBlock(int x, int y, int z, BlockState s) override;

  /// Carga o genera lo que falte a `radius` chunks de cada centro (lo más cercano primero) y
  /// suelta lo que quede lejos de todos. `budgetMs`: tiempo máximo en el hilo principal.
  void update(const std::vector<ChunkPos>& centers, int radius, double budgetMs, i64 worldTime);
  /// Carga todo lo que hay a `radius` de `center` antes de seguir (el punto de aparición, al arrancar).
  void loadNow(ChunkPos center, int radius, i64 worldTime);
  bool loaded(ChunkPos p) const { return world_.chunk(p.x, p.z) != nullptr; }
  /// Chunks recién generados (para poner animales, como en el juego).
  std::vector<ChunkPos> takeNewChunks() { return std::exchange(newChunks_, {}); }
  /// Guarda los chunks cambiados (o todos los cargados con `everything`).
  void saveAll(i64 worldTime, bool everything = false);
  std::size_t loadedCount() const { return world_.size(); }
  const TerrainGenerator& generator() const { return *generator_; }

 private:
  void request(ChunkPos p);
  void onGenerated(std::unique_ptr<Chunk> c);

  WorldSave& save_;
  GameSession* session_ = nullptr;
  std::shared_ptr<const TerrainGenerator> generator_;
  JobSystem jobs_;
  World world_;
  std::set<std::pair<int, int>> pending_;  // generándose
  std::unordered_set<ChunkPos, ChunkPosHash> unsaved_;
  std::vector<ChunkPos> newChunks_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mcw::net
