// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_prepare.hpp"

#include <array>
#include <string>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::synth {
TEST(DesignPrepare, CopiesUnlistedCalleesAndOpaquePortsWithoutChangingSources) {
  hhds::GraphLibrary library;
  auto               opaque_io = library.create_io("opaque");
  opaque_io->add_input("din", 1);
  opaque_io->add_output("dout", 2);
  auto child_io = library.create_io("child");
  child_io->add_input("a", 1);
  child_io->add_output("y", 2);
  auto child = child_io->create_graph();
  auto macro = graph_util::create_typed_node(*child, Ntype_op::Sub);
  macro.set_subnode(opaque_io);
  child->get_input_pin("a").connect_sink(macro.create_sink_pin(1));
  macro.create_driver_pin(2).connect_sink(child->get_output_pin("y"));

  auto top_io = library.create_io("top");
  top_io->add_input("a", 1);
  top_io->add_output("y", 2);
  auto top      = top_io->create_graph();
  auto instance = graph_util::create_typed_node(*top, Ntype_op::Sub);
  instance.set_subnode(child_io);
  top->get_input_pin("a").connect_sink(instance.create_sink_pin(1));
  instance.create_driver_pin(2).connect_sink(top->get_output_pin("y"));
  const auto count = [](const hhds::Graph& g) {
    size_t result = 0;
    for (const auto node : g.body().nodes()) {
      (void)node;
      ++result;
    }
    return result;
  };
  const auto                                        top_nodes = count(*top), child_nodes = count(*child);
  const std::array<std::shared_ptr<hhds::Graph>, 3> sources{nullptr, top, top};
  Preparation_budget                                budget;
  budget.max_source_nodes = top_nodes + child_nodes;
  auto prepared           = prepare_design(sources, true, "pass.usyn", &budget);
  EXPECT_FALSE(budget.refused);
  EXPECT_EQ(budget.source_nodes, top_nodes + child_nodes);  // duplicate roots are not charged twice
  ASSERT_TRUE(prepared);
  ASSERT_EQ(prepared->roots.size(), 3U);
  EXPECT_FALSE(prepared->roots[0]);
  EXPECT_EQ(prepared->roots[1], prepared->roots[2]);
  EXPECT_NE(prepared->roots[1].get(), top.get());
  EXPECT_EQ(prepared->roots[1]->get_name(), "top");
  ASSERT_GE(prepared->resolve_graphs.size(), 4U);
  EXPECT_EQ(prepared->resolve_graphs[1], prepared->roots[1]);
  auto copied_child_io  = prepared->library.find_io("child");
  auto copied_opaque_io = prepared->library.find_io("opaque");
  ASSERT_TRUE(copied_child_io);
  ASSERT_TRUE(copied_opaque_io);
  ASSERT_TRUE(copied_child_io->get_graph());
  EXPECT_NE(copied_child_io->get_graph().get(), child.get());
  EXPECT_EQ(copied_opaque_io->get_input_port_id("din"), 1U);
  EXPECT_EQ(copied_opaque_io->get_output_port_id("dout"), 2U);
  for (const auto node : prepared->roots[1]->body().nodes()) {
    if (graph_util::type_op_of(node) == Ntype_op::Sub) {
      EXPECT_EQ(node.get_subnode_graph(), copied_child_io->get_graph());
    }
  }
  (void)copied_child_io->get_graph()->create_node();
  EXPECT_EQ(count(*copied_child_io->get_graph()), child_nodes + 1);
  EXPECT_EQ(count(*child), child_nodes);
  EXPECT_EQ(count(*top), top_nodes);
  EXPECT_EQ(instance.get_subnode_graph(), child);
  EXPECT_EQ(prepared->loops.independent, 0U);
  EXPECT_EQ(prepared->loops.carried, 0U);
}
TEST(DesignPrepare, AdmissionRefusalIsStickyAndNeverPublishesAPartialPrivateCopy) {
  hhds::GraphLibrary library;
  auto               opaque = library.create_io("opaque");
  opaque->add_input("a", 1);
  opaque->add_output("y", 2);
  auto io = library.create_io("top");
  io->add_input("a", 1);
  io->add_output("y", 2);
  auto graph = io->create_graph();
  auto macro = graph_util::create_typed_node(*graph, Ntype_op::Sub);
  macro.set_subnode(opaque);
  graph->get_input_pin("a").connect_sink(macro.create_sink_pin(1));
  const auto driver = macro.create_driver_pin(2);
  driver.connect_sink(graph->get_output_pin("y"));
  const std::array sources{graph};
  const auto       source_gids = library.all_gids();
  for (std::string_view stop : {"source-port",
                                "source-node",
                                "source-pin",
                                "source-edge",
                                "copy",
                                "copied",
                                "opaque-port",
                                "opaque-copy",
                                "opaque-copied",
                                "loops",
                                "loop-scan",
                                "complete"}) {
    SCOPED_TRACE(stop);
    Preparation_budget budget;
    bool               hit   = false;
    unsigned           calls = 0;
    budget.admission         = [&](std::string_view stage, uint64_t) {
      ++calls;
      if (stage == stop) {
        hit = true;
        return false;
      }
      return true;
    };
    EXPECT_FALSE(prepare_design(sources, false, "pass.usyn", &budget));
    EXPECT_TRUE(hit);
    EXPECT_TRUE(budget.refused);
    const auto stopped_calls = calls;
    EXPECT_FALSE(prepare_design(sources, false, "pass.usyn", &budget));
    EXPECT_EQ(calls, stopped_calls);  // a later successful callback cannot erase refusal
    EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), driver);
    EXPECT_EQ(macro.get_subnode_io(), opaque);
    EXPECT_EQ(library.all_gids(), source_gids);
    EXPECT_EQ(library.find_io("opaque"), opaque);
  }
  Preparation_budget too_small;
  too_small.max_source_nodes = 0;
  EXPECT_FALSE(prepare_design(sources, false, "pass.usyn", &too_small));
  EXPECT_TRUE(too_small.refused);
  EXPECT_EQ(too_small.source_nodes, 0U);
  Preparation_budget retry;
  retry.max_source_nodes = 1;  // exactly the opaque instance; primary IO nodes are not body nodes
  auto prepared          = prepare_design(sources, false, "pass.usyn", &retry);
  ASSERT_TRUE(prepared);
  EXPECT_FALSE(retry.refused);
  EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), driver);
  EXPECT_NE(prepared->roots.front().get(), graph.get());
}

