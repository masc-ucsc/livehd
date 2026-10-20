// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_boundary.hpp"
#include "attrs.hpp"
#include "gtest/gtest.h"
#include "io_resynth.hpp"
#include "node_util.hpp"

// clang-format off
extern "C" {
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/scl/sclLib.h"
#include "map/scl/sclSize.h"
}
// clang-format on

namespace livehd::abc {
namespace {
struct Fixture {
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  Fixture() {
    auto inv = library.create_io("INVx1");
    inv->add_input("A", 1);
    inv->add_output("Y", 2);
    inv->set_bits("A", 1);
    inv->set_bits("Y", 1);
    auto io = library.create_io("top__c1");
    io->add_input("a", 1);
    io->add_output("y", 2);
    io->set_bits("a", 1);
    io->set_bits("y", 1);
    graph = io->create_graph();
    graph->get_input_node().attr(attrs::synth_region).set("top__c1");
    graph->get_input_node().attr(attrs::synth_region_id).set(1);
    auto driver = graph->get_input_pin("a");
    graph_util::set_bits(driver, 1);
    for (int i = 0; i < 4; ++i) {
      auto n = graph_util::create_typed_node(*graph, Ntype_op::Sub);
      n.set_subnode(inv);
      n.set_name("inv" + std::to_string(i));
      driver.connect_sink(n.create_sink_pin("A"));
      driver = n.create_driver_pin("Y");
      graph_util::set_bits(driver, 1);
    }
    driver.connect_sink(graph->get_output_pin("y"));
    graph->commit();
  }
};
TEST(AbcIo, MappedBoundaryTimingAndProvedRewrite) {
  Fixture               f;
  auto                  mapper = synth::make_io_mapper("abc",
                                                       {
                                                           {   "library", "inou/prp/tests/abc/timing.lib"},
                                                           {"max_fanout",                            "32"}
  });
  synth::Io_environment env;
  env.inputs["a[0]"]    = 950;
  env.required["y[0]"]  = 1000;
  const auto original   = synth::mapped_identity(*f.graph);
  const auto inspection = mapper(*f.graph, env, {}, 1000, 5, false);
  ASSERT_TRUE(inspection.checked);
  EXPECT_TRUE(inspection.fired);
  EXPECT_EQ(inspection.input, "a[0]");
  EXPECT_EQ(synth::mapped_identity(*f.graph), original);
  EXPECT_GE(inspection.bindings.size(), 2);
  const auto unchanged = mapper(*f.graph, env, env, 1000, 5, true);
  EXPECT_FALSE(unchanged.fired);
  EXPECT_EQ(synth::mapped_identity(*f.graph), original);
  const auto result = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(result.fired);
  ASSERT_TRUE(result.accepted) << result.decision;
  EXPECT_GE(result.slack_after_ps + 1e-4, result.slack_before_ps);
  EXPECT_NE(synth::mapped_identity(*f.graph), original);
  EXPECT_EQ(f.graph->get_io()->get_input_pin_decls().front().name, "a");
  EXPECT_EQ(f.graph->get_io()->get_output_pin_decls().front().name, "y");
  EXPECT_TRUE(mapper(*f.graph, env, env, 1000, 5, false).checked);
}
TEST(AbcIo, WireLoadIsScopedAndChangesTiming) {
  Fixture               f;
  auto                  mapper = synth::make_io_mapper("abc",
                                                       {
                                                           {"library", "inou/prp/tests/abc/timing.lib"}
  });
  synth::Io_environment env;
  const auto            baseline = mapper(*f.graph, env, {}, 1000, 5, false);
  env.wire.capacitance_ff_um     = .2;
  env.wire.length_um             = {
      {1, 100},
      {4, 300}
  };
  const auto loaded = mapper(*f.graph, env, {}, 1000, 5, false);
  ASSERT_TRUE(loaded.checked);
  EXPECT_GT(loaded.wire_delay_ps, loaded.no_wire_delay_ps);
  env.wire         = {};
  const auto after = mapper(*f.graph, env, {}, 1000, 5, false);
  EXPECT_DOUBLE_EQ(after.wire_delay_ps, baseline.wire_delay_ps);
}
TEST(AbcIo, ConstantOutputsStayGraphConstants) {
  Fixture f;
  f.graph->get_io()->add_output("zero", 3);
  f.graph->get_io()->set_bits("zero", 1);
  graph_util::create_const(*f.graph, *Dlop::create_integer(0)).connect_sink(f.graph->get_output_pin("zero"));
  f.graph->get_io()->add_output("one", 4);
  f.graph->get_io()->set_bits("one", 1);
  graph_util::create_const(*f.graph, *Dlop::create_integer(1)).connect_sink(f.graph->get_output_pin("one"));
  f.graph->commit();
  auto                  mapper = synth::make_io_mapper("abc",
                                                       {
                                                           {"library", "inou/prp/tests/abc/timing.lib"}
  });
  synth::Io_environment env;
  env.inputs["a[0]"]   = 950;
  env.required["y[0]"] = 1000;
  const auto result    = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(result.accepted) << result.decision;
  auto driver = f.graph->get_output_pin("zero").get_driver_pin();
  ASSERT_TRUE(driver.is_const());
  EXPECT_TRUE(graph_util::const_of(driver).is_just_i64());
  EXPECT_FALSE(graph_util::const_of(driver).bit_test(0));
  auto one = f.graph->get_output_pin("one").get_driver_pin();
  ASSERT_TRUE(one.is_const());
  EXPECT_TRUE(graph_util::const_of(one).bit_test(0));
  for (auto node : f.graph->body().nodes()) {
    if (auto io = node.get_subnode_io()) {
      EXPECT_NE(io->get_name(), "_const0_");
      EXPECT_NE(io->get_name(), "_const1_");
    }
  }
}
TEST(AbcIo, NativeStateAndClockPinSurviveWholeColorRewrite) {
  Fixture f;
  f.graph->get_io()->add_input("clk", 3);
  f.graph->get_io()->set_bits("clk", 1);
  auto clock = f.graph->get_input_pin("clk");
  graph_util::set_bits(clock, 1);
  auto state_io = f.library.create_io("DFF_KEEP");
  state_io->add_input("D", 1);
  state_io->add_input("CLK", 2);
  state_io->add_output("Q", 3);
  for (const auto* pin : {"D", "CLK", "Q"}) {
    state_io->set_bits(pin, 1);
  }
  auto state = graph_util::create_typed_node(*f.graph, Ntype_op::Sub);
  state.set_subnode(state_io);
  state.set_name("state");
  auto y    = f.graph->get_output_pin("y");
  auto data = y.get_driver_pin();
  y.del_sink(data);
  data.connect_sink(state.create_sink_pin("D"));
  clock.connect_sink(state.create_sink_pin("CLK"));
  auto q = state.create_driver_pin("Q");
  graph_util::set_bits(q, 1);
  q.connect_sink(y);
  f.graph->commit();
  auto                  mapper = synth::make_io_mapper("abc",
                                                       {
                                                           {"library", "inou/prp/tests/abc/timing.lib"}
  });
  synth::Io_environment env;
  env.inputs["a[0]"]   = 950;
  env.required["y[0]"] = 1000;
  const auto result    = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(result.accepted) << result.decision;
  bool found = false;
  for (auto node : f.graph->body().nodes()) {
    if (node.get_name() == "state") {
      found = true;
      ASSERT_NE(node.get_subnode_io(), nullptr);
      EXPECT_EQ(node.get_subnode_io()->get_name(), "DFF_KEEP");
      EXPECT_EQ(node.get_sink_pin("CLK").get_driver_pin(), f.graph->get_input_pin("clk"));
      EXPECT_EQ(f.graph->get_output_pin("y").get_driver_pin(), node.get_driver_pin("Q"));
    }
  }
  EXPECT_TRUE(found);
}
TEST(AbcIo, WireCapInterpolationAndExtrapolationUseFf) {
  auto* frame    = Abc_FrameCreate();
  auto* previous = Abc_FrameEnter(frame);
  EXPECT_EQ(Cmd_CommandExecute(frame, "read_lib -s inou/prp/tests/abc/timing.lib"), 0);
  auto* library  = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
  auto* original = library->default_wire_load;
  {
    synth::Wire_load wire;
    wire.capacitance_ff_um = .2;
    wire.length_um         = {
        {1, 100},
        {4, 300}
    };
    Wire_load_scope scope(wire, "fixture");
    EXPECT_STREQ(library->default_wire_load, "lhd_fixture");
    auto* model = Abc_SclFindWireLoadModel(library, 0);
    auto* caps  = Abc_SclFindWireCaps(model, 5);
    EXPECT_NEAR(Vec_FltEntry(caps, 1), 20, 1e-4);
    EXPECT_NEAR(Vec_FltEntry(caps, 2), 100. / 3, 1e-4);
    EXPECT_NEAR(Vec_FltEntry(caps, 5), 220. / 3, 1e-4);
    Vec_FltFree(caps);
  }
  EXPECT_EQ(library->default_wire_load, original);
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
}
}  // namespace
}  // namespace livehd::abc
