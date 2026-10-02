// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ── Generic fork-per-method race harness (F3, 2f-fcore) ─────────────────────
// The shared parallel-proof primitive extracted from run_auto_portfolio's
// ind|bmc fork race so BOTH lec (Query_result method ladder) and verify
// (Verify_result strategy race) run over the same fork/pipe/poll/reap loop.
// Forks one child process per method; each runs its method closure, serializes
// its result over a pipe, and _exit()s (no atexit/dtors). The parent polls; the
// FIRST result that satisfies `trust` cancels (SIGKILL) the remaining children.
// If no result is trustworthy, every child runs to completion (or to the wall
// deadline) and all results are collected for the caller to merge.
//
// cvc5 cross-instance thread-safety is unverified, so methods race as PROCESSES
// (never threads). Children fork from a clean state (no cvc5 TermManager in the
// parent yet), so each builds its own solver — the CALLER MUST invoke this
// before constructing any cvc5 object in the parent.
//
// WALL DEADLINE. cvc5's tlimit-per bounds a checkSat, NOT the preprocessing a
// push()/refreshAssertions runs (NonClausalSimp, SubstitutionMap::apply) nor a
// single eager CaDiCaL solve, so a racer CAN outlive formal.timeout without
// bound: a parent that waited with poll(-1) turned formal.timeout=60 into a 15+
// minute run (lhdtrack br_fifo_shared_dynamic_flops asap7, 660 s "timeout").
// `deadline_ms` > 0 makes the parent poll with finite timeouts and, once the
// deadline passes, SIGKILL every child still running, reap it, and mark it
// `timed_out` with no result. A killed racer can only LOSE information: the
// caller reports it as Unknown, never as a verdict. `deadline_ms` <= 0 waits
// for the children forever (the old behavior). See race_deadline_ms().
//
// R must be default-constructible. `run_method(i)` runs method i and returns R
// (called ONLY in the forked child). `ser`/`deser` are the wire codec.
// `trust(i, r)` decides whether result i is a definitive winner. Returns
// forked=false when fork/pipe is unavailable so the caller runs its own
// in-process sequential fallback. A child that dies without a valid result
// leaves results[i] == R{} with done[i] == true (the caller tags it); a child
// the deadline killed leaves results[i] == R{}, done[i] == false and
// timed_out[i] == true.
//
// TEST HOOK: LIVEHD_LEC_RACER_STALL_S=<secs> makes every racer sleep that long
// before running its method — the stand-in for a cvc5 preprocessing step that
// never returns, so the deadline path is testable end to end.
// LIVEHD_LEC_RACER_STALL_ONLY=<i> limits the stall to racer i, so a test can
// wall-kill one strategy while its sibling finishes (verify: 0 = bmc-first).
// `racer_id_base` offsets that id for a caller that runs its methods as
// successive one-racer races (verify's cache-active path: bmc-first = 0, then
// ind-first = 1), so the hook names the same strategy on both paths.
//
// WHOLE-SUBTREE KILL. A racer is not always a leaf: the ABC cone pre-pass
// (cone_abc.cpp) forks an ABC child whose stall/deadline clocks only the racer
// enforces, and ABC rewriting/fraiging has no clock of its own. SIGKILLing just
// the racer pid reparented that grandchild to init, where it burned a core for
// minutes. So each racer leads its own process group and the parent kills the
// GROUP (kill_tree). Leaving lhd's group would in turn hide racers from an outer
// `killpg(lhd)` watchdog or a Ctrl-C, so each racer also watches its parent
// (watch_parent) and kills its own group the moment the parent is gone.

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "host_mem.hpp"
#include "query.hpp"

