// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "region_emit.hpp"

#include <iostream>
#include <map>

#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "node_util.hpp"

namespace livehd::usyn {
namespace {
namespace gu = graph_util;
using synth::Lid;
using synth::Lnet;

class RegionEmit : public ::testing::Test {
protected:
  hhds::GraphLibrary            source, destination;
  std::shared_ptr<hhds::Graph>  graph, body;
  partition::Region_body        rb;
  std::vector<hhds::Node_class> nodes;

  void SetUp() override {
    diag::sink().clear();
    diag::sink().set_human_stderr(false);
    diag::sink().set_jsonl_path("off");
    auto     io  = source.create_io("source");
    auto     out = destination.create_io("region");
    unsigned pid = 1;
    for (const auto& name : {"a", "clk", "en", "rst"}) {
      const int width = name == std::string_view{"a"} ? 2 : 1;
      io->add_input(name, pid);
      out->add_input(name, pid++);
      io->set_bits(name, width);
      out->set_bits(name, width);
      rb.inputs.push_back({name, {}, width, false});
    }
    for (const auto& name : {"q", "x"}) {
      io->add_output(name, pid);
      out->add_output(name, pid++);
      io->set_bits(name, 2);
      out->set_bits(name, 2);
    }
    graph          = io->create_graph();
    body           = out->create_graph();
    rb.src         = graph.get();
    rb.body        = body.get();
    rb.module_name = "region";
    for (auto& input : rb.inputs) {
      input.src_driver = graph->get_input_pin(input.name);
      gu::set_ubits(input.src_driver, input.bits);
    }
  }
  void TearDown() override {
    if (HasFailure()) {
      for (const auto& d : diag::sink().records()) {
        std::cerr << d.code << ": " << d.message << '\n';
      }
    }
    diag::sink().clear();
  }
  hhds::Node_class node(Ntype_op op, std::string_view name, int width) {
    auto n = gu::create_typed_node(*graph, op);
    n.attr(hhds::attrs::name).set(std::string{name});
    if (!Ntype::has_multiple_driver_pins(op)) {
      auto pin = n.create_driver_pin(0);
      gu::set_ubits(pin, width);
      gu::set_pin_name(pin, name);
    }
    nodes.push_back(n);
    return n;
  }
  hhds::Pin_class input(std::string_view name) { return graph->get_input_pin(name); }
  void            connect(hhds::Node_class n, std::string_view name, hhds::Pin_class value) {
    value.connect_sink(gu::setup_sink_by_name(n, name));
  }
  void constant(hhds::Node_class n, std::string_view name, int value) {
    connect(n, name, gu::create_const(*graph, *Dlop::create_integer(value)));
  }
  void output(std::string name, hhds::Pin_class value) {
    graph->get_io()->set_bits(name, gu::bits_of(value));
    body->get_io()->set_bits(name, gu::bits_of(value));
    value.connect_sink(graph->get_output_pin(name));
    rb.outputs.push_back({std::move(name), value, gu::bits_of(value), !gu::is_unsign(value)});
  }
  synth::Region_blast blast() {
    rb.nodes = nodes;
    synth::Blast_options options;
    options.logical_state = true;
    return synth::blast_region(rb, options, {});
  }
  Stateful_result select(const synth::Region_blast& b) {
    Budget work{100000000};
    return synthesize_stateful_region(b.lnet, *b.source_state, synth::State_target::cmos, {}, work);
  }
  static std::string boundary(hhds::Node_class node, int port, int bit) {
    return std::string{gu::node_name_of(node)} + "/" + std::to_string(port) + "/" + std::to_string(bit);
  }
  static std::vector<std::string> input_keys(const synth::Region_blast& b, const partition::Region_body& r) {
    std::vector<std::string> keys;
    for (const auto& origin : b.all_pi_order) {
      if (origin.kind == synth::Pi_kind::region_input) {
        const auto [port, bit] = b.pi_order[origin.index];
        keys.push_back("pi/" + r.inputs[port].name + "/" + std::to_string(bit));
      } else {
        const auto [box, port, bit] = b.bbox_pi[origin.index];
        keys.push_back("box/" + boundary(b.bboxes[box].node, b.bboxes[box].outs[port].port_id, bit));
      }
    }
    return keys;
  }
  static std::map<std::string, bool> evaluate(const synth::Region_blast& b, const partition::Region_body& r,
                                              const std::map<std::string, bool>& assignment) {
    auto                     keys = input_keys(b, r);
    std::vector<std::string> state(b.lnet.latches().size());
    for (const auto& bit : b.source_state->bits) {
      state[bit.latch] = bit.name;
    }
    const auto&       net = b.lnet;
    std::vector<bool> values(net.size());
    for (Lid id = 0; id < net.size(); ++id) {
      if (net.kind(id) == Lnet::Kind::source) {
        const auto index = net.source_index(id);
        values[id]       = assignment.at(net.is_latch_source(id) ? "q/" + state[index] : keys[index]);
      } else {
        unsigned word = 0;
        for (unsigned f = 0; f < net.fanin_count(id); ++f) {
          word |= unsigned(values[net.fanin(id, f)]) << f;
        }
        values[id] = net.eval(id, word);
      }
    }
    std::map<std::string, bool> observed;
    for (size_t i = 0; i < b.po_order.size(); ++i) {
      const auto [port, bit]                                              = b.po_order[i];
      observed["out/" + r.outputs[port].name + "/" + std::to_string(bit)] = values[net.outputs()[i].node];
    }
    for (size_t i = 0; i < b.bbox_po.size(); ++i) {
      for (const auto& target : b.bbox_po[i]) {
        const auto& box = b.bboxes[target.bx];
        observed["din/" + boundary(box.node, box.ins[target.input].port_id, target.bit)]
            = values[net.outputs()[b.po_order.size() + i].node];
      }
    }
    for (const auto& bit : b.source_state->bits) {
      observed["d/" + bit.name] = values[net.latch(bit.latch).d];
      for (const auto& control : b.source_state->controls) {
        if (control.source != bit.source) {
          continue;
        }
        for (size_t i = 0; i < control.outputs.size(); ++i) {
          observed["control/" + bit.name + "/" + std::to_string(static_cast<int>(control.kind)) + "/" + std::to_string(i)]
              = values[net.outputs()[control.outputs[i]].node];
        }
      }
    }
    return observed;
  }
};

TEST_F(RegionEmit, ReconnectsPackedPortsNativeIcgAndOpaqueLogicWithoutExtraHierarchy) {
  auto gate = node(Ntype_op::Latch, "gate_latch", 1);
  connect(gate, "din", input("en"));
  connect(gate, "enable", input("clk"));
  constant(gate, "posclk", 0);
  auto clk = node(Ntype_op::And, "gclk", 1);
  input("clk").connect_sink(gu::setup_sink_pid(clk, 0));
  gate.get_driver_pin(0).connect_sink(gu::setup_sink_pid(clk, 0));
  auto opaque_io = source.create_io("external_cell");
  opaque_io->add_input("d", 1);
  opaque_io->set_bits("d", 2);
  opaque_io->add_output("y", 2);
  opaque_io->set_bits("y", 2);
  opaque_io->add_output("unused", 3);
  opaque_io->set_bits("unused", 1);
  auto opaque = node(Ntype_op::Sub, "opaque", 2);
  opaque.set_subnode(opaque_io);
  auto y = opaque.create_driver_pin(2);
  gu::set_ubits(y, 2);
  auto reg = node(Ntype_op::Flop, "r", 2);
  connect(reg, "din", y);
  connect(reg, "clock_pin", clk.get_driver_pin(0));
  connect(reg, "enable", input("en"));
  connect(reg, "reset_pin", input("rst"));
  constant(reg, "async", 1);
  constant(reg, "negreset", 1);
  constant(reg, "initial", 2);
  auto xor_node = node(Ntype_op::Xor, "feedback", 2);
  input("a").connect_sink(gu::setup_sink_pid(xor_node, 0));
  reg.get_driver_pin(0).connect_sink(gu::setup_sink_pid(xor_node, 0));
  xor_node.get_driver_pin(0).connect_sink(opaque.create_sink_pin(1));
  output("q", reg.get_driver_pin(0));
  output("x", y);
  auto original = blast();
  ASSERT_EQ(original.status, synth::Region_blast::Status::blasted);
  ASSERT_EQ(original.source_state->clocks.size(), 1U);
  auto selected = select(original);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{1000000};
  auto   result = emit_logical_region(rb, original, *selected.region, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  unsigned                      subs = 0, latches = 0, registers = 0;
  std::vector<hhds::Node_class> rebuilt_nodes;
  for (auto n : body->body().nodes()) {
    rebuilt_nodes.push_back(n);
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Sub) {
      ++subs;
      EXPECT_EQ(n.get_subnode_io()->get_name(), "external_cell");
      EXPECT_FALSE(n.get_subnode_graph());
      EXPECT_EQ(gu::bits_of(n.get_driver_pin(3)), 1);
    }
    latches   += op == Ntype_op::Latch;
    registers += op == Ntype_op::Flop;
  }
  EXPECT_EQ(subs, 1U);
  EXPECT_EQ(latches, 1U);
  EXPECT_EQ(registers, 2U);
  partition::Region_body round;
  round.src         = body.get();
  round.nodes       = rebuilt_nodes;
  round.module_name = "round";
  for (const auto& p : rb.inputs) {
    round.inputs.push_back({p.name, body->get_input_pin(p.name), p.bits, p.sign});
  }
  for (const auto& p : rb.outputs) {
    round.outputs.push_back({p.name, body->get_output_pin(p.name).get_driver_pin(), p.bits, p.sign});
  }
  synth::Blast_options options;
  options.logical_state = true;
  auto rebuilt          = synth::blast_region(round, options, {});
  ASSERT_EQ(rebuilt.status, synth::Region_blast::Status::blasted);
  ASSERT_EQ(rebuilt.source_state->clocks.size(), 1U);
  auto keys = input_keys(original, rb);
  for (const auto& bit : original.source_state->bits) {
    keys.push_back("q/" + bit.name);
  }
  ASSERT_LT(keys.size(), 12U);
  for (unsigned x = 0; x < (1U << keys.size()); ++x) {
    std::map<std::string, bool> assignment;
    for (size_t i = 0; i < keys.size(); ++i) {
      assignment[keys[i]] = (x >> i) & 1;
    }
    EXPECT_EQ(evaluate(original, rb, assignment), evaluate(rebuilt, round, assignment)) << x;
  }
}

TEST_F(RegionEmit, WideNativeWiringStaysPackedAndNarrowLanesAreExplicitlyFitted) {
  gu::set_ubits(input("a"), 8192);
  graph->get_io()->set_bits("a", 8192);
  body->get_io()->set_bits("a", 8192);
  rb.inputs[0].bits = 8192;
  auto narrow       = node(Ntype_op::Concat, "narrow", 2);
  input("a").connect_sink(narrow.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(2)).connect_sink(narrow.create_sink_pin(1));
  output("q", narrow.get_driver_pin(0));
  auto wide = node(Ntype_op::Concat, "wide", 8193);
  input("a").connect_sink(wide.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(8192)).connect_sink(wide.create_sink_pin(1));
  input("en").connect_sink(wide.create_sink_pin(2));
  gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(wide.create_sink_pin(3));
  output("x", wide.get_driver_pin(0));
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  ASSERT_TRUE(b.direct_native_output[0]);
  ASSERT_TRUE(b.direct_native_output[1]);
  ASSERT_LT(b.lnet.size(), 10U);
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work, 100);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  auto q     = body->get_output_pin("q").get_driver_pin().get_master_node();
  auto lanes = gu::concat_lanes(q);
  ASSERT_EQ(lanes.size(), 1U);
  EXPECT_TRUE(gu::concat_lane_violation(lanes).empty());
  EXPECT_EQ(gu::bits_of(lanes[0].value), 2);
  EXPECT_EQ(gu::type_op_of(lanes[0].value.get_master_node()), Ntype_op::Get_mask);
  auto w = body->get_output_pin("x").get_driver_pin().get_master_node();
  EXPECT_EQ(gu::concat_total_width(w), 8193);
  EXPECT_EQ(gu::concat_lanes(w)[0].value, body->get_input_pin("a"));
  unsigned count = 0;
  for (auto n : body->body().nodes()) {
    (void)n;
    ++count;
  }
  EXPECT_LT(count, 10U);
}

TEST_F(RegionEmit, InvalidOrUnadmittedBoundaryNeverReportsSuccess) {
  output("q", input("a"));
  auto b        = blast();
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget tiny{1};
  EXPECT_EQ(emit_logical_region(rb, b, *selected.region, tiny).status, Status::search_exhausted);
  auto malformed = b;
  malformed.po_order.pop_back();
  Budget work{100000};
  EXPECT_EQ(emit_logical_region(rb, malformed, *selected.region, work).status, Status::invalid);
  malformed                       = b;
  malformed.all_pi_order[0].index = 999;
  EXPECT_EQ(emit_logical_region(rb, malformed, *selected.region, work).status, Status::invalid);
}

TEST_F(RegionEmit, SparseHighBitUsesSmallConstantsAndAnExplicitMask) {
  gu::set_ubits(input("a"), 100000);
  graph->get_io()->set_bits("a", 100000);
  body->get_io()->set_bits("a", 100000);
  rb.inputs[0].bits = 100000;
  auto select       = gu::create_get_mask(*graph, input("a"), 99999, 100000);
  nodes.push_back(select);
  auto q = select.create_driver_pin(0);
  gu::set_ubits(q, 1);
  output("q", q);
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  auto selected = this->select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work, 100);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  auto mask = body->get_output_pin("q").get_driver_pin().get_master_node();
  ASSERT_EQ(gu::type_op_of(mask), Ntype_op::Get_mask);
  EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(mask, "mask")).to_just_i64(), 1);
  auto shift = gu::get_driver_of_sink_name(mask, "a").get_master_node();
  ASSERT_EQ(gu::type_op_of(shift), Ntype_op::SRA);
  EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(shift, "b")).to_just_i64(), 99999);
  EXPECT_EQ(gu::get_driver_of_sink_name(shift, "a"), body->get_input_pin("a"));
  for (auto pin : body->get_constant_node().out_sorted_pins()) {
    EXPECT_TRUE(gu::const_of(pin).is_just_i64());
  }
}

