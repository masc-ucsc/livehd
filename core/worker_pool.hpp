//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace livehd {

// Every worker this header starts gets this stack. Darwin's default for a
// SECONDARY thread is 512 KiB (the main thread gets 8 MiB), and LiveHD's
// recursive graph walks -- and ABC's own recursive DFS/mapping helpers -- blow
// straight through that in -c dbg (the CVA6 `Bus error: 10`, 2026-08-21).
inline constexpr size_t kWorkerStackBytes = 8U * 1024U * 1024U;

namespace worker_pool_detail {

struct Worker_args {
  const std::function<void(size_t)>* work  = nullptr;
  std::exception_ptr*                error = nullptr;
  size_t                             index = 0;
};

inline void* run_worker(void* opaque) noexcept {
  auto* args = static_cast<Worker_args*>(opaque);
  try {
    (*args->work)(args->index);
  } catch (...) {
    *args->error = std::current_exception();
  }
  return nullptr;
}

inline void* run_task(void* opaque) noexcept {
  std::unique_ptr<std::packaged_task<void()>> task(static_cast<std::packaged_task<void()>*>(opaque));
  (*task)();  // packaged_task parks any exception in the future
  return nullptr;
}

// pthread_attr_t carrying kWorkerStackBytes; throws like the pool below does.
class Worker_attr {
public:
  Worker_attr() {
    int rc = pthread_attr_init(&attr_);
    if (rc != 0) {
      throw std::system_error(rc, std::generic_category(), "pthread_attr_init");
    }
    rc = pthread_attr_setstacksize(&attr_, std::max(kWorkerStackBytes, static_cast<size_t>(PTHREAD_STACK_MIN)));
    if (rc != 0) {
      pthread_attr_destroy(&attr_);
      throw std::system_error(rc, std::generic_category(), "pthread_attr_setstacksize");
    }
  }
  Worker_attr(const Worker_attr&)            = delete;
  Worker_attr& operator=(const Worker_attr&) = delete;
  ~Worker_attr() { pthread_attr_destroy(&attr_); }

  [[nodiscard]] const pthread_attr_t* get() const { return &attr_; }

private:
  pthread_attr_t attr_{};
};

}  // namespace worker_pool_detail

// ONE big-stack worker, launched and joined individually — the shape a dynamic
// scheduler needs (run_workers below is fork/join over a fixed count). Same
// kWorkerStackBytes rationale; `std::async`/`std::thread` would hand the job
// Darwin's 512 KiB default instead.
class Async_worker {
public:
  Async_worker()                               = default;
  Async_worker(const Async_worker&)            = delete;
  Async_worker& operator=(const Async_worker&) = delete;
  ~Async_worker() { join(); }

  // Starts `work` on a fresh thread. Throws (leaving the worker idle) when the
  // thread cannot be created; the work's own exception surfaces from get().
  template <typename Work>
  void start(Work&& work) {
    auto task = std::make_unique<std::packaged_task<void()>>(std::forward<Work>(work));
    auto fut  = task->get_future();

    const worker_pool_detail::Worker_attr attr;
    pthread_t                             thread{};
    const int                             rc = pthread_create(&thread, attr.get(), worker_pool_detail::run_task, task.get());
    if (rc != 0) {
      throw std::system_error(rc, std::generic_category(), "pthread_create");
    }
    (void)task.release();  // run_task owns it now
    future_  = std::move(fut);
    thread_  = thread;
    running_ = true;
  }

  // True while a job is in flight (i.e. started and not yet joined).
  [[nodiscard]] bool valid() const { return running_; }
  // Non-blocking: has the job finished? (The thread may still be exiting.)
  [[nodiscard]] bool ready() const { return running_ && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
  // Join the thread and re-throw whatever the job threw. valid() then false.
  void               get() {
    join();
    if (future_.valid()) {
      future_.get();
    }
  }
  // Join the thread, discarding any exception the job parked.
  void join() {
    if (!running_) {
      return;
    }
    pthread_join(thread_, nullptr);
    running_ = false;
  }

private:
  std::future<void> future_;
  pthread_t         thread_{};
  bool              running_ = false;
};

// Run exactly worker_count workers and join them before returning. pthreads are
// used instead of std::thread so recursive compiler walks get a predictable
// stack on platforms whose secondary-thread default is small.
template <typename Work>
void run_workers(size_t worker_count, Work&& work) {
  if (worker_count == 0) {
    return;
  }

  std::function<void(size_t)>                  callable(std::forward<Work>(work));
  std::vector<pthread_t>                       threads(worker_count);
  std::vector<std::exception_ptr>              errors(worker_count);
  std::vector<worker_pool_detail::Worker_args> args(worker_count);

  const worker_pool_detail::Worker_attr attr;

  int    rc      = 0;
  size_t started = 0;
  for (; started < worker_count; ++started) {
    args[started] = {&callable, &errors[started], started};
    rc            = pthread_create(&threads[started], attr.get(), worker_pool_detail::run_worker, &args[started]);
    if (rc != 0) {
      break;
    }
  }

  for (size_t i = 0; i < started; ++i) {
    const int join_rc = pthread_join(threads[i], nullptr);
    if (rc == 0 && join_rc != 0) {
      rc = join_rc;
    }
  }
  if (rc != 0) {
    throw std::system_error(rc, std::generic_category(), "pthread worker pool");
  }
  for (const auto& error : errors) {
    if (error) {
      std::rethrow_exception(error);
    }
  }
}

}  // namespace livehd
