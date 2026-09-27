#include "core/jobs.h"

#include <algorithm>
#include <chrono>

#include "core/log.h"

namespace mcw {

JobSystem::JobSystem(int threads) {
  for (int i = 0; i < threads; i++) workers_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) t.join();
}

int JobSystem::defaultThreadCount() {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  return 0;
#else
  const int hw = static_cast<int>(std::thread::hardware_concurrency());
#if defined(__EMSCRIPTEN__)
  return std::clamp(hw - 1, 1, 4);  // el pool de pthreads del build web es de 6
#else
  return std::clamp(hw - 2, 1, 8);
#endif
#endif
}

void JobSystem::submit(std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void JobSystem::postToMain(std::function<void()> fn) {
  std::lock_guard lock(mainMutex_);
  mainTasks_.push_back(std::move(fn));
}

std::size_t JobSystem::queued() const {
  std::lock_guard lock(mutex_);
  return jobs_.size();
}

void JobSystem::workerLoop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
      if (stop_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    try {
      job();
    } catch (const std::exception& e) {
      log::error("excepción en un trabajo: {}", e.what());
    }
  }
}

void JobSystem::pump(double budgetMs) {
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(budgetMs);

  if (workers_.empty()) {
    while (clock::now() < deadline) {
      std::function<void()> job;
      {
        std::lock_guard lock(mutex_);
        if (jobs_.empty()) break;
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job();
    }
  }

  std::deque<std::function<void()>> tasks;
  {
    std::lock_guard lock(mainMutex_);
    tasks.swap(mainTasks_);
  }
  while (!tasks.empty()) {
    tasks.front()();
    tasks.pop_front();
    if (clock::now() >= deadline && !tasks.empty()) {
      // Devolver lo que no ha dado tiempo a ejecutar, manteniendo el orden.
      std::lock_guard lock(mainMutex_);
      mainTasks_.insert(mainTasks_.begin(), std::make_move_iterator(tasks.begin()), std::make_move_iterator(tasks.end()));
      break;
    }
  }
}

}  // namespace mcw
