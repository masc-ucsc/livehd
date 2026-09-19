// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sim_color_plan.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "split_selfref.hpp"

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

Loop_fixture make_compact_loop(std::string_view tag, uint64_t count, bool surrounding_cones = false) {
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
  auto seed = parent->get_input_pin("seed");
  if (surrounding_cones) {
    auto before = gu::create_typed_node(*parent, Ntype_op::Not);
    seed.connect_sink(before.create_sink_pin(0));
    seed = before.create_driver_pin(0);
    gu::set_bits(seed, 8);
  }
  seed.connect_sink(compact.create_sink_pin(0));
  auto output = compact.create_driver_pin(1);
  gu::set_bits(output, 8);
  auto result = output;
  if (surrounding_cones) {
    auto after = gu::create_typed_node(*parent, Ntype_op::Not);
    result.connect_sink(after.create_sink_pin(0));
    result = after.create_driver_pin(0);
    gu::set_bits(result, 8);
  }
  result.connect_sink(parent->get_output_pin("result"));
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

// `unbounded_inner`: the fed-back field is itself a pack with one operand of
// unknown extent (a signed Sum), bounded only by the inner Or's own u4 width --
// the shape of XS Rob's deqPtr bundle, whose 141-bit inner pack sits at <<179.
std::shared_ptr<hhds::Graph> make_disjoint_or_pack_feedback(std::string_view tag, bool unbounded_inner = false) {
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
  auto packed    = packed_or.create_driver_pin(0);
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
  gu::create_const(*graph, *Dlop::create_integer(0xf)).connect_sink(feedback_masked.create_sink_pin(1));
  auto feedback = feedback_masked.create_driver_pin(0);
  gu::set_bits(feedback, 16);
  gu::set_unsign(feedback);
  auto feedback_pack = gu::create_typed_node(*graph, Ntype_op::Or);
  feedback.connect_sink(feedback_pack.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(feedback_pack.create_sink_pin(1));
  if (unbounded_inner) {
    auto sum = gu::create_typed_node(*graph, Ntype_op::Sum);
    feedback.connect_sink(gu::setup_sink_pid(sum, 0));
    gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(sum, 0));
    auto sum_out = sum.create_driver_pin(0);
    gu::set_sbits(sum_out, 4);
    sum_out.connect_sink(feedback_pack.create_sink_pin(2));
  }
  auto feedback_packed = feedback_pack.create_driver_pin(0);
  gu::set_bits(feedback_packed, unbounded_inner ? 4 : 16);
  gu::set_unsign(feedback_packed);
  feedback_packed.connect_sink(packed_or.create_sink_pin(1));
  return graph;
}

std::shared_ptr<hhds::Graph> make_cross_child_packed_feedback(std::string_view tag) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_") + std::string(tag));

  // One pure-comb child forwards the entire packed record. A second consumes
  // only bit 8 and produces only the low byte. Treating either call as one
  // word-valued node invents a loop; the real cones are high -> bit8 -> low.
  auto select_io = lib.create_io(std::string(tag) + "_select");
  select_io->add_input("record", 0);
  select_io->add_input("alternate", 1);
  select_io->add_input("choice", 2);
  select_io->add_output("selected", 3);
  select_io->set_bits("record", 16);
  select_io->set_bits("alternate", 16);
  select_io->set_bits("choice", 1);
  select_io->set_bits("selected", 16);
  auto select = select_io->create_graph();
  auto mux    = gu::create_typed_node(*select, Ntype_op::Mux);
  select->get_input_pin("choice").connect_sink(mux.create_sink_pin(0));
  select->get_input_pin("record").connect_sink(mux.create_sink_pin(1));
  select->get_input_pin("alternate").connect_sink(mux.create_sink_pin(2));
  auto mux_value = mux.create_driver_pin(0);
  gu::set_bits(mux_value, 16);
  gu::set_unsign(mux_value);
  auto masked = gu::create_typed_node(*select, Ntype_op::And);
  mux_value.connect_sink(masked.create_sink_pin(0));
  gu::create_const(*select, *Dlop::create_integer(0xffff)).connect_sink(masked.create_sink_pin(1));
  auto masked_value = masked.create_driver_pin(0);
  gu::set_bits(masked_value, 16);
  gu::set_unsign(masked_value);
  auto merge = gu::create_typed_node(*select, Ntype_op::Or);
  masked_value.connect_sink(merge.create_sink_pin(0));
  gu::create_const(*select, *Dlop::create_integer(0)).connect_sink(merge.create_sink_pin(1));
  auto selected = merge.create_driver_pin(0);
  gu::set_bits(selected, 16);
  gu::set_unsign(selected);
  selected.connect_sink(select->get_output_pin("selected"));

  auto low_io = lib.create_io(std::string(tag) + "_low");
  low_io->add_input("shift", 0);
  low_io->add_input("data", 1);
  low_io->add_output("low", 2);
  low_io->set_bits("shift", 1);
  low_io->set_bits("data", 8);
  low_io->set_bits("low", 8);
  auto low     = low_io->create_graph();
  auto low_xor = gu::create_typed_node(*low, Ntype_op::Xor);
  low->get_input_pin("shift").connect_sink(low_xor.create_sink_pin(0));
  low->get_input_pin("data").connect_sink(low_xor.create_sink_pin(1));
  auto low_value = low_xor.create_driver_pin(0);
  gu::set_bits(low_value, 8);
  gu::set_unsign(low_value);
  low_value.connect_sink(low->get_output_pin("low"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("high", 0);
  parent_io->add_input("data", 1);
  parent_io->add_input("choice", 2);
  parent_io->add_output("record", 3);
  parent_io->set_bits("high", 1);
  parent_io->set_bits("data", 8);
  parent_io->set_bits("choice", 1);
  parent_io->set_bits("record", 16);
  auto parent = parent_io->create_graph();

  auto packed_or = gu::create_typed_node(*parent, Ntype_op::Or);
  auto packed    = packed_or.create_driver_pin(0);
  gu::set_bits(packed, 16);
  gu::set_unsign(packed);

  auto select_call = gu::create_typed_node(*parent, Ntype_op::Sub);
  select_call.set_subnode(select_io);
  packed.connect_sink(select_call.create_sink_pin(0));
  parent->get_input_pin("choice").connect_sink(select_call.create_sink_pin(2));
  auto selected_record = select_call.create_driver_pin(3);
  gu::set_bits(selected_record, 16);
  gu::set_unsign(selected_record);

  auto high_read = gu::create_typed_node(*parent, Ntype_op::Get_mask);
  selected_record.connect_sink(gu::setup_sink_by_name(high_read, "a"));
  gu::create_const(*parent, *Dlop::create_integer(0x100)).connect_sink(gu::setup_sink_by_name(high_read, "mask"));
  auto shift = high_read.create_driver_pin(0);
  gu::set_bits(shift, 1);
  gu::set_unsign(shift);

  auto low_call = gu::create_typed_node(*parent, Ntype_op::Sub);
  low_call.set_subnode(low_io);
  shift.connect_sink(low_call.create_sink_pin(0));
  parent->get_input_pin("data").connect_sink(low_call.create_sink_pin(1));
  auto computed_low = low_call.create_driver_pin(2);
  gu::set_bits(computed_low, 8);
  gu::set_unsign(computed_low);
  computed_low.connect_sink(packed_or.create_sink_pin(1));

  auto high_shift = gu::create_typed_node(*parent, Ntype_op::SHL);
  parent->get_input_pin("high").connect_sink(high_shift.create_sink_pin(0));
  gu::create_const(*parent, *Dlop::create_integer(8)).connect_sink(high_shift.create_sink_pin(1));
  auto shifted_high = high_shift.create_driver_pin(0);
  gu::set_bits(shifted_high, 16);
  gu::set_unsign(shifted_high);
  shifted_high.connect_sink(packed_or.create_sink_pin(0));
  shifted_high.connect_sink(select_call.create_sink_pin(1));
  packed.connect_sink(parent->get_output_pin("record"));
  return parent;
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

// sim.tune fixture: the root's `lfsr` toggles every period (D = ~lfsr), while
// two instances of `idle_leaf` register `hold` -- a root input nothing moves --
// behind a `chain`-deep combinational cone. A chain of 32+ nodes makes the leaf
// a fence candidate, so fence ratio 0 and "none" plan different colors.
std::shared_ptr<hhds::Graph> make_lfsr_with_idle_children(std::string_view tag, size_t chain) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_color_plan_tune_") + std::string(tag));

  auto child_io = lib.create_io(std::string(tag) + "_idle_leaf");
  child_io->add_input("clk", 0);
  child_io->add_input("d", 1);
  child_io->add_output("q", 2);
  child_io->set_bits("clk", 1);
  child_io->set_bits("d", 8);
  child_io->set_bits("q", 8);
  auto child = child_io->create_graph();
  auto value = child->get_input_pin("d");
  for (size_t i = 0; i < chain; ++i) {
    auto inv = gu::create_typed_node(*child, Ntype_op::Not);
    value.connect_sink(inv.create_sink_pin(0));
    value = inv.create_driver_pin(0);
    gu::set_bits(value, 8);
  }
  auto held = gu::create_typed_node(*child, Ntype_op::Flop);
  held.set_name("held");
  child->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(held, "clock_pin"));
  value.connect_sink(gu::setup_sink_by_name(held, "din"));
  auto held_q = held.create_driver_pin(0);
  gu::set_bits(held_q, 8);
  held_q.connect_sink(child->get_output_pin("q"));

  auto io = lib.create_io(std::string(tag) + "_lfsr_top");
  io->add_input("clk", 0);
  io->add_input("hold", 1);
  io->add_output("lfsr_q", 2);
  io->add_output("idle_a", 3);
  io->add_output("idle_b", 4);
  io->set_bits("clk", 1);
  for (const auto name : {"hold", "lfsr_q", "idle_a", "idle_b"}) {
    io->set_bits(name, 8);
  }
  auto graph = io->create_graph();
  auto lfsr  = gu::create_typed_node(*graph, Ntype_op::Flop);
  lfsr.set_name("lfsr");
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(lfsr, "clock_pin"));
  auto lfsr_q = lfsr.create_driver_pin(0);
  gu::set_bits(lfsr_q, 8);
  auto next = gu::create_typed_node(*graph, Ntype_op::Not);
  lfsr_q.connect_sink(next.create_sink_pin(0));
  auto next_value = next.create_driver_pin(0);
  gu::set_bits(next_value, 8);
  next_value.connect_sink(gu::setup_sink_by_name(lfsr, "din"));
  lfsr_q.connect_sink(graph->get_output_pin("lfsr_q"));
  for (const auto name : {"idle_a", "idle_b"}) {
    auto call = gu::create_typed_node(*graph, Ntype_op::Sub);
    call.set_subnode(child_io);
    call.set_name(name);
    graph->get_input_pin("clk").connect_sink(call.create_sink_pin(0));
    graph->get_input_pin("hold").connect_sink(call.create_sink_pin(1));
    call.create_driver_pin(2).connect_sink(graph->get_output_pin(name));
  }
  return graph;
}

