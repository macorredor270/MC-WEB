#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "client/mesher.h"
#include "world/chunk.h"

namespace mcw {

class JobSystem;

/// Web Workers para el build web sin hilos (páginas sin COOP/COEP, donde no hay SharedArrayBuffer).
/// Cada worker carga su propio módulo (mcweb-worker.wasm) y hace los trabajos pesados: generar
/// chunks y construir mallas. Los datos van y vienen como bytes. Así el hilo principal queda libre
/// para dibujar a la frecuencia de la pantalla (60, 120 Hz...). En nativo no hace nada.
class WorkerPool {
 public:
  using ChunkDone = std::function<void(std::unique_ptr<Chunk>)>;
  using MeshDone = std::function<void(MeshOutput)>;
  using Failed = std::function<void()>;

  explicit WorkerPool(JobSystem& jobs);
  ~WorkerPool();
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  /// ¿Hay Web Workers en esta plataforma? (solo en el build web sin hilos)
  static bool supported();
  /// Número de workers recomendado según los núcleos del dispositivo.
  static int suggestedCount();

  /// Arranca `count` workers. `expectedLayers` es el número de capas de textura tras hornear los
  /// modelos en la página: un worker que obtenga otro número no se usa (mallaría distinto).
  bool start(int count, u64 seed, const std::vector<u8>& modelBundle, int expectedLayers);

  /// Nuevo mundo: los workers cambian de semilla y de tipo de generador (los mensajes van en orden,
  /// así que lo que se pida después ya usa el mundo nuevo).
  void setWorld(u64 seed, const std::string& generatorName, const std::string& options, bool structures);

  int readyCount() const;
  /// Trabajos que se pueden encargar ahora mismo sin hacer cola en los workers.
  int capacity() const;
  int inFlight() const { return static_cast<int>(pending_.size()); }

  /// Encargan un trabajo. El resultado (o el fallo) llega por `JobSystem::postToMain`.
  bool generate(ChunkPos p, ChunkDone done, Failed failed);
  bool mesh(const MeshInput& in, MeshDone done, Failed failed);

  // Llamadas desde JavaScript (ver worker_pool.cpp)
  void onReady(int worker, int layers);
  void onResult(int worker, int id, const u8* data, int len);
  void onError(int worker, int id);

 private:
  struct Worker {
    bool ready = false, failed = false;
    int inFlight = 0;
  };
  struct Pending {
    int worker = 0;
    bool isMesh = false;
    ChunkDone chunkDone;
    MeshDone meshDone;
    Failed failed;
  };
  int pickWorker() const;
  void fail(Pending p);

  JobSystem& jobs_;
  std::vector<Worker> workers_;
  std::unordered_map<int, Pending> pending_;
  int nextId_ = 1;
  int expectedLayers_ = 0;
};

}  // namespace mcw
