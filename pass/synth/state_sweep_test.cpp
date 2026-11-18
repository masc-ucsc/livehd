// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "state_sweep.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "flat_state.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "specialize.hpp"

namespace livehd::synth {
namespace {

namespace gu = livehd::graph_util;

constexpr hhds::Port_id kClk = 1, kData = 2, kOut = 3;

// One definition `name`: inputs clk (1 bit) and d (`d_bits`), output o (`o_bits`).
struct Module {
  std::shared_ptr<hhds::GraphIO> io;
  std::shared_ptr<hhds::Graph>   graph;
};

Module make_module(hhds::GraphLibrary& library, const std::string& name, int d_bits, int o_bits) {
  Module m;
  m.io = library.create_io(name);
  m.io->add_input("clk", kClk);
  m.io->add_input("d", kData);
  m.io->add_output("o", kOut);
  m.io->set_bits("clk", 1);
  m.io->set_bits("d", static_cast<uint32_t>(d_bits));
  m.io->set_bits("o", static_cast<uint32_t>(o_bits));
  for (const char* port : {"clk", "d", "o"}) {
    m.io->set_unsign(port, true);
  }
  m.graph = m.io->create_graph();
  return m;
}

hhds::Node_class make_reg(hhds::Graph& g, const hhds::Pin_class& din, const hhds::Pin_class& clk, int bits, std::string_view name) {
  auto reg = gu::create_typed_node(g, Ntype_op::Flop, bits);
  gu::set_unsign(reg.create_driver_pin(0));
  din.connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
  clk.connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
  if (!name.empty()) {
    reg.attr(hhds::attrs::name).set(std::string{name});
  }
  return reg;
}

hhds::Pin_class slice(hhds::Graph& g, const hhds::Pin_class& value, int lo, int hi) {
  auto get = gu::create_get_mask(g, value, lo, hi);
  auto out = get.create_driver_pin(0);
  gu::set_ubits(out, hi - lo);
  return out;
}

std::vector<hhds::Node_class> nodes_of(hhds::Graph& g, Ntype_op op) {
  std::vector<hhds::Node_class> out;
  for (auto n : g.body().nodes()) {
    if (gu::type_op_of(n) == op) {
      out.push_back(n);
    }
  }
  return out;
}

std::optional<State_sweep_stats> sweep(const std::vector<std::shared_ptr<hhds::Graph>>& defs, std::string_view top) {
  return sweep_unobserved_state(defs, top, nullptr);
}

TEST(StateSweep, RemovesStateThatOnlyFeedsItself) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               sum = gu::create_typed_node(g, Ntype_op::Sum, 16);
  auto               reg = make_reg(g, sum.create_driver_pin(0), g.get_input_pin("clk"), 16, "counter");
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(sum, "as"));
  gu::create_const(g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(sum, "as"));
  g.get_input_pin("d").connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed, 1U);
  EXPECT_EQ(stats->register_bits, 16U);
  EXPECT_EQ(stats->cells_removed, 1U);
  EXPECT_TRUE(nodes_of(g, Ntype_op::Flop).empty());
  EXPECT_TRUE(nodes_of(g, Ntype_op::Sum).empty());
}

TEST(StateSweep, KeepsRegistersAnOutputObserves) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 8, "r");
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed + stats->registers_trimmed + stats->registers_split + stats->cells_removed, 0U);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 8);
}

TEST(StateSweep, NarrowsARegisterToItsHighestObservedBit) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 32, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "r");
  slice(g, reg.create_driver_pin(0), 0, 8).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_trimmed, 1U);
  EXPECT_EQ(stats->trimmed_bits, 24U);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  const auto q = nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0);
  EXPECT_EQ(gu::bits_of(q), 8);
  EXPECT_TRUE(gu::is_unsign(q));
}

TEST(StateSweep, NarrowsARegisterToTheRangeOfItsInput) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 14, 33);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 33, "size");
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_trimmed, 1U);
  EXPECT_EQ(stats->trimmed_bits, 19U);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 14);
}