TEST_F(RegionEmit, NativeMemoryRetainsAsyncResetAndPortAttributes) {
  auto memory = node(Ntype_op::Memory, "storage", 2);
  memory.attr(attrs::memory_async_reset).set(1);
  constant(memory, "bits", 2);
  constant(memory, "size", 1);
  constant(memory, "type", 2);
  constant(memory, "initial", 1);
  connect(memory, "update", input("a"));
  connect(memory, "update_enable", input("en"));
  connect(memory, "clock_pin", input("clk"));
  connect(memory, "reset", input("rst"));
  auto q = memory.create_driver_pin(Ntype::Memory_readall_pid);
  gu::set_ubits(q, 2);
  gu::set_pin_name(q, "contents");
  q.attr(attrs::pin_offset).set(4);
  output("q", q);
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  ASSERT_TRUE(b.lnet.latches().empty());
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  auto output = body->get_output_pin("q").get_driver_pin();
  auto neo    = output.get_master_node();
  ASSERT_EQ(gu::type_op_of(neo), Ntype_op::Memory);
  EXPECT_EQ(neo.attr(attrs::memory_async_reset).get_or(0), 1);
  EXPECT_EQ(gu::pin_name_of(output), "contents");
  EXPECT_EQ(output.attr(attrs::pin_offset).get_or(0), 4);
  EXPECT_EQ(gu::get_driver_of_sink_name(neo, "update"), body->get_input_pin("a"));
  EXPECT_EQ(gu::get_driver_of_sink_name(neo, "reset"), body->get_input_pin("rst"));
  EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(neo, "initial")).to_just_i64(), 1);
}

