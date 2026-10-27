// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_opentimer.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "inou/lefdef/mapped_graph.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"

namespace {
class Sdc_probe : public Pass_opentimer {
public:
  explicit Sdc_probe(const Eprp_var& var) : Pass_opentimer(var) {
    timer.set_num_threads(1);
    timer.insert_primary_input("a").insert_primary_output("y").insert_net("wire");
    timer.disconnect_pin("a").disconnect_pin("y");
    timer.connect_pin("a", "wire").connect_pin("y", "wire");
  }
  using Pass_opentimer::clock_qor_json;
  using Pass_opentimer::endpoint_tns;
  using Pass_opentimer::read_sdc;
  using Pass_opentimer::timer;
  bool complete() const { return constraints_complete_; }
  bool propagated(const std::string& name) const { return propagated_clocks_.contains(name); }
};
class Circuit_probe : public Pass_opentimer {
public:
  explicit Circuit_probe(const Eprp_var& var) : Pass_opentimer(var) { timer.set_num_threads(1); }
  using Pass_opentimer::apply_case_analysis;
  using Pass_opentimer::build_circuit;
  using Pass_opentimer::cache_key;
  using Pass_opentimer::compute_timing;
  using Pass_opentimer::timer;
};
class SdcClock : public ::testing::Test {
protected:
  std::string directory;
  Eprp_var    var;
  void        SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-sdc-clock-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    var.dict["files"] = directory + "/unused.v";
    std::ofstream(directory + "/unused.v") << "";
    var.set_stage_labels(var.dict);
  }
  void        TearDown() override { std::filesystem::remove_all(directory); }
  std::string write(const std::string& text) {
    const auto path = directory + "/timing.sdc";
    std::ofstream(path) << text;
    return path;
  }
};
TEST_F(SdcClock, ImportedOpenInputsAreAllowedButLostConnectionsStillFail) {
  const auto path = directory + "/macro.lib";
  std::ofstream(path) << R"lib(library(macros) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(MACRO) {
      area : 1;
      pin(A) { direction : input; capacitance : 1; }
      pin("MASK[0]") { direction : input; capacitance : 1; }
      pin("MASK[1]") { direction : input; capacitance : 1; }
      pin(Y) { direction : output; function : "A";
        timing() { related_pin : "A"; timing_sense : positive_unate;
          cell_rise(scalar) { values("10"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
  })lib";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  livehd::physical::Technology tech;
  for (const auto* name : {"A", "MASK[0]", "MASK[1]"}) {
    tech.macros["MACRO"].pins[name].direction = "INPUT";
  }
  tech.macros["MACRO"].pins["Y"].direction = "OUTPUT";
  livehd::physical::Floorplan fp;
  fp.design = "top";
  fp.components.push_back({"macro", "MACRO", {}});
  livehd::physical::Terminal input, output;
  input.name       = "a";
  input.direction  = "INPUT";
  output.name      = "y";
  output.direction = "OUTPUT";
  fp.terminals     = {input, output};
  fp.nets          = {
      { "in", {{"PIN", "a"}, {"macro", "A"}}},
      {"out", {{"macro", "Y"}, {"PIN", "y"}}}
  };
  hhds::GraphLibrary library;
  const auto         graph    = livehd::physical::mapped_graph(library, fp, tech);
  const auto         node     = graph->get_output_pin("y").get_driver_pins().front().get_master_node();
  const auto         instance = std::string(node.get_name()) + "__n" + std::to_string(node.get_debug_nid());
  Circuit_probe      probe(var);
  probe.build_circuit(graph);
  EXPECT_NO_THROW(probe.compute_timing(graph));
  // These remain electrically open, never silently tied to a constant.
  ASSERT_EQ(probe.timer.pins().at(instance + ":MASK[0]").net(), nullptr);
  ASSERT_EQ(probe.timer.pins().at(instance + ":MASK[1]").net(), nullptr);
  // A lost connection on an input that DEF did connect must remain an error.
  const auto wire = probe.timer.pins().at(instance + ":A").net()->name();
  probe.timer.disconnect_pin(instance + ":A");
  EXPECT_THROW(probe.compute_timing(graph), std::runtime_error);
  // Even an erroneous open annotation cannot hide a timer connection missing
  // from an input that the current graph still connects.
  node.attr(livehd::attrs::physical_open_inputs).set({"A", "MASK[0]", "MASK[1]"});
  EXPECT_THROW(probe.compute_timing(graph), std::runtime_error);
  probe.timer.connect_pin(instance + ":A", wire);
  EXPECT_NO_THROW(probe.compute_timing(graph));
  // Re-evaluation must consult current intent, not an old cached whitelist.
  const auto accepted_key = probe.cache_key(graph);
  ASSERT_FALSE(accepted_key.empty());
  node.attr(livehd::attrs::physical_open_inputs).set({"MASK[0]"});
  EXPECT_NE(probe.cache_key(graph), accepted_key);
  EXPECT_THROW(probe.compute_timing(graph), std::runtime_error);
}
TEST_F(SdcClock, ResolvesClockRelativeSetupAndHoldWithSeparateCornersAndTransitions) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay -clock virtual -min 3 [all_inputs]
set_input_delay -clock virtual -max 10 [all_inputs]
set_input_delay -clock virtual -max -rise 12 [get_ports a]
set_output_delay -clock virtual -max 20 [all_outputs]
set_output_delay -clock virtual -min -5 [all_outputs]
)sdc"));
  ASSERT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 68);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::FALL), 70);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MIN, ot::RISE), -2);
  EXPECT_EQ(pass.timer.report_slack("y", ot::MIN, ot::FALL), -2);
  const auto report = pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
  EXPECT_NE(report.find("\"setup_slack\":68"), std::string::npos);
  EXPECT_NE(report.find("\"hold_slack\":-2"), std::string::npos);
}

