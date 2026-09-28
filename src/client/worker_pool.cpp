#include "client/worker_pool.h"

#include <algorithm>

#include "core/jobs.h"
#include "core/log.h"
#include "world/chunk_codec.h"

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define MCW_WEB_WORKERS 1
#include <emscripten/emscripten.h>
#else
#define MCW_WEB_WORKERS 0
#endif

namespace mcw {
namespace {

constexpr int kMaxPerWorker = 48;  // trabajos en cola por worker: nunca se queda parado entre dos frames
WorkerPool* g_pool = nullptr;

}  // namespace

#if MCW_WEB_WORKERS

// --- Lado JavaScript -------------------------------------------------------------------------------
// Los workers se crean a partir de mcweb-worker.js (junto a la página). Los resultados llegan en
// mensajes; se copian a la memoria del módulo y se pasan a C++ con _mcw_pool_result.

EM_JS(int, mcw_js_hw_concurrency, (), {
  return (typeof navigator !== 'undefined' && navigator.hardwareConcurrency) ? navigator.hardwareConcurrency : 4;
});

EM_JS(int, mcw_js_workers_start, (int count, unsigned seedLo, unsigned seedHi, const u8* bundle, int len), {
  if (typeof Worker === 'undefined') return 0;
  const bytes = len > 0 ? HEAPU8.slice(bundle, bundle + len) : null;
  const url = Module['mcwWorkerUrl'] || 'mcweb-worker.js';
  Module['mcwWorkers'] = [];
  let started = 0;
  for (let i = 0; i < count; i++) {
    let w;
    try {
      w = new Worker(url);
    } catch (e) {
      console.warn('MC-WEB: no se pudo crear un Web Worker:', e);
      break;
    }
    const index = i;
    w.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'ready') { Module['_mcw_pool_ready'](index, m.layers); return; }
      if (m.type === 'error') {
        console.warn('MC-WEB: error en el worker', index, m.message);
        Module['_mcw_pool_error'](index, m.id | 0);
        return;
      }
      const u8 = new Uint8Array(m.buf);
      const p = Module['_malloc'](Math.max(1, u8.length));
      HEAPU8.set(u8, p);
      Module['_mcw_pool_result'](index, m.id, p, u8.length);
      Module['_free'](p);
    };
    w.onerror = (e) => {
      if (e.preventDefault) e.preventDefault();
      console.warn('MC-WEB: el worker', index, 'ha fallado:', e.message);
      Module['_mcw_pool_error'](index, -1);
    };
    w.postMessage({ type: 'init', seedLo: seedLo, seedHi: seedHi, bundle: bytes ? bytes.buffer.slice(0) : null });
    Module['mcwWorkers'].push(w);
    started++;
  }
  return started;
});

EM_JS(void, mcw_js_worker_gen, (int w, int id, int cx, int cz), {
  Module['mcwWorkers'][w].postMessage({ type: 'gen', id: id, cx: cx, cz: cz });
});

EM_JS(void, mcw_js_worker_mesh, (int w, int id, const u8* data, int len), {
  const copy = HEAPU8.slice(data, data + len);
  Module['mcwWorkers'][w].postMessage({ type: 'mesh', id: id, input: copy.buffer }, [copy.buffer]);
});

EM_JS(void, mcw_js_workers_world, (unsigned seedLo, unsigned seedHi, const char* name, const char* options, int structures), {
  const msg = { type: 'world', seedLo: seedLo, seedHi: seedHi, name: UTF8ToString(name), options: UTF8ToString(options), structures: structures };
  for (const w of Module['mcwWorkers'] || []) w.postMessage(msg);
});

EM_JS(void, mcw_js_workers_stop, (), {
  for (const w of Module['mcwWorkers'] || []) w.terminate();
  Module['mcwWorkers'] = [];
});

extern "C" {
EMSCRIPTEN_KEEPALIVE void mcw_pool_ready(int worker, int layers) {
  if (g_pool) g_pool->onReady(worker, layers);
}
EMSCRIPTEN_KEEPALIVE void mcw_pool_result(int worker, int id, const u8* data, int len) {
  if (g_pool) g_pool->onResult(worker, id, data, len);
}
EMSCRIPTEN_KEEPALIVE void mcw_pool_error(int worker, int id) {
  if (g_pool) g_pool->onError(worker, id);
}
}

#endif

WorkerPool::WorkerPool(JobSystem& jobs) : jobs_(jobs) { g_pool = this; }

WorkerPool::~WorkerPool() {
#if MCW_WEB_WORKERS
  if (!workers_.empty()) mcw_js_workers_stop();
#endif
  if (g_pool == this) g_pool = nullptr;
}

bool WorkerPool::supported() { return MCW_WEB_WORKERS != 0; }

int WorkerPool::suggestedCount() {
#if MCW_WEB_WORKERS
  // Un núcleo para la página; el resto, hasta 4, para los workers
  return std::clamp(mcw_js_hw_concurrency() - 1, 1, 4);
#else
  return 0;
#endif
}

