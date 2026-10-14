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
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "host_mem.hpp"
#include "job_manager.hpp"
#include "query.hpp"

namespace livehd::lec {

template <class R>
struct Race_result {
  bool                     forked       = true;   // false => run the sequential fallback
  int                      winner       = -1;     // first trustworthy method index, or -1
  bool                     deadline_hit = false;  // the wall deadline fired (>= 1 racer killed)
  long long                deadline_ms  = 0;      // the deadline the race ran under (0 = none)
  long long                elapsed_ms   = 0;      // parent wall time of the whole race
  std::vector<R>           results;               // per-method result (default R{} if a child died / was killed)
  std::vector<bool>        done;                  // whether method i produced any result
  std::vector<bool>        timed_out;             // method i was SIGKILLed at the deadline (no result)
  // Why method i DIED without a valid result ("" = it did not): the decoded wait
  // status (child_death_cause). An out-of-memory death says "out of memory", so
  // the caller's undecided detail classifies TIMEOUT-memory; a crash says
  // "engine crash" (REFUTED-unexplained, a bug to report).
  std::vector<std::string> death;
  std::vector<bool>        started, queued;
  std::vector<long long>   arm_ms;
};

// Exit codes a forked solver child uses when an exception escaped its method:
// the child must never unwind into the parent's stack frames (it would go on
// running the caller's code as a second copy of the process), so it reports the
// escape through its exit status instead of a serialized result.
// std::bad_alloc, or a failed allocation the fatal-signal handler caught at the
// RLIMIT_AS cap (lhd_kernel_common's crash handler): the memory backstop denied
// an allocation.
inline constexpr int kChildExitBadAlloc  = 86;
inline constexpr int kChildExitException = 87;  // any other exception escaped the method

// Decode why a forked solver child produced no valid result. `nsiblings` is how
// many children shared the memory budget (arm_child_share gives each a
// 1/nsiblings RLIMIT_AS share), `killed_by_us` that the parent itself sent the
// SIGKILL (the wall backstop -- the caller words that one). The memory cases
// say "out of memory" (TIMEOUT-memory); everything else is an engine crash.
//   - std::bad_alloc in the child, or a SIGSEGV the lhd crash handler caught with
//     the address space at its RLIMIT_AS cap (kChildExitBadAlloc): out of memory.
//   - SIGKILL the parent did not send: the OS / cgroup out-of-memory killer.
//   - SIGSEGV / SIGABRT / SIGBUS / SIGTRAP with the RLIMIT_AS backstop armed and
//     the child's peak RSS at >= 1/4 of its address-space share: malloc failed at
//     the backstop and a solver that does not null-check it died (cvc5/ABC/
//     CaDiCaL abort or fault instead of throwing). Far below the share it is a
//     genuine crash, never blamed on memory.
inline std::string child_death_cause(int status, const struct rusage& usage, int nsiblings, bool killed_by_us) {
  std::string why;
#ifdef __APPLE__
  const uint64_t peak = static_cast<uint64_t>(usage.ru_maxrss);
#else
  const uint64_t peak = static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
  const auto mib = [](uint64_t b) { return std::to_string(b >> 20) + " MiB"; };
  if (WIFEXITED(status)) {
    const int code = WEXITSTATUS(status);
    if (code == kChildExitBadAlloc) {
      return "out of memory: the memory backstop denied an allocation in the forked solver (std::bad_alloc, or a crash "
             "at the RLIMIT_AS cap; peak RSS "
             + mib(peak) + ")";
    }
    if (code == kChildExitException) {
      return "engine crash: an exception escaped the forked solver (exit " + std::to_string(code) + ")";
    }
    return code == 0 ? std::string{"engine crash: the forked solver exited without a serialized result"}
                     : "engine crash: the forked solver exited with status " + std::to_string(code);
  }
  if (!WIFSIGNALED(status)) {
    return "engine crash: the forked solver stopped without a result";
  }
  const int sig = WTERMSIG(status);
  why           = "killed by signal " + std::to_string(sig);
  if (const char* sn = ::strsignal(sig); sn != nullptr) {
    why += std::string(" (") + sn + ")";
  }
  if (killed_by_us) {
    return why;
  }
  if (sig == SIGKILL) {
    return why + "; out of memory: a SIGKILL LiveHD did not send is the OS/cgroup out-of-memory killer (peak RSS " + mib(peak)
           + ")";
  }
  if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS || sig == SIGTRAP) {
    struct rlimit lim{};
    uint64_t      cap = 0;
    if (::getrlimit(RLIMIT_AS, &lim) == 0 && lim.rlim_cur != RLIM_INFINITY) {
      cap = static_cast<uint64_t>(lim.rlim_cur) / static_cast<uint64_t>(std::max(1, nsiblings));
    }
    if (cap != 0 && peak >= cap / 4) {
      return why + "; out of memory: the child reached the memory backstop (RLIMIT_AS share ~" + mib(cap) + ", peak RSS "
             + mib(peak) + ") and its solver died on a failed allocation";
    }
    return why + "; engine crash (peak RSS " + mib(peak) + (cap != 0 ? ", far below the " + mib(cap) + " memory backstop" : "")
           + "): a LiveHD bug to report";
  }
  return why + "; engine crash";
}

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
                         long long deadline_ms = 0, int racer_id_base = 0, uint64_t job_units = 1,
                         const std::function<int(int)>&                   stall_id   = {},
                         const std::function<int(const Race_result<R>&)>& resolve    = {},
                         const std::function<uint64_t(int)>&              work_units = {}) {
  auto&                      manager = livehd::Job_manager::global();
  livehd::Job_manager::Yield parent_slot(manager);
  Race_result<R>             out;
  out.started.assign(nmethods, false);
  out.queued.assign(nmethods, false);
  out.arm_ms.assign(nmethods, 0);
  std::vector<long long> started_at(nmethods, 0);
  out.results.assign(static_cast<size_t>(nmethods), R{});
  out.done.assign(static_cast<size_t>(nmethods), false);
  out.timed_out.assign(static_cast<size_t>(nmethods), false);
  out.death.assign(static_cast<size_t>(nmethods), std::string{});
  out.deadline_ms = deadline_ms > 0 ? deadline_ms : 0;
  const auto t0   = std::chrono::steady_clock::now();
  auto       ms   = [&t0]() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
  };

  std::vector<int>              rfd(nmethods, -1), wfd(nmethods, -1);
  std::vector<pid_t>            pid(nmethods, -1);
  bool                          fork_ok = true;
  const pid_t                   self    = ::getpid();
  std::vector<livehd::Job_slot> leases(nmethods);
  auto                          launch = [&](int i) {
    auto lease = manager.try_acquire(livehd::Job_class::formal_arm, work_units ? work_units(i) : job_units, 0, !out.queued[i]);
    if (!lease) {
      out.queued[i] = true;
      return false;
    }
    int p[2];
    if (::pipe(p) != 0) {
      fork_ok = false;
      return false;
    }
    rfd[i]  = p[0];
    wfd[i]  = p[1];
    pid_t c = ::fork();
    if (c < 0) {
      fork_ok = false;
      return false;
    }
    if (c == 0) {
      lease.adopt(::getpid());          // claim before nested dispatch can yield this slot
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
      if (const uint64_t share = livehd::cost::arm_child_share(std::min<unsigned>(nmethods, manager.ceiling()));
          share != 0 && std::getenv("LIVEHD_MEMORY_DEBUG") != nullptr) {
        std::fprintf(stderr,
                     "lec: racer %d/%d capped (RLIMIT_AS = %llu MiB)\n",
                     i,
                     nmethods,
                     static_cast<unsigned long long>(share >> 20));
      }
      race_detail::maybe_test_stall(stall_id ? stall_id(i) : racer_id_base + i);
      // Nothing may unwind out of the child: it would continue in the CALLER's
      // frames as a second copy of the process. An escape exits with a code
      // the parent decodes (child_death_cause).
      try {
        R           r    = run_method(i);
        std::string blob = frame_blob(ser(r));
        race_detail::write_fd_all(wfd[i], blob.data(), blob.size());
      } catch (const std::bad_alloc&) {
        ::_exit(kChildExitBadAlloc);
      } catch (...) {
        ::_exit(kChildExitException);
      }
      ::close(wfd[i]);
      ::_exit(0);
    }
    out.started[i] = true;
    started_at[i]  = ms();
    leases[i]      = std::move(lease);
    leases[i].adopt(c);
    pid[i] = c;
    ::setpgid(c, c);  // also here: the group must exist before any kill_tree(c)
    ::close(wfd[i]);  // parent never writes
    wfd[i] = -1;
    return true;
  };
  int next = 0;
  while (next < nmethods && launch(next)) {
    ++next;
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
    while (next < nmethods && launch(next)) {
      ++next;
    }
    if (!fork_ok) {
      break;
    }
    // Finite poll: never block past the deadline. The cap of 1 s per poll is
    // only a re-check cadence; a child's EOF still wakes the poll immediately.
    int wait_ms = next < nmethods ? 10 : -1;
    if (out.deadline_ms > 0) {
      const long long left = out.deadline_ms - ms();
      if (left <= 0) {
        out.deadline_hit = true;
        break;
      }
      wait_ms = static_cast<int>(std::min<long long>(left, next < nmethods ? 10 : 1000));
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
      int           status = 0;
      struct rusage usage{};
      while (::wait4(pid[i], &status, 0, &usage) < 0 && errno == EINTR) {
      }
#ifdef __APPLE__
      leases[i].complete(static_cast<uint64_t>(usage.ru_maxrss));
#else
      leases[i].complete(static_cast<uint64_t>(usage.ru_maxrss) * 1024);
#endif
      manager.reap_tree(pid[i]);
      pid[i]        = -1;
      out.done[i]   = true;
      out.arm_ms[i] = ms() - started_at[i];
      --remaining;
      std::string_view payload;
      if (!unframe_blob(bufs[i], payload) || !deser(payload, out.results[i])) {
        out.results[i] = R{};  // truncated/absent frame: no result, never a partial verdict
        out.death[i]   = child_death_cause(status, usage, std::min<int>(nmethods, static_cast<int>(manager.ceiling())), false);
      }
      if (trust(i, out.results[i])) {
        out.winner = i;
      } else if (resolve) {
        out.winner = resolve(out);
      }
    }
  }
  // Reap: kill any worker still running (the losers, or every racer the
  // deadline caught) together with everything it forked, wait on all (no
  // zombies). A racer killed by the deadline
  // is recorded as timed_out with the default (no-verdict) result: whatever
  // partial bytes it wrote are discarded, never deserialized.
  for (int i = 0; i < nmethods; ++i) {
    if (!out.done[i] && out.deadline_hit && out.winner < 0) {
      out.timed_out[i] = true;
    }
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
      int           st = 0;
      struct rusage usage{};
      while (::wait4(pid[i], &st, 0, &usage) < 0 && errno == EINTR) {
      }
#ifdef __APPLE__
      leases[i].complete(static_cast<uint64_t>(usage.ru_maxrss));
#else
      leases[i].complete(static_cast<uint64_t>(usage.ru_maxrss) * 1024);
#endif
      manager.reap_tree(pid[i]);
    }
  }
  out.elapsed_ms = ms();
  for (int i = 0; i < nmethods; ++i) {
    if (out.started[i] && !out.done[i]) {
      out.arm_ms[i] = out.elapsed_ms - started_at[i];
    }
  }
  return out;
}

}  // namespace livehd::lec
