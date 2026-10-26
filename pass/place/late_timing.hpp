// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "clock_constraints.hpp"
#include "core/physical_rc.hpp"
#include "core/timing_feedback.hpp"
#include "gate_waveform.hpp"
#include "hhds/graph.hpp"
#include "pass/liberty/timing_model.hpp"
#include "star_rc.hpp"

namespace livehd::placement::late {
using Pair = std::array<double, 2>;
struct Driving_cell {
  std::string cell, output, related_pin;
  Pair        input_slew_ps{};
};
struct Input {
  Pair                        arrival_ps{}, slew_ps{};
  std::optional<Driving_cell> driving_cell;
  std::string                 clock;
};
struct Output {
  Pair        required_ps{}, load_ff{};
  std::string clock;
};
struct Constraints {
  std::map<std::string, Input>                  inputs;
  std::map<std::string, Output>                 outputs;
  std::optional<Ideal_clock>                    clock;
  std::map<std::string, Ideal_clock>            clocks;
  std::map<std::string, Clock_alias>            clock_aliases;
  std::set<std::pair<std::string, std::string>> asynchronous;
  std::set<std::string>                         cuts;
  std::map<std::string, bool>                   constants;
  // The SDC adapter must affirm that it covered the entire supported subset.
  // Explicit boundaries alone are useful observations, not certified Fmax.
  bool                                          complete = false;
  std::vector<std::string>                      unsupported;
};
// A capture endpoint's share of its clock period: the setup requirement is
// fraction * period - uncertainty - setup, and the endpoint's period estimate
// is period - slack / fraction. Fraction 1 with the clock uncertainty is the
// ordinary full-cycle check.
struct Capture_budget {
  double                fraction = 1;    // (0,1]
  std::optional<double> uncertainty_ps;  // replaces the clock uncertainty for this cell's setup checks
};
// Characterized cells outside the Liberty library, looked up by declaration
// name after it. A name must not collide with a Liberty cell. Capture budgets
// are keyed by cell name; an absent entry is {1, clock uncertainty}.
struct Provisional_cells {
  liberty::timing::Library              library;
  std::map<std::string, Capture_budget> capture;
  std::string                           label;
};
struct Predecessor {
  size_t   pin        = 0;
  unsigned transition = 0;
};
struct State {
  std::optional<double>      arrival_ps, required_ps, slew_ps, load_ff;
  std::optional<Predecessor> predecessor;
};
struct Pin_result {
  std::string          name;
  hhds::Pin_class      graph_pin;
  // An internal characterized output may have no materialized graph pin when
  // it has no external net. Its instance/declaration still identify it exactly.
  hhds::Node_class     graph_node;
  hhds::Port_id        declaration_port = 0;
  std::array<State, 2> transitions;
  bool                 clock = false, endpoint = false;
  std::optional<bool>  constant;
  // The ideal clock waveform reaching a clock pin, by name.
  std::optional<std::string> clock_name;
};
struct Feasible_timing_graph {
  struct Vertex {
    size_t                pin = 0, domain = 0;
    unsigned              transition = 0;
    std::optional<double> boundary_arrival_ps, endpoint_required_ps;
  };
  struct Arc {
    size_t from = 0, to = 0, timing_edge = 0;
    double delay_ps = 0;
    bool   wire     = false;
  };
  std::optional<double>                   reference_period_ps;
  std::vector<std::optional<std::string>> launch_domains;
  std::vector<Vertex>                     vertices;
  std::vector<Arc>                        arcs;
};
struct Evaluation_options {
  // Capture every eligible transition arc, not only worst predecessors.
  // Graph vertices reference Result::pins and retain separate launch domains.
  bool           capture_feasible_graph = false;
  Driver_options driver;
  // Seed only the clock-to-Q launches of cells (by declaration name) the
  // filter accepts, and timed inputs only with seed_inputs, so endpoint
  // slack can be split by launch class. Requirements are unchanged; an
  // endpoint no seeded launch reaches is reported uncovered.
  std::function<bool(std::string_view cell)> launch_filter;
  bool                                       seed_inputs = true;
};
struct Result {
  struct Domain_state {
    std::optional<double>      arrival_ps, required_ps;
    std::optional<Predecessor> predecessor;
  };
  struct Domain {
    std::optional<std::string>               launch_clock;
    std::vector<std::array<Domain_state, 2>> pins;
  };
  struct Clock_summary {
    double                period_ps = 0;
    std::optional<double> wns_ps, tns_ps, achievable_period_ps;
    size_t                timed_endpoints    = 0;
    bool                  independent_period = true;
  };
  std::vector<Domain>                  domains;
  std::map<std::string, Clock_summary> clocks;
  std::vector<Pin_result>              pins;
  std::vector<std::string>             unsupported;
  std::vector<std::string>             warnings;
  std::optional<double>                wns_ps, tns_ps, achievable_period_ps;
  std::optional<Feasible_timing_graph> feasible_graph;
  bool                                 certified           = false;
  uint64_t                             topology_generation = 0, model_generation = 0;
  // A separate epoch component: strict and characterized-boundary observations
  // must not share multiplier history or a retained-period comparison.
  bool                                 characterized_boundary_policy = false;
  bool                                 bounded_boundary_policy       = false;
  size_t                               visited_pins = 0, evaluated_arcs = 0;
  size_t                               waveform_pins = 0, waveform_arcs = 0, required_pins = 0;
  size_t                               delay_only_wire_drivers = 0;
  size_t           driver_fits = 0, distributed_driver_fits = 0, lumped_driver_fits = 0, driver_fit_failures = 0;
  size_t           table_boundary_driver_fits     = 0;
  size_t           full_load_boundary_driver_fits = 0, crossing_boundary_driver_fits = 0;
  // Timed endpoints with a capture fraction below 1. Their period estimate is
  // period - slack / fraction; WNS/TNS keep the raw slack. The feasible-graph
  // objective and GP timing do not support them.
  size_t                               fractional_endpoints = 0;
  // Optimization consumers require certified period coverage; an incomplete
  // observation must never become a zero-delay scalar feedback result.
  timing::Feedback feedback() const;
};
// A native, single-thread late-mode timer. The mapped flat LGraph is the
// connectivity authority; no netlist is synthesized or optimized. Rebuild the
// binding after topology edits, outside active graph walks. Library and graph
// lifetimes must cover the session. Geometry/RC can change between evaluations.
// Constraints support named ideal positive-edge clocks, phase-aware related
// edge pairs, validated same-waveform aliases and asynchronous capture cuts.
// Unsupported clocks, latches, conditions or library coverage are
// reported and cannot produce certified Fmax. RC uses charge-matched pi driver
// and Elmore load waveforms with explicit characterized boundary approximations.
// Provisional cells extend the Liberty lookup; without them (or with an empty
// set) every result is unchanged.
class Proxy {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  Proxy(const hhds::Graph& graph, std::shared_ptr<const liberty::timing::Library> models, Constraints constraints,
        uint64_t topology_generation = 0, uint64_t model_generation = 0,
        std::shared_ptr<const Provisional_cells> provisional = nullptr);
  ~Proxy();
  Proxy(Proxy&&) noexcept;
  Proxy& operator=(Proxy&&) noexcept;
  Result evaluate(const physical::Parasitics* parasitics = nullptr, Star_context* star = nullptr,
                  Evaluation_options options = {}) const;
};
}  // namespace livehd::placement::late
