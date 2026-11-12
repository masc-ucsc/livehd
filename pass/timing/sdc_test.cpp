// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

#include "gtest/gtest.h"
#include "sdc_tcl.hpp"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;
namespace tcl  = livehd::timing::fast::tcl;

namespace {
constexpr float inf = std::numeric_limits<float>::infinity();

// A netlist with names: ports, cells by instance name, nets by the name given to their driver.
struct Design : fast::Design_names {
  lt::Library                                  library = lt::parse(fast::test::library_text(1.0));
  fast::Cell_types                             types;
  std::unique_ptr<fast::Netlist>               netlist;
  std::map<std::string, uint32_t, std::less<>> cell_by, net_by;

  Design() {
    const lt::Library*             pointer = &library;
    const std::vector<std::string> names{"c0"};
    types   = fast::Cell_types::build(std::span<const lt::Library* const>(&pointer, 1), names);
    netlist = std::make_unique<fast::Netlist>(types);
  }
  uint32_t in(const char* name) { return netlist->pin(netlist->add_input_port(name), 0u); }
  uint32_t out(const char* name) { return netlist->pin(netlist->add_output_port(name), 0u); }
  uint32_t cell(const char* type, const char* name) {
    const auto c = netlist->add_cell(types.find(type));
    cell_by[name] = c;
    return c;
  }
  uint32_t pin(const char* instance, const char* name) const { return netlist->pin(cell_by.find(instance)->second, name); }
  void     wire(uint32_t driver, uint32_t sink) { netlist->connect(driver, sink); }
  uint32_t port(const std::string& name) const {
    for (const auto& p : netlist->ports()) {
      if (p.name == name) {
        return netlist->pin(p.cell, 0u);
      }
    }
    return fast::no_index;
  }
  fast::Constraints read(std::string_view text, fast::Sdc_units units = {}) const { return fast::read_constraints(text, units, *netlist, this); }

  [[nodiscard]] uint32_t cell(std::string_view instance) const override {
    const auto it = cell_by.find(instance);
    return it == cell_by.end() ? fast::no_index : it->second;
  }
  void cells(const std::function<void(std::string_view, uint32_t)>& visit) const override {
    for (const auto& [name, c] : cell_by) {
      visit(name, c);
    }
  }
  [[nodiscard]] uint32_t net_driver(std::string_view net) const override {
    const auto it = net_by.find(net);
    return it == net_by.end() ? fast::no_index : it->second;
  }
  void nets(const std::function<void(std::string_view, uint32_t)>& visit) const override {
    for (const auto& [name, driver] : net_by) {
      visit(name, driver);
    }
  }
};

// Ports only: clock `clk`, inputs `reset`, `a[0]`, `a[1]`, outputs `z`, `y[0]`, `y[1]`.
struct Ports : Design {
  Ports() {
    for (const char* n : {"clk", "reset", "a[0]", "a[1]"}) {
      in(n);
    }
    for (const char* n : {"z", "y[0]", "y[1]"}) {
      out(n);
    }
    netlist->finalize();
  }
};

bool listed(const std::vector<std::string>& unsupported, std::string_view part) {
  return std::ranges::any_of(unsupported, [&](const auto& u) { return u.find(part) != std::string::npos; });
}
std::string all(const std::vector<std::string>& unsupported) {
  std::string text;
  for (const auto& u : unsupported) {
    text += u + "\n";
  }
  return text;
}

std::string run(std::string_view script) {
  tcl::Interp interp([](tcl::Interp::Words&, std::string&) { return false; });
  return interp.eval(script);
}
}  // namespace

// ---- the Tcl the constraint files are written in ----

TEST(SdcTcl, WordsVariablesAndLists) {
  EXPECT_EQ(run("set a 3; set b \"x $a\"; set b"), "x 3");
  EXPECT_EQ(run("set a {x $a [b]}"), "x $a [b]");
  EXPECT_EQ(run("set a 5\nset b ${a}0"), "50");
  EXPECT_EQ(run("set a 1 ;# a comment\n# another\nset a"), "1");
  EXPECT_EQ(run("set a [list 1 {b c} d]\nllength $a"), "3");
  EXPECT_EQ(run("lindex [list 1 {b c} d] 1"), "b c");
  EXPECT_EQ(run("lindex {a b c} end"), "c");
  EXPECT_EQ(run("concat {a b} c {d  e}"), "a b c d  e");
  EXPECT_EQ(run("set l {}; lappend l a; lappend l {b c}; set l"), "a {b c}");
  EXPECT_EQ(run("set a \\\n  7"), "7");
  EXPECT_EQ(run("lsort -unique {b a b c}"), "a b c");
  EXPECT_EQ(run("join {a b c} ,"), "a,b,c");
  EXPECT_EQ(run("lrange {a b c d} 1 2"), "b c");
  EXPECT_EQ(run("set ::g 4; set g"), "4");
  // A list element that needs it is braced, and comes back whole.
  EXPECT_EQ(run("set l [list {a[0]} b]; lindex $l 0"), "a[0]");
}

TEST(SdcTcl, Expressions) {
  EXPECT_EQ(run("expr 1300 / 2"), "650");
  EXPECT_EQ(run("expr 7 / 2"), "3");
  EXPECT_EQ(run("expr -7 / 2"), "-4");  // Tcl rounds an integer division down
  EXPECT_EQ(run("expr 7 / 2.0"), "3.5");
  EXPECT_EQ(run("set p 2500; set f 0.2; expr $p * $f"), "500.0");
  EXPECT_EQ(run("expr {4.0 * 0.683716}"), "2.734864");
  EXPECT_EQ(run("set p 10; expr {$p * 0.2 + 1}"), "3.0");
  EXPECT_EQ(run("expr (1 + 2) * 3"), "9");
  EXPECT_EQ(run("expr {[llength {a b}] == 2}"), "1");
  EXPECT_EQ(run("set d tx; expr {$d eq \"tx\"}"), "1");
  EXPECT_EQ(run("set d rx; expr {$d eq \"tx\" || $d ne \"rx\"}"), "0");
  EXPECT_EQ(run("expr {1 < 2 && 2 <= 2 && 3 > 2 && 2 >= 2 && 1 != 2}"), "1");
  EXPECT_EQ(run("expr {max(1, 2.5, 2)}"), "2.5");
  EXPECT_EQ(run("expr {int(3.7) + round(2.5) + abs(-1)}"), "7");
  EXPECT_EQ(run("expr {1 ? 5 : 6}"), "5");
  EXPECT_EQ(run("expr 1e3 + 1"), "1001.0");
  EXPECT_EQ(run("expr 10 % 3"), "1");
  // The side that is not needed is not evaluated.
  EXPECT_EQ(run("expr {0 && [nosuchcommand]}"), "0");
  EXPECT_EQ(run("expr {[info exists x] && $x > 3}"), "0");
  EXPECT_THROW(run("expr {1 +}"), tcl::Error);
  EXPECT_THROW(run("expr {$undefined + 1}"), tcl::Error);
}

