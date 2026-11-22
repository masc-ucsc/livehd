// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_tree.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "pass/place/steiner_net.hpp"
#include "pass/timing/wire.hpp"

namespace livehd::pcrs {
void characterize_clock_driver(Clock_driver& driver, const liberty::timing::Cell& cell, std::optional<double> input_slew_ps) {
  driver.arcs.clear();
  driver.reference_slew_ps = 0;
  const auto in = cell.pins.find(driver.input), out = cell.pins.find(driver.output);
  if (in == cell.pins.end() || out == cell.pins.end()) {
    return;
  }
  driver.input_max_slew_ps = in->second.max_transition_ps.value_or(0);
  using liberty::timing::Arc;
  std::array<std::vector<const Arc*>, 2> edges;
  for (const auto& arc : out->second.arcs) {
    if (arc.related_pin != driver.input
        || (!arc.type.starts_with("combinational") && arc.type != "rising_edge" && arc.type != "falling_edge")
        || !arc.unsupported.empty()) {
      continue;
    }
    for (size_t t = 0; t < edges.size(); ++t) {
      if (arc.delay[t] && arc.slew[t]) {
        edges[t].push_back(&arc);
      }
    }
  }
  if (edges[0].empty() || edges[1].empty()) {
    return;
  }
  const auto&            thresholds = cell.thresholds;
  // A FO4 buffer driven by another FO4 buffer gives a library-scaled reference
  // before the parent's size is known. Final STA propagates its actual input waveform.
  liberty::timing::Query query{0, 4 * driver.input_ff};
  for (int stage = 0; !input_slew_ps && stage < 2; ++stage) {
    double slew = 0;
    for (size_t t = 0; t < edges.size(); ++t) {
      for (const auto* arc : edges[t]) {
        slew = std::max(slew, arc->slew[t]->lookup(query));
      }
    }
    query.input_slew_ps = slew;
  }
  query.input_slew_ps      = input_slew_ps.value_or(query.input_slew_ps);
  driver.reference_slew_ps = query.input_slew_ps;
  for (size_t t = 0; t < edges.size(); ++t) {
    const auto factor
        = std::log(t == 0 ? (1 - thresholds.lower[t]) / (1 - thresholds.upper[t]) : thresholds.upper[t] / thresholds.lower[t])
          / thresholds.slew_derate;
    for (const auto* arc : edges[t]) {
      driver.arcs.push_back({arc->delay[t]->output_load_slice(query), arc->slew[t]->output_load_slice(query), factor});
    }
  }
}

namespace {
struct Load {
  size_t id;
  // Use the name of the first logical sink as a deterministic position tie breaker across levels.
  size_t first;
  double x, y, cap, max_slew;
};
struct Bounds {
  double xl, yl, xh, yh;
};

}  // namespace
struct Clock_plan_cache::State {
  const std::vector<Clock_driver>*                           library = nullptr;
  Clock_limits                                               limits{};
  bool                                                       reuse_regions = false;
  std::vector<Clock_sink>                                    membership;
  Clock_tree                                                 groups;
  std::map<std::string, double, std::less<>>                 leaf_wire_skew_ps;
  std::vector<Load>                                          top;
  size_t                                                     first_leaf = 0;
  bool                                                       has_flops  = false;
  std::map<std::vector<double>, std::optional<Clock_branch>> fits;
};
namespace {
enum class Plan_depth { leaves, tree };

class Planner {
public:
  Planner(const std::vector<Clock_sink>& s, const std::vector<Clock_driver>& d, const Clock_limits& l, Clock_plan_cache* session)
      : sinks(s), drivers(d), limits(l), cache(session) {}

