// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/interned_name.hpp"
#include "pass/liberty/timing_model.hpp"

namespace livehd::pcrs {
struct Clock_drive_unavailable : std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};
struct Clock_infeasible : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// One invocation serves one physical clock net. A gating function, a root
// or a polarity change starts a separate net. Coordinates in micrometres.
struct Clock_sink {
  Pin_label                                name;
  double                                   x_um = 0, y_um = 0, cap_ff = 0;
  bool                                     macro       = false;
  double                                   max_slew_ps = 0;
  std::optional<unsigned>                  drive_steps{};        // explicit size step for the final leaf containing this sink
  Pin_label                                coupled_group{};      // Pins at a discrete clock-gate fork must share a leaf.
  std::optional<std::pair<double, double>> previous_position{};  // Before relocating a private phase cell, in micrometres.
  Name                                     chunk{};              // Accepted floorplan home.
  double                                   downstream_ps = 0;    // Latest effective arrival below a gate, relative to requested arrival.
  double   insertion_ps = 0, wanted_ps = 0;          // Internal clock-pin latency and desired relative effective arrival.
  double   leaf_delay_ps                       = 0;  // Requested physical delay ahead of this leaf.
  bool     separate_leaf                       = false;
  uint64_t graph_pin                           = 0;  // HHDS pin class index in the accepted physical graph.
  // Earliest arrival through the gate and its own sink subtree, when it differs from downstream_ps (the latest).
  // The subtree below a gate has its own spread: the tree above sees the interval, not one number.
  std::optional<double> downstream_min_ps{};
  [[nodiscard]] bool valid_downstream() const {
    return std::isfinite(downstream_ps)
           && (!downstream_min_ps || (std::isfinite(*downstream_min_ps) && *downstream_min_ps <= downstream_ps));
  }
  [[nodiscard]] double  tail_lo_ps() const { return downstream_min_ps.value_or(downstream_ps) + insertion_ps - wanted_ps; }
  [[nodiscard]] double  tail_hi_ps() const { return downstream_ps + insertion_ps - wanted_ps; }
  bool     operator==(const Clock_sink&) const = default;
};

struct Clock_driver {
  std::string master, input, output;
  double      input_ff = 0, max_load_ff = 0, area_um2 = 0;
  double      resistance_ohm = 0, max_slew_ps = 0;
  size_t      max_fanout   = 0;  // zero means the library specifies no limit
  double      intrinsic_ps = 0, input_max_slew_ps = 0;
  // Rise and fall NLDM slices at an input slew representative of a fanout-of-four stage.
  // Only arcs of the actual buffer input/output pair enter this model.
  struct Arc {
    liberty::timing::Table delay, slew;  // at the reference input transition, over the output load
    double                 wire_slew_factor = 0;
  };
  std::vector<Arc> arcs{};
  double           reference_slew_ps = 0;
};
void characterize_clock_driver(Clock_driver& driver, const liberty::timing::Cell& cell, std::optional<double> input_slew_ps = {});

// Existing port or cell output that drives the root buffer. An unconstrained port
// has zero output resistance, but still charges the distributed wire resistance.
struct Clock_source {
  double                  x_um = 0, y_um = 0;
  Clock_driver            driver;
  std::vector<Clock_sink> direct{};  // Existing phase-sensitive loads retained at the source.
  std::string             chunk{};   // Empty for an external clock.
};

