// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass_opentimer.hpp"

namespace {
class Critical_probe : public Pass_opentimer {
public:
  explicit Critical_probe(const Eprp_var& var) : Pass_opentimer(var) { timer.set_num_threads(1); }
  using Pass_opentimer::read_sdc;
  using Pass_opentimer::timer;
  using Pass_opentimer::timing_node_key;
  using Pass_opentimer::timing_origins_;
  using Pass_opentimer::timing_pin_nodes_;
  void analyze(const std::shared_ptr<hhds::Graph>& graph) {
    prepare_critical_targets();
    annotate_critical(graph);
    ASSERT_TRUE(apply_annotations(graph, annotation_records_, annotation_summary_));
  }
};
class Critical : public ::testing::Test {
protected:
  std::string                  directory;
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  hhds::Node_class             slow, near, fast, merge;
  void                         SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-critical-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    graph = library.create_io("test")->create_graph();
    slow  = livehd::graph_util::create_typed_node(*graph, Ntype_op::And);
    near  = livehd::graph_util::create_typed_node(*graph, Ntype_op::And);
    fast  = livehd::graph_util::create_typed_node(*graph, Ntype_op::And);
    merge = livehd::graph_util::create_typed_node(*graph, Ntype_op::And);
    slow.attr(livehd::attrs::color).set(17);
    std::ofstream lib(directory + "/cells.lib");
    lib << R"lib(library(test) {
      delay_model : table_lookup; time_unit : "1ns"; capacitive_load_unit (1,pf);
      cell(BUF) {
        area : 1;
        pin(A) { direction : input; capacitance : 0; }
        pin(Y) { direction : output; function : "A";
          timing() { related_pin : "A"; timing_sense : positive_unate;
            cell_rise(scalar) { values("0.1"); } cell_fall(scalar) { values("0.1"); }
            rise_transition(scalar) { values("0.01"); } fall_transition(scalar) { values("0.01"); }
          }
        }
      }
      cell(MERGE) {
        area : 1;
        pin(A) { direction : input; capacitance : 0; }
        pin(B) { direction : input; capacitance : 0; }
        pin(Y) { direction : output; function : "A | B";
          timing() { related_pin : "A B"; timing_sense : positive_unate;
            cell_rise(scalar) { values("0.04"); } cell_fall(scalar) { values("0.04"); }
            rise_transition(scalar) { values("0.01"); } fall_transition(scalar) { values("0.01"); }
          }
        }
      }
    })lib";
  }
  void     TearDown() override { std::filesystem::remove_all(directory); }
  Eprp_var options(std::string targets = "clk:1ns") {
    Eprp_var var;
    var.dict["files"]    = directory + "/cells.lib";
    var.dict["critical"] = "true";
    var.dict["targets"]  = targets;
    var.set_stage_labels(var.dict);
    return var;
  }
  void network(Critical_probe& pass) {
    auto& timer = pass.timer;
    timer.read_celllib(directory + "/cells.lib");
    for (const auto& name : {"a", "b", "c"}) {
      timer.insert_primary_input(name).insert_net(name).insert_gate(std::string{name} + "buf", "BUF");
      timer.connect_pin(std::string{name} + "buf:A", name);
      timer.insert_net(std::string{name} + "out").connect_pin(std::string{name} + "buf:Y", std::string{name} + "out");
      for (auto split : {ot::MIN, ot::MAX}) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          timer.set_at(name, split, tran, name == std::string{"a"} ? .84F : name == std::string{"b"} ? .82F : 0.F);
          timer.set_slew(name, split, tran, .01F);
        }
      }
    }
    timer.insert_gate("merge", "MERGE").connect_pin("merge:A", "aout").connect_pin("merge:B", "bout");
    timer.insert_net("merged").connect_pin("merge:Y", "merged");
    timer.insert_primary_output("y").disconnect_pin("y").connect_pin("y", "merged");
    timer.insert_primary_output("z").disconnect_pin("z").connect_pin("z", "cout");
    pass.timing_pin_nodes_["abuf:Y"]  = slow;
    pass.timing_pin_nodes_["bbuf:Y"]  = near;
    pass.timing_pin_nodes_["cbuf:Y"]  = fast;
    pass.timing_pin_nodes_["merge:Y"] = merge;
    timer.update_timing();
  }
  std::string sdc(std::string text) {
    const auto path = directory + "/clock.sdc";
    std::ofstream(path) << text;
    return path;
  }
};
TEST_F(Critical, MarksEveryNearCriticalBranchAndKeepsPartitionColors) {
  Critical_probe pass(options());
  network(pass);
  pass.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_TRUE(near.attr(livehd::attrs::critical).has());  // second-worst path, not the winning spine
  EXPECT_TRUE(merge.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(fast.attr(livehd::attrs::critical).has());
  EXPECT_EQ(slow.attr(livehd::attrs::color).get(), 17);
}
TEST_F(Critical, TargetMarginDoesNotMarkComfortablyPassingPathsAndClearsOldMarks) {
  {
    Critical_probe pass(options());
    network(pass);
    pass.analyze(graph);
    ASSERT_TRUE(slow.attr(livehd::attrs::critical).has());
  }
  Critical_probe pass(options("clk:2ns"));
  network(pass);
  pass.analyze(graph);
  for (auto node : {slow, near, fast, merge}) {
    EXPECT_FALSE(node.attr(livehd::attrs::critical).has());
  }
}
TEST_F(Critical, SdcDefaultsAndOverridesUseTheSameRequiredArrival) {
  Critical_probe pass(options("virtual:1ns"));
  network(pass);
  pass.read_sdc(sdc("create_clock -name virtual -period 2\nset_output_delay -clock virtual 0 [all_outputs]\n"));
  pass.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_NEAR(*pass.timer.report_rat("y", ot::MAX, ot::RISE), 1, 1e-6);
}
TEST_F(Critical, MultipleClockTargetsStayIndependent) {
  Critical_probe pass(options("core:1ns,bus:4ns"));
  network(pass);
  pass.read_sdc(
      sdc("create_clock -name core -period 2\ncreate_clock -name bus -period 2\n"
          "set_output_delay -clock core 0 [get_ports y]\nset_output_delay -clock bus 0 [get_ports z]\n"));
  pass.analyze(graph);
  EXPECT_TRUE(near.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(fast.attr(livehd::attrs::critical).has());
  EXPECT_NEAR(*pass.timer.report_rat("z", ot::MAX, ot::RISE), 4, 1e-6);
}
TEST_F(Critical, SharedDefinitionUnionsEvidenceAcrossOccurrences) {
  Critical_probe pass(options());
  network(pass);
  pass.timing_origins_[Critical_probe::timing_node_key(fast)] = slow;
  pass.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(fast.attr(livehd::attrs::sta).has());
}
TEST_F(Critical, ScratchGraphsMayReuseOriginalNodeIndices) {
  hhds::GraphLibrary scratch;
  auto               copy  = scratch.create_io("test")->create_graph();
  auto               clone = livehd::graph_util::create_typed_node(*copy, Ntype_op::And);
  ASSERT_EQ(clone.get_debug_nid(), slow.get_debug_nid());
  Critical_probe pass(options());
  network(pass);
  pass.timing_pin_nodes_["abuf:Y"]                             = clone;
  pass.timing_origins_[Critical_probe::timing_node_key(clone)] = slow;
  pass.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(clone.attr(livehd::attrs::sta).has());
}
TEST_F(Critical, HoldOnlyOutputDelayDoesNotReplaceSetupTarget) {
  Critical_probe pass(options(""));
  network(pass);
  pass.read_sdc(sdc("create_clock -name v -period 2\nset_output_delay -clock v -min -0.2 [all_outputs]\n"));
  pass.analyze(graph);
  EXPECT_FALSE(slow.attr(livehd::attrs::critical).has());
  EXPECT_NEAR(*pass.timer.report_rat("y", ot::MAX, ot::RISE), 2, 1e-6);
  ASSERT_TRUE(pass.timer.report_rat("y", ot::MIN, ot::RISE).has_value());
  EXPECT_NEAR(*pass.timer.report_rat("y", ot::MIN, ot::RISE), .2, 1e-6);
}
TEST_F(Critical, ConfigurableMarginIncludesTheBoundaryAndViolations) {
  auto var                     = options();
  var.dict["critical_percent"] = "2";
  var.set_stage_labels(var.dict);
  Critical_probe pass(var);
  network(pass);
  pass.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(near.attr(livehd::attrs::critical).has());
  var.dict["critical_percent"] = "0";
  var.dict["targets"]          = "clk:.9ns";
  var.set_stage_labels(var.dict);
  Critical_probe violated(var);
  network(violated);
  violated.analyze(graph);
  EXPECT_TRUE(slow.attr(livehd::attrs::critical).has());
  EXPECT_TRUE(near.attr(livehd::attrs::critical).has());
  EXPECT_FALSE(fast.attr(livehd::attrs::critical).has());
}
}  // namespace
