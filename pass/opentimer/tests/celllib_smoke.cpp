// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "ot/liberty/celllib.hpp"
#include "ot/timer/timer.hpp"

namespace {
class CelllibSmoke : public testing::Test {
protected:
  std::filesystem::path path;

  void SetUp() override {
    char name[] = "/tmp/livehd-celllib-XXXXXX";
    int  fd     = mkstemp(name);
    ASSERT_GE(fd, 0);
    close(fd);
    path = name;
  }
  void TearDown() override { std::filesystem::remove(path); }
  void write(const std::string& body) { std::ofstream(path) << body; }
};

TEST_F(CelllibSmoke, TemplateAxesAndBusAttributes) {
  write(R"lib(library(test) {
    lu_table_template(delay) {
      variable_1 : input_net_transition;
      variable_2 : total_output_net_capacitance;
      index_1("1, 2"); index_2("3, 4, 5");
    }
    type(data) { base_type : array; data_type : bit; bit_width : 4; bit_from : 0; bit_to : 3; }
    cell(RAM) {
      pin(clk) { direction : input; clock : true; }
      bus(A) {
        bus_type : data; direction : input; capacitance : 0.25;
        pin(A[3:0]) {
          timing() { related_pin : clk; timing_type : setup_rising;
            rise_constraint(delay) { values("1, 2, 3", "4, 5, 6"); }
          }
        }
      }
      bus(Q) {
        bus_type : data; direction : output;
        timing() { related_pin : clk; timing_type : rising_edge;
          cell_rise(delay) { index_1("7"); values("8, 9, 10"); }
        }
      }
    }
  })lib");
  ot::Celllib lib;
  lib.read(path);
  const auto* cell = lib.cell("RAM");
  ASSERT_NE(cell, nullptr);
  EXPECT_EQ(cell->cellpins.size(), 9);
  for (int bit = 0; bit < 4; ++bit) {
    const auto* input = cell->cellpin("A[" + std::to_string(bit) + "]");
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->direction, ot::CellpinDirection::INPUT);
    EXPECT_EQ(input->capacitance, 0.25f);
    ASSERT_EQ(input->timings.size(), 1);
    const auto& lut = input->timings.front().rise_constraint;
    ASSERT_TRUE(lut);
    EXPECT_EQ(lut->indices1, (std::vector<float>{1, 2}));
    EXPECT_EQ(lut->indices2, (std::vector<float>{3, 4, 5}));
    EXPECT_EQ(lut->table, (std::vector<float>{1, 2, 3, 4, 5, 6}));
    const auto* output = cell->cellpin("Q[" + std::to_string(bit) + "]");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->direction, ot::CellpinDirection::OUTPUT);
    ASSERT_EQ(output->timings.size(), 1);
    const auto& out_lut = output->timings.front().cell_rise;
    ASSERT_TRUE(out_lut);
    EXPECT_EQ(out_lut->indices1, (std::vector<float>{7}));
    EXPECT_EQ(out_lut->indices2, (std::vector<float>{3, 4, 5}));
    EXPECT_EQ(out_lut->table, (std::vector<float>{8, 9, 10}));
  }
}