TEST_F(RegionEmit, SignedCombinationalBoundaryPreservesTheOperandInterpretation) {
  auto neg = node(Ntype_op::Not, "signed_value", 2);
  connect(neg, "a", input("en"));
  gu::set_sign(neg.get_driver_pin(0));
  auto opaque_io = source.create_io("signed_reader");
  opaque_io->add_input("d", 1);
  opaque_io->set_bits("d", 2);
  opaque_io->add_output("q", 2);
  opaque_io->set_bits("q", 2);
  auto opaque = node(Ntype_op::Sub, "reader", 2);
  opaque.set_subnode(opaque_io);
  neg.get_driver_pin(0).connect_sink(opaque.create_sink_pin(1));
  auto q = opaque.create_driver_pin(2);
  gu::set_ubits(q, 2);
  output("q", q);
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  ASSERT_EQ(b.bboxes.size(), 1U);
  ASSERT_EQ(b.bboxes[0].ins.size(), 1U);
  ASSERT_TRUE(b.bboxes[0].ins[0].sign);
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  for (auto neo : body->body().nodes()) {
    if (gu::type_op_of(neo) != Ntype_op::Sub) {
      continue;
    }
    auto value = neo.get_sink_pin(1).get_driver_pin();
    EXPECT_FALSE(gu::is_unsign(value));
    EXPECT_EQ(gu::bits_of(value), 2);
    EXPECT_EQ(gu::type_op_of(value.get_master_node()), Ntype_op::Sext);
  }
}

