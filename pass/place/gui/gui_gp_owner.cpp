// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_gp_owner.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/process_group.hpp"
#include "gui_gp_reports.hpp"
#include "gui_ipc.hpp"
#include "gui_native_wire.hpp"
#include "gui_wire.hpp"
#include "pass/place/place.hpp"
#include "pass/place/steiner_topology.hpp"

extern char** environ;
namespace livehd::placement::gui {
namespace dp = DreamPlace::native;
namespace fs = std::filesystem;

namespace {
thread_local int                          branch_channel    = -1;
thread_local uint64_t                     branch_identity   = 0;
thread_local const std::function<bool()>* branch_idle_pulls = nullptr;
struct Poll_channel {
  int                          old;
  uint64_t                     identity;
  const std::function<bool()>* previous_pulls;
  Poll_channel(int fd, uint64_t id, const std::function<bool()>* idle)
      : old(std::exchange(branch_channel, fd))
      , identity(std::exchange(branch_identity, id))
      , previous_pulls(std::exchange(branch_idle_pulls, idle)) {}
  ~Poll_channel() {
    branch_channel    = old;
    branch_identity   = identity;
    branch_idle_pulls = previous_pulls;
  }
};
enum Host_hook : unsigned { observe = 1, progress = 2, before = 4, after = 8, pulls = 16, flute = 32, check_idle_pulls = 64 };
std::string unsupported(const dp::Options& o) {
  if (o.num_threads != 1) {
    return "native GP requires a serial placement recipe";
  }
  if (!o.interact_optimizer_identity || o.interact_interval <= 0 || o.max_iterations <= 0) {
    return "no live GP polls";
  }
  if (o.gp_search.enabled || o.magnet.enabled || o.trial_tuning) {
    return "native GP has a nested optimizer";
  }
  if (o.routability && o.route.model == dp::RouteModel::external) {
    return "native GP uses an external routing engine";
  }
  return {};
}
}  // namespace
bool native_gp_available() { return branch_channel >= 0 && (!branch_idle_pulls || (*branch_idle_pulls && (*branch_idle_pulls)())); }
bool retain_native_gp(const std::string& endpoint, uint64_t identity) {
  if (!native_gp_available() || !identity || identity != branch_identity || endpoint.empty()) {
    return false;
  }
  send_message(branch_channel, "retain", wire::pack(endpoint, identity));
  const auto reply = receive_message(branch_channel);
  if (reply.first == "error") {
    return false;
  }
  if (reply.first != "retained") {
    throw std::runtime_error("native GP retention lost its control channel");
  }
  return true;
}
void serve_native_gp(int fd, const std::string& inputs, std::set<pid_t>& children, bool (*single_thread)(), bool hosted) {
  dp::Design  design;
  dp::Options o;
  unsigned    hooks = 0;
  if (hosted) {
    wire::unpack(inputs, design, o, hooks);
  } else {
    wire::unpack(inputs, design, o);
  }
  if (auto why = unsupported(o); !why.empty()) {
    throw std::invalid_argument(why);
  }
  if (hooks & ~(observe | progress | before | after | pulls | flute | check_idle_pulls)) {
    throw std::invalid_argument("invalid native GP host hooks");
  }
  const pid_t                     owner_pid = getpid();
  bool                            child     = false;
  fs::path                        result_dir;
  int                             target        = 0;
  bool                            pulls_changed = false;
  std::optional<Native_gp_resume> resumed;
  std::set<pid_t>                 retained;
  const auto                      reap_retained = [&](bool block) {
    for (auto it = retained.begin(); it != retained.end();) {
      pid_t found;
      do {
        found = waitpid(*it, nullptr, block ? 0 : WNOHANG);
      } while (found < 0 && errno == EINTR);
      if (found == *it || (found < 0 && errno == ECHILD)) {
        it = retained.erase(it);
      } else {
        ++it;
      }
    }
  };
  const auto reap = [&](std::string_view data) {
    auto pid = wire::read<pid_t>(data);
    if (!children.contains(pid)) {
      send_message(fd, "error", "unknown native GP branch");
      return;
    }
    int   status;
    pid_t found;
    do {
      found = waitpid(pid, &status, WNOHANG);
    } while (found < 0 && errno == EINTR);
    if (found < 0) {
      throw std::system_error(errno, std::generic_category(), "reap native GP branch");
    }
    if (found) {
      children.erase(pid);
    }
    send_message(fd, "reap", wire::pack(found != 0, found != 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0));
  };
  const auto call = [&](const char* op, const std::string& bytes) {
    send_message(fd, op, bytes);
    auto [kind, reply] = receive_message(fd);
    if (kind != "reply") {
      throw std::runtime_error("lost native GP host at " + std::string(op));
    }
    return reply;
  };
  if (hooks & flute) {
    o.route_initialize = initialize_flute_tables;
  }
  if (hooks & observe) {
    o.observer = [&](const dp::Design& d, dp::PlacementStage stage, int iter) {
      if (!child) {
        call("observe", wire::pack(d, stage, iter));
      }
    };
  }
  if (hooks & progress) {
    o.progress_observer = [&](const dp::GpSample& row) {
      if (!child) {
        call("progress", wire::pack(row));
      }
    };
  }
  if (hooks & before) {
    o.before_legalize = [&](dp::Design& d) { d = wire::read<dp::Design>(call("before", wire::pack(d))); };
  }
  if (hooks & after) {
    o.after_macro_legalize = [&](dp::Design& d) { d = wire::read<dp::Design>(call("after", wire::pack(d))); };
  }
  if (hooks & pulls) {
    o.macro_pull_update = [&, first_check = true](const dp::Design& d, int i) mutable {
      if (child) {
        return std::optional<std::vector<dp::MacroPull>>{};
      }
      // The ordinary dynamic hint callback returns no update while idle. Query that
      // condition before transmitting the physical design; always deliver its first poll
      // and every poll after a replacement so clearing an earlier pull is not lost.
      if (!first_check && !pulls_changed && (hooks & check_idle_pulls) && wire::read<bool>(call("idle_pulls", {}))) {
        return std::optional<std::vector<dp::MacroPull>>{};
      }
      first_check    = false;
      auto update    = wire::read<std::optional<std::vector<dp::MacroPull>>>(call("pulls", wire::pack(d, i)));
      pulls_changed |= update.has_value();
      return update;
    };
  }
  o.interact = [&](const dp::InteractView& v) {
    const auto sample = [&] {
      return wire::pack(v.optimizer_identity,
                        v.iteration,
                        v.finishing,
                        std::span(v.x, size_t(v.count)),
                        std::span(v.y, size_t(v.count)));
    };
    if (resumed) {
      if (v.finishing) {
        resumed->finish(v);
        _exit(0);
      }
      return dp::InteractUpdate{};
    }
    if (child) {
      if ((target && v.iteration >= target) || v.finishing) {
        const auto    bytes = sample();
        std::ofstream output(result_dir / "native.tmp", std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.write(bytes.data(), bytes.size());
        output.close();
        fs::rename(result_dir / "native.tmp", result_dir / "native.bin");
        _exit(0);
      }
      return dp::InteractUpdate{};
    }
    reap_retained(false);
    if (hosted) {
      send_message(fd,
                   "host_frame",
                   wire::pack(sample(),
                              v.first,
                              pulls_changed,
                              bool(v.centroids),
                              v.centroids ? *v.centroids : std::vector<dp::CentroidSample>{}));
    } else {
      send_message(fd, "frame", sample());
    }
    for (;;) {
      auto [op, data] = receive_message(fd);
      if (op == "advance") {
        return wire::read<dp::InteractUpdate>(data);
      }
      if (op == "reap") {
        reap(data);
        continue;
      }
      if (op == "retain") {
        std::string endpoint;
        uint64_t    expected;
        wire::unpack(data, endpoint, expected);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (v.first || v.finishing || expected != v.optimizer_identity || pulls_changed || !single_thread() || endpoint.empty()
            || endpoint.size() >= sizeof(addr.sun_path)) {
          send_message(fd, "error", "ineligible live GP baseline");
          continue;
        }
        std::memcpy(addr.sun_path, endpoint.c_str(), endpoint.size() + 1);
        const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
          if (listener >= 0) {
            close(listener);
          }
          send_message(fd, "error", "cannot bind native GP endpoint");
          continue;
        }
        if (listen(listener, 1) < 0) {
          close(listener);
          unlink(endpoint.c_str());
          send_message(fd, "error", "cannot listen on native GP endpoint");
          continue;
        }
        const pid_t pid = fork();
        if (pid < 0) {
          close(listener);
          unlink(endpoint.c_str());
          send_message(fd, "error", "cannot retain native GP");
          continue;
        }
        if (!pid) {
          // The controller directory is private. A dead host cannot strand an owner before attach.
          close(fd);
#ifdef __linux__
          if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != owner_pid)
            _exit(2);
#endif
          pollfd pfd{listener, POLLIN, 0};
          int    ready;
          do {
            ready = poll(&pfd, 1, 15000);
          } while (ready < 0 && errno == EINTR);
          int channel = ready > 0 ? accept(listener, nullptr, nullptr) : -1;
          close(listener);
          unlink(endpoint.c_str());
          if (channel < 0)
            _exit(2);
#ifdef SO_NOSIGPIPE
          int one = 1;
          if (setsockopt(channel, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0)
            _exit(2);
#endif
          try {
            int status = serve_frozen_owner(channel, owner_pid, v.optimizer_identity);
            _exit(status);
          } catch (Native_gp_resume& continuation) {
            child   = true;
            resumed = std::move(continuation);
            return std::move(resumed->patch);
          }
        }
        retained.insert(pid);
        close(listener);
        send_message(fd, "retained", {});
        continue;
      }
      if (op != "fork_gp") {
        send_message(fd, "error", "unknown native GP operation");
        continue;
      }
      std::string        dir;
      uint64_t           expected = 0;
      dp::InteractUpdate patch;
      wire::unpack(data, expected, dir, target, patch);
      if ((hooks & pulls) || !expected || expected != v.optimizer_identity || v.finishing || target < 0
          || (target && target <= v.iteration) || patch.extend) {
        send_message(fd, "error", "stale baseline or invalid native GP stop");
        continue;
      }
      if (!single_thread()) {
        throw std::runtime_error("native GP owner lost its single-thread invariant");
      }
      const pid_t pid = fork();
      if (pid < 0) {
        send_message(fd, "error", "could not fork native GP");
        continue;
      }
      if (!pid) {
        child = true;
        close(fd);
        setpgid(0, 0);
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != owner_pid) {
          _exit(2);
        }
#else
        process_group::watch_parent(owner_pid);
#endif
        result_dir = std::move(dir);
        return patch;
      }
      setpgid(pid, pid);
      children.insert(pid);
      send_message(fd, "started", wire::pack(pid));
    }
  };
  try {
    const auto result = dp::place(design, o);
    if (child) {
      _exit(2);
    }  // No callback reached its declared stop.
    send_message(
        fd,
        "done",
        hosted ? wire::pack(result, design, !retained.empty()) : wire::pack(result.iterations, design.node_x, design.node_y));
    while (!children.empty()) {
      auto [op, data] = receive_message(fd);
      if (op == "reap") {
        reap(data);
      } else {
        send_message(fd, "error", "native GP has finished");
      }
    }
    if (hosted) {
      reap_retained(true);
    }
  } catch (const std::exception& e) {
    if (resumed) {
      resumed->fail(e.what());
      _exit(2);
    }
    if (child) {
      std::ofstream(result_dir / "error.txt") << e.what();
      _exit(2);
    }
    throw;
  }
}
std::optional<dp::Result> run_native_gp(const std::string& executable, dp::Design& design, const dp::Options& host,
                                        std::string& why, const std::function<bool()>& idle_pulls) {
  why = unsupported(host);
  if (!why.empty()) {
    return {};
  }
  if (!host.interact) {
    why = "no GUI interaction callback";
    return {};
  }
  dp::Options recipe = host;
  unsigned    hooks  = 0;
  const auto  take   = [&](auto& cb, unsigned bit) {
    if (cb) {
      hooks |= bit;
      cb     = {};
    }
  };
  recipe.interact = {};
  take(recipe.observer, observe);
  take(recipe.progress_observer, progress);
  take(recipe.before_legalize, before);
  take(recipe.after_macro_legalize, after);
  take(recipe.macro_pull_update, pulls);
  if ((hooks & pulls) && idle_pulls && idle_pulls()) {
    hooks |= check_idle_pulls;
  }
  if (const auto init = recipe.route_initialize.target<void (*)()>(); init && *init == initialize_flute_tables) {
    take(recipe.route_initialize, flute);
  }
  // Name any remaining host dependency so a recipe that has to replay is diagnosable.
  std::string opaque;
  if (recipe.macro_merge_check) {
    opaque += " macro_merge_check";
  }
  if (recipe.magnet_runtime) {
    opaque += " magnet_runtime";
  }
  if (recipe.macro_motion_observer) {
    opaque += " macro_motion_observer";
  }
  if (recipe.macro_step_observer) {
    opaque += " macro_step_observer";
  }
  if (recipe.trial_timing) {
    opaque += " trial_timing";
  }
  if (recipe.trial_observer) {
    opaque += " trial_observer";
  }
  if (recipe.macro_density_margin) {
    opaque += " macro_density_margin";
  }
  if (recipe.terminal_update) {
    opaque += " terminal_update";
  }
  if (recipe.movable_update) {
    opaque += " movable_update";
  }
  if (recipe.checkpoint_observer) {
    opaque += " checkpoint_observer";
  }
  if (recipe.checkpoint) {
    opaque += " checkpoint";
  }
  if (recipe.route_external) {
    opaque += " route_external";
  }
  if (recipe.route_physical_snapshot) {
    opaque += " route_physical_snapshot";
  }
  if (recipe.route_initialize) {
    opaque += " route_initialize";
  }
  if (!opaque.empty()) {
    why = "native GP requires host hooks:" + opaque;
    return {};
  }
  std::string inputs;
  try {
    inputs = wire::pack(design, recipe, hooks);
  } catch (const std::invalid_argument& e) {
    why = e.what();
    return {};
  }
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) {
    throw std::system_error(errno, std::generic_category(), "native GP channel");
  }
  struct Channel {
    int   local, remote;
    pid_t pid = -1;
    ~Channel() {
      if (local >= 0) {
        close(local);
      }
      if (remote >= 0) {
        close(remote);
      }
      if (pid > 0) {
        kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
      }
    }
  } ch{pair[0], pair[1]};
