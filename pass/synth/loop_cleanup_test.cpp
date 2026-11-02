// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_cleanup.hpp"

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "query.hpp"

namespace livehd::synth {
namespace {
namespace gu = livehd::graph_util;

hhds::Pin_class op(hhds::Graph& graph, Ntype_op type, const std::vector<hhds::Pin_class>& inputs, int bits) {
  auto node = gu::create_typed_node(graph, type);
  for (const auto& input : inputs) {
    input.connect_sink(gu::setup_sink_pid(node, 0));
  }
  auto output = node.create_driver_pin(0);
  gu::set_ubits(output, bits);
  gu::set_color(node, 1);
  return output;
}

hhds::Pin_class constant(hhds::Graph& graph, int value) { return gu::create_const(graph, *Dlop::create_integer(value)); }

std::shared_ptr<hhds::Graph> top(hhds::GraphLibrary& library, const char* name) {
  auto io = library.create_io(name);
  io->add_input("hoist", 1);
  io->set_bits("hoist", 2);
  io->set_unsign("hoist", true);
  io->add_input("seed", 2);
  io->set_bits("seed", 1);
  io->set_unsign("seed", true);
  io->add_output("result", 3);
  io->set_bits("result", 1);
  io->set_unsign("result", true);
  auto graph = io->create_graph();
  gu::set_ubits(graph->get_input_pin("hoist"), 2);
  gu::set_ubits(graph->get_input_pin("seed"), 1);
  return graph;
}

class Parallel_boundary : public testing::TestWithParam<bool> {};

TEST_P(Parallel_boundary, PreservesDeclaredUpperBitAcrossExtraction) {
  hhds::GraphLibrary library;
  auto               body_io = library.create_io("body");
  body_io->add_input("hoist", 1);
  body_io->set_bits("hoist", 2);
  body_io->set_unsign("hoist", true);
  body_io->add_input("index", 2);
  body_io->set_bits("index", 2);
  body_io->add_input("carry", 3);
  body_io->set_bits("carry", 1);
  body_io->set_unsign("carry", true);
  body_io->add_output("next", 4);
  body_io->set_bits("next", 1);
  body_io->set_unsign("next", true);
  auto body  = body_io->create_graph();
  auto hoist = body->get_input_pin("hoist");
  gu::set_ubits(hoist, 2);
  gu::set_sbits(body->get_input_pin("index"), 2);
  gu::set_ubits(body->get_input_pin("carry"), 1);
  std::vector<std::shared_ptr<hhds::Graph>> definitions;
  if (GetParam()) {
    auto source_io = library.create_io("source");
    source_io->add_input("in", 1);
    source_io->set_bits("in", 2);
    source_io->set_unsign("in", true);
    source_io->add_output("out", 2);
    source_io->set_bits("out", 2);
    source_io->set_unsign("out", true);
    auto source = source_io->create_graph();
    gu::set_ubits(source->get_input_pin("in"), 2);
    source->get_input_pin("in").connect_sink(source->get_output_pin("out"));
    definitions.push_back(source);
    auto instance = gu::create_typed_node(*body, Ntype_op::Sub);
    instance.set_subnode(source_io);
    hoist.connect_sink(instance.create_sink_pin(1));
    hoist = instance.create_driver_pin(2);
  }
  // Reproduce the LRU body's local low-lane stamp: the actual source port is
  // two bits wide. Creating a new one-bit interface here discarded bit 1.
  gu::set_ubits(hoist, 1);
  auto combined = op(*body, Ntype_op::Or, {hoist, constant(*body, 1)}, 2);
  auto high     = op(*body, Ntype_op::EQ, {combined, constant(*body, 3)}, 1);
  op(*body, Ntype_op::Or, {body->get_input_pin("carry"), high}, 1).connect_sink(body->get_output_pin("next"));

  auto               parent   = top(library, "parent");
  auto               instance = gu::create_typed_node(*parent, Ntype_op::Sub);
  hhds::Subnode_loop descriptor;
  descriptor.count       = 2;
  descriptor.index_input = 2;
  instance.set_subnode(body_io, descriptor);
  parent->get_input_pin("hoist").connect_sink(instance.create_sink_pin(1));
  parent->get_input_pin("seed").connect_sink(instance.create_sink_pin(3));
  auto output = instance.create_driver_pin(4);
  gu::set_ubits(output, 1);
  output.connect_sink(instance.create_sink_pin(3));
  output.connect_sink(parent->get_output_pin("result"));
  definitions.insert(definitions.end(), {body, parent});

  Loop_preparation prepared;
  ASSERT_TRUE(prepare_loop_bodies(definitions, true, prepared));
  ASSERT_EQ(prepared.expanded, 1);
  ASSERT_FALSE(prepared.shared_bodies.empty());
  auto reference          = top(library, "reference");
  auto reference_combined = op(*reference, Ntype_op::Or, {reference->get_input_pin("hoist"), constant(*reference, 1)}, 2);
  auto reference_high     = op(*reference, Ntype_op::EQ, {reference_combined, constant(*reference, 3)}, 1);
  op(*reference, Ntype_op::Or, {reference->get_input_pin("seed"), reference_high}, 1)
      .connect_sink(reference->get_output_pin("result"));
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> callees;
  for (const auto& graph : definitions) {
    callees.emplace(graph->get_gid(), graph.get());
  }
  for (const auto& graph : prepared.shared_bodies) {
    callees.emplace(graph->get_gid(), graph.get());
  }
  lec::Lec_options options;
  options.engine     = "ind";
  const auto checked = lec::prove_equal(reference.get(), parent.get(), options, &callees);
  EXPECT_EQ(checked.verdict, lec::Verdict::Proven) << checked.detail << ' ' << checked.witness;
}

INSTANTIATE_TEST_SUITE_P(SourcePort, Parallel_boundary, testing::Bool());

}  // namespace
}  // namespace livehd::synth
