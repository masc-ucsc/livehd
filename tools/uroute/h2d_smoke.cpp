// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "dreamplace/native/macro/h2d.hpp"
#include "dreamplace/native/magnet.hpp"
#include "dreamplace/native/rudy.hpp"
#include "dreamplace/native/uroute_weights.hpp"

namespace dp = DreamPlace::native;
namespace mp = DreamPlace::native::macro;
int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      throw std::invalid_argument("usage: h2d_smoke weights features labels");
    }
    const auto check = [](bool ok, const char* message) {
      if (!ok) {
        throw std::runtime_error(message);
      }
    };
    auto       weights  = std::make_shared<const dp::UrouteWeights>(argv[1]);
    auto       features = dp::read_uroute_maps(argv[2]), labels = dp::read_uroute_maps(argv[3]);
    dp::Design d;
    d.layout      = {0, 0, 120, 40, 1, 1};
    d.num_movable = 6;
    d.node_x      = {10, 50, 90, 24, 64, 104, 0, 120};
    d.node_y      = {16, 16, 16, 20, 20, 20, 20, 20};
    d.node_size_x = {10, 10, 10, 2, 2, 2, 0, 0};
    d.node_size_y = {8, 8, 8, 1, 1, 1, 0, 0};
    d.node_macro  = {1, 1, 1, 0, 0, 0, 0, 0};
    std::vector<mp::NodeInfo> info;
    for (int i = 0; i < 3; ++i) {
      info.push_back({"s" + std::to_string(i) + ".ram", "s" + std::to_string(i), "ram", true});
    }
    for (int i = 0; i < 3; ++i) {
      info.push_back({"s" + std::to_string(i) + ".logic", "s" + std::to_string(i), "buf"});
    }
    info.push_back({"@io:in", {}, {}, false, false, true});
    info.push_back({"@io:out", {}, {}, false, false, true});
    const int chain[] = {6, 0, 3, 1, 4, 2, 5, 7};
    for (int n = 0; n < 7; ++n) {
      for (int k = 0; k < 2; ++k) {
        const int node = chain[n + k];
        d.flat_net2pin.push_back(d.pin2node.size());
        d.pin2node.push_back(node);
        d.pin_offset_x.push_back(k ? 0 : d.node_size_x[node]);
        d.pin_offset_y.push_back(d.node_size_y[node] / 2);
      }
      d.flat_net2pin_start.push_back(d.pin2node.size());
    }
    d.build_maps();
    mp::Parameters p;
    p.K               = 1;
    p.iters_per_outer = 5;
    p.seed_sa_evals = p.evals = 8;
    p.bins                    = 8;
    p.routability             = true;
    p.route_cap_h = p.route_cap_v = 100;
    dp::Options cells;
    cells.max_iterations = 100;
    cells.min_iterations = 20;
    cells.stop_overflow  = .5;
    cells.bins_x = cells.bins_y  = 8;
    cells.route.model            = dp::RouteModel::rsmt;
    cells.route.probe            = true;
    cells.route.probe_model      = dp::RouteModel::uroute;
    cells.route.probe_iterations = 20;
    cells.uroute                 = std::make_shared<const dp::UrouteContext>(weights,
                                                                             std::vector<dp::UrouteSupport>{
                                                                                 {"h2d-fixture", features, labels}
    });
    int probes                   = 0;
    cells.route_probe_observer   = [&](const dp::Design& snapshot, int, int) {
      ++probes;
      check(snapshot.node_x.size() == 8 && snapshot.pin2node.size() == 14, "H2D probed abstract geometry");
      check(std::count(snapshot.node_macro.begin(), snapshot.node_macro.end(), 1) == 3, "H2D probe lost physical macros");
    };
    const auto result = mp::place_h2d(d, info, p, {}, cells);
    check(result.legal && result.placement.route_probed && probes == 1, "H2D physical probe did not run exactly once");
    check(result.placement.route_model == dp::RouteModel::uroute && result.placement.route_probe_overflow > 0,
          "H2D did not retain failed URoute probe feedback");
    check(result.placement.uroute_sample_ids == std::vector<std::string>{"h2d-fixture"}, "H2D lost selected support IDs");
    dp::RudyOptions route{8, 8, 100, 100};
    route.model = dp::RudyModel::rsmt;
    check(std::abs(result.rudy_overflow - dp::rudy(d, route).overflow) < 1e-12, "H2D macro scoring consumed learned pressure");
    dp::Design pair;
    pair.layout      = {0, 0, 200, 200, 1, 1};
    pair.num_movable = 2;
    pair.node_x      = {20, 34};
    pair.node_y      = {20, 20};
    pair.node_size_x = pair.node_size_y = {10, 10};
    pair.node_halo_x = pair.node_halo_y = {2, 2};
    pair.node_macro                     = {1, 1};
    pair.build_maps();
    dp::Options magnet;
    magnet.magnet.enabled       = true;
    magnet.magnet.settle_window = 4;
    magnet.max_iterations       = 120;
    magnet.min_iterations       = 60;
    magnet.bins_x = magnet.bins_y = 8;
    magnet.target_density         = .9;
    magnet.density_upper_bound    = true;
    magnet.add_fillers = magnet.detailed_place = false;
    magnet.macro_only_legalize                 = true;
    magnet.routability                         = true;
    magnet.route.model                         = dp::RouteModel::automatic;
    magnet.route.uroute_ladder                 = true;
    magnet.route.hold = magnet.route.uroute_hold = 1;
    magnet.uroute                                = cells.uroute;
    bool saw_merged_members                      = false;
    magnet.route_physical_snapshot               = [&](const dp::Design& snapshot) {
      check(snapshot.node_x.size() == 2 && snapshot.node_macro == std::vector<std::uint8_t>{1, 1},
            "rigid macro routing saw a contracted or halo-only footprint");
      saw_merged_members
          |= std::any_of(snapshot.node_group_id.begin(), snapshot.node_group_id.end(), [](auto id) { return id != 0; });
      return snapshot;
    };
    const auto merged = dp::place(pair, magnet);
    check(merged.magnet && merged.magnet->mapping.groups.size() == 1 && saw_merged_members,
          "rigid macro test did not measure expanded merged members");
    check(merged.route_model == dp::RouteModel::uroute && merged.route_uroute_switch_iteration > 0,
          "rigid macro restart lost the learned latch");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