TEST(StateSweep, AResetValueWiderThanTheInputKeepsTheBitsItNeeds) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 14, 33);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 33, "size");
  gu::create_const(g, *Dlop::create_integer(1 << 20))
      .connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "initial"))));
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 21);
}

TEST(StateSweep, SplitsAScatteredRegisterIntoNamedSingleBitRegisters) {
  hhds::GraphLibrary library;
  auto               m = make_module(library, "top", 64, 8);
  auto&              g = *m.graph;
  // sr <= {sr[55:0], d[7:0]}: the far end is read, so only the top byte of the
  // chain is observed at the output; the shifted-in bits below it are not.
  auto reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 64, "sr");
  slice(g, reg.create_driver_pin(0), 56, 64).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_split, 1U);
  EXPECT_EQ(stats->split_bits, 8U);
  const auto flops = nodes_of(g, Ntype_op::Flop);
  ASSERT_EQ(flops.size(), 8U);
  std::vector<std::string> names;
  for (const auto& flop : flops) {
    EXPECT_EQ(gu::bits_of(flop.create_driver_pin(0)), 1);
    names.emplace_back(gu::node_name_of(flop));
  }
  std::ranges::sort(names);
  const std::vector<std::string> expected{"sr[56]", "sr[57]", "sr[58]", "sr[59]", "sr[60]", "sr[61]", "sr[62]", "sr[63]"};
  EXPECT_EQ(names, expected);
  EXPECT_EQ(nodes_of(g, Ntype_op::Concat).size(), 1U);
}

TEST(StateSweep, FollowsTheDemandThroughAShiftingRegister) {
  hhds::GraphLibrary library;
  auto               m = make_module(library, "top", 8, 8);
  auto&              g = *m.graph;
  // sr <= {sr[23:0], d}: reading the top byte needs every stage below it.
  auto reg = gu::create_typed_node(g, Ntype_op::Flop, 32);
  gu::set_unsign(reg.create_driver_pin(0));
  g.get_input_pin("clk").connect_sink(
      gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
  auto cat = gu::create_typed_node(g, Ntype_op::Concat);
  gu::set_ubits(cat.create_driver_pin(0), 32);
  slice(g, reg.create_driver_pin(0), 0, 24).connect_sink(gu::setup_sink_pid(cat, 0));
  gu::create_const(g, *Dlop::create_integer(24)).connect_sink(gu::setup_sink_pid(cat, 1));
  g.get_input_pin("d").connect_sink(gu::setup_sink_pid(cat, 2));
  gu::create_const(g, *Dlop::create_integer(8)).connect_sink(gu::setup_sink_pid(cat, 3));
  cat.create_driver_pin(0).connect_sink(
      gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
  slice(g, reg.create_driver_pin(0), 24, 32).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_split + stats->registers_removed + stats->registers_trimmed, 0U);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 32);
}

TEST(StateSweep, NarrowsAChildRegisterTheParentReadsInPart) {
  hhds::GraphLibrary library;
  auto               child = make_module(library, "child", 16, 16);
  auto               reg   = make_reg(*child.graph, child.graph->get_input_pin("d"), child.graph->get_input_pin("clk"), 16, "r");
  reg.create_driver_pin(0).connect_sink(child.graph->get_output_pin("o"));

  auto  parent = make_module(library, "top", 16, 4);
  auto& top    = *parent.graph;
  auto  inst   = gu::create_typed_node(top, Ntype_op::Sub);
  inst.set_subnode(child.io);
  top.get_input_pin("clk").connect_sink(inst.create_sink_pin(kClk));
  top.get_input_pin("d").connect_sink(inst.create_sink_pin(kData));
  auto out = inst.create_driver_pin(kOut);
  gu::set_ubits(out, 16);
  slice(top, out, 0, 4).connect_sink(top.get_output_pin("o"));

  const std::vector<std::shared_ptr<hhds::Graph>> defs{parent.graph, child.graph};
  const auto                                      stats = sweep(defs, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_trimmed, 1U);
  ASSERT_EQ(nodes_of(*child.graph, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(*child.graph, Ntype_op::Flop)[0].create_driver_pin(0)), 4);
}