// Everything support() and occurrences() publish, as text: two plans have the
// same tables iff these strings are equal.
std::string tune_tables_text(const livehd::sim::Color_plan& plan) {
  const auto& support  = plan.support();
  std::string text     = std::format("available={} exact={} words={} total-ge={} total-sites={} total-cost={} total-cost-flat={}\n",
                                     support.available,
                                     support.exact,
                                     support.words,
                                     support.total_ge,
                                     support.total_sites,
                                     support.total_cost,
                                     support.total_cost_flat);
  text                += std::format("fold classes={} members={} ge={} sites={} cost={} cost-flat={}\n",
                                     support.fold_classes,
                                     support.fold_members,
                                     support.fold_ge,
                                     support.fold_sites,
                                     support.fold_cost,
                                     support.fold_cost_flat);
  for (const auto& source : support.sources) {
    const auto site
        = source.site == livehd::sim::Color_plan::invalid_index ? std::string("-") : plan.sites()[source.site].storage_id;
    text += std::format("source kind={} site={} port={} bucket={} occ={}\n",
                        static_cast<int>(source.kind),
                        site,
                        source.port,
                        source.bucket,
                        source.occurrence);
  }
  for (size_t c = 0; c < support.classes.size(); ++c) {
    text += std::format("class ge={} sites={} cost={} cost-flat={} occ={} bits=",
                        support.classes[c].ge,
                        support.classes[c].sites,
                        support.classes[c].cost,
                        support.classes[c].cost_flat,
                        support.classes[c].occurrence);
    for (uint32_t w = 0; w < support.words; ++w) {
      text += std::format("{:016x}", support.class_bits[c * support.words + w]);
    }
    text += "\n";
  }
  for (const auto& occurrence : plan.occurrences()) {
    text += std::format("occurrence path={} def={} ge={} cost={} cost-flat={} sites={} state={} sources={}\n",
                        occurrence.path,
                        occurrence.def,
                        occurrence.ge,
                        occurrence.cost,
                        occurrence.cost_flat,
                        occurrence.sites,
                        occurrence.state_sites,
                        occurrence.sources);
  }
  return text;
}

// An executable site: live, with at least one version (the sites support()
// classes and occurrences() weigh).
std::vector<bool> executable_sites(const livehd::sim::Color_plan& plan) {
  std::vector<bool> executable(plan.sites().size(), false);
  for (const auto& version : plan.version_sites()) {
    executable[version.base_site] = plan.sites()[version.base_site].live;
  }
  return executable;
}

// Every support weight column sums to its published total, and the occurrences
// carry the same ge / cost / cost_flat as the classes (the fold class included).
void expect_tune_weight_sums(const livehd::sim::Color_plan& plan) {
  const auto& support = plan.support();
  uint64_t    ge = 0, sites = 0, cost = 0, cost_flat = 0;
  for (const auto& cls : support.classes) {
    ge        += cls.ge;
    sites     += cls.sites;
    cost      += cls.cost;
    cost_flat += cls.cost_flat;
    EXPECT_GE(cls.cost, cls.sites) << "every site costs at least one word";
    EXPECT_GE(cls.cost_flat, cls.sites) << "every site costs at least one word";
  }
  EXPECT_EQ(ge, support.total_ge);
  EXPECT_EQ(sites, support.total_sites);
  EXPECT_EQ(cost, support.total_cost);
  EXPECT_EQ(cost_flat, support.total_cost_flat);
  uint64_t occ_ge = 0, occ_sites = 0, occ_cost = 0, occ_cost_flat = 0;
  for (const auto& occurrence : plan.occurrences()) {
    occ_ge        += occurrence.ge;
    occ_sites     += occurrence.sites;
    occ_cost      += occurrence.cost;
    occ_cost_flat += occurrence.cost_flat;
  }
  EXPECT_EQ(occ_ge, support.total_ge);
  EXPECT_EQ(occ_sites, support.total_sites);
  EXPECT_EQ(occ_cost, support.total_cost);
  EXPECT_EQ(occ_cost_flat, support.total_cost_flat);
  const auto executable = executable_sites(plan);
  EXPECT_EQ(static_cast<uint64_t>(std::ranges::count(executable, true)), support.total_sites)
      << "every executable site is in exactly one class";
}

