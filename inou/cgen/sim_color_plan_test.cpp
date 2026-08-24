// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sim_color_plan.hpp"

#include <algorithm>
#include <memory>
#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {

namespace gu = livehd::graph_util;

struct Loop_fixture {
  std::shared_ptr<hhds::Graph> parent;
  hhds::Node_class             compact;
};

struct Flop_latch_fixture {
  std::shared_ptr<hhds::Graph> graph;
  hhds::Node_class             flop;
  hhds::Node_class             latch;
};

Loop_fixture make_compact_loop(std::string_view tag, uint64_t count) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));

  auto body_io = lib.create_io(std::string(tag) + "_body");
  body_io->add_input("acc_in", 0);
  body_io->add_output("acc_out", 1);
  body_io->set_bits("acc_in", 8);
  body_io->set_bits("acc_out", 8);
  auto body = body_io->create_graph();
  auto inv  = gu::create_typed_node(*body, Ntype_op::Not);
  body->get_input_pin("acc_in").connect_sink(inv.create_sink_pin(0));
  inv.create_driver_pin(0).connect_sink(body->get_output_pin("acc_out"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("seed", 0);
  parent_io->add_output("result", 1);
  parent_io->set_bits("seed", 8);
  parent_io->set_bits("result", 8);
  auto parent = parent_io->create_graph();

  auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
  compact.set_name("user_loop_name_must_not_be_identity");
  hhds::Subnode_loop loop;
  loop.count = count;
  compact.set_subnode(body_io, loop);
  parent->get_input_pin("seed").connect_sink(compact.create_sink_pin(0));
  auto output = compact.create_driver_pin(1);
  output.connect_sink(parent->get_output_pin("result"));
  output.connect_sink(compact.create_sink_pin(0));
  compact.subnode_group().validate();
  return {parent, compact};
}

std::shared_ptr<hhds::Graph> make_parallel(std::string_view tag, bool reverse_creation) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_parallel");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_output("x", 2);
  io->add_output("y", 3);
  auto graph = io->create_graph();

  hhds::Node_class na;
  hhds::Node_class nb;
  if (reverse_creation) {
    nb = gu::create_typed_node(*graph, Ntype_op::Not);
    na = gu::create_typed_node(*graph, Ntype_op::Not);
  } else {
    na = gu::create_typed_node(*graph, Ntype_op::Not);
    nb = gu::create_typed_node(*graph, Ntype_op::Not);
  }
  na.set_name(reverse_creation ? "renamed_a" : "a_internal");
  nb.set_name(reverse_creation ? "renamed_b" : "b_internal");
  graph->get_input_pin("a").connect_sink(na.create_sink_pin(0));
  graph->get_input_pin("b").connect_sink(nb.create_sink_pin(0));
  na.create_driver_pin(0).connect_sink(graph->get_output_pin("x"));
  nb.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  return graph;
}

std::shared_ptr<hhds::Graph> make_conditional_pair() {
  auto& lib      = livehd::Hhds_graph_library::instance("lgdb_color_plan_conditional");
  auto  child_io = lib.create_io("conditional_child");
  child_io->add_input("__valid", 0);
  child_io->add_input("x", 1);
  child_io->add_output("y", 2);
  auto child = child_io->create_graph();
  auto body  = gu::create_typed_node(*child, Ntype_op::Not);
  child->get_input_pin("x").connect_sink(body.create_sink_pin(0));
  body.create_driver_pin(0).connect_sink(child->get_output_pin("y"));

  auto parent_io = lib.create_io("conditional_parent");
  parent_io->add_input("__valid", 0);
  parent_io->add_input("local", 1);
  parent_io->add_input("x", 2);
  parent_io->add_output("a", 3);
  parent_io->add_output("b", 4);
  auto parent = parent_io->create_graph();

  auto local_guard = gu::create_typed_node(*parent, Ntype_op::Not);
  parent->get_input_pin("local").connect_sink(local_guard.create_sink_pin(0));
  auto local_guard_value = local_guard.create_driver_pin(0);

  auto conditional = gu::create_typed_node(*parent, Ntype_op::Sub);
  conditional.set_subnode(child_io);
  local_guard_value.connect_sink(conditional.create_sink_pin(0));
  parent->get_input_pin("x").connect_sink(conditional.create_sink_pin(1));
  conditional.create_driver_pin(2).connect_sink(parent->get_output_pin("a"));

  auto forwarded = gu::create_typed_node(*parent, Ntype_op::Sub);
  forwarded.set_subnode(child_io);
  parent->get_input_pin("__valid").connect_sink(forwarded.create_sink_pin(0));
  parent->get_input_pin("x").connect_sink(forwarded.create_sink_pin(1));
  forwarded.create_driver_pin(2).connect_sink(parent->get_output_pin("b"));
  return parent;
}

