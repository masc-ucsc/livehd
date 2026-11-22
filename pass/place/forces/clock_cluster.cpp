// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_cluster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_map>

#include "pass/place/capacity_bins.hpp"

namespace livehd::placement {
namespace dp = DreamPlace::native;

namespace {
struct Offset {
  double x = 0, y = 0;
};
struct Span {
  double xmin = INFINITY, xmax = -INFINITY, ymin = INFINITY, ymax = -INFINITY;
  void   add(double x, double y) {
    xmin = std::min(xmin, x);
    xmax = std::max(xmax, x);
    ymin = std::min(ymin, y);
    ymax = std::max(ymax, y);
  }
  double length() const { return xmax - xmin + ymax - ymin; }
};
// Compare a proposed move with the data wires at this checkpoint, including
// earlier accepted moves. A register's D and Q can share a feedback net.
bool data_move_pays(const dp::Design& d, int i, Offset proposed, double clock_saving, const std::vector<Offset>& offsets,
                    std::vector<int>& visited) {
  double cost = 0;
  for (int j = d.flat_node2pin_start[i]; j < d.flat_node2pin_start[i + 1]; ++j) {
    const int pin = d.flat_node2pin[j], net = d.pin2net[pin];
    if (!d.net_mask[net] || visited[net] == i) {
      continue;
    }
    visited[net]    = i;
    const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    // Only local data nets guide a leaf. This also bounds the work per flop.
    if (end - begin > 16) {
      continue;
    }
    Span   before, after;
    double timing_weight = 0;
    for (int k = begin; k < end; ++k) {
      const int    other = d.flat_net2pin[k], node = d.pin2node[other];
      const double x = d.node_x[node] + d.pin_offset_x[other] + offsets[node].x;
      const double y = d.node_y[node] + d.pin_offset_y[other] + offsets[node].y;
      before.add(x, y);
      after.add(x + (node == i ? proposed.x : 0), y + (node == i ? proposed.y : 0));
      if (d.pin_timing && !d.pin_timing->weight.empty()) {
        timing_weight = std::max(timing_weight, d.pin_timing->weight[other]);
      }
    }
    const double delta  = after.length() - before.length();
    const double weight = d.net_weights[net] + timing_weight;
    if (weight > 1 && delta > 1.e-8) {
      return false;
    }
    cost += weight * delta;
  }
  return cost <= clock_saving + 1.e-8;
}
}  // namespace

std::vector<dp::PairAnchor> clock_leaf_anchors(const std::vector<Clock_anchor_group>& groups, double strength,
                                               const std::vector<double>& criticality) {
  std::vector<dp::PairAnchor> pairs;
  if (strength <= 0) {
    return pairs;
  }
  std::unordered_map<int, size_t> gate_leaves;
  size_t                          count = 0;
  for (const auto& [gate, members] : groups) {
    const size_t n = members.size();
    if (n == 0) {
      continue;
    }
    count += n * (n - 1) / 2;
    if (gate >= 0) {
      ++gate_leaves[gate];
      count += n;
    }
  }
  pairs.reserve(count);
  for (const auto& [gate, members] : groups) {
    const size_t degree = members.size() + (gate >= 0);
    if (degree < 2) {
      continue;
    }
    const auto append = [&](int first, int second, size_t leaves) {
      const double weight = strength / ((degree - 1) * std::max({1., criticality.at(first), criticality.at(second)}));
      pairs.push_back({std::min(first, second), std::max(first, second), weight / leaves});
    };
    // The same gate participates in every leaf of its distribution. Each leaf
    // gets an equal share of the gate's weight, including singleton leaves.
    const auto leaves = gate < 0 ? 1 : gate_leaves.at(gate);
    for (size_t i = 0; i < members.size(); ++i) {
      for (size_t j = 0; j < i; ++j) {
        append(members[i], members[j], 1);
      }
      if (gate >= 0) {
        append(members[i], gate, leaves);
      }
    }
  }
  return pairs;
}

size_t retained_clock_anchors(const std::vector<Clock_anchor_group>& groups, const std::vector<dp::PairAnchor>& previous,
                              size_t node_count) {
  if (previous.empty()) {
    return 0;
  }
  std::vector<const Clock_anchor_group*> membership(node_count);
  for (const auto& group : groups) {
    for (const int node : group.members) {
      membership.at(node) = &group;
    }
  }
  return std::ranges::count_if(previous, [&](const auto& pair) {
    const auto* a = membership.at(pair.first);
    const auto* b = membership.at(pair.second);
    return (a && (a == b || a->gate == pair.second)) || (b && b->gate == pair.first);
  });
}

Clock_cluster_step clock_cluster_step(const dp::Design& d, std::vector<std::vector<int>> domains, double strength, double density) {
  Clock_cluster_step step;
  if (domains.empty() || strength <= 0) {
    return step;
  }
  const auto&   l      = d.layout;
  // Roughly 24 movable cells per bin. Account for the footprint of fixed macros
  // in every covered bin, including bins that contain no macro center.
  const double  aspect = (l.xh - l.xl) / (l.yh - l.yl);
  const double  count  = std::max(1., d.num_movable / 24.);
  Capacity_bins grid({l.xl, l.yl, l.xh, l.yh},
                     std::clamp(int(std::round(std::sqrt(count * aspect))), 1, 128),
                     std::clamp(int(std::round(std::sqrt(count / aspect))), 1, 128));
  const double  bin_area = grid.dx * grid.dy;
  for (auto& bin : grid.bins) {
    bin.capacity = bin_area;
  }
  std::vector<double> pins(grid.bins.size());
  double              total_pins = 0, total_area = 0;
  const auto          rectangle
      = [&](int i, double x, double y) { return physical::Rect{x, y, x + d.node_size_x[i], y + d.node_size_y[i]}; };
  const auto pin_bin = [&](int pin, double x, double y) {
    const int bx = std::clamp(int(std::floor((x + d.pin_offset_x[pin] - l.xl) / grid.dx)), 0, int(grid.nx) - 1);
    const int by = std::clamp(int(std::floor((y + d.pin_offset_y[pin] - l.yl) / grid.dy)), 0, int(grid.ny) - 1);
    return grid.index(bx, by);
  };
  for (size_t p = 0; p < d.pin2node.size(); ++p) {
    const int i = d.pin2node[p];
    ++pins[pin_bin(p, d.node_x[i], d.node_y[i])];
    if (i < d.num_movable) {
      ++total_pins;
    }
  }
  for (int i = 0; i < int(d.node_x.size()); ++i) {
    const double area = d.node_size_x[i] * d.node_size_y[i];
    if (area <= 0) {
      continue;
    }
    const bool fixed = i >= d.num_movable;
    grid.each(rectangle(i, d.node_x[i], d.node_y[i]), [&](size_t b, double a) {
      if (fixed) {
        grid.bins[b].capacity -= a;
      } else {
        grid.bins[b].after += a;
      }
    });
    if (!fixed) {
      total_area += area;
    }
  }
  for (auto& bin : grid.bins) {
    bin.capacity   = std::max(0., bin.capacity);
    step.overflow += std::max(0., bin.after - density * bin.capacity);
  }
  step.overflow     /= std::max(1., total_area);
  // Spread first. The pull rises continuously to its requested strength as excess density disappears.
  const double ramp  = std::clamp(1. - step.overflow / .5, 0., 1.);
  if (ramp == 0 || total_area <= 0) {
    return step;
  }
  const double        pin_limit = density * total_pins / total_area;
  std::vector<int>    pin_delta(pins.size());
  std::vector<size_t> touched;
  std::vector<Offset> accepted(d.node_x.size());
  std::vector<int>    visited(d.net_weights.size(), -1);
  struct Position {
    double x = 0, y = 0, criticality = 1;
  };
  std::vector<Position> anchors(d.num_movable);
  const auto            center_x = [&](int i) { return d.node_x[i] + .5 * d.node_size_x[i]; };
  const auto            center_y = [&](int i) { return d.node_y[i] + .5 * d.node_size_y[i]; };
  for (auto& domain : domains) {
    for (int i : domain) {
      auto&  anchor = anchors[i];
      // A flop's low-fanout data neighbours influence its choice of leaf.
      double wx = 0, wy = 0, mass = 0;
      for (int k = d.flat_node2pin_start[i]; k < d.flat_node2pin_start[i + 1]; ++k) {
        const int pin = d.flat_node2pin[k], net = d.pin2net[pin];
        if (!d.net_mask[net]) {
          continue;
        }
        const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
        double    weight = d.net_weights[net];
        if (d.pin_timing && !d.pin_timing->weight.empty()) {
          weight += d.pin_timing->weight[pin];
        }
        anchor.criticality = std::max(anchor.criticality, weight);
        if (end - begin > 16 || end - begin < 2) {
          continue;
        }
        const double share = weight / (end - begin - 1);
        for (int at = begin; at < end; ++at) {
          const int other = d.flat_net2pin[at];
          const int node  = d.pin2node[other];
          if (node == i) {
            continue;
          }
          wx   += share * (d.node_x[node] + d.pin_offset_x[other]);
          wy   += share * (d.node_y[node] + d.pin_offset_y[other]);
          mass += share;
        }
      }
      anchor.x = mass > 0 ? .75 * center_x(i) + .25 * wx / mass : center_x(i);
      anchor.y = mass > 0 ? .75 * center_y(i) + .25 * wy / mass : center_y(i);
    }
    const auto group = [&](auto&& self, auto begin, auto end) -> void {
      if (end - begin < 2) {
        return;
      }
      if (end - begin > 24) {
        const auto xorder     = [&](int a, int b) { return std::tie(anchors[a].x, a) < std::tie(anchors[b].x, b); };
        const auto yorder     = [&](int a, int b) { return std::tie(anchors[a].y, a) < std::tie(anchors[b].y, b); };
        auto [xl, xh]         = std::minmax_element(begin, end, xorder);
        auto [yl, yh]         = std::minmax_element(begin, end, yorder);
        const bool horizontal = anchors[*xh].x - anchors[*xl].x >= anchors[*yh].y - anchors[*yl].y;
        auto       mid        = begin + (end - begin) / 2;
        std::nth_element(begin, mid, end, [&](int a, int b) { return horizontal ? xorder(a, b) : yorder(a, b); });
        self(self, begin, mid);
        self(self, mid, end);
        return;
      }
      ++step.groups;
      double cx = 0, cy = 0;
      for (auto it = begin; it != end; ++it) {
        cx += center_x(*it);
        cy += center_y(*it);
      }
      cx /= end - begin;
      cy /= end - begin;
      for (auto it = begin; it != end; ++it) {
        const int i = *it;
        if (d.legalization_fixed(i)) {
          continue;
        }
        const double dx = cx - center_x(i), dy = cy - center_y(i);
        // A single correction can travel at most one row. GP remains free to
        // undo the move at the next step, including across provisional groups.
        const double scale
            = std::min(strength, l.row_height / std::max(l.row_height, std::hypot(dx, dy))) * ramp / anchors[i].criticality;
        const double    ox = d.node_x[i], oy = d.node_y[i];
        dp::PositionBox box{l.xl, l.yl, l.xh - d.node_size_x[i], l.yh - d.node_size_y[i]};
        if (!d.node_boxes.empty()) {
          const auto& fence = d.node_boxes[i];
          box.xl            = std::max(box.xl, fence.xl);
          box.yl            = std::max(box.yl, fence.yl);
          box.xh            = std::min(box.xh, fence.xh);
          box.yh            = std::min(box.yh, fence.yh);
        }
        if (box.xl > box.xh || box.yl > box.yh) {
          continue;
        }
        const double nx = std::clamp(ox + scale * dx, box.xl, box.xh), ny = std::clamp(oy + scale * dy, box.yl, box.yh);
        if (nx == ox && ny == oy) {
          continue;
        }
        const Offset proposed{nx - ox, ny - oy};
        const double saving = std::abs(dx) + std::abs(dy) - std::abs(dx - proposed.x) - std::abs(dy - proposed.y);
        if (!data_move_pays(d, i, proposed, saving, accepted, visited)) {
          ++step.data_rejections;
          continue;
        }
        const auto from = rectangle(i, ox, oy), to = rectangle(i, nx, ny);
        bool       fits = true;
        grid.each(to, [&](size_t b, double a) {
          const auto&  bin   = grid.bins[b];
          const double extra = a - Capacity_bins::overlap(from, bin.bounds);
          if (extra > 1.e-9 && bin.after + extra > density * bin.capacity) {
            fits = false;
          }
        });
        touched.clear();
        for (int k = d.flat_node2pin_start[i]; k < d.flat_node2pin_start[i + 1]; ++k) {
          const int    pin     = d.flat_node2pin[k];
          const size_t old_bin = pin_bin(pin, ox, oy), new_bin = pin_bin(pin, nx, ny);
          if (old_bin == new_bin) {
            continue;
          }
          --pin_delta[old_bin];
          ++pin_delta[new_bin];
          touched.push_back(old_bin);
          touched.push_back(new_bin);
        }
        for (size_t b : touched) {
          if (pin_delta[b] > 0 && pins[b] + pin_delta[b] > pin_limit * grid.bins[b].capacity) {
            fits = false;
          }
        }
        for (size_t b : touched) {
          if (fits) {
            pins[b] += pin_delta[b];
          }
          pin_delta[b] = 0;
        }
        if (!fits) {
          ++step.congested;
          continue;
        }
        grid.each(from, [&](size_t b, double a) { grid.bins[b].after -= a; });
        grid.each(to, [&](size_t b, double a) { grid.bins[b].after += a; });
        accepted[i] = proposed;
        step.moves.push_back({i, nx, ny, 0});
        step.displacement += std::hypot(nx - ox, ny - oy);
      }
    };
    group(group, domain.begin(), domain.end());
  }
  return step;
}
}  // namespace livehd::placement
