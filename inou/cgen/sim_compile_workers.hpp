// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "sim_compile_jobs.hpp"
#include "worker_pool.hpp"

namespace livehd::sim {
// Bound outstanding work, including queued jobs. Completion by ANY worker
// releases a submitter; a slow first job never leaves the other CPUs idle.
// Move-only jobs own their LLVM context/module and never share LLVM state.
class Compile_workers {
public:
  explicit Compile_workers(unsigned jobs = 0) : limit_(jobs == 0 ? available_compile_jobs() : jobs) {}
  Compile_workers(const Compile_workers&)            = delete;
  Compile_workers& operator=(const Compile_workers&) = delete;

  ~Compile_workers() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) {
      worker->join();
    }
  }

  template <class Fn>
  auto submit(Fn&& fn) -> std::future<std::invoke_result_t<Fn>> {
    std::packaged_task<std::invoke_result_t<Fn>()> job(std::forward<Fn>(fn));
    auto                                           result = job.get_future();
    std::unique_lock                               lock(mutex_);
    space_.wait(lock, [&] { return outstanding_ < limit_; });
    // Start threads on demand: a one-object module need not launch hundreds.
    // Big-stack workers (livehd::Async_worker): LLVM's SelectionDAG legalization
    // of a very wide value recurses deeply, and a Darwin secondary thread gets
    // only 512 KiB by default.
    if (workers_.size() < outstanding_ + 1) {
      workers_.push_back(std::make_unique<livehd::Async_worker>());
      workers_.back()->start([this] { run(); });
    }
    jobs_.emplace_back([job = std::move(job)]() mutable { job(); });
    ++outstanding_;
    lock.unlock();
    ready_.notify_one();
    return result;
  }

private:
  void run() {
    for (;;) {
      std::packaged_task<void()> job;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) {
          return;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job();  // packaged_task transports exceptions to the caller's future
      {
        std::lock_guard lock(mutex_);
        --outstanding_;
      }
      space_.notify_one();
    }
  }

  const size_t                           limit_;
  std::mutex                             mutex_;
  std::condition_variable                ready_, space_;
  std::deque<std::packaged_task<void()>> jobs_;
  std::vector<std::unique_ptr<livehd::Async_worker>> workers_;
  size_t                                 outstanding_ = 0;
  bool                                   stopping_    = false;
};
}  // namespace livehd::sim
