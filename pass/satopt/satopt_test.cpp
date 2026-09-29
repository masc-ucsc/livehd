// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <cstdlib>
#include <stdexcept>

#include "blast.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"
#include "satopt_detail.hpp"
#include "satopt_mux.hpp"

namespace gu = livehd::graph_util;
namespace {
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, s;
  hhds::Node_class             mux;
  Fixture(std::string_view name) {
    auto io = lib.create_io(name);
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    g = io->create_graph();
    x = g->get_input_pin("x");
    gu::set_ubits(x, 8);
    auto red = gu::create_typed_node(*g, Ntype_op::Ror);
    x.connect_sink(red.create_sink_pin(0));
    s = red.create_driver_pin(0);
    gu::set_ubits(s, 1);
    gu::set_color(red, 1);
    mux = gu::create_typed_node(*g, Ntype_op::Mux);
    gu::set_color(mux, 2);
    s.connect_sink(mux.create_sink_pin(0));
    x.connect_sink(mux.create_sink_pin(1));
    gu::create_const(*g, *Dlop::create_integer(255)).connect_sink(mux.create_sink_pin(2));
    auto y = mux.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(g->get_output_pin("y"));
  }
};
}  // namespace

TEST(Satopt, SharedHotmuxBlasterUsesEveryControlBit) {
  Fixture f("satopt_hotmux_control");
  gu::drop_drivers(f.mux);
  gu::set_type_op(f.mux, Ntype_op::Hotmux);
  f.x.connect_sink(f.mux.create_sink_pin(0));
  gu::create_const(*f.g, *Dlop::create_integer(255)).connect_sink(f.mux.create_sink_pin(1));
  gu::create_const(*f.g, *Dlop::create_integer(0)).connect_sink(f.mux.create_sink_pin(2));
  struct Boolean {
    int        values[2] = {0, 1};
    const int* zero() const { return &values[0]; }
    const int* one() const { return &values[1]; }
    const int* inv(const int* a) const { return &values[*a ^ 1]; }
    const int* and_(const int* a, const int* b) const { return &values[*a & *b]; }
    const int* or_(const int* a, const int* b) const { return &values[*a | *b]; }
    const int* xor_(const int* a, const int* b) const { return &values[*a ^ *b]; }
  } ops;
  for (int input = 0; input < 256; ++input) {
    std::vector<const int*> slots(8);
    auto                    read = [&](const hhds::Pin_class& p, int bit) {
      const bool value = p.is_const() ? gu::const_of(p).bit_test(bit) : ((input >> bit) & 1) != 0;
      return value ? ops.one() : ops.zero();
    };
    auto refuse = [](const hhds::Node_class&,
                     std::string_view,
                     std::string_view,
                     std::string_view,
                     std::string_view       = {},
                     const hhds::Pin_class& = {},
                     std::string_view       = {}) { throw std::runtime_error("unsupported test cell"); };
    auto shift  = [](const hhds::Node_class&, std::string_view, const Dlop&, const hhds::Pin_class&) {
      throw std::runtime_error("unsupported test shift");
    };
    livehd::synth::blast_comb(f.mux, 8, slots, ops, read, {}, {}, {}, refuse, shift);
    for (const int* value : slots) {
      EXPECT_EQ(*value, input != 0) << input;
    }
  }
}

