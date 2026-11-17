// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_worker.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

#include "core/hash_util.hpp"
#include "core/job_manager.hpp"
#include "core/process_group.hpp"
#include "gui_ipc.hpp"
#include "gui_wire.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

extern char** environ;
namespace livehd::placement::gui {
namespace fs = std::filesystem;
namespace {
// A scorer may fail partway through placement or exit zero without all OpenDB checks.
// Intermediate DPL failures are diagnostic: only the independently checked final result counts.
std::string routed_validation_error(const rapidjson::Value& receipt, int process_status) {
  if (!WIFEXITED(process_status) || WEXITSTATUS(process_status) != 0) {
    return "validation process failed after reporting a routed row";
  }
  if (!receipt.HasMember("openroad_exit") || !receipt["openroad_exit"].IsInt() || receipt["openroad_exit"].GetInt() != 0) {
    return "validation receipt does not confirm a successful OpenROAD exit";
  }
  if (!receipt.HasMember("inputs") || !receipt["inputs"].IsObject()) {
    return "validation receipt is missing the downstream recipe";
  }
  if (!receipt.HasMember("placement_checks") || !receipt["placement_checks"].IsObject()) {
    return "validation receipt is missing placement checks";
  }
  const auto& checks = receipt["placement_checks"];
  for (auto name : {"applied_macro_poses", "routed_macro_poses", "final_legality"}) {
    if (!checks.HasMember(name) || !checks[name].IsString() || std::string_view(checks[name].GetString()) != "passed") {
      return std::format("validation receipt did not pass {}", name);
    }
  }
  const auto& row = receipt["row"];
  if (!row.HasMember("drc") || !row["drc"].IsUint() || row["drc"].GetUint() >= 1000) {
    return "routed row lacks a final DRC count below the benchmark cutoff";
  }
  const auto* timing = receipt.HasMember("setup_timing") && receipt["setup_timing"].IsObject() ? &receipt["setup_timing"] : nullptr;
  const bool measured = timing && timing->HasMember("status") && (*timing)["status"].IsString()
                        && std::string_view((*timing)["status"].GetString()) == "measured";
  for (auto name : {"wns", "tns"}) {
    if (!measured) {
      if (row.HasMember(name) && !row[name].IsNull()) {
        return "validation row has timing without a final setup measurement";
      }
    } else if (!row.HasMember(name) || !row[name].IsNumber() || !timing->HasMember(name) || !(*timing)[name].IsNumber()
               || !std::isfinite(row[name].GetDouble()) || row[name].GetDouble() > 0 || row[name].GetDouble() != (*timing)[name].GetDouble()) {
      return std::format("validation row disagrees with its final setup measurement for {}", name);
    }
  }
  return {};
}
std::string session_build(const std::string& json) {
  rapidjson::Document d;
  d.Parse(json.c_str());
  if (!d.IsObject()) {
    return {};
  }
  // Config holds the session object; the worker's hello wraps it in a protocol response.
  const auto& session = d.HasMember("session") ? d["session"] : static_cast<const rapidjson::Value&>(d);
  return session.IsObject() && session.HasMember("build") && session["build"].IsString() ? session["build"].GetString() : "";
}
std::chrono::steady_clock::time_point deadline_for(const Config& cfg, const std::string& tier,
                                                   std::chrono::steady_clock::time_point start) {
  return cfg.worker_deadline == decltype(start){} ? start + std::chrono::seconds(cfg.timeout_for(tier)) : cfg.worker_deadline;
}
std::string json_string(const std::string& value) {
  rapidjson::StringBuffer                    buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.String(value.data(), value.size());
  return buf.GetString();
}
std::string serialize(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}
struct Socket {
  int fd;
  ~Socket() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
};
fs::path scratch_pattern(const fs::path& parent, pid_t controller) {
  return parent / std::format("lhd-x-{}-XXXXXX", controller);
}
constexpr auto worker_pattern = "x-XXXXXX";
fs::path worker_socket(const fs::path& directory) { return directory / "agent.sock"; }
}  // namespace

// T4 can use an external validation command. Only compare complete, typed
// endpoint evidence; a legacy receipt can still supply physical deltas.
const rapidjson::Value* routed_endpoints(const rapidjson::Value& receipt) {
  if (!receipt.HasMember("setup_timing") || !receipt["setup_timing"].IsObject()) {
    return nullptr;
  }
  const auto& timing = receipt["setup_timing"];
  if (!timing.HasMember("status") || timing["status"] != "measured" || !timing.HasMember("endpoints")
      || !timing["endpoints"].IsObject()) {
    return nullptr;
  }
  const auto& e = timing["endpoints"];
  if (!e.HasMember("status") || e["status"] != "measured" || !e.HasMember("engine") || e["engine"] != "OpenSTA"
      || !e.HasMember("analysis") || e["analysis"] != "final_setup" || !e.HasMember("clock_periods_ns")
      || !e["clock_periods_ns"].IsObject()) {
    return nullptr;
  }
  for (const auto& clock : e["clock_periods_ns"].GetObject()) {
    if (!clock.value.IsNumber() || !(clock.value.GetDouble() > 0)) {
      return nullptr;
    }
  }
  auto group = [](const rapidjson::Value& g) {
    return g.IsObject() && g.HasMember("endpoints") && g["endpoints"].IsUint() && g["endpoints"].GetUint() > 0
           && g.HasMember("failing") && g["failing"].IsUint() && g["failing"].GetUint() <= g["endpoints"].GetUint()
           && g.HasMember("population") && g["population"].IsString() && g["population"].GetStringLength() == 64
           && g.HasMember("clock_population") && g["clock_population"].IsString() && g["clock_population"].GetStringLength() == 64
           && g.HasMember("wns_ps") && g["wns_ps"].IsNumber() && g.HasMember("tns_ps") && g["tns_ps"].IsNumber();
  };
  if (!e.HasMember("total") || !group(e["total"])) {
    return nullptr;
  }
  for (const auto* key : {"path_families", "capture_clocks"}) {
    if (!e.HasMember(key) || !e[key].IsObject()) {
      return nullptr;
    }
    for (const auto& item : e[key].GetObject()) {
      if (!group(item.value)) {
        return nullptr;
      }
    }
  }
  return &e;
}
std::string default_agent_socket() {
  auto parent = fs::temp_directory_path();
  // The controller's PID is not known yet. Leave room for its private worker
  // socket too: a valid main socket alone does not guarantee exploration works.
  const auto longest = worker_socket(scratch_pattern(parent, std::numeric_limits<pid_t>::max()) / worker_pattern);
  if (longest.native().size() >= sizeof(sockaddr_un::sun_path)) {
    parent = "/tmp";
  }
  return (parent / std::format("lhd-gui-{}.sock", getpid())).string();
}
void clean_exploration_roots(const fs::path& parent) {
  std::error_code ec;
  for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
    const auto                 path   = it->path();
    const auto                 name   = path.filename().string();
    constexpr std::string_view prefix = "lhd-x-";
    if (!name.starts_with(prefix)) {
      continue;
    }
    const auto dash = name.find('-', prefix.size());
    if (dash == name.npos || name.size() - dash != 7) {
      continue;
    }
    int owner                 = 0;
    const auto [after, error] = std::from_chars(name.data() + prefix.size(), name.data() + dash, owner);
    if (error != std::errc{} || after != name.data() + dash || owner <= 1 || kill(owner, 0) == 0 || errno != ESRCH) {
      continue;
    }
    Socket      lease{open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    struct stat stat{};
    if (lease.fd < 0 || fstat(lease.fd, &stat) != 0 || stat.st_uid != geteuid() || (stat.st_mode & 0777) != 0700
        || flock(lease.fd, LOCK_EX | LOCK_NB) != 0) {
      continue;
    }
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
}
std::string Session::scratch_root_locked() {
  if (!region_root_.empty()) {
    return region_root_;
  }
  auto pattern = scratch_pattern(fs::path(cfg_.agent_socket).parent_path(), getpid()).string();
  if (!mkdtemp(pattern.data())) {
    throw std::system_error(errno, std::generic_category(), "create private placement directory");
  }
  scratch_lease_ = open(pattern.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (scratch_lease_ < 0 || flock(scratch_lease_, LOCK_EX | LOCK_NB) != 0) {
    const int error = errno;
    if (scratch_lease_ >= 0) {
      close(scratch_lease_);
    }
    scratch_lease_ = -1;
    rmdir(pattern.c_str());
    throw std::system_error(error, std::generic_category(), "lock private placement directory");
  }
  region_root_ = pattern;
  return region_root_;
}
static fs::path private_worker_dir(const fs::path& root) {
  std::string pattern = (root / worker_pattern).string();
  if (!::mkdtemp(pattern.data())) {
    throw std::system_error(errno, std::generic_category(), "create exploration directory");
  }
  return pattern;
}

Snapshot_owner::Snapshot_owner(const Config& cfg, const std::string& checkpoint, const std::string& document,
                               const std::string& native_endpoint) {
  Socket local{-1};
  native_ = !native_endpoint.empty();
  if (native_) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (native_endpoint.size() >= sizeof(addr.sun_path)) {
      throw std::runtime_error("native GP endpoint is too long");
    }
    local.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (local.fd < 0) {
      throw std::system_error(errno, std::generic_category(), "native GP checkpoint channel");
    }
    fcntl(local.fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(local.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    std::memcpy(addr.sun_path, native_endpoint.c_str(), native_endpoint.size() + 1);
    if (connect(local.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      throw std::system_error(errno, std::generic_category(), "attach native GP checkpoint");
    }
  } else {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
      throw std::system_error(errno, std::generic_category(), "snapshot channel");
    }
    local.fd = pair[0];
    Socket remote{pair[1]};
#ifdef SO_NOSIGPIPE
    int on = 1;
    for (int fd : pair) {
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    }
#endif
    fcntl(local.fd, F_SETFD, FD_CLOEXEC);
    fcntl(remote.fd, F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // dup2 clears close-on-exec on the child's single private channel.
    const int fd = std::max(local.fd, remote.fd) + 1;
    posix_spawn_file_actions_adddup2(&actions, remote.fd, fd);
    auto      descriptor = std::to_string(fd), parent = std::to_string(getpid());
    char*     args[]{const_cast<char*>(cfg.worker_args.at(0).c_str()),
                     const_cast<char*>("--placement-snapshot-owner"),
                     descriptor.data(),
                     parent.data(),
                     nullptr};
    const int rc = posix_spawnp(&pid_, args[0], &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) {
      throw std::system_error(rc, std::generic_category(), "spawn snapshot state owner");
    }
    close(remote.fd);
    remote.fd = -1;
  }
  try {
    // The owner can grow while this thread is blocked sending the checkpoint or awaiting timer reconstruction.
    // Shutting down the private channel wakes either operation so the ordinary error path reaps the owner.
    std::atomic<bool> exceeded = false;
    std::jthread      budget_watch;
    if (cfg.session_memory_bytes) {
      budget_watch = std::jthread([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
          if (process_tree_rss(cfg.session_root_pid) > cfg.session_memory_bytes) {
            exceeded.store(true);
            shutdown(local.fd, SHUT_RDWR);
            return;
          }
          for (unsigned tick = 0; tick < 10 && !stop.stop_requested(); ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
          }
        }
      });
    }
    std::pair<std::string, std::string> reply;
    try {
      send_message(local.fd, "checkpoint", checkpoint);
      send_message(local.fd, "document", document);
      reply = receive_message(local.fd);
    } catch (...) {
      if (exceeded.load()) {
        throw std::runtime_error("session memory budget exhausted during snapshot initialization");
      }
      throw;
    }
    if (budget_watch.joinable()) {
      budget_watch.request_stop();
      budget_watch.join();
      if (exceeded.load() || process_tree_rss(cfg.session_root_pid) > cfg.session_memory_bytes) {
        throw std::runtime_error("session memory budget exhausted during snapshot initialization");
      }
    }
    if (reply.first != "ready") {
      throw std::runtime_error("snapshot initialization: " + reply.second);
    }
    socket_  = local.fd;
    local.fd = -1;
  } catch (...) {
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
    throw;
  }
}
Snapshot_owner::~Snapshot_owner() {
  if (socket_ >= 0) {
    close(socket_);
  }
  if (pid_ > 0) {
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
  }
}
pid_t Snapshot_owner::launch(const fs::path& dir, const std::string& id, const std::string& patch, const std::string& score,
                             const std::string& relax) {
  std::lock_guard lock(mutex_);
  send_message(socket_, "fork", wire::pack(dir.string(), id, patch, score, relax));
  auto [status, reason] = receive_message(socket_);
  if (status != "started") {
    throw std::runtime_error(reason);
  }
  return wire::read<pid_t>(reason);
}
bool Snapshot_owner::finished(pid_t pid) {
  std::lock_guard lock(mutex_);
  send_message(socket_, "poll", wire::pack(pid));
  auto [status, data] = receive_message(socket_);
  if (status != "poll") {
    throw std::runtime_error(data);
  }
  return wire::read<bool>(data);
}
void Snapshot_owner::release(pid_t pid, double& cpu_s, uint64_t& rss) {
  std::lock_guard lock(mutex_);
  send_message(socket_, "reap", wire::pack(pid));
  auto [status, reason] = receive_message(socket_);
  if (status != "reaped") {
    throw std::runtime_error(reason);
  }
  wire::unpack(reason, cpu_s, rss);
}

Replay_worker::Replay_worker(const Config& cfg, const fs::path& root, std::string id, const std::string& identity,
                             std::string patch, Job_slot slot, std::shared_ptr<Snapshot_owner> owner, std::string relax)
    : id_(std::move(id))
    , tier_(relax.empty() ? "T1" : "T2")
    , replayed_baseline_(identity)
    , owner_(std::move(owner))
    , snapshot_(true)
    , slot_(std::move(slot))
    , start_(std::chrono::steady_clock::now()) {
  dir_ = private_worker_dir(root);
  try {
    std::string terms;
    for (const auto& [loss, params] : cfg.score) {
      terms += std::format(R"({}{{"loss":{},"weight":{},"half":{}}})",
                           terms.empty() ? "" : ",",
                           json_string(loss),
                           params.first,
                           params.second);
    }
    const auto score
        = std::format(R"({{"cmd":"score_config","estimator":{},"components":[{}]}})", json_string(cfg.score_estimator), terms);
    pid_ = owner_->launch(dir_, id_, patch, score, relax);
    slot_.adopt(pid_);
    slot_.detach();
    startup_s_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    status_   = "running";
    monitor_  = std::jthread([this](std::stop_token stop) {
      while (!stop.stop_requested()) {
        {
          std::lock_guard lock(mutex_);
          if (pid_ <= 0) {
            return;
          }
          try {
            if (owner_->finished(pid_)) {
              std::ifstream f(dir_ / "result.bin", std::ios::binary);
              if (!f) {
                std::ifstream counts(dir_ / "work.json");
                rapidjson::Document work;
                work.Parse(std::string(std::istreambuf_iterator<char>(counts), {}).c_str());
                if (work.IsObject()) {
                  work_ = serialize(work);
                }
                std::ifstream error(dir_ / "error.txt");
                std::string   reason{std::istreambuf_iterator<char>(error), {}};
                throw std::runtime_error(reason.empty() ? "snapshot child exited without a result" : reason);
              }
              std::string         bytes{std::istreambuf_iterator<char>(f), {}};
              auto                doc = Session::read_result_snapshot(bytes);
              rapidjson::Document d;
              d.Parse<rapidjson::kParseFullPrecisionFlag>(doc->state_json().c_str());
              work_ = serialize(d["work"]);
              const auto number
                  = [&](const char* key) { return d[key].IsNumber() ? std::format("{}", d[key].GetDouble()) : "null"; };
              result_ = std::format(
                  R"({{"timing_available":{},"wns_ps":{},"tns_ps":{},"geometry_status":{},"identity":{},"topology":{},"masters":{},"summary":{},"signature":{}}})",
                  d["has_timing"].GetBool(),
                  number("wns"),
                  number("tns"),
                  json_string(d["geometry_status"].GetString()),
                  json_string(d["placement_identity"].GetString()),
                  json_string(d["topology"].GetString()),
                  json_string(d["masters"].GetString()),
                  doc->agent_call(R"({"cmd":"summary"})"),
                  doc->agent_call(R"({"cmd":"signature"})"));
              status_ = "done";
            }
          } catch (const std::exception& ex) {
            status_ = "failed";
            reason_ = ex.what();
          }
          if (status_ != "running") {
            wait_worker(nullptr, 0);
            pid_ = -1;
            end_ = std::chrono::steady_clock::now();
            slot_.complete(std::max(peak_rss_bytes_, peak_tree_rss_bytes_));
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    });
    watch_resources(cfg);
  } catch (...) {
    cancel();
    std::error_code ec;
    fs::remove_all(dir_, ec);
    throw;
  }
}

Replay_worker::Replay_worker(const Config& cfg, const fs::path& root, std::string id, const std::shared_ptr<Session>& proposal,
                             double placement_seconds, Job_slot slot)
    : id_(std::move(id)), tier_("T4"), slot_(std::move(slot)), start_(std::chrono::steady_clock::now()) {
  dir_ = private_worker_dir(root);
  try {
    const auto coords   = proposal->agent_call(R"({"cmd":"export","what":"macros","format":"placereasoner"})");
    const auto sig      = proposal->agent_call(R"({"cmd":"signature"})");
    rapidjson::Document summary;
    summary.Parse(proposal->agent_call(R"({"cmd":"summary"})").c_str());
    const std::string geometry = summary["geometry_status"].GetString();
    const auto snapshot = proposal->result_snapshot();
    if (!std::ofstream(dir_ / "result.bin", std::ios::binary).write(snapshot.data(), snapshot.size())
        || !(std::ofstream(dir_ / "coords.json") << coords)) {
      throw std::runtime_error("could not save downstream validation input");
    }
    threads_ = cfg.explore_threads;
    auto args = cfg.validation_args;
    args.insert(args.end(),
                {"--coords",
                 (dir_ / "coords.json").string(),
                 "--workdir",
                 (dir_ / "score").string(),
                 "--receipt",
                 (dir_ / "receipt.json").string(),
                 "--placement-seconds",
                 std::to_string(placement_seconds),
                 "--threads",
                 std::to_string(threads_)});
    args.insert(args.begin(), {cfg.worker_args.at(0), "--placement-validation", std::to_string(getpid())});
    std::vector<char*> argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, (dir_ / "worker.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    const auto rc = posix_spawnp(&pid_, argv.front(), &actions, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) {
      pid_ = -1;
      throw std::system_error(rc, std::generic_category(), "spawn validation flow");
    }
    slot_.adopt(pid_);
    slot_.detach();
    startup_s_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    status_  = "running";
    monitor_   = std::jthread([this,
                             sig,
                             geometry,
                             score     = proposal->config().score,
                             estimator = proposal->config().score_estimator,
                             deadline  = deadline_for(cfg, "T4", start_)](std::stop_token stop) {
      while (!stop.stop_requested()) {
        {
          std::lock_guard lock(mutex_);
          if (pid_ <= 0) {
            return;
          }
          int        code   = 0;
          const auto reaped = wait_worker(&code, WNOHANG);
          if (reaped == pid_) {
            pid_    = -1;
            status_ = "failed";
            reason_ = "validation flow exited without a routed result";
            std::ifstream       stream(dir_ / "receipt.json");
            const std::string   receipt{std::istreambuf_iterator<char>(stream), {}};
            rapidjson::Document parsed;
            parsed.Parse<rapidjson::kParseFullPrecisionFlag>(receipt.data(), receipt.size());
            if (parsed.IsObject() && parsed.HasMember("row") && parsed["row"].IsObject() && parsed["row"].HasMember("status")
                && parsed["row"]["status"].IsString()) {
              const std::string_view row_status = parsed["row"]["status"].GetString();
              if (row_status == "routed") {
                reason_ = routed_validation_error(parsed, code);
                if (reason_.empty()) {
                  status_ = "done";
                }
              }
              // A failed downstream row is useful evidence. A contradictory routed row must
              // not appear as a measured success in the ledger; its receipt is still downloadable.
              if (status_ == "done" || row_status == "failed") {
                Summary_metrics m;
                m.geometry     = geometry;  // The archived coordinates are the submitted macro proposal.
                m.timing_model = "final_downstream_sta";
                if (status_ == "done" && parsed.HasMember("setup_timing") && parsed["setup_timing"].IsObject()
                    && parsed["setup_timing"].HasMember("status") && parsed["setup_timing"]["status"] == "measured") {
                  // Keep the benchmark's final row precision; routed wirelength is not placement HPWL.
                  m.wns = parsed["row"]["wns"].GetDouble() * 1000;
                  m.tns = parsed["row"]["tns"].GetDouble() * 1000;
                  if (const auto* groups = routed_endpoints(parsed)) {
                    rapidjson::StringBuffer                    buffer;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                    groups->Accept(writer);
                    m.endpoints = buffer.GetString();
                  }
                }
                routed_summary_ = measurement_summary(m, score, estimator);
                result_         = std::format(R"({{"validation":{},"signature":{},"geometry_status":"{}","summary":{}}})",
                                      receipt,
                                      sig,
                                      geometry,
                                      routed_summary_);
              }
            }
          } else if (std::chrono::steady_clock::now() >= deadline) {
            process_group::kill_tree(pid_);
            while (wait_worker(nullptr, 0) < 0 && errno == EINTR) {
            }
            pid_    = -1;
            status_ = "timed_out";
            reason_ = "validation wall deadline reached";
          }
          if (pid_ < 0) {
            end_ = std::chrono::steady_clock::now();
            slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
    watch_resources(cfg);
  } catch (...) {
    cancel();
    std::error_code ec;
    fs::remove_all(dir_, ec);
    throw;
  }
}
Replay_worker::Replay_worker(const Config& cfg, const fs::path& root, std::string id, const Restart_request& base,
                             const std::string& identity, Job_slot slot, std::string tier, std::string patch, std::string relax,
                             std::string repair_mode, unsigned repair_limit, std::string until)
    : id_(std::move(id)), tier_(std::move(tier)), baseline_(identity), gate_(base.label), slot_(std::move(slot)), start_(std::chrono::steady_clock::now()) {
  foreground_main_seq_ = base.seq;
  foreground_gate_nth_ = base.nth;
  if (cfg.worker_args.empty()) {
    throw std::runtime_error("no replay recipe");
  }
  dir_ = private_worker_dir(root);
  try {
    socket_                = worker_socket(dir_).string();
    const auto    baseline = dir_ / "baseline.jsonl";
    std::ofstream trace(baseline);
    if (base.captured_trace) {
      trace << *base.captured_trace;
    } else if (base.seq || !base.trace_source.empty()) {
      std::ifstream input(base.trace_source.empty() ? cfg.trace_path : base.trace_source);
      if (!input) {
        throw std::runtime_error("baseline trace unavailable");
      }
      for (std::string line; std::getline(input, line);) {
        rapidjson::Document d;
        d.Parse(line.c_str());
        if (d.HasParseError() || !d.IsObject() || !d.HasMember("seq") || !d["seq"].IsUint64()) {
          throw std::runtime_error("invalid baseline trace");
        }
        if (d["seq"].GetUint64() <= base.seq) {
          trace << line << '\n';
        }
      }
    }
    trace.close();
    if (!trace) {
      throw std::runtime_error("cannot save private baseline trace");
    }
    // Only normalized input options are passed. All worker-owned outputs are
    // relative to its private directory, including the resume segment.
    auto args = cfg.worker_args;
    threads_ = tier_.empty() ? 1 : cfg.explore_threads;
    for (size_t i = 1; i + 1 < args.size() && args[i] != "--";) {
      if (args[i] == "--set"
          && (args[i + 1].starts_with("synth.place.threads=") || args[i + 1].starts_with("place.threads=")
              || args[i + 1].starts_with("lhd.jobs="))) {
        args.erase(args.begin() + i, args.begin() + i + 2);
        continue;
      }
      ++i;
    }
    for (const auto& [key, value] : base.set) {
      const auto end = std::find(args.begin(), args.end(), "--");
      if (key == "lhd.seed") {
        const auto seed = std::find(args.begin(), end, "--seed");
        if (seed == end || seed + 1 == end) {
          throw std::runtime_error("missing replay seed");
        }
        *(seed + 1) = value;
      }
      const auto prefix = key + "=";
      for (auto it = args.begin(); it != args.end() && *it != "--";) {
        if (*it == "--set" && it + 1 != args.end() && (it + 1)->starts_with(prefix)) {
          it = args.erase(it, it + 2);
        } else {
          ++it;
        }
      }
      if (key != "lhd.seed") {
        args.insert(std::find(args.begin(), args.end(), "--"), {"--set", prefix + value});
      }
    }
    const auto               raw     = std::find(args.begin(), args.end(), "--");
    std::vector<std::string> options = {"--result-json",
                                        (dir_ / "flow.json").string(),
                                        "--set",
                                        std::format("lhd.jobs={}", threads_),  // the worker's thread budget
                                        "--workdir",
                                        (dir_ / "W").string(),
                                        "--gui",
                                        "--gui-no-open",
                                        "--gui-replay",
                                        baseline.string(),
                                        "--gui-resume",
                                        base.label + ":" + std::to_string(base.nth),
                                        "--gui-replay-seq",
                                        std::to_string(base.seq)};
    args.insert(raw, options.begin(), options.end());
    const int table = Job_manager::global().shared_fd();
    if (table < 0) {
      throw std::runtime_error("no shared placement admission table");
    }
    const int child_table = table + 1;
    args.insert(args.begin() + 1,
                {"--placement-worker",
                 std::to_string(::getpid()),
                 socket_,
                 id_,
                 std::to_string(child_table),
                 tier_.empty()    ? "foreground"
                 : tier_ == "T3" && !relax.empty() ? "T3-scoped"
                 : until == "end" ? "T2-end"
                                  : tier_,
                 std::to_string(repair_limit),
                 repair_mode,
                 std::to_string(cfg.timing_call_limit)});
    std::vector<char*> argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, table, child_table);
    const auto log = (dir_ / "worker.log").string();
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    const auto rc = posix_spawnp(&pid_, argv.front(), &actions, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) {
      pid_ = -1;
      throw std::system_error(rc, std::generic_category(), "spawn replay evaluator");
    }
    slot_.adopt(pid_);
    slot_.detach();
    std::string terms;
    for (const auto& [loss, params] : cfg.score) {
      terms += std::format(R"({}{{"loss":{},"weight":{},"half":{}}})",
                           terms.empty() ? "" : ",",
                           json_string(loss),
                           params.first,
                           params.second);
    }
    const auto score_config
        = std::format(R"({{"cmd":"score_config","estimator":{},"components":[{}]}})", json_string(cfg.score_estimator), terms);
    monitor_ = std::jthread([this,
                             identity,
                             variant = base.recipe_variant,
                             source  = cfg.source_identity,
                             build   = session_build(cfg.hello),
                             score_config,
                             label   = base.label,
                             deadline = deadline_for(cfg, tier_.empty() ? "foreground" : tier_, start_),
                             memory   = uint64_t(cfg.worker_memory_mb) << 20,
                             until   = until == "end" ? "placement_end" : std::move(until),
                             patch   = std::move(patch),
                             relax   = std::move(relax)](std::stop_token stop) {
      bool     configured = false, patched = false, requested = false, relax_requested = false, repair_started = false;
      bool     advanced        = false;
      uint64_t departure_frame = 0;
      uint64_t evaluation = 0, map_version = 0;
      // Each command is acknowledged on the private flow thread before the
      // next one is submitted. A failed patch cannot publish a partial result.
      std::vector<std::string> commands;
      if (!patch.empty()) {
        rapidjson::Document document;
        document.Parse<rapidjson::kParseFullPrecisionFlag>(patch.c_str());
        const auto append = [&](const rapidjson::Value& cmd) {
          rapidjson::StringBuffer                    buf;
          rapidjson::Writer<rapidjson::StringBuffer> w(buf);
          cmd.Accept(w);
          commands.emplace_back(buf.GetString());
        };
        if (document.IsArray()) {
          for (const auto& entry : document.GetArray()) {
            append(entry);
          }
        } else {
          append(document);
        }
      }
      size_t      next_command = 0;
      uint64_t    command_sequence = 0;
      bool        waiting_command = false;
      std::string topology, masters;
      std::string measured_epoch;
      uint64_t measured_sequence = 0;
      while (!stop.stop_requested()) {
        std::string status, reason;
        if (std::chrono::steady_clock::now() >= deadline) {
          status = "timed_out";
          reason = "wall deadline reached";
        } else if (fs::exists(socket_)) {
          try {
            const auto          state = agent(R"({"cmd":"state"})");
            rapidjson::Document d;
            d.Parse<rapidjson::kParseFullPrecisionFlag>(state.c_str());
            if (d.IsObject() && d.HasMember("state")) {
              const std::string current = d["state"].GetString();
              {
                std::lock_guard lock(mutex_);
                if (startup_s_ >= 0) {
                  work_ = serialize(d["work"]);
                }
                // A move or ranking refresh may already have advanced the live
                // frame. The worker file still identifies the last full result.
                if (tier_.empty() && status_ == "ready" && d["evaluation_seq"].GetUint64()
                    && (measured_epoch != d["epoch"].GetString() || measured_sequence != d["evaluation_seq"].GetUint64())) {
                  read_foreground_measurement_locked();
                  measured_epoch = d["epoch"].GetString();
                  measured_sequence = d["evaluation_seq"].GetUint64();
                }
              }
              if (d["timing_budget_exhausted"].GetBool()) {
                status = "budget_exhausted";
                reason = "candidate timing call budget exhausted";
              } else if (current == "failed") {
                status = "failed";
                reason = d["error"].GetString();
              } else if ((d["paused"].GetBool() && std::string_view(d["gate"].GetString()) == (advanced ? until : label)
                          && (!advanced || d["frame"].GetUint64() != departure_frame) && !repair_started)
                         || (repair_started && std::string_view(d["gate"].GetString()) == "repair_result")) {
                bool approved = false;
                {
                  std::lock_guard lock(mutex_);
                  if (status_ == "starting") {
                    startup_s_         = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
                    replayed_baseline_ = d["baseline_identity"].GetString();
                    replay_work_       = serialize(d["replay_work"]);
                    work_              = serialize(d["work"]);
                    if ((!variant && identity != replayed_baseline_)
                        || (variant && (!d.HasMember("source_identity") || source != d["source_identity"].GetString()))) {
                      status = "stale";
                      reason = std::format("replayed baseline {} differs from requested {}",
                                           d["baseline_identity"].GetString(),
                                           identity);
                    } else {
                      topology = d["topology"].GetString();
                      masters  = d["masters"].GetString();
                      status_  = "configuring";
                    }
                  }
                  approved = status_ == "running";
                }
                if (status == "stale" && d.HasMember("source_identity") && source != d["source_identity"].GetString()) {
                  reason += "; physical inputs or the LiveHD build changed during this session";
                  if (!build.empty()) {
                    try {
                      const auto replay_build = session_build(agent(R"({"cmd":"hello"})"));
                      if (!replay_build.empty() && replay_build != build) {
                        reason = std::format("LiveHD build changed during this session ({} -> {}); restart with the same executable to compare",
                                             build, replay_build);
                      }
                    } catch (const std::exception&) {
                      // A worker can exit during diagnosis. Preserve the original stale verdict.
                    }
                  }
                }
                if (!configured && status.empty()) {
                  const auto configure = [&](const std::string& command) {
                    rapidjson::Document answer;
                    answer.Parse(agent(command).c_str());
                    if (!answer.IsObject() || !answer.HasMember("ok") || !answer["ok"].GetBool()) {
                      throw std::runtime_error("evaluator rejected experiment configuration");
                    }
                  };
                  configure(score_config);
                  if (!tier_.empty()) {
                    configure(R"({"cmd":"policy","fast":true})");
                  }
                  configured = true;
                  std::lock_guard lock(mutex_);
                  if (pid_ <= 0) {
                    return;
                  }
                  status_ = tier_.empty() ? "ready" : "running";
                } else if (approved && !patched) {
                  if (waiting_command && d["completed_command"].GetUint64() >= command_sequence) {
                    rapidjson::Document receipt;
                    receipt.Parse(agent(std::format(R"({{"cmd":"command_status","sequence":{}}})", command_sequence)).c_str());
                    if (!receipt.IsObject() || !receipt.HasMember("status") || std::string_view(receipt["status"].GetString()) != "applied") {
                      status = "failed";
                      reason = receipt.IsObject() && receipt.HasMember("reason") ? receipt["reason"].GetString() : "patch was not applied";
                    } else {
                      waiting_command = false;
                      ++next_command;
                    }
                  }
                  if (status.empty() && !waiting_command) {
                    if (next_command == commands.size()) {
                      patched = true;
                    } else {
                      rapidjson::Document answer;
                      answer.Parse(agent(commands[next_command]).c_str());
                      if (!answer.IsObject() || !answer.HasMember("ok") || !answer["ok"].GetBool()) {
                        status = "failed";
                        reason = answer.IsObject() && answer.HasMember("error") ? answer["error"].GetString() : "patch rejected";
                      } else {
                        command_sequence = answer["sequence"].GetUint64();
                        waiting_command  = true;
                      }
                    }
                  }
                } else if (approved && !until.empty() && !advanced) {
                  rapidjson::Document answer;
                  answer.Parse(
                      agent(std::format(R"({{"cmd":"cmd","command":{{"cmd":"run_until","gate":{}}}}})", json_string(until)))
                          .c_str());
                  if (!answer.IsObject() || !answer.HasMember("ok") || !answer["ok"].GetBool()) {
                    status = "failed";
                    reason = "could not advance to requested stop";
                  } else {
                    departure_frame = d["frame"].GetUint64();
                    advanced        = true;
                  }
                } else if (approved && tier_ != "T3" && !relax.empty() && !relax_requested) {
                  rapidjson::Document answer;
                  answer.Parse(agent(relax).c_str());
                  if (!answer.IsObject() || !answer.HasMember("ok") || !answer["ok"].GetBool()) {
                    status = "failed";
                    reason = answer.IsObject() && answer.HasMember("error") ? answer["error"].GetString() : "relax request failed";
                  }
                  relax_requested = true;
                } else if (approved && relax_requested && std::string_view(d["legalization"]["status"].GetString()) == "failed") {
                  status = "failed";
                  reason = d["legalization"]["error"].GetString();
                } else if (approved && tier_ == "T3" && !repair_started) {
                  // Run through ordinary phase switches and pause only at the authoritative final candidate.
                  auto                reply = agent(relax.empty() ? R"({"cmd":"cmd","command":{"cmd":"finish"}})" : relax);
                  rapidjson::Document ack;
                  ack.Parse(reply.c_str());
                  if (!ack.IsObject() || !ack.HasMember("ok") || !ack["ok"].GetBool()) {
                    status = "failed";
                    reason = "could not release repair evaluator";
                  } else {
                    repair_started = true;
                  }
                } else if (approved && !requested
                           && (relax.empty() || tier_ == "T3" || std::string_view(d["legalization"]["status"].GetString()) == "applied")) {
                  evaluation  = d["evaluation_seq"].GetUint64();
                  map_version = d["map_v"].GetUint64();
                  agent(R"({"cmd":"cmd","command":{"cmd":"regions","on":true}})");
                  agent(R"({"cmd":"cmd","command":{"cmd":"map","kind":"all"}})");
                  agent(R"({"cmd":"evaluate"})");
                  requested = true;
                } else if (approved && requested && d["evaluation_seq"].GetUint64() > evaluation
                           && d["evaluation_frame"].GetUint64() == d["frame"].GetUint64() && !d["has_timing"].GetBool()) {
                  status = "failed";
                  reason = d["note"].GetString();
                  if (reason.empty()) {
                    reason = "full timing unavailable for this placement";
                  }
                } else if (approved && requested && d["evaluation_seq"].GetUint64() > evaluation
                           && d["map_v"].GetUint64() > map_version && d["map_frame"].GetUint64() == d["frame"].GetUint64()
                           && d["frame"].GetUint64() == d["evaluation_frame"].GetUint64() && !d["timing_est"].GetBool()) {
                  if (tier_ != "T3" && (topology != d["topology"].GetString() || masters != d["masters"].GetString())) {
                    status = "failed";
                    reason = "implementation changed during frozen timing";
                  } else {
                    // Publish only a complete document. The directory is private,
                    // and readers cannot discover it through the job until done.
                    const auto    signature = agent(R"({"cmd":"signature"})");
                    const auto    bytes     = request("GET", "/result.snapshot", {});
                    std::ofstream file(dir_ / "result.tmp", std::ios::binary);
                    file.write(bytes.data(), bytes.size());
                    file.close();
                    if (!file) {
                      throw std::runtime_error("cannot persist placement result");
                    }
                    fs::rename(dir_ / "result.tmp", dir_ / "result.bin");
                    std::lock_guard lock(mutex_);
                    if (pid_ <= 0) {
                      return;
                    }
                    result_ = std::format(
                        R"({{"timing_available":{},"wns_ps":{},"tns_ps":{},"geometry_status":{},"identity":{},"topology":{},"masters":{}}})",
                        d["has_timing"].GetBool(),
                        d["wns"].IsNumber() ? std::format("{}", d["wns"].GetDouble()) : "null",
                        d["tns"].IsNumber() ? std::format("{}", d["tns"].GetDouble()) : "null",
                        json_string(d["geometry_status"].GetString()),
                        json_string(d["placement_identity"].GetString()),
                        json_string(d["topology"].GetString()),
                        json_string(d["masters"].GetString()));
                    rapidjson::StringBuffer                    buf;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
                    d["summary"].Accept(writer);
                    result_.pop_back();
                    result_ += std::string{",\"summary\":"} + buf.GetString() + "}";
                    result_.pop_back();
                    result_ += std::string{",\"signature\":"} + signature + "}";
                    if (tier_ == "T3") {
                      result_.pop_back();
                      result_ += std::format(R"(,"baseline_topology":{},"baseline_masters":{},"repair_trials":{}}})",
                                             json_string(topology),
                                             json_string(masters),
                                             d["repair_trials"].GetUint());
                    }
                    status = "done";
                  }
                }
              } else if (current == "finished") {
                status = tier_.empty() ? "done" : "failed";
                reason = advanced ? "requested placement gate was not reached: " + until : "evaluator finished";
              }
            }
          } catch (const std::exception& e) {
            if (requested && fs::exists(dir_ / "result.tmp")) {
              status = "failed";
              reason = e.what();
            }
          }
        }
        {
          std::lock_guard lock(mutex_);
          int             result = 0;
          if (pid_ <= 0) {
            return;
          }
          if (wait_worker(&result, WNOHANG) == pid_) {
            pid_ = -1;
            if (status.empty()) {
              if (tier_.empty() && WIFEXITED(result) && WEXITSTATUS(result) == 0 && status_ == "ready") {
                status = "done";
                reason = "evaluator finished";
              } else {
                status = "failed";
                reason = "evaluator exited before publishing a result";
              }
            }
          }
          if (!status.empty()) {
            if (tier_.empty()) {
              read_foreground_measurement_locked();
            }
            status_ = std::move(status);
            reason_ = std::move(reason);
            if (pid_ > 0 && (status_ != "done" || !tier_.empty())) {
              process_group::kill_tree(pid_);
              while (wait_worker(nullptr, 0) < 0 && errno == EINTR) {
              }
              pid_ = -1;
            }
            // A worker that finishes between two resource polls (the watcher samples every 500 ms) was never
            // measured; the reaped peak RSS (wait4) still tells whether it overran, so it publishes nothing.
            if (memory && peak_rss_bytes_ > memory && status_ != "failed") {
              peak_tree_rss_bytes_ = std::max(peak_tree_rss_bytes_, peak_rss_bytes_);
              status_              = "budget_exhausted";
              reason_              = "worker process tree exceeded memory limit";
              result_.clear();
            }
            end_ = std::chrono::steady_clock::now();
            slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
    watch_resources(cfg);
  } catch (...) {
    cancel();
    std::error_code ec;
    fs::remove_all(dir_, ec);
    throw;
  }
}
Replay_worker::Replay_worker(const Config& cfg, std::string id, const Fork_files& files, const Fork_request& req, Job_slot slot)
    : dir_(req.dir), id_(std::move(id)), tier_("region"), slot_(std::move(slot)), start_(std::chrono::steady_clock::now()) {
  threads_ = cfg.explore_threads;
  auto      args  = cfg.region_args;
  const int table = Job_manager::global().shared_fd();
  if (args.empty() || table < 0) {
    throw std::runtime_error("region recipe unavailable");
  }
  const int child_table = table + 1;
  socket_               = worker_socket(dir_).string();
  args.insert(args.begin() + 1,
              {"--placement-worker", std::to_string(getpid()), socket_, "main", std::to_string(child_table), "region", "0", "", "-1"});
  args.insert(args.end(),
              {"lg:" + files.net_dir,
               "--top",
               files.top,
               "--workdir",
               (dir_ / "W").string(),
               "--gui",
               "--gui-no-open",
               "--result-json",
               (dir_ / "result.json").string(),
               "--set",
               "synth.place.floorplan=" + files.floorplan,
               "--set",
               "synth.place.start_map=" + files.seed,
               "--set",
               std::format("lhd.jobs={}", threads_)});  // the worker's thread budget
  std::vector<char*> argv;
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  const auto                 log = (dir_ / "child.log").string();
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, table, child_table);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attr, 0);
  const int rc = posix_spawnp(&pid_, argv.front(), &actions, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  if (rc) {
    pid_ = -1;
    throw std::system_error(rc, std::generic_category(), "spawn region evaluator");
  }
  slot_.adopt(pid_);
  slot_.detach();
  try {
    monitor_ = std::jthread([this, deadline = deadline_for(cfg, "region", start_)](std::stop_token stop) {
      bool seen = false;
      while (!stop.stop_requested()) {
        if (!seen && fs::exists(socket_)) {
          try {
            rapidjson::Document d;
            d.Parse(request("POST", "/agent", R"({"cmd":"hello"})").c_str());
            if (d.IsObject() && d.HasMember("url") && d["url"].IsString()) {
              std::lock_guard lock(mutex_);
              if (pid_ <= 0) {
                return;
              }
              startup_s_  = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
              region_url_ = d["url"].GetString();
              status_     = "running";
              seen        = true;
              region_started_.notify_all();
            }
          } catch (const std::exception&) {
          }  // listener may still be starting
        }
        {
          std::lock_guard lock(mutex_);
          if (pid_ <= 0) {
            return;
          }
          int status;
          if (wait_worker(&status, WNOHANG) == pid_) {
            pid_    = -1;
            status_ = "failed";
            reason_ = "region evaluator exited without a result";
            std::ifstream file(dir_ / "result.json");
            if (WIFEXITED(status) && !WEXITSTATUS(status) && file) {
              std::string         bytes{std::istreambuf_iterator<char>(file), {}};
              rapidjson::Document d;
              d.Parse(bytes.c_str());
              if (d.IsObject() && d.HasMember("qor") && d["qor"].HasMember("place")) {
                rapidjson::StringBuffer                    buf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
                d["qor"]["place"].Accept(writer);
                result_ = buf.GetString();
                status_ = "done";
                reason_.clear();
              }
            }
            end_ = std::chrono::steady_clock::now();
            slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
            region_started_.notify_all();
            return;
          }
          if (std::chrono::steady_clock::now() >= deadline) {
            process_group::kill_tree(pid_);
            while (wait_worker(nullptr, 0) < 0 && errno == EINTR) {
            }
            pid_    = -1;
            status_ = "timed_out";
            reason_ = "region wall deadline reached";
            end_    = std::chrono::steady_clock::now();
            slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
            region_started_.notify_all();
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
    watch_resources(cfg);
  } catch (...) {
    cancel();
    throw;
  }
}
std::string Replay_worker::await_region_page() {
  std::unique_lock lock(mutex_);
  region_started_.wait_for(lock, std::chrono::seconds(60), [&] { return !region_url_.empty() || status_ != "starting"; });
  if (region_url_.empty()) {
    throw std::runtime_error(reason_.empty() ? "region page failed to start within 60 seconds" : reason_);
  }
  return region_url_;
}
void Replay_worker::watch_resources(const Config& cfg) {
  // This watchdog stays live even when the evaluator's control socket is
  // blocked servicing a long full-design measurement.
  resource_watch_ = std::jthread([this,
                                  memory = uint64_t(cfg.worker_memory_mb) << 20,
                                  session_root = cfg.session_root_pid,
                                  session_limit = cfg.session_memory_bytes,
                                  disk   = uint64_t(cfg.worker_disk_mb) << 20,
                                  deadline = deadline_for(cfg, tier_.empty() ? "foreground" : tier_, start_)](std::stop_token stop) {
    while (!stop.stop_requested()) {
      pid_t child;
      {
        std::lock_guard lock(mutex_);
        child = pid_;
      }
      if (child <= 0) {
        return;
      }
      const auto                                    rss  = process_tree_rss(child);
      const auto session_rss = session_limit ? process_tree_rss(session_root) : 0;
      uint64_t                                      used = 0;
      std::error_code                               ec;
      std::filesystem::recursive_directory_iterator entry(dir_, std::filesystem::directory_options::skip_permission_denied, ec);
      for (decltype(entry) end; !ec && entry != end; entry.increment(ec)) {
        struct stat file{};
        if (lstat(entry->path().c_str(), &file) == 0 && S_ISREG(file.st_mode)) {
          used += uint64_t(file.st_blocks) * 512;
        }
      }
      {
        std::lock_guard lock(mutex_);
        if (child != pid_) {
          return;
        }
        peak_tree_rss_bytes_ = std::max(peak_tree_rss_bytes_, rss);
        peak_session_rss_bytes_ = std::max(peak_session_rss_bytes_, session_rss);
        peak_disk_bytes_     = std::max(peak_disk_bytes_, used);
        const bool overdue   = std::chrono::steady_clock::now() >= deadline;
        if ((memory && rss > memory) || session_rss > session_limit || (disk && used > disk) || overdue) {
          process_group::kill_tree(pid_);
          while (wait_worker(nullptr, 0) < 0 && errno == EINTR) {
          }
          pid_    = -1;
          end_    = std::chrono::steady_clock::now();
          status_ = overdue ? "timed_out" : "budget_exhausted";
          reason_ = overdue                  ? "wall deadline reached"
                    : rss > memory && memory ? "worker process tree exceeded memory limit"
                    : session_rss > session_limit ? "session process tree exceeded memory budget"
                                             : "worker exceeded scratch disk limit";
          result_.clear();
          slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
          region_started_.notify_all();
          return;
        }
      }
      for (unsigned tick = 0; tick < 10 && !stop.stop_requested(); ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  });
}
Replay_worker::~Replay_worker() {
  cancel();
  std::error_code ec;
  fs::remove_all(dir_, ec);
}
void Replay_worker::cancel() {
  std::lock_guard close(close_mutex_);
  resource_watch_.request_stop();
  monitor_.request_stop();
  {
    std::lock_guard lock(mutex_);
    if (pid_ > 0) {
      process_group::kill_tree(pid_);
      while (wait_worker(nullptr, 0) < 0 && errno == EINTR) {
      }
      pid_ = -1;
    }
    if (status_ == "ready" || status_ == "starting" || status_ == "configuring" || status_ == "running") {
      status_ = "cancelled";
    }
    if (end_ == decltype(end_){}) {
      end_ = std::chrono::steady_clock::now();
    }
    slot_.complete(std::max(peak_tree_rss_bytes_, peak_rss_bytes_));
    region_started_.notify_all();
  }
  if (monitor_.joinable()) {
    monitor_.join();
  }
  if (resource_watch_.joinable()) {
    resource_watch_.request_stop();
    resource_watch_.join();
  }
}
// wait4 includes the worker's reaped descendants. Linux reports KiB, Darwin bytes.
pid_t Replay_worker::wait_worker(int* status, int flags) {
  if (owner_) {
    try {
      owner_->release(pid_, cpu_s_, peak_rss_bytes_);
    } catch (const std::exception&) {
    }
    // Closed checkpoints can be freed while the ledger still retains this worker's results.
    owner_.reset();
    return pid_;
  }
  struct rusage usage{};
  const pid_t   child = wait4(pid_, status, flags, &usage);
  if (child > 0) {
    const auto seconds = [](timeval t) { return t.tv_sec + t.tv_usec * 1e-6; };
    cpu_s_             = seconds(usage.ru_utime) + seconds(usage.ru_stime);
    peak_rss_bytes_    = static_cast<uint64_t>(usage.ru_maxrss);
#ifndef __APPLE__
    peak_rss_bytes_ *= 1024;
#endif
  }
  return child;
}
bool Replay_worker::ready() const {
  std::lock_guard lock(mutex_);
  return status_ == "ready" || status_ == "done";
}
bool Replay_worker::terminal() const {
  std::lock_guard lock(mutex_);
  return status_ != "ready" && status_ != "starting" && status_ != "configuring" && status_ != "running";
}
void Replay_worker::read_foreground_measurement_locked() {
  std::ifstream file(dir_ / "foreground-measurement.json");
  if (file) {
    foreground_measurement_.assign(std::istreambuf_iterator<char>(file), {});
  }
}

void Replay_worker::collect_foreground_history(size_t byte_limit) {
  // Called after the worker tree has stopped. The final JSONL record may
  // have been interrupted by cancellation. Keep only complete records.
  std::ifstream trace(dir_ / "foreground-segment.jsonl");
  std::string segment = "[";
  uint64_t hash = hash_util::fnv1a64("["), count = 0;
  bool evicted = false;
  for (std::string line; std::getline(trace, line);) {
    rapidjson::Document action;
    action.Parse(line.c_str());
    if (action.IsObject() && action.HasMember("cmd") && action["cmd"].IsString()) {
      if (count != 0) {
        hash = hash_util::fnv1a64(",", hash);
      }
      ++count;
      const auto entry = serialize(action);
      hash = hash_util::fnv1a64(entry, hash);
      if (evicted) {
        continue;
      }
      if (segment.size() + entry.size() + 2 > byte_limit) {
        segment.clear();
        evicted = true;
        continue;
      }
      if (count > 1) {
        segment += ',';
      }
      segment += entry;
    }
  }
  std::lock_guard lock(mutex_);
  if (!evicted) {
    segment += "]";
  }
  foreground_segment_ = std::move(segment);
  foreground_segment_evicted_ = evicted;
  foreground_segment_hash_ = hash_util::fnv1a64("]", hash);
  foreground_segment_count_ = count;
  read_foreground_measurement_locked();
}
uint64_t Replay_worker::unallocated_memory(uint64_t reservation) const {
  std::lock_guard lock(mutex_);
  if (pid_ <= 0) {
    return 0;
  }
  const auto resident = process_tree_rss(pid_);
  return reservation - std::min(reservation, resident);
}
std::string Replay_worker::status() const {
  std::lock_guard lock(mutex_);
  const double    elapsed
      = std::chrono::duration<double>((end_ == decltype(end_){} ? std::chrono::steady_clock::now() : end_) - start_).count();
  const bool replay = !snapshot_ && tier_ != "T4" && tier_ != "region";
  const double startup = startup_s_ >= 0 ? startup_s_ : elapsed;
  auto row = std::format(
      R"({{"id":{},"worker":"{}","state":{},"reason":{},"threads":{},"replay_s":{},"startup_s":{},"run_s":{},"worker_s":{},"work":{},"replay_work":{},"cpu_s":{},"peak_rss_bytes":{},"sampled_tree_rss_bytes":{},"sampled_session_rss_bytes":{},"sampled_scratch_bytes":{},"replayed_baseline":{},"page_url":{},"tier":{},"result":{}}})",
      json_string(id_),
      snapshot_       ? "snapshot"
      : tier_ == "T4" ? "external_validation"
                      : "replay",
      json_string(status_),
      json_string(reason_),
      threads_,
      replay ? startup : 0,
      startup,
      startup_s_ >= 0 ? std::format("{}", std::max(0., elapsed - startup)) : "null",
      elapsed,
      work_,
      replay_work_,
      cpu_s_ >= 0 ? std::format("{}", cpu_s_) : "null",
      peak_rss_bytes_ ? std::to_string(peak_rss_bytes_) : "null",
      peak_tree_rss_bytes_,
      peak_session_rss_bytes_,
      peak_disk_bytes_,
      json_string(replayed_baseline_),
      json_string(status_ == "running" ? region_url_ : ""),
      json_string(tier_.empty() ? "foreground" : tier_),
      result_.empty() ? "null" : result_);
  if (tier_.empty()) {
    row.pop_back();
    if (foreground_segment_evicted_) {
      row += std::format(R"(,"segment_evicted":true,"segment_commands":{},"segment_hash":"{:016x}")",
                         foreground_segment_count_, foreground_segment_hash_);
    }
    row += std::format(R"(,"mode":"foreground","parent":"main","baseline":{},"gate":{},"gate_nth":{},"trace_seq":{},"segment":{},"last_measurement":{}}})",
                       json_string(baseline_), json_string(gate_), foreground_gate_nth_, foreground_main_seq_,
                       foreground_segment_.empty() ? "null" : foreground_segment_,
                       foreground_measurement_.empty() ? "null" : foreground_measurement_);
  }
  return row;
}
std::string Replay_worker::request(std::string_view method, std::string_view path, std::string_view body) const {
  if (path.find_first_of("\r\n") != path.npos) {
    throw std::runtime_error("invalid exploration route");
  }
  Socket      sock{::socket(AF_UNIX, SOCK_STREAM, 0)};
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socket_.size() >= sizeof(addr.sun_path)) {
    throw std::runtime_error("worker socket path too long");
  }
  std::memcpy(addr.sun_path, socket_.c_str(), socket_.size() + 1);
  if (sock.fd < 0 || ::connect(sock.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error("worker socket unavailable");
  }
  timeval timeout{20, 0};
  ::setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
  int on = 1;
  ::setsockopt(sock.fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
  std::string data
      = std::format("{} {} HTTP/1.1\r\nHost: localhost\r\nContent-Length: {}\r\n\r\n{}", method, path, body.size(), body);
  size_t sent = 0;
  while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto n = ::send(sock.fd, data.data() + sent, data.size() - sent, flags);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      throw std::runtime_error("worker stopped during request");
    }
    sent += n;
  }
  data.clear();
  char chunk[16384];
  for (;;) {
    const auto n = ::recv(sock.fd, chunk, sizeof(chunk), 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      throw std::runtime_error("worker reply unavailable");
    }
    if (n == 0) {
      break;
    }
    data.append(chunk, n);
  }
  const auto at = data.find("\r\n\r\n");
  if (at == data.npos || !data.starts_with("HTTP/1.1 200 ")) {
    throw std::runtime_error("worker returned an incomplete or failed response");
  }
  const auto                 header = std::string_view(data).substr(0, at);
  constexpr std::string_view field  = "\r\nContent-Length: ";
  const auto                 len    = header.find(field);
  if (len == header.npos) {
    throw std::runtime_error("missing worker response length");
  }
  auto number             = header.substr(len + field.size());
  number                  = number.substr(0, number.find("\r\n"));
  size_t bytes            = 0;
  const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), bytes);
  if (error != std::errc{} || end != number.data() + number.size() || bytes != data.size() - at - 4) {
    throw std::runtime_error("incomplete worker response");
  }
  return data.substr(at + 4);
}
std::shared_ptr<Session> Replay_worker::saved_result() const {
  std::lock_guard lock(mutex_);
  if (tier_.empty() || tier_ == "region" || status_ != "done") {
    return {};
  }
  if (auto doc = document_.lock()) {
    return doc;
  }
  std::ifstream stream(dir_ / "result.bin", std::ios::binary);
  if (!stream) {
    throw std::runtime_error("saved placement result unavailable");
  }
  const std::string        bytes{std::istreambuf_iterator<char>(stream), {}};
  std::shared_ptr<Session> doc = Session::read_result_snapshot(bytes, routed_summary_);
  document_                    = doc;
  return doc;
}
std::string Replay_worker::agent(const std::string& json) const {
  if (auto doc = saved_result()) {
    return doc->agent_call(json);
  }
  return request("POST", "/agent", json);
}
std::string Replay_worker::get(const std::string& route) const {
  if (tier_ == "T4" && route.starts_with("/validation/")) {
    const auto artifact = route.substr(12, route.find('?') == route.npos ? route.npos : route.find('?') - 12);
    static const std::set<std::string> known{"receipt.json",
                                             "coords.json",
                                             "score/inputs.json",
                                             "score/score.tcl",
                                             "score/row.csv",
                                             "score/setup_timing.json",
                                             "score/setup_endpoints.tsv",
                                             "score/openroad.log",
                                             "score/routed_macro_poses.tsv",
                                             "score/placement_checks.json",
                                             "score/macro_poses.tsv"};
    if (!known.contains(artifact)) {
      const auto name = fs::path(artifact).filename().string();
      if (!name.starts_with("04_") || !name.ends_with(".drc.rpt") || artifact != "score/reports/" + name) {
        throw std::invalid_argument("unknown validation artifact");
      }
    }
    std::ifstream input(dir_ / artifact, std::ios::binary);
    if (!input) {
      throw std::runtime_error("artifact unavailable");
    }
    return {std::istreambuf_iterator<char>(input), {}};
  }
  if (auto doc = saved_result()) {
    return doc->read_document(route);
  }
  return request("GET", route, {});
}
}  // namespace livehd::placement::gui