Flop_latch_fixture make_flop_feeds_high_latch(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_flop_high_latch_") + std::string(tag));
  auto  io  = lib.create_io(std::string("flop_high_latch_") + std::string(tag));
  io->add_input("clk", 0);
  io->add_input("d", 1);
  io->add_output("qf", 2);
  io->add_output("ql", 3);
  auto graph = io->create_graph();

  auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  graph->get_input_pin("d").connect_sink(gu::setup_sink_by_name(flop, "din"));
  auto fq = flop.create_driver_pin(0);
  fq.connect_sink(graph->get_output_pin("qf"));

  auto latch = gu::create_typed_node(*graph, Ntype_op::Latch);
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(latch, "enable"));
  fq.connect_sink(gu::setup_sink_by_name(latch, "din"));
  latch.create_driver_pin(0).connect_sink(graph->get_output_pin("ql"));
  return {graph, flop, latch};
}

Flop_latch_fixture make_data_gated_latch(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_data_latch_") + std::string(tag));
  auto  io  = lib.create_io(std::string("data_latch_") + std::string(tag));
  io->add_input("en", 0);
  io->add_input("d", 1);
  io->add_output("q", 2);
  auto graph = io->create_graph();

  auto latch = gu::create_typed_node(*graph, Ntype_op::Latch);
  graph->get_input_pin("en").connect_sink(gu::setup_sink_by_name(latch, "enable"));
  graph->get_input_pin("d").connect_sink(gu::setup_sink_by_name(latch, "din"));
  latch.create_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  return {graph, {}, latch};
}

std::shared_ptr<hhds::Graph> make_combinational_chain(std::string_view tag, size_t length) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_chain");
  io->add_input("in", 0);
  io->add_output("out", 1);
  io->set_bits("in", 32);
  io->set_bits("out", 32);
  auto graph = io->create_graph();
  gu::set_bits(graph->get_input_pin("in"), 32);

  auto driver = graph->get_input_pin("in");
  for (size_t i = 0; i < length; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Not);
    driver.connect_sink(node.create_sink_pin(0));
    driver = node.create_driver_pin(0);
    gu::set_bits(driver, 32);
  }
  driver.connect_sink(graph->get_output_pin("out"));
  return graph;
}

std::shared_ptr<hhds::Graph> make_fixed_lane_extract(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_fixed_lane");
  io->add_input("in", 0);
  io->add_output("out", 1);
  io->set_bits("in", 16);
  io->set_bits("out", 4);
  io->set_unsign("in", true);
  io->set_unsign("out", true);
  auto graph = io->create_graph();

  auto producer = gu::create_typed_node(*graph, Ntype_op::Not);
  graph->get_input_pin("in").connect_sink(producer.create_sink_pin(0));
  auto packed = producer.create_driver_pin(0);
  gu::set_bits(packed, 16);
  gu::set_unsign(packed);

  auto get_mask = gu::create_typed_node(*graph, Ntype_op::Get_mask);
  packed.connect_sink(gu::setup_sink_by_name(get_mask, "a"));
  gu::create_const(*graph, *Dlop::create_integer(0xf0)).connect_sink(gu::setup_sink_by_name(get_mask, "mask"));
  auto lane = get_mask.create_driver_pin(0);
  gu::set_bits(lane, 4);
  gu::set_unsign(lane);
  lane.connect_sink(graph->get_output_pin("out"));
  return graph;
}

std::shared_ptr<hhds::Graph> make_fixed_top_input_lane_extract(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_fixed_top_input_lane");
  io->add_input("in", 0);
  io->add_output("out", 1);
  io->set_bits("in", 16);
  io->set_bits("out", 4);
  io->set_unsign("in", true);
  io->set_unsign("out", true);
  auto graph = io->create_graph();

  auto get_mask = gu::create_typed_node(*graph, Ntype_op::Get_mask);
  graph->get_input_pin("in").connect_sink(gu::setup_sink_by_name(get_mask, "a"));
  gu::create_const(*graph, *Dlop::create_integer(0xf0)).connect_sink(gu::setup_sink_by_name(get_mask, "mask"));
  auto lane = get_mask.create_driver_pin(0);
  gu::set_bits(lane, 4);
  gu::set_unsign(lane);
  lane.connect_sink(graph->get_output_pin("out"));
  return graph;
}