struct Clock_limits {
  size_t   fanout     = 24;
  double   wire_ff_um = 0, wire_ohm_um = 0;
  double   radius_um               = 0;                                   // optional hard limit for leaf groups
  size_t   repeater_sites          = std::numeric_limits<size_t>::max();  // absolute placement capacity
  unsigned leaf_drive_steps        = 0;                                   // discrete faster legal drivers per leaf
  bool     minimize_source_latency = false;    // Size a gated domain's shared repeater chain against its insertion delay.
  bool     allow_source_fork       = false;    // Compare direct drive of the top branches against one more shared buffer.
  std::optional<double> source_fork_cap_ff{};  // For a primary port without a driving cell, bound its new load by the original net.
  double                dbu_per_um                            = 1000;
  bool                  balance                               = false;
  bool                  operator==(const Clock_limits&) const = default;
  std::optional<std::array<double, 4>> leaf_rc{}, trunk_rc{};  // Calibrated branches; the existing source keeps platform RC.
  std::optional<std::array<double, 4>> directional_rc{};       // Horizontal R,C and vertical R,C per micron.
  bool     split_scheduled_leaves = false;  // Keep explicit drives feasible on the nominal reference for a balancing trial.
  unsigned source_drive_stages    = 0;      // First one or two stages of the nominal tree to size together.
  // Largest estimated transition a planned stage may deliver to its loads, ps. Zero keeps the library's
  // max_transition as the only bound, which lets every stage run at the limit of the smallest buffer.
  // Declared last: callers and tests initialize the fields above by position.
  double target_slew_ps = 0;
  // Faster legal drivers for each planned trunk stage (the stages above the leaves), as leaf_drive_steps does for
  // leaves. A trunk stage is few cells and its delay is shared by every sink below it. UINT_MAX: until no faster
  // driver exists at its load.
  unsigned trunk_drive_steps      = 0;
  // Arrival differences below this between sibling subtrees are left alone when balancing: padding a gap the
  // clock period tolerates costs buffers for no timing gain. Zero pads to the resolution of the smallest stage.
  double   skew_bound_ps          = 0;
};

struct Clock_branch {
  size_t              driver;  // index in the supplied library
  double              x_um, y_um, load_ff, radius_um;
  // Children index either the input sinks, or earlier entries in branches.
  bool                leaf;
  std::vector<size_t> children;
  double              delay_ps = 0, far_slew_ps = 0, wire_um = 0;
  unsigned            drive_steps      = 0;
  double              cell_delay_ps    = 0;
  double              input_arrival_ps = 0;  // Estimated from the physical source pin.

  [[nodiscard]] double member_distance_um(const Clock_sink& sink) const {
    return std::abs(sink.x_um - x_um) + std::abs(sink.y_um - y_um);
  }
};

struct Clock_tree {
  // Each root is driven by the existing source. Every sink appears once.
  std::vector<Clock_branch>                   branches;
  std::vector<size_t>                         roots;
  std::vector<std::pair<std::string, size_t>> chunk_entries;
  size_t                                      border_moves = 0, wire_repeaters = 0;
  double                                      source_wire_um = 0, source_load_ff = 0, source_far_slew_ps = 0;
  size_t                                      source_repeaters           = 0;
  bool                                        source_pair_available      = false;
  bool                                        used_previous_leaf_regions = false;
  struct Leaf_metrics {
    double                        min_delay_ps = 0, max_delay_ps = 0, wire_um = 0;
    double                        delay_stddev_ps = 0, star_wire_um = 0, max_member_distance_um = 0;
    size_t                        members = 0;
    std::map<size_t, size_t>      size_histogram{};
    std::map<std::string, size_t> driver_histogram{};
  };
  Leaf_metrics        initial_leaves, refined_leaves;
  // From the existing source output pin, using the fitted Liberty stage and the
  // driver-rooted Steiner RC of each net. Indices in arrival_ps match the input sinks.
  std::vector<double> arrival_ps;
  std::vector<double> retained_arrival_ps;
  double              source_cell_delay_ps = 0;
  double              max_leaf_skew_ps = 0, target_skew_ps = 0;
  size_t              balance_buffers  = 0;
  size_t              schedule_buffers = 0;
};

// A session belongs to one immutable technology/driver library. Source changes
// are retimed on every call. Geometry, limits and discrete-gate membership are
// checked before reusing a partition or an electrical fit.
struct Clock_plan_cache {
  struct State;
  std::shared_ptr<State> state;
  size_t                 grouping_hits = 0, reused_stages = 0, fitted_stages = 0;
};
Clock_tree plan_clock_tree(const std::vector<Clock_sink>& sinks, const std::vector<Clock_driver>& drivers,
                           const Clock_limits& limits, const std::optional<Clock_source>& source = {},
                           Clock_plan_cache* cache = nullptr);
// Placement anchors need the fitted leaf groups before any trunk or source is installed.
std::vector<Clock_branch> plan_clock_leaves(const std::vector<Clock_sink>& sinks, const std::vector<Clock_driver>& drivers,
                                            const Clock_limits& limits);
}  // namespace livehd::pcrs
