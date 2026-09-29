// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "process_tree.hpp"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif

namespace livehd::cost {
namespace {
constexpr size_t               limit = 4096;
std::optional<Process_reading> read_process(int pid) {
  Process_reading result;
  result.pid = pid;
#if defined(__APPLE__)
  proc_bsdinfo info{};
  if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) {
    return {};
  }
  result.parent         = static_cast<int>(info.pbi_ppid);
  result.birth          = info.pbi_start_tvsec;
  result.birth_fraction = info.pbi_start_tvusec;
  result.stopped        = info.pbi_status == SSTOP;
  result.zombie         = info.pbi_status == SZOMB;
  rusage_info_v4 usage{};
  if (proc_pid_rusage(pid, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) == 0) {
    result.bytes = usage.ri_phys_footprint;
  }
#else
  std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
  std::string   line;
  std::getline(input, line);
  // comm is parenthesized and may itself contain spaces or closing parens.
  const auto close = line.rfind(')');
  if (!input || close == std::string::npos) {
    return {};
  }
  std::istringstream fields(line.substr(close + 1));
  char               state;
  if (!(fields >> state >> result.parent)) {
    return {};
  }
  result.stopped = state == 'T' || state == 't';
  result.zombie  = state == 'Z';
  std::string ignored;
  for (int field = 5; field < 22; ++field) {
    if (!(fields >> ignored)) {
      return {};
    }
  }
  uint64_t   virtual_bytes = 0, pages = 0;
  const auto page = sysconf(_SC_PAGESIZE);
  if (!(fields >> result.birth >> virtual_bytes >> pages) || page <= 0
      || pages > std::numeric_limits<uint64_t>::max() / uint64_t(page)) {
    return {};
  }
  result.bytes = pages * uint64_t(page);
#endif
  return result;
}
std::vector<int> children(int parent, bool& truncated, uint64_t& missing) {
  std::vector<int> result;
#if defined(__APPLE__)
  std::array<int, limit> pids{};
  const auto             bytes = proc_listpids(PROC_PPID_ONLY, static_cast<uint32_t>(parent), pids.data(), sizeof(pids));
  if (bytes < 0) {
    ++missing;
    return result;
  }
  truncated |= bytes >= static_cast<int>(sizeof(pids));
  for (size_t i = 0; i < std::min(size_t(bytes) / sizeof(int), pids.size()); ++i) {
    if (pids[i] > 0) {
      result.push_back(pids[i]);
    }
  }
#else
  std::error_code error;
  const auto      tasks = std::filesystem::path("/proc") / std::to_string(parent) / "task";
  size_t          count = 0;
  for (std::filesystem::directory_iterator it(tasks, error), end; !error && it != end; it.increment(error)) {
    if (++count > limit) {
      truncated = true;
      break;
    }
    std::ifstream input(it->path() / "children");
    if (!input) {
      ++missing;
      continue;
    }
    int pid;
    while (input >> pid) {
      if (result.size() == limit) {
        truncated = true;
        break;
      }
      if (pid > 0) {
        result.push_back(pid);
      }
    }
    if (truncated) {
      break;
    }
  }
  missing += bool(error);
#endif
  return result;
}
}  // namespace

Process_tree_reading read_process_tree(int root) {
  Process_tree_reading             result;
  std::vector<std::pair<int, int>> pending{
      {root, 0}
  };
  std::set<int> seen;
  for (size_t index = 0; index < pending.size(); ++index) {
    const auto [pid, parent] = pending[index];
    if (!seen.insert(pid).second) {
      continue;
    }
    auto process = read_process(pid);
    if (!process || (parent && process->parent != parent)) {
      ++result.missing;
      continue;
    }
    result.missing += process->bytes == 0;
    result.bytes    = process->bytes > std::numeric_limits<uint64_t>::max() - result.bytes ? std::numeric_limits<uint64_t>::max()
                                                                                           : result.bytes + process->bytes;
    result.processes.push_back(*process);
    for (auto child : children(pid, result.truncated, result.missing)) {
      if (pending.size() == limit) {
        result.truncated = true;
        break;
      }
      pending.emplace_back(child, pid);
    }
  }
  return result;
}

bool stop_process_tree(int root) {
  auto leader = read_process(root);
  if (!leader || leader->parent != getpid()) {
    return false;  // Never signal an arbitrary caller-supplied PID.
  }
  std::vector<int> owned{root};
  std::set<int>    seen;
  bool             complete = true, truncated = false;
  uint64_t         missing  = 0;
  const auto       deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (size_t index = 0; index < owned.size(); ++index) {
    const auto pid = owned[index];
    if (!seen.insert(pid).second) {
      continue;
    }
    if (kill(pid, SIGSTOP) != 0 && errno != ESRCH) {
      complete = false;
    }
    bool frozen = false;
    while (std::chrono::steady_clock::now() < deadline) {
      auto process = read_process(pid);
      if (!process || process->zombie) {
        break;
      }
      if (process->stopped) {
        frozen = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!frozen) {
      complete = false;
      continue;
    }
    for (auto child : children(pid, truncated, missing)) {
      auto reading = read_process(child);
      if (!reading || reading->parent != pid) {
        ++missing;
        continue;
      }
      if (owned.size() == limit) {
        truncated = true;
        break;
      }
      owned.push_back(child);
    }
  }
  for (auto it = owned.rbegin(); it != owned.rend(); ++it) {
    if (kill(*it, SIGKILL) != 0 && errno != ESRCH) {
      complete = false;
    }
  }
  // The root PID is still owned/unreaped, so its own process group cannot
  // acquire a reused identity while cleanup is in progress.
  if (getpgid(root) == root) {
    if (kill(-root, SIGKILL) != 0 && errno != ESRCH) {
      complete = false;
    }
  }
  return complete && !truncated && missing == 0;
}
}  // namespace livehd::cost
