// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <cstdlib>
#include <stdexcept>

#include "abc_blast.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"

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
bool has(const livehd::abc::Satopt_result& result, const hhds::Node_class& n, int arm, int bit, livehd::abc::Mux_fact::Kind kind) {
  auto it = result.mux.find(static_cast<uint64_t>(n.get_debug_nid()));
  if (it == result.mux.end()) {
    return false;
  }
  for (const auto& f : it->second) {
    if (f.arm == arm && f.bit == bit && f.kind == kind) {
      return true;
    }
  }
  return false;
}
}  // namespace
TEST(Satopt, CrossRegionConditionalZerosAndExactReuse) {
  Fixture    f("satopt_conditional");
  const auto dir    = std::string(std::getenv("TEST_TMPDIR")) + "/satopt";
  auto       result = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_GT(result->survivors, 0);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_TRUE(livehd::abc::satopt(f.g.get(), dir)->reused);
  // Change a producer across the boundary while leaving the mux untouched.
  auto red = f.s.get_master_node();
  gu::set_type_op(red, Ntype_op::Not);
  auto changed = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_FALSE(changed->reused);
  EXPECT_FALSE(has(*changed, f.mux, 0, 7, livehd::abc::Mux_fact::Kind::zero));
}
TEST(Satopt, InRegionFactsAreNotCandidates) {
  Fixture f("satopt_one_region");
  gu::set_color(f.s.get_master_node(), 2);
  auto result = livehd::abc::satopt(f.g.get());
  EXPECT_EQ(result->candidates, 0);
  auto explicit_run = livehd::abc::satopt(f.g.get(), {}, true);
  EXPECT_GT(explicit_run->proven, 0);
}
TEST(Satopt, IndependentFreeInputsDoNotBecomeConstants) {
  Fixture f("satopt_free");
  gu::drop_drivers(f.mux.create_sink_pin(0));
  f.x.connect_sink(f.mux.create_sink_pin(0));
  // A nonzero 8-bit selector must see bit 7, not just bit 0.
  auto result = livehd::abc::satopt(f.g.get(), {}, true);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_FALSE(has(*result, f.mux, 1, 7, livehd::abc::Mux_fact::Kind::zero));
}

TEST(Satopt, ComplementedArmsAreProven) {
  Fixture f("satopt_complement");
  auto    inv = gu::create_typed_node(*f.g, Ntype_op::Not);
  f.x.connect_sink(inv.create_sink_pin(0));
  auto value = inv.create_driver_pin(0);
  gu::set_ubits(value, 8);
  gu::set_color(inv, 1);
  gu::drop_drivers(f.mux.create_sink_pin(2));
  value.connect_sink(f.mux.create_sink_pin(2));
  auto result = livehd::abc::satopt(f.g.get());
  for (int bit = 0; bit < 8; ++bit) {
    EXPECT_TRUE(has(*result, f.mux, 1, bit, livehd::abc::Mux_fact::Kind::complement));
  }
}

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
    livehd::abc::blast_comb(f.mux, 8, slots, ops, read, {}, {}, {}, refuse, shift);
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
  auto s   = livehd::abc::optimize_selects({f.g});
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
  auto s = livehd::abc::optimize_selects({f.g});
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
  auto s    = livehd::abc::optimize_selects({f.g});
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
  auto s = livehd::abc::optimize_selects({f.g});
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
  auto s = livehd::abc::optimize_selects({f.g});
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
  EXPECT_EQ(livehd::abc::optimize_selects({first.g}, dir).proven, 1);
  // Same parent, edited callee (x < x + 0 is never true): nothing is reused.
  Select_fixture second("select_edit");
  auto           m = build(second, false);
  auto           s = livehd::abc::optimize_selects({second.g}, dir);
  EXPECT_EQ(s.reused, 0);
  EXPECT_EQ(s.proven, 1);
  EXPECT_TRUE(is_const(m, 0, 0));
}

TEST(SatoptSelect, WrappingSumIsNotAlwaysGreater) {
  Select_fixture f("select_wrap");
  // Stamped u8, so x + 1 wraps to 0 at x == 255. The seeds all miss it; cvc5
  // refutes it.
  auto sum = f.op(Ntype_op::Sum, {{0, f.x}, {0, f.konst(1)}}, 8);
  auto m   = f.mux(f.op(Ntype_op::LT, {{0, f.x}, {1, sum}}, 1));
  auto s   = livehd::abc::optimize_selects({f.g});
  EXPECT_EQ(s.survivors, 1);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
}

TEST(SatoptSelect, FreeAndRarelyTrueSelectsStay) {
  Select_fixture f("select_free");
  // All eight seeds miss w == K, so only the prover can reject it.
  auto m = f.mux(f.op(Ntype_op::EQ, {{0, f.w}, {0, f.konst(0x12345678)}}, 1));
  auto s = livehd::abc::optimize_selects({f.g});
  EXPECT_EQ(s.survivors, 1);
  EXPECT_EQ(s.proven, 0);
  EXPECT_FALSE(m.create_sink_pin(0).get_driver_pin().is_const());
  EXPECT_TRUE(m.create_sink_pin(2).get_driver_pin() == f.y);
  EXPECT_EQ(f.count(Ntype_op::EQ), 1);
}

TEST(SatoptSelect, FlopEnablesAreTied) {
  Select_fixture on("select_enable_on");
  auto           a = on.flop(on.always());
  EXPECT_EQ(livehd::abc::optimize_selects({on.g}).enables, 1);
  EXPECT_TRUE(is_const(a, 4, 1));
  EXPECT_EQ(on.count(Ntype_op::LT), 0);
  Select_fixture off("select_enable_off");
  auto           b = off.flop(off.never());
  EXPECT_EQ(livehd::abc::optimize_selects({off.g}).enables, 1);
  EXPECT_TRUE(is_const(b, 4, 0));
  EXPECT_EQ(off.count(Ntype_op::EQ), 0);
}

TEST(SatoptSelect, ProofsAreReusedAcrossRuns) {
  const auto     dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_select";
  Select_fixture first("select_reuse");
  first.mux(first.never());
  auto cold = livehd::abc::optimize_selects({first.g}, dir);
  EXPECT_EQ(cold.proven, 1);
  EXPECT_EQ(cold.reused, 0);
  Select_fixture second("select_reuse");
  auto           m    = second.mux(second.never());
  auto           warm = livehd::abc::optimize_selects({second.g}, dir);
  EXPECT_EQ(warm.reused, 1);
  EXPECT_EQ(warm.survivors, 0);
  EXPECT_TRUE(is_const(m, 0, 0));
}
