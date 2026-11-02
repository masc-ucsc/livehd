// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

namespace {

hhds::Pin_class bit(hhds::Graph& g, Ntype_op op, hhds::Pin_class a, hhds::Pin_class b = {}) {
  auto node = gu::create_typed_node(g, op);
  a.connect_sink(node.create_sink_pin(0));
  if (!b.is_invalid()) {
    b.connect_sink(node.create_sink_pin(1));
  }
  auto out = node.create_driver_pin(0);
  gu::set_ubits(out, 1);
  return out;
}

hhds::Node_class latch(hhds::Graph& g, const std::string& name, hhds::Pin_class window, hhds::Pin_class data) {
  auto node = gu::create_typed_node(g, Ntype_op::Latch);
  node.attr(hhds::attrs::name).set(name);
  gu::set_ubits(node.create_driver_pin(0), 1);
  window.connect_sink(gu::setup_sink_by_name(node, "enable"));
  data.connect_sink(gu::setup_sink_by_name(node, "din"));
  return node;
}

struct Fixture {
  hhds::GraphLibrary&          lib;
  std::shared_ptr<hhds::Graph> graph;
  hhds::Node_class             source;
  hhds::Pin_class              gate;

  explicit Fixture(const std::string& tag) : lib(livehd::Hhds_graph_library::instance("lgdb_latch_window_" + tag)) {
    auto io = lib.create_io("top");
    io->add_input("clk", 1);
    io->add_input("en", 2);
    io->add_input("data", 3);
    io->add_input("other", 4);
    io->add_output("q", 5);
    io->add_output("leak", 6);
    for (const char* name : {"clk", "en", "data", "other", "q", "leak"}) {
      io->set_bits(name, 1);
      io->set_unsign(name, true);
    }
    graph  = io->create_graph();
    gate   = bit(*graph, Ntype_op::And, graph->get_input_pin("clk"), graph->get_input_pin("en"));
    source = latch(*graph, "source", bit(*graph, Ntype_op::Not, gate), graph->get_input_pin("data"));
  }

  void masked_reader(hhds::Pin_class data) {
    auto sink = latch(*graph, "sink", gate, data);
    sink.get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  }

  void reconverge() {
    auto a = source.get_driver_pin(0);
    auto b = graph->get_input_pin("other");
    // Only 27 combinational nodes, but more than 4096 paths to their reader.
    for (int i = 0; i < 13; ++i) {
      auto next_a = bit(*graph, Ntype_op::Or, a, b);
      auto next_b = bit(*graph, Ntype_op::Xor, a, b);
      a           = next_a;
      b           = next_b;
    }
    masked_reader(bit(*graph, Ntype_op::Or, a, b));
  }

  bool occurrence_is_masked() const {
    for (auto node : graph->occurrences().nodes()) {
      if (node.get_definition_index() == source.get_definition_index()) {
        return lc::gated_latch_masked_off(node);
      }
    }
    ADD_FAILURE() << "source latch missing from occurrence view";
    return false;
  }
};

}  // namespace

TEST(LatchWindow, ReconvergentFlatConeIsMasked) {
  Fixture fixture("flat");
  fixture.reconverge();
  EXPECT_TRUE(lc::gated_latch_masked_off(fixture.source));
}

TEST(LatchWindow, ReconvergentOccurrenceConeIsMasked) {
  Fixture fixture("occurrence");
  fixture.reconverge();
  EXPECT_TRUE(fixture.occurrence_is_masked());
}

TEST(LatchWindow, AnUnmaskedOutputStillRefuses) {
  Fixture fixture("leak");
  fixture.reconverge();
  fixture.source.get_driver_pin(0).connect_sink(fixture.graph->get_output_pin("leak"));
  EXPECT_FALSE(lc::gated_latch_masked_off(fixture.source));
  EXPECT_FALSE(fixture.occurrence_is_masked());
}

TEST(LatchWindow, SeparateInstancesMustKeepSeparateReaders) {
  for (bool leak_first : {false, true}) {
    Fixture fixture(leak_first ? "instances_leak_first" : "instances_masked_first");
    auto    io = fixture.lib.create_io("invert");
    // Instance pin zero is the node-as-pin, so boundary ports are nonzero.
    io->add_input("a", 1);
    io->add_output("z", 2);
    io->set_bits("a", 1);
    io->set_bits("z", 1);
    auto child    = io->create_graph();
    auto inverted = bit(*child, Ntype_op::Not, child->get_input_pin("a"));
    inverted.connect_sink(child->get_output_pin("z"));
    int child_input_edges = 0;
    for ([[maybe_unused]] auto edge : child->get_input_pin("a").out_edges()) {
      ++child_input_edges;
    }
    EXPECT_EQ(child_input_edges, 1);
    for (bool first : {true, false}) {
      auto instance = gu::create_typed_node(*fixture.graph, Ntype_op::Sub);
      instance.set_subnode(io);
      fixture.source.get_driver_pin(0).connect_sink(instance.create_sink_pin(1));
      auto out = instance.create_driver_pin(2);
      gu::set_ubits(out, 1);
      if (first == leak_first) {
        out.connect_sink(fixture.graph->get_output_pin("leak"));
      } else {
        fixture.masked_reader(out);
      }
    }
    int child_occurrences = 0;
    for (auto node : fixture.graph->occurrences().nodes()) {
      if (node.get_definition_index() == inverted.get_master_node().get_definition_index()) {
        ++child_occurrences;
      }
    }
    EXPECT_EQ(child_occurrences, 2);
    EXPECT_FALSE(fixture.occurrence_is_masked()) << "leak_first=" << leak_first;
  }
}