namespace {
struct Select_fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, y, w, clk;
  explicit Select_fixture(std::string_view name) {
    auto io = lib.create_io(name);
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_input("y", 2);
    io->set_bits("y", 8);
    io->add_input("w", 3);
    io->set_bits("w", 32);
    io->add_input("clk", 4);
    io->set_bits("clk", 1);
    io->add_output("o", 5);
    io->set_bits("o", 8);
    io->add_output("p", 6);
    io->set_bits("p", 9);
    g   = io->create_graph();
    x   = input("x", 8);
    y   = input("y", 8);
    w   = input("w", 32);
    clk = input("clk", 1);
  }
  // The IO declaration and the pin must agree: cvc5 seeds an input from its
  // declaration, ABC's cone reads the pin.
  hhds::Pin_class input(std::string_view name, int bits) {
    g->get_io()->set_unsign(name, true);
    auto p = g->get_input_pin(name);
    gu::set_ubits(p, bits);
    return p;
  }
  hhds::Pin_class konst(int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  hhds::Pin_class op(Ntype_op kind, std::initializer_list<std::pair<hhds::Port_id, hhds::Pin_class>> ins, int bits) {
    auto n = gu::create_typed_node(*g, kind);
    for (const auto& [pid, p] : ins) {
      p.connect_sink(gu::setup_sink_pid(n, pid));
    }
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, bits);
    return out;
  }
  // x + 1 never equals x and is always greater than it: cprop sees neither.
  hhds::Pin_class next() { return op(Ntype_op::Sum, {{0, x}, {0, konst(1)}}, 9); }
  hhds::Pin_class never() { return op(Ntype_op::EQ, {{0, x}, {0, next()}}, 1); }
  hhds::Pin_class always() { return op(Ntype_op::LT, {{0, x}, {1, next()}}, 1); }
  hhds::Node_class mux(const hhds::Pin_class& sel) {
    auto m = gu::create_typed_node(*g, Ntype_op::Mux);
    sel.connect_sink(m.create_sink_pin(0));
    x.connect_sink(m.create_sink_pin(1));
    y.connect_sink(m.create_sink_pin(2));
    auto out = m.create_driver_pin(0);
    gu::set_ubits(out, 8);
    out.connect_sink(g->get_output_pin("o"));
    return m;
  }
  hhds::Node_class flop(const hhds::Pin_class& en) {
    auto f = gu::create_typed_node(*g, Ntype_op::Flop);
    clk.connect_sink(f.create_sink_pin(2));
    x.connect_sink(f.create_sink_pin(3));
    en.connect_sink(f.create_sink_pin(4));
    auto q = f.create_driver_pin(0);
    gu::set_ubits(q, 8);
    q.connect_sink(g->get_output_pin("o"));
    return f;
  }
  int count(Ntype_op kind) const {
    int n = 0;
    for (const auto node : g->body().nodes()) {
      n += gu::type_op_of(node) == kind;
    }
    return n;
  }
};
hhds::Pin_class node_op(hhds::Graph& g, Ntype_op kind, std::initializer_list<std::pair<hhds::Port_id, hhds::Pin_class>> ins,
                        int bits) {
  auto n = gu::create_typed_node(g, kind);
  for (const auto& [pid, p] : ins) {
    p.connect_sink(gu::setup_sink_pid(n, pid));
  }
  auto out = n.create_driver_pin(0);
  gu::set_ubits(out, bits);
  return out;
}
// A callee `name(a:u8) -> (q:u<bits>)`; `body` returns what drives q.
template <class Body>
std::shared_ptr<hhds::GraphIO> callee(hhds::GraphLibrary& lib, std::string_view name, int bits, const Body& body) {
  auto io = lib.create_io(name);
  io->add_input("a", 1);
  io->set_bits("a", 8);
  io->set_unsign("a", true);
  io->add_output("q", 2);
  io->set_bits("q", bits);
  io->set_unsign("q", true);
  auto g = io->create_graph();
  auto a = g->get_input_pin("a");
  gu::set_ubits(a, 8);
  body(*g, a).connect_sink(g->get_output_pin("q"));
  return io;
}
hhds::Pin_class instance(hhds::Graph& g, const std::shared_ptr<hhds::GraphIO>& io, const hhds::Pin_class& a, int bits) {
  auto sub = gu::create_typed_node(g, Ntype_op::Sub);
  sub.set_subnode(io);
  a.connect_sink(sub.create_sink_pin(io->get_input_port_id("a")));
  auto q = sub.create_driver_pin(io->get_output_port_id("q"));
  gu::set_ubits(q, bits);
  return q;
}
bool is_const(const hhds::Node_class& n, hhds::Port_id pid, int64_t value) {
  const auto d = n.create_sink_pin(pid).get_driver_pin();
  return d.is_const() && gu::const_of(d).is_known_eq(*Dlop::create_integer(value));
}
}  // namespace

TEST(SatoptSelect, NeverTrueMuxSelectIsTiedAndItsDeadConeDeleted) {
  Select_fixture f("select_never");
  auto           sum = f.next();
  gu::set_ubits(sum, 9);
  sum.connect_sink(f.g->get_output_pin("p"));
  auto sel = f.op(Ntype_op::EQ, {{0, f.x}, {0, sum}}, 1);
  auto m   = f.mux(sel);
  auto s   = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.proven, 1);
  EXPECT_EQ(s.muxes, 1);
  EXPECT_TRUE(is_const(m, 0, 0));
  EXPECT_TRUE(m.create_sink_pin(1).get_driver_pin() == f.x);
  EXPECT_TRUE(is_const(m, 2, 0));  // the never-selected arm no longer reads y
  EXPECT_EQ(f.count(Ntype_op::EQ), 0);
  EXPECT_EQ(f.count(Ntype_op::Sum), 1);  // still drives output p
}

