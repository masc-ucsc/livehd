// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine.hpp"
#include "netlist.hpp"

// One typed reader for the SDC the synthesis and placement flows use (todo 2q-fasttimer, step B).
//
// An SDC file is a Tcl script, and the benchmark constraints use it as one (variables, `expr`, lists, `foreach`
// over synchronizer pins, `if {[llength ..] == 0} { error .. }`). `read_sdc` therefore runs the text in a small
// Tcl interpreter against a design (its ports, instance pins and nets by name) and returns what the SDC
// commands asked for as typed data: clocks, IO delays, loads, driving cells, exceptions, clock groups, limits.
// `resolve` turns that into the engine's constraints for a netlist.
//
// Nothing is dropped silently. A command or an option that is not modelled, a Tcl error (including the
// script's own `error`), and an object that names nothing are listed in `Sdc::unsupported`; the command that
// failed is skipped and the script goes on with the next top-level command. README.md has the table of
// commands.
namespace livehd::timing::fast {

// Units of the time and capacitance numbers in the text.
struct Sdc_units {
  double time_ps = 1, capacitance_ff = 1;
};

struct Sdc_object {
  enum class Kind : uint8_t { port, pin, net, clock, cell };
  Kind        kind = Kind::port;
  std::string name;  // a port bit `d[3]`, a pin `instance/PIN`, a net, a clock, an instance
  bool        operator==(const Sdc_object&) const = default;
};

// What the reader asks of the design. Port names are bit names (`d[3]`; a one-bit port has its plain name).
class Sdc_design {
public:
  virtual ~Sdc_design() = default;
  virtual void ports(const std::function<void(std::string_view name, bool input)>& visit) const               = 0;
  virtual void pins(const std::function<void(std::string_view instance, std::string_view pin)>& visit) const = 0;
  virtual bool has_pin(std::string_view instance, std::string_view pin) const                                = 0;
  virtual void nets(const std::function<void(std::string_view name)>& visit) const                           = 0;
  virtual bool has_net(std::string_view name) const                                                          = 0;
  virtual void cells(const std::function<void(std::string_view name)>& visit) const                          = 0;
  virtual bool has_cell(std::string_view name) const                                                         = 0;
  // False for a design that is not there (the survey of a constraint file alone): every name is then taken to
  // exist, and a pattern is one object.
  [[nodiscard]] virtual bool known() const { return true; }
  // True for a consumer that reads `Sdc::commands` and resolves names itself: a plain port, pin or clock name
  // that a query (without -quiet) does not find is then kept as an object, so the consumer sees the name and
  // applies its own rule for a missing object.
  [[nodiscard]] virtual bool keep_unmatched() const { return false; }
};

struct Sdc {
  struct Clock {
    std::string             name;
    std::vector<Sdc_object> sources;  // ports or pins; empty: a virtual clock
    // The period and the first rising and falling edge, in double: a consumer that puts clock edges on a grid
    // (the domino stage schedule) needs the number of the file, not its nearest float.
    double                  period_ps = 0, rise_ps = 0, fall_ps = 0;
    bool                    waveform_given = false;                   // false: the default, a rise at 0 and a fall at half
    std::optional<float>    uncertainty_ps, slew_ps;
    bool                    generated = false;
    std::string             master;  // of a generated clock, once found
  };
  struct Delay {
    Sdc_object  port;
    std::string clock;
    float       delay_ps = 0;
    bool        input = true;
    bool        min = true, max = true;  // which analysis the number is for
  };
  struct Load {
    Sdc_object port;
    float      load_ff = 0;
  };
  struct Transition {
    Sdc_object port;
    float      slew_ps = 0;
  };
  struct Drive {
    Sdc_object  port;
    std::string cell, pin;  // the library cell and its output pin (empty: its only output)
    float       input_slew_ps = 0;
  };
  // set_false_path. A list holds clocks, ports, pins, nets (through) and cells.
  struct Exception {
    std::vector<Sdc_object> from, through, to;
    bool                    setup = true, hold = true;
  };
  // set_max_delay: the paths from `from` (every start point when empty) to `to` (every endpoint when empty) may
  // take this long from their launch edge, in place of the time to the capture edge.
  struct Path_delay {
    std::vector<Sdc_object> from, to;
    float                   delay_ps = 0;
  };
  // set_clock_groups: no path is timed between clocks of two different groups.
  struct Clock_group {
    std::vector<std::vector<std::string>> groups;
  };
  std::vector<Clock>       clocks;
  std::vector<Delay>       delays;
  std::vector<Load>        loads;
  std::vector<Transition>  transitions;
  std::vector<Drive>       drives;
  std::vector<Exception>   false_paths;
  std::vector<Path_delay>  max_delays;
  std::vector<Clock_group> clock_groups;
  std::vector<Sdc_object>  disabled;  // set_disable_timing: no path through these pins or ports
  std::optional<float>     max_fanout;           // set_max_fanout on the design
  std::vector<std::string> unsupported;
  // The script as the flat list of the SDC commands it ran, in order, for a consumer with its own command
  // handlers: variables, expressions, lists and control flow are gone, a number is a number, and an object
  // list is `[get_ports {a b[3]}]` (`[get_pins ..]`, `[get_nets ..]`, `[get_clocks ..]`, `[get_cells ..]`,
  // `[current_design]`; a clock after -clock is its bare name). A command with an object list that came out
  // empty is not listed: it constrains nothing in this design. An entry with `error` is a command that could
  // not be evaluated (a Tcl error, a failed object query), with the name of the command.
  struct Command {
    size_t      line = 0;
    std::string text, name, error;
  };
  std::vector<Command> commands;
};

// Runs the SDC text against the design. Without a design the constraint file is surveyed alone: every object
// query answers with the pattern itself.
Sdc read_sdc(std::string_view text, Sdc_units units, const Sdc_design& design);
Sdc read_sdc(std::string_view text, Sdc_units units = {});

// The names of a netlist beyond its ports, for `resolve` and for the design the reader queries: the cell of an
// instance, the pin that drives a named net. A binder that has a graph implements it.
class Design_names {
public:
  virtual ~Design_names() = default;
  [[nodiscard]] virtual uint32_t cell(std::string_view instance) const                                              = 0;
  virtual void                   cells(const std::function<void(std::string_view name, uint32_t cell)>& visit) const = 0;
  [[nodiscard]] virtual uint32_t net_driver(std::string_view net) const                                             = 0;
  virtual void                   nets(const std::function<void(std::string_view name, uint32_t driver)>& visit) const = 0;
};

// The design of a netlist as the reader queries it. `names` may be null: a design of ports only.
class Netlist_design final : public Sdc_design {
public:
  Netlist_design(const Netlist& netlist, const Design_names* names) : netlist_(netlist), names_(names) {}
  void ports(const std::function<void(std::string_view, bool)>& visit) const override;
  void pins(const std::function<void(std::string_view, std::string_view)>& visit) const override;
  bool has_pin(std::string_view instance, std::string_view pin) const override;
  void nets(const std::function<void(std::string_view)>& visit) const override;
  bool has_net(std::string_view name) const override;
  void cells(const std::function<void(std::string_view)>& visit) const override;
  bool has_cell(std::string_view name) const override;

private:
  const Netlist&      netlist_;
  const Design_names* names_;
};

// The engine constraints for a netlist: objects resolved to pins, clocks to their source pins, false paths and
// clock groups to cut pins, excluded endpoints and launch groups (see Constraints). What names nothing in the
// netlist, and what the engine cannot express, is recorded in `unsupported` of the result.
Constraints resolve(const Sdc& sdc, const Netlist& netlist, const Design_names* names = nullptr);

// read_sdc against the netlist, then resolve.
Constraints read_constraints(std::string_view text, Sdc_units units, const Netlist& netlist, const Design_names* names = nullptr);

}  // namespace livehd::timing::fast