  Clock_tree run(const std::optional<Clock_source>& source, Plan_depth depth, bool reuse_regions = false) {
    std::vector<Load>             ordinary, top;
    std::unordered_set<Pin_label> names;
    std::map<Pin_label, Name>     fork_homes;
    for (size_t i = 0; i < sinks.size(); ++i) {
      const auto& s = sinks[i];
      if (s.name.empty() || !names.insert(s.name).second || !std::isfinite(s.x_um) || !std::isfinite(s.y_um)
          || !nonnegative(s.cap_ff) || !nonnegative(s.max_slew_ps) || !s.valid_downstream()
          || !nonnegative(s.insertion_ps) || !std::isfinite(s.wanted_ps) || !nonnegative(s.leaf_delay_ps)) {
        throw std::invalid_argument("invalid or duplicate clock sink " + s.name);
      }
      if (s.previous_position
          && (!std::isfinite(s.previous_position->first) || !std::isfinite(s.previous_position->second)
              || s.coupled_group.empty())) {
        throw std::invalid_argument("invalid previous position for clock-gate fork " + s.name);
      }
      if (!s.coupled_group.empty()) {
        const auto [owner, fresh] = fork_homes.emplace(s.coupled_group, s.chunk);
        if (!fresh && owner->second != s.chunk) {
          throw Clock_infeasible("discrete gate fork crosses clock chunks: " + s.coupled_group);
        }
        if (s.macro) {
          throw std::invalid_argument("a macro cannot belong to a discrete clock-gate fork");
        }
        auto [it, created] = groups.try_emplace(s.coupled_group, Bounds{s.x_um, s.y_um, s.x_um, s.y_um});
        auto& b            = it->second;
        b.xl               = std::min(b.xl, s.x_um);
        b.xh               = std::max(b.xh, s.x_um);
        b.yl               = std::min(b.yl, s.y_um);
        b.yh               = std::max(b.yh, s.y_um);
      }
      ordinary.push_back({i, i, s.x_um, s.y_um, s.cap_ff, s.max_slew_ps});
    }
    size_t                  first_leaf = 0;
    bool                    has_flops  = false;
    std::vector<Clock_sink> membership;
    if (cache) {
      membership = sinks;
      for (auto& sink : membership) {
        sink.drive_steps.reset();
        sink.leaf_delay_ps = 0;
      }
      if (!cache->state || cache->state->library != &drivers || cache->state->limits != limits
          || cache->state->membership != membership || cache->state->reuse_regions != reuse_regions) {
        cache->state                = std::make_shared<Clock_plan_cache::State>();
        cache->state->library       = &drivers;
        cache->state->limits        = limits;
        cache->state->reuse_regions = reuse_regions;
        cache->state->membership    = std::move(membership);
      }
    }
    if (cache && !cache->state->top.empty()) {
      tree              = cache->state->groups;
      top               = cache->state->top;
      first_leaf        = cache->state->first_leaf;
      has_flops         = cache->state->has_flops;
      leaf_wire_skew_ps = cache->state->leaf_wire_skew_ps;
      ++cache->grouping_hits;
    } else {
      std::ranges::sort(ordinary, [&](const Load& a, const Load& b) { return sinks[a.id].name < sinks[b.id].name; });
      // A hard macro has its own insertion delay and arrival target. Never share its leaf driver with a flop.
      using Home = std::pair<std::string_view, double>;
      std::map<Home, std::vector<Load>>                    flop_groups;
      // The band starts at the earliest effective arrival in it. Comparing
      // against that start bounds the full spread even for a chain of near ties.
      std::map<std::string_view, std::map<double, double>> bands;
      const auto tail = [](const Clock_sink& pin) { return pin.downstream_ps + pin.insertion_ps - pin.wanted_ps; };
      if (limits.balance) {
        double smallest_stage = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < drivers.size(); ++i) {
          const Load input{0, 0, 0, 0, drivers[i].input_ff, drivers[i].input_max_slew_ps};
          const auto stage = fit_at(std::span(&input, 1), 0, 0, false, i);
          if (stage && stage->delay_ps > 0) {
            smallest_stage = std::min(smallest_stage, stage->delay_ps);
          }
        }
        for (const auto& load : ordinary) {
          const auto& sink = sinks[load.id];
          if (!sink.macro) {
            bands[sink.chunk].try_emplace(tail(sink));
          }
        }
        for (auto& [chunk, targets] : bands) {
          double start = targets.begin()->first;
          for (auto& [value, band] : targets) {
            if (value - start > smallest_stage / 2) {
              start = value;
            }
            band = start;
          }
          if (start != targets.begin()->first) {
            leaf_wire_skew_ps.emplace(chunk, smallest_stage / 2);
          }
        }
      }
      for (const auto& load : ordinary) {
        const auto& sink = sinks[load.id];
        double      band = 0;
        if (limits.balance && !sink.macro) {
          const auto&  targets = bands.at(sink.chunk);
          const double first   = targets.begin()->first;
          // Keep all pins of a discrete gate fork together.
          band                 = sink.coupled_group.empty() ? targets.at(tail(sink)) : first;
        }
        if (sink.macro || (sink.separate_leaf && sink.coupled_group.empty())) {
          std::vector<Load> singleton{load};
          partition(singleton, 0, 1, true, top);
        } else {
          flop_groups[{sink.chunk, band}].push_back(load);
        }
      }
      first_leaf = top.size();
      std::vector<size_t> group_ends;
      for (auto& [home, flops] : flop_groups) {
        if (reuse_regions && std::ranges::any_of(flops, [&](const auto& p) { return sinks[p.id].previous_position.has_value(); })) {
          partition_moved_phases(flops, top);
          tree.used_previous_leaf_regions = true;
        } else {
          partition(flops, 0, flops.size(), true, top);
        }
        group_ends.push_back(top.size());
      }
      if (!flop_groups.empty()) {
        if (depth == Plan_depth::tree) {
          tree.initial_leaves = leaf_metrics(first_leaf);
        }
        refine_homes(top, first_leaf, group_ends);
      }
      has_flops = !flop_groups.empty();
      if (cache) {
        cache->state->groups            = tree;
        cache->state->top               = top;
        cache->state->first_leaf        = first_leaf;
        cache->state->has_flops         = has_flops;
        cache->state->leaf_wire_skew_ps = leaf_wire_skew_ps;
      }
    }
    // Refit the scheduled leaves and pass their actual input loads into trunk sizing.
    for (size_t i = 0; i < top.size();) {
      const auto&             leaf = tree.branches[top[i].id];
      std::optional<unsigned> steps;
      bool                    conflict = false;
      for (const auto id : leaf.children) {
        const auto& sink = sinks[id];
        if (sink.drive_steps) {
          if (steps && steps != sink.drive_steps) {
            if (!limits.balance && !limits.split_scheduled_leaves) {
              throw std::invalid_argument("conflicting clock leaf schedule at " + sink.name);
            }
            conflict = true;
            break;
          }
          steps = sink.drive_steps;
        }
      }
      if (conflict) {
        split_scheduled_leaf(top, i);
        continue;  // Apply the requested drive to each replacement leaf, including this slot.
      }
      auto& load = top[i++];
      if (!steps || *steps == leaf.drive_steps) {
        continue;
      }
      auto members = leaf_loads(leaf);
      auto fit     = fit_at(members, leaf.x_um, leaf.y_um, true, {}, nullptr, steps);
      if (!fit || fit->drive_steps != *steps) {
        throw Clock_drive_unavailable("unavailable clock leaf drive step at " + sinks[load.first].name);
      }
      load = store_branch(members, std::move(*fit), load.id);
    }
    if (depth == Plan_depth::leaves) {
      return std::move(tree);
    }
    if (has_flops) {
      tree.refined_leaves = leaf_metrics(first_leaf);
    }
    if (limits.balance) {
      for (const auto& b : tree.branches) {
        const auto loads = leaf_loads(b);
        intervals.push_back(arrival_interval(b, loads));
      }
    }
    for (auto& load : top) {
      double requested = 0;
      for (auto member : tree.branches.at(load.id).children) {
        requested = std::max(requested, sinks[member].leaf_delay_ps);
      }
      // Each stage drives the actual input capacitance of the preceding stage.
      // Stop at the closest realizable arrival without consuming a site for zero gain.
      while (requested > 1e-6 && tree.schedule_buffers < limits.repeater_sites) {
        const auto                  member = std::span(&load, 1);
        std::optional<Clock_branch> best;
        double                      error = requested;
        for (size_t driver = 0; driver < drivers.size(); ++driver) {
          auto stage = fit_at(member, load.x, load.y, false, driver);
          if (!stage || stage->delay_ps <= 1e-6) {
            continue;
          }
          const auto residual = std::abs(requested - stage->delay_ps);
          if (residual + 1e-6 < error) {
            error = residual;
            best  = std::move(stage);
          }
        }
        if (!best) {
          break;
        }
        requested -= best->delay_ps;
        load       = store_branch(member, std::move(*best), tree.branches.size());
        ++tree.schedule_buffers;
      }
    }
    if (std::ranges::any_of(top, [&](const Load& p) { return !sinks[p.first].chunk.empty(); })) {
      std::map<std::string_view, std::vector<Load>> homes;
      for (const auto& load : top) {
        const auto& home = sinks[load.first].chunk;
        if (home.empty()) {
          throw std::invalid_argument("missing clock chunk for " + sinks[load.first].name);
        }
        homes[home].push_back(load);
      }
      // A clock generated inside the only home it serves already has an entry
      // at its source pin. Preserve the ordinary local tree and drive choices.
      if (!source || homes.size() != 1 || homes.begin()->first != source->chunk) {
        top.clear();
        for (auto& [home, branches] : homes) {
          while (branches.size() > 1) {
            combine(branches);
          }
          tree.chunk_entries.emplace_back(home, branches.front().id);
          top.push_back(branches.front());
        }
      }
    }
    while (top.size() > 1) {
      if (limits.allow_source_fork && source && source->direct.empty() && top.size() <= limits.fanout
          && faster_direct_fork(top, *source)) {
        break;
      }
      combine(top);
    }
    if (source && limits.allow_source_fork && source->direct.empty() && top.size() == 1) {
      const auto& root        = tree.branches.at(top.front().id);
      const bool  chunk_entry = tree.chunk_entries.size() == 1 && tree.chunk_entries.front().second == top.front().id;
      // Within a home, a unary stage can implement a requested leaf delay.
      if (!root.leaf && (root.children.size() > 1 || chunk_entry)) {
        size_shared_root(top.front(), *source);
      }
    }
    if (source && (!top.empty() || !source->direct.empty())) {
      for (const auto& s : source->direct) {
        if (s.name.empty() || !names.insert(s.name).second || !std::isfinite(s.x_um) || !std::isfinite(s.y_um)
            || !nonnegative(s.cap_ff) || !nonnegative(s.max_slew_ps) || !s.valid_downstream()
            || !nonnegative(s.insertion_ps) || !std::isfinite(s.wanted_ps)) {
          throw std::invalid_argument("invalid direct clock sink " + s.name);
        }
        direct_loads.push_back({0, 0, s.x_um - source->x_um, s.y_um - source->y_um, s.cap_ff, s.max_slew_ps});
        if (s.max_slew_ps > 0) {
          direct_slew = std::min(direct_slew, s.max_slew_ps);
        }
      }

      const auto source_fit = [&] { return fit_at(top, source->x_um, source->y_um, false, {}, &source->driver); };
      auto       trunk      = source_fit();
      if (!trunk && top.empty()) {
        throw Clock_infeasible("existing source exceeds electrical limits on retained clock path to "
                               + source->direct.front().name);
      }
      if (!trunk) {
        const auto original_count = tree.wire_repeaters;
        top.front()               = bridge(top, source->x_um, source->y_um, &source->driver);
        tree.source_repeaters     = tree.wire_repeaters - original_count;
        const auto& head          = tree.branches.at(top.front().id);
        const auto& next          = tree.branches.at(head.children.front());
        tree.source_pair_available
            = source->direct.empty() && !next.leaf && (tree.source_repeaters > 1 || next.children.size() > 1);
        if (tree.source_pair_available && (limits.balance || limits.source_drive_stages)) {
          size_source_pair(top.front(), *source, limits.balance || limits.source_drive_stages > 1);
        }
        trunk = source_fit();
        if (!trunk) {
          throw Clock_infeasible("unrealizable PCRS source wire to " + sinks[top.front().first].name);
        }
      }
      tree.source_cell_delay_ps = trunk->cell_delay_ps;
      tree.source_wire_um       = trunk->wire_um;
      tree.source_load_ff       = trunk->load_ff;
      tree.source_far_slew_ps   = trunk->far_slew_ps;
    }
    for (const auto& root : top) {
      tree.roots.push_back(root.id);
    }
    measure_arrivals(source);
    Interval effective;
    const auto include = [&](const auto& pins, const auto& arrivals) {
      for (size_t i = 0; i < pins.size(); ++i) {
        const auto& pin = pins[i];
        effective.lo    = std::min(effective.lo, arrivals.at(i) + pin.tail_lo_ps());
        effective.hi    = std::max(effective.hi, arrivals.at(i) + pin.tail_hi_ps());
      }
    };
    include(sinks, tree.arrival_ps);
    if (source) {
      include(source->direct, tree.retained_arrival_ps);
    }
    if (effective.lo <= effective.hi) {
      tree.target_skew_ps = effective.spread();
    }
    return std::move(tree);
  }

  static bool nonnegative(double x) { return std::isfinite(x) && x >= 0; }

private:
  const std::vector<Clock_sink>&             sinks;
  const std::vector<Clock_driver>&           drivers;
  const Clock_limits&                        limits;
  Clock_tree                                 tree;
  Clock_plan_cache*                          cache;
  std::map<Pin_label, Bounds>                groups;
  std::map<std::string, double, std::less<>> leaf_wire_skew_ps;
  struct Interval {
    double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
    double spread() const { return hi - lo; }
  };
  mutable timing::fast::Elmore wire_solver;
  std::vector<Interval> intervals;
  std::optional<double>        quantum_ps;
  std::vector<Load>     direct_loads;
  double                direct_slew = std::numeric_limits<double>::infinity();