#ifdef SO_NOSIGPIPE
  int one = 1;
  for (int fd : pair) {
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
  }
#endif
  for (int fd : pair) {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
      throw std::system_error(errno, std::generic_category(), "native GP close-on-exec");
    }
  }
  const int                  link   = std::max(pair[0], pair[1]) + 1;
  std::string                number = std::to_string(link), parent = std::to_string(getpid());
  char*                      argv[] = {const_cast<char*>(executable.c_str()),
                                       const_cast<char*>("--placement-snapshot-owner"),
                                       number.data(),
                                       parent.data(),
                                       nullptr};
  posix_spawn_file_actions_t actions;
  int                        rc = posix_spawn_file_actions_init(&actions);
  if (rc) {
    throw std::system_error(rc, std::generic_category(), "initialize native GP spawn");
  }
  rc        = posix_spawn_file_actions_adddup2(&actions, ch.remote, link);
  pid_t pid = -1;
  if (!rc) {
    rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv, environ);
  }
  posix_spawn_file_actions_destroy(&actions);
  if (rc) {
    throw std::system_error(rc, std::generic_category(), "start native GP");
  }
  ch.pid = pid;
  close(ch.remote);
  ch.remote = -1;
  send_message(ch.local, "native_primary", inputs);
  std::string{}.swap(inputs);
  bool has_retained = false;
  for (;;) {
    auto [op, bytes] = receive_message(ch.local);
    std::string reply;
    if (op == "host_frame") {
      std::string                     sample;
      bool                            first, changed_pulls, has_centroids;
      std::vector<dp::CentroidSample> centroids;
      wire::unpack(bytes, sample, first, changed_pulls, has_centroids, centroids);
      std::vector<double> x, y;
      dp::InteractView    v;
      wire::unpack(sample, v.optimizer_identity, v.iteration, v.finishing, x, y);
      if (x.size() != design.node_x.size() || x.size() != y.size()) {
        throw std::runtime_error("native GP frame topology changed");
      }
      v.design    = &design;
      v.x         = x.data();
      v.y         = y.data();
      v.count     = int(x.size());
      v.first     = first;
      v.centroids = has_centroids ? &centroids : nullptr;
      Poll_channel scope(!first && !v.finishing && !changed_pulls ? ch.local : -1,
                         v.optimizer_identity,
                         (hooks & pulls) ? &idle_pulls : nullptr);
      send_message(ch.local, "advance", wire::pack(host.interact(v)));
      continue;
    } else if (op == "observe") {
      dp::Design         physical;
      dp::PlacementStage stage;
      int                iter;
      wire::unpack(bytes, physical, stage, iter);
      host.observer(physical, stage, iter);
    } else if (op == "progress") {
      host.progress_observer(wire::read<dp::GpSample>(bytes));
    } else if (op == "before" || op == "after") {
      auto physical = wire::read<dp::Design>(bytes);
      (op == "before" ? host.before_legalize : host.after_macro_legalize)(physical);
      reply = wire::pack(physical);
    } else if (op == "idle_pulls") {
      reply = wire::pack(idle_pulls && idle_pulls());
    } else if (op == "pulls") {
      dp::Design physical;
      int        iter;
      wire::unpack(bytes, physical, iter);
      reply = wire::pack(host.macro_pull_update(physical, iter));
    } else if (op == "done") {
      dp::Result result;
      wire::unpack(bytes, result, design, has_retained);
      if (has_retained) {
        // The controller can hold a candidate after main advances. The primary
        // remains in the session process tree until its retained owners are reaped.
        std::thread([pid = ch.pid] {
          while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
          }
        }).detach();
        ch.pid = -1;
        return result;
      }
      int   status;
      pid_t found;
      do {
        found = waitpid(ch.pid, &status, 0);
      } while (found < 0 && errno == EINTR);
      if (found != ch.pid) {
        throw std::system_error(errno, std::generic_category(), "wait for native GP");
      }
      ch.pid = -1;
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("native GP owner exited without completing");
      }
      return result;
    } else {
      throw std::runtime_error(op == "error" ? bytes : "unknown native GP host callback: " + op);
    }
    send_message(ch.local, "reply", reply);
  }
}
}  // namespace livehd::placement::gui