TEST_F(RegionEmit, SignedOneBitInputIsMaskedBeforeLogicalInversion) {
  // A signed one-bit true is -1: the logical writer's Xor(x, 1) inversion
  // would yield -2 unless the region input is first fitted to {0,1}.
  rb.inputs[2].sign = true;
  gu::set_sign(input("en"));
  auto inv = node(Ntype_op::Not, "inv", 1);
  connect(inv, "a", input("en"));
  output("q", inv.get_driver_pin(0));
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  ASSERT_FALSE(gu::is_unsign(body->get_input_pin("en")));
  unsigned masked = 0;
  for (auto neo : body->body().nodes()) {
    for (auto sink : neo.inp_sorted_pins()) {
      const auto driver = sink.get_driver_pin();
      if (driver.is_invalid()) {
        continue;
      }
      if (driver == body->get_input_pin("en")) {
        EXPECT_EQ(gu::type_op_of(neo), Ntype_op::Get_mask);
        ++masked;
      }
      if (gu::type_op_of(neo) == Ntype_op::Xor && !driver.is_const()) {
        EXPECT_TRUE(gu::is_unsign(driver));
      }
    }
  }
  EXPECT_GT(masked, 0U);
}

TEST_F(RegionEmit, MissingConcreteChildIsRefusedInsteadOfBecomingOpaque) {
  auto child_io = source.create_io("concrete");
  child_io->add_input("a", 1);
  child_io->add_output("q", 2);
  auto child = child_io->create_graph();
  child->get_input_pin("a").connect_sink(child->get_output_pin("q"));
  auto sub = node(Ntype_op::Sub, "instance", 2);
  sub.set_subnode(child_io);
  input("a").connect_sink(sub.create_sink_pin(1));
  auto q = sub.create_driver_pin(2);
  gu::set_ubits(q, 2);
  output("q", q);
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work);
  EXPECT_EQ(result.status, Status::invalid);
  EXPECT_EQ(result.reason, "native child definition is missing from output library");
  EXPECT_FALSE(destination.find_io("concrete"));
}