TEST(SdcTcl, ControlFlow) {
  EXPECT_EQ(run("set s 0; foreach x {1 2 3} { set s [expr $s + $x] }; set s"), "6");
  EXPECT_EQ(run("set s {}; foreach {a b} {p 1 q 2} { append s $a=$b, }; set s"), "p=1,q=2,");
  EXPECT_EQ(run("set s {}; foreach {a b} {p 1 q} { append s $a=$b, }; set s"), "p=1,q=,");
  EXPECT_EQ(run("set s 0; foreach x {1 2 3 4} { if {$x == 2} continue; if {$x == 4} break; incr s $x }; set s"), "4");
  EXPECT_EQ(run("if {0} { set r a } elseif {1} { set r b } else { set r c }"), "b");
  EXPECT_EQ(run("if {0} { set r a } elseif {0} { set r b } else { set r c }"), "c");
  EXPECT_EQ(run("if {0} { set r a }"), "");
  EXPECT_EQ(run("set s 0; for {set i 0} {$i < 4} {incr i} { incr s $i }; set s"), "6");
  EXPECT_EQ(run("set i 0; while {$i < 3} { incr i }; set i"), "3");
  EXPECT_EQ(run("proc twice {x {y 1}} { return [expr $x * 2 + $y] }\ntwice 4"), "9");
  EXPECT_EQ(run("proc twice {x {y 1}} { expr $x * 2 + $y }\ntwice 4 0"), "8");
  EXPECT_EQ(run("catch { error boom } m; set m"), "boom");
  EXPECT_EQ(run("catch { set a 1 }"), "0");
  EXPECT_EQ(run("set a 1; unset a; info exists a"), "0");
  EXPECT_THROW(run("error {not here}"), tcl::Error);
  EXPECT_THROW(run("nosuchcommand 1"), tcl::Error);
}

TEST(SdcTcl, StringsFormatAndSearch) {
  EXPECT_EQ(run("format {sync0\\[%d\\]/D} 3"), "sync0\\[3\\]/D");
  EXPECT_EQ(run("format %s_%03d_%.2f a 7 1.5"), "a_007_1.50");
  EXPECT_EQ(run("string match {a*[0-9]} ab7"), "1");
  EXPECT_EQ(run("string length hello"), "5");
  EXPECT_EQ(run("string toupper ab"), "AB");
  EXPECT_EQ(run("string equal a a"), "1");
  EXPECT_EQ(run("regexp {^clk_(.*)$} clk_core whole sub; set sub"), "core");
  EXPECT_EQ(run("regsub -all {\\.} a.b.c / r; set r"), "a/b/c");
  EXPECT_EQ(run("lsearch {a b c} b"), "1");
  EXPECT_EQ(run("lsearch {a b c} z"), "-1");
  EXPECT_EQ(run("lsearch -inline -all -not -exact {a b c b} b"), "a c");
  EXPECT_EQ(run("lsearch -inline -all -not {clk a1 a2 b} a*"), "clk b");
  EXPECT_EQ(run("lsearch -all -regexp {clk a1 a2 b} {^a[0-9]$}"), "1 2");
  EXPECT_EQ(run("lsearch -inline {a b c} c"), "c");
}

// ---- the constraint files of the benchmarks ----

TEST(Sdc, EyerissStyle) {
  Ports      ports;
  const auto c = ports.read(R"(
set ::sta_dynamic_loop_breaking 1
current_design TOP
set clk_name  clk
set clk_port_name clk
set clk_period 2500
set clk_io_pct 0.2
set clk_port [get_ports $clk_port_name]
create_clock -name $clk_name -period $clk_period $clk_port
set non_clock_inputs [lsearch -inline -all -not -exact [all_inputs] $clk_port]
set_input_delay  [expr $clk_period * $clk_io_pct] -clock $clk_name $non_clock_inputs
set_output_delay [expr $clk_period * $clk_io_pct] -clock $clk_name [all_outputs]
# Reset is an asynchronous control input
set_false_path -from [get_ports reset]
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.clocks.size(), 1u);
  EXPECT_EQ(c.clocks[0].name, "clk");
  EXPECT_FLOAT_EQ(c.clocks[0].period_ps, 2500);
  ASSERT_EQ(c.clocks[0].source_pins.size(), 1u);
  EXPECT_EQ(c.clocks[0].source_pins[0], ports.port("clk"));
  // The three non-clock inputs and the three outputs have a 500 ps delay.
  EXPECT_EQ(c.inputs.size(), 3u);
  for (const auto& in : c.inputs) {
    EXPECT_NE(in.port, "clk");
    EXPECT_FLOAT_EQ(in.delay_ps, 500);
    EXPECT_EQ(in.clock, "clk");
    EXPECT_TRUE(in.late && in.early);
  }
  EXPECT_EQ(c.outputs.size(), 3u);
  ASSERT_EQ(c.cut_pins.size(), 1u);
  EXPECT_EQ(c.cut_pins[0], ports.port("reset"));
  EXPECT_EQ(c.groups, 1u);
}

TEST(Sdc, Sha3StyleWithLoadDrivingCellAndFanout) {
  Ports      ports;
  const auto c = ports.read(R"(
current_design sha3
set clk_name  clk
set clk_port_name clk
set clk_period 929
set clk_port [get_ports $clk_port_name]

create_clock -name $clk_name -period $clk_period $clk_port

set non_clock_inputs [all_inputs -no_clocks]
set_input_delay 10 -clock $clk_name $non_clock_inputs
set_output_delay 10 -clock $clk_name [all_outputs]

set_driving_cell -lib_cell BUF -pin Y $non_clock_inputs
set_load [expr 4.0 * 0.5] [all_outputs]
set_max_fanout 32 [current_design]
set_wire_load_mode enclosed
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.inputs.size(), 3u);
  for (const auto& in : c.inputs) {
    EXPECT_EQ(in.drive_type, ports.types.find("BUF"));
    EXPECT_EQ(ports.types.pin(in.drive_type, in.drive_pin).name, "Y");
  }
  ASSERT_EQ(c.outputs.size(), 3u);
  for (const auto& out : c.outputs) {
    EXPECT_FLOAT_EQ(out.load_ff, 2.0f);
    EXPECT_FLOAT_EQ(out.delay_ps, 10);
    EXPECT_TRUE(out.check);
  }
  EXPECT_FLOAT_EQ(c.max_fanout, 32);

  const auto missing = ports.read("create_clock -name c -period 5 [get_ports clk]\nset_driving_cell -lib_cell NOPE -pin QN [all_inputs -no_clocks]\n");
  EXPECT_TRUE(listed(missing.unsupported, "NOPE")) << all(missing.unsupported);
}