namespace livehd::lec {

template <class R>
struct Race_result {
  bool              forked       = true;   // false => run the sequential fallback
  int               winner       = -1;     // first trustworthy method index, or -1
  bool              deadline_hit = false;  // the wall deadline fired (>= 1 racer killed)
  long long         deadline_ms  = 0;      // the deadline the race ran under (0 = none)
  long long         elapsed_ms   = 0;      // parent wall time of the whole race
  std::vector<R>    results;               // per-method result (default R{} if a child died / was killed)
  std::vector<bool> done;                  // whether method i produced any result
  std::vector<bool> timed_out;             // method i was SIGKILLed at the deadline (no result)
};

namespace race_detail {

inline void write_fd_all(int fd, const char* p, size_t n) {
  while (n > 0) {
    ssize_t w = ::write(fd, p, n);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
}

// Kill a racer and every process it forked. The racer leads its own group
// (setpgid in both parent and child, so the group exists before any kill);
// the plain kill covers a racer whose setpgid failed.
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
      // Orphaned (the parent was killed alone): take the racer's subtree down
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
// start a detached thread that ends the racer's group once `parent` is gone.
// A failed thread start only loses the parent-death cleanup, never a verdict.
inline void watch_parent(pid_t parent) {
  const bool own_group = ::setpgid(0, 0) == 0;
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

inline void maybe_test_stall(int i) {
  if (const char* only = std::getenv("LIVEHD_LEC_RACER_STALL_ONLY"); only != nullptr && *only != '\0') {
    if (std::strtol(only, nullptr, 10) != i) {
      return;  // stall just the named racer (e.g. 0 = verify's bmc-first)
    }
  }
  if (const char* s = std::getenv("LIVEHD_LEC_RACER_STALL_S"); s != nullptr && *s != '\0') {
    const long secs = std::strtol(s, nullptr, 10);
    if (secs > 0) {
      ::sleep(static_cast<unsigned>(secs));
    }
  }
}

}  // namespace race_detail

template <class R>
Race_result<R> fork_race(int nmethods, const std::function<R(int)>& run_method, const std::function<std::string(const R&)>& ser,
                         const std::function<bool(std::string_view, R&)>& deser, const std::function<bool(int, const R&)>& trust,
                         long long deadline_ms = 0, int racer_id_base = 0) {
  Race_result<R> out;
  out.results.assign(static_cast<size_t>(nmethods), R{});
  out.done.assign(static_cast<size_t>(nmethods), false);
  out.timed_out.assign(static_cast<size_t>(nmethods), false);
  out.deadline_ms = deadline_ms > 0 ? deadline_ms : 0;
  const auto t0   = std::chrono::steady_clock::now();
  auto       ms   = [&t0]() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
  };

  std::vector<int>   rfd(nmethods, -1), wfd(nmethods, -1);
  std::vector<pid_t> pid(nmethods, -1);
  bool               fork_ok = true;
  const pid_t        self    = ::getpid();
  for (int i = 0; i < nmethods; ++i) {
    int p[2];
    if (::pipe(p) != 0) {
      fork_ok = false;
      break;
    }
    rfd[i]  = p[0];
    wfd[i]  = p[1];
    pid_t c = ::fork();
    if (c < 0) {
      fork_ok = false;
      break;
    }
    if (c == 0) {
      race_detail::watch_parent(self);  // own group + die with the parent (see WHOLE-SUBTREE KILL)
      // child i: keep only its own write fd (the parent already closed every
      // earlier child's write end), run its method, serialize, _exit.
      for (int j = 0; j <= i; ++j) {
        if (rfd[j] >= 0) {
          ::close(rfd[j]);
        }
        if (j != i && wfd[j] >= 0) {
          ::close(wfd[j]);
        }
      }
      // Take only a 1/nmethods share of the process memory budget. RLIMIT_AS is
      // per-process and inherited, so without this every racer may allocate the
      // WHOLE budget and the tree totals nmethods x budget -- the host-killing
      // case the backstop exists to prevent. A child that outgrows its share dies
      // and reports no result, which the parent already tags as Unknown (never a
      // false verdict). Must happen before run_method builds any cvc5 object.
      if (const uint64_t share = livehd::cost::arm_child_share(nmethods);
          share != 0 && std::getenv("LIVEHD_MEMORY_DEBUG") != nullptr) {
        std::fprintf(stderr,
                     "lec: racer %d/%d capped (RLIMIT_AS = %llu MiB)\n",
                     i,
                     nmethods,
                     static_cast<unsigned long long>(share >> 20));
      }
      race_detail::maybe_test_stall(racer_id_base + i);
      R           r    = run_method(i);
      std::string blob = frame_blob(ser(r));
      race_detail::write_fd_all(wfd[i], blob.data(), blob.size());
      ::close(wfd[i]);
      ::_exit(0);
    }
    pid[i] = c;
    ::setpgid(c, c);  // also here: the group must exist before any kill_tree(c)
    ::close(wfd[i]);  // parent never writes
    wfd[i] = -1;
  }

  if (!fork_ok) {
    for (int j = 0; j < nmethods; ++j) {
      if (rfd[j] >= 0) {
        ::close(rfd[j]);
      }
      if (wfd[j] >= 0) {
        ::close(wfd[j]);
      }
      if (pid[j] > 0) {
        race_detail::kill_tree(pid[j]);
        int st = 0;
        while (::waitpid(pid[j], &st, 0) < 0 && errno == EINTR) {
        }
      }
    }
    out.forked = false;
    return out;
  }

  std::vector<std::string> bufs(nmethods);
  int                      remaining = nmethods;
  while (remaining > 0 && out.winner < 0) {
    // Finite poll: never block past the deadline. The cap of 1 s per poll is
    // only a re-check cadence; a child's EOF still wakes the poll immediately.
    int wait_ms = -1;
    if (out.deadline_ms > 0) {
      const long long left = out.deadline_ms - ms();
      if (left <= 0) {
        out.deadline_hit = true;
        break;
      }
      wait_ms = static_cast<int>(left < 1000 ? left : 1000);
    }
    std::vector<struct pollfd> pfds;
    std::vector<int>           map;
    for (int i = 0; i < nmethods; ++i) {
      if (!out.done[i]) {
        struct pollfd pf;
        pf.fd      = rfd[i];
        pf.events  = POLLIN;
        pf.revents = 0;
        pfds.push_back(pf);
        map.push_back(i);
      }
    }
    int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), wait_ms);
    if (pr == 0) {
      continue;  // nothing yet; the top of the loop re-checks the deadline
    }
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (size_t k = 0; k < pfds.size() && out.winner < 0; ++k) {
      if (pfds[k].revents == 0) {
        continue;
      }
      int     i = map[k];
      char    tmp[8192];
      ssize_t n = ::read(rfd[i], tmp, sizeof tmp);
      if (n > 0) {
        bufs[i].append(tmp, static_cast<size_t>(n));
        continue;  // worker may still be writing; EOF (n==0) marks done
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      out.done[i] = true;
      --remaining;
      std::string_view payload;
      if (!unframe_blob(bufs[i], payload) || !deser(payload, out.results[i])) {
        out.results[i] = R{};  // truncated/absent frame: no result, never a partial verdict
      }
      if (trust(i, out.results[i])) {
        out.winner = i;
      }
    }
  }
  // Reap: kill any worker still running (the losers, or every racer the
  // deadline caught) together with everything it forked, wait on all (no
  // zombies). A racer killed by the deadline
  // is recorded as timed_out with the default (no-verdict) result: whatever
  // partial bytes it wrote are discarded, never deserialized.
  for (int i = 0; i < nmethods; ++i) {
    if (!out.done[i] && pid[i] > 0) {
      race_detail::kill_tree(pid[i]);
      if (out.winner < 0 && out.deadline_hit) {
        out.timed_out[i] = true;
        out.results[i]   = R{};
      }
    }
    if (rfd[i] >= 0) {
      ::close(rfd[i]);
    }
  }
  for (int i = 0; i < nmethods; ++i) {
    if (pid[i] > 0) {
      int st = 0;
      while (::waitpid(pid[i], &st, 0) < 0 && errno == EINTR) {
      }
    }
  }
  out.elapsed_ms = ms();
  return out;
}

}  // namespace livehd::lec
