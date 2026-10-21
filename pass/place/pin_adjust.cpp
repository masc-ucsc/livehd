// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_adjust.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
using Point = std::pair<double, double>;
struct Bounds {
  double xl, yl, xh, yh, weight;
};
}  // namespace
Pin_adjust_stats adjust_fp_pins(dp::Design& d, physical::Floorplan& fp, const std::vector<std::string>& buses,
                                const std::vector<int>& bits, double pitch, double step_fraction, double improvement,
                                bool initialize) {
  const size_t count = fp.terminals.size();
  if (buses.size() != count || bits.size() != count || count > d.node_x.size() || !std::isfinite(pitch) || pitch <= 0
      || !std::isfinite(step_fraction) || step_fraction <= 0 || step_fraction > .5 || !std::isfinite(improvement) || improvement < 0
      || improvement >= 1) {
    throw std::invalid_argument("invalid FP pin adjustment input");
  }
  pitch              = std::ceil(pitch);  // Integer-DBU slots retain the requested minimum separation.
  const size_t first = d.node_x.size() - count;
  const auto&  die   = fp.die;
  const double width = die.xh - die.xl, height = die.yh - die.yl;
  const double perimeter = 2 * (width + height), limit = step_fraction * perimeter;
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("invalid IO die boundary");
  }
  const auto edge_distance = [&](Point p) {
    return std::min(
        {std::abs(p.first - die.xl), std::abs(p.first - die.xh), std::abs(p.second - die.yl), std::abs(p.second - die.yh)});
  };
  const auto position = [&](Point p) {
    // Imported pin centers can be inset from the edge by half their shape.
    // Project those centers for circular ordering; retain their physical XY.
    const double offset = edge_distance(p);
    if (offset > pitch) {
      throw std::invalid_argument("adjustable bus has a pin away from the die perimeter");
    }
    if (std::abs(p.first - die.xl) == offset) {
      return std::clamp(p.second - die.yl, 0., height);
    }
    if (std::abs(p.second - die.yh) == offset) {
      return height + std::clamp(p.first - die.xl, 0., width);
    }
    if (std::abs(p.first - die.xh) == offset) {
      return height + width + std::clamp(die.yh - p.second, 0., height);
    }
    return 2 * height + width + std::clamp(die.xh - p.first, 0., width);
  };
  const auto distance = [&](Point a, Point b) {
    const double delta = std::abs(position(a) - position(b));
    return std::min(delta, perimeter - delta);
  };
  std::vector<Point>                      xy;
  std::map<std::string, std::vector<int>> groups;
  for (size_t i = 0; i < count; ++i) {
    if (first + i < static_cast<size_t>(d.num_movable) || d.node_size_x[first + i] || d.node_size_y[first + i]) {
      throw std::invalid_argument("IO adjustment requires a zero-area fixed terminal suffix");
    }
    xy.emplace_back(fp.terminals[i].x, fp.terminals[i].y);
    groups[buses[i]].push_back(i);
  }
  for (auto& [name, group] : groups) {
    (void)name;
    std::sort(group.begin(), group.end(), [&](int a, int b) { return bits[a] < bits[b]; });
    for (size_t i = 1; i < group.size(); ++i) {
      if (bits[group[i - 1]] == bits[group[i]]) {
        throw std::invalid_argument("duplicate IO bus lane");
      }
    }
  }
  const auto         fixed = [&](int pin) { return fp.terminals[pin].status == physical::Status::FIXED; };
  // Corners have a full-pitch margin: adjacent-edge rectangles cannot collide.
  std::vector<Point> slots;
  for (double y = die.yl + pitch; y <= die.yh - pitch; y += pitch) {
    slots.emplace_back(die.xl, std::round(y));
    slots.emplace_back(die.xh, std::round(y));
  }
  for (double x = die.xl + pitch; x <= die.xh - pitch; x += pitch) {
    slots.emplace_back(std::round(x), die.yl);
    slots.emplace_back(std::round(x), die.yh);
  }
  // An imported fixed pin need not lie on our generated slot grid.
  for (size_t i = 0; i < count; ++i) {
    if (fixed(i)) {
      const auto& group = groups.at(buses[i]);
      if (std::any_of(group.begin(), group.end(), [&](int p) { return !fixed(p); })) {
        (void)position(xy[i]);
        slots.push_back(xy[i]);
      }
    }
  }
  std::sort(slots.begin(), slots.end(), [&](Point a, Point b) { return position(a) < position(b); });
  slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
  std::vector<double> slot_position;
  for (Point p : slots) {
    slot_position.push_back(position(p));
  }
  // Ideal clocks are masked from GP's wirelength objective. IO assignment
  // still needs their actual sink distribution; a sink-centroid objective
  // avoids HPWL's flat interior (and its arbitrary first-slot corner choice).
  std::vector<std::optional<Point>> clock_centers(d.net_mask.size());
  for (size_t net = 0; net < d.net_mask.size(); ++net) {
    if (d.net_mask[net]) {
      continue;
    }
    double x = 0, y = 0;
    int    sinks = 0;
    for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
      const int p = d.flat_net2pin[k], n = d.pin2node[p];
      if (size_t(n) < first) {
        x += d.node_x[n] + d.pin_offset_x[p];
        y += d.node_y[n] + d.pin_offset_y[p];
        ++sinks;
      }
    }
    if (sinks) {
      clock_centers[net] = Point{x / sinks, y / sinks};
    }
  }
  std::vector<std::vector<int>>    incident(count);
  std::vector<std::vector<Bounds>> bounds(count);
  for (size_t i = 0; i < count; ++i) {
    std::set<int> nets;
    for (int k = d.flat_node2pin_start[first + i]; k < d.flat_node2pin_start[first + i + 1]; ++k) {
      nets.insert(d.pin2net[d.flat_node2pin[k]]);
    }
    for (int net : nets) {
      if (!d.net_mask[net] && !clock_centers[net]) {
        continue;
      }
      incident[i].push_back(net);
      if (clock_centers[net]) {
        const auto [x, y] = *clock_centers[net];
        bounds[i].push_back({x, y, x, y, 1});
        continue;
      }
      Bounds b{INFINITY, INFINITY, -INFINITY, -INFINITY, d.net_weights[net]};
      for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
        const int p = d.flat_net2pin[k], n = d.pin2node[p];
        if (n == int(first + i)) {
          continue;
        }
        const double x = d.node_x[n] + d.pin_offset_x[p], y = d.node_y[n] + d.pin_offset_y[p];
        b.xl = std::min(b.xl, x);
        b.xh = std::max(b.xh, x);
        b.yl = std::min(b.yl, y);
        b.yh = std::max(b.yh, y);
      }
      if (std::isfinite(b.xl)) {
        bounds[i].push_back(b);
      }
    }
  }
  const auto cost = [&](int pin, Point p) {
    double value = 0;
    for (const auto& b : bounds[pin]) {
      value += b.weight * (std::max(b.xh, p.first) - std::min(b.xl, p.first) + std::max(b.yh, p.second) - std::min(b.yl, p.second));
    }
    return value;
  };
  // Exact acceptance scores touch only the affected nets; scanning the whole
  // design for every lane/reversal would make IO adjustment dominate GP.
  const auto net_cost = [&](const std::vector<int>& nets) {
    double value = 0;
    for (int net : nets) {
      if (clock_centers[net]) {
        const auto [cx, cy] = *clock_centers[net];
        for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
          const auto node = size_t(d.pin2node[d.flat_net2pin[k]]);
          if (node >= first) {
            value += std::abs(xy[node - first].first - cx) + std::abs(xy[node - first].second - cy);
          }
        }
        continue;
      }
      double xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
      for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
        const int    p = d.flat_net2pin[k], n = d.pin2node[p];
        const double x = (size_t(n) >= first ? xy[n - first].first : d.node_x[n]) + d.pin_offset_x[p];
        const double y = (size_t(n) >= first ? xy[n - first].second : d.node_y[n]) + d.pin_offset_y[p];
        xl             = std::min(xl, x);
        xh             = std::max(xh, x);
        yl             = std::min(yl, y);
        yh             = std::max(yh, y);
      }
      if (std::isfinite(xl)) {
        value += d.net_weights[net] * (xh - xl + yh - yl);
      }
    }
    return value;
  };
  const auto apply = [&] {
    for (size_t i = 0; i < count; ++i) {
      d.node_x[first + i] = xy[i].first;
      d.node_y[first + i] = xy[i].second;
    }
  };
  const auto ordered = [&](const std::vector<int>& group) {
    if (group.size() < 3) {
      return true;
    }
    std::vector<int> order = group;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return position(xy[a]) < position(xy[b]); });
    const auto zero = std::find(order.begin(), order.end(), group.front()) - order.begin();
    bool       up = true, down = true;
    for (size_t k = 0; k < group.size(); ++k) {
      up   &= order[(zero + k) % group.size()] == group[k];
      down &= order[(zero + group.size() - k) % group.size()] == group[k];
    }
    return up || down;
  };
  Pin_adjust_stats stats;
  stats.before               = dp::hpwl(d);
  const auto        original = xy;
  std::vector<bool> occupied(count);
  for (size_t i = 0; i < count; ++i) {
    occupied[i] = !initialize || fixed(i);
  }
  for (const auto& [name, group] : groups) {
    (void)name;
    if (std::all_of(group.begin(), group.end(), fixed)) {
      continue;
    }
    // Mark occupied perimeter intervals once per bus, rather than checking
    // every other pin inside the lane-by-slot dynamic program.
    std::vector<int> blocked(slots.size()), blocker(slots.size(), -1);
    for (size_t i = 0; i < count; ++i) {
      if (!occupied[i]) {
        continue;
      }
      // A fully fixed imported pin may lie inside the die. It is an obstacle
      // only when its actual geometry reaches a perimeter slot.
      if (edge_distance(xy[i]) > pitch) {
        for (size_t j = 0; j < slots.size(); ++j) {
          if (std::abs(xy[i].first - slots[j].first) + std::abs(xy[i].second - slots[j].second) < pitch - .1) {
            ++blocked[j];
            blocker[j] = i;
          }
        }
        continue;
      }
      const double t = position(xy[i]);
      for (double shift : {-perimeter, 0., perimeter}) {
        auto lo = std::upper_bound(slot_position.begin(), slot_position.end(), t + shift - pitch + .1);
        auto hi = std::lower_bound(slot_position.begin(), slot_position.end(), t + shift + pitch - .1);
        for (auto it = lo; it != hi; ++it) {
          const size_t j = it - slot_position.begin();
          ++blocked[j];
          blocker[j] = i;
        }
      }
    }
    const auto clear = [&](size_t slot, int pin) { return !blocked[slot] || (blocked[slot] == 1 && blocker[slot] == pin); };
    if (initialize) {
      // Ordered assignment by prefix-min dynamic programming. Other buses may
      // occupy intervening slots. A fixed lane anchors the circular cut; free
      // buses try the perimeter origin and their first lane's preferred slot.
      const auto          anchor = std::find_if(group.begin(), group.end(), fixed);
      const int           pivot  = anchor == group.end() ? 0 : int(anchor - group.begin());
      std::vector<size_t> starts{0};
      if (anchor != group.end()) {
        starts = {size_t(std::find(slots.begin(), slots.end(), xy[*anchor]) - slots.begin())};
      } else if (!slots.empty()) {
        starts.push_back(
            std::min_element(slots.begin(), slots.end(), [&](Point a, Point b) { return cost(group[0], a) < cost(group[0], b); })
            - slots.begin());
      }
      double             best = INFINITY;
      std::vector<Point> chosen;
      for (int direction : {1, -1}) {
        for (size_t start : starts) {
          const size_t m = slots.size();
          if (!m || start >= m) {
            continue;
          }
          std::vector<int> sequence;
          for (size_t k = 0; k < group.size(); ++k) {
            sequence.push_back(group[(pivot + direction * int(k) + int(group.size())) % group.size()]);
          }
          std::vector<double>           previous(m, INFINITY), current(m, INFINITY);
          std::vector<std::vector<int>> back(group.size(), std::vector<int>(m, -1));
          for (size_t k = 0; k < sequence.size(); ++k) {
            double prefix    = INFINITY;
            int    prefix_at = -1;
            std::fill(current.begin(), current.end(), INFINITY);
            for (size_t s = 0; s < m; ++s) {
              if (s && previous[s - 1] < prefix) {
                prefix    = previous[s - 1];
                prefix_at = s - 1;
              }
              const Point p   = slots[(start + s) % m];
              const int   pin = sequence[k];
              if ((fixed(pin) && p != xy[pin]) || !clear((start + s) % m, pin)) {
                continue;
              }
              const double prior = k ? prefix : 0;
              if (!std::isfinite(prior)) {
                continue;
              }
              current[s] = prior + cost(pin, p);
              back[k][s] = prefix_at;
            }
            previous.swap(current);
          }
          int at = std::min_element(previous.begin(), previous.end()) - previous.begin();
          if (previous[at] < best) {
            best   = previous[at];
            chosen = xy;
            for (int k = int(sequence.size()) - 1; k >= 0; --k) {
              chosen[sequence[k]] = slots[(start + at) % m];
              at                  = back[k][at];
            }
          }
        }
      }
      if (chosen.empty()) {
        throw std::runtime_error("no feasible ordered IO slots for bus " + name);
      }
      xy = std::move(chosen);
      for (int pin : group) {
        occupied[pin] = true;
      }
      apply();
      continue;
    }
    if (!ordered(group)) {
      throw std::runtime_error("FP IO bus lost its circular bit order");
    }
    for (int pin : group) {
      if (fixed(pin)) {
        continue;
      }
      const Point before = xy[pin];
      Point       best   = before;
      double      value  = cost(pin, before);
      // Tangential moves may skip free slots, but cannot violate bus order.
      // Other lanes of this bus may have moved since the occupancy map was
      // built. Check those few lanes explicitly; other-bus obstacles are static.
      for (size_t slot = 0; slot < slots.size(); ++slot) {
        const Point p = slots[slot];
        if (distance(before, p) > limit || !clear(slot, pin)) {
          continue;
        }
        if (std::any_of(group.begin(), group.end(), [&](int q) {
              return q != pin && std::abs(xy[q].first - p.first) + std::abs(xy[q].second - p.second) < pitch - .1;
            })) {
          continue;
        }
        const double candidate = cost(pin, p);
        if (candidate >= value) {
          continue;
        }
        xy[pin] = p;
        if (ordered(group)) {
          value = candidate;
          best  = p;
        }
      }
      xy[pin]            = best;
      const double next  = net_cost(incident[pin]);
      xy[pin]            = before;
      const double prior = net_cost(incident[pin]);
      if (next < prior - improvement * std::max(1., cost(pin, before))) {
        xy[pin] = best;
        apply();
      }
    }
    if (group.size() > 1) {
      const auto       saved = xy;
      std::vector<int> nets;
      for (int pin : group) {
        nets.insert(nets.end(), incident[pin].begin(), incident[pin].end());
      }
      std::sort(nets.begin(), nets.end());
      nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
      const double prior     = net_cost(nets);
      bool         allowed   = true;
      // Reverse around each possible physical anchor. This includes reversals
      // around a fixed middle lane; unrelated interleaved pins retain positions.
      double       best_cost = prior;
      auto         best      = xy;
      for (size_t shift = 0; shift < group.size(); ++shift) {
        xy = saved;
        for (size_t k = 0; k < group.size(); ++k) {
          xy[group[k]] = saved[group[(shift + group.size() - k) % group.size()]];
        }
        allowed = true;
        for (int pin : group) {
          allowed &= (!fixed(pin) || xy[pin] == saved[pin]) && distance(xy[pin], original[pin]) <= limit;
        }
        if (!allowed) {
          continue;
        }
        const double value = net_cost(nets);
        if (value < best_cost) {
          best_cost = value;
          best      = xy;
        }
      }
      double local = 0;
      for (int pin : group) {
        local += cost(pin, saved[pin]);
      }
      xy = saved;
      if (best_cost < prior - improvement * std::max(1., local)) {
        xy = std::move(best);
        ++stats.reversals;
      }
      apply();
    }
  }
  apply();
  for (size_t i = 0; i < count; ++i) {
    if (xy[i] != original[i]) {
      ++stats.moved;
      stats.travel += distance(xy[i], original[i]);
    }
    fp.terminals[i].x = std::llround(xy[i].first);
    fp.terminals[i].y = std::llround(xy[i].second);
  }
  stats.after = dp::hpwl(d);
  return stats;
}
}  // namespace livehd::placement
