// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_boundary.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"

namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
using namespace livehd::satopt;

struct Design {
  hhds::GraphLibrary           lib;
  // A definition with 8-bit unsigned inputs `a`, `b` and output `o`.
  std::shared_ptr<hhds::Graph> def(std::string_view name) {
    auto io = lib.create_io(name);
    int  id = 1;
    for (auto port : {"a", "b"}) {
      io->add_input(port, id++);
      io->set_bits(port, 8);
      io->set_unsign(port, true);
    }
    io->add_output("o", id);
    io->set_bits("o", 8);
    io->set_unsign("o", true);
    auto g = io->create_graph();
    gu::set_ubits(g->get_input_pin("a"), 8);
    gu::set_ubits(g->get_input_pin("b"), 8);
    return g;
  }
  static Pin cn(hhds::Graph& g, int v) { return gu::create_const(g, *Dlop::create_integer(v)); }
  static Pin op(hhds::Graph& g, Ntype_op kind, std::initializer_list<Pin> inputs, int color) {
    auto n = gu::create_typed_node(g, kind);
    for (const auto& p : inputs) {
      p.connect_sink(gu::setup_sink_pid(n, 0));
    }
    if (color != 0) {
      gu::set_color(n, color);
    }
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, 8);
    return out;
  }
};

// The reader of `sink`'s node operand that is not `other`.
Pin operand_not(const Node& n, const Pin& other) {
  for (const auto& in_pin : n.inp_sorted_pins()) {
    const auto d = in_pin.get_driver_pin();
    if (d != other) {
      return d;
    }
  }
  return {};
}

TEST(SatoptBoundary, ColorCrossingBitsBecomeConstantsForTheReader) {
  Design d;
  auto   g = d.def("cross");
  auto   a = g->get_input_pin("a");
  auto   b = g->get_input_pin("b");
  // color 1: v = (a & 0xF0) | 1 -- bits 3..1 are 0 and bit 0 is 1 whatever `a` is.
  auto   v = Design::op(*g, Ntype_op::Or, {Design::op(*g, Ntype_op::And, {a, Design::cn(*g, 0xF0)}, 1), Design::cn(*g, 1)}, 1);
  // color 2 reads it.
  auto   x = Design::op(*g, Ntype_op::Xor, {v, b}, 2);
  x.connect_sink(g->get_output_pin("o"));

  Options opts;
  opts.stages  = {Stage::boundary};
  const auto r = run(g.get(), opts).at(Stage::boundary);
  EXPECT_EQ(r.state, Stage_state::completed);
  EXPECT_EQ(r.proven, 1U);
  EXPECT_EQ(r.bits, 4U);
  // The reader now reads a rebuilt value of its own color, not the color-1 value.
  const auto seen = operand_not(x.get_master_node(), b);
  ASSERT_FALSE(seen.is_invalid());
  EXPECT_NE(seen, v);
  EXPECT_EQ(gu::color_of(seen.get_master_node()), 2);
  EXPECT_EQ(gu::type_op_of(seen.get_master_node()), Ntype_op::Or);  // Or(And(v, 0xF0), 1)
}

TEST(SatoptBoundary, NothingToProveKeepsTheGraph) {
  Design d;
  auto   g = d.def("free");
  auto   a = g->get_input_pin("a");
  auto   b = g->get_input_pin("b");
  auto   v = Design::op(*g, Ntype_op::And, {a, b}, 1);  // no constant bit
  auto   x = Design::op(*g, Ntype_op::Xor, {v, b}, 2);
  x.connect_sink(g->get_output_pin("o"));
  Options opts;
  opts.stages  = {Stage::boundary};
  const auto r = run(g.get(), opts).at(Stage::boundary);
  EXPECT_EQ(r.proven, 0U);
  EXPECT_EQ(operand_not(x.get_master_node(), b), v);
}

TEST(SatoptBoundary, UncoloredGraphIsInapplicable) {
  Design d;
  auto   g = d.def("plain");
  auto   v = Design::op(*g, Ntype_op::And, {g->get_input_pin("a"), Design::cn(*g, 0xF0)}, 0);
  v.connect_sink(g->get_output_pin("o"));
  Options opts;
  opts.stages = {Stage::boundary};
  EXPECT_EQ(run(g.get(), opts).at(Stage::boundary).state, Stage_state::inapplicable);
}

// Two instances of `child` drive its input `a` with bit 0 set (one with bits 1..0 set): only what both agree on,
// bit 0 = 1, is read as a constant inside the child. Its output o = a & 1 is then the constant 1 for every parent reader.
TEST(SatoptBoundary, InstanceInputsAgreeAndOutputsComeBack) {
  Design d;
  auto   child = d.def("child");
  {
    auto low = Design::op(*child, Ntype_op::And, {child->get_input_pin("a"), Design::cn(*child, 1)}, 0);
    low.connect_sink(child->get_output_pin("o"));
  }
  auto       top      = d.def("top");
  auto       a        = top->get_input_pin("a");
  auto       b        = top->get_input_pin("b");
  const auto instance = [&](const Pin& drive) {
    auto sub = gu::create_typed_node(*top, Ntype_op::Sub);
    sub.set_subnode(child->get_io());
    drive.connect_sink(sub.create_sink_pin(1));
    b.connect_sink(sub.create_sink_pin(2));
    auto out = sub.create_driver_pin(3);
    gu::set_ubits(out, 8);
    return out;
  };
  auto o1 = instance(Design::op(*top, Ntype_op::Or, {a, Design::cn(*top, 1)}, 0));
  auto o2 = instance(Design::op(*top, Ntype_op::Or, {b, Design::cn(*top, 3)}, 0));
  auto x  = Design::op(*top, Ntype_op::Xor, {o1, o2}, 0);
  x.connect_sink(top->get_output_pin("o"));

  Stage_report report;
  Meter        meter;
  prove_instance_bits({child, top}, "top", Profile::shared, report, meter);
  // Round 0: both instances read o's bits 7..1 as 0 (14 bits) and the child reads a's bit 0 as 1 (1 bit); round 1: o's
  // bit 0 is then 1 for both instances (2 bits).
  EXPECT_EQ(report.bits, 17U);
  // The child alone, its inputs free, now computes the constant 1 ...
  {
    livehd::formal::Prover prover(child.get());
    const auto             o = child->get_output_pin("o").get_driver_pin();
    EXPECT_EQ(prover.masked_const(o, 8, *Dlop::create_integer(0xFF), *Dlop::create_integer(1)).verdict,
              livehd::formal::Verdict::Proven);
  }
  // ... and the parent, with every instance output a free word, computes 1 ^ 1.
  {
    livehd::formal::Prove_options options;
    options.subs_as_symbols = true;
    livehd::formal::Prover prover(top.get(), options);
    EXPECT_EQ(prover.masked_const(x, 8, *Dlop::create_integer(0xFF), *Dlop::create_integer(0)).verdict,
              livehd::formal::Verdict::Proven);
  }
}
}  // namespace