TEST(SatoptSelect, AlwaysTrueHotmuxControlWinsOverLaterArms) {
  Select_fixture f("select_hotmux");
  auto           hot = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
  f.op(Ntype_op::Ror, {{0, f.y}}, 1).connect_sink(hot.create_sink_pin(0));
  f.x.connect_sink(hot.create_sink_pin(1));
  f.always().connect_sink(hot.create_sink_pin(2));
  f.y.connect_sink(hot.create_sink_pin(3));
  f.op(Ntype_op::Ror, {{0, f.x}}, 1).connect_sink(hot.create_sink_pin(4));
  f.konst(7).connect_sink(hot.create_sink_pin(5));
  f.x.connect_sink(hot.create_sink_pin(6));
  auto out = hot.create_driver_pin(0);
  gu::set_ubits(out, 8);
  out.connect_sink(f.g->get_output_pin("o"));
  auto s = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.proven, 1);
  EXPECT_EQ(s.hotmux_arms, 2);
  // The earlier free arm keeps priority over the always-on one.
  EXPECT_FALSE(hot.create_sink_pin(0).get_driver_pin().is_const());
  EXPECT_TRUE(hot.create_sink_pin(1).get_driver_pin() == f.x);
  EXPECT_TRUE(is_const(hot, 2, 1));
  EXPECT_TRUE(hot.create_sink_pin(3).get_driver_pin() == f.y);
  EXPECT_TRUE(is_const(hot, 4, 0));
  EXPECT_TRUE(is_const(hot, 5, 0));
  EXPECT_TRUE(is_const(hot, 6, 0));  // fallback
  EXPECT_EQ(f.count(Ntype_op::LT), 0);
  EXPECT_EQ(f.count(Ntype_op::Ror), 1);
}

TEST(SatoptSelect, BodylessSubOutputStays) {
  Select_fixture f("select_sub");
  auto           child = f.lib.create_io("select_sub_child");
  child->add_output("q", 1);
  child->set_bits("q", 8);
  child->set_unsign("q", true);
  auto sub = gu::create_typed_node(*f.g, Ntype_op::Sub);
  sub.set_subnode(child);
  auto q = sub.create_driver_pin(1);
  gu::set_ubits(q, 8);
  // No body to descend into, and cvc5 cannot encode an opaque output.
  auto next = f.op(Ntype_op::Sum, {{0, q}, {0, f.konst(1)}}, 9);
  auto m    = f.mux(f.op(Ntype_op::EQ, {{0, q}, {0, next}}, 1));
  auto s    = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.survivors, 1);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
}

TEST(SatoptSelect, CalleeBodyIsDescended) {
  Select_fixture f("select_callee");
  auto           inc = callee(f.lib, "select_callee_inc", 9, [](hhds::Graph& g, const hhds::Pin_class& a) {
    return node_op(g, Ntype_op::Sum, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(1))}}, 9);
  });
  // x < inc(a=x).q holds only through the callee's body and its input binding.
  auto m = f.mux(f.op(Ntype_op::LT, {{0, f.x}, {1, instance(*f.g, inc, f.x, 9)}}, 1));
  auto s = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.proven, 1);
  EXPECT_TRUE(is_const(m, 0, 1));
  EXPECT_TRUE(is_const(m, 1, 0));
}

TEST(SatoptSelect, CalleeConstantPassesTheSeeds) {
  Select_fixture f("select_callee_const");
  auto           zero = callee(f.lib, "select_callee_zero", 8, [](hhds::Graph& g, const hhds::Pin_class& a) {
    return node_op(g, Ntype_op::And, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(0))}}, 8);
  });
  // A free (undescended) callee output would be random, so the seeds would
  // reject this before cvc5 saw it.
  auto q = instance(*f.g, zero, f.x, 8);
  auto m = f.mux(f.op(Ntype_op::EQ, {{0, q}, {0, f.konst(0)}}, 1));
  auto s = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.survivors, 1);
  EXPECT_EQ(s.proven, 1);
  EXPECT_TRUE(is_const(m, 0, 1));
}