TEST(StateSweep, DropsAChildRegisterTheParentNeverReads) {
  hhds::GraphLibrary library;
  auto               child = make_module(library, "child", 16, 16);
  auto               reg   = make_reg(*child.graph, child.graph->get_input_pin("d"), child.graph->get_input_pin("clk"), 16, "r");
  reg.create_driver_pin(0).connect_sink(child.graph->get_output_pin("o"));

  auto  parent = make_module(library, "top", 16, 16);
  auto& top    = *parent.graph;
  auto  inst   = gu::create_typed_node(top, Ntype_op::Sub);
  inst.set_subnode(child.io);
  top.get_input_pin("clk").connect_sink(inst.create_sink_pin(kClk));
  top.get_input_pin("d").connect_sink(inst.create_sink_pin(kData));
  top.get_input_pin("d").connect_sink(top.get_output_pin("o"));

  const std::vector<std::shared_ptr<hhds::Graph>> defs{parent.graph, child.graph};
  const auto                                      stats = sweep(defs, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed, 1U);
  EXPECT_TRUE(nodes_of(*child.graph, Ntype_op::Flop).empty());
}

// Two instances of one child: one parent output reads the first instance's register, nothing reads the second's.
// The plain sweep joins the reads (the child register stays for both); split_by_demand gives the unread instance its
// own copy, whose register the sweep then drops while the read one keeps it.
TEST(StateSweep, SplitByDemandDropsStateOnlyAnUnreadInstanceCarries) {
  hhds::GraphLibrary library;
  auto               child = make_module(library, "child", 16, 16);
  auto               reg   = make_reg(*child.graph, child.graph->get_input_pin("d"), child.graph->get_input_pin("clk"), 16, "r");
  reg.create_driver_pin(0).connect_sink(child.graph->get_output_pin("o"));

  auto                          parent = make_module(library, "top", 16, 16);
  auto&                         top    = *parent.graph;
  std::vector<hhds::Node_class> insts;
  for (int i = 0; i < 2; ++i) {
    auto inst = gu::create_typed_node(top, Ntype_op::Sub);
    inst.set_subnode(child.io);
    top.get_input_pin("clk").connect_sink(inst.create_sink_pin(kClk));
    top.get_input_pin("d").connect_sink(inst.create_sink_pin(kData));
    gu::set_ubits(inst.create_driver_pin(kOut), 16);
    insts.push_back(inst);
  }
  insts[0].get_driver_pin(kOut).connect_sink(top.get_output_pin("o"));  // insts[1]'s output is never read

  {  // without the split the shared register survives
    hhds::GraphLibrary lib2;
    auto               c2 = make_module(lib2, "child", 16, 16);
    auto               r2 = make_reg(*c2.graph, c2.graph->get_input_pin("d"), c2.graph->get_input_pin("clk"), 16, "r");
    r2.create_driver_pin(0).connect_sink(c2.graph->get_output_pin("o"));
    auto  p2 = make_module(lib2, "top", 16, 16);
    auto& t2 = *p2.graph;
    for (int i = 0; i < 2; ++i) {
      auto inst = gu::create_typed_node(t2, Ntype_op::Sub);
      inst.set_subnode(c2.io);
      t2.get_input_pin("clk").connect_sink(inst.create_sink_pin(kClk));
      t2.get_input_pin("d").connect_sink(inst.create_sink_pin(kData));
      auto out = inst.create_driver_pin(kOut);
      gu::set_ubits(out, 16);
      if (i == 0) {
        out.connect_sink(t2.get_output_pin("o"));
      }
    }
    ASSERT_TRUE(sweep({p2.graph, c2.graph}, "top"));
    EXPECT_EQ(nodes_of(*c2.graph, Ntype_op::Flop).size(), 1U);
  }

  std::vector<std::shared_ptr<hhds::Graph>> defs{parent.graph, child.graph};
  const auto                                made = split_by_demand(defs, "top", nullptr);
  ASSERT_TRUE(made);
  EXPECT_EQ(*made, 1);
  ASSERT_EQ(defs.size(), 3U);
  // The read instance keeps the original; the unread one points at the content-named copy.
  EXPECT_EQ(insts[0].get_subnode_graph().get(), child.graph.get());
  const auto copy = insts[1].get_subnode_graph();
  ASSERT_TRUE(copy);
  EXPECT_NE(copy.get(), child.graph.get());
  EXPECT_TRUE(std::string{copy->get_name()}.starts_with("child__d"));
  // A second split finds nothing left to separate.
  const auto again = split_by_demand(defs, "top", nullptr);
  ASSERT_TRUE(again);
  EXPECT_EQ(*again, 0);

  const auto stats = sweep(defs, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed, 1U);
  EXPECT_EQ(nodes_of(*child.graph, Ntype_op::Flop).size(), 1U);
  EXPECT_TRUE(nodes_of(*copy, Ntype_op::Flop).empty());
}

