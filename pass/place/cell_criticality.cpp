// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_criticality.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace livehd::placement {
namespace dp = DreamPlace::native;

Cell_criticality Cell_criticality::from(const timing::Feedback& feedback, const physical::Floorplan& fp,
                                        const std::vector<bool>* ignored) {
  const size_t     count = fp.components.size();
  Cell_criticality result;
  result.timed_units = std::isfinite(feedback.time_unit_ps) && feedback.time_unit_ps > 0;
  result.slack_ps.resize(count);
  result.clock_connected.resize(count);
  result.untimed.resize(count);
  result.connected.resize(count);
  std::map<std::string, size_t> cells;
  for (size_t i = 0; i < count; ++i) {
    cells[fp.components[i].name] = i;
  }
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    if (ignored && (*ignored)[n]) {
      continue;
    }
    const auto& net   = fp.nets[n];
    const bool  clock = std::any_of(net.connections.begin(), net.connections.end(), [&](const auto& c) {
      return feedback.is_clock_pin(c.instance, c.pin);
    });
    for (const auto& c : net.connections) {
      const auto found = cells.find(c.instance);
      if (found == cells.end()) {
        continue;
      }
      const auto i              = found->second;
      const auto pin            = feedback.pin_rank(c.instance, c.pin);
      result.connected[i]       = true;
      result.clock_connected[i] = result.clock_connected[i] || clock;
      const double slack        = pin.slack * feedback.time_unit_ps;
      if (!std::isfinite(pin.slack) || !std::isfinite(slack)) {
        result.untimed[i] = true;
        continue;
      }
      auto& minimum = result.slack_ps[i];
      minimum       = minimum ? std::min(*minimum, slack) : slack;
    }
  }
  return result;
}

std::vector<double> node_weight_criticality(const dp::Design& d) {
  std::vector<double> criticality(d.node_x.size(), 1);
  for (size_t net = 0; net < d.net_weights.size(); ++net) {
    if (d.net_mask.empty() || d.net_mask[net]) {
      for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
        const int node    = d.pin2node[d.flat_net2pin[k]];
        criticality[node] = std::max(criticality[node], d.net_weights[net]);
      }
    }
  }
  return criticality;
}

void limit_displacement(dp::Design& work, const std::vector<double>& criticality, double reach) {
  if (work.node_boxes.empty()) {
    work.node_boxes.assign(work.node_x.size(), {work.layout.xl, work.layout.yl, work.layout.xh, work.layout.yh});
  }
  for (int i = 0; i < work.num_movable; ++i) {
    const double limit = reach / std::sqrt(criticality[i]);
    auto&        box   = work.node_boxes[i];
    box.xl             = std::max(box.xl, work.node_x[i] - limit);
    box.xh             = std::min(box.xh, work.node_x[i] + limit);
    box.yl             = std::max(box.yl, work.node_y[i] - limit);
    box.yh             = std::min(box.yh, work.node_y[i] + limit);
  }
}
}  // namespace livehd::placement