std::shared_ptr<hhds::Graph> make_disjoint_or_pack_feedback(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_or_pack_feedback");
  io->add_input("low", 0);
  io->add_output("out", 1);
  io->set_bits("low", 1);
  io->set_bits("out", 1);
  io->set_unsign("low", true);
  io->set_unsign("out", true);
  auto graph = io->create_graph();
  gu::set_bits(graph->get_input_pin("low"), 1);
  gu::set_unsign(graph->get_input_pin("low"));

  auto packed_or = gu::create_typed_node(*graph, Ntype_op::Or);
  auto packed = packed_or.create_driver_pin(0);
  gu::set_bits(packed, 16);
  gu::set_unsign(packed);

  auto wide_read = gu::create_typed_node(*graph, Ntype_op::Get_mask);
  packed.connect_sink(gu::setup_sink_by_name(wide_read, "a"));
  gu::create_const(*graph, *Dlop::create_integer(0xffff)).connect_sink(gu::setup_sink_by_name(wide_read, "mask"));
  auto wide_lane = wide_read.create_driver_pin(0);
  gu::set_bits(wide_lane, 16);
  gu::set_unsign(wide_lane);

  auto low_read = gu::create_typed_node(*graph, Ntype_op::Get_mask);
  wide_lane.connect_sink(gu::setup_sink_by_name(low_read, "a"));
  gu::create_const(*graph, *Dlop::create_integer(0x100)).connect_sink(gu::setup_sink_by_name(low_read, "mask"));
  auto low_lane = low_read.create_driver_pin(0);
  gu::set_bits(low_lane, 1);
  gu::set_unsign(low_lane);
  low_lane.connect_sink(graph->get_output_pin("out"));

  auto high_lane = gu::create_typed_node(*graph, Ntype_op::SHL);
  graph->get_input_pin("low").connect_sink(high_lane.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(8)).connect_sink(high_lane.create_sink_pin(1));
  auto shifted = high_lane.create_driver_pin(0);
  gu::set_bits(shifted, 16);
  gu::set_unsign(shifted);
  shifted.connect_sink(packed_or.create_sink_pin(0));

  auto feedback_wide = gu::create_typed_node(*graph, Ntype_op::Not);
  low_lane.connect_sink(feedback_wide.create_sink_pin(0));
  auto feedback_value = feedback_wide.create_driver_pin(0);
  gu::set_bits(feedback_value, 16);
  gu::set_unsign(feedback_value);
  auto feedback_masked = gu::create_typed_node(*graph, Ntype_op::And);
  feedback_value.connect_sink(feedback_masked.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(0xf)).connect_sink(feedback_masked.create_sink_pin(0));
  auto feedback = feedback_masked.create_driver_pin(0);
  gu::set_bits(feedback, 16);
  gu::set_unsign(feedback);
  auto feedback_pack = gu::create_typed_node(*graph, Ntype_op::Or);
  feedback.connect_sink(feedback_pack.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(feedback_pack.create_sink_pin(0));
  auto feedback_packed = feedback_pack.create_driver_pin(0);
  gu::set_bits(feedback_packed, 16);
  gu::set_unsign(feedback_packed);
  feedback_packed.connect_sink(packed_or.create_sink_pin(0));
  return graph;
}

std::shared_ptr<hhds::Graph> make_memory_with_late_port_clock(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));
  auto  io  = lib.create_io(std::string(tag) + "_late_memory_clock");
  io->add_input("clk", 0);
  io->add_output("q", 1);
  io->set_bits("clk", 1);
  io->set_bits("q", 8);
  auto graph = io->create_graph();

  auto memory = gu::create_typed_node(*graph, Ntype_op::Memory);
  memory.set_name("late_clock_mem");
  constexpr hhds::Port_id port = 3;
  graph->get_input_pin("clk").connect_sink(memory.create_sink_pin(port * Ntype::Memory_port_stride + 2));
  auto q = memory.create_driver_pin(0);
  gu::set_bits(q, 8);
  q.connect_sink(graph->get_output_pin("q"));
  return graph;
}

