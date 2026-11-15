// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

namespace livehd::process_group {
// Kill a worker and every process it forked. The racer leads its own group
// (setpgid in both parent and child, so the group exists before any kill);
// the plain kill covers a worker whose setpgid failed.
inline void kill_tree(pid_t p) {
  ::kill(-p, SIGKILL);
  ::kill(p, SIGKILL);
}

struct Parent_watch {
  pid_t parent;
  bool  own_group;
};

inline void* parent_watch_main(void* arg) {
  const Parent_watch w = *static_cast<Parent_watch*>(arg);
  delete static_cast<Parent_watch*>(arg);
  for (;;) {
    if (::getppid() != w.parent) {
      // Orphaned (the parent was killed alone): take the worker's subtree down
      // too -- but only when the group is ours; otherwise it is the caller's
      // job group (a `| tee` sibling, the shell's pipeline), never ours to kill.
      if (w.own_group) {
        ::kill(0, SIGKILL);
      }
      ::_exit(0);
    }
    ::usleep(100 * 1000);
  }
  return nullptr;
}

// Child side, right after fork: lead a new process group (see kill_tree) and
// start a detached thread that ends the worker's group once `parent` is gone.
// A failed thread start only loses the parent-death cleanup, never a verdict.
inline void watch_parent(pid_t parent) {
  const bool     own_group = ::setpgid(0, 0) == 0;
  pthread_attr_t attr;
  if (::pthread_attr_init(&attr) != 0) {
    return;
  }
  ::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  // Keep the platform default stack: linked libraries' static TLS can exceed
  // 64 KiB, in which case pthread_create rejects a smaller stack with EINVAL
  // and no parent-death watchdog runs. It still counts against RLIMIT_AS.
  auto*     w = new Parent_watch{parent, own_group};
  pthread_t t;
  if (::pthread_create(&t, &attr, parent_watch_main, w) != 0) {
    delete w;
  }
  ::pthread_attr_destroy(&attr);
}

}  // namespace livehd::process_group