TEST_F(SdcClock, TotalNegativeSlackCountsEachEndpointOnce) {
  Sdc_probe pass(var);
  pass.timer.insert_primary_output("z").disconnect_pin("z").connect_pin("z", "wire");
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 140 -clock virtual [all_inputs]
set_input_delay -fall 130 -clock virtual [all_inputs]
set_output_delay 20 -clock virtual [all_outputs]
)sdc"));
  EXPECT_NEAR(pass.timer.report_tns(ot::MAX).value(), -220, 1e-5);
  EXPECT_NEAR(pass.endpoint_tns(ot::MAX), -120, 1e-5);
}
TEST_F(SdcClock, BracedBusSelectorsPreserveEveryIndexAndContinueThroughTheList) {
  Sdc_probe pass(var);
  for (const auto& name : {"data[0]", "data[1]"}) {
    pass.timer.insert_primary_input(name).disconnect_pin(name).connect_pin(name, "wire");
  }
  for (const auto& name : {"out[0]", "out[1]"}) {
    pass.timer.insert_primary_output(name).disconnect_pin(name).connect_pin(name, "wire");
  }
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [get_ports {a data[0] data[1]}]
set_output_delay 20 -clock virtual [get_ports {y out[0] out[1]}]
)sdc"));
  for (const auto& name : {"y", "out[0]", "out[1]"}) {
    EXPECT_EQ(pass.timer.report_slack(name, ot::MAX, ot::RISE), 70);
  }
  EXPECT_EQ(pass.timer.report_at("data[1]", ot::MAX, ot::RISE), 10);
  pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, VerilogBusSelectorsBindPinTrackerDotNames) {
  Sdc_probe pass(var);
  for (const auto& name : {"data", "data.1"}) {
    pass.timer.insert_primary_input(name).disconnect_pin(name).connect_pin(name, "wire");
  }
  for (const auto& name : {"out", "out.1"}) {
    pass.timer.insert_primary_output(name).disconnect_pin(name).connect_pin(name, "wire");
  }
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [get_ports {a data[0] data[1]}]
set_output_delay 20 -clock virtual [get_ports {y out[0] out[1]}]
)sdc"));
  EXPECT_EQ(pass.timer.report_at("data.1", ot::MAX, ot::RISE), 10);
  EXPECT_EQ(pass.timer.report_slack("out.1", ot::MAX, ot::RISE), 70);
  pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, ExpandsScalarSelectorVariablesAndArithmeticLoads) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
current_design top
set clock_name virtual
set period 100
set inputs [all_inputs -no_clocks]
set output [get_ports y]
set cap 0.25
create_clock -name $clock_name -period ${period}
set_input_delay 10 -clock $clock_name $inputs
set_output_delay 20 -clock $clock_name $output
set_load [expr {4.0 * $cap}] $output
)sdc"));
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 70);
  EXPECT_FLOAT_EQ(pass.timer.pins().at("y").cap(ot::MAX, ot::RISE), 1);
  pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
}

