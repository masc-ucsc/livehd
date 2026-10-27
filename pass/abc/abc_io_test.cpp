// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <filesystem>
#include <fstream>

#include "abc_boundary.hpp"
#include "attrs.hpp"
#include "gtest/gtest.h"
#include "inou/cgen/cgen_verilog.hpp"
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
  explicit Fixture(int length = 4) {
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
    for (int i = 0; i < length; ++i) {
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
TEST(AbcIo, GrowthAdmissionRejectsBeforePublishingAProvedReplacement) {
  Fixture    f;
  auto       mapper = synth::make_io_mapper("abc",
                                            {
                                                {"library", "inou/prp/tests/abc/timing.lib"}
  });
  auto       store  = std::make_shared<synth::Io_store>(std::string(std::getenv("TEST_TMPDIR")) + "/growth_admission", 1000, false);
  const auto original           = synth::mapped_identity(*f.graph);
  unsigned   queries            = 0;
  store->admit_physical_rewrite = [&](auto& candidate, const auto& masters) {
    ++queries;
    EXPECT_TRUE(masters.empty());
    EXPECT_EQ(synth::mapped_identity(*f.graph), original);
    EXPECT_NE(synth::mapped_identity(candidate), original);
    return false;
  };
  mapper.attach_store(store);
  synth::Io_environment env;
  env.inputs["a[0]"]   = 950;
  env.required["y[0]"] = 1000;
  const auto result    = mapper(*f.graph, env, {}, 1000, 5, true);
  EXPECT_TRUE(result.fired);
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.decision, "physical growth budget exceeded");
  EXPECT_EQ(queries, 1U);
  EXPECT_EQ(synth::mapped_identity(*f.graph), original);
}
TEST(AbcIo, RemappedBusSelectionEmitsStructuralVerilog) {
  Fixture f;
  auto    input = f.graph->get_input_pin("a");
  f.graph->get_io()->set_bits("a", 2);
  graph_util::set_bits(input, 2);
  auto first = f.graph->get_output_pin("y").get_driver_pin();
  while (first.get_master_node().get_name() != "inv0") {
    first = first.get_master_node().get_sink_pin("A").get_driver_pin();
  }
  auto sink = first.get_master_node().get_sink_pin("A");
  sink.del_sink(input);
  auto select = graph_util::create_get_mask(*f.graph, input, 1).create_driver_pin(0);
  graph_util::set_bits(select, 1);
  graph_util::set_unsign(select);
  select.connect_sink(sink);
  f.graph->commit();
  auto                  mapper = synth::make_io_mapper("abc",
                                                       {
                                                           {"library", "inou/prp/tests/abc/timing.lib"}
  });
  synth::Io_environment env;
  env.inputs["a[1]"]   = 950;
  env.required["y[0]"] = 1000;
  auto result          = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(result.accepted) << result.decision;
  EXPECT_TRUE(mapper(*f.graph, env, env, 1000, 5, false).checked);
  const auto directory = testing::TempDir() + "/abc_io_structural";
  std::filesystem::create_directories(directory);
  Cgen_verilog emitter(directory);
  emitter.do_from_graph(f.graph);
  std::ifstream stream(directory + "/top__c1.v");
  ASSERT_TRUE(stream);
  const std::string verilog{std::istreambuf_iterator<char>(stream), {}};
  EXPECT_NE(verilog.find("a[1]"), std::string::npos);
  EXPECT_EQ(verilog.find("always"), std::string::npos);
  EXPECT_EQ(verilog.find("reg "), std::string::npos);
  std::filesystem::remove_all(directory);
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
TEST(AbcIo, FailedFrequencyTargetPreventsRepeatedSynthesis) {
  Fixture    f;
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/abc_failed_target";
  std::filesystem::remove_all(directory);
  auto store  = std::make_shared<synth::Io_store>(directory, 1000);
  auto mapper = synth::make_io_mapper("abc",
                                      {
                                          {"library", "inou/prp/tests/abc/timing.lib"}
  });
  mapper.attach_store(store);
  synth::Io_environment env;
  env.inputs["a[0]"]   = 1100;
  env.required["y[0]"] = 1000;
  const auto first     = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(first.fired);
  EXPECT_DOUBLE_EQ(first.required_period_ps, 1000);
  EXPECT_DOUBLE_EQ(first.requested_period_ps, 900);
  const auto identity = synth::mapped_identity(*f.graph);
  ASSERT_TRUE(store->target_failed("top__c1", identity, 900)) << first.decision;
  store->save();
  mapper.attach_store(std::make_shared<synth::Io_store>(directory, 1000));
  const auto repeated = mapper(*f.graph, env, {}, 1000, 5, true);
  EXPECT_FALSE(repeated.checked);
  EXPECT_FALSE(repeated.fired);
  EXPECT_FALSE(repeated.accepted);
  EXPECT_EQ(repeated.decision, "previous synthesis missed target");
  EXPECT_EQ(synth::mapped_identity(*f.graph), identity);
  const auto tighter = mapper(*f.graph, env, {}, 900, 5, true);
  EXPECT_EQ(tighter.decision, "previous synthesis missed target");
  const auto relaxed = mapper(*f.graph, env, {}, 1200, 5, true);
  EXPECT_EQ(relaxed.decision, "previous synthesis missed target");
}
TEST(AbcIo, AchievedMarginAvoidsRemappingAfterSmallPlacementChanges) {
  Fixture f(24);
  auto    store  = std::make_shared<synth::Io_store>(std::string(std::getenv("TEST_TMPDIR")) + "/abc_target_margin", 1000, false);
  auto    mapper = synth::make_io_mapper("abc",
                                         {
                                             {"library", "inou/prp/tests/abc/timing.lib"}
  });
  mapper.attach_store(store);
  synth::Io_environment env;
  env.inputs["a[0]"]   = 850;
  env.required["y[0]"] = 1000;
  const auto first     = mapper(*f.graph, env, {}, 1000, 5, true);
  ASSERT_TRUE(first.accepted) << first.decision;
  ASSERT_GE(first.slack_after_ps, 100);
  ASSERT_TRUE(store->target_covered("top__c1", synth::mapped_identity(*f.graph), 1000, env));
  auto shifted            = env;
  shifted.inputs["a[0]"] += 20;
  const auto repeat       = mapper(*f.graph, shifted, env, 1000, 5, true);
  EXPECT_FALSE(repeat.checked);
  EXPECT_FALSE(repeat.fired);
  EXPECT_EQ(repeat.decision, "within previous synthesis margin");
  shifted.inputs["a[0]"] += 90;
  const auto outside      = mapper(*f.graph, shifted, env, 1000, 5, true);
  EXPECT_TRUE(outside.checked);
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
TEST(AbcIo, NativeMemoryMetadataSurvivesWholeColorRewrite) {
  Fixture f;
  auto    memory = graph_util::create_typed_node(*f.graph, Ntype_op::Memory, 1);
  memory.set_name("memory");
  f.graph->get_input_pin("a").connect_sink(memory.create_sink_pin(0));
  f.graph->get_output_pin("y").get_driver_pin().connect_sink(memory.create_sink_pin(3));
  const std::vector<std::pair<hhds::Port_id, int64_t>> metadata = {
      {                            1,  16},
      {                            5,   0},
      {                            6,   1},
      {                            7,   0},
      {                            8,   1},
      {                            9, 512},
      {                           10,   0},
      {                           11,   0},
      {                           15,   1},
      {Ntype::Memory_port_stride + 1,  16},
      {Ntype::Memory_port_stride + 9, 512}
  };
  for (const auto& [pid, value] : metadata) {
    graph_util::create_const(*f.graph, *Dlop::create_integer(value)).connect_sink(memory.create_sink_pin(pid));
  }
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
    if (node.get_name() != "memory") {
      continue;
    }
    found = true;
    EXPECT_EQ(graph_util::type_op_of(node), Ntype_op::Memory);
    for (const auto& [pid, value] : metadata) {
      const auto driver = node.get_sink_pin(pid).get_driver_pin();
      ASSERT_TRUE(driver.is_const()) << pid;
      ASSERT_TRUE(graph_util::const_of(driver).is_just_i64()) << pid;
      EXPECT_EQ(graph_util::const_of(driver).to_just_i64(), value) << pid;
    }
    EXPECT_EQ(node.get_sink_pin(0).get_driver_pin(), f.graph->get_input_pin("a"));
    EXPECT_EQ(node.get_sink_pin(3).get_driver_pin(), f.graph->get_output_pin("y").get_driver_pin());
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