std::shared_ptr<hhds::Graph> make_narrow_child_boundary(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));

  auto child_io = lib.create_io(std::string(tag) + "_child");
  child_io->add_input("x", 0);
  child_io->add_input("clk", 1);
  child_io->add_output("y", 2);
  child_io->set_bits("x", 64);
  child_io->set_bits("clk", 1);
  child_io->set_bits("y", 64);
  auto child = child_io->create_graph();
  gu::set_bits(child->get_input_pin("x"), 64);
  gu::set_bits(child->get_input_pin("clk"), 1);
  auto state = gu::create_typed_node(*child, Ntype_op::Flop);
  child->get_input_pin("x").connect_sink(gu::setup_sink_by_name(state, "din"));
  child->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(state, "clock_pin"));
  auto child_value = state.create_driver_pin(0);
  gu::set_bits(child_value, 64);
  child_value.connect_sink(child->get_output_pin("y"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("x", 0);
  parent_io->add_input("clk", 1);
  parent_io->add_output("y", 2);
  parent_io->set_bits("x", 65);
  parent_io->set_bits("clk", 1);
  parent_io->set_bits("y", 64);
  auto parent = parent_io->create_graph();
  gu::set_bits(parent->get_input_pin("x"), 65);
  gu::set_bits(parent->get_input_pin("clk"), 1);
  auto pnot = gu::create_typed_node(*parent, Ntype_op::Not);
  parent->get_input_pin("x").connect_sink(pnot.create_sink_pin(0));
  auto wide_value = pnot.create_driver_pin(0);
  gu::set_bits(wide_value, 65);

  auto instance = gu::create_typed_node(*parent, Ntype_op::Sub);
  instance.set_subnode(child_io);
  wide_value.connect_sink(instance.create_sink_pin(0));
  parent->get_input_pin("clk").connect_sink(instance.create_sink_pin(1));
  instance.create_driver_pin(2).connect_sink(parent->get_output_pin("y"));
  return parent;
}

std::shared_ptr<hhds::Graph> make_child_with_site_free_output_alias(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));

  auto child_io = lib.create_io(std::string(tag) + "_child");
  child_io->add_input("alias_in", 0);
  child_io->add_input("clk", 1);
  child_io->add_input("d", 2);
  child_io->add_output("alias_out", 3);
  child_io->add_output("q", 4);
  for (const auto name : {"alias_in", "d", "alias_out", "q"}) {
    child_io->set_bits(name, 8);
  }
  child_io->set_bits("clk", 1);
  auto child = child_io->create_graph();
  child->get_input_pin("alias_in").connect_sink(child->get_output_pin("alias_out"));
  auto flop = gu::create_typed_node(*child, Ntype_op::Flop);
  child->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  child->get_input_pin("d").connect_sink(gu::setup_sink_by_name(flop, "din"));
  flop.create_driver_pin(0).connect_sink(child->get_output_pin("q"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("x", 0);
  parent_io->add_input("clk", 1);
  parent_io->add_input("d", 2);
  parent_io->add_output("x_out", 3);
  parent_io->add_output("q", 4);
  for (const auto name : {"x", "d", "x_out", "q"}) {
    parent_io->set_bits(name, 8);
  }
  parent_io->set_bits("clk", 1);
  auto parent   = parent_io->create_graph();
  auto instance = gu::create_typed_node(*parent, Ntype_op::Sub);
  instance.set_subnode(child_io);
  parent->get_input_pin("x").connect_sink(instance.create_sink_pin(0));
  parent->get_input_pin("clk").connect_sink(instance.create_sink_pin(1));
  parent->get_input_pin("d").connect_sink(instance.create_sink_pin(2));
  instance.create_driver_pin(3).connect_sink(parent->get_output_pin("x_out"));
  instance.create_driver_pin(4).connect_sink(parent->get_output_pin("q"));
  return parent;
}

}  // namespace

