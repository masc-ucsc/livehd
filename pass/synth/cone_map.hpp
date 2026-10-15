// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
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
}  // namespace livehd::synth
