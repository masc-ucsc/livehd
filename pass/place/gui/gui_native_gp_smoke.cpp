// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Compare two actual forks inside a serial native solve with independent uninterrupted solves.
// In particular iteration 20 still has density/stopping work to do after the interactive callback.
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "gui_gp_options.hpp"
#include "gui_ipc.hpp"
#include "gui_native_wire.hpp"
#include "gui_wire.hpp"

extern char** environ;
namespace dp   = DreamPlace::native;
namespace gui  = livehd::placement::gui;
namespace wire = gui::wire;
namespace fs   = std::filesystem;
static void require(bool b, const char* why) {
  if (!b) {
    throw std::runtime_error(why);
  }
}
static dp::Design fixture() {
  dp::Design d;
  d.layout      = {0, 0, 80, 40, 1, 1};
  d.num_movable = 4;
  d.node_x      = {10, 20, 30, 40, 0, 80};
  d.node_y      = {10, 20, 10, 20, 20, 20};
  d.node_size_x = {2, 2, 2, 8, 0, 0};
  d.node_size_y = {1, 1, 1, 6, 0, 0};
  d.node_halo_x = d.node_halo_y = {0, 0, 0, 1, 0, 0};
  d.node_macro                  = {0, 0, 0, 1, 0, 0};
  d.pin2node                    = {4, 0, 0, 1, 1, 2, 2, 3, 3, 5};
  d.pin_offset_x.assign(10, 0);
  d.pin_offset_y.assign(10, 0);
  d.flat_net2pin       = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  d.flat_net2pin_start = {0, 2, 4, 6, 8, 10};
  d.pin_timing.emplace();
  d.pin_timing->driver = {-1, 0, -1, 2, -1, 4, -1, 6, -1, 8};
  d.pin_timing->weight = {0, 2, 0, 1, 0, 1, 0, 2, 0, 1};
  d.build_maps();
  return d;
}
static constexpr int      source = 20, stop = 60;
static dp::InteractUpdate edit(const dp::Design& d) {
  dp::InteractUpdate u;
  int                node = 0;
  while (node + 1 < d.num_movable && (d.node_macro.empty() || !d.node_macro[node])) {
    ++node;
  }
  u.pins.push_back({node,
                    d.layout.xl + .36 * (d.layout.xh - d.layout.xl - d.node_size_x.at(node)),
                    d.layout.yl + .31 * (d.layout.yh - d.layout.yl - d.node_size_y.at(node)),
                    0});
  return u;
}
struct Reference {
  std::map<int, std::string> regular;
  std::string                end;
  std::vector<double>        x, y;
};
static Reference reference(dp::Design d, dp::Options o, bool patch) {
  Reference ref;
  o.interact = [&](const dp::InteractView& v) {
    auto frame = wire::pack(v.optimizer_identity,
                            v.iteration,
                            v.finishing,
                            std::span(v.x, size_t(v.count)),
                            std::span(v.y, size_t(v.count)));
    if (v.finishing) {
      ref.end = std::move(frame);
    } else {
      ref.regular.emplace(v.iteration, std::move(frame));
    }
    if (!v.finishing && v.iteration == source && patch) {
      return edit(d);
    }
    return dp::InteractUpdate{};
  };
  auto result = dp::place(d, o);
  require(result.filler_count > 0 && result.iterations == stop, "fixture did not exercise private fillers and full horizon");
  ref.x = d.node_x;
  ref.y = d.node_y;
  return ref;
}
static dp::Design physical_input(const char* path) {
  // Optional standalone numeric interchange for a separate integration pilot. No external test dependency.
  std::ifstream f(path);
  dp::Design    d;
  int           n, nets;
  f >> n >> d.num_movable >> nets >> d.layout.xl >> d.layout.yl >> d.layout.xh >> d.layout.yh >> d.layout.site_width
      >> d.layout.row_height;
  if (!f || n < 4 || nets < 1) {
    throw std::runtime_error("invalid numeric design header");
  }
  for (int i = 0; i < n; ++i) {
    double x, y, w, h;
    int    macro;
    f >> x >> y >> w >> h >> macro;
    d.node_x.push_back(x);
    d.node_y.push_back(y);
    d.node_size_x.push_back(w);
    d.node_size_y.push_back(h);
    d.node_macro.push_back(macro);
  }
  for (int i = 0; i < nets; ++i) {
    int pins;
    f >> pins;
    for (int j = 0; j < pins; ++j) {
      int    node;
      double dx, dy;
      f >> node >> dx >> dy;
      d.flat_net2pin.push_back(int(d.pin2node.size()));
      d.pin2node.push_back(node);
      d.pin_offset_x.push_back(dx);
      d.pin_offset_y.push_back(dy);
    }
    d.flat_net2pin_start.push_back(int(d.pin2node.size()));
  }
  if (!f) {
    throw std::runtime_error("truncated numeric design");
  }
  d.build_maps();
  d.validate();
  return d;
}
int main(int argc, char** argv) {
  try {
    require(argc == 2 || argc == 3, "need path to lhd executable and optional numeric design");
    auto        d = argc == 3 ? physical_input(argv[2]) : fixture();
    dp::Options o;
    o.max_iterations = o.schedule_iterations = o.min_iterations = stop;
    o.num_threads                                               = 1;
    o.bins_x = o.bins_y           = 16;
    o.interact_interval           = 10;
    o.interact_optimizer_identity = true;
    o.legalize = o.detailed_place  = false;
    o.add_fillers                  = true;
    o.stop_overflow                = 0;
    o.wirelength_gamma_start       = .83;
    o.adaptive_density             = true;
    o.schedule_iterations         += 5;
    auto opaque                    = o;
    opaque.macro_density_margin    = [](int) { return .1; };
    bool refused                   = false;
    try {
      (void)wire::pack(opaque);
    } catch (const std::invalid_argument&) {
      refused = true;
    }
    require(refused, "serialized a live host callback");
    // Invalid owner wire data must never silently drop the pin-timing objective.
    require(wire::read<dp::Design>(wire::pack(d)).pin_timing == d.pin_timing, "numeric handoff lost pin timing");
    const auto clean = reference(d, o, false), patched = reference(d, o, true);
    require(clean.regular.at(source) == patched.regular.at(source) && clean.end != patched.end, "ineffective branch witness");
    int pair[2];
    require(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair), "socketpair failed");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int targetfd = std::max(pair[0], pair[1]) + 1;
    posix_spawn_file_actions_adddup2(&actions, pair[1], targetfd);
    std::string fdarg = std::to_string(targetfd), parentarg = std::to_string(getpid());
    char*       args[] = {argv[1], const_cast<char*>("--placement-snapshot-owner"), fdarg.data(), parentarg.data(), nullptr};
    pid_t       pid;
    require(!posix_spawn(&pid, args[0], &actions, nullptr, args, environ), "exec clean LHD failed");
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    const int fd = pair[0];
    gui::send_message(fd, "native_gp", wire::pack(d, o));
    const fs::path tmp     = std::getenv("TEST_TMPDIR") ? std::getenv("TEST_TMPDIR") : fs::temp_directory_path();
    std::string    scratch = (tmp / "native-gp-XXXXXX").string();
    require(mkdtemp(scratch.data()) != nullptr, "mkdtemp failed");
    std::map<pid_t, std::pair<fs::path, std::string>> branches;
    std::map<int, std::string>                        observed;
    for (;;) {
      auto [op, data] = gui::receive_message(fd);
      if (op == "done") {
        int                 steps;
        std::vector<double> x, y;
        wire::unpack(data, steps, x, y);
        require(steps == stop && x == clean.x && y == clean.y, "children changed final main placement");
        break;
      }
      if (op != "frame") {
        throw std::runtime_error(op + ": " + data);
      }
      uint64_t            id;
      int                 iteration;
      bool                finishing;
      std::vector<double> x, y;
      wire::unpack(data, id, iteration, finishing, x, y);
      require(id && (finishing ? clean.end : clean.regular.at(iteration)) == data,
              "main checkpoint differs from uninterrupted reference");
      if (!finishing) {
        observed[iteration] = data;
      }
      if (!finishing && iteration == source) {
        gui::send_message(fd, "fork_gp", wire::pack(id ^ 1, scratch, 0, dp::InteractUpdate{}));
        require(gui::receive_message(fd).first == "error", "accepted stale optimizer identity");
        for (int i = 0; i < 3; ++i) {
          const auto dir = fs::path(scratch) / std::to_string(i);
          fs::create_directory(dir);
          const int target = i == 1 ? source + 10 : 0;
          gui::send_message(fd, "fork_gp", wire::pack(id, dir.string(), target, i ? edit(d) : dp::InteractUpdate{}));
          auto [reply, body] = gui::receive_message(fd);
          require(reply == "started", "could not fork from native callback");
          branches.emplace(wire::read<pid_t>(body),
                           std::pair{dir,
                                     target ? patched.regular.at(target)
                                     : i    ? patched.end
                                            : clean.end});
        }
      }
      gui::send_message(fd, "advance", wire::pack(dp::InteractUpdate{}));
    }
    require(branches.size() == 3 && observed == clean.regular, "incomplete callback comparison");
    for (auto& [p, result] : branches) {
      for (;;) {
        gui::send_message(fd, "reap", wire::pack(p));
        auto [op, data] = gui::receive_message(fd);
        require(op == "reap", "reap failed");
        bool stopped, ok;
        wire::unpack(data, stopped, ok);
        if (stopped) {
          require(ok, "native child failed");
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      std::ifstream f(result.first / "native.bin", std::ios::binary);
      std::string   bytes{std::istreambuf_iterator<char>(f), {}};
      require(bytes == result.second, "private native continuation differs from independently patched reference");
    }
    close(fd);
    int status;
    require(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "owner failed");
    fs::remove_all(scratch);
    std::cout << "Matched all " << observed.size() << " main GP polls and final physical coordinates, the unchanged branch, "
              << "and independently patched next-poll and gp_end branches. Source iteration " << source << ", final " << stop
              << ", nodes " << d.node_x.size() << ", nets " << d.flat_net2pin_start.size() - 1 << ".\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