TEST(SimColorPlan, CompactLoopDiscoveryIsConstantSizeAndCutsCarry) {
  auto fixture = make_compact_loop("compact", 1'000'000'000ULL);
  auto plan    = livehd::sim::Color_plan::discover(fixture.parent.get());

  ASSERT_TRUE(plan.complete());
  EXPECT_EQ(plan.summary().grouped_sites, 1u) << "the executable plan owns one loop control site, never one site per ordinal";
  EXPECT_EQ(plan.summary().physical_occurrence_sites, 1u)
      << "the independent executable coverage count treats the native body as owned by its control site";
  EXPECT_EQ(plan.summary().compact_loops, 1u);
  EXPECT_EQ(plan.summary().carry_edges_cut, 1u);
  EXPECT_TRUE(plan.validate_retained_handles());

  const auto text        = plan.report();
  const auto observation = text.find("observation-map begin");
  ASSERT_NE(observation, std::string::npos);
  EXPECT_EQ(text.substr(0, observation).find("user_loop_name_must_not_be_identity"), std::string::npos)
      << "user names are allowed only on the observation surface, never in schedule identity";
  EXPECT_EQ(text.find("gid"), std::string::npos);
  EXPECT_EQ(text.find("nid"), std::string::npos);
  EXPECT_NE(text.find("kind=loop-control"), std::string::npos);
  EXPECT_NE(text.find("kind=loop-carry cut=true"), std::string::npos);
}

TEST(SimColorPlan, MemoryClockOnLaterPortCreatesStateUpdate) {
  auto graph = make_memory_with_late_port_clock("late_memory_clock");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  size_t memory_site = livehd::sim::Color_plan::invalid_index;
  for (size_t i = 0; i < plan.sites().size(); ++i) {
    if (gu::type_op_of(plan.sites()[i].node) == Ntype_op::Memory) {
      memory_site = i;
      break;
    }
  }
  ASSERT_NE(memory_site, livehd::sim::Color_plan::invalid_index);
  EXPECT_TRUE(std::ranges::any_of(plan.version_sites(), [&](const auto& version) {
    return version.base_site == memory_site && version.role == livehd::sim::Color_plan::Version_role::state_update;
  })) << "a clock on any Memory port makes the array sequential";
}

TEST(SimColorPlan, RetainedPoliciesSurvivePlanMove) {
  auto fixture  = make_compact_loop("move", 4);
  auto original = livehd::sim::Color_plan::discover(fixture.parent.get());
  auto moved    = std::move(original);
  EXPECT_TRUE(moved.validate_retained_handles());
  EXPECT_EQ(moved.summary().compact_loops, 1u);
}

TEST(SimColorPlan, ReportIgnoresConstructionOrderGraphNamesAndNodeNames) {
  auto forward = make_parallel("ordered", false);
  auto reverse = make_parallel("reversed", true);

  const auto a = livehd::sim::Color_plan::discover(forward.get());
  const auto b = livehd::sim::Color_plan::discover(reverse.get());
  ASSERT_TRUE(a.complete());
  ASSERT_TRUE(b.complete());
  EXPECT_EQ(a.report(), b.report());
  EXPECT_EQ(a.summary().colors, 4u);
  EXPECT_EQ(a.summary().kernel_classes, 2u) << "the two identical NOT kernels reuse once in each state-version slot";
  EXPECT_EQ(a.summary().kernel_reuses, 2u);
  for (const auto& kernel : a.kernel_classes()) {
    EXPECT_EQ(kernel.colors.size(), 2u);
  }
}

TEST(SimColorPlan, ChildPortCastKeepsProducerStorageAndConsumerWidthsSeparate) {
  auto graph = make_narrow_child_boundary("narrow_boundary");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  bool   found_narrowing_boundary = false;
  size_t top_outputs              = 0;
  for (const auto& slot : plan.boundary_slots()) {
    if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_output) {
      ++top_outputs;
      EXPECT_NE(slot.producer_version, livehd::sim::Color_plan::invalid_index)
          << "a root output driven directly by a child must resolve through the Sub boundary";
    }
    for (const auto& consumer : slot.consumers) {
      if (slot.width == 65 && consumer.width == 64) {
        found_narrowing_boundary = true;
      }
    }
  }
  EXPECT_TRUE(found_narrowing_boundary)
      << "the direct ABI must truncate a widened producer at the erased 64-bit child port rather than leak its sign bit\n"
      << plan.report();
  EXPECT_EQ(top_outputs, 2u) << "pre-rise and post-fall public output versions must both be published\n" << plan.report();
  EXPECT_EQ(plan.report().find("observe output port=2 name=\"y\" site=unbound"), std::string::npos);
}

TEST(SimColorPlan, SiteFreeChildOutputAliasResolvesOccurrenceInput) {
  auto graph = make_child_with_site_free_output_alias("site_free_alias");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  ASSERT_TRUE(plan.summary().versioning_complete) << plan.report();
  size_t child_alias_slots = 0;
  for (const auto& slot : plan.boundary_slots()) {
    if (slot.kind != livehd::sim::Color_plan::Boundary_kind::observation_output || slot.public_port != 3) {
      continue;
    }
    ++child_alias_slots;
    EXPECT_EQ(slot.producer_version, livehd::sim::Color_plan::invalid_index)
        << "the child alias is sourced directly by the bound root input";
    EXPECT_EQ(slot.producer_port, 0u);
  }
  EXPECT_EQ(child_alias_slots, 2u) << "both child observation versions must retain the pure alias\n" << plan.report();
}