TEST(DesignPrepare, ZeroNodeFeedthroughStillChargesPrimaryPortsAndEdges) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("wire");
  io->add_input("a", 1);
  io->add_output("y", 2);
  auto graph = io->create_graph();
  graph->get_input_pin("a").connect_sink(graph->get_output_pin("y"));
  const std::array   sources{graph};
  Preparation_budget budget;
  budget.max_source_nodes = 0;
  uint64_t ports = 0, edges = 0;
  budget.admission = [&](std::string_view stage, uint64_t work) {
    ports += stage == "source-port" ? work : 0;
    edges += stage == "source-edge" ? work : 0;
    return true;
  };
  ASSERT_TRUE(prepare_design(sources, false, "pass.usyn", &budget));
  EXPECT_EQ(budget.source_nodes, 0U);
  EXPECT_EQ(ports, 2U);
  EXPECT_EQ(edges, 1U);
  Preparation_budget stopped;
  bool               copied = false;
  stopped.admission         = [&](std::string_view stage, uint64_t) {
    copied |= stage == "copied";
    return stage != "source-edge";
  };
  EXPECT_FALSE(prepare_design(sources, false, "pass.usyn", &stopped));
  EXPECT_TRUE(stopped.refused);
  EXPECT_FALSE(copied);
  EXPECT_EQ(graph->get_output_pin("y").get_driver_pin(), graph->get_input_pin("a"));
}

