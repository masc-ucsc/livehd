// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gated_fold.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace {

namespace gu = livehd::graph_util;

// clk, en, rst, a, b: 1-bit inputs; sa, sb: 4-bit signed; outputs q0..q2.
struct Fixture {
  hhds::GraphLibrary             library;
  std::shared_ptr<hhds::GraphIO> io;
  std::shared_ptr<hhds::Graph>   graph;
  int                            regs = 0;

  explicit Fixture(const std::string& name = "top") {
    io                = library.create_io(name);
    hhds::Port_id pid = 1;
    for (const char* in : {"clk", "en", "rst", "a", "b", "sa", "sb"}) {
      const bool wide = in[0] == 's';
      io->add_input(in, pid++);
      io->set_bits(in, wide ? 4 : 1);
      io->set_unsign(in, !wide);
    }
    for (const char* out : {"q0", "q1", "q2"}) {
      io->add_output(out, pid++);
      io->set_bits(out, 1);
      io->set_unsign(out, true);
    }
    graph = io->create_graph();
    for (const char* in : {"clk", "en", "rst", "a", "b"}) {
      gu::set_ubits(graph->get_input_pin(in), 1);
    }
    gu::set_sbits(graph->get_input_pin("sa"), 4);
    gu::set_sbits(graph->get_input_pin("sb"), 4);
  }

  hhds::Pin_class in(const char* name) const { return graph->get_input_pin(name); }

  static hhds::Pin_class op(hhds::Graph& g, Ntype_op type, const std::vector<hhds::Pin_class>& operands, int bits = 1,
                            bool sign = false) {
    auto n = gu::create_typed_node(g, type);
    for (const auto& o : operands) {
      gu::setup_sink_pid(n, 0).connect_driver(o);
    }
    auto out = n.create_driver_pin(0);
    if (sign) {
      gu::set_sbits(out, bits);
    } else {
      gu::set_ubits(out, bits);
    }
    return out;
  }
  hhds::Pin_class op(Ntype_op type, const std::vector<hhds::Pin_class>& operands) { return op(*graph, type, operands); }

  // A latch on `enable` whose D is `din`: transparent while clk is low when
  // `low`, else while it is high.
  hhds::Pin_class latch(const hhds::Pin_class& din, bool low) {
    auto l = gu::create_typed_node(*graph, Ntype_op::Latch, 1);
    (low ? op(Ntype_op::Not, {in("clk")}) : in("clk")).connect_sink(gu::setup_sink_by_name(l, "enable"));
    din.connect_sink(gu::setup_sink_by_name(l, "din"));
    auto q = l.create_driver_pin(0);
    gu::set_ubits(q, 1);
    return q;
  }
  hhds::Pin_class and_gate(const hhds::Pin_class& en) { return op(Ntype_op::And, {in("clk"), latch(en, true)}); }
  hhds::Pin_class or_gate(const hhds::Pin_class& en) {
    return op(Ntype_op::Or, {in("clk"), op(Ntype_op::Not, {latch(en, false)})});
  }

  // A named register (the LEC pairs the two sides by name) on `clk`, with an
  // optional enable and synchronous reset to 0, driving output `port`.
  hhds::Node_class reg(const char* port, const hhds::Pin_class& clk, const hhds::Pin_class& din, const hhds::Pin_class& enable = {},
                       bool sync_reset = false) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop, 1);
    flop.set_name("r" + std::to_string(regs++));
    din.connect_sink(gu::setup_sink_by_name(flop, "din"));
    clk.connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    if (!enable.is_invalid()) {
      enable.connect_sink(gu::setup_sink_by_name(flop, "enable"));
    }
    if (sync_reset) {
      in("rst").connect_sink(gu::setup_sink_by_name(flop, "reset_pin"));
      gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(flop, "initial"));
    }
    auto q = flop.create_driver_pin(0);
    gu::set_ubits(q, 1);
    q.connect_sink(graph->get_output_pin(port));
    return flop;
  }

  size_t nodes() const {
    size_t n = 0;
    for ([[maybe_unused]] auto node : graph->body().nodes()) {
      ++n;
    }
    return n;
  }

  Gated_fold_result fold(bool fall_sample = false) {
    return fold_gated_clocks(graph.get(), {.domain_clock = "clk", .fall_sample = fall_sample});
  }
};