TEST(SimColorPlan, ConditionalBoundaryExemptsForwardedDefinitionValid) {
  auto graph = make_conditional_pair();
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete());
  EXPECT_EQ(plan.summary().conditional_regions, 1u);
  EXPECT_EQ(plan.summary().grouped_sites, 5u) << "three parent nodes plus one child body at each of two call sites";
  EXPECT_EQ(plan.summary().outer_sites, 4u) << "the local condition is opaque; forwarded __valid descends";
  EXPECT_NE(plan.report().find("kind=conditional-control"), std::string::npos);

  size_t conditional_owner = livehd::sim::Color_plan::invalid_index;
  for (size_t site = 0; site < plan.sites().size(); ++site) {
    if (plan.sites()[site].kind == livehd::sim::Color_plan::Site_kind::conditional_control) {
      conditional_owner = site;
      break;
    }
  }
  ASSERT_NE(conditional_owner, livehd::sim::Color_plan::invalid_index);
  bool saw_conditional_body = false;
  bool saw_unconditional    = false;
  for (const auto& version : plan.version_sites()) {
    saw_conditional_body |= version.control_owner == conditional_owner;
    saw_unconditional    |= version.control_owner == livehd::sim::Color_plan::invalid_index;
  }
  EXPECT_TRUE(saw_conditional_body);
  EXPECT_TRUE(saw_unconditional) << "the forwarded __valid occurrence is part of its enclosing contract, not a new local region";
  for (const auto& color : plan.colors()) {
    const size_t owner = plan.version_sites()[color.members.front()].control_owner;
    for (const size_t member : color.members) {
      EXPECT_EQ(plan.version_sites()[member].control_owner, owner) << "coarsening must not erase a structural activation boundary\n"
                                                                   << plan.report();
    }
  }
}

TEST(SimColorPlan, TransparentHighLatchReadsPostRiseFlopState) {
  auto fixture = make_flop_feeds_high_latch("event_order");
  auto plan    = livehd::sim::Color_plan::discover(fixture.graph.get());

  ASSERT_TRUE(plan.complete());
  ASSERT_TRUE(plan.summary().versioning_complete) << plan.report();
  ASSERT_TRUE(plan.summary().version_dag_acyclic);
  size_t flop_base  = plan.sites().size();
  size_t latch_base = plan.sites().size();
  for (size_t i = 0; i < plan.sites().size(); ++i) {
    if (plan.sites()[i].node.base_node() == fixture.flop) {
      flop_base = i;
    }
    if (plan.sites()[i].node.base_node() == fixture.latch) {
      latch_base = i;
    }
  }
  ASSERT_LT(flop_base, plan.sites().size());
  ASSERT_LT(latch_base, plan.sites().size());

  size_t flop_update    = plan.version_sites().size();
  size_t flop_post_rise = plan.version_sites().size();
  size_t latch_update   = plan.version_sites().size();
  for (size_t i = 0; i < plan.version_sites().size(); ++i) {
    const auto& site = plan.version_sites()[i];
    if (site.base_site == flop_base && site.role == livehd::sim::Color_plan::Version_role::state_update) {
      flop_update = i;
      EXPECT_EQ(site.version, livehd::sim::Color_plan::State_version::pre_rise);
      EXPECT_EQ(site.slot, livehd::sim::Color_plan::Execution_slot::rise_commit);
    }
    if (site.base_site == flop_base && site.role == livehd::sim::Color_plan::Version_role::state_read
        && site.version == livehd::sim::Color_plan::State_version::post_rise) {
      flop_post_rise = i;
    }
    if (site.base_site == latch_base && site.role == livehd::sim::Color_plan::Version_role::state_update) {
      latch_update = i;
      EXPECT_EQ(site.version, livehd::sim::Color_plan::State_version::post_rise);
      EXPECT_EQ(site.slot, livehd::sim::Color_plan::Execution_slot::fall_commit);
    }
  }
  EXPECT_LT(flop_update, plan.version_sites().size());
  ASSERT_LT(flop_post_rise, plan.version_sites().size());
  ASSERT_LT(latch_update, plan.version_sites().size());

  bool linked       = false;
  bool transitioned = false;
  for (const auto& edge : plan.version_dependencies()) {
    linked       |= edge.producer == flop_post_rise && edge.consumer == latch_update;
    transitioned |= edge.producer == flop_update && edge.consumer == flop_post_rise;
  }
  EXPECT_TRUE(transitioned) << "the rise update produces the flop's post-rise state version";
  EXPECT_TRUE(linked) << "the high latch consumes the flop's post-rise Q, never its pre-commit Q";
  EXPECT_LT(plan.version_sites()[flop_update].execution_order, plan.version_sites()[flop_post_rise].execution_order);
  EXPECT_LT(plan.version_sites()[flop_post_rise].execution_order, plan.version_sites()[latch_update].execution_order);

  size_t current_slots = 0;
  size_t pending_slots = 0;
  for (const auto& slot : plan.boundary_slots()) {
    current_slots += slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current;
    pending_slots += slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending;
  }
  EXPECT_EQ(current_slots, 2u) << "each state occurrence owns one persistent current-value slot\n" << plan.report();
  EXPECT_EQ(pending_slots, 2u) << "each data input is parked before its state-only commit action";
  EXPECT_TRUE(plan.summary().boundary_one_writer);
  EXPECT_TRUE(plan.summary().boundary_dominance);
}

