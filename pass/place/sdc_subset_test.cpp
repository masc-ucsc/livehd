// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc_subset.hpp"

#include <stdexcept>

#include "gtest/gtest.h"

namespace late = livehd::placement::late;
namespace {
const std::string constraints = R"sdc(
# Saved constraints: literal bus selectors, positive clocks, a forwarded clock.
current_design top
create_clock -name sys -period 3.6 [get_pins {phy/user_clk}]
create_clock -name ref -period 10 [get_ports {refclk}]
create_generated_clock -name fwd -source [get_pins {phy/user_clk}] -divide_by 1 [get_ports {clk}]
set_clock_groups -name async -asynchronous \
 -group [get_clocks {ref}] \
 -group [get_clocks {sys}]
set_input_delay .72 -clock [get_clocks {sys}] -add_delay [get_ports {data[0]}]
set_output_delay .72 -clock [get_clocks {sys}] -add_delay [get_ports {out[0]}]
set_load -pin_load .005 [get_ports {out[0]}]
set_driving_cell -lib_cell BUF -pin Y -input_transition_rise .1 -input_transition_fall .2 [get_ports {data[0]}]
)sdc";
}
TEST(SdcSubset, SavedClocksIoLoadsAndDriversUseExplicitUnits) {
  const auto result = late::read_saved_sdc(constraints, "top", {1000, 1000});
  EXPECT_TRUE(result.complete);
  EXPECT_EQ(result.clocks.at("sys").period_ps, 3600);
  EXPECT_TRUE(result.clocks.at("sys").roots.contains("phy:user_clk"));
  EXPECT_EQ(result.clock_aliases.at("fwd").source, "sys");
  EXPECT_EQ(result.inputs.at("data[0]").clock, "sys");
  EXPECT_EQ(result.inputs.at("data[0]").arrival_ps[0], 720);
  ASSERT_TRUE(result.inputs.at("data[0]").driving_cell);
  EXPECT_EQ(result.inputs.at("data[0]").driving_cell->input_slew_ps[1], 200);
  EXPECT_EQ(result.outputs.at("out[0]").required_ps[0], 2880);
  EXPECT_EQ(result.outputs.at("out[0]").load_ff[1], 5);
  const late::Clock_system clocks({}, result.clocks, result.clock_aliases, result.asynchronous);
  EXPECT_FALSE(clocks.separation(clocks.find("ref"), clocks.find("fwd")));
}
TEST(SdcSubset, LiteralListsNegativeIoDelaysAndIdealOffsetsRemainDistinct) {
  const auto result = late::read_saved_sdc(R"(
    current_design top;
    create_clock -name main -period 100 -waveform {25 75} [get_ports {ca cb}]
    set_input_delay -10 -clock [get_clocks {main}] [get_ports {a[0] a[1]}]
    set_output_delay -5 -clock [get_clocks {main}] [get_ports {z}]
  )",
                                           "top",
                                           {1, 1});
  EXPECT_EQ(result.clocks.at("main").roots.size(), 2);
  EXPECT_EQ(result.clocks.at("main").phase_ps, 25);
  EXPECT_EQ(result.clocks.at("main").falling_ps, 75);
  EXPECT_EQ(result.inputs.at("a[1]").arrival_ps[0], -10);
  EXPECT_EQ(result.outputs.at("z").required_ps[0], 105);
}
TEST(SdcSubset, UnsupportedCommandsQueriesAndClockTransformsNeverAffirmCoverage) {
  for (const std::string extra :
       {"set_false_path -from [get_ports {data[0]}]",
        "set_input_delay 1 -clock [get_clocks {sys}] [get_ports {*}]",
        "set_input_delay 1 -clock [get_clocks {missing}] [get_ports {a}]",
        "create_generated_clock -name div -source [get_pins {phy/user_clk}] -divide_by 2 [get_ports {divclk}]",
        "set_input_delay 1 -clock [get_clocks {sys}] -rise [get_ports {a}]"}) {
    EXPECT_THROW(late::read_saved_sdc(constraints + extra, "top", {1, 1}), std::invalid_argument) << extra;
  }
  EXPECT_THROW(late::read_saved_sdc(constraints, "other", {1, 1}), std::invalid_argument);
  EXPECT_THROW(late::read_saved_sdc(constraints, "top", {0, 1}), std::invalid_argument);
  EXPECT_THROW(late::read_saved_sdc(constraints + "set_load -pin_load 1 [get_ports {unconstrained}]", "top", {1, 1}),
               std::invalid_argument);
}
