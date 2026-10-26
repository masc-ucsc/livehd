// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Read-only native RUDY evaluation of an extracted physical pin snapshot.
// Input: xl yl xh yh site_width row_height capacity_h capacity_v bins;
// node_count, then x y width height is_macro blocked_h blocked_v per node;
// net_count, then pin_count followed by node_id absolute_x absolute_y per pin.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>

#include "dreamplace/native/placement.hpp"
#include "dreamplace/native/rudy.hpp"
#include "dreamplace/native/uroute_weights.hpp"
#include "pass/place/steiner_topology.hpp"

namespace dp = DreamPlace::native;

int main(int argc, char** argv) {
  try {
    const bool learned = argc >= 7 && std::string(argv[3]) == "uroute" && (argc - 5) % 2 == 0 && (argc - 5) / 2 <= 16;
    if (argc != 3 && argc != 4 && !learned) {
      throw std::invalid_argument(
          "usage: rudy_metrics physical_snapshot.txt output_prefix [rudy|rsmt] OR uroute weights.bin features.bin labels.bin "
          "[features.bin labels.bin ...] (1..16 supports)");
    }
    std::ifstream   input(argv[1]);
    dp::Design      d;
    dp::RudyOptions options;
    if (argc == 4) {
      const std::string model = argv[3];
      if (model != "rudy" && model != "rsmt") {
        throw std::invalid_argument("unknown route model");
      }
      if (model == "rsmt") {
        options.model = dp::RudyModel::rsmt;
      }
    }
    options.initialize = livehd::placement::initialize_flute_tables;
    input >> d.layout.xl >> d.layout.yl >> d.layout.xh >> d.layout.yh >> d.layout.site_width >> d.layout.row_height
        >> options.horizontal_capacity >> options.vertical_capacity >> options.bins_x;
    options.bins_y = options.bins_x;
    int nodes = 0, nets = 0;
    input >> nodes;
    if (!input || nodes < 0) {
      throw std::invalid_argument("invalid physical snapshot header");
    }
    for (int i = 0; i < nodes; ++i) {
      double x, y, w, h, blocked_h, blocked_v;
      int    macro;
      input >> x >> y >> w >> h >> macro >> blocked_h >> blocked_v;
      d.node_x.push_back(x);
      d.node_y.push_back(y);
      d.node_size_x.push_back(w);
      d.node_size_y.push_back(h);
      d.node_macro.push_back(macro != 0);
      d.node_blocked_capacity_h.push_back(blocked_h);
      d.node_blocked_capacity_v.push_back(blocked_v);
    }
    input >> nets;
    if (!input || nets < 0) {
      throw std::invalid_argument("invalid net count");
    }
    for (int n = 0; n < nets; ++n) {
      int pins = 0;
      input >> pins;
      if (!input || pins < 2) {
        throw std::invalid_argument("invalid physical hypernet");
      }
      for (int k = 0; k < pins; ++k) {
        int    node;
        double x, y;
        input >> node >> x >> y;
        if (!input || node < 0 || node >= nodes) {
          throw std::invalid_argument("invalid physical pin");
        }
        d.flat_net2pin.push_back(d.pin2node.size());
        d.pin2node.push_back(node);
        d.pin_offset_x.push_back(x - d.node_x[node]);
        d.pin_offset_y.push_back(y - d.node_y[node]);
      }
      d.flat_net2pin_start.push_back(d.pin2node.size());
      d.net_weights.push_back(1);
    }
    if (!input) {
      throw std::invalid_argument("truncated physical snapshot");
    }
    d.build_maps();
    d.validate();
    double context_setup_seconds = 0;
    size_t support_count         = 0;
    if (learned) {
      const auto                     started = std::chrono::steady_clock::now();
      auto                           weights = std::make_shared<const dp::UrouteWeights>(argv[4]);
      std::vector<dp::UrouteSupport> supports;
      for (int i = 5; i < argc; i += 2) {
        supports.push_back({std::to_string((i - 5) / 2), dp::read_uroute_maps(argv[i]), dp::read_uroute_maps(argv[i + 1])});
      }
      support_count         = supports.size();
      options.uroute        = std::make_shared<const dp::UrouteContext>(std::move(weights), std::move(supports));
      options.model         = dp::RudyModel::uroute;
      context_setup_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    }
    std::ofstream out(std::string(argv[2]) + ".json");
    out << std::setprecision(17) << "{\"bins\":" << options.bins_x << ",\"hpwl_dbu\":" << dp::hpwl(d) << ",\"nodes\":" << nodes
        << ",\"nets\":" << nets << ",\"pins\":" << d.pin2node.size()
        << ",\"capacity_h_tracks_per_dbu\":" << options.horizontal_capacity
        << ",\"capacity_v_tracks_per_dbu\":" << options.vertical_capacity;
    out << ",\"route_model\":\"" << (learned ? "uroute" : options.model == dp::RudyModel::rsmt ? "rsmt" : "rudy") << '"';
    if (learned) {
      out << ",\"uroute_support_count\":" << support_count << ",\"uroute_context_setup_seconds\":" << context_setup_seconds
          << ",\"uroute_network_grid\":[" << options.uroute->bins_x() << ',' << options.uroute->bins_y() << ']';
    }
    out << ",\"degree_histogram\":{";
    std::map<int, int> histogram;
    for (int n = 0; n < nets; ++n) {
      ++histogram[d.flat_net2pin_start[n + 1] - d.flat_net2pin_start[n]];
    }
    bool first = true;
    for (auto [degree, count] : histogram) {
      if (!first) {
        out << ',';
      }
      first = false;
      out << '"' << degree << "\":" << count;
    }
    out << '}';
    for (bool enhanced : {false, true}) {
      options.macro_blockages = options.pin_access = enhanced;
      const auto          started                  = std::chrono::steady_clock::now();
      const auto          r                        = dp::rudy(d, options);
      const double        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::vector<double> utilization;
      size_t              congested = 0;
      std::ofstream       map(std::string(argv[2]) + (enhanced ? "_enhanced.tsv" : "_base.tsv"));
      map << "x\ty\thorizontal\tvertical\tpin_access\tmaximum\tblocked_fraction\n" << std::setprecision(17);
      for (size_t i = 0; i < r.horizontal.size(); ++i) {
        const double u = std::max({r.horizontal[i], r.vertical[i], r.pin_utilization[i]});
        utilization.push_back(u);
        congested += u > 1;
        map << i / options.bins_y << '\t' << i % options.bins_y << '\t' << r.horizontal[i] << '\t' << r.vertical[i] << '\t'
            << r.pin_utilization[i] << '\t' << u << '\t' << (r.blocked_fraction.empty() ? 0 : r.blocked_fraction[i]) << '\n';
      }
      std::sort(utilization.begin(), utilization.end());
      out << (enhanced ? ",\"enhanced\":{" : ",\"base\":{") << "\"overflow\":" << r.overflow << ",\"seconds\":" << seconds
          << ",\"max_utilization\":" << r.max_utilization << ",\"congested_bins\":" << congested
          << ",\"mean_utilization\":" << std::accumulate(utilization.begin(), utilization.end(), 0.) / utilization.size()
          << ",\"p95_utilization\":" << utilization[(utilization.size() - 1) * 95 / 100]
          << ",\"p99_utilization\":" << utilization[(utilization.size() - 1) * 99 / 100]
          << ",\"peak_horizontal\":" << *std::max_element(r.horizontal.begin(), r.horizontal.end())
          << ",\"peak_vertical\":" << *std::max_element(r.vertical.begin(), r.vertical.end())
          << ",\"peak_pin_access\":" << *std::max_element(r.pin_utilization.begin(), r.pin_utilization.end())
          << ",\"measured_nets\":" << r.measured_nets << ",\"degenerate_nets\":" << r.degenerate_nets
          << ",\"unroutable_nets\":" << r.unroutable_nets << ",\"blocked_pins\":" << r.blocked_pins
          << ",\"detour_wirelength_dbu\":" << r.detour_wirelength << '}';
    }
    out << "}\n";
    if (!out) {
      throw std::runtime_error("cannot write physical metrics");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