TEST(SimColorPlan, CoarsensCombinationalChainsWithoutCrossingExecutionSlots) {
  auto graph = make_combinational_chain("coarsen", 6);
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete());
  ASSERT_TRUE(plan.summary().versioning_complete);
  ASSERT_TRUE(plan.summary().version_dag_acyclic);
  ASSERT_TRUE(plan.summary().color_dag_acyclic);
  EXPECT_EQ(plan.summary().fine_colors, 12u);
  EXPECT_EQ(plan.summary().colors, 2u) << "one whole chain per required observation slot";
  EXPECT_EQ(plan.summary().color_merges, 10u);
  ASSERT_EQ(plan.colors().size(), 2u);
  EXPECT_NE(plan.colors()[0].slot, plan.colors()[1].slot);
  for (const auto& color : plan.colors()) {
    EXPECT_EQ(color.members.size(), 6u);
    EXPECT_LE(color.gate_equivalents, 10'000u);
    for (const size_t member : color.members) {
      EXPECT_EQ(plan.version_sites()[member].slot, color.slot);
      EXPECT_EQ(plan.version_sites()[member].role, livehd::sim::Color_plan::Version_role::data);
    }
  }
  EXPECT_EQ(plan.summary().value_uses, 12u) << "six exact edge uses at each of the two observation versions";
  EXPECT_EQ(plan.summary().boundary_slots, 3u) << "one stable top input plus pre-rise and post-fall output slots";
  EXPECT_EQ(plan.summary().boundary_bits, 96u);
  size_t top_inputs  = 0;
  size_t top_outputs = 0;
  for (const auto& slot : plan.boundary_slots()) {
    top_inputs  += slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input;
    top_outputs += slot.kind == livehd::sim::Color_plan::Boundary_kind::top_output;
    EXPECT_EQ(slot.width, 32u);
  }
  EXPECT_EQ(top_inputs, 1u);
  EXPECT_EQ(top_outputs, 2u);
}

TEST(SimColorPlan, ConstantGetMaskUsesAnLsbAlignedBoundaryLane) {
  auto graph = make_fixed_lane_extract("fixed_lane");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  size_t lane_uses = 0;
  for (const auto& use : plan.value_uses()) {
    const auto& consumer = plan.version_sites()[use.consumer_version];
    if (livehd::graph_util::type_op_of(plan.sites()[consumer.base_site].node.base_node()) != Ntype_op::Get_mask
        || use.consumer_port != Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
      continue;
    }
    ++lane_uses;
    EXPECT_TRUE(use.preextracted);
    EXPECT_EQ(use.producer_extract_lo, 4u);
    EXPECT_EQ(use.producer_extract_hi, 8u);
    EXPECT_EQ(use.producer_shift, 0u);
    EXPECT_EQ(use.width, 4u);
    EXPECT_EQ(use.consumer_width, 4u);
    EXPECT_TRUE(use.unsign);
  }
  EXPECT_EQ(lane_uses, 2u) << "the pre-rise and post-fall observations use the same fixed lane contract\n" << plan.report();
}