TEST(DesignPrepare, SharedOpaqueDeclarationIsAdmittedOnlyOnce) {
  hhds::GraphLibrary library;
  auto               opaque = library.create_io("opaque");
  for (uint32_t i = 1; i <= 16; ++i) {
    opaque->add_input("a" + std::to_string(i), i);
  }
  opaque->add_output("y", 17);
  auto io    = library.create_io("top");
  auto graph = io->create_graph();
  for (unsigned i = 0; i < 8; ++i) {
    auto node = graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(opaque);
  }
  const std::array   sources{graph, graph};
  Preparation_budget budget;
  budget.max_source_nodes = 8;
  uint64_t ports = 0, copies = 0;
  budget.admission = [&](std::string_view stage, uint64_t work) {
    ports  += stage == "opaque-port" ? work : 0;
    copies += stage == "opaque-copy";
    return true;
  };
  auto prepared = prepare_design(sources, false, "pass.usyn", &budget);
  ASSERT_TRUE(prepared);
  EXPECT_EQ(budget.source_nodes, 8U);
  EXPECT_EQ(ports, 17U);
  EXPECT_EQ(copies, 1U);
  EXPECT_EQ(prepared->roots[0], prepared->roots[1]);
  EXPECT_EQ(prepared->library.find_io("opaque")->get_input_pin_decls().size(), 16U);
  EXPECT_EQ(library.find_io("opaque"), opaque);
}

TEST(DesignPrepare, ClockExpansionRefusalDiscardsPrivateCopyAndPreservesSource) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("gated");
  io->add_input("clk", 1);
  io->add_input("en", 2);
  io->add_output("gclk", 3);
  auto graph = io->create_graph();
  auto clock = graph_util::create_typed_node(*graph, Ntype_op::Clock_cell);
  graph->get_input_pin("clk").connect_sink(graph_util::setup_sink_by_name(clock, "clk_ref"));
  graph->get_input_pin("en").connect_sink(graph_util::setup_sink_by_name(clock, "en"));
  auto output = clock.create_driver_pin(0);
  graph_util::set_ubits(output, 1);
  output.connect_sink(graph->get_output_pin("gclk"));
  const std::array sources{graph};
  for (std::string_view stop : {"clock-cell-scan", "clock-cell-expand", "clock-cell-reader", "clock-cell-rewire"}) {
    SCOPED_TRACE(stop);
    Preparation_budget budget;
    bool               hit = false;
    budget.admission       = [&](std::string_view stage, uint64_t) {
      hit |= stage == stop;
      return stage != stop;
    };
    EXPECT_FALSE(prepare_design(sources, false, "pass.usyn", &budget));
    EXPECT_TRUE(hit);
    EXPECT_TRUE(budget.refused);
    EXPECT_EQ(graph->get_output_pin("gclk").get_driver_pin(), output);
    EXPECT_EQ(graph_util::type_op_of(clock), Ntype_op::Clock_cell);
  }
  auto prepared = prepare_design(sources, false, "pass.usyn");
  ASSERT_TRUE(prepared);
  EXPECT_EQ(graph_util::type_op_of(prepared->roots[0]->get_output_pin("gclk").get_driver_pin().get_master_node()), Ntype_op::And);
  EXPECT_EQ(graph->get_output_pin("gclk").get_driver_pin(), output);
}