TEST(Sdc, PortsByPatternBitsAndFalseToOutput) {
  Ports      ports;
  const auto c = ports.read(R"(
create_clock -name core -period 1000 [get_ports clk]
set_clock_uncertainty 25 [get_clocks core]
set_clock_transition 12 core
set_input_delay 100 -clock core [get_ports {a[0] reset}]
set_input_delay 300 -max -clock core [get_ports a*]
set_input_delay 5 -min -clock core [get_ports a*]
set_output_delay 50 -clock core [get_ports y*]
set_input_transition 20 [all_inputs]
set_load 7 [get_ports z]
set_false_path -to [get_ports z]
set_false_path -from [get_ports nothere]
)");
  EXPECT_FLOAT_EQ(c.clocks[0].uncertainty_ps, 25);
  EXPECT_FLOAT_EQ(c.clocks[0].slew_ps, 12);
  // reset at 100; both bits of a late at 300 and early at 5.
  ASSERT_EQ(c.inputs.size(), 3u);
  for (const auto& in : c.inputs) {
    EXPECT_FLOAT_EQ(in.slew_ps, 20);
    if (in.port == "reset") {
      EXPECT_FLOAT_EQ(in.delay_ps, 100);
      EXPECT_FALSE(in.early_set);
    } else {
      EXPECT_FLOAT_EQ(in.delay_ps, 300);
      EXPECT_TRUE(in.early_set);
      EXPECT_FLOAT_EQ(in.early_delay_ps, 5);
    }
  }
  // The two bits of y are checked; z only carries its load.
  ASSERT_EQ(c.outputs.size(), 3u);
  for (const auto& out : c.outputs) {
    EXPECT_EQ(out.check, out.port != "z");
    EXPECT_FLOAT_EQ(out.load_ff, out.port == "z" ? 7 : 0);
  }
  ASSERT_EQ(c.excluded_endpoints.size(), 1u);
  EXPECT_EQ(c.excluded_endpoints[0], ports.port("z"));
  EXPECT_TRUE(c.cut_pins.empty());
  // A port that is not there is said, and the exception that names only it is not applied to anything.
  EXPECT_TRUE(listed(c.unsupported, "nothere")) << all(c.unsupported);
}

TEST(Sdc, MinAndMaxOnlyDelays) {
  Ports      ports;
  const auto c = ports.read(R"(
create_clock -name c -period 1000 [get_ports clk]
set_input_delay -max 300 -clock c [get_ports reset]
set_input_delay -min 40 -clock c [get_ports {a[0]}]
set_output_delay -min 5 -clock c [get_ports z]
)");
  ASSERT_EQ(c.inputs.size(), 2u);
  for (const auto& in : c.inputs) {
    if (in.port == "reset") {
      EXPECT_TRUE(in.late);
      EXPECT_FALSE(in.early);
      EXPECT_FLOAT_EQ(in.delay_ps, 300);
    } else {
      EXPECT_FALSE(in.late);
      EXPECT_TRUE(in.early);
      EXPECT_FLOAT_EQ(in.delay_ps, 40);
    }
  }
  EXPECT_TRUE(c.outputs.empty());
  EXPECT_TRUE(listed(c.unsupported, "set_output_delay -min"));
}

TEST(Sdc, WhatIsNotModelledIsListedOnceAndTheScriptGoesOn) {
  Ports      ports;
  const auto c = ports.read(R"(
create_clock -name c -period 100 [get_ports clk]
set_multicycle_path 2 -from [get_ports a*]
set_multicycle_path 3 -from [get_ports a*]
set_input_delay [expr $undefined * 2] -clock c [all_inputs]
set_false_path -rise_from [get_ports reset]
set_false_path -from [get_ports reset] -through [get_ports z] -through [get_ports z]
some_tool_command 1 2
foreach p {reset nothere} {
  if {[llength [get_ports -quiet $p]] == 0} {
    error "port $p is missing"
  }
  set_input_delay 1 -clock c [get_ports $p]
}
set_output_delay 7 -clock c [get_ports z]
)");
  size_t multicycle = 0;
  for (const auto& u : c.unsupported) {
    multicycle += u.starts_with("set_multicycle_path");
  }
  EXPECT_EQ(multicycle, 1u);
  EXPECT_TRUE(listed(c.unsupported, "undefined")) << all(c.unsupported);
  EXPECT_TRUE(listed(c.unsupported, "-rise_from"));
  EXPECT_TRUE(listed(c.unsupported, "more than one -through"));
  EXPECT_TRUE(listed(c.unsupported, "some_tool_command"));
  EXPECT_TRUE(listed(c.unsupported, "port nothere is missing"));
  // The loop set the delay of reset before it failed, and the command after the loop still ran.
  ASSERT_EQ(c.inputs.size(), 1u);
  EXPECT_EQ(c.inputs[0].port, "reset");
  ASSERT_EQ(c.outputs.size(), 1u);
  EXPECT_FLOAT_EQ(c.outputs[0].delay_ps, 7);
}

TEST(Sdc, UnitsContinuationAndWaveform) {
  Ports      ports;
  const auto c = ports.read("create_clock -name c \\\n -period 2 -waveform [list 0 [expr 2 / 2]] [get_ports clk]\nset_load 3 [all_outputs]\n"
                            "set_clock_transition -rise -min 0.1 [get_clocks c]\nset_clock_transition -fall -max 0.2 [get_clocks {c}]\n",
                            {.time_ps = 1000, .capacitance_ff = 1000});
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.clocks.size(), 1u);
  EXPECT_FLOAT_EQ(c.clocks[0].period_ps, 2000);
  EXPECT_FLOAT_EQ(c.clocks[0].slew_ps, 200);
  ASSERT_EQ(c.outputs.size(), 3u);
  EXPECT_FLOAT_EQ(c.outputs[0].load_ff, 3000);
  EXPECT_FALSE(c.outputs[0].check);
}

TEST(Sdc, SurveyWithoutADesign) {
  const auto sdc = fast::read_sdc(R"(
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [lsearch -inline -all -not -exact [all_inputs] [get_ports clk]]
set_false_path -from [get_ports rst] -to [get_pins -hierarchical -regexp {^u\.sync\[0\]/D$}]
set_false_path -through [get_nets u.q]
)");
  EXPECT_TRUE(sdc.unsupported.empty()) << all(sdc.unsupported);
  ASSERT_EQ(sdc.clocks.size(), 1u);
  ASSERT_EQ(sdc.false_paths.size(), 2u);
  EXPECT_EQ(sdc.false_paths[0].to.front().kind, fast::Sdc_object::Kind::pin);
  EXPECT_EQ(sdc.false_paths[1].through.front().kind, fast::Sdc_object::Kind::net);
}

// ---- the script as flat commands, for a consumer with its own command handlers (pass.opentimer) ----

