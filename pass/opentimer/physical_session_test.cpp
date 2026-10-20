// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/place/repair.hpp"
#include "pass_opentimer.hpp"

namespace {
namespace gu   = livehd::graph_util;
using Feedback = livehd::timing::Feedback;
struct Circuit {
  hhds::GraphLibrary                            library;
  std::shared_ptr<hhds::Graph>                  graph;
  std::shared_ptr<livehd::physical::Parasitics> rc = std::make_shared<livehd::physical::Parasitics>();
  Circuit(bool strong, bool extra, double resistance = 100, bool shift_indices = false) {
    auto io = library.create_io("top");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    graph = io->create_graph();
    gu::set_bits(graph->get_input_pin("a"), 1);
    if (shift_indices) {
      const auto marker = gu::create_const(*graph, *Dlop::create_integer(42));
      EXPECT_FALSE(marker.is_invalid());
    }
    const auto gate = [&](std::string master, std::string name, const hhds::Pin_class& input, const std::string& physical_input) {
      const auto output = master.find("__buf_") != std::string::npos ? "X" : "Y";
      auto       cell   = library.create_io(master);
      cell->add_input("A", 0);
      cell->set_bits("A", 1);
      cell->add_output(output, 0);
      cell->set_bits(output, 1);
      auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
      node.set_subnode(cell);
      node.set_name(name);
      node.create_sink_pin("A").connect_driver(input);
      auto pin = node.create_driver_pin(output);
      gu::set_bits(pin, 1);
      node.attr(livehd::attrs::place).set(Ann_place(10, 10, 10, 10));
      rc->cells.push_back({name, 10, 10, 0, master, 10, 10});
      rc->nets.push_back({
          physical_input,
          {{physical_input, .1}, {name + ":A", .2}},
          {{0, 1, resistance}}
      });
      return pin;
    };
    auto        u      = gate(strong ? "sky130_fd_sc_hd__buf_2" : "sky130_fd_sc_hd__buf_1", "u", graph->get_input_pin("a"), "a");
    std::string driver = "u:X";
    if (extra) {
      u      = gate("sky130_fd_sc_hd__buf_1", "b", u, driver);
      driver = "b:X";
    }
    auto v = gate("sky130_fd_sc_hd__inv_1", "v", u, driver);
    graph->get_output_pin("y").connect_driver(v);
    rc->nets.push_back({
        "v:Y",
        {{"v:Y", .1}, {"y", .2}},
        {{0, 1, resistance}}
    });
  }
};
class PhysicalSession : public ::testing::Test {
protected:
  std::filesystem::path directory;
  Eprp_var              options;
  void                  SetUp() override {
    directory = std::filesystem::path(std::getenv("TEST_TMPDIR")) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "timing.sdc") << "create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 "
                                               "[all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n";
    std::ifstream source("inou/lefdef/tests/data/sky130hd.lib");
    std::string   liberty{std::istreambuf_iterator<char>(source), {}};
    const auto    begin = liberty.find("cell (\"sky130_fd_sc_hd__buf_1\")");
    ASSERT_NE(begin, std::string::npos);
    auto     end   = liberty.find('{', begin);
    unsigned depth = 1;
    while (depth && ++end < liberty.size()) {
      depth += liberty[end] == '{';
      depth -= liberty[end] == '}';
    }
    ASSERT_EQ(depth, 0u);
    auto variant = liberty.substr(begin, end - begin + 1);
    variant.replace(variant.find("sky130_fd_sc_hd__buf_1"), std::string("sky130_fd_sc_hd__buf_1").size(), "sky130_fd_sc_hd__buf_2");
    auto cap = variant.find("capacitance :", variant.find("pin (\"A\")"));
    variant.replace(cap, variant.find(';', cap) - cap + 1, "capacitance : 0.004206;");
    liberty.insert(liberty.rfind('}'), variant + "\n");
    std::ofstream(directory / "cells.lib") << liberty;
    options.dict = {
        {"files", (directory / "cells.lib").string() + "," + (directory / "timing.sdc").string()},
        {  "top",                                                                          "top"},
        { "hier",                                                                        "false"},
        {  "qor",                                   (directory / "must_not_exist.json").string()}
    };
    options.set_stage_labels(options.dict);
  }
  void        TearDown() override { std::filesystem::remove_all(directory); }
  static void compare(const Feedback& actual, const Feedback& expected) {
    const auto number = [](double a, double b) { EXPECT_NEAR(a, b, 1e-5 * std::max({1., std::abs(a), std::abs(b)})); };
    number(actual.delay, expected.delay);
    number(actual.area, expected.area);
    number(actual.electrical_excess, expected.electrical_excess);
    EXPECT_EQ(actual.violations, expected.violations);
    number(actual.time_unit_ps, expected.time_unit_ps);
    const auto optional = [&](auto a, auto b) {
      ASSERT_EQ(bool(a), bool(b));
      if (a && b) {
        number(*a, *b);
      }
    };
    optional(actual.wns, expected.wns);
    optional(actual.tns, expected.tns);
    optional(actual.hold_wns, expected.hold_wns);
    ASSERT_EQ(actual.pins.size(), expected.pins.size());
    for (const auto& [name, pin] : expected.pins) {
      ASSERT_TRUE(actual.pins.contains(name)) << name;
      const auto& value = actual.pins.at(name);
      optional(value.arrival, pin.arrival);
      optional(value.required, pin.required);
      optional(value.slack, pin.slack);
      optional(value.slew, pin.slew);
      optional(value.load_ff, pin.load_ff);
      optional(value.capacitance_ff, pin.capacitance_ff);
      EXPECT_EQ(value.clock, pin.clock);
      EXPECT_EQ(value.violations, pin.violations);
      number(value.electrical_excess, pin.electrical_excess);
    }
    EXPECT_EQ(actual.path, expected.path);
  }
};
TEST_F(PhysicalSession, ResizeRcTopologyAndRejectedTrialRollbackMatchFreshTiming) {
  livehd::opentimer::Physical_session session(options);
  const auto                          check = [&](Circuit& circuit) {
    const auto                          actual = session.evaluate(circuit.graph, circuit.rc);
    livehd::opentimer::Physical_session fresh(options);
    compare(actual, fresh.evaluate(circuit.graph, circuit.rc));
    EXPECT_FALSE(std::filesystem::exists(directory / "must_not_exist.json"));
    EXPECT_TRUE(actual.source_report.empty());
  };
  Circuit initial(false, false);
  check(initial);
  const auto nets = session.statistics().updated_nets;
  check(initial);
  EXPECT_EQ(nets, session.statistics().updated_nets);
  Circuit resized(true, false, 100, true);
  check(resized);
  EXPECT_EQ(nets, session.statistics().updated_nets);  // Repower does not reload unchanged wire RC.
  Circuit moved(true, false, 500, true);
  check(moved);
  EXPECT_GT(session.statistics().updated_nets, nets);
  Circuit inserted(true, true, 500, true);
  check(inserted);
  check(initial);  // Rejected insertion/resize/RC trial: undo all three kinds of delta.
  EXPECT_EQ(session.statistics().builds, 1u);
  EXPECT_EQ(session.statistics().evaluations, 6u);
  EXPECT_GE(session.statistics().repowered_gates, 2u);
  EXPECT_EQ(session.statistics().inserted_gates, 1u);
  EXPECT_EQ(session.statistics().removed_gates, 1u);
}
TEST_F(PhysicalSession, DirectFeedbackMatchesExplicitReportAndDoesNotReloadFiles) {
  Circuit circuit(false, false);
  auto    report_options     = options;
  report_options.graphs      = {circuit.graph};
  report_options.physical_rc = circuit.rc;
  report_options.dict["qor"] = (directory / "explicit_report.json").string();
  report_options.set_stage_labels(report_options.dict);
  Pass_opentimer::time_work(report_options);
  std::ifstream                       input(directory / "explicit_report.json");
  const std::string                   json{std::istreambuf_iterator<char>(input), {}};
  const auto                          expected = livehd::placement::timing_feedback(json);
  livehd::opentimer::Physical_session session(options);
  compare(session.evaluate(circuit.graph, circuit.rc), expected);
  std::filesystem::remove(directory / "timing.sdc");
  std::filesystem::remove(directory / "cells.lib");
  // Repeated timing must not reread SDC or touch the configured report path.
  compare(session.evaluate(circuit.graph, circuit.rc), expected);
  EXPECT_FALSE(std::filesystem::exists(directory / "must_not_exist.json"));
}
TEST_F(PhysicalSession, InvalidCandidateDoesNotPoisonTheRetainedNetlist) {
  Circuit                             initial(false, false), candidate(true, false);
  livehd::opentimer::Physical_session session(options);
  const auto                          before  = session.evaluate(initial.graph, initial.rc);
  auto                                unknown = candidate.library.create_io("missing_timing_cell");
  unknown->add_input("A", 0);
  unknown->set_bits("A", 1);
  unknown->add_output("Y", 0);
  unknown->set_bits("Y", 1);
  for (auto n : candidate.graph->body().nodes()) {
    if (n.get_name() == "v") {
      n.set_subnode(unknown);
    }
  }
  for (auto& c : candidate.rc->cells) {
    if (c.name == "v") {
      c.master = "missing_timing_cell";
    }
  }
  EXPECT_THROW(session.evaluate(candidate.graph, candidate.rc), std::runtime_error);
  compare(session.evaluate(initial.graph, initial.rc), before);
}
}  // namespace
