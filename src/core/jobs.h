#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mcw {

/// Pool de hilos simple. Con 0 hilos (web sin pthreads) los trabajos se ejecutan en `pump()`
/// desde el hilo principal, con un presupuesto de tiempo por frame.
class JobSystem {
 public:
  explicit JobSystem(int threads);
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  void submit(std::function<void()> job);
  /// Encola trabajo para el hilo principal (p.ej. subir una malla a la GPU).
  void postToMain(std::function<void()> fn);
  /// Hilo principal: ejecuta tareas para el hilo principal y, sin hilos, trabajos pendientes.
  void pump(double budgetMs);

  /// Espera a que no quede ningún trabajo en cola ni ejecutándose (antes de destruir lo que usan).
  void waitIdle();

  int threadCount() const { return static_cast<int>(workers_.size()); }
  std::size_t queued() const;
  /// Número recomendado de hilos de trabajo para esta máquina.
  static int defaultThreadCount();

 private:
  void workerLoop();

  std::vector<std::thread> workers_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  std::mutex mainMutex_;
  std::deque<std::function<void()>> mainTasks_;
  std::atomic<bool> stop_{false};
  std::atomic<int> running_{0};
};

}  // namespace mcw