// pass.color stores a shared definition's colors per instance path (hier_color, keyed by the (definition gid, Sub id)
// steps from the top). A copy made after coloring under a copied parent changes those steps: rehome_hier_colors must
// give the copy the colors of the path now pointing at it, or the whole subtree reads uncolored (one color-0 region).
TEST(StateSweep, RehomedCopiesKeepTheirInstancePathColors) {
  hhds::GraphLibrary library;
  auto               leaf = make_module(library, "leaf", 4, 4);
  auto               reg  = make_reg(*leaf.graph, leaf.graph->get_input_pin("d"), leaf.graph->get_input_pin("clk"), 4, "r");
  reg.create_driver_pin(0).connect_sink(leaf.graph->get_output_pin("o"));

  const auto instance = [](hhds::Graph& g, const std::shared_ptr<hhds::GraphIO>& callee) {
    auto inst = gu::create_typed_node(g, Ntype_op::Sub);
    inst.set_subnode(callee);
    g.get_input_pin("clk").connect_sink(inst.create_sink_pin(kClk));
    g.get_input_pin("d").connect_sink(inst.create_sink_pin(kData));
    gu::set_ubits(inst.create_driver_pin(kOut), 4);
    return inst;
  };
  auto mid      = make_module(library, "mid", 4, 4);
  auto mid_leaf = instance(*mid.graph, leaf.io);
  mid_leaf.get_driver_pin(kOut).connect_sink(mid.graph->get_output_pin("o"));
  auto top = make_module(library, "top", 4, 4);
  auto a   = instance(*top.graph, mid.io);
  auto b   = instance(*top.graph, mid.io);
  a.get_driver_pin(kOut).connect_sink(top.graph->get_output_pin("o"));

  const auto top_gid = top.graph->get_gid();
  const auto key     = [](const hhds::Node_class& n) {
    return hhds::make_node_attr_key(static_cast<uint64_t>(n.get_debug_nid() & ~static_cast<hhds::Nid>(3)));
  };
  const auto step = [](const hhds::Node_class& sub) {
    const auto site = sub.get_definition_index();
    return hhds::Hier_attr_step{site.gid, site.value, std::nullopt};
  };
  const auto color = [&](hhds::Graph* g, const hhds::Node_class& n, std::vector<hhds::Hier_attr_step> steps) {
    hhds::AttrRef<livehd::attrs::hier_color_t> ref{g, key(n), top_gid, std::move(steps)};
    return ref.has() ? std::optional<int32_t>{ref.get()} : std::nullopt;
  };
  hhds::AttrRef<livehd::attrs::hier_color_t>{
      leaf.graph.get(),
      key(reg),
      top_gid,
      {step(a), step(mid_leaf)}
  }
      .set(3);
  hhds::AttrRef<livehd::attrs::hier_color_t>{
      leaf.graph.get(),
      key(reg),
      top_gid,
      {step(b), step(mid_leaf)}
  }
      .set(4);

  // Instance b gets its own copies of mid and leaf, as fold_flat_state / split_by_demand make them after coloring.
  std::vector<std::shared_ptr<hhds::Graph>> defs{top.graph, mid.graph, leaf.graph};
  auto                                      mid_copy  = specialize::clone_definition(defs, mid.graph, "mid__copy");
  auto                                      leaf_copy = specialize::clone_definition(defs, leaf.graph, "leaf__copy");
  b.set_subnode(mid_copy->get_io());
  hhds::Node_class copy_leaf_inst;
  for (auto n : mid_copy->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      n.set_subnode(leaf_copy->get_io());
      copy_leaf_inst = n;
    }
  }
  ASSERT_FALSE(copy_leaf_inst.is_invalid());
  const auto copy_reg = nodes_of(*leaf_copy, Ntype_op::Flop);
  ASSERT_EQ(copy_reg.size(), 1U);
  const std::vector<hhds::Hier_attr_step> b_path{step(b), step(copy_leaf_inst)};
  EXPECT_FALSE(color(leaf_copy.get(), copy_reg[0], b_path));  // the copy under the copied parent reads uncolored

  rehome_hier_colors(defs,
                     top.graph,
                     {
                         { mid_copy.get(),  mid.graph.get()},
                         {leaf_copy.get(), leaf.graph.get()}
  });
  EXPECT_EQ(color(leaf_copy.get(), copy_reg[0], b_path), std::optional<int32_t>{4});
  EXPECT_EQ(color(leaf.graph.get(), reg, {step(a), step(mid_leaf)}), std::optional<int32_t>{3});  // a's path untouched
}

