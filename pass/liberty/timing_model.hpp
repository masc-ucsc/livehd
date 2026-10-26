// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::liberty::timing {
enum class Transition : unsigned { rise, fall };
enum class Variable { none, input_slew, output_load, related_slew, constrained_slew };
enum class Sense { positive, negative, non_unate, unknown };
struct Query {
  double input_slew_ps = 0, output_load_ff = 0, related_slew_ps = 0, constrained_slew_ps = 0;
};
// Tables own normalized ps/fF coordinates. Extrapolate linearly using the two
// nearest boundary points, matching ordinary NLDM interpolation. No allocation
// or unit conversion is performed by lookup(). Scalar/one-point axes are valid.
struct Table {
  Variable            x_variable = Variable::none, y_variable = Variable::none;
  std::vector<double> x, y, values;  // row-major: x then y
  void                validate() const;
  double              lookup(const Query& query) const;
  // Fix every coordinate except output load. This owned scalar/1D projection
  // preserves interpolation and extrapolation for repeated load queries.
  Table               output_load_slice(const Query& fixed) const;
};
struct Thresholds {
  std::array<double, 2> input{.5, .5}, output{.5, .5}, lower{.2, .2}, upper{.8, .8};
  double                slew_derate = 1;
  void                  validate() const;
};
struct Arc {
  std::string                         related_pin, type, when;
  Sense                               sense = Sense::unknown;
  std::array<std::optional<Table>, 2> delay, slew, constraint;
  std::vector<std::string>            unsupported;
};
struct Pin {
  std::string                          name, direction, function;
  bool                                 clock = false, three_state = false;
  std::array<std::optional<double>, 2> capacitance_ff;
  std::optional<double>                max_capacitance_ff, max_transition_ps;
  std::vector<Arc>                     arcs;
};
struct Cell {
  std::string                name, library;
  double                     area = 0;
  Thresholds                 thresholds;
  bool                       sequential = false, latch = false, memory = false;
  // An integrated clock gate (`clock_gating_integrated_cell`): its gated-clock
  // output follows the clock pin through a combinational arc.
  bool                       clock_gate = false;
  std::map<std::string, Pin> pins;
  std::vector<std::string>   unsupported;
};
struct Library {
  Thresholds                  default_thresholds;
  std::map<std::string, Cell> cells;
};
// Parse one owned snapshot containing one or more library groups. No timer,
// external process, synthesis, global cache, or mutable model is involved.
// Malformed syntax/data throws. Unsupported models are attached to their cells
// or arcs so an unused unsupported cell does not invalidate a usable library.
// Duplicate cell names across libraries/corners are rejected: choose a corner
// explicitly rather than depending on file order.
Library parse(std::string_view text);
}  // namespace livehd::liberty::timing