TEST(Sdc, FlatCommands) {
  Ports                  ports;
  const fast::Netlist_design design(*ports.netlist, &ports);
  const auto             sdc = fast::read_sdc(R"(current_design top
set clk_name core
set clk_period 1300
set clk_port [get_ports clk]
create_clock -name $clk_name -period $clk_period -waveform [list 0 [expr $clk_period / 2]] $clk_port
set non_clock_inputs [lsearch -inline -all -not -exact [all_inputs] $clk_port]
set_input_delay [expr $clk_period * 0.2] -clock $clk_name $non_clock_inputs
set_input_delay 5 -clock [get_clocks core] [all_inputs -no_clocks]
set_output_delay 10 -clock $clk_name [all_outputs]
foreach p {reset a} {
  set_false_path -from [get_ports $p]
}
set_false_path -from [get_ports -quiet nothere]
set_max_fanout 8 [current_design]
set_input_delay $oops -clock core [all_inputs]
set_load 2 [get_ports {y[1]}]
)",
                                      {}, design);
  std::vector<std::string> text;
  for (const auto& command : sdc.commands) {
    text.push_back(command.error.empty() ? command.text : "error in " + command.name + ": " + command.error);
  }
  const std::vector<std::string> expected{
      "create_clock -name core -period 1300 -waveform {0 650} [get_ports {clk}]",
      // A list that was filtered names its ports; a whole [all_inputs] or [all_outputs] stays the selector.
      "set_input_delay 260.0 -clock core [get_ports {reset a[0] a[1]}]",
      "set_input_delay 5 -clock core [all_inputs -no_clocks]",
      "set_output_delay 10 -clock core [all_outputs]",
      "set_false_path -from [get_ports {reset}]",
      "set_false_path -from [get_ports {a[0] a[1]}]",
      // The exception on a port that is not there is not a command of this design.
      "set_max_fanout 8 [current_design]",
      "error in set_input_delay: can't read \"oops\": no such variable",
      "set_load 2 [get_ports {y[1]}]",
  };
  EXPECT_EQ(text, expected);
  // The line of each command is the line of its top-level command in the file.
  ASSERT_EQ(sdc.commands.size(), expected.size());
  EXPECT_EQ(sdc.commands[0].line, 5u);
  EXPECT_EQ(sdc.commands[4].line, 10u);
  EXPECT_EQ(sdc.commands[5].line, 10u);
  EXPECT_EQ(sdc.commands[7].line, 15u);
  EXPECT_EQ(sdc.commands[8].line, 16u);
}

// ---- clocks on pins, generated clocks, clock groups (litedram, litepci, liteeth, NVDLA partition_o) ----

namespace {
// clk_a and clk_b; ffa (clk_a) <- in; ffb (clk_b) <- INV(ffa.Q); ffc (clk_a) <- XOR(ffa.Q, ffb.Q); out <- ffc.Q.
struct Two_clocks : Design {
  uint32_t ffa, ffb, ffc, inv, x;
  Two_clocks() {
    const auto clk_a = in("clk_a"), clk_b = in("clk_b"), d = in("in"), o = out("out");
    ffa = cell("FF", "ffa");
    ffb = cell("FF", "ffb");
    ffc = cell("FF", "ffc");
    inv = cell("INV", "i1");
    x   = cell("XOR2", "x1");
    wire(clk_a, pin("ffa", "CLK"));
    wire(clk_a, pin("ffc", "CLK"));
    wire(clk_b, pin("ffb", "CLK"));
    wire(d, pin("ffa", "D"));
    wire(pin("ffa", "Q"), pin("i1", "A"));
    wire(pin("ffa", "Q"), pin("x1", "A"));
    wire(pin("i1", "Y"), pin("ffb", "D"));
    wire(pin("ffb", "Q"), pin("x1", "B"));
    wire(pin("x1", "Y"), pin("ffc", "D"));
    wire(pin("ffc", "Q"), o);
    netlist->finalize();
  }
};
const char* two_clock_text = R"(
create_clock -name a -period 1000 [get_ports clk_a]
create_clock -name b -period 1500 [get_ports clk_b]
set_input_delay 100 -clock a [get_ports in]
set_output_delay 100 -clock a [get_ports out]
)";
}  // namespace

TEST(Sdc, TwoClocksOfDifferentPeriods) {
  Two_clocks d;
  const auto c = d.read(two_clock_text);
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  // Each clock launches in its own group: from a rising edge of a (0, 1000, 2000) the next edge of b (1500,
  // 3000) is at least 500 away, and so is the next edge of a from one of b.
  ASSERT_EQ(c.groups, 2u);
  const auto ga = c.clocks[0].group, gb = c.clocks[1].group;
  EXPECT_NE(ga, gb);
  EXPECT_FLOAT_EQ(c.setup_relation_ps[ga * 2 + 0], 1000);
  EXPECT_FLOAT_EQ(c.setup_relation_ps[ga * 2 + 1], 500);
  EXPECT_FLOAT_EQ(c.setup_relation_ps[gb * 2 + 0], 500);
  EXPECT_FLOAT_EQ(c.setup_relation_ps[gb * 2 + 1], 1500);
  EXPECT_FLOAT_EQ(c.hold_relation_ps[ga * 2 + 0], 0);
  EXPECT_FLOAT_EQ(c.hold_relation_ps[ga * 2 + 1], 0);  // the edges at 0 coincide
  EXPECT_EQ(c.inputs.at(0).group, ga);

  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  const auto dc = d.pin("ffc", "D"), db = d.pin("ffb", "D");
  // ffb is reached only from clock a, in a's group, and is checked against the 500 ps relation.
  EXPECT_EQ(engine.arrival(db, 0, 0, gb), -inf);
  EXPECT_NEAR(engine.required(db, 0, 0, ga), 500 - 10, 1e-3);
  // ffc is reached from both: each group has its own arrival and its own requirement.
  EXPECT_NE(engine.arrival(dc, 0, 0, ga), -inf);
  EXPECT_NE(engine.arrival(dc, 0, 0, gb), -inf);
  EXPECT_NEAR(engine.required(dc, 0, 0, ga), 1000 - 10, 1e-3);
  EXPECT_NEAR(engine.required(dc, 0, 0, gb), 500 - 10, 1e-3);
  float worst = inf;
  for (const auto g : {ga, gb}) {
    for (unsigned t = 0; t < 2; ++t) {
      worst = std::min(worst, engine.required(dc, 0, t, g) - engine.arrival(dc, 0, t, g));
    }
  }
  EXPECT_FLOAT_EQ(engine.slack(dc), worst);
  EXPECT_EQ(engine.report().groups, 2u);
}

TEST(Sdc, FalsePathBetweenClocksLeavesTheOtherPaths) {
  Two_clocks  d;
  const auto  blocked = d.read(std::string(two_clock_text) + R"(
set_false_path -from [get_clocks b] -to [get_clocks a]
set_false_path -from [get_clocks a] -to [get_clocks b]
)");
  EXPECT_TRUE(blocked.unsupported.empty()) << all(blocked.unsupported);
  ASSERT_EQ(blocked.groups, 2u);
  const auto ga = blocked.clocks[0].group, gb = blocked.clocks[1].group;
  EXPECT_EQ(blocked.setup_relation_ps[ga * 2 + 1], inf);
  EXPECT_EQ(blocked.setup_relation_ps[gb * 2 + 0], inf);
  EXPECT_EQ(blocked.hold_relation_ps[gb * 2 + 0], inf);

  fast::Engine engine(*d.netlist, blocked);
  engine.evaluate();
  const auto dc = d.pin("ffc", "D"), db = d.pin("ffb", "D");
  // a -> b is not checked at all; ffc keeps the path from ffa and loses the one from ffb.
  EXPECT_EQ(engine.slack(db), inf);
  EXPECT_EQ(engine.hold_slack(db), inf);
  EXPECT_NE(engine.slack(dc), inf);
  EXPECT_EQ(engine.required(dc, 0, 0, gb), inf);

  // The same netlist with one clock domain only and the pin from ffb cut: the path from ffa, alone.
  auto reference = d.read(two_clock_text);
  reference.cut_pins.push_back(d.pin("x1", "B"));
  fast::Engine alone(*d.netlist, reference);
  alone.evaluate();
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_FLOAT_EQ(engine.arrival(dc, 0, t, ga), alone.arrival(dc, 0, t, reference.clocks[0].group));
  }
  EXPECT_FLOAT_EQ(engine.slack(dc), alone.slack(dc));
  EXPECT_FLOAT_EQ(engine.hold_slack(dc), alone.hold_slack(dc));
  // The worst path ends at an endpoint that is still checked and starts in clock a.
  const auto path = engine.worst_path();
  ASSERT_FALSE(path.empty());
  EXPECT_NE(path.back().pin, db);
}