TEST_F(RegionEmit, CompactLoopRetainsDescriptorSeedAndCarryEdge) {
  auto io = source.create_io("loop_body");
  io->add_input("index", 1);
  io->add_input("carry_in", 2);
  io->add_output("carry_out", 3);
  io->set_bits("index", 8);
  io->set_bits("carry_in", 2);
  io->set_bits("carry_out", 2);
  auto child = io->create_graph();
  child->get_input_pin("carry_in").connect_sink(child->get_output_pin("carry_out"));
  ASSERT_TRUE(destination.copy_from(source, "loop_body"));
  auto               sub = node(Ntype_op::Sub, "loop", 2);
  hhds::Subnode_loop descriptor;
  descriptor.first       = 2;
  descriptor.step        = 3;
  descriptor.count       = 4;
  descriptor.index_input = 1;
  sub.set_subnode(io, descriptor);
  auto q = sub.create_driver_pin(3);
  gu::set_ubits(q, 2);
  q.connect_sink(sub.create_sink_pin(2));
  input("a").connect_sink(sub.create_sink_pin(2));
  sub.subnode_group().validate();
  output("q", q);
  auto b = blast();
  ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
  auto selected = select(b);
  ASSERT_TRUE(selected.region) << selected.reason;
  Budget work{100000};
  auto   result = emit_logical_region(rb, b, *selected.region, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  hhds::Node_class neo;
  for (auto candidate : body->body().nodes()) {
    if (gu::type_op_of(candidate) == Ntype_op::Sub) {
      ASSERT_TRUE(neo.is_invalid());
      neo = candidate;
    }
  }
  ASSERT_FALSE(neo.is_invalid());
  ASSERT_TRUE(neo.is_loop_subnode());
  EXPECT_EQ(neo.subnode_loop()->first, 2);
  EXPECT_EQ(neo.subnode_loop()->step, 3);
  EXPECT_EQ(neo.subnode_loop()->count, 4U);
  EXPECT_EQ(neo.get_subnode_graph(), destination.find_io("loop_body")->get_graph());
  std::vector<hhds::Pin_class> drivers;
  for (auto pin : neo.get_sink_pin(2).get_driver_pins()) {
    drivers.push_back(pin);
  }
  EXPECT_EQ(drivers.size(), 2U);
  EXPECT_NE(std::find(drivers.begin(), drivers.end(), body->get_input_pin("a")), drivers.end());
  EXPECT_NE(std::find(drivers.begin(), drivers.end(), neo.get_driver_pin(3)), drivers.end());
  EXPECT_NO_THROW(neo.subnode_group().validate());
}

}  // namespace
}  // namespace livehd::usyn