bool WorkerPool::start(int count, u64 seed, const std::vector<u8>& modelBundle, int expectedLayers) {
#if MCW_WEB_WORKERS
  expectedLayers_ = expectedLayers;
  const int started = mcw_js_workers_start(count, static_cast<unsigned>(seed & 0xFFFFFFFFu), static_cast<unsigned>(seed >> 32),
                                           modelBundle.data(), static_cast<int>(modelBundle.size()));
  workers_.assign(static_cast<std::size_t>(std::max(0, started)), Worker{});
  log::info("Web Workers: {} arrancando ({} KB de modelos)", started, modelBundle.size() / 1024);
  return started > 0;
#else
  (void)count; (void)seed; (void)modelBundle; (void)expectedLayers;
  return false;
#endif
}

void WorkerPool::setWorld(u64 seed, const std::string& generatorName, const std::string& options, bool structures) {
#if MCW_WEB_WORKERS
  mcw_js_workers_world(static_cast<unsigned>(seed & 0xFFFFFFFFu), static_cast<unsigned>(seed >> 32), generatorName.c_str(),
                       options.c_str(), structures ? 1 : 0);
#else
  (void)seed; (void)generatorName; (void)options; (void)structures;
#endif
}

int WorkerPool::readyCount() const {
  return static_cast<int>(std::count_if(workers_.begin(), workers_.end(), [](const Worker& w) { return w.ready && !w.failed; }));
}

int WorkerPool::capacity() const {
  int c = 0;
  for (const Worker& w : workers_)
    if (w.ready && !w.failed) c += std::max(0, kMaxPerWorker - w.inFlight);
  return c;
}

int WorkerPool::pickWorker() const {
  int best = -1;
  for (int i = 0; i < static_cast<int>(workers_.size()); i++) {
    const Worker& w = workers_[i];
    if (!w.ready || w.failed || w.inFlight >= kMaxPerWorker) continue;
    if (best < 0 || w.inFlight < workers_[best].inFlight) best = i;
  }
  return best;
}

bool WorkerPool::generate(ChunkPos p, ChunkDone done, Failed failed) {
#if MCW_WEB_WORKERS
  const int w = pickWorker();
  if (w < 0) return false;
  const int id = nextId_++;
  pending_[id] = Pending{w, false, std::move(done), {}, std::move(failed)};
  workers_[w].inFlight++;
  mcw_js_worker_gen(w, id, p.x, p.z);
  return true;
#else
  (void)p; (void)done; (void)failed;
  return false;
#endif
}

bool WorkerPool::mesh(const MeshInput& in, MeshDone done, Failed failed) {
#if MCW_WEB_WORKERS
  const int w = pickWorker();
  if (w < 0) return false;
  const int id = nextId_++;
  pending_[id] = Pending{w, true, {}, std::move(done), std::move(failed)};
  workers_[w].inFlight++;
  mcw_js_worker_mesh(w, id, reinterpret_cast<const u8*>(&in), static_cast<int>(sizeof(MeshInput)));
  return true;
#else
  (void)in; (void)done; (void)failed;
  return false;
#endif
}

void WorkerPool::onReady(int worker, int layers) {
  if (worker < 0 || worker >= static_cast<int>(workers_.size())) return;
  if (layers != expectedLayers_) {
    log::warn("Web Worker {}: {} capas de textura en vez de {}; no se usa", worker, layers, expectedLayers_);
    workers_[worker].failed = true;
    return;
  }
  workers_[worker].ready = true;
  if (readyCount() == static_cast<int>(workers_.size())) log::info("Web Workers: {} listos", readyCount());
}

void WorkerPool::onResult(int worker, int id, const u8* data, int len) {
  auto it = pending_.find(id);
  if (it == pending_.end()) return;
  Pending p = std::move(it->second);
  pending_.erase(it);
  if (p.worker >= 0 && p.worker < static_cast<int>(workers_.size())) workers_[p.worker].inFlight--;
  (void)worker;
  if (p.isMesh) {
    auto out = std::make_shared<MeshOutput>();
    if (!decodeMeshOutput(data, static_cast<std::size_t>(len), *out)) {
      fail(std::move(p));
      return;
    }
    jobs_.postToMain([done = std::move(p.meshDone), out] { done(std::move(*out)); });
  } else {
    auto chunk = decodeChunk(data, static_cast<std::size_t>(len));
    if (!chunk) {
      fail(std::move(p));
      return;
    }
    auto holder = std::make_shared<std::unique_ptr<Chunk>>(std::move(chunk));
    jobs_.postToMain([done = std::move(p.chunkDone), holder] { done(std::move(*holder)); });
  }
}

void WorkerPool::fail(Pending p) {
  if (p.failed) jobs_.postToMain(std::move(p.failed));
}

void WorkerPool::onError(int worker, int id) {
  if (id > 0) {
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    Pending p = std::move(it->second);
    pending_.erase(it);
    if (p.worker >= 0 && p.worker < static_cast<int>(workers_.size())) workers_[p.worker].inFlight--;
    fail(std::move(p));
    return;
  }
  // El worker entero ha fallado: no se usa más y sus trabajos se repiten en otro sitio
  if (worker < 0 || worker >= static_cast<int>(workers_.size())) return;
  workers_[worker].failed = true;
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.worker == worker) {
      fail(std::move(it->second));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  workers_[worker].inFlight = 0;
}

}  // namespace mcw