TEST(Sdc, AsynchronousClockGroups) {
  Two_clocks d;
  const auto groups = d.read(std::string(two_clock_text) + "set_clock_groups -asynchronous -group [get_clocks a] -group [get_clocks b]\n");
  const auto paths  = d.read(std::string(two_clock_text)
                             + "set_false_path -from [get_clocks b] -to [get_clocks a]\nset_false_path -from [get_clocks a] -to [get_clocks b]\n");
  EXPECT_TRUE(groups.unsupported.empty()) << all(groups.unsupported);
  EXPECT_EQ(groups.setup_relation_ps, paths.setup_relation_ps);
  EXPECT_EQ(groups.hold_relation_ps, paths.hold_relation_ps);
  // One group named: it is apart from every other clock.
  const auto one = d.read(std::string(two_clock_text) + "set_clock_groups -asynchronous -group {a}\n");
  EXPECT_EQ(one.setup_relation_ps, paths.setup_relation_ps);
  // Clocks of one period and no exception share a group: nothing is paid for a second clock.
  const auto same = d.read("create_clock -name a -period 1000 [get_ports clk_a]\ncreate_clock -name b -period 1000 [get_ports clk_b]\n");
  EXPECT_EQ(same.groups, 1u);
}

TEST(Sdc, GeneratedClockOnAPinAndOnAnOutputPort) {
  // clk -> div (a flip-flop that divides by two) -> the clock of ffx; clk also leaves through the port clk_out.
  Design d;
  const auto clk = d.in("clk"), din = d.in("din"), clk_out = d.out("clk_out"), q = d.out("q");
  d.cell("FF", "div");
  d.cell("INV", "divn");
  d.cell("FF", "ffx");
  d.cell("FF", "ff0");
  d.cell("BUF", "fwd");
  d.wire(clk, d.pin("div", "CLK"));
  d.wire(clk, d.pin("ff0", "CLK"));
  d.wire(clk, d.pin("fwd", "A"));
  d.wire(d.pin("fwd", "Y"), clk_out);
  d.wire(d.pin("div", "Q"), d.pin("divn", "A"));
  d.wire(d.pin("div", "Q"), d.pin("ffx", "CLK"));
  d.wire(d.pin("divn", "Y"), d.pin("div", "D"));
  d.wire(din, d.pin("ff0", "D"));
  d.wire(d.pin("ff0", "Q"), d.pin("ffx", "D"));
  d.wire(d.pin("ffx", "Q"), q);
  d.netlist->finalize();
  const auto c = d.read(R"(
set clk_port [get_ports clk]
create_clock -name clk -period 1000 $clk_port
set driver [get_pins -quiet div/Q]
if {[llength $driver] == 1} {
  create_generated_clock -name half -source $clk_port -divide_by 2 $driver
} else {
  error "the divider is missing"
}
create_generated_clock -name fwd -source [get_ports clk] -divide_by 1 [get_ports clk_out]
set_input_delay 100 -clock clk [all_inputs -no_clocks]
set_output_delay 100 -clock clk [all_outputs]
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.clocks.size(), 3u);
  EXPECT_EQ(c.clocks[1].name, "half");
  EXPECT_FLOAT_EQ(c.clocks[1].period_ps, 2000);
  ASSERT_EQ(c.clocks[1].source_pins.size(), 1u);
  EXPECT_EQ(c.clocks[1].source_pins[0], d.pin("div", "Q"));
  EXPECT_FLOAT_EQ(c.clocks[2].period_ps, 1000);
  EXPECT_EQ(c.clocks[2].source_pins[0], clk_out);

  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // ffx is clocked by the generated clock, the flip-flop on clk by clk, and nothing is unclocked.
  EXPECT_EQ(engine.report().unclocked_flops, 0u);
  EXPECT_TRUE(engine.clock_network(d.pin("ffx", "CLK")));
  EXPECT_TRUE(engine.clock_network(d.pin("div", "Q")));
  EXPECT_TRUE(engine.clock_network(clk_out));
  // clk -> half: from an edge of clk (0, 1000) to the next edge of half (2000): 1000.
  const auto dx = d.pin("ffx", "D");
  EXPECT_NEAR(engine.required(dx, 0, 0), 1000 - 10, 1e-3);
  // The forwarded clock is not a data endpoint; q is, launched by `half` and captured by clk a period later.
  bool has_clk_out = false, has_q = false;
  for (const auto pin : engine.endpoint_pins()) {
    has_clk_out = has_clk_out || pin == clk_out;
    has_q       = has_q || pin == q;
  }
  EXPECT_FALSE(has_clk_out);
  EXPECT_TRUE(has_q);
  EXPECT_NEAR(engine.required(q, 0, 0), 1000 - 100, 1e-3);
}

TEST(Sdc, ClockOnThePinOfACellAndAVirtualClock) {
  // The clock comes out of a cell inside the design (a macro in litepci; a buffer here).
  Design d;
  const auto ref = d.in("ref"), din = d.in("din"), q = d.out("q");
  d.cell("BUF", "pll");
  d.cell("FF", "ff");
  d.wire(ref, d.pin("pll", "A"));
  d.wire(d.pin("pll", "Y"), d.pin("ff", "CLK"));
  d.wire(din, d.pin("ff", "D"));
  d.wire(d.pin("ff", "Q"), q);
  d.netlist->finalize();
  const auto c = d.read(R"(
create_clock -name sys -period 800 [get_pins pll/Y]
create_clock -name ref -period 10000 [get_ports ref]
create_clock -name io -period 800
set_clock_groups -asynchronous -group [get_clocks sys] -group [get_clocks ref]
set_input_delay 100 -clock io [get_ports din]
set_output_delay 100 -clock io [get_ports q]
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.clocks.size(), 3u);
  EXPECT_EQ(c.clocks[0].source_pins.front(), d.pin("pll", "Y"));
  EXPECT_TRUE(c.clocks[2].source_pins.empty());
  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  EXPECT_EQ(engine.report().unclocked_flops, 0u);
  // The flip-flop is clocked by sys, not by ref whose network ends at the pin sys is made on.
  EXPECT_NEAR(engine.required(d.pin("ff", "D"), 0, 0), 800 - 10, 1e-3);
  EXPECT_EQ(engine.report().endpoints, 2u);

  const auto gone = d.read("create_clock -name sys -period 800 [get_pins nothere/Y]\n");
  EXPECT_TRUE(listed(gone.unsupported, "nothere/Y")) << all(gone.unsupported);
}