TEST(StateSweep, AnInstanceOfABodyLessCellObservesItsInputs) {
  hhds::GraphLibrary library;
  auto               cell = library.create_io("cell");
  cell->add_input("a", 1);
  cell->add_output("y", 2);
  auto  parent = make_module(library, "top", 8, 8);
  auto& top    = *parent.graph;
  auto  reg    = make_reg(top, top.get_input_pin("d"), top.get_input_pin("clk"), 8, "r");
  auto  inst   = gu::create_typed_node(top, Ntype_op::Sub);
  inst.set_subnode(cell);
  reg.create_driver_pin(0).connect_sink(inst.create_sink_pin(1));
  top.get_input_pin("d").connect_sink(top.get_output_pin("o"));

  const auto stats = sweep({parent.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed + stats->registers_trimmed + stats->registers_split, 0U);
  EXPECT_EQ(nodes_of(top, Ntype_op::Flop).size(), 1U);
}

TEST(StateSweep, EveryOutputOfADefinitionNobodyInstantiatesIsObserved) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "orphan", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 8, "r");
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "");
  ASSERT_TRUE(stats);
  EXPECT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 8);
}

TEST(StateSweep, ACounterReadAtItsLowBitsKeepsOnlyThoseBits) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 4);
  auto&              g   = *m.graph;
  auto               sum = gu::create_typed_node(g, Ntype_op::Sum, 17);
  auto               reg = make_reg(g, sum.create_driver_pin(0), g.get_input_pin("clk"), 16, "count");
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(sum, "as"));
  gu::create_const(g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(sum, "as"));
  slice(g, reg.create_driver_pin(0), 0, 4).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_trimmed, 1U);
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 4);
}

TEST(StateSweep, AWordTheSetMaskOverwritesIsNotObserved) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "base");
  // o = Set_mask(base, d, 8, 16)[15:8]: the window is all of the result that is read.
  auto set = gu::create_set_mask(g, reg.create_driver_pin(0), g.get_input_pin("d"), 8, 16);
  gu::set_ubits(set.create_driver_pin(0), 32);
  slice(g, set.create_driver_pin(0), 8, 16).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed, 1U);
  EXPECT_TRUE(nodes_of(g, Ntype_op::Flop).empty());
  // The base operand of the Set_mask is tied low, not left undriven.
  ASSERT_EQ(nodes_of(g, Ntype_op::Set_mask).size(), 1U);
  EXPECT_TRUE(gu::get_driver_of_sink_name(nodes_of(g, Ntype_op::Set_mask)[0], "a").is_const());
}