TEST(SatoptSelect, InstancesKeepSeparateState) {
  Select_fixture f("select_callee_state");
  auto           reg = callee(f.lib, "select_callee_reg", 8, [](hhds::Graph& g, const hhds::Pin_class& a) {
    auto flop = gu::create_typed_node(g, Ntype_op::Flop);
    a.connect_sink(flop.create_sink_pin(3));
    auto q = flop.create_driver_pin(0);
    gu::set_ubits(q, 8);
    return q;
  });
  auto q1 = instance(*f.g, reg, f.x, 8);
  auto q2 = instance(*f.g, reg, f.x, 8);
  auto eq = f.op(Ntype_op::EQ, {{0, q1}, {0, q2}}, 1);
  auto same = f.op(Ntype_op::EQ, {{0, q1}, {0, q1}}, 1);
  // Same definition, same input, two registers: never assumed equal.
  livehd::formal::Prover p(f.g.get(), {.descend_subs = true});
  EXPECT_EQ(p.is_true(eq).verdict, livehd::formal::Verdict::Refuted);
  EXPECT_EQ(p.is_true(same).verdict, livehd::formal::Verdict::Proven);
  // Without descent the callee stays opaque.
  livehd::formal::Prover flat(f.g.get());
  EXPECT_EQ(flat.is_true(same).verdict, livehd::formal::Verdict::Unknown);
}

TEST(SatoptSelect, CalleeEditInvalidatesReuse) {
  const auto dir   = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_select_callee";
  auto       build = [](Select_fixture& f, bool increment) {
    auto body = callee(f.lib, "select_edit_callee", 9, [increment](hhds::Graph& g, const hhds::Pin_class& a) {
      return increment ? node_op(g, Ntype_op::Sum, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(1))}}, 9)
                       : node_op(g, Ntype_op::Sum, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(0))}}, 9);
    });
    return f.mux(f.op(Ntype_op::LT, {{0, f.x}, {1, instance(*f.g, body, f.x, 9)}}, 1));
  };
  Select_fixture first("select_edit");
  build(first, true);
  EXPECT_EQ(livehd::satopt::optimize_selects({first.g}, dir).proven, 1);
  // Same parent, edited callee (x < x + 0 is never true): nothing is reused.
  Select_fixture second("select_edit");
  auto           m = build(second, false);
  auto           s = livehd::satopt::optimize_selects({second.g}, dir);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 1);
  EXPECT_TRUE(is_const(m, 0, 0));
}

// Switching which definition an instance calls invalidates reuse even when
// every recorded callee is unchanged: x < plus1(x) is always true, x <
// plus0(x) never.
TEST(SatoptSelect, CalleeSwitchInvalidatesReuse) {
  const auto dir   = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_select_switch";
  auto       build = [](Select_fixture& f, bool plus1) {
    auto one  = callee(f.lib, "switch_plus1", 9, [](hhds::Graph& g, const hhds::Pin_class& a) {
      return node_op(g, Ntype_op::Sum, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(1))}}, 9);
    });
    auto zero = callee(f.lib, "switch_plus0", 9, [](hhds::Graph& g, const hhds::Pin_class& a) {
      return node_op(g, Ntype_op::Sum, {{0, a}, {0, gu::create_const(g, *Dlop::create_integer(0))}}, 9);
    });
    return f.mux(f.op(Ntype_op::LT, {{0, f.x}, {1, instance(*f.g, plus1 ? one : zero, f.x, 9)}}, 1));
  };
  Select_fixture first("select_switch");
  auto           a = build(first, true);
  EXPECT_EQ(livehd::satopt::optimize_selects({first.g}, dir).proven, 1);
  EXPECT_TRUE(is_const(a, 0, 1));
  Select_fixture second("select_switch");
  auto           b = build(second, false);
  const auto     s = livehd::satopt::optimize_selects({second.g}, dir);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 1);
  EXPECT_TRUE(is_const(b, 0, 0));
}

// A reused row charges what its search cost, so a warm run leaves the same
// budget as a cold one; a budget that cannot afford it searches again and the
// complete row survives for a later run.
TEST(SatoptBudget, ReuseReplaysTheSearchCost) {
  const auto dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_budget_replay";
  const auto run = [&](livehd::satopt::Meter& m) {
    Select_fixture f("budget_replay");
    f.mux(f.never());
    m.begin_stage(0);
    return livehd::satopt::optimize_selects({f.g}, dir, livehd::satopt::Profile::synthesis, nullptr, &m);
  };
  livehd::satopt::Meter cold(livehd::satopt::Budget{});
  EXPECT_EQ(run(cold).reused, 0);
  livehd::satopt::Meter warm(livehd::satopt::Budget{});
  EXPECT_EQ(run(warm).reused, 1);
  EXPECT_GT(cold.work_done(), 0u);
  EXPECT_EQ(warm.work_done(), cold.work_done());
  EXPECT_EQ(warm.queries_done(), cold.queries_done());
  livehd::satopt::Budget none;
  none.queries = 0;
  livehd::satopt::Meter starved(none);
  const auto            s = run(starved);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 0);
  livehd::satopt::Meter again(livehd::satopt::Budget{});
  EXPECT_EQ(run(again).reused, 1);
}

