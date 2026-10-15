// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <sys/types.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace livehd {
unsigned available_cpus();
struct Host_resources {
  unsigned cpus = 1;
  double   load = 0;
  uint64_t ram = 0, available = 0, rss = 0;
};
Host_resources host_resources();
enum class Job_class : unsigned { formal_arm, sim_compile, usyn_region, count };
struct Job_stats {
  uint64_t admitted = 0, queued = 0, memory_waits = 0, cpu_waits = 0;
  unsigned active = 0, peak = 0;
  double   bytes_per_unit = 0;
};
class Job_manager;
class Job_slot {
public:
  Job_slot() = default;
  ~Job_slot();
  Job_slot(Job_slot&&) noexcept;
  Job_slot& operator=(Job_slot&&) noexcept;
  Job_slot(const Job_slot&)            = delete;
  Job_slot& operator=(const Job_slot&) = delete;
  explicit  operator bool() const { return manager_ != nullptr; }
  void      adopt(pid_t child);
  // Explicit ownership handoff for a reservation queued to another thread.
  void      detach();
  void      attach();
  void      complete(uint64_t peak_bytes = 0);

private:
  friend class Job_manager;
  Job_slot(Job_manager* manager, unsigned id, uint64_t generation);
  Job_manager* manager_    = nullptr;
  unsigned     id_         = 0;
  uint64_t     generation_ = 0;
};
class Job_manager {
public:
  using Clock = std::chrono::steady_clock;
  using Probe = std::function<Host_resources()>;
  explicit Job_manager(Probe probe = host_resources);
  ~Job_manager();
  Job_manager(const Job_manager&)                   = delete;
  Job_manager&        operator=(const Job_manager&) = delete;
  static Job_manager& global();
  void                configure(unsigned ceiling);
  Probe               exchange_probe(Probe probe);  // change injected measurements only while idle
  unsigned            ceiling() const;
  // NESTED work: a process forked below the one that created this manager
  // (a racer, an isolated worker, a helper a racer forked) runs on behalf of a
  // parent that is blocked until it finishes, so it already runs on that
  // parent's admitted capacity. Such a process finds its slots by owner (a
  // time-sliced racer may be resumed on a lease its thread never acquired),
  // and its own first unit of work never waits for admission: the slots it
  // would wait for can be held by the very callers waiting on it.
  bool                nested() const;
  bool                owns_slot() const;
  Job_slot            try_acquire(Job_class cls, uint64_t units = 1, uint64_t prediction = 0, bool record_queue = true);
  Job_slot            acquire(Job_class cls, uint64_t units = 1, uint64_t prediction = 0, Clock::time_point deadline = {});
  std::array<Job_stats, static_cast<unsigned>(Job_class::count)> stats() const;
  void                                                           report() const;
  // Used by a parent after killing/reaping a worker, including its nested arms.
  void                                                           reap_tree(pid_t root);
  // Time slicing (fork_race): SIGSTOP the process group led by `child` (a
  // direct child of the caller) and wait until the leader has stopped. The stop
  // is sent and observed while holding the admission lock, so a stopped child
  // can never be holding that lock (a stopped holder would wedge every process
  // sharing this manager). Returns false when the child exited instead; it is
  // left unreaped for the caller's normal wait.
  bool                                                           suspend_child(pid_t child);
  class Yield {
  public:
    explicit Yield(Job_manager& manager);
    ~Yield();
    Yield(const Yield&)            = delete;
    Yield& operator=(const Yield&) = delete;

  private:
    Job_manager&                               manager_;
    std::vector<std::pair<unsigned, uint64_t>> tickets_;
  };

private:
  friend class Job_slot;
  struct Shared;
  Shared* shared_;
  Probe   probe_;
  void    release(unsigned id, uint64_t generation, uint64_t peak);
  void    adopt(unsigned id, uint64_t generation, pid_t pid);
  bool    resume(unsigned id, uint64_t generation);
};
}  // namespace livehd
