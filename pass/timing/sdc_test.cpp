// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc.hpp"

#include <algorithm>
#include <memory>

#include "gtest/gtest.h"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;

namespace {
// A netlist of ports only: clock `clk`, inputs `reset`, `a[0]`, `a[1]`, outputs `z`, `y[0]`, `y[1]`.
struct Ports {
  lt::Library                    library = lt::parse(fast::test::library_text(1.0));
  fast::Cell_types               types;
  std::unique_ptr<fast::Netlist> netlist;
  Ports() {
    const lt::Library*             pointer = &library;
    const std::vector<std::string> names{"c0"};
    types   = fast::Cell_types::build(std::span<const lt::Library* const>(&pointer, 1), names);
    netlist = std::make_unique<fast::Netlist>(types);
    for (const char* n : {"clk", "reset", "a[0]", "a[1]"}) {
      netlist->add_input_port(n);
    }
    for (const char* n : {"z", "y[0]", "y[1]"}) {
      netlist->add_output_port(n);
    }
  }
  uint32_t pin_of(const std::string& name) const {
    for (const auto& p : netlist->ports()) {
      if (p.name == name) {
        return netlist->pin(p.cell, 0u);
      }
    }
    return ~0u;
  }
};
}  // namespace

TEST(Sdc, EyerissStyle) {
  const auto sdc = fast::read_sdc(R"(
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
  EXPECT_TRUE(sdc.unsupported.empty()) << sdc.unsupported.front();
  ASSERT_EQ(sdc.clocks.size(), 1u);
  EXPECT_EQ(sdc.clocks[0].name, "clk");
  EXPECT_FLOAT_EQ(sdc.clocks[0].period_ps, 2500);

  Ports      ports;
  const auto c = fast::resolve(sdc, *ports.netlist);
  EXPECT_TRUE(c.unsupported.empty());
  ASSERT_EQ(c.clocks.size(), 1u);
  // The three non-clock inputs and the three outputs have a 500 ps delay.
  EXPECT_EQ(c.inputs.size(), 3u);
  for (const auto& in : c.inputs) {
    EXPECT_NE(in.port, "clk");
    EXPECT_FLOAT_EQ(in.delay_ps, 500);
    EXPECT_EQ(in.clock, "clk");
  }
  EXPECT_EQ(c.outputs.size(), 3u);
  ASSERT_EQ(c.cut_pins.size(), 1u);
  EXPECT_EQ(c.cut_pins[0], ports.pin_of("reset"));
}

TEST(Sdc, Sha3StyleWithLoadAndDrivingCell) {
  const auto sdc = fast::read_sdc(R"(
current_design sha3
set clk_name  clk
set clk_port_name clk
set clk_period 929
set clk_port [get_ports $clk_port_name]

create_clock -name $clk_name -period $clk_period $clk_port

set non_clock_inputs [all_inputs -no_clocks]
set_input_delay 10 -clock $clk_name $non_clock_inputs
set_output_delay 10 -clock $clk_name [all_outputs]

set_driving_cell -lib_cell DFFHQNx2_ASAP7_75t_R -pin QN $non_clock_inputs
set_load [expr 4.0 * 0.5] [all_outputs]
)");
  // set_driving_cell is reported, once, and nothing else is.
  ASSERT_EQ(sdc.unsupported.size(), 1u);
  EXPECT_NE(sdc.unsupported[0].find("set_driving_cell"), std::string::npos);
  Ports      ports;
  const auto c = fast::resolve(sdc, *ports.netlist);
  EXPECT_EQ(c.inputs.size(), 3u);
  ASSERT_EQ(c.outputs.size(), 3u);
  for (const auto& out : c.outputs) {
    EXPECT_FLOAT_EQ(out.load_ff, 2.0f);
    EXPECT_FLOAT_EQ(out.delay_ps, 10);
  }
}

TEST(Sdc, PortsByPatternBitsAndFalseToOutput) {
  const auto sdc = fast::read_sdc(R"(
create_clock -name core -period 1000 [get_ports clk]
set_clock_uncertainty 25 [get_clocks core]
set_clock_transition 12 core
set_input_delay 100 -clock core [get_ports {a[0] reset}]
set_input_delay 300 -max -clock core [get_ports a*]
set_input_delay 5 -min -clock core [get_ports a*]
set_output_delay 50 -clock core [get_ports y*]
set_input_transition 20 [all_inputs]
set_false_path -to [get_ports z]
set_false_path -from [get_ports nothere]
)");
  Ports      ports;
  const auto c = fast::resolve(sdc, *ports.netlist);
  EXPECT_FLOAT_EQ(c.clocks[0].uncertainty_ps, 25);
  EXPECT_FLOAT_EQ(c.clocks[0].slew_ps, 12);
  EXPECT_FLOAT_EQ(c.default_input_slew_ps, 20);
  // a[0] and reset at 100, then both bits of a at 300 (the -min only form is not a late constraint).
  EXPECT_EQ(c.inputs.size(), 4u);
  EXPECT_EQ(c.outputs.size(), 2u);
  ASSERT_EQ(c.excluded_endpoints.size(), 1u);
  EXPECT_EQ(c.excluded_endpoints[0], ports.pin_of("z"));
  EXPECT_TRUE(c.cut_pins.empty());
  ASSERT_FALSE(c.unsupported.empty());
  EXPECT_NE(c.unsupported.back().find("nothere"), std::string::npos);
}

TEST(Sdc, UnsupportedIsListedOnce) {
  const auto sdc = fast::read_sdc(R"(
create_clock -name c -period 100 [get_ports clk]
create_generated_clock -name d -source [get_ports clk] -divide_by 2 [get_ports clk]
create_generated_clock -name e -source [get_ports clk] -divide_by 4 [get_ports clk]
foreach p {a b} {
  set_input_delay 1 -clock c [get_ports $p]
}
set_clock_groups -asynchronous -group c -group d
set_false_path -from [get_ports a*] -to [get_ports z]
set_input_delay [expr $undefined * 2] -clock c [all_inputs]
)");
  size_t generated = 0;
  for (const auto& u : sdc.unsupported) {
    generated += u.starts_with("create_generated_clock");
  }
  EXPECT_EQ(generated, 1u);
  EXPECT_TRUE(std::ranges::any_of(sdc.unsupported, [](const auto& u) { return u.starts_with("foreach"); }));
  EXPECT_TRUE(std::ranges::any_of(sdc.unsupported, [](const auto& u) { return u.starts_with("set_clock_groups"); }));
  EXPECT_TRUE(std::ranges::any_of(sdc.unsupported, [](const auto& u) { return u.starts_with("set_false_path"); }));
  EXPECT_TRUE(std::ranges::any_of(sdc.unsupported, [](const auto& u) { return u.starts_with("set_input_delay"); }));
  EXPECT_TRUE(sdc.delays.empty());  // the foreach body is skipped, the undefined variable refuses its command
}

TEST(Sdc, UnitsAndContinuation) {
  const auto sdc = fast::read_sdc("create_clock -name c \\\n -period 2 [get_ports clk]\nset_load 3 [all_outputs]\n",
                                  {.time_ps = 1000, .capacitance_ff = 1000});
  ASSERT_EQ(sdc.clocks.size(), 1u);
  EXPECT_FLOAT_EQ(sdc.clocks[0].period_ps, 2000);
  ASSERT_EQ(sdc.loads.size(), 1u);
  EXPECT_FLOAT_EQ(sdc.loads[0].load_ff, 3000);
}