TEST(SatoptSelect, WrappingSumIsNotAlwaysGreater) {
  Select_fixture f("select_wrap");
  // Stamped u8, so x + 1 wraps to 0 at x == 255: the all-ones corner pattern
  // rejects it before any query.
  auto                         sum = f.op(Ntype_op::Sum, {{0, f.x}, {0, f.konst(1)}}, 8);
  auto                         m   = f.mux(f.op(Ntype_op::LT, {{0, f.x}, {1, sum}}, 1));
  livehd::satopt::Stage_report report;
  auto s = livehd::satopt::optimize_selects({f.g}, {}, livehd::satopt::Profile::synthesis, &report);
  EXPECT_EQ(s.survivors, 0);
  EXPECT_EQ(report.sim_rejects, 1u);
  EXPECT_EQ(report.queries, 0u);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
}

// Random and corner patterns all miss w == K; the prover refutes the first
// such select, and its counterexample (w = K) becomes a simulated column that
// rejects the second one without a query.
TEST(SatoptSelect, CounterexampleRejectsTheNextCandidate) {
  Select_fixture f("select_counterexample");
  auto           first = f.mux(f.op(Ntype_op::EQ, {{0, f.w}, {0, f.konst(0x12345678)}}, 1));
  auto           twin  = gu::create_typed_node(*f.g, Ntype_op::Mux);
  f.op(Ntype_op::EQ, {{0, f.konst(0x12345678)}, {0, f.w}}, 1).connect_sink(twin.create_sink_pin(0));
  f.x.connect_sink(twin.create_sink_pin(1));
  f.y.connect_sink(twin.create_sink_pin(2));
  gu::set_ubits(twin.create_driver_pin(0), 9);
  twin.create_driver_pin(0).connect_sink(f.g->get_output_pin("p"));
  livehd::satopt::Stage_report report;
  auto s = livehd::satopt::optimize_selects({f.g}, {}, livehd::satopt::Profile::synthesis, &report);
  EXPECT_EQ(s.survivors, 2);
  EXPECT_EQ(report.queries, 1u);
  EXPECT_EQ(report.refuted, 1u);
  EXPECT_EQ(report.sim_rejects, 1u);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(first.create_sink_pin(0).get_driver_pin().is_const());
  EXPECT_FALSE(twin.create_sink_pin(0).get_driver_pin().is_const());
}

TEST(SatoptSelect, FreeAndRarelyTrueSelectsStay) {
  Select_fixture f("select_free");
  // All eight seeds miss w == K, so only the prover can reject it.
  auto m = f.mux(f.op(Ntype_op::EQ, {{0, f.w}, {0, f.konst(0x12345678)}}, 1));
  auto s = livehd::satopt::optimize_selects({f.g});
  EXPECT_EQ(s.survivors, 1);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
  EXPECT_TRUE(m.create_sink_pin(2).get_driver_pin() == f.y);
  EXPECT_EQ(f.count(Ntype_op::EQ), 1);
}

TEST(SatoptSelect, FlopEnablesAreTied) {
  Select_fixture on("select_enable_on");
  auto           a = on.flop(on.always());
  EXPECT_EQ(livehd::satopt::optimize_selects({on.g}).enables, 1);
  EXPECT_TRUE(is_const(a, 4, 1));
  EXPECT_EQ(on.count(Ntype_op::LT), 0);
  Select_fixture off("select_enable_off");
  auto           b = off.flop(off.never());
  EXPECT_EQ(livehd::satopt::optimize_selects({off.g}).enables, 1);
  EXPECT_TRUE(is_const(b, 4, 0));
  EXPECT_EQ(off.count(Ntype_op::EQ), 0);
}

TEST(SatoptSelect, ProofsAreReusedAcrossRuns) {
  const auto     dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_select";
  Select_fixture first("select_reuse");
  first.mux(first.never());
  auto cold = livehd::satopt::optimize_selects({first.g}, dir);
  EXPECT_EQ(cold.proven, 1);
  EXPECT_EQ(cold.reused, 0);
  Select_fixture second("select_reuse");
  auto           m    = second.mux(second.never());
  auto           warm = livehd::satopt::optimize_selects({second.g}, dir);
  EXPECT_EQ(warm.reused, 1);
  EXPECT_EQ(warm.survivors, 0);
  EXPECT_TRUE(is_const(m, 0, 0));
}