  // The delay of the smallest stage the library can build into one input. Subtrees closer than half of it in
  // effective arrival are one band: padding below a stage's resolution costs a buffer for no gain.
  double stage_quantum() {
    if (quantum_ps) {
      return *quantum_ps;
    }
    double smallest = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < drivers.size(); ++i) {
      const Load input{0, 0, 0, 0, drivers[i].input_ff, drivers[i].input_max_slew_ps};
      const auto stage = fit_at(std::span(&input, 1), 0, 0, false, i);
      if (stage && stage->delay_ps > 0) {
        smallest = std::min(smallest, stage->delay_ps);
      }
    }
    quantum_ps = std::isfinite(smallest) ? smallest : 0.;
    return *quantum_ps;
  }

  // Merge by arrival, then by place. Subtrees whose sinks arrive at different times (a clock gate with its own
  // subtree below it, a macro with an insertion delay) are kept apart until each band is one root, and the band roots
  // are joined last, where the early ones are padded once. Mixing them in a spatial fork pads every early arm of the
  // fork separately.
  void combine(std::vector<Load>& top) {
    if (limits.balance && top.size() > 1 && intervals.size() >= tree.branches.size()) {
      const double        quantum = std::max(stage_quantum() / 2, limits.skew_bound_ps);
      std::vector<size_t> order(top.size());
      std::iota(order.begin(), order.end(), 0);
      std::ranges::sort(order, [&](size_t a, size_t b) {
        return std::tuple{intervals[top[a].id].hi, top[a].id} < std::tuple{intervals[top[b].id].hi, top[b].id};
      });
      std::vector<std::vector<Load>> bands;
      double                         start = 0;
      for (const auto i : order) {
        const double hi = intervals[top[i].id].hi;
        if (bands.empty() || hi - start > quantum) {
          bands.emplace_back();
          start = hi;
        }
        bands.back().push_back(top[i]);
      }
      if (bands.size() > 1 && bands.size() < top.size() && quantum > 0) {
        std::vector<Load> roots;
        for (auto& band : bands) {
          while (band.size() > 1) {
            combine_by_place(band);
          }
          roots.push_back(band.front());
        }
        top = std::move(roots);
        if (top.size() <= limits.fanout) {
          std::vector<Load> next;
          partition(top, 0, top.size(), false, next);
          top = std::move(next);
        }
        return;
      }
    }
    combine_by_place(top);
  }

  void combine_by_place(std::vector<Load>& top) {
    std::vector<Load> next;
    partition(top, 0, top.size(), false, next);
    if (next.size() >= top.size()) {
      throw Clock_infeasible("clock buffers cannot drive two branches within electrical limits");
    }
    top = std::move(next);
  }

  struct Root_fit {
    Clock_branch branch;
    double       delay_ps;
  };

