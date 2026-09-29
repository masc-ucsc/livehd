// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "worker_pool.hpp"

#include <pthread.h>

#include <exception>
#include <vector>

namespace livehd {

namespace {

struct Worker_slot {
  const std::function<void(size_t)>* body  = nullptr;
  size_t                             index = 0;
  std::exception_ptr                 error;
  pthread_t                          tid{};
  bool                               spawned = false;
};

void* worker_main(void* arg) {
  auto* slot = static_cast<Worker_slot*>(arg);
  // An exception must never escape a pthread start routine: that would call
  // std::terminate instead of surfacing the pass error on the caller.
  try {
    (*slot->body)(slot->index);
  } catch (...) {
    slot->error = std::current_exception();
  }
  return nullptr;
}

}  // namespace

// pthreads directly, not std::thread, because the stack size is the whole
// point and std::thread cannot set it.
void run_workers(size_t n_workers, const std::function<void(size_t)>& body, size_t stack_bytes) {
  std::vector<Worker_slot> slots(n_workers);

  pthread_attr_t attr;
  const bool     have_attr = pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, stack_bytes) == 0;

  for (size_t i = 0; i < n_workers; ++i) {
    auto& slot = slots[i];
    slot.body  = &body;
    slot.index = i;
    if (have_attr && pthread_create(&slot.tid, &attr, worker_main, &slot) == 0) {
      slot.spawned = true;
    }
  }
  if (have_attr) {
    pthread_attr_destroy(&attr);
  }

  // Whatever could not get a thread runs here, on the caller, once the spawned
  // workers are already racing it for the shared cursor.
  for (auto& slot : slots) {
    if (!slot.spawned) {
      worker_main(&slot);
    }
  }
  for (auto& slot : slots) {
    if (slot.spawned) {
      pthread_join(slot.tid, nullptr);
    }
  }
  for (auto& slot : slots) {
    if (slot.error) {
      std::rethrow_exception(slot.error);
    }
  }
}

}  // namespace livehd