// A search the budget cut short keeps (and applies) what it proved, but its
// proof row is partial: a later run searches again instead of reusing it.
TEST(SatoptBudget, PartialSearchIsNeverReusedAsComplete) {
  const auto dir   = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_budget_partial";
  auto       build = [](Select_fixture& f) {
    auto first  = f.mux(f.never());
    auto second = gu::create_typed_node(*f.g, Ntype_op::Mux);
    f.always().connect_sink(second.create_sink_pin(0));
    f.x.connect_sink(second.create_sink_pin(1));
    f.y.connect_sink(second.create_sink_pin(2));
    gu::set_ubits(second.create_driver_pin(0), 9);
    second.create_driver_pin(0).connect_sink(f.g->get_output_pin("p"));
    return std::pair{first, second};
  };
  livehd::satopt::Budget one;
  one.queries = 1;
  Select_fixture         starved("budget_partial");
  auto [a, b]  = build(starved);
  livehd::satopt::Meter        meter(one);
  livehd::satopt::Stage_report report;
  meter.begin_stage(0);
  auto s = livehd::satopt::optimize_selects({starved.g}, dir, livehd::satopt::Profile::synthesis, &report, &meter);
  EXPECT_EQ(s.proven, 1);
  EXPECT_EQ(report.budget_skips, 1u);
  EXPECT_TRUE(meter.exhausted());
  EXPECT_NE(a.create_sink_pin(0).get_driver_pin().is_const(), b.create_sink_pin(0).get_driver_pin().is_const());

  Select_fixture full("budget_partial");
  auto [c, d] = build(full);
  s           = livehd::satopt::optimize_selects({full.g}, dir);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 2);
  EXPECT_TRUE(is_const(c, 0, 0));
  EXPECT_TRUE(is_const(d, 0, 1));

  Select_fixture warm("budget_partial");
  build(warm);
  EXPECT_EQ(livehd::satopt::optimize_selects({warm.g}, dir).reused, 2);
}

// The proof-relevant options are part of the reuse key.
TEST(SatoptBudget, SolverOptionsAreInTheReuseKey) {
  const auto     dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_budget_options";
  Select_fixture first("budget_options");
  first.mux(first.never());
  EXPECT_EQ(livehd::satopt::optimize_selects({first.g}, dir).proven, 1);
  livehd::satopt::Budget other;
  other.budget_k = 128;
  livehd::satopt::Meter meter(other);
  meter.begin_stage(0);
  Select_fixture second("budget_options");
  second.mux(second.never());
  const auto s = livehd::satopt::optimize_selects({second.g}, dir, livehd::satopt::Profile::synthesis, nullptr, &meter);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 1);
}

// Synthesis deletes a Hotmux nobody reads (compile keeps it for its `unique
// if` obligation) together with the cone that fed only it; live logic stays.
TEST(Satopt, DropDeadLogicRemovesAnUnreadHotmuxCone) {
  Fixture f("satopt_drop_dead");
  auto    control = gu::create_typed_node(*f.g, Ntype_op::And);
  f.x.connect_sink(gu::setup_sink_pid(control, 0));
  f.x.connect_sink(gu::setup_sink_pid(control, 0));
  gu::set_ubits(control.create_driver_pin(0), 8);
  auto dead = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
  control.create_driver_pin(0).connect_sink(dead.create_sink_pin(0));
  f.x.connect_sink(dead.create_sink_pin(1));
  f.x.connect_sink(dead.create_sink_pin(2));
  EXPECT_EQ(livehd::satopt::drop_dead_logic(f.g.get()), 2u);
  size_t hotmuxes = 0, muxes = 0;
  for (const auto n : f.g->body().nodes()) {
    hotmuxes += gu::type_op_of(n) == Ntype_op::Hotmux;
    muxes += gu::type_op_of(n) == Ntype_op::Mux;
  }
  EXPECT_EQ(hotmuxes, 0u);
  EXPECT_EQ(muxes, 1u);  // the live mux driving y
  EXPECT_EQ(livehd::satopt::drop_dead_logic(f.g.get()), 0u);
}

// ---- Shared profile (B): the committed optimizer keeps every observable check.

