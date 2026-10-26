// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_jump.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
namespace dp = DreamPlace::native;
bool cell_jump_crosses(double ax, double ay, double bx, double by, double xl, double yl, double xh, double yh) {
  double     lo = 0, hi = 1;
  const auto axis = [&](double a, double b, double low, double high) {
    if (a == b) {
      return a > low && a < high;
    }
    double t0 = (low - a) / (b - a), t1 = (high - a) / (b - a);
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    lo = std::max(lo, t0);
    hi = std::min(hi, t1);
    return lo < hi;
  };
  return axis(ax, bx, xl, xh) && axis(ay, by, yl, yh) && lo < hi;
}
namespace {
using Gap = std::pair<double, double>;
// Fixed objects (including imported row exclusions) and earlier frontier cells
// reserve sites. Other GP cells remain allowed to overlap until legalization.
struct Rows {
  const dp::Design&             d;
  std::vector<std::vector<Gap>> blocked;
  explicit Rows(const dp::Design& design, const std::vector<int>& visited) : d(design) {
    blocked.resize(std::max(0, int(std::floor((d.layout.yh - d.layout.yl) / d.layout.row_height + 1e-9))));
    for (int i = 0; i < int(d.node_x.size()); ++i) {
      if (i >= d.num_movable || visited[i]) {
        reserve(i, d.node_x[i], d.node_y[i]);
      }
    }
    for (auto& row : blocked) {
      row = physical::merge_intervals(std::move(row));
    }
  }
  void reserve(int i, double x, double y) {
    if (d.node_size_x[i] <= 0 || d.node_size_y[i] <= 0) {
      return;
    }
    const auto& l     = d.layout;
    const int   first = std::clamp(int(std::floor((y - l.yl) / l.row_height)), 0, int(blocked.size()));
    const int   last  = std::clamp(int(std::ceil((y + d.node_size_y[i] - l.yl) / l.row_height - 1e-9)), 0, int(blocked.size()));
    for (int r = first; r < last; ++r) {
      blocked[r].emplace_back(x, x + d.node_size_x[i]);
    }
  }
  std::optional<std::pair<double, double>> nearest(int node, double tx, double ty) {
    const auto&  l = d.layout;
    const double w = d.node_size_x[node], h = d.node_size_y[node];
    const auto   box = d.node_boxes.empty() ? dp::PositionBox{l.xl, l.yl, l.xh - w, l.yh - h} : d.node_boxes[node];
    const double xl = std::max(l.xl, box.xl), xh = std::min(l.xh - w, box.xh);
    const double yl = std::max(l.yl, box.yl), yh = std::min(l.yh - h, box.yh);
    if (xl > xh || yl > yh) {
      return {};
    }
    const int        height = std::max(1, int(std::ceil(h / l.row_height - 1e-9)));
    std::vector<int> order;
    for (int r = 0; r + height <= int(blocked.size()); ++r) {
      const double y = l.yl + r * l.row_height;
      if (y >= yl && y <= yh) {
        order.push_back(r);
      }
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return std::pair{std::abs(l.yl + a * l.row_height - ty), a} < std::pair{std::abs(l.yl + b * l.row_height - ty), b};
    });
    double best = std::numeric_limits<double>::infinity(), best_x = 0, best_y = 0;
    for (int r : order) {
      const double y = l.yl + r * l.row_height, dy = std::abs(y - ty);
      if (dy > best) {
        break;
      }
      std::vector<Gap> occupied;
      for (int k = r; k < r + height; ++k) {
        occupied.insert(occupied.end(), blocked[k].begin(), blocked[k].end());
      }
      occupied       = physical::merge_intervals(std::move(occupied));
      const auto gap = [&](double a, double b) {
        const double first = std::ceil((std::max(a, xl) - l.xl) / l.site_width - 1e-9);
        const double last  = std::floor((std::min(b - w, xh) - l.xl) / l.site_width + 1e-9);
        if (first > last) {
          return;
        }
        const double site = std::clamp(std::round((tx - l.xl) / l.site_width), first, last);
        const double x = l.xl + site * l.site_width, cost = std::abs(x - tx) + dy;
        if (std::tuple{cost, y, x} < std::tuple{best, best_y, best_x}) {
          best   = cost;
          best_x = x;
          best_y = y;
        }
      };
      double left = l.xl;
      for (const auto& [a, b] : occupied) {
        gap(left, a);
        left = std::max(left, b);
      }
      gap(left, l.xh);
    }
    if (!std::isfinite(best)) {
      return {};
    }
    reserve(node, best_x, best_y);
    return std::pair{best_x, best_y};
  }
};
struct Link {
  int parent_pin, child_pin;
};
}  // namespace

