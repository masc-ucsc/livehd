// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_ipc.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>
#include <thread>
#include <utility>

#include "core/job_manager.hpp"
#include "gui_wire.hpp"
#include "gui_worker.hpp"

extern char** environ;
namespace livehd::placement::gui {
using namespace wire;
namespace {
void sys_error(const char* why) { throw std::system_error(errno, std::generic_category(), why); }
void transfer(int fd, void* buffer, size_t length, bool sending) {
  auto* p = static_cast<char*>(buffer);
  while (length) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto n = sending ? ::send(fd, p, length, flags) : ::recv(fd, p, length, 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      sys_error("GUI controller channel");
    }
    if (n == 0) {
      throw std::runtime_error("GUI controller channel closed");
    }
    p      += n;
    length -= size_t(n);
  }
}
}  // namespace
void send_message(int fd, const std::string& op, const std::string& data) {
  uint64_t sizes[]{op.size(), data.size()};
  transfer(fd, sizes, sizeof(sizes), true);
  transfer(fd, const_cast<char*>(op.data()), op.size(), true);
  transfer(fd, const_cast<char*>(data.data()), data.size(), true);
}
std::pair<std::string, std::string> receive_message(int fd) {
  uint64_t sizes[2]{};
  transfer(fd, sizes, sizeof(sizes), false);
  if (sizes[0] > 80 || sizes[1] > (uint64_t{1} << 34)) {
    throw std::runtime_error("invalid GUI controller header");
  }
  std::string op(sizes[0], '\0'), data(sizes[1], '\0');
  transfer(fd, op.data(), op.size(), false);
  transfer(fd, data.data(), data.size(), false);
  return {std::move(op), std::move(data)};
}
namespace {
struct Controller {
  int         fd;
  Session*    session = nullptr;
  std::string callback(const std::string& op, const std::string& data) const {
    send_message(fd, op, data);
    auto [reply, body] = receive_message(fd);
    if (reply != "done") {
      throw std::runtime_error(body);
    }
    return body;
  }
  std::string dispatch(const std::string& op, const std::string& data) {
    auto& s = *session;
    if (op == "scene") {
      auto                scene = std::make_shared<Scene>();
      std::vector<double> x, y;
      bool                mark;
      unpack(data, *scene, x, y, mark);
      s.begin_phase(scene, x.data(), y.data(), x.size(), mark);
    } else if (op == "gate" || op == "poll" || op == "gp_end" || op == "edit") {
      std::vector<double> x, y;
      Stats               stats;
      std::string         stage;
      bool                has_map, has_timing, has_fork, has_legalizer, has_intent, has_orientation;
      unpack(data, stage, x, y, stats, has_map, has_timing, has_fork, has_legalizer, has_intent, has_orientation);
      const Map_provider maps
          = [&](const std::string& kind, const Frame& frame) { return read<Grid>(callback("map", pack(kind, frame.x, frame.y))); };
      const Timing_provider timing = [&](Timing_query& q) {
        const auto* frame = q.frame;
        bool        ok;
        unpack(callback("timing", pack(q, frame->x, frame->y, frame->stats.optimizer_identity)), ok, q);
        return ok;
      };
      s.set_fork_builder(has_fork ? Session::Fork_builder([&](const Fork_request& r) {
        return read<Fork_files>(callback("build_fork", pack(r.dir, r.box, r.cells, r.frame->x, r.frame->y)));
      })
                                  : Session::Fork_builder{});
      s.set_legalizer(has_legalizer ? Legalizer([&](const Legalize_request& req) {
        return read<Legalize_result>(callback("legalize", pack(req)));
      })
                                    : Legalizer{});
      s.set_orientation_provider(
          has_orientation ? Orientation_provider([&](const Orientation_edits& flips) { callback("orient", pack(flips)); })
                          : Orientation_provider{});
      s.set_intent_provider(has_intent ? Intent_provider([&](const std::string& records, const Frame& frame, bool apply) {
        return callback("intent", pack(records, frame.x, frame.y, apply));
      })
                                       : Intent_provider{});
      const auto* mp = has_map ? &maps : nullptr;
      const auto* tp = has_timing ? &timing : nullptr;
      if (op == "gate") {
        return pack(s.gate(stage, x.data(), y.data(), x.size(), stats, mp, tp));
      }
      if (op == "poll") {
        return pack(s.poll(stats.iteration, x.data(), y.data(), x.size(), stats, mp, tp));
      }
      if (op == "gp_end") {
        auto r = s.gp_end(x.data(), y.data(), x.size(), stats, mp, tp);
        return pack(r.edits, r.extend);
      }
      auto r = s.edit_wait(x.data(), y.data(), x.size(), stats, mp, tp);
      return pack(r.edits, r.again);
    } else if (op == "policy") {
      return pack(s.policy());
    } else if (op == "begin_repair") {
      s.begin_terminal_repair(read<Mutation_policy>(data));
    } else if (op == "solver_timing_call") {
      s.solver_timing_call(read<bool>(data));
    } else if (op == "repair_trials") {
      unsigned trials;
      std::vector<std::string> changed;
      unpack(data, trials, changed);
      s.set_repair_trials(trials, changed);
    } else if (op == "selected_names") {
      return pack(s.selected_names());
    } else if (op == "get_intent") {
      return s.intent();
    } else if (op == "options") {
      s.set_options_json(data);
    } else if (op == "note") {
      s.set_note(data);
    } else if (op == "stages") {
      s.set_stages(read<std::vector<Stage_switch>>(data));
    } else if (op == "period") {
      s.set_clock_period_ps(read<double>(data));
    } else if (op == "set_timing") {
      double wns, tns;
      int    iter;
      unpack(data, wns, tns, iter);
      s.set_timing(wns, tns, iter);
    } else if (op == "pins" || op == "legalization_pins") {
      return pack(s.all_pins(op == "legalization_pins"));
    } else if (op == "anchors") {
      return pack(s.pin_positions());
    } else if (op == "macro_moves") {
      return pack(s.macro_moves());
    } else if (op == "placement") {
      return pack(s.placement_override());
    } else if (op == "set_placement") {
      s.set_placement(read<decltype(s.placement_override())>(data));
    } else if (op == "recorded_edits") {
      return pack(s.recorded_edits());
    } else if (op == "record_edits") {
      s.record_edits(read<decltype(s.recorded_edits())>(data));
    } else if (op == "edit_left") {
      return pack(s.edit_left());
    } else if (op == "submit") {
      uint64_t sequence = 0;
      const auto error = s.submit(data, &sequence);
      return pack(error, sequence);
    } else if (op == "result") {
      s.set_result(read<std::vector<std::tuple<std::string, double, double>>>(data));
    } else if (op == "finish") {
      s.wait_finish();
    } else if (op == "fail") {
      std::string title, detail;
      unpack(data, title, detail);
      s.fail(title, detail);
    } else {
      throw std::runtime_error("unknown evaluator operation " + op);
    }
    return {};
  }
};
}  // namespace
Controller_link::Controller_link(const Config& cfg) : config_(cfg) {
  int pair[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) {
    sys_error("GUI socketpair");
  }
  for (const auto fd : pair) {
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  // dup2 in the spawned image clears close-on-exec. A fixed private fd also
  // avoids passing an inheritable descriptor through the evaluator runtime.
  const int table = Job_manager::global(-1, true).shared_fd();
  if (table < 0) {
    posix_spawn_file_actions_destroy(&actions);
    ::close(pair[0]);
    ::close(pair[1]);
    throw std::runtime_error("placement controller requires a shared session admission table");
  }
  const int child_fd    = std::max({table, pair[0], pair[1]}) + 1;
  const int child_table = child_fd + 1;
  posix_spawn_file_actions_adddup2(&actions, table, child_table);
  std::string admission = std::to_string(child_table);
  posix_spawn_file_actions_adddup2(&actions, pair[1], child_fd);
  std::string descriptor = std::to_string(child_fd);
  char*       argv[]     = {const_cast<char*>(cfg.controller_executable.c_str()),
                            const_cast<char*>("--placement-controller"),
                            descriptor.data(),
                            admission.data(),
                            nullptr};
  const int   rc         = posix_spawnp(&pid_, argv[0], &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(pair[1]);
  if (rc) {
    ::close(pair[0]);
    throw std::system_error(rc, std::generic_category(), "spawn placement controller");
  }
  socket_ = pair[0];
  try {
    unpack(call("start", pack(cfg)), url, token, port);
  } catch (...) {
    ::close(socket_);
    while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
    throw;
  }
}
Controller_link::~Controller_link() {
  ::close(socket_);
  while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
  }
}

std::string Controller_link::call(const std::string& operation, std::string payload, const Map_provider* maps,
                                  const Timing_provider* timing, const Session::Fork_builder* builder, const Legalizer* legalizer,
                                  const Intent_provider* intent, const Orientation_provider* orientation) const {
  send_message(socket_, operation, payload);
  for (;;) {
    auto [op, data] = receive_message(socket_);
    if (op == "done") {
      return data;
    }
    if (op == "error") {
      throw std::runtime_error(data);
    }
    try {
      std::string result;
      if (op == "map" || op == "timing") {
        Frame frame;
        if (op == "map" && maps) {
          std::string kind;
          unpack(data, kind, frame.x, frame.y);
          result = pack((*maps)(kind, frame));
        } else if (op == "timing" && timing) {
          Timing_query q;
          unpack(data, q, frame.x, frame.y, frame.stats.optimizer_identity);
          q.frame = &frame;
          bool ok = (*timing)(q);
          result  = pack(ok, q);
        } else {
          throw std::runtime_error("missing evaluator provider");
        }
      } else if (op == "orient" && orientation && *orientation) {
        (*orientation)(read<Orientation_edits>(data));
      } else if (op == "intent" && intent && *intent) {
        std::string records;
        Frame       frame;
        bool        apply;
        unpack(data, records, frame.x, frame.y, apply);
        result = (*intent)(records, frame, apply);
      } else if (op == "legalize" && legalizer && *legalizer) {
        result = pack((*legalizer)(read<Legalize_request>(data)));
      } else if (op == "restart") {
        result = config_.restart(read<Restart_request>(data));
      } else if (op == "build_fork" && builder) {
        Fork_request req;
        auto         frame = std::make_shared<Frame>();
        unpack(data, req.dir, req.box, req.cells, frame->x, frame->y);
        req.frame = frame;
        req.scene = scene;
        result    = pack((*builder)(req));
      } else {
        throw std::runtime_error("unknown controller callback " + op);
      }
      send_message(socket_, "done", result);
    } catch (const std::exception& e) {
      send_message(socket_, "error", e.what());
    }
  }
}

int serve_controller(int fd, int admission_fd, Job_manager::Probe probe) {
  Job_manager::global(admission_fd);
  ::fcntl(fd, F_SETFD, FD_CLOEXEC);
  try {
    auto [op, body] = receive_message(fd);
    if (op != "start") {
      return 1;
    }
    auto cfg = read<Config>(body);
    if (!cfg.worker_args.empty() && cfg.target == "main") {
      if (probe) {
        Job_manager::global().exchange_probe(std::move(probe));
      }
      clean_exploration_roots(std::filesystem::path(cfg.agent_socket).parent_path());
    }
    Controller controller{fd};
    cfg.restart = [&](const Restart_request& r) {
      auto restart        = r;
      restart.listen_port = controller.session->port();
      restart.token       = controller.session->token();
      controller.session->suspend_listeners();
      const auto  why = controller.callback("restart", pack(restart));  // successful exec closes this channel
      std::string bind_error;
      if (!controller.session->start(&bind_error)) {
        throw std::runtime_error(why + ": " + bind_error);
      }
      return why;
    };
    Session session(std::move(cfg));
    controller.session = &session;
    std::string error;
    if (!session.start(&error)) {
      send_message(fd, "error", error);
      return 1;
    }
    send_message(fd, "done", pack(session.url(), session.token(), session.port()));
    // The channel remains open across all gates. EOF must release a gate even
    // if no client ever sends another request (including SIGKILL of main).
    std::jthread watcher([&](std::stop_token stop) {
      pollfd peer{fd, 0, 0};
      while (!stop.stop_requested()) {
        if (::poll(&peer, 1, 100) > 0 && (peer.revents & (POLLHUP | POLLERR))) {
          session.shutdown();
          return;
        }
      }
    });
    for (;;) {
      auto [command, data] = receive_message(fd);
      try {
        send_message(fd, "done", controller.dispatch(command, data));
      } catch (const std::exception& e) {
        send_message(fd, "error", e.what());
      }
    }
  } catch (const std::exception&) {
    return 0;
  }  // EOF on normal shutdown or evaluator death
}
}  // namespace livehd::placement::gui