// A definition `name` holding a latch-and gate: Q = CK & L, L transparent
// while CK is low, its D computed by `cone` from the definition's inputs
// x, y (4-bit signed). Instantiated once in `f` on clk, sa, sb.
template <class Cone>
hhds::Pin_class gate_in_definition(Fixture& f, const char* name, Cone cone) {
  auto dio = f.library.create_io(name);
  dio->add_input("CK", 1);
  dio->add_input("x", 2);
  dio->add_input("y", 3);
  dio->add_output("Q", 4);
  for (const char* p : {"CK", "Q"}) {
    dio->set_bits(p, 1);
    dio->set_unsign(p, true);
  }
  for (const char* p : {"x", "y"}) {
    dio->set_bits(p, 4);
    dio->set_unsign(p, false);
  }
  auto def = dio->create_graph();
  gu::set_ubits(def->get_input_pin("CK"), 1);
  gu::set_sbits(def->get_input_pin("x"), 4);
  gu::set_sbits(def->get_input_pin("y"), 4);
  auto l = gu::create_typed_node(*def, Ntype_op::Latch, 1);
  Fixture::op(*def, Ntype_op::Not, {def->get_input_pin("CK")}).connect_sink(gu::setup_sink_by_name(l, "enable"));
  cone(*def).connect_sink(gu::setup_sink_by_name(l, "din"));
  auto lq = l.create_driver_pin(0);
  gu::set_ubits(lq, 1);
  Fixture::op(*def, Ntype_op::And, {def->get_input_pin("CK"), lq}).connect_sink(def->get_output_pin("Q"));
  auto inst = gu::create_typed_node(*f.graph, Ntype_op::Sub);
  inst.set_subnode(dio);
  f.in("clk").connect_sink(inst.create_sink_pin(1));
  f.in("sa").connect_sink(inst.create_sink_pin(2));
  f.in("sb").connect_sink(inst.create_sink_pin(3));
  auto gclk = inst.create_driver_pin(4);
  gu::set_ubits(gclk, 1);
  return gclk;
}

TEST(GatedFold, LatchAndFolds) {
  Fixture    f;
  const auto gclk   = f.and_gate(f.in("en"));
  const auto flop   = f.reg("q0", gclk, f.op(Ntype_op::Xor, {f.in("a"), f.in("b")}));
  const auto result = f.fold();
  ASSERT_EQ(result.registers.at(flop).code, "fold");
  EXPECT_EQ(result.gates.at("latch-and"), 1U);
  // clock := root & gclk, enable := the latch's D.
  const auto clock = gu::get_driver_of_sink_name(flop, "clock_pin");
  EXPECT_EQ(gu::type_op_of(clock.get_master_node()), Ntype_op::And);
  EXPECT_TRUE(result.registers.at(flop).gclk == gclk);
  EXPECT_TRUE(gu::get_driver_of_sink_name(flop, "enable") == f.in("en"));
}

// The reset outranks the enable (rst ? 0 : en ? d : Q), so on the root clock
// it would reset with the gate closed, which the gated register never does:
// the register keeps its gate untouched. lhd/tests/lhd_domplace_gates_smoke.py
// proves the result and refutes the root-clock reading against the RTL.
TEST(GatedFold, SyncResetKeepsTheGate) {
  Fixture    f;
  const auto gclk    = f.and_gate(f.in("en"));
  const auto with    = f.reg("q0", gclk, f.in("a"), {}, true);
  const auto without = f.reg("q1", gclk, f.in("b"));
  const auto result  = f.fold();
  EXPECT_EQ(result.registers.at(with).code, "gated-sync-reset");
  EXPECT_EQ(result.refused.at("gated-sync-reset"), 1U);
  EXPECT_TRUE(gu::get_driver_of_sink_name(with, "clock_pin") == gclk);
  EXPECT_TRUE(gu::get_driver_of_sink_name(with, "enable").is_invalid());
  EXPECT_EQ(result.registers.at(without).code, "fold");
}

