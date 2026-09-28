#include "net/server_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "core/log.h"
#include "save/chunk_io.h"

namespace mcw::net {

ServerWorld::ServerWorld(WorldSave& save, u64 seed, GeneratorSettings settings, int threads)
    : save_(save), generator_(std::make_shared<TerrainGenerator>(seed, std::move(settings))), jobs_(threads) {}

ServerWorld::~ServerWorld() {
  *alive_ = false;
  jobs_.waitIdle();
}

void ServerWorld::setBlock(int x, int y, int z, BlockState s) {
  ChunkSet modified;
  world_.setBlock(x, y, z, s, modified);
  unsaved_.insert({x >> 4, z >> 4});
}

void ServerWorld::request(ChunkPos p) {
  if (world_.chunk(p.x, p.z) || pending_.count({p.x, p.z})) return;
  // Guardado: se lee aquí mismo (es rápido); si no, se genera en un hilo
  if (session_) {
    if (auto saved = save::loadChunk(save_.regions(), p, *session_)) {
      ChunkSet modified;
      world_.insert(std::move(saved), modified);
      return;
    }
  }
  pending_.insert({p.x, p.z});
  auto gen = generator_;
  auto alive = alive_;
  JobSystem* jobs = &jobs_;
  jobs_.submit([gen, alive, jobs, this, p] {
    auto chunk = gen->generate(p.x, p.z);
    auto holder = std::make_shared<std::unique_ptr<Chunk>>(std::move(chunk));
    jobs->postToMain([alive, this, holder] {
      if (*alive) onGenerated(std::move(*holder));
    });
  });
}

void ServerWorld::onGenerated(std::unique_ptr<Chunk> c) {
  const ChunkPos p = c->pos();
  pending_.erase({p.x, p.z});
  if (world_.chunk(p.x, p.z)) return;
  ChunkSet modified;
  world_.insert(std::move(c), modified);
  newChunks_.push_back(p);
  unsaved_.insert(p);  // un chunk nuevo se guarda aunque no se toque, como en el juego
}

void ServerWorld::update(const std::vector<ChunkPos>& centers, int radius, double budgetMs, i64 worldTime) {
  const auto start = std::chrono::steady_clock::now();
  auto elapsedMs = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
  // Lo generado en los hilos entra en el mundo (en este hilo)
  jobs_.pump(budgetMs * 0.5);

  // Pedir lo que falta, de cerca a lejos (sin llenar la cola más de la cuenta)
  const std::size_t maxPending = static_cast<std::size_t>(std::max(4, jobs_.threadCount() * 4));
  for (int d = 0; d <= radius && pending_.size() < maxPending && elapsedMs() < budgetMs; d++)
    for (const ChunkPos& c : centers)
      for (int dz = -d; dz <= d; dz++)
        for (int dx = -d; dx <= d; dx++) {
          if (std::max(std::abs(dx), std::abs(dz)) != d || pending_.size() >= maxPending) continue;
          request({c.x + dx, c.z + dz});
        }

  // Soltar (guardando) lo que ya no está cerca de nadie
  std::vector<ChunkPos> far;
  for (const auto& [p, chunk] : world_.chunks()) {
    bool near = false;
    for (const ChunkPos& c : centers) near |= std::abs(p.x - c.x) <= radius + 2 && std::abs(p.z - c.z) <= radius + 2;
    if (!near) far.push_back(p);
  }
  for (const ChunkPos& p : far) {
    if (elapsedMs() > budgetMs) break;
    if (session_) save::storeChunk(save_.regions(), *world_.chunk(p.x, p.z), *session_, true, worldTime);
    unsaved_.erase(p);
    world_.remove(p);
  }
}

void ServerWorld::loadNow(ChunkPos center, int radius, i64 worldTime) {
  for (int i = 0; i < 20000; i++) {
    bool all = true;
    for (int dz = -radius; dz <= radius && all; dz++)
      for (int dx = -radius; dx <= radius && all; dx++) all = loaded({center.x + dx, center.z + dz});
    if (all) return;
    update({center}, radius, 50.0, worldTime);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  log::warn("no se pudo cargar el punto de aparición a tiempo");
}

void ServerWorld::saveAll(i64 worldTime, bool everything) {
  if (!session_) return;
  if (everything)
    for (const auto& [p, c] : world_.chunks()) unsaved_.insert(p);
  // Las criaturas y los objetos se mueven: sus chunks también se guardan
  for (const Mob& m : session_->mobs()) unsaved_.insert({static_cast<int>(std::floor(m.pos.x)) >> 4, static_cast<int>(std::floor(m.pos.z)) >> 4});
  for (const ItemEntity& e : session_->items())
    unsaved_.insert({static_cast<int>(std::floor(e.pos.x)) >> 4, static_cast<int>(std::floor(e.pos.z)) >> 4});
  for (const ChunkPos& p : unsaved_)
    if (const Chunk* c = world_.chunk(p.x, p.z)) save::storeChunk(save_.regions(), *c, *session_, false, worldTime);
  unsaved_.clear();
}

}  // namespace mcw::net