size_t occurrence_named(const livehd::sim::Color_plan& plan, std::string_view path) {
  for (size_t i = 0; i < plan.occurrences().size(); ++i) {
    if (plan.occurrences()[i].path == path) {
      return i;
    }
  }
  return livehd::sim::Color_plan::invalid_index;
}

bool class_reads(const livehd::sim::Color_plan& plan, size_t cls, uint32_t bucket) {
  const auto& support = plan.support();
  return (support.class_bits[cls * support.words + bucket / 64] >> (bucket % 64)) & 1U;
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

TEST(SimColorPlan, LoopSharesColorWithSurroundingLogicWithoutExpanding) {
  auto fixture = make_compact_loop("loop_with_cones", 1'000'000'000ULL, true);
  for (bool llvm_runtime_calls : {false, true}) {
    const auto plan = livehd::sim::Color_plan::discover(fixture.parent.get(), true, llvm_runtime_calls);
    ASSERT_TRUE(plan.complete()) << plan.report();
    EXPECT_EQ(plan.summary().grouped_sites, 3u);
    EXPECT_EQ(plan.summary().compact_loops, 1u);
    EXPECT_TRUE(std::ranges::any_of(plan.colors(), [&](const auto& color) {
      return color.members.size() > 1 && std::ranges::any_of(color.members, [&](size_t member) {
               return plan.sites()[plan.version_sites()[member].base_site].kind == livehd::sim::Color_plan::Site_kind::loop_control;
             });
    })) << plan.report();
  }
}

TEST(SimColorPlan, PrivateRepairSplitsPackedFeedbackAcrossPureCombChildren) {
  auto                                  graph = make_cross_child_packed_feedback("cross_child_packed_feedback");
  absl::flat_hash_set<hhds::Node_class> before;
  gu::word_level_cycle_nodes(graph.get(), /*strict=*/true, before);
  EXPECT_FALSE(before.empty());

  EXPECT_GT(gu::repair_simulator_packed_cycles(graph.get()), 0);
  auto after = livehd::sim::Color_plan::discover(graph.get());
  EXPECT_TRUE(after.complete()) << (after.errors().empty() ? "" : after.errors().front());
  EXPECT_TRUE(after.validate_retained_handles());

  absl::flat_hash_set<hhds::Node_class> residual;
  gu::word_level_cycle_nodes(graph.get(), /*strict=*/false, residual);
  EXPECT_TRUE(residual.empty());
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

TEST(SimColorPlan, PreserveSubstantialLeafModulesButFuseTinyHelpers) {
  for (const size_t length : {1u, 64u}) {
    auto& lib      = livehd::Hhds_graph_library::instance("lgdb_color_leaf_" + std::to_string(length));
    auto  child_io = lib.create_io("leaf");
    child_io->add_input("x", 0);
    child_io->add_output("y", 1);
    child_io->set_bits("x", 8);
    child_io->set_bits("y", 8);
    auto child = child_io->create_graph();
    auto value = child->get_input_pin("x");
    for (size_t i = 0; i < length; ++i) {
      auto node = gu::create_typed_node(*child, Ntype_op::Not);
      value.connect_sink(node.create_sink_pin(0));
      value = node.create_driver_pin(0);
      gu::set_bits(value, 8);
    }
    value.connect_sink(child->get_output_pin("y"));
    auto io = lib.create_io("parent");
    io->add_input("a", 0);
    io->add_input("b", 1);
    io->add_output("x", 2);
    io->add_output("y", 3);
    for (const auto name : {"a", "b", "x", "y"}) {
      io->set_bits(name, 8);
    }
    auto graph = io->create_graph();
    for (size_t i = 0; i < 2; ++i) {
      auto call = gu::create_typed_node(*graph, Ntype_op::Sub);
      call.set_subnode(child_io);
      graph->get_input_pin(i == 0 ? "a" : "b").connect_sink(call.create_sink_pin(0));
      call.create_driver_pin(1).connect_sink(graph->get_output_pin(i == 0 ? "x" : "y"));
    }
    auto plan = livehd::sim::Color_plan::discover(graph.get(), false);
    ASSERT_TRUE(plan.complete()) << plan.report();
    if (length == 1) {
      EXPECT_EQ(plan.colors().size(), 2u);
    } else {
      EXPECT_EQ(plan.colors().size(), 4u);
      EXPECT_GT(plan.summary().kernel_reuses, 0u);
      for (const auto& color : plan.colors()) {
        ASSERT_FALSE(color.members.empty());
        const auto& path = plan.sites()[plan.version_sites()[color.members.front()].base_site].node.path();
        for (const auto member : color.members) {
          EXPECT_EQ(plan.sites()[plan.version_sites()[member].base_site].node.path(), path);
        }
      }
    }
  }
}

TEST(SimColorPlan, ReportIgnoresConstructionOrderGraphNamesAndNodeNames) {
  auto forward = make_parallel("ordered", false);
  auto reverse = make_parallel("reversed", true);

  const auto a = livehd::sim::Color_plan::discover(forward.get());
  const auto b = livehd::sim::Color_plan::discover(reverse.get());
  ASSERT_TRUE(a.complete());
  ASSERT_TRUE(b.complete());
  // The trailing occurrence map is a names-allowed section, like the
  // observation map: it names the definition (`ordered_parallel` vs
  // `reversed_parallel` here). Everything before it must still be identical.
  const auto schedule = [](const std::string& report) { return report.substr(0, report.find("occurrence-map begin")); };
  EXPECT_EQ(schedule(a.report()), schedule(b.report()));
  EXPECT_NE(a.report().find("occurrence path=\"\" def=\"ordered_parallel\""), std::string::npos) << a.report();
  EXPECT_EQ(a.summary().colors, 2u) << "small independent cones share one color per phase";
  EXPECT_EQ(a.summary().kernel_classes, 2u);
  EXPECT_EQ(a.summary().kernel_reuses, 0u);
  for (const auto& kernel : a.kernel_classes()) {
    EXPECT_EQ(kernel.colors.size(), 1u);
  }
}

TEST(SimColorPlan, StructuralHashPreservesPortRolesAndOperandMultiplicity) {
  const auto sum_shape = [](std::string_view tag, bool reverse, bool subtract, bool repeat) {
    auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_shape_multiset_") + std::string(tag));
    auto  io  = lib.create_io("top");
    io->add_input("a", 0);
    io->add_input("b", 1);
    io->add_input("c", 2);
    io->add_output("y", 3);
    for (auto name : {"a", "b", "c", "y"}) {
      io->set_bits(name, 8);
      io->set_unsign(name, true);
    }
    auto       graph   = io->create_graph();
    auto       sum     = gu::create_typed_node(*graph, Ntype_op::Sum, 8);
    const auto a       = graph->get_input_pin("a");
    const auto b       = gu::create_const(*graph, *Dlop::create_integer(5));
    const auto first   = reverse ? b : a;
    const auto second  = reverse ? a : b;
    const auto connect = [&](const hhds::Pin_class& pin) { pin.connect_sink(sum.create_sink_pin(subtract && pin == b ? 1 : 0)); };
    connect(first);
    connect(second);
    if (repeat) {
      // Two graph inputs have the same discovery shape, but both edges count.
      graph->get_input_pin("c").connect_sink(sum.create_sink_pin(0));
    }
    sum.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
    const auto plan = livehd::sim::Color_plan::discover(graph.get());
    EXPECT_TRUE(plan.complete()) << plan.report();
    for (const auto& site : plan.sites()) {
      if (gu::type_op_of(site.node) == Ntype_op::Sum) {
        return site.structural_id;
      }
    }
    ADD_FAILURE() << "sum missing from the discovery plan";
    return std::string{};
  };
  const auto base = sum_shape("base", false, false, false);
  EXPECT_EQ(base, sum_shape("reverse", true, false, false));
  EXPECT_NE(base, sum_shape("subtract", false, true, false));
  EXPECT_NE(base, sum_shape("repeat", false, false, true));
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
  // A rise-only design fuses the flop capture into its producer's color, so
  // the same narrowing may be an internal value use instead of a slot; the
  // emitter applies the identical truncation to either.
  for (const auto& use : plan.value_uses()) {
    if (use.width == 65 && use.consumer_width == 64) {
      found_narrowing_boundary = true;
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

// An inner pack with one unbounded operand is still bounded by its own
// unsigned width, so the read at bit 8 keeps a unique owner. Bailing there
// made the whole word the read's producer and left a false feedback cycle.
TEST(SimColorPlan, NestedPackWithUnboundedOperandIsBoundedByItsWidth) {
  auto graph = make_disjoint_or_pack_feedback("nested_unbounded_pack", true);
  auto plan  = livehd::sim::Color_plan::discover(graph.get());

  ASSERT_TRUE(plan.complete()) << plan.report();
  EXPECT_TRUE(plan.summary().version_dag_acyclic) << plan.report();
}

TEST(SimColorPlan, StateActionsMergeWithinTheirExecutionSlot) {
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
    for (const auto member : color.members) {
      EXPECT_EQ(plan.version_sites()[member].slot, site.slot);
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

TEST(SimColorPlan, LiveWordBudgetBoundsFanoutWithoutSplittingLowPressureChains) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_pressure");
  auto  io  = lib.create_io("pressure");
  for (unsigned i = 0; i < 64; ++i) {
    io->add_input("in" + std::to_string(i), i);
    io->add_output("out" + std::to_string(i), 64 + i);
    io->set_bits("in" + std::to_string(i), 64);
    io->set_bits("out" + std::to_string(i), 64);
  }
  auto graph = io->create_graph();
  for (unsigned i = 0; i < 64; ++i) {
    auto inv = gu::create_typed_node(*graph, Ntype_op::Not);
    graph->get_input_pin("in" + std::to_string(i)).connect_sink(inv.create_sink_pin(0));
    inv.create_driver_pin(0).connect_sink(graph->get_output_pin("out" + std::to_string(i)));
  }
  // The coarsener under an explicit 20-word budget (the default is wider;
  // `sim.tune.live_words` selects it per run): 64 independent 64-bit inversions
  // cannot share one color, a 128-deep chain still can.
  const auto plan = livehd::sim::Color_plan::discover(graph.get(), true, false, 20);
  ASSERT_TRUE(plan.complete()) << plan.report();
  EXPECT_GT(plan.colors().size(), 2u);
  size_t members = 0;
  for (const auto& color : plan.colors()) {
    EXPECT_LE(color.peak_live_words, 20u);
    members += color.members.size();
  }
  EXPECT_EQ(members, plan.version_sites().size());

  auto       chain      = make_combinational_chain("pressure_chain", 128);
  const auto chain_plan = livehd::sim::Color_plan::discover(chain.get(), true, false, 20);
  ASSERT_TRUE(chain_plan.complete());
  EXPECT_EQ(chain_plan.colors().size(), 2u) << "long chains need few simultaneously live values";
  for (const auto& color : chain_plan.colors()) {
    EXPECT_LE(color.peak_live_words, 20u);
  }
}

TEST(SimColorPlan, IndivisibleWideValuesRemainSingletons) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_wide_pressure");
  auto  io  = lib.create_io("wide_pressure");
  io->add_input("a", 0);
  io->add_output("b", 1);
  io->set_bits("a", 2048);
  io->set_bits("b", 2048);
  auto graph = io->create_graph();
  auto inv   = gu::create_typed_node(*graph, Ntype_op::Not);
  graph->get_input_pin("a").connect_sink(inv.create_sink_pin(0));
  inv.create_driver_pin(0).connect_sink(graph->get_output_pin("b"));
  const auto plan = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.report();
  for (const auto& color : plan.colors()) {
    EXPECT_EQ(color.members.size(), 1u);
    EXPECT_GT(color.peak_live_words, 20u);
  }
}

TEST(SimColorPlan, TuneSupportIsInvariantAcrossTuneKnobs) {
  auto graph = make_lfsr_with_idle_children("invariant", 40);

  const auto baseline = livehd::sim::Color_plan::discover(graph.get(), true, false, 256, livehd::sim::Color_plan::kNoFences);
  ASSERT_TRUE(baseline.complete()) << baseline.report();
  ASSERT_TRUE(baseline.support().available);
  ASSERT_FALSE(baseline.support().sources.empty());
  const auto expected = tune_tables_text(baseline);

  std::set<size_t> color_counts;
  for (const uint64_t live_words : {1U, 20U, 256U}) {
    for (const int64_t fence : {livehd::sim::Color_plan::kNoFences, int64_t{16}, int64_t{0}}) {
      for (const bool runtime_calls : {false, true}) {
        const auto plan = livehd::sim::Color_plan::discover(graph.get(), true, runtime_calls, live_words, fence);
        ASSERT_TRUE(plan.complete()) << plan.report();
        EXPECT_EQ(tune_tables_text(plan), expected) << "live_words=" << live_words << " fence=" << fence;
        color_counts.insert(plan.colors().size());
      }
    }
  }
  EXPECT_GT(color_counts.size(), 1u) << "the knobs must actually move the schedule, or this test proves nothing";
}

TEST(SimColorPlan, TuneSupportIdleChildNeverReadsTheLfsr) {
  auto       graph = make_lfsr_with_idle_children("lfsr", 4);
  const auto plan  = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.report();
  const auto& support = plan.support();
  ASSERT_TRUE(support.available);
  EXPECT_TRUE(support.exact);

  const size_t root   = occurrence_named(plan, "");
  const size_t idle_a = occurrence_named(plan, "idle_a");
  const size_t idle_b = occurrence_named(plan, "idle_b");
  ASSERT_NE(root, livehd::sim::Color_plan::invalid_index) << plan.report();
  ASSERT_NE(idle_a, livehd::sim::Color_plan::invalid_index) << plan.report();
  ASSERT_NE(idle_b, livehd::sim::Color_plan::invalid_index) << plan.report();
  EXPECT_EQ(plan.occurrences()[idle_a].def, "lfsr_idle_leaf");
  EXPECT_EQ(plan.occurrences()[root].def, "lfsr_lfsr_top");

  // Sources: the three flops (lfsr, two `held`), then the root inputs in port
  // order.
  uint32_t lfsr_bucket = UINT32_MAX;
  uint32_t hold_bucket = UINT32_MAX;
  size_t   state       = 0;
  bool     inputs_last = true;
  bool     seen_input  = false;
  for (const auto& source : support.sources) {
    using Kind = livehd::sim::Color_plan::Support_source::Kind;
    if (source.kind == Kind::top_input) {
      seen_input = true;
      EXPECT_EQ(source.occurrence, plan.occurrences().size());
      if (source.port == 1) {
        hold_bucket = source.bucket;
      }
      continue;
    }
    inputs_last &= !seen_input;
    ASSERT_EQ(source.kind, Kind::state);
    ++state;
    if (source.occurrence == root) {
      lfsr_bucket = source.bucket;
    }
  }
  EXPECT_TRUE(inputs_last);
  EXPECT_EQ(state, 3u);
  EXPECT_EQ(support.sources.size(), 5u) << "3 flops + clk + hold";
  ASSERT_NE(lfsr_bucket, UINT32_MAX);
  ASSERT_NE(hold_bucket, UINT32_MAX);
  EXPECT_EQ(plan.occurrences()[root].sources, 1u);
  EXPECT_EQ(plan.occurrences()[idle_a].sources, 1u);

  bool root_reads_lfsr = false;
  bool idle_reads_hold = false;
  for (size_t c = 0; c < support.classes.size(); ++c) {
    const auto occurrence = support.classes[c].occurrence;
    if (occurrence == idle_a || occurrence == idle_b) {
      EXPECT_FALSE(class_reads(plan, c, lfsr_bucket)) << "an idle child's work never depends on the LFSR";
      idle_reads_hold |= class_reads(plan, c, hold_bucket);
    }
    if (occurrence == root) {
      root_reads_lfsr |= class_reads(plan, c, lfsr_bucket);
      EXPECT_FALSE(class_reads(plan, c, hold_bucket)) << "the LFSR cone never reads `hold`";
    }
  }
  EXPECT_TRUE(root_reads_lfsr) << "the LFSR's next state reads its own Q";
  EXPECT_TRUE(idle_reads_hold) << "the leaf's register loads `hold`";
  EXPECT_NE(plan.report().find("support words=1 sources=5 exact=true"), std::string::npos) << plan.report();
  EXPECT_NE(plan.report().find("occurrence path=\"idle_a\" def=\"lfsr_idle_leaf\""), std::string::npos) << plan.report();
}

TEST(SimColorPlan, TuneOccurrencesSumTheirSitesGe) {
  auto       graph = make_lfsr_with_idle_children("occurrence_ge", 6);
  const auto plan  = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.report();

  // An executable site: live, with at least one version.
  std::vector<bool> executable(plan.sites().size(), false);
  for (const auto& version : plan.version_sites()) {
    executable[version.base_site] = plan.sites()[version.base_site].live;
  }
  uint64_t root_ge  = 0;
  uint64_t child_ge = 0;
  for (size_t s = 0; s < plan.sites().size(); ++s) {
    if (!executable[s]) {
      continue;
    }
    (plan.sites()[s].node.path().steps().empty() ? root_ge : child_ge) += plan.sites()[s].gate_equivalents;
  }
  ASSERT_GT(root_ge, 0u);
  ASSERT_GT(child_ge, 0u);

  uint64_t occurrence_total = 0;
  for (const auto& occurrence : plan.occurrences()) {
    occurrence_total += occurrence.ge;
  }
  EXPECT_EQ(plan.occurrences().size(), 3u);
  EXPECT_EQ(plan.occurrences()[occurrence_named(plan, "")].ge, root_ge);
  EXPECT_EQ(plan.occurrences()[occurrence_named(plan, "idle_a")].ge + plan.occurrences()[occurrence_named(plan, "idle_b")].ge,
            child_ge);
  EXPECT_EQ(plan.occurrences()[occurrence_named(plan, "idle_a")].ge, plan.occurrences()[occurrence_named(plan, "idle_b")].ge);
  EXPECT_EQ(plan.occurrences()[occurrence_named(plan, "idle_a")].state_sites, 1u);
  EXPECT_EQ(occurrence_total, root_ge + child_ge);

  uint64_t class_total = 0;
  for (const auto& cls : plan.support().classes) {
    class_total += cls.ge;
  }
  EXPECT_EQ(class_total, occurrence_total);
  EXPECT_EQ(plan.support().total_ge, occurrence_total);

  // Every site of this fixture is at most 8 bits wide and there is no loop or
  // opaque call: one word per executable site under both cost weights.
  expect_tune_weight_sums(plan);
  EXPECT_EQ(plan.support().total_cost, plan.support().total_sites);
  EXPECT_EQ(plan.support().total_cost_flat, plan.support().total_sites);
}

TEST(SimColorPlan, TuneCompactLoopWeighsBodyTimesLanesAndIsOpaque) {
  constexpr uint64_t lanes   = 7;
  auto               fixture = make_compact_loop("tune_loop", lanes, true);
  const auto         plan    = livehd::sim::Color_plan::discover(fixture.parent.get());
  ASSERT_TRUE(plan.complete()) << plan.report();

  uint64_t body_ge = 0;
  for (const auto node : fixture.compact.get_subnode_graph()->body().nodes()) {
    body_ge += gu::mappable_ge_weight(node);
  }
  ASSERT_GT(body_ge, 0u);
  uint64_t expected = 0;
  size_t   loops    = 0;
  for (const auto& site : plan.sites()) {
    if (!site.live) {
      continue;
    }
    if (site.kind == livehd::sim::Color_plan::Site_kind::loop_control) {
      expected += body_ge * lanes;
      ++loops;
    } else if (site.kind == livehd::sim::Color_plan::Site_kind::data) {
      expected += site.gate_equivalents;
    }
  }
  ASSERT_EQ(loops, 1u);
  ASSERT_EQ(plan.occurrences().size(), 1u) << "the loop runs in the caller's body";
  EXPECT_EQ(plan.occurrences().front().path, "");
  EXPECT_EQ(plan.occurrences().front().ge, expected);
  EXPECT_EQ(plan.support().total_ge, expected);

  // cost: the body's words (one per 8-bit node) x lanes, plus one word per
  // surrounding 8-bit site; cost_flat: the loop site is its own one word.
  uint64_t body_words = 0;
  for (const auto node : fixture.compact.get_subnode_graph()->body().nodes()) {
    body_words += gu::is_builtin_node(node) ? 0 : 1;
  }
  ASSERT_GT(body_words, 0u);
  const auto     executable = executable_sites(plan);
  const uint64_t data_sites = std::ranges::count_if(std::views::iota(size_t{0}, plan.sites().size()), [&](size_t s) {
    return executable[s] && plan.sites()[s].kind == livehd::sim::Color_plan::Site_kind::data;
  });
  EXPECT_EQ(plan.support().total_cost, body_words * lanes + data_sites);
  EXPECT_EQ(plan.support().total_cost_flat, 1 + data_sites);
  EXPECT_EQ(plan.occurrences().front().cost, plan.support().total_cost);
  expect_tune_weight_sums(plan);

  const auto& sources = plan.support().sources;
  ASSERT_EQ(sources.size(), 2u) << "the opaque loop, then the `seed` input";
  EXPECT_EQ(sources[0].kind, livehd::sim::Color_plan::Support_source::Kind::opaque);
  EXPECT_EQ(plan.sites()[sources[0].site].kind, livehd::sim::Color_plan::Site_kind::loop_control);
  EXPECT_EQ(sources[1].kind, livehd::sim::Color_plan::Support_source::Kind::top_input);
}

TEST(SimColorPlan, TuneSupportBucketsContiguouslyPastTheMaskWidth) {
  // 1100 independent registers + one input: more sources than 16 words hold,
  // so neighbouring sources share a bucket (contiguous, monotone, in range).
  constexpr size_t flops = 1100;
  auto&            lib   = livehd::Hhds_graph_library::instance("lgdb_color_plan_tune_wide");
  auto             io    = lib.create_io("tune_wide");
  io->add_input("clk", 0);
  for (size_t i = 0; i < flops; ++i) {
    io->add_output("q" + std::to_string(i), static_cast<hhds::Port_id>(1 + i));
    io->set_bits("q" + std::to_string(i), 1);
  }
  io->set_bits("clk", 1);
  auto graph = io->create_graph();
  for (size_t i = 0; i < flops; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    auto q = flop.create_driver_pin(0);
    gu::set_bits(q, 1);
    auto inv = gu::create_typed_node(*graph, Ntype_op::Not);
    q.connect_sink(inv.create_sink_pin(0));
    auto d = inv.create_driver_pin(0);
    gu::set_bits(d, 1);
    d.connect_sink(gu::setup_sink_by_name(flop, "din"));
    q.connect_sink(graph->get_output_pin("q" + std::to_string(i)));
  }
  const auto plan = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.errors().front();
  const auto& support = plan.support();
  ASSERT_TRUE(support.available);
  EXPECT_EQ(support.words, livehd::sim::Color_plan::kMaxSupportWords);
  EXPECT_FALSE(support.exact);
  ASSERT_EQ(support.sources.size(), flops + 1);
  uint32_t previous = 0;
  for (const auto& source : support.sources) {
    EXPECT_GE(source.bucket, previous);
    EXPECT_LT(source.bucket, 64 * support.words);
    previous = source.bucket;
  }
  EXPECT_EQ(support.sources.back().bucket, 64 * support.words - 1);
  EXPECT_LE(support.classes.size(), livehd::sim::Color_plan::kMaxSupportClasses);
  EXPECT_EQ(support.class_bits.size(), support.classes.size() * support.words);
}

TEST(SimColorPlan, TuneNestedCompactLoopBelongsToItsCallerBody) {
  // The loop fixture's parent, instantiated as `u_loops` under a new root: the
  // loop runs in `u_loops`'s body, one step up from the loop's callee path.
  auto  fixture = make_compact_loop("tune_nested_loop", 5, true);
  auto& lib     = livehd::Hhds_graph_library::instance("lgdb_color_plan_tune_nested_loop");
  auto  io      = lib.create_io("tune_nested_loop_top");
  io->add_input("seed", 0);
  io->add_output("result", 1);
  io->set_bits("seed", 8);
  io->set_bits("result", 8);
  auto root = io->create_graph();
  auto call = gu::create_typed_node(*root, Ntype_op::Sub);
  call.set_subnode(fixture.parent->get_io());
  call.set_name("u_loops");
  root->get_input_pin("seed").connect_sink(call.create_sink_pin(0));
  call.create_driver_pin(1).connect_sink(root->get_output_pin("result"));

  const auto plan = livehd::sim::Color_plan::discover(root.get());
  ASSERT_TRUE(plan.complete()) << plan.report();
  const size_t caller = occurrence_named(plan, "u_loops");
  ASSERT_NE(caller, livehd::sim::Color_plan::invalid_index) << plan.report();
  EXPECT_EQ(plan.occurrences()[caller].def, "tune_nested_loop_parent");
  EXPECT_EQ(plan.occurrences().size(), 2u) << plan.report();
  size_t opaque = 0;
  for (const auto& source : plan.support().sources) {
    if (source.kind == livehd::sim::Color_plan::Support_source::Kind::opaque) {
      EXPECT_EQ(source.occurrence, caller);
      ++opaque;
    }
  }
  EXPECT_EQ(opaque, 1u);
}

TEST(SimColorPlan, TuneWiringSitesAreClassedAndCostWholeWords) {
  // in(130) -> Not(130) -> Get_mask(lane 4) -> out. The Not is 3 words and 130
  // GE; the Get_mask is pure wiring -- 0 GE, but emitted code: one word, and it
  // must be in a class like every other executable site.
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_plan_tune_wiring");
  auto  io  = lib.create_io("tune_wiring");
  io->add_input("in", 0);
  io->add_output("out", 1);
  io->set_bits("in", 130);
  io->set_bits("out", 4);
  io->set_unsign("in", true);
  io->set_unsign("out", true);
  auto graph = io->create_graph();
  auto wide  = gu::create_typed_node(*graph, Ntype_op::Not);
  graph->get_input_pin("in").connect_sink(wide.create_sink_pin(0));
  auto packed = wide.create_driver_pin(0);
  gu::set_bits(packed, 130);
  gu::set_unsign(packed);
  auto get_mask = gu::create_typed_node(*graph, Ntype_op::Get_mask);
  packed.connect_sink(gu::setup_sink_by_name(get_mask, "a"));
  gu::create_const(*graph, *Dlop::create_integer(0xf0)).connect_sink(gu::setup_sink_by_name(get_mask, "mask"));
  auto lane = get_mask.create_driver_pin(0);
  gu::set_bits(lane, 4);
  gu::set_unsign(lane);
  lane.connect_sink(graph->get_output_pin("out"));

  const auto plan = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.report();
  const auto& support = plan.support();
  ASSERT_TRUE(support.available);
  const auto executable = executable_sites(plan);
  size_t     wiring     = livehd::sim::Color_plan::invalid_index;
  for (size_t s = 0; s < plan.sites().size(); ++s) {
    if (executable[s] && gu::type_op_of(plan.sites()[s].node) == Ntype_op::Get_mask) {
      wiring = s;
    }
  }
  ASSERT_NE(wiring, livehd::sim::Color_plan::invalid_index) << plan.report();
  EXPECT_EQ(plan.sites()[wiring].gate_equivalents, 0u);
  EXPECT_EQ(support.total_sites, 2u) << "the zero-GE Get_mask joins a class";
  EXPECT_EQ(support.total_ge, 130u);
  EXPECT_EQ(support.total_cost, 3u + 1u) << "ceil(130/64) words for the Not, one for the wiring";
  EXPECT_EQ(support.total_cost_flat, support.total_cost);
  expect_tune_weight_sums(plan);
  EXPECT_NE(plan.report().find("total-ge=130 total-sites=2 total-cost=4 total-cost-flat=4"), std::string::npos) << plan.report();
}

TEST(SimColorPlan, TuneMemoryCostsItsDataWidthWords) {
  // A 130-bit-wide memory read through an 8-bit port stamp: the word cost is
  // the storage's DATA width (3 words), not the port's stamp.
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_plan_tune_memory");
  auto  io  = lib.create_io("tune_memory");
  io->add_input("clk", 0);
  io->add_output("q", 1);
  io->set_bits("clk", 1);
  io->set_bits("q", 8);
  auto graph  = io->create_graph();
  auto memory = gu::create_typed_node(*graph, Ntype_op::Memory);
  memory.set_name("tune_mem");
  gu::create_const(*graph, *Dlop::create_integer(130)).connect_sink(gu::setup_sink_by_name(memory, "bits"));
  gu::create_const(*graph, *Dlop::create_integer(4)).connect_sink(gu::setup_sink_by_name(memory, "size"));
  graph->get_input_pin("clk").connect_sink(memory.create_sink_pin(Ntype::Memory_port_stride + 2));
  auto q = memory.create_driver_pin(0);
  gu::set_bits(q, 8);
  q.connect_sink(graph->get_output_pin("q"));

  const auto plan = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.support().available) << plan.report();
  const auto executable = executable_sites(plan);
  size_t     memories   = 0;
  for (size_t s = 0; s < plan.sites().size(); ++s) {
    memories += executable[s] && gu::type_op_of(plan.sites()[s].node) == Ntype_op::Memory;
  }
  ASSERT_EQ(memories, 1u) << plan.report();
  EXPECT_EQ(plan.support().total_sites, 1u) << plan.report();
  EXPECT_EQ(plan.support().total_cost, 3u) << plan.report();
  expect_tune_weight_sums(plan);
}

// The (bits, occurrence) classes of a design past kMaxSupportClasses with ONE
// distinct row per class: 100 independent flops and one node per flop PAIR.
// Every pair node is a 1-bit And (1 GE, 1 word) except pair (3,7): a 256-bit
// Set_mask -- pure wiring, 0 GE but 4 words.
std::shared_ptr<hhds::Graph> make_pairwise_class_overflow(std::string_view name, size_t flops) {
  auto&      lib   = livehd::Hhds_graph_library::instance(std::format("lgdb_color_plan_{}", name));
  auto       io    = lib.create_io(std::string(name));
  const auto heavy = [](size_t i, size_t j) { return i == 3 && j == 7; };
  io->add_input("clk", 0);
  io->set_bits("clk", 1);
  hhds::Port_id port = 1;
  for (size_t i = 0; i < flops; ++i) {
    for (size_t j = i + 1; j < flops; ++j) {
      io->add_output(std::format("p{}_{}", i, j), port++);
      io->set_bits(std::format("p{}_{}", i, j), heavy(i, j) ? 256 : 1);
    }
  }
  auto                         graph = io->create_graph();
  std::vector<hhds::Pin_class> q(flops);
  for (size_t i = 0; i < flops; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    q[i] = flop.create_driver_pin(0);
    gu::set_bits(q[i], 1);
    auto inv = gu::create_typed_node(*graph, Ntype_op::Not);
    q[i].connect_sink(inv.create_sink_pin(0));
    auto d = inv.create_driver_pin(0);
    gu::set_bits(d, 1);
    d.connect_sink(gu::setup_sink_by_name(flop, "din"));
  }
  for (size_t i = 0; i < flops; ++i) {
    for (size_t j = i + 1; j < flops; ++j) {
      hhds::Pin_class out;
      if (heavy(i, j)) {
        auto set_mask = gu::create_typed_node(*graph, Ntype_op::Set_mask);
        q[i].connect_sink(gu::setup_sink_by_name(set_mask, "a"));
        gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(set_mask, "mask"));
        q[j].connect_sink(gu::setup_sink_by_name(set_mask, "value"));
        out = set_mask.create_driver_pin(0);
        gu::set_bits(out, 256);
      } else {
        auto pair = gu::create_typed_node(*graph, Ntype_op::And);
        q[i].connect_sink(pair.create_sink_pin(0));
        q[j].connect_sink(pair.create_sink_pin(1));
        out = pair.create_driver_pin(0);
        gu::set_bits(out, 1);
      }
      out.connect_sink(graph->get_output_pin(std::format("p{}_{}", i, j)));
    }
  }
  return graph;
}

TEST(SimColorPlan, TuneClassCapKeepsTheHeaviestCost) {
  // Over 4096 distinct rows, so even the row merge cannot fit them and the
  // cap folds. Folding by GE would drop the 0-GE Set_mask first; the ladder
  // weight (cost_flat) keeps it, and only 1-word classes reach the fold.
  constexpr size_t flops = 100;
  const auto       graph = make_pairwise_class_overflow("tune_class_cap", flops);

  const auto plan = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.errors().front();
  const auto& support = plan.support();
  ASSERT_TRUE(support.available);
  ASSERT_TRUE(support.exact) << "101 sources fit two words";
  ASSERT_EQ(support.classes.size(), livehd::sim::Color_plan::kMaxSupportClasses);
  const uint64_t pairs = flops * (flops - 1) / 2;
  ASSERT_EQ(support.fold_classes, livehd::sim::Color_plan::kSupportFoldGroups);
  EXPECT_EQ(support.fold_members, 2 * flops + pairs - (livehd::sim::Color_plan::kMaxSupportClasses - support.fold_classes))
      << "one row class per flop (it also reads clk), per inverter and per pair";
  uint64_t fold_ge = 0, fold_sites = 0, fold_cost = 0, fold_cost_flat = 0;
  for (size_t c = support.classes.size() - support.fold_classes; c < support.classes.size(); ++c) {
    const auto& fold = support.classes[c];
    EXPECT_EQ(fold.cost, fold.sites) << "only 1-word classes are folded";
    fold_ge        += fold.ge;
    fold_sites     += fold.sites;
    fold_cost      += fold.cost;
    fold_cost_flat += fold.cost_flat;
  }
  EXPECT_EQ(fold_ge, support.fold_ge);
  EXPECT_EQ(fold_sites, support.fold_sites);
  EXPECT_EQ(fold_cost, support.fold_cost);
  EXPECT_EQ(fold_cost_flat, support.fold_cost_flat);
  EXPECT_GT(support.fold_sites, 0u);
  EXPECT_LT(support.fold_sites, support.total_sites / 4) << "1182 of 5150 sites fold";
  EXPECT_EQ(support.total_sites, 2 * flops + pairs);
  EXPECT_EQ(support.total_cost, 2 * flops + pairs - 1 + 4) << "the 256-bit Set_mask costs 4 words";
  EXPECT_EQ(support.total_cost_flat, support.total_cost);
  bool heavy_kept = false;
  for (size_t c = 0; c + support.fold_classes < support.classes.size(); ++c) {
    heavy_kept |= support.classes[c].cost == 4 && support.classes[c].ge == 0 && support.classes[c].sites == 1;
  }
  EXPECT_TRUE(heavy_kept) << "the heaviest-cost_flat class is never folded, even at 0 GE";
  EXPECT_NE(
      plan.report().find(std::format("fold-classes={} fold-members={} fold-ge={} fold-sites={} fold-cost={} fold-cost-flat={}",
                                     support.fold_classes,
                                     support.fold_members,
                                     support.fold_ge,
                                     support.fold_sites,
                                     support.fold_cost,
                                     support.fold_cost_flat)),
      std::string::npos)
      << "the report surfaces the fold share (the I_s ceiling)";
  expect_tune_weight_sums(plan);
}

TEST(SimColorPlan, TuneClassCapFoldNeverEmitsAnAllOnesRow) {
  // The forced overflow above: a folded group's row is the OR of its
  // members' rows -- conservative, but never the old all-ones row, which was
  // idle only when NOTHING changed and so capped I_s (review round 2,
  // driver-weights:FX2-R2-1). Here the union of every row leaves the 27
  // unused bucket bits of the two words clear, so no row may be all-ones; a
  // fold row reads at most the flops its few contiguous pair members read.
  constexpr size_t flops = 100;
  const auto       graph = make_pairwise_class_overflow("tune_class_cap_rows", flops);
  const auto       plan  = livehd::sim::Color_plan::discover(graph.get());
  ASSERT_TRUE(plan.complete()) << plan.errors().front();
  const auto& support = plan.support();
  ASSERT_EQ(support.words, 2u);
  ASSERT_EQ(support.fold_classes, livehd::sim::Color_plan::kSupportFoldGroups);
  std::vector<uint64_t> all_rows(support.words, 0);
  for (size_t c = 0; c < support.classes.size(); ++c) {
    bool all_ones = true;
    for (uint32_t w = 0; w < support.words; ++w) {
      all_rows[w] |= support.class_bits[c * support.words + w];
      all_ones    &= support.class_bits[c * support.words + w] == ~uint64_t{0};
    }
    EXPECT_FALSE(all_ones) << "class " << c << " is an all-ones row";
  }
  EXPECT_NE(all_rows[1], ~uint64_t{0}) << "the fixture's union must not be all-ones, or this test proves nothing";
  for (size_t c = support.classes.size() - support.fold_classes; c < support.classes.size(); ++c) {
    size_t bits = 0;
    for (uint32_t w = 0; w < support.words; ++w) {
      bits += static_cast<size_t>(std::popcount(support.class_bits[c * support.words + w]));
    }
    // ~8.5 contiguous (canonically adjacent) pair rows per group: they share
    // their low flop, so the union stays near members + 1 bits.
    EXPECT_LE(bits, 2 * (support.fold_members / support.fold_classes + 2)) << "fold class " << c;
    EXPECT_GE(bits, 1u) << "fold class " << c;
  }
  // The fold is a pure function of the pre-fence plan: vector invariant.
  const auto expected = tune_tables_text(plan);
  for (const uint64_t live_words : {1U, 256U}) {
    for (const int64_t fence : {livehd::sim::Color_plan::kNoFences, int64_t{0}}) {
      const auto other = livehd::sim::Color_plan::discover(graph.get(), true, false, live_words, fence);
      ASSERT_TRUE(other.complete());
      EXPECT_EQ(tune_tables_text(other), expected) << "live_words=" << live_words << " fence=" << fence;
    }
  }
}

TEST(SimColorPlan, TuneClassCapMergesEqualRowsBeforeFolding) {
  // 4200 instances of a stateless child that inverts one root input: 4200
  // occurrences, each owning a site whose single-period fan-in is exactly that
  // input. By (bits, occurrence) that is 4200+ classes -- past the cap, and the
  // old fold ORed the lightest into one all-ones class. Equal rows are idle in
  // exactly the same sampled pairs, so merging them by row is exact: nothing
  // folds, and every idle column (a class is idle iff row & mask == 0) is
  // what the per-site rows give.
  constexpr size_t instances = 4200;
  auto&            lib       = livehd::Hhds_graph_library::instance("lgdb_color_plan_tune_class_rows");
  auto             child_io  = lib.create_io("tune_rows_child");
  child_io->add_input("a", 0);
  child_io->add_output("y", 1);
  child_io->set_bits("a", 8);
  child_io->set_bits("y", 8);
  auto child = child_io->create_graph();
  auto inv   = gu::create_typed_node(*child, Ntype_op::Not);
  child->get_input_pin("a").connect_sink(inv.create_sink_pin(0));
  auto y = inv.create_driver_pin(0);
  gu::set_bits(y, 8);
  y.connect_sink(child->get_output_pin("y"));

  auto io = lib.create_io("tune_rows_top");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->set_bits("a", 8);
  io->set_bits("b", 8);
  for (size_t i = 0; i < instances; ++i) {
    io->add_output(std::format("y{}", i), static_cast<hhds::Port_id>(2 + i));
    io->set_bits(std::format("y{}", i), 8);
  }
  auto root = io->create_graph();
  for (size_t i = 0; i < instances; ++i) {
    auto call = gu::create_typed_node(*root, Ntype_op::Sub);
    call.set_subnode(child_io);
    call.set_name(std::format("u{}", i));
    // Half the instances read `a`, half `b`: two distinct rows.
    root->get_input_pin(i % 2 == 0 ? "a" : "b").connect_sink(call.create_sink_pin(0));
    call.create_driver_pin(1).connect_sink(root->get_output_pin(std::format("y{}", i)));
  }

  const auto plan = livehd::sim::Color_plan::discover(root.get());
  ASSERT_TRUE(plan.complete()) << plan.errors().front();
  const auto& support = plan.support();
  ASSERT_TRUE(support.available);
  ASSERT_GT(plan.occurrences().size(), livehd::sim::Color_plan::kMaxSupportClasses)
      << "the (bits, occurrence) split must exceed the cap, or this test proves nothing";
  EXPECT_EQ(support.fold_classes, 0u) << "equal rows merge exactly; nothing folds";
  EXPECT_EQ(support.fold_members, 0u);
  EXPECT_EQ(support.fold_sites, 0u);
  EXPECT_EQ(support.fold_cost_flat, 0u);
  EXPECT_LE(support.classes.size(), 4u) << "a handful of distinct rows: " << support.classes.size();
  // Exact idle columns: each source's bucket, as the only changed source,
  // leaves idle exactly the sites that do not read it.
  ASSERT_EQ(support.sources.size(), 2u) << "the two root inputs";
  for (const auto& changed : support.sources) {
    uint64_t idle_sites = 0;
    for (size_t c = 0; c < support.classes.size(); ++c) {
      if (!class_reads(plan, c, changed.bucket)) {
        idle_sites += support.classes[c].sites;
      }
    }
    // The 2100 child inverters of the other input stay idle, and so does
    // whatever root wiring reads only the other input.
    EXPECT_GE(idle_sites, instances / 2) << "port " << changed.port;
    EXPECT_LT(idle_sites, support.total_sites) << "port " << changed.port;
  }
  size_t merged = 0;
  for (const auto& cls : support.classes) {
    if (cls.sites >= instances / 2) {
      ++merged;
      EXPECT_EQ(cls.occurrence, plan.occurrences().size()) << "a row shared by many occurrences spans them all";
    }
  }
  EXPECT_EQ(merged, 2u) << "one merged class per input row";
  expect_tune_weight_sums(plan);
}