TEST(StateSweep, AMuxSelectIsObservedInFullButItsArmsOnlyWhereTheOutputIs) {
  hhds::GraphLibrary library;
  auto               m = make_module(library, "top", 32, 4);
  auto&              g = *m.graph;
  auto               sel = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 8, "sel");
  auto               arm0 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "arm0");
  auto               arm1 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "arm1");
  auto               mux  = gu::create_typed_node(g, Ntype_op::Mux, 32);
  sel.create_driver_pin(0).connect_sink(gu::setup_sink_pid(mux, 0));
  arm0.create_driver_pin(0).connect_sink(gu::setup_sink_pid(mux, 1));
  arm1.create_driver_pin(0).connect_sink(gu::setup_sink_pid(mux, 2));
  slice(g, mux.create_driver_pin(0), 0, 4).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  std::vector<int> widths;
  for (const auto& flop : nodes_of(g, Ntype_op::Flop)) {
    widths.push_back(gu::bits_of(flop.create_driver_pin(0)));
  }
  std::ranges::sort(widths);
  EXPECT_EQ(widths, (std::vector<int>{4, 4, 8}));  // the select keeps its 8 bits, each arm 4
}

TEST(StateSweep, SignExtensionKeepsTheDemandOfTheBitsItPassesThrough) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 32, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "word");
  auto               ext = gu::create_typed_node(g, Ntype_op::Sext);
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(ext, "a"));
  gu::create_const(g, *Dlop::create_integer(32)).connect_sink(gu::setup_sink_by_name(ext, "b"));
  gu::set_sbits(ext.create_driver_pin(0), 40);
  slice(g, ext.create_driver_pin(0), 8, 16).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_split, 1U);
  EXPECT_EQ(stats->split_bits, 8U);
  EXPECT_EQ(nodes_of(g, Ntype_op::Flop).size(), 8U);
}

TEST(StateSweep, SignExtensionAboveTheKeptBitsDemandsTheSignBit) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 32, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "word");
  auto               ext = gu::create_typed_node(g, Ntype_op::Sext);
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(ext, "a"));
  gu::create_const(g, *Dlop::create_integer(16)).connect_sink(gu::setup_sink_by_name(ext, "b"));
  gu::set_sbits(ext.create_driver_pin(0), 32);
  slice(g, ext.create_driver_pin(0), 24, 32).connect_sink(g.get_output_pin("o"));  // all copies of bit 15

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  // Every result bit read is a copy of bit 15 of the kept window: that bit alone survives.
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 1);
  EXPECT_EQ(gu::node_name_of(nodes_of(g, Ntype_op::Flop)[0]), "word[15]");
}

TEST(StateSweep, ConstantArithmeticShiftsMoveTheDemandUp) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 32, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "word");
  auto               sra = gu::create_typed_node(g, Ntype_op::SRA);
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(sra, "a"));
  gu::create_const(g, *Dlop::create_integer(8)).connect_sink(gu::setup_sink_by_name(sra, "b"));
  gu::set_ubits(sra.create_driver_pin(0), 24);
  slice(g, sra.create_driver_pin(0), 0, 8).connect_sink(g.get_output_pin("o"));  // register bits 8..15

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_split, 1U);
  EXPECT_EQ(stats->split_bits, 8U);
  std::vector<std::string> names;
  for (const auto& flop : nodes_of(g, Ntype_op::Flop)) {
    names.emplace_back(gu::node_name_of(flop));
  }
  std::ranges::sort(names);
  const std::vector<std::string> expected{"word[10]", "word[11]", "word[12]", "word[13]", "word[14]", "word[15]", "word[8]", "word[9]"};
  EXPECT_EQ(names, expected);
}

TEST(StateSweep, AHugeConstantOperandDemandsEveryBitInsteadOfAllocatingABitmap) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 32, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "word");
  auto               sra = gu::create_typed_node(g, Ntype_op::SRA);
  reg.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(sra, "a"));
  gu::create_const(g, *Dlop::create_integer(int64_t{1} << 40)).connect_sink(gu::setup_sink_by_name(sra, "b"));
  gu::set_ubits(sra.create_driver_pin(0), 8);
  sra.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_removed + stats->registers_trimmed + stats->registers_split, 0U);  // not followed: all 32 bits stay
  ASSERT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
  EXPECT_EQ(gu::bits_of(nodes_of(g, Ntype_op::Flop)[0].create_driver_pin(0)), 32);
}