// A carried compact loop of 8 iterations, `acc = acc | t_i` with t_i =
// Mux(s ^ i, 0, x ^ i) when `guarded` (the shape cprop's guard_accumulation
// leaves in a lifted body), else t_i = x ^ i. Expanded, it is one Or link per
// iteration. Loop_reassoc::all merges either chain into one n-ary Or;
// Loop_reassoc::guarded (pass.usyn) merges only the guarded one; ::none keeps
// both. The source graphs never change.
TEST(DesignPrepare, LoopReassocMergesGuardedChainsForGuardedAndEveryChainForAll) {
  namespace gu = graph_util;
  struct Loop_design {
    hhds::GraphLibrary           library;
    std::shared_ptr<hhds::Graph> top, body;
    explicit Loop_design(bool guarded) {
      auto bio = library.create_io("body");
      bio->add_input("acc", 1);
      bio->add_input("idx", 2);
      bio->add_input("s", 3);
      bio->add_input("x", 4);
      bio->add_output("acc_out", 5);
      for (auto name : {"acc", "idx", "s", "x", "acc_out"}) {
        bio->set_bits(name, 4);
        bio->set_unsign(name, true);
      }
      body = bio->create_graph();
      for (auto name : {"acc", "idx", "s", "x"}) {
        gu::set_ubits(body->get_input_pin(name), 4);
      }
      const auto op2 = [&](Ntype_op op, hhds::Pin_class a, hhds::Pin_class b) {
        auto node = gu::create_typed_node(*body, op);
        a.connect_sink(gu::setup_sink_pid(node, 0));
        b.connect_sink(gu::setup_sink_pid(node, 0));
        gu::set_color(node, 1);  // colored, as after pass.color: cprop does not flatten it
        auto out = node.create_driver_pin(0);
        gu::set_ubits(out, 4);
        return out;
      };
      auto term = op2(Ntype_op::Xor, body->get_input_pin("x"), body->get_input_pin("idx"));
      if (guarded) {
        auto sel   = op2(Ntype_op::Xor, body->get_input_pin("s"), body->get_input_pin("idx"));
        auto guard = gu::create_typed_node(*body, Ntype_op::Mux);
        gu::set_color(guard, 1);
        sel.connect_sink(gu::setup_sink_pid(guard, 0));
        gu::create_const(*body, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_pid(guard, 1));
        term.connect_sink(gu::setup_sink_pid(guard, 2));
        term = guard.create_driver_pin(0);
        gu::set_ubits(term, 4);
      }
      op2(Ntype_op::Or, body->get_input_pin("acc"), term).connect_sink(body->get_output_pin("acc_out"));

      auto tio = library.create_io("top");
      tio->add_input("s", 1);
      tio->add_input("x", 2);
      tio->add_output("y", 3);
      for (auto name : {"s", "x", "y"}) {
        tio->set_bits(name, 4);
        tio->set_unsign(name, true);
      }
      top = tio->create_graph();
      gu::set_ubits(top->get_input_pin("s"), 4);
      gu::set_ubits(top->get_input_pin("x"), 4);
      hhds::Subnode_loop loop;
      loop.first       = 0;
      loop.step        = 1;
      loop.count       = 8;
      loop.index_input = 2;
      auto inst        = gu::create_typed_node(*top, Ntype_op::Sub);
      inst.set_subnode(bio, loop);
      gu::set_color(inst, 1);
      gu::create_const(*top, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_pid(inst, 1));
      top->get_input_pin("s").connect_sink(gu::setup_sink_pid(inst, 3));
      top->get_input_pin("x").connect_sink(gu::setup_sink_pid(inst, 4));
      auto out = inst.create_driver_pin(5);
      gu::set_ubits(out, 4);
      out.connect_sink(gu::setup_sink_pid(inst, 1));  // the carry
      out.connect_sink(top->get_output_pin("y"));
    }
  };
  const auto ors = [](const hhds::Graph& g) {
    size_t n = 0;
    for (const auto node : g.body().nodes()) {
      n += gu::type_op_of(node) == Ntype_op::Or ? 1 : 0;
    }
    return n;
  };
  for (const bool guarded : {true, false}) {
    for (const auto mode : {Loop_reassoc::none, Loop_reassoc::guarded, Loop_reassoc::all}) {
      SCOPED_TRACE(std::string(guarded ? "guarded" : "plain") + " mode " + std::to_string(static_cast<int>(mode)));
      Loop_design      d(guarded);
      const std::array sources{d.top};
      auto             prepared = prepare_design(sources, true, "pass.usyn", nullptr, false, mode);
      ASSERT_TRUE(prepared);
      EXPECT_EQ(prepared->loops.expanded, 1U);
      const bool merge = mode == Loop_reassoc::all || (mode == Loop_reassoc::guarded && guarded);
      const auto links = ors(*prepared->roots[0]);
      EXPECT_GE(links, 1U);
      if (merge) {
        EXPECT_EQ(links, 1U);
        EXPECT_GE(prepared->reassoc.absorbed, 6U);
      } else {
        EXPECT_GE(links, 7U);  // one link per iteration (the first may fold into its constant carry-in)
        EXPECT_EQ(prepared->reassoc.absorbed, 0U);
      }
      EXPECT_EQ(ors(*d.top), 0U);   // the source keeps its compact loop
      EXPECT_EQ(ors(*d.body), 1U);
    }
  }
}

}  // namespace livehd::synth