// The two libraries deliberately reuse a template name. Copies and merges
// must retain the second library's own axis meanings and waveform conventions.
std::string threshold_libraries() {
  return R"lib(
library(logic) {
  time_unit : "1ps"; capacitive_load_unit(1,ff);
  slew_lower_threshold_pct_rise : 10; slew_upper_threshold_pct_rise : 90;
  slew_lower_threshold_pct_fall : 10; slew_upper_threshold_pct_fall : 90;
  lu_table_template(shared) {
    variable_1 : input_net_transition; variable_2 : total_output_net_capacitance;
    index_1("0,100"); index_2("0,100");
  }
  cell(BUF) {
    pin(A) { direction : input; capacitance : 2; }
    pin(Y) { direction : output;
      timing() { related_pin : "A"; timing_sense : positive_unate;
        cell_rise(shared) { values("10,210", "60,260"); }
        cell_fall(shared) { values("10,210", "60,260"); }
        rise_transition(shared) { values("4,104", "54,154"); }
        fall_transition(shared) { values("4,104", "54,154"); }
      }
    }
  }
}
library(ram) {
  time_unit : "1ns"; capacitive_load_unit(1,pf);
  slew_lower_threshold_pct_rise : 20; slew_upper_threshold_pct_rise : 80;
  slew_lower_threshold_pct_fall : 20; slew_upper_threshold_pct_fall : 80;
  lu_table_template(shared) {
    variable_1 : total_output_net_capacitance; variable_2 : input_net_transition;
    index_1("0,0.1"); index_2("0,0.1");
  }
  cell(RAM) {
    pin(A) { direction : input; capacitance : 0.002; }
    pin(Y) { direction : output;
      timing() { related_pin : "A"; timing_sense : positive_unate;
        cell_rise(shared) { values("0.01,0.06", "0.21,0.26"); }
        cell_fall(shared) { values("0.01,0.06", "0.21,0.26"); }
        rise_transition(shared) { values("0.004,0.054", "0.104,0.154"); }
        fall_transition(shared) { values("0.004,0.054", "0.104,0.154"); }
      }
    }
  }
})lib";
}

TEST_F(CelllibSmoke, ScopedTemplatesSurviveCopiesCollisionsAndMixedUnits) {
  write(threshold_libraries());
  ot::Celllib copy;
  {
    auto libraries = ot::Celllib::read_all(path);
    copy           = libraries.at(1);
  }
  const auto& arc = copy.cell("RAM")->cellpin("Y")->timings.front();
  EXPECT_NEAR(arc.delay(ot::RISE, ot::RISE, .08f, .004f).value(), .058f, 1e-6);
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.read_celllib(path).update_timing();
  for (auto split : {ot::MIN, ot::MAX}) {
    const auto& library = timer.celllib(split);
    const auto& timing  = library->cell("RAM")->cellpin("Y")->timings.front();
    EXPECT_NEAR(timing.delay(ot::RISE, ot::RISE, 80, 4).value(), 58, 1e-4);
    EXPECT_NEAR(timing.slew(ot::RISE, ot::RISE, 80, 4).value(), 48, 1e-4);
    EXPECT_NEAR(library->cell("RAM")->cellpin("A")->capacitance.value(), 2, 1e-5);
  }
}

void build_threshold_chain(ot::Timer& timer) {
  timer.insert_primary_input("a").insert_primary_output("y");
  timer.insert_gate("b", "BUF").insert_gate("m", "RAM");
  for (const auto* net : {"input", "middle", "output"}) {
    timer.insert_net(net);
  }
  timer.disconnect_pin("a").disconnect_pin("y");
  timer.connect_pin("a", "input").connect_pin("b:A", "input");
  timer.connect_pin("b:Y", "middle").connect_pin("m:A", "middle");
  timer.connect_pin("m:Y", "output").connect_pin("y", "output");
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      timer.set_at("a", split, tran, 10).set_slew("a", split, tran, 80).set_load("y", split, tran, 4);
    }
  }
}

TEST_F(CelllibSmoke, MixedThresholdChainMatchesIndependentOpenStaCalculation) {
  write(threshold_libraries());
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.read_celllib(path).update_timing();
  build_threshold_chain(timer);
  timer.update_timing();
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      EXPECT_NEAR(timer.pins().at("b:Y").at(split, tran).value(), 64, 1e-4);
      EXPECT_NEAR(timer.pins().at("m:A").slew(split, tran).value(), 34.5, 1e-4);
      EXPECT_NEAR(timer.pins().at("y").at(split, tran).value(), 99.25, 1e-4);
      EXPECT_NEAR(timer.pins().at("y").slew(split, tran).value(), 33.666667, 1e-4);
    }
  }
}