  // The first repeater and the stage it drives are shared by every buffered sink.
  // Price the two together: speeding up the second stage also loads the first.
  void size_source_pair(Load& entry, const Clock_source& source, bool size_next) {
    auto&             head = tree.branches.at(entry.id);
    auto&             next = tree.branches.at(head.children.at(0));
    std::vector<Load> loads;
    for (auto id : next.children) {
      const auto& branch = tree.branches.at(id);
      const auto& driver = drivers[branch.driver];
      loads.push_back({id, entry.first, branch.x_um, branch.y_um, driver.input_ff, driver.input_max_slew_ps});
    }
    const auto candidates = [&](size_t current) {
      const double area  = drivers[current].area_um2;
      double       upper = std::numeric_limits<double>::infinity();
      for (const auto& driver : drivers) {
        if (driver.area_um2 > area) {
          upper = std::min(upper, driver.area_um2);
        }
      }
      std::vector<size_t> choices{current};
      for (size_t i = 0; i < drivers.size(); ++i) {
        if (i != current && drivers[i].area_um2 >= area && drivers[i].area_um2 <= upper) {
          choices.push_back(i);
        }
      }
      return choices;
    };
    const auto   heads     = candidates(head.driver);
    Clock_branch best_head = head, best_next = next;
    const auto   input = [&](const Clock_branch& b, size_t id) {
      return Load{id, entry.first, b.x_um, b.y_um, drivers[b.driver].input_ff, drivers[b.driver].input_max_slew_ps};
    };
    std::pair  best_cost{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    const auto children = size_next ? candidates(next.driver) : std::vector<size_t>{next.driver};
    for (auto j : children) {
      const auto child = fit_at(loads, next.x_um, next.y_um, false, j);
      if (!child) {
        continue;
      }
      const std::array child_input{input(*child, head.children.front())};
      for (auto i : heads) {
        const auto parent = fit_at(child_input, head.x_um, head.y_um, false, i);
        if (!parent) {
          continue;
        }
        const std::array parent_input{input(*parent, entry.id)};
        const auto       upstream = fit_at(parent_input, source.x_um, source.y_um, false, {}, &source.driver);
        if (!upstream || (limits.source_fork_cap_ff && upstream->load_ff > *limits.source_fork_cap_ff)) {
          continue;
        }
        const std::pair cost{upstream->delay_ps + parent->delay_ps + child->delay_ps, drivers[i].area_um2 + drivers[j].area_um2};
        if (cost < best_cost) {
          best_cost = cost;
          best_head = *parent;
          best_next = *child;
        }
      }
    }
    best_head.children = std::move(head.children);
    best_next.children = std::move(next.children);
    head               = std::move(best_head);
    next               = std::move(best_next);
    entry              = input(head, entry.id);
  }

  std::optional<Root_fit> fit_shared_root(std::span<const Load> children, double x, double y, const Clock_source& source) const {
    std::optional<Root_fit> best;
    for (size_t size = 0; size < drivers.size(); ++size) {
      const auto stage = fit_at(children, x, y, false, size);
      if (!stage) {
        continue;
      }
      const auto& d = drivers[size];
      const Load  input{0, children.front().first, x, y, d.input_ff, d.input_max_slew_ps};
      const auto  upstream = fit_at(std::span(&input, 1), source.x_um, source.y_um, false, {}, &source.driver);
      if (!upstream || (limits.source_fork_cap_ff && upstream->load_ff > *limits.source_fork_cap_ff)) {
        continue;
      }
      const double delay = upstream->delay_ps + stage->delay_ps;
      if (!best
          || std::tie(delay, d.area_um2, d.master)
                 < std::tie(best->delay_ps, drivers[best->branch.driver].area_um2, drivers[best->branch.driver].master)) {
        best = Root_fit{*stage, delay};
      }
    }
    return best;
  }

  void size_shared_root(Load& entry, const Clock_source& source) {
    auto&             root = tree.branches.at(entry.id);
    std::vector<Load> children;
    for (auto child : root.children) {
      const auto& b = tree.branches.at(child);
      const auto& d = drivers.at(b.driver);
      children.push_back({child, entry.first, b.x_um, b.y_um, d.input_ff, d.input_max_slew_ps});
    }
    if (auto best = fit_shared_root(children, root.x_um, root.y_um, source)) {
      best->branch.children = std::move(root.children);
      root                  = std::move(best->branch);
      if (limits.balance) {
        intervals[entry.id] = arrival_interval(root, children);
      }
      entry.cap      = drivers[root.driver].input_ff;
      entry.max_slew = drivers[root.driver].input_max_slew_ps;
    }
  }

  bool faster_direct_fork(std::span<const Load> top, const Clock_source& source) const {
    const auto direct = fit_at(top, source.x_um, source.y_um, false, {}, &source.driver);
    if (!direct || (limits.source_fork_cap_ff && direct->load_ff > *limits.source_fork_cap_ff)) {
      return false;
    }
    const auto trunk = fit(top, bounds(top), false);
    if (!trunk) {
      return true;
    }
    const auto buffered = fit_shared_root(top, trunk->x_um, trunk->y_um, source);
    return !buffered || direct->delay_ps < buffered->delay_ps;
  }

  void measure_arrivals(const std::optional<Clock_source>& source) {
    std::vector<double> inputs(tree.branches.size());
    const auto          branch_input = [&](size_t id) {
      const auto& b = tree.branches[id];
      const auto& d = drivers[b.driver];
      return Load{id, 0, b.x_um, b.y_um, d.input_ff, d.input_max_slew_ps};
    };
    if (source && (!tree.roots.empty() || !direct_loads.empty())) {
      std::vector<Load> roots;
      for (auto id : tree.roots) {
        roots.push_back(branch_input(id));
      }
      const auto wire = solve_wire(roots, source->x_um, source->y_um, true, false);
      for (size_t i = 0; i < roots.size(); ++i) {
        inputs[roots[i].id] = wire.delays[i];
      }
      tree.retained_arrival_ps.assign(wire.delays.begin() + roots.size(), wire.delays.end());
    }
    tree.arrival_ps.resize(sinks.size());
    for (size_t i = tree.branches.size(); i-- > 0;) {
      auto& b            = tree.branches[i];
      b.input_arrival_ps = inputs[i];
      std::vector<Load> loads;
      if (b.leaf) {
        loads = leaf_loads(b);
      } else {
        for (auto child : b.children) {
          loads.push_back(branch_input(child));
        }
      }
      const auto   wire   = solve_wire(loads, b.x_um, b.y_um, false, b.leaf);
      const double output = inputs[i] + b.cell_delay_ps;
      for (size_t j = 0; j < loads.size(); ++j) {
        (b.leaf ? tree.arrival_ps : inputs)[loads[j].id] = output + wire.delays[j];
      }
      if (b.leaf) {
        tree.max_leaf_skew_ps = std::max(tree.max_leaf_skew_ps, wire.max_delay - std::ranges::min(wire.delays));
      }
    }
  }

  struct Wire_load {
    double              length = 0, cap = 0, max_delay = 0, max_wire_slew = 0;
    std::vector<double> delays;
  };

  Wire_load solve_wire(std::span<const Load> loads, double x, double y, bool existing, bool leaf) const {
    const auto dbu = limits.dbu_per_um;
    const auto base
        = limits.directional_rc.value_or(std::array{limits.wire_ohm_um, limits.wire_ff_um, limits.wire_ohm_um, limits.wire_ff_um});
    const auto                             rc = existing ? base : (leaf ? limits.leaf_rc : limits.trunk_rc).value_or(base);
    const size_t pin_count = 1 + loads.size() + (existing ? direct_loads.size() : 0);
    std::vector<std::pair<double, double>> pins;
    std::vector<float> cap;
    pins.reserve(pin_count);
    cap.reserve(pin_count);
    const auto append = [&](double px, double py, double ff) {
      pins.emplace_back(px * dbu, py * dbu);
      cap.push_back(ff);
    };
    append(x, y, 0);
    for (const auto& p : loads) {
      append(p.x, p.y, p.cap);
    }
    if (existing) {
      for (const auto& p : direct_loads) {
        append(x + p.x, y + p.y, p.cap);
      }
    }
    const auto net = placement::steiner_net(pins, 0, rc, dbu);
    cap.resize(net.node_cap_ff.size());
    for (size_t i = 0; i < cap.size(); ++i) {
      cap[i] += net.node_cap_ff[i];
    }
    std::vector<timing::fast::Wire_edge> edges;
    edges.reserve(net.edges.size());
    for (const auto& e : net.edges) {
      edges.push_back({e.from, e.to, static_cast<float>(e.resistance_ohm)});
    }
    if (!wire_solver.solve(cap, edges)) {
      throw std::logic_error("PCRS Steiner RC is not a tree");
    }
    Wire_load wire;
    wire.length = net.wire_length_um;
    wire.cap    = wire_solver.total_cap_ff();
    wire.delays.reserve(pins.size() - 1);
    float max_impulse_ps2 = 0;
    for (size_t i = 1; i < pins.size(); ++i) {
      const double delay = wire_solver.delay_ps(i);
      wire.delays.push_back(delay);
      wire.max_delay     = std::max(wire.max_delay, delay);
      max_impulse_ps2 = std::max(max_impulse_ps2, wire_solver.impulse_ps2(i));
    }
    wire.max_wire_slew = std::sqrt(double(max_impulse_ps2));
    return wire;
  }

  Interval arrival_interval(const Clock_branch& branch, std::span<const Load> loads,
                            std::optional<std::pair<size_t, Interval>> substitute = {}) const {
    const auto wire = solve_wire(loads, branch.x_um, branch.y_um, false, branch.leaf);
    Interval   result;
    for (size_t i = 0; i < loads.size(); ++i) {
      const auto   id    = loads[i].id;
      const double delay = branch.cell_delay_ps + wire.delays[i];
      Interval     tail;
      if (branch.leaf) {
        tail.lo = sinks[id].tail_lo_ps();
        tail.hi = sinks[id].tail_hi_ps();
      } else {
        tail = substitute && substitute->first == i ? substitute->second : intervals.at(id);
      }
      result.lo = std::min(result.lo, delay + tail.lo);
      result.hi = std::max(result.hi, delay + tail.hi);
    }
    return result;
  }

  // Match physically different child subtrees at their common parent. A real buffer
  // on an early arm changes the upstream pin capacitance, so retime that net too.
  void balance_arms(std::span<Load> loads, Clock_branch& parent) {
    if (!limits.balance || loads.size() < 2) {
      return;
    }
    auto before = arrival_interval(parent, loads);
    if (before.spread() <= std::max(1e-6, limits.skew_bound_ps)) {
      return;
    }
    const double target = before.hi;
    const auto   error  = [&](const Clock_branch& stage, const Wire_load& net, std::optional<std::pair<size_t, Interval>> changed) {
      double sum = 0;
      for (size_t i = 0; i < loads.size(); ++i) {
        const auto   range = changed && changed->first == i ? changed->second : intervals[loads[i].id];
        const double delay = stage.cell_delay_ps + net.delays[i];
        const double lo = target - delay - range.lo, hi = target - delay - range.hi;
        sum += lo * lo + hi * hi;
      }
      return sum;
    };
    // A long downstream gate path can take several stages to match. Each accepted
    // stage reduces the error and consumes a site from the common placement budget.
    std::vector<bool> used(loads.size());
    while (tree.wire_repeaters + tree.balance_buffers + tree.schedule_buffers < limits.repeater_sites
           && before.spread() > std::max(1e-6, limits.skew_bound_ps)) {
      struct Choice {
        Clock_branch stub, parent;
        Interval     range;
        double       error;
      };
      std::optional<Choice> best;
      const auto            wire     = solve_wire(loads, parent.x_um, parent.y_um, false, false);
      const auto            baseline = error(parent, wire, {});
      size_t                side     = loads.size();
      for (size_t i = 0; i < loads.size(); ++i) {
        if (!used[i]
            && (side == loads.size()
                || intervals[loads[i].id].lo + wire.delays[i] < intervals[loads[side].id].lo + wire.delays[side])) {
          side = i;
        }
      }
      if (side == loads.size()) {
        break;
      }
      used[side]         = true;
      const auto child   = loads[side];
      auto       members = std::span(&loads[side], 1);
      for (size_t d = 0; d < drivers.size(); ++d) {
        auto stub = fit_at(members, child.x, child.y, false, d);
        if (!stub || stub->delay_ps <= 0 || stub->delay_ps >= 2 * before.spread()) {
          continue;
        }
        auto tail             = intervals[child.id];
        tail.lo              += stub->delay_ps;
        tail.hi              += stub->delay_ps;
        loads[side].cap       = drivers[d].input_ff;
        loads[side].max_slew  = drivers[d].input_max_slew_ps;
        auto fork             = fit_at(loads, parent.x_um, parent.y_um, false);
        if (fork) {
          const auto range      = arrival_interval(*fork, loads, std::pair{side, tail});
          const auto trial_wire = solve_wire(loads, fork->x_um, fork->y_um, false, false);
          const auto cost       = error(*fork, trial_wire, std::pair{side, tail});
          if (range.spread() <= before.spread() + 1e-6 && range.hi <= before.hi + 1e-6 && cost + 1e-6 < baseline
              && (!best
                  || std::tuple{cost, drivers[d].area_um2, drivers[d].master}
                         < std::tuple{best->error, drivers[best->stub.driver].area_um2, drivers[best->stub.driver].master})) {
            best = Choice{*stub, *fork, range, cost};
          }
        }
        loads[side] = child;
      }
      if (!best) {
        continue;
      }
      loads[side] = store_branch(members, std::move(best->stub), tree.branches.size());
      parent      = std::move(best->parent);
      before      = best->range;
      ++tree.balance_buffers;
      std::fill(used.begin(), used.end(), false);
    }
  }

  Clock_tree::Leaf_metrics leaf_metrics(size_t begin) const {
    const auto               leaves = std::span(tree.branches).subspan(begin);
    Clock_tree::Leaf_metrics metrics{leaves.front().delay_ps, leaves.front().delay_ps};
    double                   sum = 0;
    for (const auto& leaf : leaves) {
      metrics.min_delay_ps  = std::min(metrics.min_delay_ps, leaf.delay_ps);
      metrics.max_delay_ps  = std::max(metrics.max_delay_ps, leaf.delay_ps);
      metrics.wire_um      += leaf.wire_um;
      ++metrics.size_histogram[leaf.children.size()];
      ++metrics.driver_histogram[drivers[leaf.driver].master];
      for (auto child : leaf.children) {
        const double distance           = leaf.member_distance_um(sinks[child]);
        metrics.star_wire_um           += distance;
        metrics.max_member_distance_um  = std::max(metrics.max_member_distance_um, distance);
        ++metrics.members;
      }
      sum += leaf.delay_ps;
    }
    const auto mean = sum / leaves.size();
    for (const auto& leaf : leaves) {
      const double deviation   = leaf.delay_ps - mean;
      metrics.delay_stddev_ps += deviation * deviation;
    }
    metrics.delay_stddev_ps = std::sqrt(metrics.delay_stddev_ps / leaves.size());
    return metrics;
  }

  static Bounds bounds(std::span<const Load> loads) {
    Bounds b{loads.front().x, loads.front().y, loads.front().x, loads.front().y};
    for (const auto& p : loads) {
      b.xl = std::min(b.xl, p.x);
      b.yl = std::min(b.yl, p.y);
      b.xh = std::max(b.xh, p.x);
      b.yh = std::max(b.yh, p.y);
    }
    return b;
  }

  // The middle of the bounding box first. When a stage cannot be legal there (a lopsided cluster puts the box middle
  // far from most pins, which is wire and load), try the coordinate medians, the load-weighted centre and, for a
  // small group, each pin's own site, and take the legal one with the least delay. A group that cannot be split,
  // a discrete gate fork, is infeasible only when no site works.
  std::optional<Clock_branch> fit(std::span<const Load> loads, Bounds b, bool leaf) const {
    if (loads.size() > limits.fanout) {
      return {};
    }
    if (auto middle = fit_at(loads, std::midpoint(b.xl, b.xh), std::midpoint(b.yl, b.yh), leaf)) {
      return middle;
    }
    std::vector<std::pair<double, double>> sites;
    std::vector<double>                    xs, ys;
    double                                 weight = 0, cx = 0, cy = 0;
    for (const auto& p : loads) {
      xs.push_back(p.x);
      ys.push_back(p.y);
      const double w  = p.cap > 0 ? p.cap : 1;
      weight         += w;
      cx             += w * p.x;
      cy             += w * p.y;
    }
    const auto median = [](std::vector<double>& v) {
      std::ranges::sort(v);
      return v.size() % 2 ? v[v.size() / 2] : std::midpoint(v[v.size() / 2 - 1], v[v.size() / 2]);
    };
    sites.emplace_back(median(xs), median(ys));
    sites.emplace_back(cx / weight, cy / weight);
    if (loads.size() <= 6) {
      for (const auto& p : loads) {
        sites.emplace_back(p.x, p.y);
      }
    }
    std::optional<Clock_branch> best;
    for (const auto& [x, y] : sites) {
      if (auto stage = fit_at(loads, x, y, leaf); stage && (!best || stage->delay_ps < best->delay_ps)) {
        best = std::move(stage);
      }
    }
    return best;
  }

  std::optional<Clock_branch> fit_at(std::span<const Load> loads, double x, double y, bool leaf,
                                     std::optional<size_t> only_driver = {}, const Clock_driver* existing = nullptr,
                                     std::optional<unsigned> requested_steps = {}) const {
    if (!cache || existing || leaf) {
      return fit_uncached(loads, x, y, leaf, only_driver, existing, requested_steps);
    }
    std::vector<double> key{x,
                            y,
                            only_driver ? double(*only_driver) : -1.,
                            double(requested_steps.value_or(limits.trunk_drive_steps))};
    key.reserve(4 + 4 * loads.size());
    for (const auto& p : loads) {
      key.insert(key.end(), {p.x, p.y, p.cap, p.max_slew});
    }
    auto& fits       = cache->state->fits;
    auto [it, added] = fits.try_emplace(std::move(key));
    if (added) {
      it->second = fit_uncached(loads, x, y, leaf, only_driver, existing, requested_steps);
      ++cache->fitted_stages;
    } else {
      ++cache->reused_stages;
    }
    return it->second;
  }
  std::optional<Clock_branch> fit_uncached(std::span<const Load> loads, double x, double y, bool leaf,
                                           std::optional<size_t> only_driver, const Clock_driver* existing,
                                           std::optional<unsigned> requested_steps) const {
    const unsigned steps  = requested_steps.value_or(leaf ? limits.leaf_drive_steps : existing ? 0 : limits.trunk_drive_steps);
    const auto     fanout = loads.size() + (existing ? direct_loads.size() : 0);
    // The grouping limit bounds what a planned stage fans out to. Pins the existing source keeps (a discrete gate's
    // fork, a retained phase pin) were on it before the tree: they count against its own Liberty limit below, and a
    // source with more of them than the leaf knob is not a reason to fail the build.
    if (loads.size() > limits.fanout) {
      return {};
    }
    double radius = 0, sink_limit = existing ? direct_slew : std::numeric_limits<double>::infinity();
    for (const auto& p : loads) {
      radius = std::max(radius, std::abs(p.x - x) + std::abs(p.y - y));
      if (p.max_slew > 0) {
        sink_limit = std::min(sink_limit, p.max_slew);
      }
    }
    if (leaf && limits.radius_um && radius > limits.radius_um) {
      return {};
    }
    // The stages PCRS inserts honour the clock transition target. The existing source keeps its library limit: its
    // drive is not ours to choose, and a target it cannot meet must not fail the build.
    if (!existing && limits.target_slew_ps > 0) {
      sink_limit = std::min(sink_limit, limits.target_slew_ps);
    }
    const auto                  wire = solve_wire(loads, x, y, existing != nullptr, leaf);
    // A buffer before a leaf cannot correct differences in wire delay between
    // its pins. Keep that spread within the arrival band when fitting a chunk
    // with competing targets. A phase fork must stay physically connected.
    if (leaf && loads.size() > 1 && !leaf_wire_skew_ps.empty()) {
      const auto& first  = sinks[loads.front().id];
      const auto  budget = leaf_wire_skew_ps.find(first.chunk);
      if (budget != leaf_wire_skew_ps.end()) {
        const auto [lo, hi] = std::ranges::minmax(wire.delays);
        if (hi - lo > budget->second && (first.coupled_group.empty() || !std::ranges::all_of(loads, [&](const Load& p) {
                                           return sinks[p.id].coupled_group == first.coupled_group;
                                         }))) {
          return {};
        }
      }
    }
    const double                cap = wire.cap;
    std::optional<Clock_branch> best;
    std::vector<Clock_branch>   candidates;
    const size_t                end = existing ? 1 : only_driver ? *only_driver + 1 : drivers.size();
    for (size_t i = only_driver.value_or(0); i < end; ++i) {
      const auto& d = existing ? *existing : drivers[i];
      if ((d.max_load_ff && cap > d.max_load_ff) || (d.max_fanout && fanout > d.max_fanout)) {
        continue;
      }
      // Without a requested drive step, only a smaller legal cell can replace the best fit.
      if (best && !steps
          && std::tie(d.area_um2, d.master) >= std::tie(drivers[best->driver].area_um2, drivers[best->driver].master)) {
        continue;
      }
      const liberty::timing::Query query{d.reference_slew_ps, cap};
      double                       delay = 0, slew = 0;
      if (d.arcs.empty()) {
        delay = d.intrinsic_ps + 1e-3 * d.resistance_ohm * cap;
        slew  = std::hypot(d.reference_slew_ps, 2.2 * (1e-3 * d.resistance_ohm * cap + wire.max_wire_slew));
      } else {
        for (const auto& arc : d.arcs) {
          delay                   = std::max(delay, arc.delay.lookup(query));
          const double transition = std::max(0., arc.slew.lookup(query));
          slew                    = std::max(slew, std::hypot(transition, arc.wire_slew_factor * wire.max_wire_slew));
        }
      }
      if ((d.max_slew_ps && slew > d.max_slew_ps) || slew > sink_limit) {
        continue;
      }
      Clock_branch candidate{i, x, y, cap, radius, leaf, {}, delay + wire.max_delay, slew, wire.length};
      candidate.cell_delay_ps = delay;
      if (steps) {
        candidates.push_back(candidate);
      }
      if (!best || std::tie(d.area_um2, d.master) < std::tie(drivers[best->driver].area_um2, drivers[best->driver].master)) {
        best = std::move(candidate);
      }
    }
    // Walk the area/delay tradeoff. A larger cell can be slower at this load.
    // Keep the default inventory order for step zero. On an equal-area step
    // try the fastest cell first, so equivalent footprints cost only one step.
    std::ranges::sort(candidates, [&](const Clock_branch& a, const Clock_branch& b) {
      const auto& da = drivers[a.driver];
      const auto& db = drivers[b.driver];
      return std::tie(da.area_um2, a.delay_ps, a.far_slew_ps, da.master)
             < std::tie(db.area_um2, b.delay_ps, b.far_slew_ps, db.master);
    });
    for (auto& candidate : candidates) {
      if (best->drive_steps == steps) {
        break;
      }
      if (candidate.delay_ps < best->delay_ps && candidate.far_slew_ps <= best->far_slew_ps) {
        candidate.drive_steps = best->drive_steps + 1;
        best                  = std::move(candidate);
      }
    }
    return best;
  }

  Load store_branch(std::span<Load> loads, Clock_branch branch, size_t id) {
    std::ranges::sort(loads, [&](const Load& a, const Load& b) { return sinks[a.first].name < sinks[b.first].name; });
    for (const auto& p : loads) {
      branch.children.push_back(p.id);
    }
    const auto& driver = drivers[branch.driver];
    Load        parent{id, loads.front().first, branch.x_um, branch.y_um, driver.input_ff, driver.input_max_slew_ps};
    if (!branch.leaf && limits.balance) {
      intervals.push_back(arrival_interval(branch, loads));
    }
    if (id == tree.branches.size()) {
      tree.branches.push_back(std::move(branch));
    } else {
      tree.branches.at(id) = std::move(branch);
    }
    return parent;
  }

  std::vector<Load> leaf_loads(const Clock_branch& branch) const {
    std::vector<Load> group;
    group.reserve(branch.children.size() + 1);
    for (auto id : branch.children) {
      const auto& s = sinks[id];
      group.push_back({id, id, s.x_um, s.y_um, s.cap_ff, s.max_slew_ps});
    }
    return group;
  }

  // Bound neighbour work in dense or coincident leaf bins. The x/y ordering
  // also supplies adjacent leaves when irregular placement leaves empty bins.
  std::vector<std::vector<size_t>> neighbours(std::span<const Load> leaves) const {
    std::vector<std::vector<size_t>> result(leaves.size());
    const auto                       box = bounds(leaves);
    const double                     w = box.xh - box.xl, h = box.yh - box.yl;
    const double pitch = std::max({1. / limits.dbu_per_um, (w + h) / leaves.size(), std::sqrt(w * h / leaves.size())});
    using Bin          = std::pair<int64_t, int64_t>;
    const auto bin = [&](const Load& p) -> Bin { return {std::floor((p.x - box.xl) / pitch), std::floor((p.y - box.yl) / pitch)}; };
    std::map<Bin, std::vector<size_t>> grid;
    const auto                         key = [&](size_t i) { return std::tuple{leaves[i].x, leaves[i].y, leaves[i].id}; };
    std::vector<size_t>                order(leaves.size());
    std::iota(order.begin(), order.end(), 0);
    std::erase_if(order, [&](size_t i) { return tree.branches[leaves[i].id].children.empty(); });
    std::ranges::sort(order, {}, key);
    for (const auto id : order) {
      grid[bin(leaves[id])].push_back(id);
    }
    for (size_t offset = 0; offset < order.size(); ++offset) {
      const auto i    = order[offset];
      auto&      near = result[i];
      if (offset) {
        near.push_back(order[offset - 1]);
      }
      if (offset + 1 < order.size()) {
        near.push_back(order[offset + 1]);
      }
      const auto [x, y] = bin(leaves[i]);
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          const auto at = grid.find({x + dx, y + dy});
          if (at == grid.end()) {
            continue;
          }
          const auto& ids   = at->second;
          const auto  pivot = std::ranges::lower_bound(ids, key(i), {}, key) - ids.begin();
          for (auto j = std::max<ptrdiff_t>(0, pivot - 6); j < std::min<ptrdiff_t>(ids.size(), pivot + 7); ++j) {
            if (ids[j] != i) {
              near.push_back(ids[j]);
            }
          }
        }
      }
      const auto distance = [&](size_t j) {
        return std::tuple{std::abs(leaves[j].x - leaves[i].x) + std::abs(leaves[j].y - leaves[i].y), leaves[j].id};
      };
      std::ranges::sort(near, {}, distance);
      near.erase(std::unique(near.begin(), near.end()), near.end());
      if (near.size() > 6) {
        near.resize(6);
      }
    }
    return result;
  }

  void refine_homes(std::vector<Load>& top, size_t first, std::span<const size_t> group_ends) {
    size_t start = first;
    for (const size_t end : group_ends) {
      auto   leaves     = std::span(top).subspan(start, end - start);
      double mean_delay = 0, mean_cap = 0;
      for (const auto& p : leaves) {
        mean_delay += tree.branches[p.id].delay_ps;
        mean_cap   += tree.branches[p.id].load_ff;
      }
      mean_delay /= leaves.size();
      mean_cap   /= leaves.size();
      std::vector<size_t>                                            revisions(leaves.size(), 1);
      std::map<std::pair<size_t, size_t>, std::pair<size_t, size_t>> tested;
      bool                                                           changed;
      do {
        changed             = false;
        const auto adjacent = neighbours(leaves);
        for (size_t i = 0; i < leaves.size(); ++i) {
          auto& from = tree.branches[leaves[i].id];
          if (from.children.empty()) {
            continue;
          }
          if (from.children.size() < 6) {
            for (auto j : adjacent[i]) {
              const auto& to = tree.branches[leaves[j].id];
              if (to.children.empty() || from.children.size() + to.children.size() > limits.fanout) {
                continue;
              }
              auto       combined = leaf_loads(to);
              const auto extra    = leaf_loads(from);
              combined.insert(combined.end(), extra.begin(), extra.end());
              if (auto legal = fit(combined, bounds(combined), true)) {
                leaves[j] = store_branch(combined, std::move(*legal), leaves[j].id);
                from.children.clear();
                ++revisions[i];
                ++revisions[j];
                changed = true;
                break;
              }
            }
          }
          if (from.children.empty()) {
            continue;
          }
          for (auto j : adjacent[i]) {
            if (tree.branches[leaves[j].id].children.empty()) {
              continue;
            }
            const auto [a, b]   = std::minmax(i, j);
            auto&      last     = tested[{a, b}];
            const auto versions = std::pair{revisions[a], revisions[b]};
            if (last == versions) {
              continue;
            }
            last = versions;
            if (refine(leaves[i], leaves[j], mean_delay, mean_cap)) {
              ++revisions[i];
              ++revisions[j];
              changed = true;
            }
          }
        }
      } while (changed);
      start = end;
    }
    std::erase_if(top, [&](const Load& p) { return tree.branches[p.id].children.empty(); });
    // Only leaf branches exist here. Compact after merging while child IDs still refer to original sink pins.
    std::vector<Clock_branch> compact;
    compact.reserve(top.size());
    for (auto& p : top) {
      compact.push_back(std::move(tree.branches[p.id]));
      p.id = compact.size() - 1;
    }
    tree.branches = std::move(compact);
  }

  bool refine(Load& a, Load& b, double mean_delay, double mean_cap) {
    const auto& left  = tree.branches[a.id];
    const auto& right = tree.branches[b.id];
    if (left.delay_ps == right.delay_ps) {
      return false;
    }
    auto&       sender   = left.delay_ps > right.delay_ps ? a : b;
    auto&       receiver = left.delay_ps > right.delay_ps ? b : a;
    const auto& slow     = tree.branches[sender.id];
    const auto& fast     = tree.branches[receiver.id];
    if (slow.children.size() <= 6 || fast.children.size() >= limits.fanout) {
      return false;
    }
    auto       from = leaf_loads(slow), to = leaf_loads(fast);
    // Rank border candidates by difference in distance to the two centres.
    // The fanout cap bounds each trial and the four nearest keep refinement linear in leaf count.
    const auto distance_delta = [&](const Load& p) {
      return std::abs(p.x - fast.x_um) + std::abs(p.y - fast.y_um) - std::abs(p.x - slow.x_um) - std::abs(p.y - slow.y_um);
    };
    const size_t candidates = std::min<size_t>(4, from.size());
    std::partial_sort(from.begin(), from.begin() + candidates, from.end(), [&](const Load& x, const Load& y) {
      return std::tuple{distance_delta(x), sinks[x.id].name} < std::tuple{distance_delta(y), sinks[y.id].name};
    });
    const auto cost = [&](const Clock_branch& branch) {
      const double delay = mean_delay ? (branch.delay_ps - mean_delay) / mean_delay : 0;
      const double load  = mean_cap ? (branch.load_ff - mean_cap) / mean_cap : 0;
      return delay * delay + load * load;
    };
    double                spread = cost(slow) + cost(fast);
    std::optional<size_t> best;
    Clock_branch          new_from, new_to;
    for (size_t i = 0; i < candidates; ++i) {
      if (!sinks[from[i].id].coupled_group.empty()) {
        continue;
      }
      std::swap(from[i], from.back());
      to.push_back(from.back());
      const auto remaining = std::span(from).first(from.size() - 1);
      const auto from_box = bounds(remaining), to_box = bounds(to);
      const auto radius = [](std::span<const Load> pins, Bounds box) {
        const double x = std::midpoint(box.xl, box.xh), y = std::midpoint(box.yl, box.yh);
        double       far = 0;
        for (const auto& pin : pins) {
          far = std::max(far, std::abs(pin.x - x) + std::abs(pin.y - y));
        }
        return far;
      };
      const auto                  max_radius = std::max(slow.radius_um, fast.radius_um);
      std::optional<Clock_branch> x, y;
      if (radius(remaining, from_box) <= max_radius && radius(to, to_box) <= max_radius) {
        x = fit(remaining, from_box, true);
        y = fit(to, to_box, true);
      }
      to.pop_back();
      std::swap(from[i], from.back());
      if (!x || !y || x->wire_um + y->wire_um > slow.wire_um + fast.wire_um || std::max(x->delay_ps, y->delay_ps) > slow.delay_ps
          || std::max(x->radius_um, y->radius_um) > std::max(slow.radius_um, fast.radius_um)
          || drivers[x->driver].area_um2 + drivers[y->driver].area_um2
                 > drivers[slow.driver].area_um2 + drivers[fast.driver].area_um2) {
        continue;
      }
      const double delta = cost(*x) + cost(*y);
      if (delta + 1e-12 < spread) {
        spread   = delta;
        best     = i;
        new_from = *x;
        new_to   = *y;
      }
    }
    if (best) {
      to.push_back(from[*best]);
      from.erase(from.begin() + *best);
      sender   = store_branch(from, std::move(new_from), sender.id);
      receiver = store_branch(to, std::move(new_to), receiver.id);
      ++tree.border_moves;
      return true;
    }
    return false;
  }

  // Maximum electrically legal Manhattan length on this arm. The caller has already
  // checked the zero-length connection. Keep a placement-coordinate rounding margin.
  static double reach(double length, double margin, auto&& legal) {
    if (legal(length)) {
      return std::max(0., length - margin);
    }
    double lo = 0, hi = length;
    for (int i = 0; i < std::numeric_limits<double>::digits && hi - lo > margin; ++i) {
      const double mid = std::midpoint(lo, hi);
      if (legal(mid)) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    return std::max(0., lo - margin);
  }

  // Join two far subtrees with equal depth, or one subtree to its fixed source. A first stage
  // drives the actual child. Subsequent stages drive a repeater input. The fork
  // drives both arms, so it has a separate electrical reach.
  Load bridge(std::span<Load> children, double x, double y, const Clock_driver* source = nullptr) {
    const auto   b      = bounds(children);
    const double length = std::abs(children[0].x - x) + std::abs(children[0].y - y);
    const double margin = std::max(
        2.0 / limits.dbu_per_um,
        32 * std::numeric_limits<double>::epsilon()
            * std::max({1., std::abs(b.xl), std::abs(b.xh), std::abs(b.yl), std::abs(b.yh), std::abs(x), std::abs(y), length}));
    struct Chain {
      size_t                driver, depth;
      double                root_span, internal_span, area, insertion_ps;
      std::array<double, 2> end_span;
    };
    std::optional<Chain> best;
    const auto           remaining
        = (limits.repeater_sites - tree.wire_repeaters - tree.balance_buffers - tree.schedule_buffers) / children.size();
    for (size_t i = 0; i < drivers.size(); ++i) {
      const auto& driver = drivers[i];
      Load        input{0, 0, 0, 0, driver.input_ff, driver.input_max_slew_ps};
      // Both arms use the same axis proportions (their fork is the midpoint).
      // Probe in that direction so anisotropic layers give the same reach at placement.
      const auto  tip = [&](Load load, size_t side, double span) {
        const double fraction = length ? span / length : 0;
        load.x                = std::lerp(x, children[side].x, fraction);
        load.y                = std::lerp(y, children[side].y, fraction);
        return load;
      };
      const auto fork = [&](double span) {
        std::array loads{tip(input, 0, span), tip(input, children.size() - 1, span)};
        return fit_at(std::span(loads).first(children.size()), x, y, false, {}, source);
      };
      const auto arm = [&](Load load, double span) {
        const auto p = tip(load, 0, span);
        return fit_at(std::span(&p, 1), x, y, false, i);
      };
      if (!fork(0) || !std::ranges::all_of(children, [&](const Load& child) { return arm(child, 0).has_value(); })) {
        continue;
      }
      Chain chain{i, 1, reach(length, margin, fork), 0, 0, 0, {}};
      for (size_t side = 0; side < children.size(); ++side) {
        chain.end_span[side] = reach(length, margin, [&](double span) { return arm(children[side], span); });
      }
      const double gap = length - chain.root_span - std::ranges::min(std::span(chain.end_span).first(children.size()));
      if (gap > 0) {
        if (!arm(input, 0)) {
          continue;
        }
        chain.internal_span = reach(length, margin, [&](double span) { return arm(input, span); });
        if (chain.internal_span <= 0) {
          continue;
        }
        const double extra = std::ceil(gap / chain.internal_span);
        if (extra >= static_cast<double>(remaining)) {
          continue;
        }
        chain.depth += static_cast<size_t>(extra);
      }
      if (chain.depth > remaining) {
        continue;
      }
      const auto root = fork(chain.root_span);
      if (!root) {
        continue;
      }
      chain.area = children.size() * chain.depth * driver.area_um2 + (source ? 0 : drivers[root->driver].area_um2);
      if (source && limits.minimize_source_latency) {
        // These repeaters are common to every planned sink. Compare drivers at
        // the wire lengths they will actually see, including their load on the
        // existing port/gate. A limit-of-reach delay is not the placed delay.
        const double capacity = chain.end_span[0] + (chain.depth - 1) * chain.internal_span + chain.root_span;
        const double scale    = capacity ? length / capacity : 0.;
        const auto   upstream = fork(chain.root_span * scale);
        const auto   end      = arm(children.front(), chain.end_span[0] * scale);
        if (!upstream || !end) {
          continue;
        }
        chain.insertion_ps = upstream->delay_ps + end->delay_ps;
        if (chain.depth > 1) {
          const auto middle = arm(input, chain.internal_span * scale);
          if (!middle) {
            continue;
          }
          chain.insertion_ps += (chain.depth - 1) * middle->delay_ps;
        }
      }
      if (!best
          || std::tie(chain.depth, chain.insertion_ps, chain.area, driver.master)
                 < std::tie(best->depth, best->insertion_ps, best->area, drivers[best->driver].master)) {
        best = chain;
      }
    }
    if (!best) {
      throw Clock_infeasible(
          "no legal clock repeater chain "
          + (source ? "from source to " + sinks[children.front().first].name
                    : "between " + sinks[children.front().first].name + " and " + sinks[children.back().first].name));
    }
    std::array storage{children.front(), children.back()};
    auto       tips = std::span(storage).first(children.size());
    for (size_t side = 0; side < tips.size(); ++side) {
      // Distribute the available wire in proportion to each stage's legal reach.
      const double capacity = best->end_span[side] + (best->depth - 1) * best->internal_span + best->root_span;
      for (size_t k = 0; k < best->depth; ++k) {
        const double fraction = capacity ? (best->end_span[side] + k * best->internal_span) / capacity : 0;
        const double bx = std::lerp(children[side].x, x, fraction), by = std::lerp(children[side].y, y, fraction);
        auto         load  = std::span(&tips[side], 1);
        auto         stage = fit_at(load, bx, by, false, best->driver);
        if (!stage) {
          throw Clock_infeasible("unrealizable clock wire repeater at " + sinks[tips[side].first].name);
        }
        tips[side] = store_branch(load, std::move(*stage), tree.branches.size());
      }
    }
    tree.wire_repeaters += tips.size() * best->depth;
    if (source) {
      return tips.front();
    }
    auto root = fit_at(tips, x, y, false);
    if (!root) {
      throw Clock_infeasible("unrealizable clock fork at " + sinks[tips[0].first].name);
    }
    balance_arms(tips, *root);
    return store_branch(tips, std::move(*root), tree.branches.size());
  }

  // Keep a relocated phase pin from shifting every median in its domain.
  // Reconstruct the original spatial partitions, assign each fork to the
  // region containing its stationary pin, and refit at the real positions.
  void partition_moved_phases(const std::vector<Load>& flops, std::vector<Load>& top) {
    auto seed_loads = flops;
    for (auto& load : seed_loads) {
      if (const auto& previous = sinks[load.id].previous_position) {
        std::tie(load.x, load.y) = *previous;
      }
    }
    Planner           seed(sinks, drivers, limits, nullptr);
    std::vector<Load> seed_roots;
    seed.partition(seed_loads, 0, seed_loads.size(), true, seed_roots);
    std::unordered_map<size_t, size_t> region;
    std::map<Pin_label, size_t>        anchors;
    for (size_t i = 0; i < seed.tree.branches.size(); ++i) {
      for (const auto pin : seed.tree.branches[i].children) {
        region[pin]   = i;
        const auto& s = sinks[pin];
        if (!s.coupled_group.empty()) {
          const auto [it, inserted] = anchors.try_emplace(s.coupled_group, pin);
          const auto& old           = sinks[it->second];
          if (!inserted
              && std::pair{s.previous_position.has_value(), s.name} < std::pair{old.previous_position.has_value(), old.name}) {
            it->second = pin;
          }
        }
      }
    }
    std::vector<std::vector<Load>> batches(seed.tree.branches.size());
    for (const auto& load : flops) {
      const auto& group = sinks[load.id].coupled_group;
      batches[region.at(group.empty() ? load.id : anchors.at(group))].push_back(load);
    }
    for (auto& batch : batches) {
      if (!batch.empty()) {
        partition(batch, 0, batch.size(), true, top);
      }
    }
  }

  void split_scheduled_leaf(std::vector<Load>& top, size_t index) {
    const size_t                  id      = top[index].id;
    auto                          members = leaf_loads(tree.branches[id]);
    std::map<Pin_label, unsigned> fork_steps;
    for (const auto& load : members) {
      const auto& s = sinks[load.id];
      if (s.drive_steps && !s.coupled_group.empty()) {
        const auto [it, fresh] = fork_steps.emplace(s.coupled_group, *s.drive_steps);
        if (!fresh && it->second != *s.drive_steps) {
          throw std::invalid_argument("conflicting clock leaf schedule in discrete gate fork " + s.coupled_group);
        }
      }
    }
    std::map<unsigned, std::vector<Load>> groups_by_drive;
    for (const auto& load : members) {
      const auto& s     = sinks[load.id];
      const auto  fork  = fork_steps.find(s.coupled_group);
      const auto  drive = fork == fork_steps.end() ? s.drive_steps.value_or(tree.branches[id].drive_steps) : fork->second;
      groups_by_drive[drive].push_back(load);
    }
    std::vector<Load> replacements;
    for (auto& [drive, group] : groups_by_drive) {
      partition(group, 0, group.size(), true, replacements, replacements.empty() ? std::optional{id} : std::nullopt);
    }
    top[index] = replacements.front();
    top.insert(top.end(), replacements.begin() + 1, replacements.end());
  }

  void partition(std::vector<Load>& loads, size_t lo, size_t hi, bool leaf, std::vector<Load>& parents,
                 std::optional<size_t> replace = {}) {
    const size_t n     = hi - lo;
    auto         group = std::span(loads).subspan(lo, n);
    const auto   b     = bounds(group);
    if (auto branch = fit(group, b, leaf)) {
      if (!leaf) {
        balance_arms(group, *branch);
      }
      parents.push_back(store_branch(group, std::move(*branch), replace.value_or(tree.branches.size())));
      return;
    }
    if (n == 1) {
      throw Clock_infeasible("no legal clock driver for " + sinks[loads[lo].first].name);
    }
    if (!leaf && n == 2) {
      parents.push_back(bridge(group, std::midpoint(b.xl, b.xh), std::midpoint(b.yl, b.yh)));
      return;
    }
    const bool horizontal = b.xh - b.xl >= b.yh - b.yl;
    size_t     mid        = lo + n / 2;
    if (n > limits.fanout) {
      // More than a group's worth: cut whole groups' worth off, so the pieces fill to the limit. Halving by count
      // alone leaves 25 pins as 12 and 13, and 51,677 as 4,096 leaves of about 12 instead of 2,154 of 24.
      const size_t g = (n + limits.fanout - 1) / limits.fanout;
      mid            = lo + n * (g / 2) / g;
    }
    if (leaf && !groups.empty()) {
      const auto key = [&](const Load& p) {
        const auto& sink = sinks[p.id];
        double      x = p.x, y = p.y;
        if (!sink.coupled_group.empty()) {
          const auto& box = groups.at(sink.coupled_group);
          x               = std::midpoint(box.xl, box.xh);
          y               = std::midpoint(box.yl, box.yh);
        }
        return std::tuple{horizontal ? x : y,
                          horizontal ? y : x,
                          !sink.coupled_group.empty(),
                          sink.coupled_group.empty() ? sink.name : sink.coupled_group,
                          sink.name};
      };
      std::ranges::sort(group, {}, key);
      const auto& name = sinks[loads[mid].id].coupled_group;
      if (!name.empty() && sinks[loads[mid - 1].id].coupled_group == name) {
        size_t left = mid - 1, right = mid + 1;
        while (left > lo && sinks[loads[left - 1].id].coupled_group == name) {
          --left;
        }
        while (right < hi && sinks[loads[right].id].coupled_group == name) {
          ++right;
        }
        if (left == lo && right == hi) {
          throw Clock_infeasible("no legal common clock leaf for discrete gate fork " + name);
        }
        mid = (right == hi || (left != lo && mid - left <= right - mid)) ? left : right;
      }
    } else {
      const auto order = [&](const Load& a, const Load& c) {
        return horizontal ? std::tie(a.x, a.y, sinks[a.first].name) < std::tie(c.x, c.y, sinks[c.first].name)
                          : std::tie(a.y, a.x, sinks[a.first].name) < std::tie(c.y, c.x, sinks[c.first].name);
      };
      if (leaf && n <= limits.fanout) {
        // Electrical or radius failure. Isolate the spatial outliers across the widest gap.
        std::ranges::sort(group, order);
        const auto gap = [&](size_t i) { return horizontal ? loads[i].x - loads[i - 1].x : loads[i].y - loads[i - 1].y; };
        for (size_t i = lo + 1; i < hi; ++i) {
          if (gap(i) > gap(mid)) {
            mid = i;
          }
        }
      } else {
        std::nth_element(loads.begin() + lo, loads.begin() + mid, loads.begin() + hi, order);
      }
    }
    partition(loads, lo, mid, leaf, parents, replace);
    partition(loads, mid, hi, leaf, parents);
  }
};

