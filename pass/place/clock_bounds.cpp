// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_bounds.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace livehd::placement {
namespace {
// Tight setup and hold floors impose x[to] >= x[from]. Vertices on a
// directed cycle of these inequalities must shift by exactly the same delay.
std::vector<size_t> equal_delay_groups(const std::vector<double>& budgets, const std::vector<Leaf_slack_edge>& edges) {
  std::vector<std::vector<size_t>> forward(budgets.size()), reverse(budgets.size());
  auto connect = [&](size_t from, size_t to) {
    forward[from].push_back(to);
    reverse[to].push_back(from);
  };
  for (size_t pin = 1; pin < budgets.size(); ++pin) {
    connect(0, pin);  // Every delay is nonnegative relative to the fixed vertex.
    if (budgets[pin] == 0) {
      connect(pin, 0);
    }
  }
  for (const auto& edge : edges) {
    if (edge.slack_ps <= edge.margin_ps) {
      connect(edge.from, edge.to);
    }
  }

  // Every vertex is reachable from zero. Use explicit stacks for large clocks.
  std::vector<bool>                      seen(budgets.size());
  std::vector<size_t>                    finished;
  std::vector<std::pair<size_t, size_t>> stack{{0, 0}};
  seen[0] = true;
  while (!stack.empty()) {
    auto& [pin, next] = stack.back();
    if (next == forward[pin].size()) {
      finished.push_back(pin);
      stack.pop_back();
      continue;
    }
    const auto child = forward[pin][next++];
    if (!seen[child]) {
      seen[child] = true;
      stack.emplace_back(child, 0);
    }
  }
  std::vector<size_t> group(budgets.size(), budgets.size()), pending;
  for (auto it = finished.rbegin(); it != finished.rend(); ++it) {
    if (group[*it] != budgets.size()) {
      continue;
    }
    group[*it] = *it;
    pending.push_back(*it);
    while (!pending.empty()) {
      const auto pin = pending.back();
      pending.pop_back();
      for (auto parent : reverse[pin]) {
        if (group[parent] == budgets.size()) {
          group[parent] = *it;
          pending.push_back(parent);
        }
      }
    }
  }
  return group;
}
}  // namespace

