// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Read-only native RUDY evaluation of an extracted physical pin snapshot.
// Input: xl yl xh yh site_width row_height capacity_h capacity_v bins;
// node_count, then x y width height is_macro blocked_h blocked_v per node;
// net_count, then pin_count followed by node_id absolute_x absolute_y per pin.
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

#include "dreamplace/native/placement.hpp"
#include "dreamplace/native/rudy.hpp"

namespace dp = DreamPlace::native;

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("usage: rudy_metrics physical_snapshot.txt output_prefix");
    }
    std::ifstream   input(argv[1]);
    dp::Design      d;
    dp::RudyOptions options;
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
    std::ofstream out(std::string(argv[2]) + ".json");
    out << std::setprecision(17) << "{\"bins\":" << options.bins_x << ",\"hpwl_dbu\":" << dp::hpwl(d) << ",\"nodes\":" << nodes
        << ",\"nets\":" << nets << ",\"pins\":" << d.pin2node.size()
        << ",\"capacity_h_tracks_per_dbu\":" << options.horizontal_capacity
        << ",\"capacity_v_tracks_per_dbu\":" << options.vertical_capacity;
    for (bool enhanced : {false, true}) {
      options.macro_blockages = options.pin_access = enhanced;
      const auto          r                        = dp::rudy(d, options);
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
      out << (enhanced ? ",\"enhanced\":{" : ",\"base\":{") << "\"overflow\":" << r.overflow
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
