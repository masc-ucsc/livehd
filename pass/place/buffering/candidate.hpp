// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "pass/place/gate_waveform.hpp"

namespace livehd::placement::buffering {
struct Point {
  double x = 0, y = 0;
  bool   operator==(const Point&) const = default;
};
struct Rect {
  double xl = 0, yl = 0, xh = 0, yh = 0;
};
struct Cell_ports {
  std::string cell, input, output;
};
struct Resource_use {
  std::string resource;
  uint64_t    units = 0;
};
struct Buffer {
  std::string               name;
  Cell_ports                model;
  Point                     input, output;
  Rect                      body;
  bool                      placement_allowed = true;
  std::vector<Resource_use> demand;
};
struct Sink {
  std::string                          name, cell, pin;
  Point                                position;
  std::array<std::optional<double>, 2> required_ps;
  // Dimensionless endpoint multipliers, from the shared objective. Never
  // subtracted from physical required times. Unconstrained states carry zero.
  std::array<double, 2>                multiplier{};
};
struct Driver {
  std::string           name;
  Cell_ports            model;
  Point                 output;
  // Physical INPUT transition order. The selected unconditional source arc
  // maps polarity; inserted buffers must be noninverting one-input cells.
  std::array<double, 2> input_arrival_ps{}, input_slew_ps{};
};
struct Terminal {
  size_t node   = 0;
  bool   buffer = false;
  size_t index  = 0;
};
struct Wire_tree {
  // Stage 0 is the existing driver; stage k+1 is buffers[k]. All pin loads
  // are added from the library exactly once. rc contains WIRE capacitance only.
  size_t                driver_stage = 0, driver_node = 0;
  physical::Rc_net      rc;
  std::vector<Point>    positions;
  std::vector<Terminal> terminals;
  bool                  routing_allowed = true;
};
struct Candidate {
  std::vector<Buffer>    buffers;
  std::vector<Wire_tree> trees;
};
struct Context {
  Driver                          driver;
  std::vector<Sink>               sinks;
  Rect                            core;
  std::vector<Rect>               placement_exclusions;
  std::map<std::string, uint64_t> capacity;
  double                          normalization_period_ps = 1;
  size_t                          max_waveform_fits = 100000, max_rc_nodes = 1000000;
  bool                            clock_net = false;
  // Same explicit policy as native GP/reference/final timing. Disabled by
  // default; admitted approximations still need independent fidelity checks.
  late::Driver_options            driver_options;
};
struct Pin_state {
  std::array<double, 2>                arrival_ps{}, slew_ps{};
  std::array<std::optional<double>, 2> required_ps;
};
enum class Status { evaluated, unsupported, budget_exhausted };
struct Violation {
  std::string object, kind;
  double      actual = 0, limit = 0;
};
struct Evaluation {
  Status                                 status = Status::evaluated;
  std::string                            reason;
  bool                                   electrically_feasible = true, resources_feasible = true;
  std::optional<bool>                    meets_required;
  std::vector<Violation>                 violations;
  std::map<std::string, Pin_state>       pins;
  std::map<std::string, uint64_t>        demand;
  std::array<std::optional<double>, 2>   root_required_ps;
  double                                 area_um2 = 0, wire_length_um = 0, multiplier_cost = 0;
  size_t                                 waveform_fits                 = 0;
  bool                                   characterized_boundary_policy = false, bounded_boundary_policy = false;
  std::map<late::Driver_mode, size_t>    driver_modes;
  std::map<late::Driver_failure, size_t> driver_failures;
  bool feasible() const { return status == Status::evaluated && electrically_feasible && resources_feasible; }
};
// Common evaluator for fully expanded candidates, before either search pruning
// or materialization. Uses the existing native GaTe/pi and per-receiver waveform
// kernels, with fresh loads on EVERY affected segment. Exact on the supplied RC
// topology and selected source arc; a full native graph walk must still verify
// source-arc rank/context changes before committing a physical edit.
// No netlist edit, search optimum, routing-capacity inference or whole-design
// timing certification is implied. Malformed topology/identity throws; missing
// models, numerical fit failure and bounded work exhaustion remain distinct.
Evaluation evaluate(const liberty::timing::Library&, const Context&, const Candidate&);
}  // namespace livehd::placement::buffering