// latch-or: gclk = clk | ~L, so root & gclk would be the bare root and a
// register that stays CMOS (its redundant enable dropped) would load on
// every edge. The fold ORs instead: root | gclk == gclk.
TEST(GatedFold, LatchOrKeepsItsGatedClock) {
  Fixture    f;
  const auto gclk = f.or_gate(f.in("en"));
  const auto flop = f.reg("q0", gclk, f.in("a"));
  EXPECT_EQ(f.fold().registers.at(flop).code, "gated-fall-sample");
  const auto result = f.fold(/*fall_sample=*/true);
  ASSERT_EQ(result.registers.at(flop).code, "fold");
  EXPECT_EQ(result.gates.at("latch-or"), 1U);
  const auto node = gu::get_driver_of_sink_name(flop, "clock_pin").get_master_node();
  EXPECT_EQ(gu::type_op_of(node), Ntype_op::Or);
  std::vector<hhds::Pin_class> operands;
  for (const auto& sink : node.inp_sorted_pins()) {
    for (const auto& drv : sink.get_driver_pins()) {
      operands.push_back(drv);
    }
  }
  ASSERT_EQ(operands.size(), 2U);
  EXPECT_TRUE((operands[0] == f.in("clk") && operands[1] == gclk) || (operands[1] == f.in("clk") && operands[0] == gclk));
}

// An enable computed in a definition is cloned into the top with its
// attributes: (x ^ y) < 0 holds only while the xor is signed. The copies do
// not take the definition's names.
TEST(GatedFold, ClonedEnableKeepsSignedness) {
  Fixture    f;
  const auto gclk   = gate_in_definition(f, "lcg_signed", [](hhds::Graph& def) {
    auto xr = Fixture::op(def, Ntype_op::Xor, {def.get_input_pin("x"), def.get_input_pin("y")}, 4, true);
    xr.get_master_node().attr(hhds::attrs::name).set("xr");
    auto lt = gu::create_typed_node(def, Ntype_op::LT);
    xr.connect_sink(gu::setup_sink_pid(lt, 0));
    gu::create_const(def, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_pid(lt, 1));
    auto o = lt.create_driver_pin(0);
    gu::set_ubits(o, 1);
    return o;
  });
  const auto flop   = f.reg("q0", gclk, f.in("a"));
  const auto result = f.fold();
  ASSERT_EQ(result.registers.at(flop).code, "fold");
  int xors = 0;
  for (auto node : f.graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Xor) {
      ++xors;
      EXPECT_FALSE(gu::is_unsign(node.get_driver_pin(0)));
      EXPECT_EQ(gu::bits_of(node.get_driver_pin(0)), 4);
      EXPECT_FALSE(node.attr(hhds::attrs::name).has());
    }
  }
  EXPECT_EQ(xors, 1);
}

// A gate whose enable cone holds state is refused before any node is
// created (the Not in front of the flop is not cloned either), and once for
// every register it clocks.
TEST(GatedFold, RefusedEnableLeavesNoNodes) {
  Fixture    f;
  const auto gclk   = gate_in_definition(f, "lcg_state", [](hhds::Graph& def) {
    auto held = gu::create_typed_node(def, Ntype_op::Flop, 1);
    Fixture::op(def, Ntype_op::Xor, {def.get_input_pin("x"), def.get_input_pin("y")}, 4, true)
        .connect_sink(gu::setup_sink_by_name(held, "din"));
    def.get_input_pin("CK").connect_sink(gu::setup_sink_by_name(held, "clock_pin"));
    auto hq = held.create_driver_pin(0);
    gu::set_ubits(hq, 1);
    return Fixture::op(def, Ntype_op::Not, {hq});
  });
  const auto r0     = f.reg("q0", gclk, f.in("a"));
  const auto r1     = f.reg("q1", gclk, f.in("b"));
  const auto before = f.nodes();
  const auto result = f.fold();
  EXPECT_EQ(f.nodes(), before);
  EXPECT_EQ(result.registers.at(r0).code, "gated-enable-state");
  EXPECT_EQ(result.registers.at(r1).code, "gated-enable-state");
  EXPECT_EQ(result.refused.at("gated-enable-state"), 2U);
  EXPECT_TRUE(result.gates.empty());
}

}  // namespace
}  // namespace livehd::synth
