// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// External, serial measurement harness. All synthesis still runs through lhd.
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "host_mem.hpp"
#include "process_tree.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

extern char** environ;
namespace {
volatile sig_atomic_t interrupted = 0;
void                  on_signal(int signal) { interrupted = signal; }
using Clock = std::chrono::steady_clock;
struct Child {
  pid_t pid = -1;
  ~Child() {
    if (pid > 0) {
      try {
        if (!livehd::cost::stop_process_tree(pid)) {
          kill(pid, SIGKILL);
        }
      } catch (...) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
      }
      int status;
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
};
uint64_t number(std::string_view text) {
  uint64_t value;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error("invalid numeric measurement limit");
  }
  return value;
}
}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  fs::path failure_archive;
  try {
    fs::path archive;
    uint64_t seconds = 120, memory_mb = 0;
    int      command = 1;
    for (; command < argc && std::string_view(argv[command]) != "--"; command += 2) {
      if (command + 1 == argc) {
        throw std::runtime_error("missing measurement option value");
      }
      const std::string_view option(argv[command]);
      if (option == "--archive") {
        archive = argv[command + 1];
      } else if (option == "--seconds") {
        seconds = number(argv[command + 1]);
      } else if (option == "--memory-mb") {
        memory_mb = number(argv[command + 1]);
      } else {
        throw std::runtime_error("unknown measurement option");
      }
    }
    ++command;
    if (archive.empty() || command >= argc || seconds == 0 || seconds > 86400 || memory_mb > 1048576) {
      throw std::runtime_error(
          "usage: measure_synth --archive NEW_DIR [--seconds 1..86400] [--memory-mb MB] -- EXECUTABLE ARGS...");
    }
    const auto budget
        = memory_mb ? livehd::cost::budget_bytes(static_cast<int>(memory_mb)) : livehd::cost::configured_budget_bytes();
    if (!budget) {
      throw std::runtime_error("memory budget unavailable; provide --memory-mb");
    }
    archive = fs::absolute(archive);
    fs::create_directories(archive.parent_path());
    if (!fs::create_directory(archive)) {
      throw std::runtime_error("measurement archive must be fresh");
    }
    failure_archive                       = archive;
    const auto                 executable = fs::absolute(argv[command]).string();
    const auto                 log        = (archive / "command.log").string();
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions)) {
      throw std::runtime_error("cannot initialize command redirection");
    }
    const auto redir  = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    const auto errdup = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    const auto input  = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes)) {
      posix_spawn_file_actions_destroy(&actions);
      throw std::runtime_error("cannot initialize command spawn");
    }
    const auto flags = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    const auto group = posix_spawnattr_setpgroup(&attributes, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    Child      child;
    const auto started = Clock::now();
    const auto error   = (redir || errdup || input || flags || group)
                             ? EINVAL
                             : posix_spawn(&child.pid, executable.c_str(), &actions, &attributes, argv + command, environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
      child.pid = -1;
      throw std::runtime_error("cannot spawn measured command");
    }
    std::ofstream samples(archive / "samples.jsonl");
    if (!samples) {
      throw std::runtime_error("cannot open measurement samples");
    }
    uint64_t                                   scans = 0, missing = 0, omitted = 0, peak = 0, largest_tree = 0;
    std::vector<livehd::cost::Process_reading> peak_processes;
    std::string                                reason           = "completed";
    bool                                       cleanup_complete = true, cleanup_attempted = false;
    while (true) {
      const auto reading = livehd::cost::read_process_tree(child.pid);
      ++scans;
      missing      += reading.missing;
      largest_tree  = std::max(largest_tree, uint64_t(reading.processes.size()));
      if (reading.bytes > peak) {
        peak           = reading.bytes;
        peak_processes = reading.processes;
      }
      const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
      if (scans <= 65536) {
        samples << std::format("{{\"ms\":{},\"bytes\":{},\"processes\":{},\"missing\":{},\"truncated\":{}}}\n",
                               elapsed,
                               reading.bytes,
                               reading.processes.size(),
                               reading.missing,
                               reading.truncated);
      } else {
        ++omitted;
      }
      siginfo_t status{};
      int       waited;
      do {
        waited = waitid(P_PID, static_cast<id_t>(child.pid), &status, WEXITED | WNOHANG | WNOWAIT);
      } while (waited < 0 && errno == EINTR);
      if (waited < 0) {
        if (errno == ECHILD) {
          child.pid = -1;
        }
        throw std::runtime_error("cannot observe measured command exit");
      }
      if (interrupted) {
        reason = "interrupted";
      } else if (reading.truncated) {
        reason = "process_scan_limit";
      } else if (reading.bytes > budget) {
        reason = "memory_limit";
      } else if (elapsed >= double(seconds) * 1000) {
        reason = "time_limit";
      }
      if (status.si_pid == child.pid) {
        break;
      }
      if (reason != "completed") {
        cleanup_attempted = true;
        cleanup_complete  = livehd::cost::stop_process_tree(child.pid);
        if (!cleanup_complete) {
          kill(child.pid, SIGKILL);
        }
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    int   status;
    pid_t waited;
    do {
      waited = waitpid(child.pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child.pid) {
      if (errno == ECHILD) {
        child.pid = -1;
      }
      throw std::runtime_error("cannot reap measured command");
    }
    child.pid          = -1;
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto code    = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (reason == "completed" && code != 0) {
      reason = "command_failed";
    }
    samples.close();
    if (!samples) {
      throw std::runtime_error("cannot write measurement samples");
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
    out.StartObject();
    out.Key("schema_version");
    out.Uint(1);
    out.Key("kind");
    out.String("synthesis_process_measurement");
    out.Key("scope");
    out.String("spawn_through_root_reap");
    out.Key("cwd");
    out.String(fs::current_path().string().c_str());
    out.Key("executable");
    out.String(executable.c_str());
    out.Key("argv");
    out.StartArray();
    for (int i = command; i < argc; ++i) {
      out.String(argv[i]);
    }
    out.EndArray();
    out.Key("reason");
    out.String(reason.c_str());
    out.Key("exit_code");
    out.Int(code);
    out.Key("wall_ms");
    out.Double(elapsed);
    out.Key("memory_metric");
#if defined(__APPLE__)
    out.String("sum_physical_footprint");
#else
    out.String("sum_rss");
#endif
    out.Key("discovery_scope");
    out.String("current_parentage_descendants");
    out.Key("atomic_snapshot");
    out.Bool(false);
    out.Key("sampled_peak_bytes");
    if (peak) {
      out.Uint64(peak);
    } else {
      out.Null();
    }
    out.Key("memory_budget_bytes");
    out.Uint64(budget);
    out.Key("time_budget_seconds");
    out.Uint64(seconds);
    out.Key("sampling_pause_ms");
    out.Uint(25);
    out.Key("samples");
    out.Uint64(scans);
    out.Key("missing_readings");
    out.Uint64(missing);
    out.Key("omitted_sample_rows");
    out.Uint64(omitted);
    out.Key("maximum_processes");
    out.Uint64(largest_tree);
    out.Key("guard_cleanup_scope");
    out.String("current_parentage_descendants_and_owned_root_group");
    out.Key("guard_cleanup_complete");
    if (!cleanup_attempted) {
      out.Null();
    } else {
      out.Bool(cleanup_complete);
    }
    out.Key("peak_processes");
    out.StartArray();
    for (const auto& process : peak_processes) {
      out.StartObject();
      out.Key("pid");
      out.Int(process.pid);
      out.Key("parent");
      out.Int(process.parent);
      out.Key("birth");
      out.Uint64(process.birth);
      out.Key("birth_fraction");
      out.Uint64(process.birth_fraction);
      out.Key("bytes");
      out.Uint64(process.bytes);
      out.EndObject();
    }
    out.EndArray();
    out.EndObject();
    std::ofstream report(archive / "measurement.json");
    report << buffer.GetString() << '\n';
    report.close();
    if (!report) {
      throw std::runtime_error("cannot write command measurement");
    }
    std::println("{}", (archive / "measurement.json").string());
    return reason == "completed" && code == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    if (!failure_archive.empty()) {
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
      out.StartObject();
      out.Key("schema_version");
      out.Uint(1);
      out.Key("kind");
      out.String("synthesis_measurement_failure");
      out.Key("error");
      out.String(error.what());
      out.EndObject();
      std::ofstream(failure_archive / "measurement_failure.json") << buffer.GetString() << '\n';
    }
    std::fprintf(stderr, "synthesis measurement failed: %s\n", error.what());
    return 2;
  }
}