TEST_F(SdcClock, ExactInputFalsePathPreservesUnrelatedPathsAndSurvivesLaterConstraints) {
  Sdc_probe pass(var);
  pass.timer.insert_primary_input("b").insert_primary_output("z").insert_net("other");
  pass.timer.disconnect_pin("b").disconnect_pin("z");
  pass.timer.connect_pin("b", "other").connect_pin("z", "other");
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [all_inputs]
set_output_delay 20 -clock virtual [all_outputs]
set_false_path -from [get_ports a]
)sdc"));
  EXPECT_FALSE(pass.timer.report_at("y", ot::MAX, ot::RISE));
  EXPECT_FALSE(pass.timer.report_slack("y", ot::MAX, ot::RISE));
  EXPECT_FLOAT_EQ(pass.timer.report_slack("z", ot::MAX, ot::RISE).value(), 70);
  pass.read_sdc(write("set_input_delay 20 -clock virtual [all_inputs]\n"));
  EXPECT_FALSE(pass.timer.report_at("y", ot::MAX, ot::RISE));
  EXPECT_FLOAT_EQ(pass.timer.report_slack("z", ot::MAX, ot::RISE).value(), 60);
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, ExactListSearchExcludesOnlyTheSelectedInputPort) {
  Sdc_probe pass(var);
  pass.timer.insert_primary_input("clk");
  pass.read_sdc(write(R"sdc(
set clk_port [get_ports clk]
set inputs [lsearch -inline -all -not -exact [all_inputs] $clk_port]
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual $inputs
set_output_delay 20 -clock virtual [all_outputs]
)sdc"));
  EXPECT_FLOAT_EQ(pass.timer.report_at("a", ot::MAX, ot::RISE).value(), 10);
  EXPECT_FALSE(pass.timer.report_at("clk", ot::MAX, ot::RISE));
  EXPECT_FLOAT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE).value(), 70);
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, OutputListSearchRetainsConstraintsWhenTheClockIsNotAnOutput) {
  for (const auto* excluded : {"clock_out", "clk"}) {
    SCOPED_TRACE(excluded);
    Sdc_probe pass(var);
    pass.timer.insert_primary_input("clk");
    pass.timer.insert_primary_output("clock_out").disconnect_pin("clock_out").connect_pin("clock_out", "wire");
    pass.read_sdc(write(std::format(R"sdc(
set clk_port [get_ports {}]
set outputs [lsearch -inline -all -not -exact [all_outputs] $clk_port]
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [all_inputs]
set_output_delay 20 -clock virtual $outputs
)sdc",
                                    excluded)));
    EXPECT_FLOAT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE).value(), 70);
    const auto slack = pass.timer.report_slack("clock_out", ot::MAX, ot::RISE);
    if (std::string_view(excluded) == "clock_out") {
      EXPECT_FALSE(slack);
    } else {
      ASSERT_TRUE(slack);
      EXPECT_FLOAT_EQ(*slack, 70);
    }
    EXPECT_TRUE(pass.complete());
  }
}
TEST_F(SdcClock, NestedListSearchExcludesEveryNamedInputAndBackslashJoinsLines) {
  Sdc_probe pass(var);
  pass.timer.insert_primary_input("clk_p").insert_primary_input("clk_n");
  // The litepci/HighTide idiom: one nested search per excluded port, continued over two lines.
  pass.read_sdc(write(R"sdc(
set non_clock_inputs [lsearch -inline -all -not \
    [lsearch -inline -all -not [all_inputs] clk_p] clk_n]
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual $non_clock_inputs
set_output_delay 20 -clock virtual [all_outputs]
)sdc"));
  EXPECT_FLOAT_EQ(pass.timer.report_at("a", ot::MAX, ot::RISE).value(), 10);
  EXPECT_FALSE(pass.timer.report_at("clk_p", ot::MAX, ot::RISE));
  EXPECT_FALSE(pass.timer.report_at("clk_n", ot::MAX, ot::RISE));
  EXPECT_FLOAT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE).value(), 70);
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, ClockOnAnInternalPinIsCreatedAndAMissingPinOnlyWarns) {
  Sdc_probe pass(var);
  // `a` is a pin of the timing graph; `inst/pin` would be spelled `inst:pin` there. Both forms of
  // create_clock define the clock, and neither is a hard error (the SDC also names generated clocks).
  pass.read_sdc(write(R"sdc(
create_clock -name on_pin -period 100 [get_pins a]
create_clock -name nowhere -period 50 [get_pins macro/clk_out]
)sdc"));
  pass.timer.update_timing();
  ASSERT_TRUE(pass.timer.clocks().contains("on_pin"));
  EXPECT_FLOAT_EQ(pass.timer.clocks().at("on_pin").period(), 100);
  ASSERT_TRUE(pass.timer.clocks().contains("nowhere"));
  EXPECT_FLOAT_EQ(pass.timer.clocks().at("nowhere").period(), 50);
}
TEST_F(SdcClock, TclControlStructureBodyIsSkippedAsOneUnit) {
  Sdc_probe pass(var);
  // HighTide's NyuziProcessor SDC loops over the JTAG ports. The body lines mean nothing without the loop
  // variables, so reading them one by one failed on `$port`; the whole braced command is skipped instead.
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [all_inputs]
foreach {port bit} {
    a 1 b 0
} {
    set pin [get_pins -hierarchical -regexp [format {^x\[%d\]$} $bit]]
    if {[llength $pin] != 1} {
        error "expected one pin for $port"
    }
    set_false_path -from [get_ports $port] -to $pin
}
unset port bit pin
set_output_delay 20 -clock virtual [all_outputs]
)sdc"));
  EXPECT_FLOAT_EQ(pass.timer.report_at("a", ot::MAX, ot::RISE).value(), 10);
  EXPECT_FLOAT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE).value(), 70);
}
TEST_F(SdcClock, ExprDividesIntegersLikeTcl) {
  Sdc_probe                           pass(var);
  const std::string                   clock   = "create_clock -name virtual -period 100\n";
  const std::pair<const char*, float> cases[] = {
      { "1001/2", 500},
      {  "5/2.0", 2.5},
      {   "-5/2",  -3},
      {"(1+1)/4",   0},
      { "0x1E/2",  15},
      { "0x1F/2",  15}
  };
  for (const auto& [expression, expected] : cases) {
    pass.read_sdc(write(clock + std::format("set_input_delay [expr {}] -clock virtual [get_ports a]\n", expression)));
    EXPECT_FLOAT_EQ(pass.timer.report_at("a", ot::MAX, ot::RISE).value(), expected) << expression;
  }
}
TEST_F(SdcClock, TclSubstitutionStaysInsideBracesAndUnmodelledCommandsDegrade) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [get_ports {d$x}]
set_input_delay 10 -clock virtual [all_inputs]
set_clock_uncertainty $::env(U) virtual
set x $::env(X)
set_output_delay 20 -clock virtual [all_outputs]
)sdc"));
  EXPECT_FALSE(pass.complete());
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 70);
}
TEST_F(SdcClock, SkipsLoadAndDrivingCellForms) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set_input_delay 10 -clock virtual [all_inputs]
set_output_delay 20 -clock virtual [all_outputs]
set_load -pin_load 0.1 [get_ports y]
set_load 0.2 [all_inputs]
set_load 0.2 [get_nets wire]
set_driving_cell -library lib -lib_cell NOPE [get_ports a]
set_driving_cell -lib_cell NOPE [get_ports a]
)sdc"));
  EXPECT_FALSE(pass.complete());
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 70);
}
TEST_F(SdcClock, PropagatedClockAcceptsBracedQuotedAndListedNames) {
  Sdc_probe pass(var);
  pass.timer.insert_primary_input("c2");
  pass.read_sdc(write(R"sdc(
create_clock -name one -period 100 [get_ports a]
create_clock -name two -period 100 [get_ports c2]
set_propagated_clock [get_clocks {one}]
)sdc"));
  EXPECT_TRUE(pass.propagated("one"));
  EXPECT_FALSE(pass.propagated("two"));
  pass.read_sdc(write("set_propagated_clock [get_clocks \"two\"]\n"));
  EXPECT_TRUE(pass.propagated("two"));
}
TEST_F(SdcClock, PropagatedClockListedNamesAreEachResolved) {
  const std::string clocks
      = "create_clock -name one -period 100 [get_ports a]\ncreate_clock -name two -period 100 [get_ports c2]\n";
  {
    Sdc_probe pass(var);
    pass.timer.insert_primary_input("c2");
    pass.read_sdc(write(clocks + "set_propagated_clock [get_clocks {one two}]\n"));
    EXPECT_TRUE(pass.propagated("one"));
    EXPECT_TRUE(pass.propagated("two"));
  }
  {
    Sdc_probe pass(var);
    pass.timer.insert_primary_input("c2");
    EXPECT_THROW(pass.read_sdc(write(clocks + "set_propagated_clock [get_clocks {one nope}]\n")), std::runtime_error);
    EXPECT_FALSE(pass.propagated("nope"));
  }
}
TEST_F(SdcClock, DrivingCellUsesLoadedSlewAndExternalDelayIncrement) {
  const auto library = directory + "/driver.lib";
  std::ofstream(library) << R"lib(library(driver) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    lu_table_template(table) {
      variable_1 : input_net_transition; variable_2 : total_output_net_capacitance;
      index_1("0,100"); index_2("0,100");
    }
    cell(BUF) {
      pin(A) { direction : input; capacitance : 2; }
      pin(Y) { direction : output;
        timing() { related_pin : "A"; timing_sense : positive_unate;
          cell_rise(table) { values("10,210", "60,260"); }
          cell_fall(table) { values("10,210", "60,260"); }
          rise_transition(table) { values("4,104", "54,154"); }
          fall_transition(table) { values("4,104", "54,154"); }
        }
      }
    }
  })lib";
  Sdc_probe pass(var);
  pass.timer.read_celllib(library).update_timing();
  pass.read_sdc(write(R"sdc(
create_clock -name virtual -period 100
set ports [all_inputs -no_clocks]
set_input_delay 10 -clock virtual $ports
set_output_delay 20 -clock virtual [all_outputs]
set_load 2 [all_outputs]
set_driving_cell -lib_cell BUF -from_pin A -pin Y -input_transition_rise 20 -input_transition_fall 20 $ports
)sdc"));
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      EXPECT_NEAR(pass.timer.report_at("a", split, tran).value(), 14, 1e-4);
      EXPECT_NEAR(pass.timer.report_slew("a", split, tran).value(), 16, 1e-4);
    }
  }
  // An explicit transition after a driving-cell command replaces its waveform.
  pass.read_sdc(write("set_input_transition 80 [get_ports a]\n"));
  EXPECT_NEAR(pass.timer.report_at("a", ot::MAX, ot::RISE).value(), 10, 1e-4);
  EXPECT_NEAR(pass.timer.report_slew("a", ot::MAX, ot::RISE).value(), 80, 1e-4);
}
TEST_F(SdcClock, BindsPhysicalClockSourceAndSubtractsOutputDelayFromItsPeriod) {
  Sdc_probe pass(var);
  pass.read_sdc(write(R"sdc(
set name clk
set port [get_ports a]
set period 100
create_clock -name $name -period $period $port
set_input_delay 10 -clock $name [all_inputs]
set_output_delay 20 -clock $name [all_outputs]
)sdc"));
  EXPECT_EQ(pass.timer.report_slack("y", ot::MAX, ot::RISE), 70);
  // Broader physical-clock coverage remains conservative.
  EXPECT_FALSE(pass.complete());
}
TEST_F(SdcClock, VirtualClockNameDoesNotTurnADataInputIntoAPhysicalClock) {
  Sdc_probe pass(var);
  pass.read_sdc(
      write("create_clock -name a -period 100\n"
            "set_input_delay -clock a 10 [all_inputs -no_clocks]\n"
            "set_output_delay -clock a 20 [all_outputs]\n"));
  pass.clock_qor_json();
  EXPECT_TRUE(pass.complete());
}
TEST_F(SdcClock, MissingAndUnsupportedClockSemanticsCannotCertifyCoverage) {
  const std::string clock  = "create_clock -name virtual -period 100\n";
  const std::string input  = "set_input_delay -clock virtual 10 [all_inputs]\n";
  const std::string output = "set_output_delay -clock virtual 20 [all_outputs]\n";
  for (const auto& text : {clock + "set_input_delay -clock virtual -max 10 [all_inputs]\n" + output,
                           clock + input + "set_output_delay -clock virtual -max 20 [all_outputs]\n",
                           clock + input + "set_output_delay -clock missing 20 [all_outputs]\n",
                           clock + input + output + "set_input_delay 5 [all_inputs]\n",
                           clock + input + output + "create_clock -name second -period 80\n",
                           clock + input + "set_output_delay -clock virtual -clock_fall 20 [all_outputs]\n",
                           clock + input + "set_output_delay -clock virtual 20 [all_outputs unknown]\n",
                           clock + input + "set_output_delay -clock virtual 20 [get_ports y\n",
                           "create_clock -name virtual -period 100 [get_ports a]\n" + input + output,
                           "create_clock -name virtual -period 100 -waveform {0 40}\n" + input + output}) {
    Sdc_probe pass(var);
    pass.read_sdc(write(text));
    pass.clock_qor_json();
    EXPECT_FALSE(pass.complete()) << text;
  }
}

TEST_F(SdcClock, UnusedCellOutputsStillPresentTheirInputCapacitance) {
  const auto path = directory + "/loads.lib";
  std::ofstream(path) << R"lib(library(loads) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(BUF) {
      pin(A) { direction : input; capacitance : 2; }
      pin(Y) { direction : output;
        timing() { related_pin : "A"; timing_sense : positive_unate;
          cell_rise(scalar) { values("5"); } cell_fall(scalar) { values("5"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
  })lib";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto master = library.create_io("BUF");
  master->add_input("A", 0);
  master->set_bits("A", 1);
  master->add_output("Y", 0);
  master->set_bits("Y", 1);
  auto input = graph->get_input_pin("a");
  livehd::graph_util::set_bits(input, 1);
  for (bool used : {true, false}) {
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(used ? "used" : "unused");
    node.set_subnode(master);
    node.create_sink_pin("A").connect_driver(input);
    if (used) {
      auto output = node.create_driver_pin("Y");
      livehd::graph_util::set_bits(output, 1);
      graph->get_output_pin("y").connect_driver(output);
    }
  }
  Circuit_probe probe(var);
  probe.build_circuit(graph);
  probe.timer.update_timing();
  EXPECT_EQ(probe.timer.num_gates(), 2);
  EXPECT_NEAR(probe.timer.report_load("a", ot::MAX, ot::RISE).value(), 4, 1e-5);
}

TEST_F(SdcClock, DeclaredOutputWidthsExtendAndTruncateTheirDrivers) {
  const auto path = directory + "/wires.lib";
  std::ofstream(path) << "library(wires) { time_unit : \"1ps\"; capacitive_load_unit(1,ff); }\n";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  for (const auto& [name, bits] : std::map<std::string, int>{
           {   "signed_bus", 4},
           { "unsigned_bus", 4},
           {"signed_scalar", 1}
  }) {
    io->add_input(name, io->get_input_pin_decls().size());
    io->set_bits(name, bits);
  }
  struct Output {
    const char* name;
    const char* driver;
    int         width;
  };
  const Output outputs[] = {
      {  "sign_extended",    "signed_bus", 8},
      {     "sign_alias",    "signed_bus", 8},
      {  "zero_extended",  "unsigned_bus", 8},
      {      "truncated",    "signed_bus", 2},
      {"scalar_extended", "signed_scalar", 3}
  };
  for (const auto& output : outputs) {
    io->add_output(output.name, io->get_output_pin_decls().size());
    io->set_bits(output.name, output.width);
  }
  auto graph = io->create_graph();
  livehd::graph_util::set_sbits(graph->get_input_pin("signed_bus"), 4);
  livehd::graph_util::set_sbits(graph->get_input_pin("signed_scalar"), 1);
  livehd::graph_util::set_ubits(graph->get_input_pin("unsigned_bus"), 4);
  for (const auto& output : outputs) {
    graph->get_output_pin(output.name).connect_driver(graph->get_input_pin(output.driver));
  }
  Circuit_probe probe(var);
  probe.build_circuit(graph);
  probe.timer.update_timing();
  EXPECT_EQ(probe.timer.primary_outputs().size(), 29);
  for (const auto& output : outputs) {
    for (int bit = 0; bit < output.width; ++bit) {
      const auto pin = bit ? std::format("{}.{}", output.name, bit) : output.name;
      ASSERT_TRUE(probe.timer.pins().contains(pin)) << pin;
      const auto source_bit = std::min(bit, output.driver == std::string("signed_scalar") ? 0 : 3);
      const auto expected   = std::string(output.name) == "zero_extended" && bit >= 4 ? std::string("__lhd_ot_zero__")
                              : source_bit ? std::format("{}.{}", output.driver, source_bit)
                                           : output.driver;
      ASSERT_NE(probe.timer.pins().at(pin).net(), nullptr) << pin;
      EXPECT_EQ(probe.timer.pins().at(pin).net()->name(), expected) << pin;
    }
    EXPECT_FALSE(probe.timer.primary_outputs().contains(std::format("{}.{}", output.name, output.width)));
  }
}

TEST_F(SdcClock, CaseAnalysisPrunesConstantPathsAndRestrictsNonUnateArcs) {
  const auto path = directory + "/case.lib";
  std::ofstream(path) << R"lib(library(cases) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(T0) { dont_use : true; pin(Y) { direction : output; function : "0"; } }
    cell(T1) { pin(Y) { direction : output; function : "1"; } }
    cell(BUF) {
      pin(A) { direction : input; capacitance : 2; }
      pin(Y) { direction : output; function : "A";
        timing() { related_pin : "A"; timing_sense : positive_unate;
          cell_rise(scalar) { values("10"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
    cell(AND) {
      pin(A) { direction : input; capacitance : 2; }
      pin(B) { direction : input; capacitance : 2; }
      pin(Y) { direction : output; function : "A * B";
        timing() { related_pin : "A B"; timing_sense : positive_unate;
          cell_rise(scalar) { values("20"); } cell_fall(scalar) { values("20"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
    cell(XOR) {
      pin(A) { direction : input; capacitance : 2; }
      pin(B) { direction : input; capacitance : 2; }
      pin(Y) { direction : output; function : "A ^ B";
        timing() { related_pin : "A B"; timing_sense : non_unate;
          cell_rise(scalar) { values("2"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
  })lib";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Circuit_probe probe(var);
  auto&         timer = probe.timer;
  timer.read_celllib(path).insert_primary_input("a");
  for (const auto& [instance, master] : std::vector<std::pair<std::string, std::string>>{
           {  "zero",  "T0"},
           {   "one",  "T1"},
           {  "copy", "BUF"},
           {"masked", "AND"},
           {"invert", "XOR"}
  }) {
    timer.insert_gate(instance, master);
  }
  for (const auto* net : {"data", "zero", "one", "copied"}) {
    timer.insert_net(net);
  }
  timer.disconnect_pin("a").connect_pin("a", "data");
  timer.connect_pin("zero:Y", "zero").connect_pin("copy:A", "zero");
  timer.connect_pin("copy:Y", "copied").connect_pin("masked:A", "copied");
  timer.connect_pin("one:Y", "one").connect_pin("invert:B", "one");
  timer.connect_pin("masked:B", "data").connect_pin("invert:A", "data");
  for (auto split : {ot::MIN, ot::MAX}) {
    timer.set_at("a", split, ot::RISE, 200).set_at("a", split, ot::FALL, 500);
    timer.set_slew("a", split, ot::RISE, 1).set_slew("a", split, ot::FALL, 1);
  }
  ASSERT_EQ(timer.report_at("masked:Y", ot::MAX, ot::RISE).value(), 220);
  ASSERT_EQ(timer.report_at("invert:Y", ot::MAX, ot::FALL).value(), 510);
  probe.apply_case_analysis();
  EXPECT_FALSE(timer.report_at("masked:Y", ot::MAX, ot::RISE));
  EXPECT_FALSE(timer.report_at("masked:Y", ot::MAX, ot::FALL));
  EXPECT_EQ(timer.report_at("invert:Y", ot::MAX, ot::FALL).value(), 210);
  EXPECT_EQ(timer.report_at("invert:Y", ot::MAX, ot::RISE).value(), 502);
  EXPECT_EQ(timer.report_load("data", ot::MAX, ot::RISE).value(), 4);
  timer.set_case_masks({});
  EXPECT_EQ(timer.report_at("masked:Y", ot::MAX, ot::RISE).value(), 220);
  EXPECT_EQ(timer.report_at("invert:Y", ot::MAX, ot::FALL).value(), 510);
}

TEST_F(SdcClock, ConditionalTimingSelectsKnownCasesAndKeepsUnknownFallback) {
  const auto path = directory + "/conditional.lib";
  std::ofstream(path) << R"lib(library(conditions) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(T0) { pin(Y) { direction : output; function : "0"; } }
    cell(T1) { pin(Y) { direction : output; function : "1"; } }
    cell(COND) {
      pin(A) { direction : input; capacitance : 2; }
      pin(B) { direction : input; capacitance : 2; }
      pin(Y) { direction : output; function : "A";
        timing() { related_pin : "A"; timing_sense : positive_unate; when : "B";
          cell_rise(scalar) { values("25"); } cell_fall(scalar) { values("25"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
        timing() { related_pin : "A"; timing_sense : positive_unate;
          cell_rise(scalar) { values("100"); } cell_fall(scalar) { values("100"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); }
        }
      }
    }
  })lib";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Circuit_probe probe(var);
  auto&         timer = probe.timer;
  timer.read_celllib(path).insert_primary_input("a");
  timer.insert_gate("zero", "T0").insert_gate("one", "T1");
  for (const auto* name : {"on", "off", "unknown"}) {
    timer.insert_gate(name, "COND");
  }
  for (const auto* net : {"data", "zero", "one"}) {
    timer.insert_net(net);
  }
  timer.disconnect_pin("a").connect_pin("a", "data");
  timer.connect_pin("zero:Y", "zero").connect_pin("off:B", "zero");
  timer.connect_pin("one:Y", "one").connect_pin("on:B", "one");
  timer.connect_pin("unknown:B", "data");
  for (const auto* name : {"on", "off", "unknown"}) {
    timer.connect_pin(std::string(name) + ":A", "data");
  }
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      timer.set_at("a", split, tran, 10).set_slew("a", split, tran, 1);
    }
  }
  probe.apply_case_analysis();
  for (auto tran : {ot::RISE, ot::FALL}) {
    EXPECT_EQ(timer.report_at("on:Y", ot::MIN, tran).value(), 35);
    EXPECT_EQ(timer.report_at("on:Y", ot::MAX, tran).value(), 35);
    EXPECT_EQ(timer.report_at("off:Y", ot::MIN, tran).value(), 110);
    EXPECT_EQ(timer.report_at("off:Y", ot::MAX, tran).value(), 110);
    EXPECT_EQ(timer.report_at("unknown:Y", ot::MIN, tran).value(), 35);
    EXPECT_EQ(timer.report_at("unknown:Y", ot::MAX, tran).value(), 110);
  }
}

// A placed three-port memory (write port 0, read ports 1 and 2) timed with
// hand-built placement RC. Every addr/din/enable sink is a capture endpoint;
// the clock pins only carry clock-net RC.
// Timer-fidelity regressions found against OpenSTA on the resize study inputs.
constexpr const char* kFlopLib = R"lib(library(flops) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(DFF) {
      pin(CLK) { direction : input; capacitance : 1; clock : true; }
      pin(D) { direction : input; capacitance : 1;
        timing() { related_pin : "CLK"; timing_type : setup_rising;
          rise_constraint(scalar) { values("10"); } fall_constraint(scalar) { values("10"); } }
      }
      pin(RN) { direction : input; capacitance : 1; }
      pin(Q) { direction : output; function : "IQ";
        timing() { related_pin : "CLK"; timing_type : rising_edge;
          cell_rise(scalar) { values("20"); } cell_fall(scalar) { values("20"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); } }
        timing() { related_pin : "RN"; timing_type : clear; timing_sense : positive_unate;
          cell_fall(scalar) { values("30"); } fall_transition(scalar) { values("1"); } }
      }
      ff(IQ, IQN) { clocked_on : "CLK"; next_state : "D"; clear : "!RN"; }
    }
    cell(DFFN) {
      pin(CLK) { direction : input; capacitance : 1; clock : true; }
      pin(D) { direction : input; capacitance : 1;
        timing() { related_pin : "CLK"; timing_type : setup_falling;
          rise_constraint(scalar) { values("10"); } fall_constraint(scalar) { values("10"); } }
        timing() { related_pin : "CLK"; timing_type : hold_falling;
          rise_constraint(scalar) { values("5"); } fall_constraint(scalar) { values("5"); } }
      }
      pin(Q) { direction : output; function : "IQ";
        timing() { related_pin : "CLK"; timing_type : falling_edge;
          cell_rise(scalar) { values("20"); } cell_fall(scalar) { values("20"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); } }
      }
      ff(IQ, IQN) { clocked_on : "!CLK"; next_state : "D"; }
    }
  })lib";
TEST_F(SdcClock, AsyncClearToOutputArcsAreNotTimedAsData) {
  // OpenSTA disables preset/clear -> Q arcs by default; a reset tree must not
  // become the worst data path.
  const auto path = directory + "/flops.lib";
  std::ofstream(path) << kFlopLib;
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Circuit_probe probe(var);
  auto&         timer = probe.timer;
  timer.read_celllib(path).insert_primary_input("clk").insert_primary_input("rst").insert_primary_output("y");
  timer.insert_gate("f", "DFF");
  for (const auto* net : {"c", "r", "q"}) {
    timer.insert_net(net);
  }
  timer.disconnect_pin("clk").connect_pin("clk", "c").connect_pin("f:CLK", "c");
  timer.disconnect_pin("rst").connect_pin("rst", "r").connect_pin("f:RN", "r");
  timer.disconnect_pin("y").connect_pin("f:Q", "q").connect_pin("y", "q");
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      timer.set_at("clk", split, tran, 0).set_slew("clk", split, tran, 1);
      timer.set_at("rst", split, tran, 500).set_slew("rst", split, tran, 1);
    }
  }
  const auto at = timer.report_at("y", ot::MAX, ot::FALL);
  ASSERT_TRUE(at);
  EXPECT_LT(*at, 100);  // clock-to-Q only; with the clear arc it would be 530
}
TEST_F(SdcClock, EachSetupCheckUsesThePeriodOfItsCaptureClock) {
  const auto path = directory + "/flops.lib";
  std::ofstream(path) << kFlopLib;
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Sdc_probe pass(var);
  auto&     timer = pass.timer;
  timer.read_celllib(path).insert_primary_input("c1").insert_primary_input("c2");
  timer.insert_gate("fast", "DFF").insert_gate("slow", "DFF");
  timer.insert_net("n1").insert_net("n2");
  timer.disconnect_pin("c1").connect_pin("c1", "n1").connect_pin("fast:CLK", "n1");
  timer.disconnect_pin("c2").connect_pin("c2", "n2").connect_pin("slow:CLK", "n2");
  timer.connect_pin("fast:D", "wire").connect_pin("slow:D", "wire");
  pass.read_sdc(
      write("create_clock -name slow_clk -period 6400 [get_ports c2]\n"
            "create_clock -name fast_clk -period 1000 [get_ports c1]\n"
            "set_input_delay -clock fast_clk 0 [get_ports a]\n"));
  timer.set_ideal_clocks({"fast_clk", "slow_clk"});
  for (const auto* port : {"a", "c1", "c2"}) {
    for (auto split : {ot::MIN, ot::MAX}) {
      timer.set_slew(port, split, ot::RISE, 1).set_slew(port, split, ot::FALL, 1);
    }
  }
  const auto fast = timer.report_slack("fast:D", ot::MAX, ot::RISE);
  const auto slow = timer.report_slack("slow:D", ot::MAX, ot::RISE);
  ASSERT_TRUE(fast && slow) << timer.report_at("fast:D", ot::MAX, ot::RISE).value_or(-1) << " "
                            << timer.report_at("fast:CLK", ot::MAX, ot::RISE).value_or(-1) << " "
                            << timer.report_rat("fast:D", ot::MAX, ot::RISE).value_or(-1);
  EXPECT_NEAR(*fast, 1000 - 10, 1e-3);
  EXPECT_NEAR(*slow, 6400 - 10, 1e-3);
}
TEST_F(SdcClock, NegativeEdgeCheckCapturesTheSameCycleRisingLaunch) {
  // A posedge -> negedge (half-cycle) path is captured at the falling edge of
  // the same cycle for setup and checked against the previous fall for hold.
  const auto path = directory + "/flops.lib";
  std::ofstream(path) << kFlopLib;
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Sdc_probe pass(var);
  auto&     timer = pass.timer;
  timer.read_celllib(path).insert_primary_input("c");
  timer.insert_gate("gate", "DFFN").insert_net("n");
  timer.disconnect_pin("c").connect_pin("c", "n").connect_pin("gate:CLK", "n");
  timer.connect_pin("gate:D", "wire");
  pass.read_sdc(write("create_clock -name clk -period 1000 [get_ports c]\nset_input_delay -clock clk 0 [get_ports a]\n"));
  timer.set_ideal_clocks({"clk"});
  for (const auto* port : {"a", "c"}) {
    for (auto split : {ot::MIN, ot::MAX}) {
      timer.set_slew(port, split, ot::RISE, 1).set_slew(port, split, ot::FALL, 1);
    }
  }
  const auto setup = timer.report_slack("gate:D", ot::MAX, ot::RISE);
  const auto hold  = timer.report_slack("gate:D", ot::MIN, ot::RISE);
  ASSERT_TRUE(setup && hold);
  EXPECT_NEAR(*setup, 500 - 10, 1e-3);
  EXPECT_GT(*hold, 0);
}
TEST_F(SdcClock, InvertedClockCaptureIsTheSameCycleFallingEdge) {
  // A positive-edge flop behind a clock inverter (the enable flop of a
  // synthesized clock gate) captures at the source clock's falling edge: a
  // half-cycle path from a rising launch, as OpenSTA times it. It used to be
  // captured a full period later, 1.5 ns optimistic on NVDLA partition_p.
  auto lib = std::string(kFlopLib);
  lib.insert(lib.rfind('}'), R"lib(cell(CKINV) {
      pin(A) { direction : input; capacitance : 1; }
      pin(Y) { direction : output; function : "!A";
        timing() { related_pin : "A"; timing_sense : negative_unate;
          cell_rise(scalar) { values("0"); } cell_fall(scalar) { values("0"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); } } } }
  )lib");
  const auto path = directory + "/flops_inv.lib";
  std::ofstream(path) << lib;
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Sdc_probe pass(var);
  auto&     timer = pass.timer;
  timer.read_celllib(path).insert_primary_input("c");
  timer.insert_gate("inv", "CKINV").insert_gate("gate", "DFF").insert_net("n").insert_net("ni");
  timer.disconnect_pin("c").connect_pin("c", "n").connect_pin("inv:A", "n");
  timer.connect_pin("inv:Y", "ni").connect_pin("gate:CLK", "ni");
  timer.connect_pin("gate:D", "wire");
  pass.read_sdc(write("create_clock -name clk -period 1000 [get_ports c]\nset_input_delay -clock clk 0 [get_ports a]\n"));
  timer.set_ideal_clocks({"clk"});
  for (const auto* port : {"a", "c"}) {
    for (auto split : {ot::MIN, ot::MAX}) {
      timer.set_slew(port, split, ot::RISE, 1).set_slew(port, split, ot::FALL, 1);
    }
  }
  const auto setup = timer.report_slack("gate:D", ot::MAX, ot::RISE);
  ASSERT_TRUE(setup);
  EXPECT_NEAR(*setup, 500 - 10, 1e-3);
}
TEST_F(SdcClock, ClockWaveformIsHonoredOnAPhysicalSource) {
  // `-waveform` used to make the clock virtual, timing the clock network as data.
  Sdc_probe pass(var);
  pass.read_sdc(write("create_clock -name clk -period 1265 -waveform {0 750} [get_ports a]\n"));
  pass.timer.update_timing();
  const auto& clocks = pass.timer.clocks();
  ASSERT_TRUE(clocks.contains("clk"));
  EXPECT_FLOAT_EQ(clocks.at("clk").period(), 1265);
  EXPECT_FLOAT_EQ(clocks.at("clk").waveform(ot::RISE), 0);
  EXPECT_FLOAT_EQ(clocks.at("clk").waveform(ot::FALL), 750);
}
TEST_F(SdcClock, ResizingAGateKeepsItsCaseAnalysisMasks) {
  // An incremental repower rebuilt the arcs with every case mask cleared, so a
  // constant-disabled false path through the resized gate became real.
  const auto path = directory + "/case.lib";
  std::ofstream(path) << R"lib(library(cases) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(T0) { pin(Y) { direction : output; function : "0"; } }
    cell(AND) { area : 1;
      pin(A) { direction : input; capacitance : 2; }
      pin(B) { direction : input; capacitance : 2; }
      pin(Y) { direction : output; function : "A * B";
        timing() { related_pin : "A B"; timing_sense : positive_unate;
          cell_rise(scalar) { values("20"); } cell_fall(scalar) { values("20"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); } } } }
    cell(AND_STRONG) { area : 2;
      pin(A) { direction : input; capacitance : 3; }
      pin(B) { direction : input; capacitance : 3; }
      pin(Y) { direction : output; function : "A * B";
        timing() { related_pin : "A B"; timing_sense : positive_unate;
          cell_rise(scalar) { values("10"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("1"); } fall_transition(scalar) { values("1"); } } } }
  })lib";
  var.dict["files"] = path;
  var.set_stage_labels(var.dict);
  Circuit_probe probe(var);
  auto&         timer = probe.timer;
  timer.read_celllib(path).insert_primary_input("a");
  timer.insert_gate("zero", "T0").insert_gate("masked", "AND");
  timer.insert_net("data").insert_net("zero");
  timer.disconnect_pin("a").connect_pin("a", "data").connect_pin("masked:A", "data");
  timer.connect_pin("zero:Y", "zero").connect_pin("masked:B", "zero");
  for (auto split : {ot::MIN, ot::MAX}) {
    timer.set_at("a", split, ot::RISE, 200).set_at("a", split, ot::FALL, 200);
    timer.set_slew("a", split, ot::RISE, 1).set_slew("a", split, ot::FALL, 1);
  }
  ASSERT_TRUE(timer.report_at("masked:Y", ot::MAX, ot::RISE));
  probe.apply_case_analysis();
  ASSERT_FALSE(timer.report_at("masked:Y", ot::MAX, ot::RISE));
  timer.repower_gate("masked", "AND_STRONG");
  EXPECT_FALSE(timer.report_at("masked:Y", ot::MAX, ot::RISE));
  timer.repower_gate("masked", "AND");
  EXPECT_FALSE(timer.report_at("masked:Y", ot::MAX, ot::RISE));
}

class PhysicalMemory : public ::testing::Test {
protected:
  std::string        directory;
  hhds::GraphLibrary library;
  void               SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-physical-memory-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    std::ofstream(directory + "/units.lib") << R"lib(library(units) {
      delay_model : table_lookup; time_unit : "1ns"; capacitive_load_unit (1,ff);
    })lib";
  }
  void                TearDown() override { std::filesystem::remove_all(directory); }
  // `shared_clock`: only port 0 has a clock pin; ports 1 and 2 have none.
  rapidjson::Document time(bool shared_clock, const std::string& name, bool critical = false) {
    namespace gu                        = livehd::graph_util;
    auto                             io = library.create_io(name);
    const std::map<std::string, int> widths{
        {"clk", 1},
        { "wa", 3},
        { "ra", 3},
        { "rb", 3},
        {"din", 4},
        { "en", 1}
    };
    unsigned port = 0;
    for (const auto* input : {"clk", "wa", "ra", "rb", "din", "en"}) {
      io->add_input(input, port++);
      io->set_bits(input, widths.at(input));
    }
    io->add_output("y", 0);
    io->set_bits("y", 4);
    io->add_output("z", 1);
    io->set_bits("z", 4);
    auto graph = io->create_graph();
    for (const auto& d : io->get_input_pin_decls()) {
      gu::set_bits(graph->get_input_pin(d.name), d.bits);
    }
    auto mem = gu::create_typed_node(*graph, Ntype_op::Memory);
    mem.set_name("ram");
    const auto constant = [&](unsigned pid, int value) {
      mem.create_sink_pin(pid).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
    };
    constant(1, 4);
    constant(9, 8);
    constant(6, 1);
    std::map<std::string, std::vector<std::string>> sinks;  // input bit -> memory sink pins
    const auto                                      connect = [&](unsigned pid, const std::string& input) {
      mem.create_sink_pin(pid).connect_driver(graph->get_input_pin(input));
      for (int bit = 0; bit < widths.at(input); ++bit) {
        sinks[widths.at(input) > 1 ? std::format("{}[{}]", input, bit) : input].push_back(std::format("ram:in_{}[{}]", pid, bit));
      }
    };
    for (unsigned p = 0; p < 3; ++p) {
      const auto base = p * Ntype::Memory_port_stride;
      connect(base, p == 0 ? "wa" : p == 1 ? "ra" : "rb");
      if (p == 0 || !shared_clock) {
        connect(base + 2, "clk");
      }
      if (p == 1) {
        constant(base + 4, 1);
      } else {
        connect(base + 4, "en");
      }
      constant(base + 7, 1);
      constant(base + 10, p != 0);
    }
    connect(3, "din");
    for (unsigned pid = 1; pid <= 2; ++pid) {
      auto q = mem.create_driver_pin(pid);
      gu::set_bits(q, 4);
      graph->get_output_pin(pid == 1 ? "y" : "z").connect_driver(q);
    }
    mem.attr(livehd::attrs::place).set(Ann_place(100, 100, 50, 50));
    auto rc      = std::make_shared<livehd::physical::Parasitics>();
    rc->identity = "hand-built";
    rc->cells.push_back({"ram", 100, 100, 0, "", 50, 50});
    for (const auto& [driver, pins] : sinks) {
      livehd::physical::Rc_net net{driver, {{driver, 1}}, {}};
      for (const auto& pin : pins) {
        net.edges.push_back({0, static_cast<unsigned>(net.nodes.size()), driver == "clk" ? 5000.0 : 10.0});
        net.nodes.push_back({pin, 1});
      }
      rc->nets.push_back(std::move(net));
    }
    Eprp_var var;
    var.graphs           = {graph};
    var.physical_rc      = rc;
    var.dict["files"]    = directory + "/units.lib";
    var.dict["targets"]  = "clk:10ns";
    var.dict["top"]      = name;
    var.dict["hier"]     = "false";
    var.dict["critical"] = critical ? "true" : "false";
    var.dict["qor"]      = directory + "/" + name + ".json";
    var.set_stage_labels(var.dict);
    Pass_opentimer::time_work(var);
    std::ifstream       file(directory + "/" + name + ".json");
    std::string         contents{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document json;
    json.Parse(contents.c_str());
    if (!critical) {
      livehd::opentimer::Physical_session session(var);
      const auto                          feedback = session.evaluate(graph, rc);
      const auto                          repeated = session.evaluate(graph, rc);
      EXPECT_EQ(session.statistics().builds, 1u);
      EXPECT_EQ(feedback.pins.size(), repeated.pins.size());
      for (const auto& p : json["designs"][0]["physical_pins"].GetArray()) {
        const auto& pin = feedback.pins.at(p["pin"].GetString());
        EXPECT_EQ(pin.clock, p["clock"].GetBool());
        EXPECT_EQ(pin.required.has_value(), p.HasMember("required"));
        if (pin.required) {
          EXPECT_NEAR(*pin.required, p["required"].GetDouble(), 1e-5);
        }
      }
    }
    return json;
  }
  // Every addr/din/enable capture has a required time; clock pins have none.
  static void check(const rapidjson::Document& json) {
    ASSERT_FALSE(json.HasParseError());
    const auto& design = json["designs"][0];
    EXPECT_TRUE(design["constraints_complete"].GetBool());
    EXPECT_EQ(std::string_view(design["critical_pin"].GetString()).find("clock_pin"), std::string_view::npos)
        << design["critical_pin"].GetString();
    unsigned captures = 0;
    for (const auto& p : design["physical_pins"].GetArray()) {
      const std::string_view pin = p["pin"].GetString();
      if (pin.starts_with("ram:in_2[") || pin.starts_with("ram:in_18[") || pin.starts_with("ram:in_34[")) {
        EXPECT_FALSE(p.HasMember("required")) << pin;
      } else if (pin.starts_with("ram:in_")) {
        ++captures;
        EXPECT_TRUE(p.HasMember("required")) << pin;
      }
    }
    EXPECT_EQ(captures, 3 + 3 + 3 + 4 + 2);  // wa, ra, rb, din, port 0/2 enables
  }
};
TEST_F(PhysicalMemory, TargetOnlyClockConstrainsEveryCaptureButNotClockPins) { check(time(false, "per_port_clock")); }
TEST_F(PhysicalMemory, PortsWithoutAClockPinCaptureInTheMemoryClock) { check(time(true, "shared_clock")); }
TEST_F(PhysicalMemory, CriticalModeDoesNotTimeClockPinsAsCaptures) { check(time(false, "critical", true)); }
}  // namespace