TEST(StateSweep, ARegisterThatOnlyEverLoadsItsResetConstantIsThatConstant) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, gu::create_const(g, *Dlop::create_integer(5)), g.get_input_pin("clk"), 8, "five");
  gu::create_const(g, *Dlop::create_integer(5))
      .connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "initial"))));
  g.get_input_pin("d").connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"))));
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_constant, 1U);
  EXPECT_EQ(stats->constant_bits, 8U);
  EXPECT_TRUE(nodes_of(g, Ntype_op::Flop).empty());
}

TEST(StateSweep, ARegisterWhoseResetValueDiffersFromWhatItLoadsStaysARegister) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, gu::create_const(g, *Dlop::create_integer(5)), g.get_input_pin("clk"), 8, "five");
  gu::create_const(g, *Dlop::create_integer(0))
      .connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "initial"))));
  g.get_input_pin("d").connect_sink(gu::setup_sink_pid(reg, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"))));
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_constant, 0U);
  EXPECT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
}

TEST(StateSweep, HotmuxControlsAreObservedInFullAndItsValuesWhereTheOutputIs) {
  hhds::GraphLibrary library;
  auto               m = make_module(library, "top", 32, 4);
  auto&              g = *m.graph;
  auto               c0 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 1, "c0");
  auto               c1 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 1, "c1");
  auto               v0 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "v0");
  auto               v1 = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 32, "v1");
  auto               hot = gu::create_typed_node(g, Ntype_op::Hotmux, 32);
  c0.create_driver_pin(0).connect_sink(gu::setup_sink_pid(hot, 0));
  v0.create_driver_pin(0).connect_sink(gu::setup_sink_pid(hot, 1));
  c1.create_driver_pin(0).connect_sink(gu::setup_sink_pid(hot, 2));
  v1.create_driver_pin(0).connect_sink(gu::setup_sink_pid(hot, 3));
  slice(g, hot.create_driver_pin(0), 0, 4).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  std::vector<int> widths;
  for (const auto& flop : nodes_of(g, Ntype_op::Flop)) {
    widths.push_back(gu::bits_of(flop.create_driver_pin(0)));
  }
  std::ranges::sort(widths);
  EXPECT_EQ(widths, (std::vector<int>{1, 1, 4, 4}));
}

TEST(StateSweep, ARegisterWhoseResetIsTiedActiveIsItsResetValue) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 8, "stuck");
  const auto         pid = [](const char* name) { return static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, name)); };
  gu::create_const(g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(reg, pid("reset_pin")));  // active high, held
  gu::create_const(g, *Dlop::create_integer(9)).connect_sink(gu::setup_sink_pid(reg, pid("initial")));
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_constant, 1U);
  EXPECT_TRUE(nodes_of(g, Ntype_op::Flop).empty());
}

TEST(StateSweep, ARegisterWhoseResetIsTiedInactiveKeepsLoadingItsInput) {
  hhds::GraphLibrary library;
  auto               m   = make_module(library, "top", 8, 8);
  auto&              g   = *m.graph;
  auto               reg = make_reg(g, g.get_input_pin("d"), g.get_input_pin("clk"), 8, "live");
  const auto         pid = [](const char* name) { return static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, name)); };
  gu::create_const(g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_pid(reg, pid("reset_pin")));  // active high, never
  gu::create_const(g, *Dlop::create_integer(9)).connect_sink(gu::setup_sink_pid(reg, pid("initial")));
  reg.create_driver_pin(0).connect_sink(g.get_output_pin("o"));

  const auto stats = sweep({m.graph}, "top");
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers_constant, 0U);  // D is the input d: not a constant
  EXPECT_EQ(nodes_of(g, Ntype_op::Flop).size(), 1U);
}

TEST(StateSweep, AnAdmissionRefusalIsReported) {
  hhds::GraphLibrary library;
  auto               m = make_module(library, "top", 8, 8);
  m.graph->get_input_pin("d").connect_sink(m.graph->get_output_pin("o"));
  Preparation_budget budget;
  budget.admission = [](std::string_view, uint64_t) { return false; };
  const std::vector<std::shared_ptr<hhds::Graph>> defs{m.graph};
  EXPECT_FALSE(sweep_unobserved_state(defs, "top", &budget));
  EXPECT_TRUE(budget.refused);
}

}  // namespace
}  // namespace livehd::synth
