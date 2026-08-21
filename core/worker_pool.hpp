//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <pthread.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace livehd {

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

}  // namespace worker_pool_detail

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

  pthread_attr_t attr;
  int            rc = pthread_attr_init(&attr);
  if (rc != 0) {
    throw std::system_error(rc, std::generic_category(), "pthread_attr_init");
  }

  constexpr size_t desired_stack = 8U * 1024U * 1024U;
  const size_t     stack_size    = std::max(desired_stack, static_cast<size_t>(PTHREAD_STACK_MIN));
  rc                             = pthread_attr_setstacksize(&attr, stack_size);
  if (rc != 0) {
    pthread_attr_destroy(&attr);
    throw std::system_error(rc, std::generic_category(), "pthread_attr_setstacksize");
  }

  size_t started = 0;
  for (; started < worker_count; ++started) {
    args[started] = {&callable, &errors[started], started};
    rc            = pthread_create(&threads[started], &attr, worker_pool_detail::run_worker, &args[started]);
    if (rc != 0) {
      break;
    }
  }
  pthread_attr_destroy(&attr);

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
