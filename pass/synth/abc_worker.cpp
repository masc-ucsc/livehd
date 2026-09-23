// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_worker.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#endif

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

#include "abc_tmap.hpp"
#include "mapping_wire.hpp"

extern char** environ;
namespace livehd::synth {
namespace {
constexpr size_t max_bytes = 64 * 1024 * 1024;
constexpr auto   protocol  = "livehd-tmap-worker-v1";
using Clock                = std::chrono::steady_clock;
uint64_t child_footprint(pid_t pid) {
#if defined(__APPLE__)
  rusage_info_v4 info{};
  if (proc_pid_rusage(pid, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&info)) == 0) {
    return info.ri_phys_footprint;
  }
#else
  std::ifstream input("/proc/" + std::to_string(pid) + "/statm");
  uint64_t      virtual_pages = 0, resident_pages = 0;
  const auto    page = sysconf(_SC_PAGESIZE);
  if (input >> virtual_pages >> resident_pages && page > 0
      && resident_pages <= std::numeric_limits<uint64_t>::max() / uint64_t(page)) {
    return resident_pages * uint64_t(page);
  }
#endif
  return 0;
}
struct Scratch {
  std::filesystem::path path;
  Scratch() {
    auto pattern = (std::filesystem::temp_directory_path() / "livehd-tmap-XXXXXX").string();
    if (!mkdtemp(pattern.data())) {
      throw std::runtime_error("cannot create synthesis worker scratch");
    }
    path = pattern;
  }
  ~Scratch() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
struct Child {
  pid_t pid = -1;
  ~Child() { stop(); }
  void stop() {
    if (pid < 0) {
      return;
    }
    // posix_spawn starts exactly this child in its own process group. Keep
    // ownership until waitpid reaps it; never signal by name or reused PID.
    kill(-pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    pid = -1;
  }
};
void write_record(const std::filesystem::path& path, std::string_view text) {
  if (text.size() > max_bytes) {
    throw std::runtime_error("oversized synthesis worker record");
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), text.size());
  output.close();
  if (!output) {
    throw std::runtime_error("cannot write synthesis worker record");
  }
}
std::string read_record(const std::filesystem::path& path) {
  std::error_code error;
  const auto      size = std::filesystem::file_size(path, error);
  if (error || size > max_bytes) {
    throw std::runtime_error("missing or oversized synthesis worker record");
  }
  std::string   text(size, '\0');
  std::ifstream input(path, std::ios::binary);
  input.read(text.data(), text.size());
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("incomplete synthesis worker record");
  }
  return text;
}
Mapped_fragment response(std::string_view text) {
  wire::Reader r{text};
  if (r.text() != protocol) {
    throw std::runtime_error("synthesis worker protocol mismatch");
  }
  const auto status = static_cast<Map_status>(r.count(static_cast<uint64_t>(Map_status::invalid)));
  auto       body   = r.text();
  if (!r.data.empty()) {
    throw std::runtime_error("trailing synthesis worker response");
  }
  return status == Map_status::mapped ? wire::decode_fragment(body) : Mapped_fragment{.status = status, .reason = std::move(body)};
}
}  // namespace

std::string read_worker_record(const char* path) { return read_record(path); }
void        write_worker_record(const char* path, std::string_view data) { write_record(path, data); }

Worker_exchange exchange_worker(const std::string& executable, const char* mode, std::string_view payload,
                                const Mapping_request& request) {
  const auto started    = Clock::now();
  const auto remaining  = request.remaining_ms ? request.remaining_ms() : 0;
  uint64_t   worker_pid = 0;
  const auto exhausted  = [&] { return Worker_exchange{{}, "worker resource budget exhausted", true, false, worker_pid}; };
  if (!std::isfinite(remaining) || remaining <= 0 || (request.admission && !request.admission())) {
    return exhausted();
  }
  const auto admitted = [&] {
    const bool allowed = !request.admission || request.admission();
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count() < remaining && allowed;
  };
  try {
    if (executable.empty()) {
      throw std::runtime_error("synthesis worker executable unavailable");
    }
    Scratch    scratch;
    const auto input  = (scratch.path / "request").string();
    const auto output = (scratch.path / "response").string();
    write_record(input, payload);
    if (!admitted()) {
      return exhausted();
    }
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes)) {
      throw std::runtime_error("cannot initialize synthesis worker spawn");
    }
    const auto  flags  = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    const auto  group  = posix_spawnattr_setpgroup(&attributes, 0);
    const char* args[] = {executable.c_str(), mode, input.c_str(), output.c_str(), nullptr};
    Child       child;
    const auto  error = (flags || group)
                            ? EINVAL
                            : posix_spawn(&child.pid, executable.c_str(), nullptr, &attributes, const_cast<char**>(args), environ);
    posix_spawnattr_destroy(&attributes);
    if (error) {
      child.pid = -1;
      throw std::runtime_error("cannot start synthesis worker");
    }
    worker_pid = static_cast<uint64_t>(child.pid);
    int status = 0;
    while (true) {
      if (request.worker_admission && !request.worker_admission(child_footprint(child.pid))) {
        return exhausted();
      }
      if (!admitted()) {
        return exhausted();
      }  // Child destructor kills and reaps before scratch removal.
      const auto done = waitpid(child.pid, &status, WNOHANG);
      if (done == child.pid) {
        child.pid = -1;
        break;
      }
      if (done < 0 && errno != EINTR) {
        if (errno == ECHILD) {
          child.pid = -1;
        }
        throw std::runtime_error("cannot wait for synthesis worker");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
      throw std::runtime_error("synthesis worker exited without a result");
    }
    auto result = read_record(output);
    if (!admitted()) {
      return exhausted();
    }
    return {std::move(result), {}, false, false, worker_pid};
  } catch (const std::exception& error) {
    return {{}, error.what(), false, true, worker_pid};
  }
}

Worker_result map_in_worker(const std::string& executable, const Mapping_request& request) {
  uint64_t worker_pid = 0;
  try {
    wire::Writer w;
    w.text(protocol);
    w.text(wire::encode_request(request));
    auto exchanged = exchange_worker(executable, "--internal-synth-tmap", w.data, request);
    worker_pid     = exchanged.worker_pid;
    if (exchanged.failed || exchanged.stopped) {
      return {
          {.status = Map_status::exhausted, .reason = exchanged.reason},
          exchanged.stopped,
          exchanged.failed,
          worker_pid
      };
    }
    return {response(exchanged.data), false, false, worker_pid};
  } catch (const std::exception& error) {
    return {
        {.status = Map_status::exhausted, .reason = error.what()},
        false,
        true,
        worker_pid
    };
  }
}

int abc_tmap_worker_main(const char* request_file, const char* response_file) {
  try {
    const auto   text = read_record(request_file);
    wire::Reader reader{text};
    if (reader.text() != protocol) {
      return 2;
    }
    auto request = wire::decode_request(reader.text());
    if (!reader.data.empty()) {
      return 2;
    }
    Abc_tmap     backend;  // No deadline callback in the child: the parent enforces it externally.
    const auto   result = backend.map(request);
    wire::Writer writer;
    writer.text(protocol);
    writer.number(static_cast<uint64_t>(result.status));
    writer.text(result.status == Map_status::mapped ? wire::encode_fragment(result) : result.reason);
    write_record(response_file, writer.data);
    return 0;
  } catch (const std::exception&) {
    return 2;
  }
}
}  // namespace livehd::synth