TEST(SimColorPlan, ConstantGetMaskTopInputKeepsTheExtractedLaneWidth) {
  auto graph = make_fixed_top_input_lane_extract("fixed_top_input_lane");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  size_t lane_uses = 0;
  for (const auto& use : plan.value_uses()) {
    const auto& consumer = plan.version_sites()[use.consumer_version];
    if (livehd::graph_util::type_op_of(plan.sites()[consumer.base_site].node.base_node()) != Ntype_op::Get_mask
        || use.consumer_port != Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
      continue;
    }
    ++lane_uses;
    EXPECT_TRUE(use.top_input);
    EXPECT_TRUE(use.preextracted);
    EXPECT_EQ(use.width, 4u);
    EXPECT_EQ(use.consumer_width, 4u);
    EXPECT_EQ(use.producer_extract_lo, 4u);
    EXPECT_EQ(use.producer_extract_hi, 8u);
  }
  EXPECT_EQ(lane_uses, 2u) << plan.report();
}

TEST(SimColorPlan, DisjointOrPackDoesNotCreateAWordLevelFeedbackCycle) {
  auto graph = make_disjoint_or_pack_feedback("disjoint_or_pack");
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  ASSERT_TRUE(plan.summary().version_dag_acyclic) << plan.report();
  size_t lane_uses = 0;
  for (const auto& use : plan.value_uses()) {
    const auto& consumer = plan.version_sites()[use.consumer_version];
    if (gu::type_op_of(plan.sites()[consumer.base_site].node) != Ntype_op::Get_mask || use.consumer_port != 0) {
      continue;
    }
    ++lane_uses;
    EXPECT_TRUE(use.top_input);
    EXPECT_TRUE(use.preextracted);
    EXPECT_EQ(use.producer_extract_lo, 0u);
    EXPECT_EQ(use.producer_extract_hi, 1u);
  }
  EXPECT_EQ(lane_uses, 2u) << "the low field binds directly to its unique Or operand at both observation versions\n"
                           << plan.report();
}

TEST(SimColorPlan, StateActionsRemainSingletonColors) {
  auto fixture = make_flop_feeds_high_latch("singleton_state");
  auto plan    = livehd::sim::Color_plan::discover(fixture.graph.get());

  ASSERT_TRUE(plan.summary().color_dag_acyclic);
  std::vector<size_t> version_to_color(plan.version_sites().size(), plan.colors().size());
  for (size_t color_index = 0; color_index < plan.colors().size(); ++color_index) {
    for (const size_t member : plan.colors()[color_index].members) {
      ASSERT_LT(member, version_to_color.size());
      version_to_color[member] = color_index;
    }
  }
  for (size_t i = 0; i < plan.version_sites().size(); ++i) {
    const auto& site = plan.version_sites()[i];
    ASSERT_LT(version_to_color[i], plan.colors().size());
    const auto& color = plan.colors()[version_to_color[i]];
    EXPECT_EQ(site.slot, color.slot);
    if (site.role != livehd::sim::Color_plan::Version_role::data) {
      EXPECT_EQ(color.members.size(), 1u);
    }
  }
}

TEST(SimColorPlan, DataGatedLatchEnableIsAValueDependency) {
  auto fixture = make_data_gated_latch("enable_dependency");
  auto plan    = livehd::sim::Color_plan::discover(fixture.graph.get());

  ASSERT_TRUE(plan.summary().versioning_complete);
  size_t update = plan.version_sites().size();
  for (size_t i = 0; i < plan.version_sites().size(); ++i) {
    const auto& version = plan.version_sites()[i];
    if (version.role == livehd::sim::Color_plan::Version_role::state_update
        && plan.sites()[version.base_site].node.base_node() == fixture.latch) {
      update = i;
      break;
    }
  }
  ASSERT_LT(update, plan.version_sites().size());
  const auto enable_port  = Ntype::get_sink_pid(Ntype_op::Latch, "enable");
  const auto din_port     = Ntype::get_sink_pid(Ntype_op::Latch, "din");
  bool       reads_enable = false;
  bool       reads_din    = false;
  for (const auto& use : plan.value_uses()) {
    if (use.consumer_version != update) {
      continue;
    }
    reads_enable |= use.consumer_port == enable_port;
    reads_din    |= use.consumer_port == din_port;
  }
  EXPECT_TRUE(reads_enable) << "a changed latch enable must dirty and feed its update color";
  EXPECT_TRUE(reads_din);
}

TEST(SimColorPlan, NullRootIsAnExplicitIncompletePlan) {
  const auto plan = livehd::sim::Color_plan::discover(nullptr);
  EXPECT_FALSE(plan.complete());
  ASSERT_FALSE(plan.errors().empty());
  EXPECT_NE(plan.report().find("null simulation root"), std::string::npos);
}