std::vector<double> solve_leaf_arrivals(const std::vector<double>& budgets, const std::vector<Leaf_slack_edge>& edges) {
  if (budgets.empty() || budgets[0] != 0) {
    throw std::invalid_argument("missing fixed clock vertex");
  }
  for (double b : budgets) {
    if (!std::isfinite(b) || b < 0) {
      throw std::invalid_argument("invalid leaf arrival budget");
    }
  }
  // Components share a time origin, but separate components need not make the
  // same fraction of progress. A blocked cycle must not freeze another domain.
  std::vector<size_t> parent(budgets.size());
  std::iota(parent.begin(), parent.end(), 0);
  auto root = [&](size_t i) {
    while (i != parent[i]) {
      parent[i] = parent[parent[i]];
      i         = parent[i];
    }
    return i;
  };
  for (const auto& e : edges) {
    if (e.from >= budgets.size() || e.to >= budgets.size() || !std::isfinite(e.slack_ps) || !std::isfinite(e.margin_ps)) {
      throw std::invalid_argument("invalid leaf timing edge");
    }
    if (e.from && e.to) {
      parent[root(e.from)] = root(e.to);
    }
  }
  std::map<size_t, std::vector<Leaf_slack_edge>> groups;
  const auto equal_delay = equal_delay_groups(budgets, edges);
  for (auto e : edges) {
    if (e.from != e.to) {
      if (equal_delay[e.from] == equal_delay[e.to]) {
        // Preserve this path's floor while letting other paths in the same
        // connected component make progress on their achievable targets.
        e.margin_ps = std::min(e.slack_ps, e.margin_ps);
      }
      groups[root(e.from ? e.from : e.to)].push_back(e);
    }
  }
  std::vector<double> result(budgets.size(), 0);
  for (const auto& [id, group] : groups) {
    if (std::ranges::none_of(group, [](const auto& e) { return e.slack_ps < e.margin_ps; })) {
      continue;
    }
    std::map<size_t, size_t> index{
        {0, 0}
    };
    std::vector<size_t> global{0};
    auto                local = [&](size_t v) {
      const auto [it, added] = index.try_emplace(v, global.size());
      if (added) {
        global.push_back(v);
      }
      return it->second;
    };
    auto constraints = group;
    for (auto& e : constraints) {
      e.from = local(e.from);
      e.to   = local(e.to);
    }
    // Longest-path relaxation gives the least nonnegative delays satisfying
    // x[to] >= x[from] + required gain. A positive cycle is infeasible.
    const auto feasible = [&](double fraction) {
      std::vector<double> x(global.size(), 0);
      for (size_t iteration = 0; iteration < global.size(); ++iteration) {
        bool changed = false;
        for (const auto& e : constraints) {
          const double gap     = e.margin_ps - e.slack_ps;
          const double arrival = x[e.from] + (gap > 0 ? fraction * gap : gap);
          if (arrival <= x[e.to] + 1e-7) {
            continue;
          }
          if (arrival > budgets[global[e.to]] + 1e-7) {
            return false;
          }
          x[e.to] = arrival;
          changed = true;
        }
        if (!changed) {
          for (size_t i = 1; i < x.size(); ++i) {
            result[global[i]] = x[i];
          }
          return true;
        }
      }
      return false;
    };
    if (feasible(1)) {
      continue;
    }
    double lo = 0, hi = 1;
    for (unsigned i = 0; i < 22; ++i) {
      double mid = (lo + hi) / 2;
      if (feasible(mid)) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
  }
  return result;
}

std::map<uint64_t, unsigned> sibling_hold_drives(const Schedule_leaf& target, const std::map<Pin_label, Schedule_leaf>& captures,
                                                 const Endpoint_hold& before, const Endpoint_hold& after, double hold_margin_ps) {
  std::map<uint64_t, unsigned> drives;
  if (target.parent.empty() || target.fixed || before.size() != after.size()) {
    return drives;
  }
  for (const auto& [pin, slack] : before) {
    const auto trial = after.find(pin);
    if (trial == after.end()) {
      return {};
    }
    if (std::min<double>(slack, hold_margin_ps) - trial->second <= slack_epsilon_ps) {
      continue;
    }
    const auto capture = captures.find(pin);
    if (capture == captures.end()) {
      return {};
    }
    const auto& leaf = capture->second;
    if (leaf.fixed || leaf.parent != target.parent || leaf.anchor == target.anchor || leaf.steps == std::numeric_limits<unsigned>::max()) {
      return {};
    }
    drives.emplace(leaf.anchor, leaf.steps + 1);
  }
  return drives;
}

static void record_violation(std::optional<Clock_bound_violation>& worst, const Pin_label& pin, double reference, double trial,
                             double limit, double excess) {
  if (!worst || excess > worst->excess_ps) {
    worst = Clock_bound_violation{pin, reference, trial, limit, excess};
  }
}

// The timing graph is rebuilt for every trial. Compare the same physical pins,
// domains and corners. Global WNS and TNS cannot detect one hold failure replacing another.
Clock_bound_check check_clock_bounds(const Endpoint_hold& current_hold, const Endpoint_hold& trial_hold,
                                     const Sink_arrivals& original, const Sink_arrivals& arrivals, double period_fraction,
                                     double hold_margin_ps) {
  Clock_bound_check check;
  if (current_hold.size() != trial_hold.size() || original.size() != arrivals.size()) {
    return check;
  }
  for (const auto& [pin, prior] : current_hold) {
    const auto it = trial_hold.find(pin);
    if (it == trial_hold.end()) {
      return check;
    }
    const auto floor     = std::min<double>(prior, hold_margin_ps);
    const auto shortfall = floor - it->second;
    if (shortfall > slack_epsilon_ps) {
      record_violation(check.hold, pin, prior, it->second, floor, shortfall);
    }
  }
  for (const auto& [pin, base] : original) {
    const auto it = arrivals.find(pin);
    if (it == arrivals.end() || it->second.clock != base.clock || it->second.period_ps != base.period_ps
        || it->second.arrivals.size() != base.arrivals.size()) {
      return check;
    }
    const auto limit = base.period_ps * period_fraction;
    for (size_t i = 0; i < base.arrivals.size(); ++i) {
      const double delta = std::abs(static_cast<double>(it->second.arrivals[i]) - base.arrivals[i]);
      if (!std::isfinite(delta)) {
        return check;
      }
      check.max_shift_ps = std::max(check.max_shift_ps, delta);
      if (delta - limit > slack_epsilon_ps) {
        record_violation(check.offset, pin, base.arrivals[i], it->second.arrivals[i], limit, delta - limit);
      }
    }
  }
  check.same_coverage = true;
  return check;
}
}  // namespace livehd::placement
