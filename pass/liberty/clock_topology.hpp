// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace livehd::liberty {
enum class Clock_cell_kind { other, wire, latch, gate, flip_flop };
inline constexpr uint32_t no_clock_id              = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t conflicting_clock_driver = no_clock_id - 1;
struct Clock_path_step {
  uint32_t net, cell, input;
  bool     inverted;
};
struct Clock_input_path {
  std::vector<Clock_path_step> steps;
  uint32_t                     driver = no_clock_id;
  bool                         valid  = false;
};

// Start at physical clock loads. A stored enable may gate a clock when its latch
// is transparent in the inactive phase, or its FF samples on the edge entering
// that phase. Both paths must trace back to the same clock source.
// Input numbers index the master's ordered clock shape, independent of the netlist's pin numbering.
template <class Driver, class Kind, class Inputs, class Net, class Polarity, class Level, class Visit>
void visit_discrete_clock_gates(const std::vector<bool>& clocks, Driver driver, Kind kind, Inputs inputs, Net net,
                                Polarity polarity, Level level, Visit visit) {
  const auto walk = [&](uint32_t cell, uint32_t input) {
    Clock_input_path   path;
    std::set<uint32_t> seen;
    bool               inverted = false;
    for (;;) {
      const uint32_t n = net(cell, input);
      if (n == no_clock_id || !seen.insert(n).second) {
        return path;
      }
      const uint32_t next = driver(n);
      if (next == conflicting_clock_driver) {
        return path;
      }
      path.steps.push_back({n, cell, input, inverted});
      if (next == no_clock_id || kind(next) != Clock_cell_kind::wire) {
        path.driver = next;
        path.valid  = true;
        return path;
      }
      inverted ^= polarity(next);
      cell      = next;
      input     = 0;
    }
  };
  std::vector<uint32_t> pending;
  std::vector<bool>     visited(clocks.size());
  for (uint32_t n = 0; n < clocks.size(); ++n) {
    if (clocks[n]) {
      pending.push_back(n);
    }
  }
  while (!pending.empty()) {
    const auto n = pending.back();
    pending.pop_back();
    if (visited[n]) {
      continue;
    }
    visited[n]          = true;
    const uint32_t gate = driver(n);
    if (gate == no_clock_id || gate == conflicting_clock_driver) {
      continue;
    }
    if (kind(gate) == Clock_cell_kind::wire && net(gate, 0) != no_clock_id) {
      pending.push_back(net(gate, 0));
    }
    if (kind(gate) != Clock_cell_kind::gate) {
      continue;
    }
    for (uint32_t ctl = 0; ctl < inputs(gate); ++ctl) {
      const auto en = walk(gate, ctl);
      if (!en.valid || en.driver == no_clock_id
          || (kind(en.driver) != Clock_cell_kind::latch && kind(en.driver) != Clock_cell_kind::flip_flop)) {
        continue;
      }
      const auto lp = walk(en.driver, 0);
      if (!lp.valid) {
        continue;
      }
      for (uint32_t ck = 0; ck < inputs(gate); ++ck) {
        if (ck == ctl) {
          continue;
        }
        const auto cp = walk(gate, ck);
        if (!cp.valid || cp.steps.back().net != lp.steps.back().net) {
          continue;
        }
        const bool inactive_low = polarity(en.driver) ^ lp.steps.back().inverted ^ cp.steps.back().inverted;
        if (level(gate, ck) != inactive_low) {
          continue;
        }
        visit(n, gate, ck, ctl, en, cp, lp);
        pending.push_back(cp.steps.back().net);
        break;
      }
    }
  }
}
}  // namespace livehd::liberty