Clock_tree plan(const std::vector<Clock_sink>& sinks, const std::vector<Clock_driver>& drivers, const Clock_limits& limits,
                const std::optional<Clock_source>& source, Clock_plan_cache* cache, Plan_depth depth) {
  if (limits.fanout < 2 || limits.fanout > 30 || !Planner::nonnegative(limits.wire_ff_um)
      || !Planner::nonnegative(limits.wire_ohm_um) || !Planner::nonnegative(limits.skew_bound_ps) || !Planner::nonnegative(limits.radius_um) || !std::isfinite(limits.dbu_per_um)
      || limits.dbu_per_um <= 0 || (limits.directional_rc && !std::ranges::all_of(*limits.directional_rc, Planner::nonnegative))
      || (limits.leaf_rc && !std::ranges::all_of(*limits.leaf_rc, Planner::nonnegative))
      || (limits.trunk_rc && !std::ranges::all_of(*limits.trunk_rc, Planner::nonnegative))
      || (limits.source_fork_cap_ff && !Planner::nonnegative(*limits.source_fork_cap_ff)) || limits.source_drive_stages > 2) {
    throw std::invalid_argument("invalid PCRS clock limits");
  }
  for (const auto& d : drivers) {
    if (d.master.empty() || d.input.empty() || d.output.empty() || !Planner::nonnegative(d.input_ff)
        || !Planner::nonnegative(d.max_load_ff) || !Planner::nonnegative(d.area_um2) || !Planner::nonnegative(d.resistance_ohm)
        || !Planner::nonnegative(d.max_slew_ps)) {
      throw std::invalid_argument("invalid PCRS clock buffer " + d.master);
    }
  }
  if (source
      && (!std::isfinite(source->x_um) || !std::isfinite(source->y_um) || !Planner::nonnegative(source->driver.max_load_ff)
          || !Planner::nonnegative(source->driver.max_slew_ps) || !Planner::nonnegative(source->driver.resistance_ohm))) {
    throw std::invalid_argument("invalid PCRS clock source");
  }
  auto trial             = limits;
  trial.leaf_drive_steps = std::min<size_t>(trial.leaf_drive_steps, drivers.size());
  for (;;) {
    try {
      if (std::ranges::any_of(sinks, [](const auto& sink) { return sink.previous_position.has_value(); })) {
        try {
          return Planner(sinks, drivers, trial, cache).run(source, depth, true);
        } catch (const Clock_infeasible&) {
          // The original region may have no legal trunk after the move.
        }
      }
      return Planner(sinks, drivers, trial, cache).run(source, depth);
    } catch (const Clock_infeasible&) {
      if (!trial.leaf_drive_steps) {
        throw;
      }
      --trial.leaf_drive_steps;
    }
  }
}
}  // namespace

Clock_tree plan_clock_tree(const std::vector<Clock_sink>& sinks, const std::vector<Clock_driver>& drivers,
                           const Clock_limits& limits, const std::optional<Clock_source>& source, Clock_plan_cache* cache) {
  return plan(sinks, drivers, limits, source, cache, Plan_depth::tree);
}

std::vector<Clock_branch> plan_clock_leaves(const std::vector<Clock_sink>& sinks, const std::vector<Clock_driver>& drivers,
                                            const Clock_limits& limits) {
  return plan(sinks, drivers, limits, {}, nullptr, Plan_depth::leaves).branches;
}
}  // namespace livehd::pcrs
