// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine.hpp"
#include "netlist.hpp"

// One typed reader for the SDC subset the synthesis and placement flows use (todo 2q-fasttimer, step B).
//
// The text is read as the small Tcl it is: `set`, `$var`, `[expr ...]`, `[get_ports ..]`, `[get_clocks ..]`,
// `[all_inputs]`, `[all_outputs]` (with `-no_clocks`), `lsearch -inline -all -not -exact LIST ITEM`,
// brace and quote grouping, backslash continuation. Port selections stay symbolic until `resolve`, so
// the same parse serves any netlist. A command or form that is not modelled is listed in `unsupported`
// (once per command name) and never dropped silently; control structures (`foreach`, `if`, `proc`)
// are skipped as one unit and listed.
//
//   create_clock           -name -period [-waveform] PORT            -> Clock
//   create_generated_clock                                           -> unsupported
//   set_clock_uncertainty  VALUE [get_clocks]                        -> setup uncertainty
//   set_clock_transition   VALUE [get_clocks]                        -> ideal clock slew
//   set_input_delay / set_output_delay  VALUE -clock C PORTS         -> Delay (-min only forms are skipped)
//   set_input_transition   VALUE PORTS                               -> default input slew
//   set_load               VALUE PORTS                               -> output load
//   set_false_path         -from PORTS | -to PORTS                   -> start/end removed
//   set_driving_cell, set_max_fanout, set_clock_groups, ...          -> unsupported
//   current_design, set ::var, set_propagated_clock                  -> ignored
namespace livehd::timing::fast {

// Ports by pattern (glob over the full port bit name `d[3]` or its base `d`), or all of one direction.
struct Port_selection {
  enum class Kind { names, all_inputs, all_outputs };
  Kind                     kind = Kind::names;
  std::vector<std::string> patterns;  // Kind::names
  std::vector<std::string> excluded;  // exact names removed from an all_* selection
  bool                     no_clocks = false;
};

struct Sdc {
  struct Clock {
    std::string          name, port;
    float                period_ps = 0;
    std::optional<float> uncertainty_ps, slew_ps;
  };
  struct Delay {
    Port_selection ports;
    std::string    clock;
    float          delay_ps = 0;
    bool           input    = true;
  };
  struct Load {
    Port_selection ports;
    float          load_ff = 0;
  };
  struct False_path {
    Port_selection from, to;
  };
  std::vector<Clock>        clocks;
  std::vector<Delay>        delays;
  std::vector<Load>         loads;
  std::vector<False_path>   false_paths;
  std::optional<float>      input_transition_ps;
  std::vector<std::string>  unsupported;
};

// Units of the time and capacitance numbers in the text.
struct Sdc_units {
  double time_ps = 1, capacitance_ff = 1;
};

Sdc read_sdc(std::string_view text, Sdc_units units = {});

// The engine constraints for a netlist: selections resolved against its ports, false paths turned into cut
// pins and excluded endpoints. A pattern that names no port is recorded in `unsupported` of the result.
Constraints resolve(const Sdc& sdc, const Netlist& netlist);

}  // namespace livehd::timing::fast