// ---- false paths on pins and nets (NVDLA partition_c, NyuziProcessor) ----

namespace {
// in -> ff1.D through an XOR with ffq.Q, in -> ff2.D directly; tck -> sync.D directly; rst -> a net named
// `u.rstn` (the output of a buffer) -> the XOR of ff3.
struct Exceptions : Design {
  Exceptions() {
    const auto clk = in("clk"), din = in("in"), tck = in("tck"), rst = in("rst");
    for (const char* name : {"ffq", "ff1", "ff2", "ff3", "u.sync0[3]"}) {
      cell("FF", name);
      wire(clk, pin(name, "CLK"));
    }
    cell("XOR2", "x1");
    cell("XOR2", "x3");
    cell("BUF", "rbuf");
    wire(din, pin("x1", "A"));
    wire(pin("ffq", "Q"), pin("x1", "B"));
    wire(pin("x1", "Y"), pin("ff1", "D"));
    wire(din, pin("ff2", "D"));
    wire(tck, pin("u.sync0[3]", "D"));
    wire(rst, pin("rbuf", "A"));
    wire(pin("rbuf", "Y"), pin("x3", "A"));
    wire(pin("ffq", "Q"), pin("x3", "B"));
    wire(pin("x3", "Y"), pin("ff3", "D"));
    wire(pin("ff1", "Q"), pin("ffq", "D"));
    net_by["u.rstn"] = pin("rbuf", "Y");
    netlist->finalize();
  }
};
const char* exception_clock = "create_clock -name clk -period 1000 [get_ports clk]\nset_input_delay 100 -clock clk [all_inputs -no_clocks]\n";
}  // namespace

TEST(Sdc, FalsePathThroughANetOfTheDesign) {
  Exceptions d;
  const auto c = d.read(std::string(exception_clock) + R"(
foreach reset_sync_net {
    u.rstn
} {
  set net [get_nets $reset_sync_net]
  if {[llength $net] == 0} {
    error "constraint.sdc: reset synchronizer net $reset_sync_net not found -- update this false-path"
  }
  set_false_path -through $net
}
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.cut_pins.size(), 1u);
  EXPECT_EQ(c.cut_pins[0], d.pin("rbuf", "Y"));
  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // ff3 keeps the path from ffq and loses the one through the net.
  auto reference = d.read(exception_clock);
  reference.cut_pins.push_back(d.pin("x3", "A"));
  fast::Engine alone(*d.netlist, reference);
  alone.evaluate();
  EXPECT_FLOAT_EQ(engine.slack(d.pin("ff3", "D")), alone.slack(d.pin("ff3", "D")));

  // The same file on a netlist without the net stops at its own error, which is listed.
  const auto missing = d.read(std::string(exception_clock)
                              + "set net [get_nets -quiet nope]\nif {[llength $net] == 0} { error \"net nope not found\" }\nset_false_path -through $net\n");
  EXPECT_TRUE(listed(missing.unsupported, "net nope not found")) << all(missing.unsupported);
  EXPECT_TRUE(missing.cut_pins.empty());
}

TEST(Sdc, FalsePathFromAPortToTheOnlyPinItFeedsIsAnExclusion) {
  Exceptions d;
  const auto c = d.read(std::string(exception_clock) + R"(
foreach {jtag_port jtag_bit} {
    tck 3
} {
    set jtag_pin [get_pins -hierarchical -regexp [format \
        {^u\.sync0\[%d\]/D$} \
        $jtag_bit]]
    if {[llength $jtag_pin] != 1} {
        error "Expected one first-stage synchronizer pin for $jtag_port"
    }
    set_false_path -from [get_ports $jtag_port] -to $jtag_pin
}
unset jtag_port jtag_bit jtag_pin
)");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  // Nothing else reaches that pin, so the check goes and no launch group is needed.
  EXPECT_EQ(c.groups, 1u);
  ASSERT_EQ(c.excluded_endpoints.size(), 1u);
  EXPECT_EQ(c.excluded_endpoints[0], d.pin("u.sync0[3]", "D"));
  EXPECT_TRUE(c.cut_pins.empty());
  // A wildcard query finds the same pin.
  const auto wild = d.read(std::string(exception_clock) + "set_false_path -from [get_ports tck] -to [get_pins {u.sync0*/D}]\n");
  EXPECT_EQ(wild.excluded_endpoints, c.excluded_endpoints);
}