// The always-true control is tied to the 1 it always carries, but a later
// control the obligation reads stays: only the never-selected DATA goes.
TEST(SatoptShared, AlwaysTrueHotmuxControlKeepsLaterControls) {
  Select_fixture f("shared_hotmux");
  auto           hot = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
  f.op(Ntype_op::Ror, {{0, f.y}}, 1).connect_sink(hot.create_sink_pin(0));
  f.x.connect_sink(hot.create_sink_pin(1));
  f.always().connect_sink(hot.create_sink_pin(2));
  f.y.connect_sink(hot.create_sink_pin(3));
  const auto later = f.op(Ntype_op::Ror, {{0, f.x}}, 1);
  later.connect_sink(hot.create_sink_pin(4));
  f.konst(7).connect_sink(hot.create_sink_pin(5));
  f.x.connect_sink(hot.create_sink_pin(6));
  auto out = hot.create_driver_pin(0);
  gu::set_ubits(out, 8);
  out.connect_sink(f.g->get_output_pin("o"));
  auto s = livehd::satopt::optimize_selects({f.g}, {}, livehd::satopt::Profile::shared);
  EXPECT_EQ(s.proven, 1);
  EXPECT_FALSE(hot.create_sink_pin(0).get_driver_pin().is_const());
  EXPECT_TRUE(is_const(hot, 2, 1));
  EXPECT_TRUE(hot.create_sink_pin(3).get_driver_pin() == f.y);
  EXPECT_TRUE(hot.create_sink_pin(4).get_driver_pin() == later);  // the overlap check still reads it
  EXPECT_TRUE(is_const(hot, 5, 0));                               // never-selected data
  EXPECT_TRUE(is_const(hot, 6, 0));                               // fallback data
  EXPECT_EQ(f.count(Ntype_op::Ror), 2);
}

// A Hotmux that loses its consumer keeps its controls (the `unique if`
// obligation); the synthesis copy deletes it with its cone.
TEST(SatoptShared, SweepKeepsAHotmuxThatLostItsReader) {
  for (const auto profile : {livehd::satopt::Profile::shared, livehd::satopt::Profile::synthesis}) {
    Select_fixture f(profile == livehd::satopt::Profile::shared ? "shared_dead_hotmux" : "synth_dead_hotmux");
    auto           hot = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
    f.op(Ntype_op::Ror, {{0, f.x}}, 1).connect_sink(hot.create_sink_pin(0));
    f.x.connect_sink(hot.create_sink_pin(1));
    f.y.connect_sink(hot.create_sink_pin(2));
    auto hout = hot.create_driver_pin(0);
    gu::set_ubits(hout, 8);
    auto m = gu::create_typed_node(*f.g, Ntype_op::Mux);  // never selects the Hotmux arm
    f.never().connect_sink(m.create_sink_pin(0));
    f.x.connect_sink(m.create_sink_pin(1));
    hout.connect_sink(m.create_sink_pin(2));
    auto mout = m.create_driver_pin(0);
    gu::set_ubits(mout, 8);
    mout.connect_sink(f.g->get_output_pin("o"));
    auto s = livehd::satopt::optimize_selects({f.g}, {}, profile);
    EXPECT_EQ(s.proven, 1);
    EXPECT_TRUE(is_const(m, 2, 0));
    EXPECT_EQ(f.count(Ntype_op::Hotmux), profile == livehd::satopt::Profile::shared ? 1 : 0);
  }
}

// A cone with an unstamped width proves nothing in the shared profile: the
// prover would reason about a one-bit guess of the value.
TEST(SatoptShared, UnstampedWidthGivesNoRewrite) {
  Select_fixture f("shared_unstamped");
  auto           sum = gu::create_typed_node(*f.g, Ntype_op::Sum);  // x + 1, no bits stamped
  f.x.connect_sink(gu::setup_sink_pid(sum, 0));
  f.konst(1).connect_sink(gu::setup_sink_pid(sum, 0));
  auto sel = f.op(Ntype_op::EQ, {{0, f.x}, {0, sum.create_driver_pin(0)}}, 1);
  auto m   = f.mux(sel);
  auto s   = livehd::satopt::optimize_selects({f.g}, {}, livehd::satopt::Profile::shared);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
}