TEST_F(CelllibSmoke, InputDriverTracksLoadWithoutDoubleCountingIntrinsicDelay) {
  write(threshold_libraries());
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.read_celllib(path).update_timing();
  build_threshold_chain(timer);
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      timer.set_input_driver("a", "BUF", "A", "Y", split, tran, {20, 20});
    }
  }
  timer.update_timing();
  for (auto split : {ot::MIN, ot::MAX}) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      EXPECT_NEAR(timer.pins().at("a").at(split, tran).value(), 14, 1e-4);
      EXPECT_NEAR(timer.pins().at("a").slew(split, tran).value(), 16, 1e-4);
      EXPECT_NEAR(timer.pins().at("y").at(split, tran).value(), 59.25, 1e-4);
    }
  }
  timer.insert_gate("extra", "BUF").connect_pin("extra:A", "input").update_timing();
  EXPECT_NEAR(timer.pins().at("a").at(ot::MAX, ot::RISE).value(), 18, 1e-4);
  EXPECT_NEAR(timer.pins().at("a").slew(ot::MAX, ot::RISE).value(), 18, 1e-4);
}

TEST_F(CelllibSmoke, CapacitanceRangesUseEarlyAndLateValuesAndRebaseUnits) {
  auto              text = threshold_libraries();
  const std::string old  = "pin(A) { direction : input; capacitance : 0.002; }";
  const auto        pos  = text.find(old);
  ASSERT_NE(pos, std::string::npos);
  text.replace(pos, old.size(), R"lib(pin(A) { direction : input; capacitance : 0.002;
    rise_capacitance_range (0.001, 0.003);
    fall_capacitance_range (0.0005, 0.004);
  })lib");
  write(text);
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.read_celllib(path).insert_gate("m", "RAM").update_timing();
  const auto& pin = timer.pins().at("m:A");
  EXPECT_NEAR(pin.cap(ot::MIN, ot::RISE), 1, 1e-5);
  EXPECT_NEAR(pin.cap(ot::MAX, ot::RISE), 3, 1e-5);
  EXPECT_NEAR(pin.cap(ot::MIN, ot::FALL), .5, 1e-5);
  EXPECT_NEAR(pin.cap(ot::MAX, ot::FALL), 4, 1e-5);
}
TEST_F(CelllibSmoke, ConcatenatedLibrariesRebaseUnitsAndKeepScopedDefaults) {
  write(R"lib(
    library(logic) {
      time_unit : "1ps"; capacitive_load_unit(1,ff);
      default_input_pin_cap : 2;
      lu_table_template(delay) { variable_1 : input_net_transition; index_1("1,2"); }
      cell(BUF) { area : 1;
        pin(A) { direction : input; }
        pin(Y) { direction : output; function : "A";
          timing() { related_pin : "A";
            cell_rise(delay) { values("3,4"); }
          }
        }
      }
    }
    library(ram) {
      time_unit : "1ns"; capacitive_load_unit(1,pf);
      default_input_pin_cap : 0.025;
      lu_table_template(mem_delay) { variable_1 : input_net_transition; index_1("0.1,0.2"); }
      cell(RAM) { area : 10;
        pin(CLK) { direction : input; clock : true; }
        pin(Q) { direction : output;
          timing() { related_pin : "CLK"; timing_type : rising_edge;
            cell_rise(mem_delay) { values("0.3,0.4"); }
          }
        }
      }
    }
  )lib");
  const auto libraries = ot::Celllib::read_all(path);
  ASSERT_EQ(libraries.size(), 2);
  EXPECT_FLOAT_EQ(libraries[0].cell("BUF")->cellpin("A")->capacitance.value(), 2);
  EXPECT_FLOAT_EQ(libraries[1].cell("RAM")->cellpin("CLK")->capacitance.value(), .025);
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.read_celllib(path).update_timing();
  for (const auto corner : {ot::MIN, ot::MAX}) {
    const auto& lib = timer.celllib(corner);
    ASSERT_TRUE(lib);
    ASSERT_NE(lib->cell("BUF"), nullptr);
    ASSERT_NE(lib->cell("RAM"), nullptr);
    EXPECT_NEAR(lib->cell("RAM")->cellpin("CLK")->capacitance.value(), 25, 1e-4);
    const auto& table = lib->cell("RAM")->cellpin("Q")->timings.front().cell_rise;
    ASSERT_TRUE(table);
    EXPECT_NEAR(table->table[0], 300, 1e-3);
    EXPECT_NEAR(table->indices1[0], 100, 1e-3);
    EXPECT_NEAR(lib->cell("BUF")->cellpin("Y")->timings.front().cell_rise->table[0], 3, 1e-5);
  }
}
TEST_F(CelllibSmoke, IdealClockPreservesInversionDataRcAndCanRestorePropagation) {
  write(R"lib(library(test) {
    time_unit: "1ps"; capacitive_load_unit(1,ff); pulling_resistance_unit: "1ohm";
    cell(BUF) { area: 1;
      pin(A) { direction: input; capacitance: 1; }
      pin(Y) { direction: output; function: "A";
        timing() { related_pin: "A"; timing_sense: positive_unate;
          cell_rise(scalar) { values("10"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("2"); } fall_transition(scalar) { values("2"); }
        }
      }
    }
    cell(INV) { area: 1;
      pin(A) { direction: input; capacitance: 1; }
      pin(Y) { direction: output; function: "!A";
        timing() { related_pin: "A"; timing_sense: negative_unate;
          cell_rise(scalar) { values("10"); } cell_fall(scalar) { values("10"); }
          rise_transition(scalar) { values("2"); } fall_transition(scalar) { values("2"); }
        }
      }
    }
    cell(ANDCLK) { area: 1;
      pin(A) {direction: input; capacitance: 1;}
      pin(EN) {direction: input; capacitance: 1;}
      pin(Y) {direction: output; function: "A & EN";
        timing() { related_pin: "A"; timing_sense: positive_unate;
          cell_rise(scalar) {values("10");} cell_fall(scalar) {values("10");}
          rise_transition(scalar) {values("2");} fall_transition(scalar) {values("2");}
        }
        timing() { related_pin: "EN"; timing_sense: positive_unate;
          cell_rise(scalar) {values("10");} cell_fall(scalar) {values("10");}
          rise_transition(scalar) {values("2");} fall_transition(scalar) {values("2");}
        }
      }
    }
    cell(FF) { area: 2;
      pin(CLK) { direction: input; clock: true; capacitance: 1; }
      pin(D) { direction: input; capacitance: 1;
        timing() { related_pin: "CLK"; timing_type: setup_rising;
          rise_constraint(scalar) { values("5"); } fall_constraint(scalar) { values("5"); }
        }
        timing() { related_pin: "CLK"; timing_type: hold_rising;
          rise_constraint(scalar) { values("2"); } fall_constraint(scalar) { values("2"); }
        }
      }
      pin(Q) { direction: output;
        timing() { related_pin: "CLK"; timing_type: rising_edge; timing_sense: non_unate;
          cell_rise(scalar) { values("3"); } cell_fall(scalar) { values("3"); }
          rise_transition(scalar) { values("2"); } fall_transition(scalar) { values("2"); }
        }
      }
    }
  })lib");
  ot::Timer timer;
  timer.read_celllib(path);
  for (const auto& name : {"clk", "data", "buf_clk", "inv_clk", "data_out", "gated_clk", "q0", "q1"}) {
    timer.insert_net(name);
  }
  timer.insert_primary_input("clk").insert_primary_input("data").insert_primary_output("q0").insert_primary_output("q1");
  timer.insert_gate("cb", "BUF")
      .insert_gate("ci", "INV")
      .insert_gate("db", "BUF")
      .insert_gate("cg", "ANDCLK")
      .insert_gate("f0", "FF")
      .insert_gate("f1", "FF");
  for (const auto& [pin, net] : std::vector<std::pair<std::string, std::string>>{
           {   "clk",       "clk"},
           {  "data",      "data"},
           {  "cb:A",       "clk"},
           {  "cb:Y",   "buf_clk"},
           {  "ci:A",   "buf_clk"},
           {  "ci:Y",   "inv_clk"},
           {  "db:A",      "data"},
           {  "db:Y",  "data_out"},
           {  "cg:A",   "buf_clk"},
           { "cg:EN",  "data_out"},
           {  "cg:Y", "gated_clk"},
           {"f0:CLK", "gated_clk"},
           {"f1:CLK",   "inv_clk"},
           {  "f0:D",  "data_out"},
           {  "f1:D",  "data_out"},
           {  "f0:Q",        "q0"},
           {  "f1:Q",        "q1"},
           {    "q0",        "q0"},
           {    "q1",        "q1"}
  }) {
    timer.connect_pin(pin, net);
  }
  timer.create_clock("clock", "clk", 1000);
  for (auto el : {ot::MIN, ot::MAX}) {
    for (auto rf : {ot::RISE, ot::FALL}) {
      timer.set_at("clk", el, rf, 0).set_slew("clk", el, rf, 0);
      timer.set_at("data", el, rf, 20).set_slew("data", el, rf, 2);
    }
  }
  spef::Spef parasitics;
  parasitics.capacitance_unit = "1 FF";
  parasitics.resistance_unit  = "1 OHM";
  for (const auto& [net, driver, sink] : std::vector<std::tuple<std::string, std::string, std::string>>{
           { "buf_clk", "cb:Y", "cg:A"},
           {"data_out", "db:Y", "f0:D"}
  }) {
    spef::Net n(net, 100);
    n.caps.emplace_back(sink, "", 100);
    n.ress.emplace_back(driver, sink, 1000);
    const auto other = net == "buf_clk" ? "ci:A" : "f1:D";
    n.caps.emplace_back(other, "", 0);
    n.ress.emplace_back(driver, other, 1000);
    if (net == "data_out") {
      n.caps.emplace_back("cg:EN", "", 0);
      n.ress.emplace_back(driver, "cg:EN", 1000);
    }
    parasitics.nets.push_back(std::move(n));
  }
  timer.read_parasitics(std::move(parasitics)).update_timing();
  const auto propagated = timer.pins().at("f0:CLK").at(ot::MAX, ot::RISE);
  const auto data_at    = timer.pins().at("f0:D").at(ot::MAX, ot::RISE);
  ASSERT_TRUE(propagated);
  ASSERT_TRUE(data_at);
  EXPECT_GT(*propagated, 10);
  EXPECT_GT(*data_at, 30);  // data RC was actually applied
  timer.set_ideal_clocks({"clock"}).update_timing();
  EXPECT_TRUE(timer.pins().at("cb:Y").is_ideal_clock());
  EXPECT_FALSE(timer.pins().at("db:Y").is_ideal_clock());
  EXPECT_FALSE(timer.pins().at("cg:EN").is_ideal_clock());
  EXPECT_TRUE(timer.pins().at("cg:Y").is_ideal_clock());
  EXPECT_EQ(timer.pins().at("f0:CLK").at(ot::MAX, ot::RISE), 0);
  EXPECT_EQ(timer.pins().at("f0:CLK").slew(ot::MAX, ot::RISE), 0);
  EXPECT_EQ(timer.pins().at("f1:CLK").at(ot::MAX, ot::RISE), 500);
  EXPECT_EQ(timer.pins().at("f1:CLK").at(ot::MAX, ot::FALL), 0);
  EXPECT_EQ(timer.pins().at("f0:D").at(ot::MAX, ot::RISE), data_at);
  EXPECT_FALSE(timer.pins().at("f0:Q").is_ideal_clock());  // never idealize clock-to-Q
  timer.set_ideal_clocks({}).update_timing();
  EXPECT_EQ(timer.pins().at("f0:CLK").at(ot::MAX, ot::RISE), propagated);
  EXPECT_FALSE(timer.pins().at("f0:CLK").is_ideal_clock());
}
}  // namespace
