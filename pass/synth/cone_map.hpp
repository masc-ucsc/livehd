// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cell_netlist.hpp"
#include "lnet.hpp"

namespace livehd::synth {
// A combinational cut with an explicit placed environment. The input/output
// vectors follow Lnet port order. Units are ps and fF, independent of Liberty.
struct Cone_boundary {
  struct Input {
    double arrival_ps = 0, slew_ps = 0;
  };
  struct Output {
    double required_ps = 0, load_ff = 0;
  };
  std::vector<Input>  inputs;
  std::vector<Output> outputs;
  double              budget_ps = 0;
  // The wires the placement gives nets of this cut: mean length by fanout, and the wire's capacitance and
  // resistance per micron. Empty: the mapper assumes ideal wires, which is right for a 32-cell cone and makes a
  // whole color look faster than it will be.
  struct Wire {
    double                     capacitance_ff_um = 0, resistance_ohm_um = 0;
    std::map<unsigned, double> length_um;
  };
  Wire wire;
};
struct Cone_mapping {
  Cell_netlist netlist;
  Cell_library cells;
};
using Cone_provider = std::function<std::optional<Cone_mapping>(const Lnet&, const Cone_boundary&, const std::string&)>;
// Optional backend registration keeps physical placement and pass/synth ABC-free.
bool                        register_cone_provider(std::string name, Cone_provider provider);
bool                        has_cone_provider(std::string_view name);
std::optional<Cone_mapping> map_cone(std::string_view provider, const Lnet& logic, const Cone_boundary& boundary,
                                     const std::string& liberty);
// Combinational equivalence of two networks with the same inputs and outputs, by position. A whole color has too
// many inputs for the exhaustive check a 12-input cone gets, so its replacement is proved by a registered prover:
// true is a proof, false a refutation or a proof that did not finish. No prover registered: nullopt.
using Logic_prover = std::function<bool(const Lnet& before, const Lnet& after)>;
bool                register_logic_prover(std::string name, Logic_prover prover);
std::optional<bool> prove_equivalent(std::string_view prover, const Lnet& before, const Lnet& after);
}  // namespace livehd::synth