namespace {
// o = mux(s, hotmux(c0, x, c1, y, 0), 0) with c0 = x < 4 and c1 = x > 8:
// exclusive, but not the EQ decode cprop proves on its own. The shared 0 is
// what makes one flat selection cheaper than the nest (cprop's gain rule).
struct Collapse_fixture : Select_fixture {
  hhds::Node_class hot, outer;
  explicit Collapse_fixture(std::string_view name, bool overlap) : Select_fixture(name) {
    const auto c0 = op(Ntype_op::LT, {{0, x}, {1, konst(4)}}, 1);
    const auto c1 = overlap ? op(Ntype_op::LT, {{0, x}, {1, konst(8)}}, 1) : op(Ntype_op::GT, {{0, x}, {1, konst(8)}}, 1);
    hot           = gu::create_typed_node(*g, Ntype_op::Hotmux);
    c0.connect_sink(hot.create_sink_pin(0));
    x.connect_sink(hot.create_sink_pin(1));
    c1.connect_sink(hot.create_sink_pin(2));
    y.connect_sink(hot.create_sink_pin(3));
    konst(0).connect_sink(hot.create_sink_pin(4));
    gu::set_ubits(hot.create_driver_pin(0), 8);
    const auto s = op(Ntype_op::EQ, {{0, y}, {0, konst(7)}}, 1);
    outer        = gu::create_typed_node(*g, Ntype_op::Mux);
    s.connect_sink(outer.create_sink_pin(0));
    konst(0).connect_sink(outer.create_sink_pin(1));
    hot.create_driver_pin(0).connect_sink(outer.create_sink_pin(2));
    gu::set_ubits(outer.create_driver_pin(0), 8);
    outer.create_driver_pin(0).connect_sink(g->get_output_pin("o"));
  }
};
}  // namespace

// I: a provably exclusive Hotmux is stamped proven and absorbed into the
// enclosing selection; the output keeps every value.
TEST(SatoptCollapse, ExclusiveHotmuxIsAbsorbed) {
  Collapse_fixture f("collapse_exclusive", false);
  const auto       before = livehd::satopt::Satopt_seeds{}.sample(f.g->get_output_pin("o").get_driver_pin());
  ASSERT_TRUE(before.has_value());
  const int  selections = f.count(Ntype_op::Mux) + f.count(Ntype_op::Hotmux);
  const auto s          = livehd::satopt::collapse_hotmuxes(f.g.get(), livehd::satopt::Profile::shared);
  EXPECT_EQ(s.proven, 1u);
  EXPECT_LT(f.count(Ntype_op::Mux) + f.count(Ntype_op::Hotmux), selections);
  const auto after = livehd::satopt::Satopt_seeds{}.sample(f.g->get_output_pin("o").get_driver_pin());
  ASSERT_TRUE(after.has_value());
  for (size_t i = 0; i < before->size(); ++i) {
    EXPECT_TRUE((*before)[i].is_known_eq((*after)[i])) << i;
  }
}

// I: an overlapping `unique if` keeps its obligation and its region.
TEST(SatoptCollapse, OverlappingHotmuxStays) {
  Collapse_fixture f("collapse_overlap", true);
  const auto       s = livehd::satopt::collapse_hotmuxes(f.g.get(), livehd::satopt::Profile::shared);
  EXPECT_EQ(s.proven, 0u);
  EXPECT_EQ(s.refuted, 1u);
  EXPECT_NE(gu::proven_of(f.hot), gu::kFormalOnehot);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
}

TEST(SatoptCollapse, CoordinatorCollapsesWithoutAnAbcProver) {
  ASSERT_EQ(livehd::satopt::registered_mux_prover(), nullptr);
  Collapse_fixture        f("collapse_no_abc", false);
  const int               selections = f.count(Ntype_op::Mux) + f.count(Ntype_op::Hotmux);
  livehd::satopt::Options opts;
  opts.stages        = {livehd::satopt::Stage::hotmux};
  const auto  report = livehd::satopt::run(f.g.get(), opts);
  const auto& stage  = report.at(livehd::satopt::Stage::hotmux);
  EXPECT_EQ(stage.state, livehd::satopt::Stage_state::completed);
  EXPECT_EQ(stage.proven, 1u);
  EXPECT_LT(f.count(Ntype_op::Mux) + f.count(Ntype_op::Hotmux), selections);
}

// The sweep names the driver pins of every cell it deletes (a cache keyed by
// pin must forget them), although a deleted cell lists none any more.
TEST(Satopt, SweepRecordsDeletedPins) {
  Select_fixture f("sweep_deleted_pins");
  const auto     inner = f.op(Ntype_op::Xor, {{0, f.x}, {0, f.y}}, 8);
  const auto     outer = f.op(Ntype_op::And, {{0, inner}, {0, f.konst(15)}}, 8);
  outer.connect_sink(f.g->get_output_pin("o"));
  gu::drop_drivers(f.g->get_output_pin("o"));
  f.x.connect_sink(f.g->get_output_pin("o"));
  livehd::satopt::detail::Select_rewrite rw{*f.g, {outer}};
  rw.sweep();
  EXPECT_EQ(rw.removed, 2u);
  ASSERT_EQ(rw.deleted_pins.size(), 2u);
  EXPECT_TRUE((rw.deleted_pins[0] == outer && rw.deleted_pins[1] == inner) || (rw.deleted_pins[0] == inner && rw.deleted_pins[1] == outer));
}