Cell_jump::Cell_jump(Cell_jump_options policy, std::vector<int> physical_macros)
    : options(policy), macros(std::move(physical_macros)) {
  if (options.depth > 4 || options.settle == 0 || options.interval == 0
      || options.fix_period > unsigned(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("cell_jump requires depth 0..4 and positive settle/interval");
  }
  if (options.depth > 1
      && uint64_t(options.settle) + uint64_t(options.depth - 2) * options.interval
                 + std::max(uint64_t(options.interval), uint64_t(options.fix_period) + 1)
             > uint64_t(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("cell_jump schedule exceeds the GP iteration range");
  }
}
std::vector<dp::MovablePosition> Cell_jump::update(const dp::Design& d, int iteration) {
  if (next_depth > options.depth || iteration != int(options.settle + (next_depth - 2) * options.interval)) {
    return {};
  }
  const auto signal = [&](int net) { return d.net_mask.empty() || d.net_mask[net]; };
  const auto cell   = [&](int i) { return i < d.num_movable && (d.node_macro.empty() || !d.node_macro[i]); };
  if (depth.empty()) {
    depth.assign(d.num_movable, 0);
    std::vector<bool> anchor(d.node_x.size());
    for (int i : macros) {
      if (i < d.num_movable || i >= int(d.node_x.size())) {
        throw std::invalid_argument("cell_jump requires fixed macro bodies");
      }
      anchor[i] = true;
    }
    for (int i = d.num_movable; i < int(d.node_x.size()); ++i) {
      anchor[i] = anchor[i] || (d.node_size_x[i] == 0 && d.node_size_y[i] == 0);
    }
    for (int net = 0; net < int(d.net_weights.size()); ++net) {
      if (!signal(net)) {
        continue;
      }
      bool fixed_pin = false;
      for (int j = d.flat_net2pin_start[net]; j < d.flat_net2pin_start[net + 1]; ++j) {
        fixed_pin |= anchor[d.pin2node[d.flat_net2pin[j]]];
      }
      if (fixed_pin) {
        for (int j = d.flat_net2pin_start[net]; j < d.flat_net2pin_start[net + 1]; ++j) {
          const int i = d.pin2node[d.flat_net2pin[j]];
          if (cell(i) && !depth[i]) {
            depth[i] = 1;
            frontier.push_back(i);
          }
        }
      }
    }
    std::sort(frontier.begin(), frontier.end());
    roots = frontier.size();
  }
  if (depth.size() != size_t(d.num_movable)) {
    throw std::invalid_argument("cell_jump does not support topology changes during GP");
  }
  std::map<int, std::vector<Link>> candidates;
  for (int parent : frontier) {
    for (int k = d.flat_node2pin_start[parent]; k < d.flat_node2pin_start[parent + 1]; ++k) {
      const int p = d.flat_node2pin[k], net = d.pin2net[p];
      if (!signal(net)) {
        continue;
      }
      for (int j = d.flat_net2pin_start[net]; j < d.flat_net2pin_start[net + 1]; ++j) {
        const int q = d.flat_net2pin[j], child = d.pin2node[q];
        if (cell(child) && !depth[child]) {
          candidates[child].push_back({p, q});
        }
      }
    }
  }
  Cell_jump_sample sample;
  sample.iteration   = iteration;
  sample.depth       = next_depth;
  sample.roots       = roots;
  sample.candidates  = candidates.size();
  const auto crosses = [&](const Link& link, double x, double y) {
    const int    parent = d.pin2node[link.parent_pin];
    const double ax     = d.node_x[parent] + d.pin_offset_x[link.parent_pin];
    const double ay     = d.node_y[parent] + d.pin_offset_y[link.parent_pin];
    const double bx = x + d.pin_offset_x[link.child_pin], by = y + d.pin_offset_y[link.child_pin];
    return std::any_of(macros.begin(), macros.end(), [&](int m) {
      return cell_jump_crosses(ax,
                               ay,
                               bx,
                               by,
                               d.node_x[m],
                               d.node_y[m],
                               d.node_x[m] + d.node_size_x[m],
                               d.node_y[m] + d.node_size_y[m]);
    });
  };
  Rows                             rows(d, depth);
  std::vector<dp::MovablePosition> moves;
  frontier.clear();
  for (const auto& [child, links] : candidates) {
    frontier.push_back(child);  // Clear edges still advance the graph frontier.
    depth[child] = next_depth;
    std::optional<std::tuple<double, int, int, double, double>> target;
    for (const auto& link : links) {
      if (!crosses(link, d.node_x[child], d.node_y[child])) {
        continue;
      }
      ++sample.crossing_edges_before;
      const int    parent = d.pin2node[link.parent_pin];
      const double x      = d.node_x[parent] + d.pin_offset_x[link.parent_pin] - d.pin_offset_x[link.child_pin];
      const double y      = d.node_y[parent] + d.pin_offset_y[link.parent_pin] - d.pin_offset_y[link.child_pin];
      const auto   rank   = std::tuple{std::abs(x - d.node_x[child]) + std::abs(y - d.node_y[child]), parent, link.child_pin, x, y};
      if (!target || rank < *target) {
        target = rank;
      }
    }
    if (target) {
      ++sample.crossing_cells;
      const auto xy = rows.nearest(child, std::get<3>(*target), std::get<4>(*target));
      if (xy) {
        const auto [x, y]     = *xy;
        const double distance = std::abs(x - d.node_x[child]) + std::abs(y - d.node_y[child]);
        if (distance > 0) {
          moves.push_back({child, x, y, int(options.fix_period)});
          ++sample.moved;
          sample.total_displacement   += distance;
          sample.maximum_displacement  = std::max(sample.maximum_displacement, distance);
        }
        for (const auto& link : links) {
          sample.crossing_edges_after += crosses(link, x, y);
        }
      } else {
        ++sample.unavailable;
        for (const auto& link : links) {
          sample.crossing_edges_after += crosses(link, d.node_x[child], d.node_y[child]);
        }
      }
    }
  }
  ++next_depth;
  history.push_back(sample);
  return moves;
}
}  // namespace livehd::placement
