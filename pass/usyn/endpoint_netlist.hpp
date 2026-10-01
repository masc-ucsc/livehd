// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "logical_region.hpp"

namespace livehd::usyn {

enum class Endpoint_kind { Static, Domino, DominoLatch };

struct Endpoint_ref {
  enum class Space { native, cell };
  Space    space                                 = Space::native;
  uint32_t index                                 = 0;
  bool     inverted                              = false;  // native inversion, or the producer's free !Q port
  bool     operator==(const Endpoint_ref&) const = default;
};

struct Endpoint_rail {
  uint32_t     input = 0;  // logical variable in the function/formula
  Endpoint_ref producer;
  bool         operator==(const Endpoint_rail&) const = default;
};

struct Frozen_cell {
  Endpoint_kind              kind  = Endpoint_kind::Domino;
  uint32_t                   phase = 1;
  uint32_t                   state = 0;  // source-state/clock context; only DominoLatch owns storage
  std::string                name;
  std::array<std::string, 2> outputs{"Q", "!Q"};
  std::vector<Endpoint_ref>  inputs;
  std::vector<Endpoint_rail> rails;  // both signs can demand the same logical input
  Truth_table                function;
  Gate_formula               formula;
  Gate_formula::Metrics      metrics;
};

struct Frozen_state {
  std::string             name;
  char                    init = 'x';
  Xsignal                 q, reference_d;
  uint32_t                domain = unknown_clock_domain;
  // Exactly one storage owner. No ordinary register follows a DominoLatch in
  // the abstract topology; CMOS expansion realizes its original register.
  std::optional<uint32_t> domino_latch;
};

struct Endpoint_netlist {
  static constexpr uint32_t       version        = 1;
  uint32_t                        policy_version = version;
  Endpoint_kind                   residual_kind  = Endpoint_kind::Static;
  // Static edges can refer only to native XAG nodes/state sources. They cannot
  // hide a same-cycle Domino dependency or insert inter-phase CMOS. The graph
  // also retains canonical reference expressions for exact reconstruction.
  Xag                             native;
  std::vector<Xsignal>            inputs;
  std::vector<Xag_region::Output> outputs;
  std::vector<Frozen_state>       state;
  std::vector<Frozen_cell>        cells;
  Gate_constraints                gates;
  Endpoint_cost_model             costs;
  uint32_t                        clock_phases = 2;
  Logical_cost                    estimated_cost;
};

struct Endpoint_netlist_result {
  Status                          status = Status::invalid;
  std::optional<Endpoint_netlist> netlist;
  std::string                     reason;
};

// Freeze after selection, pair optimization, residual optimization and feedback.
// The owning semantic region retains full source/control metadata; frozen state
// indices preserve its state_bits correspondence. No ABC or solver is involved.
// Use the same domain identities as selection so shared-cell costs agree.
Endpoint_netlist_result freeze_endpoint_netlist(const Logical_region& region, const Logical_options& options, Budget& work,
                                                std::span<const uint32_t> domains = {});

// Validate kinds, ports, rail demands, functions, phase/clock dependencies,
// source correspondence and recomputed mixed cost; then reconstruct CMOS.
// Canonical XAG reconstruction must match each recorded reference D exactly.
// Invalid or exhausted input never publishes a partial behavioral graph.
Xag_region expand_endpoint_netlist(const Endpoint_netlist& netlist, Budget& work, uint32_t max_nodes = 2000000,
                                   uint32_t max_formula_nodes = 16384);

}  // namespace livehd::usyn