TEST(Sdc, FalsePathFromAPortToOneOfThePinsItFeedsIsALaunchGroup) {
  Exceptions d;
  const auto c = d.read(std::string(exception_clock) + "set_false_path -from [get_ports in] -to [get_pins ff1/D]\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  // `in` also reaches ff2, and ff1 is also reached from ffq: neither a cut nor an exclusion is right.
  EXPECT_TRUE(c.cut_pins.empty());
  EXPECT_TRUE(c.excluded_endpoints.empty());
  ASSERT_EQ(c.groups, 2u);
  uint32_t group = 0;
  for (const auto& in : c.inputs) {
    if (in.port == "in") {
      group = in.group;
    } else {
      EXPECT_EQ(in.group, c.clocks[0].group);
    }
  }
  EXPECT_NE(group, c.clocks[0].group);
  ASSERT_EQ(c.endpoint_relations.size(), 1u);
  EXPECT_EQ(c.endpoint_relations[0].pin, d.pin("ff1", "D"));
  EXPECT_EQ(c.endpoint_relations[0].group, group);
  EXPECT_EQ(c.endpoint_relations[0].setup_ps, inf);

  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // ff1: the path from ffq alone; ff2: still the path from `in`.
  auto reference = d.read(exception_clock);
  fast::Engine plain(*d.netlist, reference);
  plain.evaluate();
  reference.cut_pins.push_back(d.pin("x1", "A"));
  fast::Engine alone(*d.netlist, reference);
  alone.evaluate();
  EXPECT_FLOAT_EQ(engine.slack(d.pin("ff1", "D")), alone.slack(d.pin("ff1", "D")));
  EXPECT_GT(engine.slack(d.pin("ff1", "D")), plain.slack(d.pin("ff1", "D")));
  EXPECT_FLOAT_EQ(engine.slack(d.pin("ff2", "D")), plain.slack(d.pin("ff2", "D")));
  EXPECT_FLOAT_EQ(engine.hold_slack(d.pin("ff1", "D")), alone.hold_slack(d.pin("ff1", "D")));
}

// ---- set_max_delay ----

TEST(Sdc, MaxDelayOnABlockWithoutAClock) {
  // a -> INV -> XOR <- b, to the port z: no clock anywhere.
  Design d;
  const auto a = d.in("a"), b = d.in("b"), z = d.out("z");
  d.cell("INV", "i1");
  d.cell("XOR2", "x1");
  d.wire(a, d.pin("i1", "A"));
  d.wire(d.pin("i1", "Y"), d.pin("x1", "A"));
  d.wire(b, d.pin("x1", "B"));
  d.wire(d.pin("x1", "Y"), z);
  d.netlist->finalize();
  const auto c = d.read("set_max_delay 100 -from [all_inputs] -to [all_outputs]\nset_load 3 [all_outputs]\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  // The endpoint is reported under a virtual clock named after the command; nothing else reaches the port, so
  // the delay is the port's for every start and no launch group is needed.
  ASSERT_EQ(c.clocks.size(), 1u);
  EXPECT_EQ(c.clocks[0].name, "set_max_delay");
  EXPECT_FLOAT_EQ(c.clocks[0].period_ps, 100);
  EXPECT_EQ(c.groups, 1u);
  ASSERT_EQ(c.outputs.size(), 1u);
  EXPECT_TRUE(c.outputs[0].check);
  EXPECT_FALSE(c.outputs[0].clocked);
  EXPECT_FLOAT_EQ(c.outputs[0].load_ff, 3);
  ASSERT_EQ(c.endpoint_relations.size(), 1u);
  EXPECT_EQ(c.endpoint_relations[0].pin, z);
  EXPECT_EQ(c.endpoint_relations[0].group, fast::no_index);
  EXPECT_FLOAT_EQ(c.endpoint_relations[0].setup_ps, 100);

  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  ASSERT_EQ(engine.report().endpoints, 1u);
  // The inputs launch at 0, and the whole delay is the requirement: no setup time, no output delay.
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_FLOAT_EQ(engine.required(z, 0, t), 100);
  }
  const float longest = std::max(engine.arrival(z, 0, 0), engine.arrival(z, 0, 1));
  EXPECT_GT(longest, 30);  // an inverter and an XOR
  EXPECT_FLOAT_EQ(engine.slack(z), 100 - longest);
  EXPECT_FLOAT_EQ(engine.report().wns_ps, 100 - longest);
  // A tighter delay fails by exactly the difference.
  fast::Engine tight(*d.netlist, d.read("set_max_delay 20 -from [all_inputs] -to [all_outputs]\nset_load 3 [all_outputs]\n"));
  tight.evaluate();
  EXPECT_FLOAT_EQ(tight.report().wns_ps, 20 - longest);
}

namespace {
// in -> INV -> XOR <- ffq.Q, to the port out; in -> ffd.D as well.
struct Feedthrough : Design {
  uint32_t out_pin;
  Feedthrough() {
    const auto clk = in("clk"), din = in("in");
    out_pin = out("out");
    cell("FF", "ffq");
    cell("FF", "ffd");
    cell("INV", "i1");
    cell("XOR2", "x1");
    wire(clk, pin("ffq", "CLK"));
    wire(clk, pin("ffd", "CLK"));
    wire(din, pin("i1", "A"));
    wire(din, pin("ffd", "D"));
    wire(pin("i1", "Y"), pin("x1", "A"));
    wire(pin("ffq", "Q"), pin("x1", "B"));
    wire(pin("x1", "Y"), out_pin);
    wire(pin("ffd", "Q"), pin("ffq", "D"));
    netlist->finalize();
  }
};
const char* feedthrough_clock = "create_clock -name clk -period 1000 [get_ports clk]\nset_input_delay 100 -clock clk [get_ports in]\n";
}  // namespace

TEST(Sdc, MaxDelayFromAPortToAPortIsForThatPathOnly) {
  Feedthrough d;
  const auto  c = d.read(std::string(feedthrough_clock)
                         + "set_output_delay 100 -clock clk [get_ports out]\nset_max_delay 180 -from [get_ports in] -to [get_ports out]\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  // The port out is also reached from ffq, so the port in launches in a group of its own.
  ASSERT_EQ(c.groups, 2u);
  const auto clock_group = c.clocks[0].group, port_group = c.inputs.at(0).group;
  EXPECT_NE(clock_group, port_group);
  ASSERT_EQ(c.endpoint_relations.size(), 1u);
  EXPECT_EQ(c.endpoint_relations[0].group, port_group);
  EXPECT_FLOAT_EQ(c.endpoint_relations[0].setup_ps, 180);

  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // From the port: the delay less the output delay (the input delay is part of the arrival). From the
  // flip-flop: the period less the output delay.
  EXPECT_FLOAT_EQ(engine.required(d.out_pin, 0, 0, port_group), 180 - 100);
  EXPECT_FLOAT_EQ(engine.required(d.out_pin, 0, 0, clock_group), 1000 - 100);
  EXPECT_GE(engine.arrival(d.out_pin, 0, 0, port_group), 100);
  // The same port into the flip-flop keeps the clock relation.
  EXPECT_NEAR(engine.required(d.pin("ffd", "D"), 0, 0, port_group), 1000 - 10, 1e-3);
  // Hold is not changed by a maximum delay.
  fast::Engine plain(*d.netlist, d.read(std::string(feedthrough_clock) + "set_output_delay 100 -clock clk [get_ports out]\n"));
  plain.evaluate();
  EXPECT_FLOAT_EQ(engine.hold_slack(d.pin("ffd", "D")), plain.hold_slack(d.pin("ffd", "D")));
  EXPECT_LT(engine.slack(d.out_pin), plain.slack(d.out_pin));
}

TEST(Sdc, MaxDelayMakesAnOutputWithoutADelayAnEndpointOfThatPath) {
  Feedthrough d;
  const auto  c = d.read(std::string(feedthrough_clock) + "set_max_delay 180 -from [get_ports in] -to [get_ports out]\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  ASSERT_EQ(c.outputs.size(), 1u);
  EXPECT_FALSE(c.outputs[0].clocked);
  const auto clock_group = c.clocks[0].group, port_group = c.inputs.at(0).group;
  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // Checked from the port, with the whole delay; from the flip-flop the port stays unconstrained.
  EXPECT_FLOAT_EQ(engine.required(d.out_pin, 0, 0, port_group), 180);
  EXPECT_EQ(engine.required(d.out_pin, 0, 0, clock_group), inf);
  // Without -to, every endpoint the port reaches has the delay: the flip-flop (less its setup) and the output.
  const auto   from_only = d.read(std::string(feedthrough_clock) + "set_max_delay 70 -from [get_ports in]\n");
  fast::Engine all_ends(*d.netlist, from_only);
  all_ends.evaluate();
  const auto g = from_only.inputs.at(0).group;
  EXPECT_NEAR(all_ends.required(d.pin("ffd", "D"), 0, 0, g), 70 - 10, 1e-3);
  EXPECT_FLOAT_EQ(all_ends.required(d.out_pin, 0, 0, g), 70);
  EXPECT_NEAR(all_ends.required(d.pin("ffq", "D"), 0, 0, from_only.clocks[0].group), 1000 - 10, 1e-3);
}

TEST(Sdc, MaxDelayBetweenClocksAndToAPin) {
  Two_clocks d;
  // a -> b is 500 by the clock edges; the delay replaces it. b -> a keeps its 500.
  const auto c = d.read(std::string(two_clock_text) + "set_max_delay 300 -from [get_clocks a] -to [get_clocks b] -ignore_clock_latency\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  const auto ga = c.clocks[0].group, gb = c.clocks[1].group;
  EXPECT_FLOAT_EQ(c.setup_relation_ps[ga * 2 + 1], 300);
  EXPECT_FLOAT_EQ(c.setup_relation_ps[gb * 2 + 0], 500);
  EXPECT_FLOAT_EQ(c.hold_relation_ps[ga * 2 + 1], 0);
  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  EXPECT_NEAR(engine.required(d.pin("ffb", "D"), 0, 0, ga), 300 - 10, 1e-3);

  // A false path or asynchronous groups between the two clocks win over the delay.
  for (const char* stronger : {"set_false_path -from [get_clocks a] -to [get_clocks b]\n", "set_clock_groups -asynchronous -group a -group b\n"}) {
    const auto both = d.read(std::string(two_clock_text) + "set_max_delay 300 -from [get_clocks a] -to [get_clocks b]\n" + stronger);
    EXPECT_EQ(both.setup_relation_ps[both.clocks[0].group * 2 + 1], inf) << stronger;
    // ... also over a delay to a pin of the other domain.
    const auto   to_pin = d.read(std::string(two_clock_text) + "set_max_delay 300 -to [get_pins ffb/D]\n" + stronger);
    fast::Engine unchecked(*d.netlist, to_pin);
    unchecked.evaluate();
    EXPECT_EQ(unchecked.slack(d.pin("ffb", "D")), inf) << stronger;
  }

  // To a pin, whatever the start: the delay less the setup time, for every group.
  const auto to_pin = d.read(std::string(two_clock_text) + "set_max_delay 250 -to [get_pins ffc/D]\n");
  ASSERT_EQ(to_pin.endpoint_relations.size(), 1u);
  EXPECT_EQ(to_pin.endpoint_relations[0].group, fast::no_index);
  fast::Engine pinned(*d.netlist, to_pin);
  pinned.evaluate();
  for (const auto g : {to_pin.clocks[0].group, to_pin.clocks[1].group}) {
    EXPECT_NEAR(pinned.required(d.pin("ffc", "D"), 0, 0, g), 250 - 10, 1e-3);
  }
  // Of two delays on one path the smaller rules.
  const auto twice = d.read(std::string(two_clock_text) + "set_max_delay 400 -to [get_pins ffc/D]\nset_max_delay 250 -to [get_pins ffc/D]\n");
  ASSERT_EQ(twice.endpoint_relations.size(), 1u);
  EXPECT_FLOAT_EQ(twice.endpoint_relations[0].setup_ps, 250);
}

TEST(Sdc, MaxDelayFormsThatAreNotModelledAreListed) {
  Exceptions d;
  const auto c = d.read(std::string(exception_clock) + R"(
set_max_delay 50 -from [get_ports in] -through [get_nets u.rstn]
set_max_delay 50 -rise_from [get_ports in]
set_max_delay 50 -from [get_pins ffq/CLK] -to [get_pins ff1/D]
set_max_delay 50 -from [get_ports nothere] -to [get_pins ff1/D]
set_max_delay 50
set_min_delay 5 -to [get_pins ff1/D]
)");
  EXPECT_TRUE(listed(c.unsupported, "set_max_delay -through")) << all(c.unsupported);
  EXPECT_TRUE(listed(c.unsupported, "set_max_delay -rise_from"));
  EXPECT_TRUE(listed(c.unsupported, "set_max_delay from a register"));
  EXPECT_TRUE(listed(c.unsupported, "nothere"));
  EXPECT_TRUE(listed(c.unsupported, "set_max_delay without -from or -to"));
  EXPECT_TRUE(listed(c.unsupported, "set_min_delay"));
  // None of them changed a relation.
  EXPECT_TRUE(c.endpoint_relations.empty());
  EXPECT_EQ(c.groups, 1u);
}

// ---- driving cells and the fanout limit in the engine ----

TEST(Sdc, DrivingCellAddsItsLoadDependentDelay) {
  Design d;
  const auto clk = d.in("clk"), a = d.in("a");
  d.cell("INV", "i1");
  d.cell("FF", "ff");
  d.wire(clk, d.pin("ff", "CLK"));
  d.wire(a, d.pin("i1", "A"));
  d.wire(d.pin("i1", "Y"), d.pin("ff", "D"));
  d.netlist->finalize();
  const std::string text = "create_clock -name clk -period 1000 [get_ports clk]\nset_input_delay 100 -clock clk [get_ports a]\n";
  const auto        c    = d.read(text + "set_driving_cell -lib_cell BUF -pin Y [get_ports a]\n");
  EXPECT_TRUE(c.unsupported.empty()) << all(c.unsupported);
  fast::Engine engine(*d.netlist, c);
  engine.evaluate();
  // BUF: delay 20 (22) + 0.1 slew + 2 load, slew 5 (6) + 0.1 slew + 0.2 load; the port drives INV/A, 2 fF.
  EXPECT_NEAR(engine.arrival(a, 0, 0), 100 + 2 * 2, 1e-3);
  EXPECT_NEAR(engine.arrival(a, 0, 1), 100 + 2 * 2, 1e-3);
  EXPECT_NEAR(engine.slew(a, 0, 0), 5 + 0.2 * 2, 1e-3);
  EXPECT_NEAR(engine.slew(a, 0, 1), 6 + 0.2 * 2, 1e-3);
  fast::Engine ideal(*d.netlist, d.read(text));
  ideal.evaluate();
  EXPECT_LT(engine.slack(d.pin("ff", "D")), ideal.slack(d.pin("ff", "D")));

  // A larger inverter loads the port more: the edit moves the launch of the port with it.
  const auto before = engine.arrival(a, 0, 0);
  const auto mark   = engine.mark();
  engine.resize_cell(d.cell_by["i1"], d.types.find("INV2"));
  engine.settle();
  EXPECT_NEAR(engine.arrival(a, 0, 0), 100 + 2 * 4, 1e-3);
  engine.rollback(mark);
  EXPECT_FLOAT_EQ(engine.arrival(a, 0, 0), before);
}

TEST(Sdc, FanoutLimitOfTheDesign) {
  Design d;
  const auto clk = d.in("clk"), a = d.in("a");
  for (const char* name : {"f1", "f2", "f3"}) {
    d.cell("FF", name);
    d.wire(clk, d.pin(name, "CLK"));
  }
  d.cell("BUF", "b");
  d.wire(a, d.pin("b", "A"));
  for (const char* name : {"f1", "f2", "f3"}) {
    d.wire(d.pin("b", "Y"), d.pin(name, "D"));
  }
  d.netlist->finalize();
  const std::string text = "create_clock -name clk -period 1000 [get_ports clk]\n";
  fast::Engine      within(*d.netlist, d.read(text + "set_max_fanout 3 [current_design]\n"));
  within.evaluate();
  EXPECT_EQ(within.report().fanout_violations, 0u);
  fast::Engine over(*d.netlist, d.read(text + "set_max_fanout 2 [current_design]\n"));
  over.evaluate();
  // The buffer drives three pins against a limit of two; the clock network is not counted.
  EXPECT_EQ(over.report().fanout_violations, 1u);
  ASSERT_EQ(over.electrical_pins().size(), 1u);
  EXPECT_EQ(over.electrical_pins()[0].pin, d.pin("b", "Y"));
  EXPECT_NEAR(over.electrical_pins()[0].excess, 0.5, 1e-6);
  EXPECT_EQ(over.pin_electrical(d.pin("b", "Y")).count, 1);
}
