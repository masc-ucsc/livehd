// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "job_manager.hpp"

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#ifdef __linux__
#include <sched.h>
#else
#include <libproc.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace livehd {
namespace {
constexpr unsigned classes = static_cast<unsigned>(Job_class::count);
constexpr unsigned slots   = 4096;
struct Ticket {
  Job_manager* manager;
  unsigned     id;
  uint64_t     generation;
};
thread_local std::vector<Ticket> held;
[[maybe_unused]] uint64_t        read_number(const char* file) {
  std::ifstream f(file);
  uint64_t      v = 0;
  f >> v;
  return v;
}
bool process_alive(pid_t pid) {
  if (pid <= 0 || (kill(pid, 0) != 0 && errno == ESRCH)) {
    return false;
  }
#ifdef __linux__
  std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
  std::string   row;
  std::getline(f, row);
  const auto pos = row.rfind(')');
  if (pos != std::string::npos && pos + 2 < row.size() && (row[pos + 2] == 'Z' || row[pos + 2] == 'X')) {
    return false;
  }
#endif
  return true;
}
uint64_t rss_of(pid_t pid) {
#ifdef __linux__
  std::ifstream f("/proc/" + std::to_string(pid) + "/statm");
  uint64_t      vm = 0, rss = 0;
  f >> vm >> rss;
  return rss * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#else
  proc_taskinfo info{};
  return proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof(info)) == sizeof(info) ? info.pti_resident_size : 0;
#endif
}
uint64_t sat_add(uint64_t a, uint64_t b) { return b > UINT64_MAX - a ? UINT64_MAX : a + b; }
struct Lock {
  std::atomic<unsigned>& word;
  explicit Lock(std::atomic<unsigned>& w) : word(w) {
    const auto owner    = static_cast<unsigned>(getpid());
    unsigned   expected = 0;
    while (!word.compare_exchange_weak(expected, owner, std::memory_order_acquire)) {
      // A forked solver may be SIGKILLed while updating admission state.
      // Recover its lock after process death instead of wedging every class.
      if (expected && expected != owner && !process_alive(static_cast<pid_t>(expected))) {
        word.compare_exchange_strong(expected, 0, std::memory_order_release);
      }
      expected = 0;
      std::this_thread::yield();
    }
  }
  ~Lock() { word.store(0, std::memory_order_release); }
};
static_assert(std::atomic<unsigned>::is_always_lock_free);
}  // namespace
static unsigned affinity_cpus() {
  unsigned count = std::max(1u, std::thread::hardware_concurrency());
#ifdef __linux__
  cpu_set_t cpus;
  CPU_ZERO(&cpus);
  if (!sched_getaffinity(0, sizeof(cpus), &cpus)) {
    count = std::max(1, CPU_COUNT(&cpus));
  }
#endif
  return count;
}
unsigned available_cpus() {
  unsigned count = affinity_cpus();
  if (std::getenv("TEST_SRCDIR")) {
    count = std::min(count, 2u);
  }
  return count;
}
Host_resources host_resources() {
  Host_resources h;
  h.cpus = affinity_cpus();
  getloadavg(&h.load, 1);
#ifdef __linux__
  h.ram = static_cast<uint64_t>(sysconf(_SC_PHYS_PAGES)) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
  for (const char* file : {"/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes"}) {
    const auto cap = read_number(file);
    if (cap && cap < h.ram) {
      h.ram = cap;
    }
  }
  std::ifstream mem("/proc/meminfo");
  std::string   line;
  while (std::getline(mem, line)) {
    if (line.starts_with("MemAvailable:")) {
      unsigned long long kb = 0;
      std::sscanf(line.c_str(), "MemAvailable: %llu", &kb);
      h.available = kb * 1024;
      break;
    }
  }
  const auto cap = read_number("/sys/fs/cgroup/memory.max"), used = read_number("/sys/fs/cgroup/memory.current");
  if (cap && used) {
    h.available = std::min(h.available, cap > used ? cap - used : 0);
  }
#else
  size_t n = sizeof(h.ram);
  sysctlbyname("hw.memsize", &h.ram, &n, nullptr, 0);
  vm_statistics64_data_t vm{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
    h.available = static_cast<uint64_t>(vm.free_count + vm.inactive_count) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
  }
#endif
  h.rss = rss_of(getpid());
  return h;
}
struct Job_manager::Shared {
  std::atomic<unsigned>          lock{0};
  unsigned                       ceiling = 1, active = 0;
  uint64_t                       generation = 0, reserved = 0, baseline = 0;
  pid_t                          root = 0;
  std::array<Job_stats, classes> stats{};
  struct Entry {
    std::atomic<bool> active{false}, paused{false};
    Job_class         cls{};
    uint64_t          units = 0, predicted = 0, baseline = 0, generation = 0;
    pid_t             owner = 0, parent = 0;
  };
  std::array<Entry, slots> entries{};
  void                     reap_dead() {
    active   = 0;
    reserved = 0;
    for (auto& stat : stats) {
      stat.active = 0;
    }
    for (auto& e : entries) {
      if ((e.active || e.paused) && e.owner != getpid() && !process_alive(e.owner) && !process_alive(e.parent)) {
        e.active = e.paused = false;
      }
      if (e.active) {
        ++active;
        ++stats[static_cast<unsigned>(e.cls)].active;
        reserved = sat_add(reserved, e.predicted);
      }
    }
  }
  bool fits(const Host_resources& h, uint64_t prediction, bool& memory) const {
    if (!active) {
      return true;  // one job globally, even if its prediction exceeds RAM
    }
    if (active >= ceiling) {
      return false;
    }
    // loadavg already includes our running work. Remove that occupancy before
    // estimating spare capacity, so our own jobs do not shrink the pool twice.
    const double external = std::max(0.0, h.load - active);
    const auto   capacity = std::max(1.0, static_cast<double>(h.cpus) - std::ceil(external));
    if (active >= capacity) {
      return false;
    }
    uint64_t           actual = getpid() == root ? h.rss : rss_of(root);
    std::vector<pid_t> seen;
    for (const auto& e : entries) {
      if ((e.active || e.paused) && e.owner != root && e.owner > 0 && std::find(seen.begin(), seen.end(), e.owner) == seen.end()) {
        seen.push_back(e.owner);
        actual = sat_add(actual, rss_of(e.owner));
      }
    }
    const uint64_t allocated = actual > baseline ? actual - baseline : 0;
    const uint64_t remainder = reserved > allocated ? reserved - allocated : 0;
    const uint64_t projected = sat_add(sat_add(actual, remainder), prediction);
    memory                   = !h.ram || !h.available || projected > h.ram / 2 || sat_add(remainder, prediction) > h.available;
    return !memory;
  }
};
Job_manager::Job_manager(Probe probe) : probe_(std::move(probe)) {
  void* p = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    throw std::bad_alloc();
  }
  shared_                          = new (p) Shared{};
  shared_->root                    = getpid();
  shared_->ceiling                 = std::min(slots, available_cpus());
  shared_->stats[0].bytes_per_unit = 256;
  shared_->stats[1].bytes_per_unit = 2048;
  shared_->stats[2].bytes_per_unit = 4096;
}
Job_manager::~Job_manager() { munmap(shared_, sizeof(Shared)); }
Job_manager& Job_manager::global() {
  static Job_manager manager;
  return manager;
}
void Job_manager::configure(unsigned n) {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  shared_->ceiling = std::min(slots, n ? n : available_cpus());
}
Job_manager::Probe Job_manager::exchange_probe(Probe probe) {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  if (shared_->active) {
    throw std::logic_error("cannot change resource probes while jobs are active");
  }
  return std::exchange(probe_, std::move(probe));
}
unsigned Job_manager::ceiling() const {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  return shared_->ceiling;
}
bool Job_manager::nested() const { return getpid() != shared_->root; }
bool Job_manager::owns_slot() const {
  Lock       lock(shared_->lock);
  const auto self = getpid();
  for (const auto& ticket : held) {
    if (ticket.manager == this) {
      const auto& entry = shared_->entries[ticket.id];
      if (entry.active && entry.generation == ticket.generation && entry.owner == self) {
        return true;
      }
    }
  }
  if (self != shared_->root) {
    // A nested process is single-purpose: every slot it owns is its own, even
    // one handed to it by time slicing (never in this thread's tickets).
    for (const auto& entry : shared_->entries) {
      if (entry.active && entry.owner == self) {
        return true;
      }
    }
  }
  return false;
}
Job_slot::Job_slot(Job_manager* manager, unsigned id, uint64_t generation) : manager_(manager), id_(id), generation_(generation) {
  held.push_back({manager, id, generation});
}
Job_slot::~Job_slot() { complete(); }
Job_slot::Job_slot(Job_slot&& o) noexcept : manager_(std::exchange(o.manager_, nullptr)), id_(o.id_), generation_(o.generation_) {}
Job_slot& Job_slot::operator=(Job_slot&& o) noexcept {
  if (this != &o) {
    complete();
    manager_    = std::exchange(o.manager_, nullptr);
    id_         = o.id_;
    generation_ = o.generation_;
  }
  return *this;
}
void Job_slot::adopt(pid_t child) {
  if (manager_) {
    manager_->adopt(id_, generation_, child);
  }
}
void Job_slot::detach() {
  std::erase_if(held, [&](const Ticket& t) { return t.manager == manager_ && t.id == id_ && t.generation == generation_; });
}
void Job_slot::attach() {
  if (manager_) {
    held.push_back({manager_, id_, generation_});
  }
}
void Job_slot::complete(uint64_t peak) {
  if (auto* manager = std::exchange(manager_, nullptr)) {
    manager->release(id_, generation_, peak);
  }
}
Job_slot Job_manager::try_acquire(Job_class cls, uint64_t units, uint64_t prediction, bool record_queue) {
  const auto host = probe_();
  Lock       lock(shared_->lock);
  shared_->reap_dead();
  auto& stat = shared_->stats[static_cast<unsigned>(cls)];
  if (!prediction) {
    prediction = static_cast<uint64_t>(
        std::min<double>(static_cast<double>(UINT64_MAX / 2), (64ull << 20) + std::max<uint64_t>(1, units) * stat.bytes_per_unit));
  }
  bool memory = false;
  if (!shared_->fits(host, prediction, memory)) {
    if (record_queue) {
      ++stat.queued;
    }
    if (memory) {
      ++stat.memory_waits;
    } else {
      ++stat.cpu_waits;
    }
    return {};
  }
  for (unsigned id = 0; id < slots; ++id) {
    auto& e = shared_->entries[id];
    if (e.active || e.paused) {
      continue;
    }
    if (!shared_->active) {
      shared_->baseline = host.rss;
    }
    e.cls        = cls;
    e.units      = std::max<uint64_t>(1, units);
    e.predicted  = prediction;
    e.baseline   = host.rss;
    e.generation = ++shared_->generation;
    e.owner      = getpid();
    e.parent     = getppid();
    // Publish only after metadata is complete; killed owners cannot leave
    // a half-filled reservation visible to lock recovery.
    e.active.store(true, std::memory_order_release);
    ++shared_->active;
    shared_->reserved = sat_add(shared_->reserved, prediction);
    ++stat.admitted;
    ++stat.active;
    stat.peak = std::max(stat.peak, stat.active);
    return Job_slot(this, id, e.generation);
  }
  return {};
}
Job_slot Job_manager::acquire(Job_class cls, uint64_t units, uint64_t prediction, Clock::time_point deadline) {
  bool queued = false;
  for (;;) {
    if (deadline != Clock::time_point{} && Clock::now() >= deadline) {
      return {};
    }
    auto slot = try_acquire(cls, units, prediction, !queued);
    if (slot) {
      return slot;
    }
    queued = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
void Job_manager::adopt(unsigned id, uint64_t generation, pid_t pid) {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  auto& e = shared_->entries[id];
  if (e.generation == generation) {
    e.parent = pid == getpid() ? getppid() : getpid();
    e.owner  = pid;
  }
}
void Job_manager::release(unsigned id, uint64_t generation, uint64_t peak) {
  // A reaped entry may already be inactive; its thread-local ticket must still
  // disappear so a later manager/slot cannot inherit stale ownership.
  std::erase_if(held, [&](const Ticket& t) { return t.manager == this && t.id == id && t.generation == generation; });
  uint64_t observed_rss = peak;
  if (!observed_rss) {
    try {
      observed_rss = probe_().rss;
    } catch (...) { /* Measurement cannot prevent release. */
    }
  }
  Lock lock(shared_->lock);
  shared_->reap_dead();
  auto& e = shared_->entries[id];
  if (e.generation != generation || (!e.active && !e.paused)) {
    return;
  }
  auto& stat = shared_->stats[static_cast<unsigned>(e.cls)];
  if (e.active) {
    --shared_->active;
    --stat.active;
    shared_->reserved -= e.predicted;
  }
  const auto rss = observed_rss;
  if (rss > e.baseline) {
    const double observed = static_cast<double>(rss - e.baseline) / e.units / std::max(1u, stat.active + 1);
    stat.bytes_per_unit   = 0.75 * stat.bytes_per_unit + 0.25 * observed;
  }
  e.active = e.paused = false;
}
std::array<Job_stats, classes> Job_manager::stats() const {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  return shared_->stats;
}
void Job_manager::report() const {
  const auto  all     = stats();
  const char* names[] = {"formal", "sim_compile", "usyn"};
  for (unsigned i = 0; i < classes; ++i) {
    if (all[i].admitted) {
      std::printf("jobs[stats]: %s %llu admitted, %u peak, %llu queued, %llu memory waits, %llu CPU waits, %.1f bytes/unit\n",
                  names[i],
                  static_cast<unsigned long long>(all[i].admitted),
                  all[i].peak,
                  static_cast<unsigned long long>(all[i].queued),
                  static_cast<unsigned long long>(all[i].memory_waits),
                  static_cast<unsigned long long>(all[i].cpu_waits),
                  all[i].bytes_per_unit);
    }
  }
}
void Job_manager::reap_tree(pid_t pid) {
  Lock lock(shared_->lock);
  shared_->reap_dead();
  std::vector<pid_t> dead{pid};
  for (size_t i = 0; i < dead.size(); ++i) {
    for (auto& e : shared_->entries) {
      if ((e.active || e.paused) && (e.owner == dead[i] || e.parent == dead[i])) {
        if (e.owner != dead[i] && std::find(dead.begin(), dead.end(), e.owner) == dead.end()) {
          dead.push_back(e.owner);
        }
        if (e.active) {
          --shared_->active;
          --shared_->stats[static_cast<unsigned>(e.cls)].active;
          shared_->reserved -= e.predicted;
        }
        e.active = e.paused = false;
      }
    }
  }
}
bool Job_manager::suspend_child(pid_t child) {
  if (child <= 0) {
    return false;
  }
  Lock lock(shared_->lock);
  ::kill(-child, SIGSTOP);  // the child leads its own group (fork_race kill_tree)
  ::kill(child, SIGSTOP);   // and a child whose setpgid failed
  siginfo_t info{};
  // WNOWAIT: observe the stop (or an exit that beat it) without consuming it,
  // so the caller's later wait4 still reaps an exited child normally.
  while (::waitid(P_PID, static_cast<id_t>(child), &info, WSTOPPED | WEXITED | WNOWAIT) < 0) {
    if (errno != EINTR) {
      return false;
    }
  }
  return info.si_code == CLD_STOPPED;
}
Job_manager::Yield::Yield(Job_manager& manager) : manager_(manager) {
  Lock lock(manager_.shared_->lock);
  manager_.shared_->reap_dead();
  const auto self  = getpid();
  auto       pause = [&](unsigned id) {
    auto& e = manager_.shared_->entries[id];
    e.active = false;
    e.paused = true;
    --manager_.shared_->active;
    --manager_.shared_->stats[static_cast<unsigned>(e.cls)].active;
    manager_.shared_->reserved -= e.predicted;
    tickets_.push_back({id, e.generation});
  };
  for (const auto& t : held) {
    if (t.manager == &manager_) {
      auto& e = manager_.shared_->entries[t.id];
      if (e.active && e.generation == t.generation && e.owner == self) {
        pause(t.id);
      }
    }
  }
  if (self != manager_.shared_->root) {
    // Nested: also the slots time slicing handed this process (see nested()).
    for (unsigned id = 0; id < slots; ++id) {
      const auto& e = manager_.shared_->entries[id];
      if (e.active && e.owner == self) {
        pause(id);
      }
    }
  }
}
bool Job_manager::resume(unsigned id, uint64_t generation) {
  const auto h = probe_();
  Lock       lock(shared_->lock);
  shared_->reap_dead();
  auto& e = shared_->entries[id];
  if (e.generation != generation || !e.paused) {
    return true;
  }
  bool memory = false;
  if (!shared_->fits(h, e.predicted, memory)) {
    return false;
  }
  e.paused = false;
  e.active = true;
  ++shared_->active;
  auto& stat = shared_->stats[static_cast<unsigned>(e.cls)];
  ++stat.active;
  stat.peak         = std::max(stat.peak, stat.active);
  shared_->reserved = sat_add(shared_->reserved, e.predicted);
  return true;
}
Job_manager::Yield::~Yield() {
  for (const auto& [id, generation] : tickets_) {
    while (!manager_.resume(id, generation)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
}  // namespace livehd
