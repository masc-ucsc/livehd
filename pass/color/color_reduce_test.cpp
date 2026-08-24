// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.color `reduce`: repeated-cone extraction. The property under test is the
// TRANSFORM CONTRACT: an extracted pattern must be instantiated with a
// WITNESSED port correspondence (role-correct wiring at every site, however the
// occurrences were spelled or ordered), sites below min_count must be left
// byte-identical, and the splice must never orphan a reader or leak a node.

#include "color_reduce.hpp"

#include <string>
#include <vector>

#include "cell.hpp"
#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::color_reduce;
using livehd::color::Reduce_opts;
using livehd::color::Reduce_stats;
using livehd::graph_util::create_typed_node;

namespace gu = livehd::graph_util;

namespace {

Reduce_opts small_opts() {
  Reduce_opts o;
  o.min_count = 3;
  o.min_nodes = 2;  // the fixtures use 2-node cones
  return o;
}

// One occurrence of the reference 2-node cone:
//   xor_in -> Xor -> And <- and_in ; And -> `sink` (an output pin or flop din).
// The two leaves play DIFFERENT roles, which is what makes role-correct port
// binding observable.
struct Occ {
  hhds::Node_class x, a;
};

Occ make_cone(hhds::Graph* g, const hhds::Pin_class& xor_in, const hhds::Pin_class& and_in, const hhds::Pin_class& sink) {
  auto x = create_typed_node(*g, Ntype_op::Xor);
  xor_in.connect_sink(x.create_sink_pin(0));
  auto a = create_typed_node(*g, Ntype_op::And);
  x.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
  and_in.connect_sink(a.create_sink_pin(0));
  a.create_driver_pin(0).connect_sink(sink);
  return {x, a};
}

std::vector<hhds::Node_class> subs_of(hhds::Graph* g) {
  std::vector<hhds::Node_class> subs;
  for (auto n : g->body().nodes()) {
    if (n.is_invalid() || gu::is_builtin_node(n)) {
      continue;
    }
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      subs.push_back(n);
    }
  }
  return subs;
}

uint64_t count_ops(hhds::Graph* g, Ntype_op op) {
  uint64_t c = 0;
  for (auto n : g->body().nodes()) {
    if (!n.is_invalid() && !gu::is_builtin_node(n) && gu::type_op_of(n) == op) {
      ++c;
    }
  }
  return c;
}

// The pattern input port (by name) whose body pin feeds the Xor -- the "xor
// role". Everything else is the "and role".
std::string xor_role_port(const std::shared_ptr<hhds::GraphIO>& gio) {
  auto body = gio->get_graph();
  EXPECT_TRUE(body != nullptr);
  for (const auto& d : gio->get_input_pin_decls()) {
    auto ip = body->get_input_pin(d.name);
    for (const auto& e : ip.out_edges()) {
      if (gu::type_op_of(e.sink.get_master_node()) == Ntype_op::Xor) {
        return d.name;
      }
    }
  }
  return {};
}

// The driver feeding `sub`'s sink pin for port `pname`.
hhds::Pin_class driver_into(const hhds::Node_class& sub, const std::shared_ptr<hhds::GraphIO>& gio, const std::string& pname) {
  uint32_t pid = 0;
  for (const auto& d : gio->get_input_pin_decls()) {
    if (d.name == pname) {
      pid = static_cast<uint32_t>(d.port_id);
    }
  }
  for (const auto& e : sub.inp_edges()) {
    if (static_cast<uint32_t>(e.sink.get_port_id()) == pid) {
      return e.driver;
    }
  }
  return {};
}

}  // namespace

// Three identical cones collapse to one shared def instantiated three times,
// with role-correct wiring at every site -- including a site whose leaves were
// wired in the opposite creation order.
TEST(ColorReduce, ExtractsThreeIdenticalCones) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_three");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  gio->add_output("y0", 7);
  gio->add_output("y1", 8);
  gio->add_output("y2", 9);
  auto g = gio->create_graph();

  make_cone(g.get(), g->get_input_pin("in0"), g->get_input_pin("in1"), g->get_output_pin("y0"));
  make_cone(g.get(), g->get_input_pin("in2"), g->get_input_pin("in3"), g->get_output_pin("y1"));
  // Third site spelled backwards: and-leaf wired before the xor-leaf. Role
  // binding must not care.
  {
    auto a = create_typed_node(*g, Ntype_op::And);
    g->get_input_pin("in5").connect_sink(a.create_sink_pin(0));
    auto x = create_typed_node(*g, Ntype_op::Xor);
    g->get_input_pin("in4").connect_sink(x.create_sink_pin(0));
    x.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
    a.create_driver_pin(0).connect_sink(g->get_output_pin("y2"));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(6u, st.nodes_deleted);
  EXPECT_EQ(0u, st.verify_dropped);

  // The parent def holds only the three instances now.
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::Xor));
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::And));
  auto subs = subs_of(g.get());
  ASSERT_EQ(3u, subs.size());

  auto pio = subs[0].get_subnode_io();
  ASSERT_TRUE(pio != nullptr);
  EXPECT_TRUE(std::string{pio->get_name()}.starts_with("pat_"));
  EXPECT_EQ(2u, pio->get_input_pin_decls().size());
  EXPECT_EQ(1u, pio->get_output_pin_decls().size());
  for (const auto& s : subs) {
    EXPECT_EQ(pio->get_name(), s.get_subnode_io()->get_name()) << "all sites share ONE def";
    // Deliberately uncolored: color ids are pass.partition region ids, and a
    // pattern stamp would collide with whatever coloring the graph carries.
    EXPECT_FALSE(gu::has_color(s));
  }

  // Role-correct wiring: at every site, the xor-role port reads the xor leaf.
  auto xr = xor_role_port(pio);
  ASSERT_FALSE(xr.empty());
  absl::flat_hash_set<std::string> xor_leaves;
  for (const auto& s : subs) {
    auto d = driver_into(s, pio, xr);
    ASSERT_FALSE(d.is_invalid());
    xor_leaves.insert(std::string{gu::pin_name_of(d)});
  }
  EXPECT_EQ((absl::flat_hash_set<std::string>{"in0", "in2", "in4"}), xor_leaves);

  // Every output is still driven -- by an instance pin.
  for (const char* out : {"y0", "y1", "y2"}) {
    auto op    = g->get_output_pin(out);
    bool wired = false;
    for (const auto& e : op.inp_edges()) {
      wired = gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sub;
    }
    EXPECT_TRUE(wired) << out;
  }
}

// Two occurrences stay untouched under min_count=3.
TEST(ColorReduce, BelowMinCountUntouched) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_two");
  for (int i = 0; i < 4; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  gio->add_output("y0", 5);
  gio->add_output("y1", 6);
  auto g = gio->create_graph();

  make_cone(g.get(), g->get_input_pin("in0"), g->get_input_pin("in1"), g->get_output_pin("y0"));
  make_cone(g.get(), g->get_input_pin("in2"), g->get_input_pin("in3"), g->get_output_pin("y1"));

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(0u, st.patterns);
  EXPECT_EQ(0u, st.nodes_deleted);
  EXPECT_EQ(2u, count_ops(g.get(), Ntype_op::Xor));
  EXPECT_EQ(2u, count_ops(g.get(), Ntype_op::And));
  EXPECT_TRUE(subs_of(g.get()).empty());
}

// The loop-unrolling shape: cones identical except for one constant. The slot
// whose values diverge (5,5,5,7) is PROMOTED to an input port, every site feeds
// its own value, and all four sites share one body.
TEST(ColorReduce, DivergentConstIsPromotedToPort) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_const");
  for (int i = 0; i < 4; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 4; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 5);
  }
  auto g = gio->create_graph();

  auto k5 = gu::create_const(*g, *Dlop::create_integer(5));
  auto k7 = gu::create_const(*g, *Dlop::create_integer(7));

  auto make = [&](int i, const hhds::Pin_class& k) {
    auto x = create_typed_node(*g, Ntype_op::Xor);
    g->get_input_pin(std::string{"in"} + std::to_string(i)).connect_sink(x.create_sink_pin(0));
    auto a = create_typed_node(*g, Ntype_op::And);
    x.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
    k.connect_sink(a.create_sink_pin(0));
    a.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  };
  make(0, k5);
  make(1, k5);
  make(2, k5);
  make(3, k7);

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(4u, st.occurrences) << "differing consts parameterize, they do not split the bucket";
  EXPECT_EQ(1u, st.promoted_consts);
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::Xor));
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::And));
  auto subs = subs_of(g.get());
  ASSERT_EQ(4u, subs.size());
  // Every site feeds its OWN value into the const port: {5,5,5,7}.
  std::vector<std::string> fed;
  for (const auto& s : subs) {
    for (const auto& e : s.inp_edges()) {
      if (gu::is_const_pin(e.driver)) {
        fed.push_back(gu::hydrate_const(e.driver).serialize());
      }
    }
  }
  ASSERT_EQ(4u, fed.size()) << "exactly one const port per site";
  std::sort(fed.begin(), fed.end());
  EXPECT_EQ(Dlop::create_integer(5)->serialize(), fed[0]);
  EXPECT_EQ(Dlop::create_integer(7)->serialize(), fed[3]);
  EXPECT_NE(fed[0], fed[3]);
  // The shared body reads the port, not a baked-in constant.
  auto body = subs[0].get_subnode_graph();
  ASSERT_TRUE(body != nullptr);
  for (auto n : body->body().nodes()) {
    if (n.is_invalid() || gu::is_builtin_node(n)) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      EXPECT_FALSE(gu::is_const_pin(e.driver)) << "the divergent const must live at the call sites";
    }
  }
}

// A Get_mask mask determines wiring identity, not runtime data. Differing
// masks must split the pattern bucket instead of becoming a const input port:
// pass.abc deliberately requires this operand to remain constant.
TEST(ColorReduce, DivergentGetMaskStaysStructural) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_structural_mask");
  for (int i = 0; i < 4; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
    gio->add_output(std::string{"y"} + std::to_string(i), i + 5);
  }
  auto g = gio->create_graph();

  auto k5   = gu::create_const(*g, *Dlop::create_integer(5));
  auto k6   = gu::create_const(*g, *Dlop::create_integer(6));
  auto make = [&](int i, const hhds::Pin_class& mask) {
    auto x = create_typed_node(*g, Ntype_op::Xor);
    g->get_input_pin(std::string{"in"} + std::to_string(i)).connect_sink(x.create_sink_pin(0));
    auto xp = x.create_driver_pin(0);
    gu::set_bits(xp, 3);
    auto slice = create_typed_node(*g, Ntype_op::Get_mask);
    xp.connect_sink(gu::setup_sink_by_name(slice, "a"));
    mask.connect_sink(gu::setup_sink_by_name(slice, "mask"));
    auto out = slice.create_driver_pin(0);
    gu::set_bits(out, 2);
    out.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  };
  make(0, k5);
  make(1, k5);
  make(2, k5);
  make(3, k6);

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(0u, st.promoted_consts);
  EXPECT_EQ(1u, count_ops(g.get(), Ntype_op::Xor));
  EXPECT_EQ(1u, count_ops(g.get(), Ntype_op::Get_mask));
  EXPECT_EQ(3u, subs_of(g.get()).size());
}

// A constant every site agrees on stays INSIDE the body: no const port, no
// per-site const wiring.
TEST(ColorReduce, AgreedConstStaysInternal) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_consteq");
  for (int i = 0; i < 3; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 4);
  }
  auto g = gio->create_graph();

  auto k5 = gu::create_const(*g, *Dlop::create_integer(5));
  for (int i = 0; i < 3; ++i) {
    auto x = create_typed_node(*g, Ntype_op::Xor);
    g->get_input_pin(std::string{"in"} + std::to_string(i)).connect_sink(x.create_sink_pin(0));
    auto a = create_typed_node(*g, Ntype_op::And);
    x.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
    k5.connect_sink(a.create_sink_pin(0));
    a.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(0u, st.promoted_consts);
  auto subs = subs_of(g.get());
  ASSERT_EQ(3u, subs.size());
  for (const auto& s : subs) {
    for (const auto& e : s.inp_edges()) {
      EXPECT_FALSE(gu::is_const_pin(e.driver)) << "an agreed const is body-internal";
    }
  }
  auto body = subs[0].get_subnode_graph();
  ASSERT_TRUE(body != nullptr);
  bool body_has_const = false;
  for (auto n : body->body().nodes()) {
    if (n.is_invalid() || gu::is_builtin_node(n)) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      body_has_const = body_has_const || gu::is_const_pin(e.driver);
    }
  }
  EXPECT_TRUE(body_has_const);
}

// A pattern shared across defs gets ONE body: 2 sites in one def + 1 in
// another reach min_count together.
TEST(ColorReduce, CrossDefOccurrencesShareOneBody) {
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gioa = lib.create_io("red_xdef_a");
  for (int i = 0; i < 4; ++i) {
    gioa->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  gioa->add_output("y0", 5);
  gioa->add_output("y1", 6);
  auto ga = gioa->create_graph();
  make_cone(ga.get(), ga->get_input_pin("in0"), ga->get_input_pin("in1"), ga->get_output_pin("y0"));
  make_cone(ga.get(), ga->get_input_pin("in2"), ga->get_input_pin("in3"), ga->get_output_pin("y1"));

  auto giob = lib.create_io("red_xdef_b");
  giob->add_input("p", 1);
  giob->add_input("q", 2);
  giob->add_output("z", 3);
  auto gb = giob->create_graph();
  make_cone(gb.get(), gb->get_input_pin("p"), gb->get_input_pin("q"), gb->get_output_pin("z"));

  Reduce_stats st;
  hhds::Graph* defs[] = {ga.get(), gb.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  auto sa = subs_of(ga.get());
  auto sb = subs_of(gb.get());
  ASSERT_EQ(2u, sa.size());
  ASSERT_EQ(1u, sb.size());
  EXPECT_EQ(sa[0].get_subnode_io()->get_name(), sb[0].get_subnode_io()->get_name());
}

// A cone that terminates at a flop's din is still a cone (the flop is a
// boundary, never a member) and the flop reads the instance afterwards.
TEST(ColorReduce, ConeFeedingFlopExtracts) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_flop");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"q"} + std::to_string(i), i + 7);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto f = create_typed_node(*g, Ntype_op::Flop);
    make_cone(g.get(),
              g->get_input_pin(std::string{"in"} + std::to_string(2 * i)),
              g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1)),
              f.create_sink_pin(3));  // din
    f.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"q"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::Flop)) << "flops are never extracted";
  for (auto n : g->body().nodes()) {
    if (n.is_invalid() || gu::is_builtin_node(n) || gu::type_op_of(n) != Ntype_op::Flop) {
      continue;
    }
    bool din_from_sub = false;
    for (const auto& e : n.inp_edges()) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) == 3) {
        din_from_sub = gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sub;
      }
    }
    EXPECT_TRUE(din_from_sub);
  }
}

// Leaf width is part of the pattern: three 8-bit cones extract, the two 4-bit
// twins are a different (under-count) bucket and stay.
TEST(ColorReduce, LeafWidthSplitsBuckets) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_width");
  for (int i = 0; i < 10; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 5; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 11);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 5; ++i) {
    auto xi = g->get_input_pin(std::string{"in"} + std::to_string(2 * i));
    auto ai = g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1));
    gu::set_bits(xi, i < 3 ? 8 : 4);
    gu::set_bits(ai, i < 3 ? 8 : 4);
    make_cone(g.get(), xi, ai, g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(2u, count_ops(g.get(), Ntype_op::Xor)) << "the 4-bit twins stay";
  EXPECT_EQ(3u, subs_of(g.get()).size());
}

// Two chained patterns: cone C1's root feeds cone C2 at every site, both
// buckets extract, and the inter-cone edge survives as instance-to-instance
// wiring (the spliced-pin forwarding path).
TEST(ColorReduce, ChainedPatternsRewireThroughForwarding) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_chain");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"t"} + std::to_string(i), i + 7);   // C1 tap
    gio->add_output(std::string{"y"} + std::to_string(i), i + 10);  // C2 out
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    // C1: Or -> Sum, root taps a primary output AND feeds C2 (fanout 2 => root).
    auto o = create_typed_node(*g, Ntype_op::Or);
    g->get_input_pin(std::string{"in"} + std::to_string(2 * i)).connect_sink(o.create_sink_pin(0));
    auto s = create_typed_node(*g, Ntype_op::Sum);
    o.create_driver_pin(0).connect_sink(s.create_sink_pin(0));
    s.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"t"} + std::to_string(i)));
    // C2: Xor -> And, and-leaf = C1's root.
    make_cone(g.get(),
              g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1)),
              s.create_driver_pin(0),
              g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(2u, st.patterns);
  EXPECT_EQ(6u, st.occurrences);
  auto subs = subs_of(g.get());
  ASSERT_EQ(6u, subs.size());
  // Every C2 instance reads some C1 instance directly.
  uint64_t sub_to_sub = 0;
  for (const auto& s : subs) {
    for (const auto& e : s.inp_edges()) {
      if (gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sub) {
        ++sub_to_sub;
      }
    }
  }
  EXPECT_EQ(3u, sub_to_sub);
}

// A parallel duplicate edge (r+r: the same driver pin twice into one variadic
// sink port) is refused at mining -- hhds edge-slot promotion can dedupe the
// recreated pair, so splicing it would risk r+r -> r.
TEST(ColorReduce, DupEdgeConeRefused) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_dupedge");
  for (int i = 0; i < 3; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 4);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto x = create_typed_node(*g, Ntype_op::Xor);
    g->get_input_pin(std::string{"in"} + std::to_string(i)).connect_sink(x.create_sink_pin(0));
    auto s  = create_typed_node(*g, Ntype_op::Sum);
    auto xd = x.create_driver_pin(0);
    xd.connect_sink(s.create_sink_pin(0));
    xd.connect_sink(s.create_sink_pin(0));  // x + x: parallel duplicate edge
    s.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  EXPECT_EQ(0u, st.patterns);
  EXPECT_GE(st.dup_edge_skipped, 3u);
  EXPECT_TRUE(subs_of(g.get()).empty());
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::Sum));
}

// A comb-module instance never joins a cone (hhds stamps comb Subs
// non-loop-break, so the exclusion must be explicit): the instance survives
// with its target intact, and no pattern body ever contains a Sub.
TEST(ColorReduce, CombSubIsBoundaryNotMember) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  cio = lib.create_io("red_child");
  cio->add_input("ci", 1);
  cio->add_output("co", 2);
  auto cg = cio->create_graph();
  {
    auto n = create_typed_node(*cg, Ntype_op::Not);
    cg->get_input_pin("ci").connect_sink(n.create_sink_pin(0));
    n.create_driver_pin(0).connect_sink(cg->get_output_pin("co"));
    cg->commit();
  }

  auto gio = lib.create_io("red_subwrap");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 7);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto sub = create_typed_node(*g, Ntype_op::Sub);
    sub.set_subnode(cio);
    make_cone(g.get(),
              g->get_input_pin(std::string{"in"} + std::to_string(2 * i)),
              g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1)),
              sub.create_sink_pin(1));
    sub.create_driver_pin(2).connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  hhds::Graph* defs[] = {g.get(), cg.get()};
  ASSERT_TRUE(color_reduce(defs, small_opts(), &st));

  // The Xor/And cones extract; the child instances stay, targets intact.
  EXPECT_EQ(1u, st.patterns);
  uint64_t child_subs = 0;
  for (const auto& s : subs_of(g.get())) {
    auto sio = s.get_subnode_io();
    ASSERT_TRUE(sio != nullptr) << "every instance keeps a target";
    auto nm = std::string{sio->get_name()};
    if (nm == "red_child") {
      ++child_subs;
    } else {
      EXPECT_TRUE(nm.starts_with("pat_"));
      auto body = s.get_subnode_graph();
      ASSERT_TRUE(body != nullptr);
      EXPECT_EQ(0u, count_ops(body.get(), Ntype_op::Sub)) << "no Sub ever enters a pattern body";
    }
  }
  EXPECT_EQ(3u, child_subs);
}

// With the text-profit guard on, an interface-heavy candidate (whose instance
// would cost more lines than its statements save) is skipped, and says so.
TEST(ColorReduce, PortHeavyBucketSkipped) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_portheavy");
  for (int i = 0; i < 15; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 16);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    // 2 members, 5 distinct leaves: 5 + 1 > 2 * 2.
    auto a = create_typed_node(*g, Ntype_op::And);
    for (int j = 0; j < 4; ++j) {
      g->get_input_pin(std::string{"in"} + std::to_string(5 * i + j)).connect_sink(a.create_sink_pin(0));
    }
    auto o = create_typed_node(*g, Ntype_op::Or);
    a.create_driver_pin(0).connect_sink(o.create_sink_pin(0));
    g->get_input_pin(std::string{"in"} + std::to_string(5 * i + 4)).connect_sink(o.create_sink_pin(0));
    o.create_driver_pin(0).connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  Reduce_stats st;
  auto         o      = small_opts();
  o.min_win           = 1;  // the guard under test; small_opts leaves it inert
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(0u, st.patterns);
  EXPECT_GE(st.port_heavy_skipped, 1u);
  EXPECT_TRUE(subs_of(g.get()).empty());
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::And));
}

// A node bound does not constrain bit-level synthesis cost. The GE ceiling
// rejects a repeated wide arithmetic body before it becomes one shared module
// that is still too expensive for ABC to map within the optimization budget.
TEST(ColorReduce, OversizePatternSkipped) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_oversize");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 7);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto a = g->get_input_pin(std::string{"in"} + std::to_string(2 * i));
    auto b = g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1));
    gu::set_bits(a, 64);
    gu::set_bits(b, 64);
    auto mult = create_typed_node(*g, Ntype_op::Mult);
    a.connect_sink(mult.create_sink_pin(0));
    b.connect_sink(mult.create_sink_pin(0));
    auto out = mult.create_driver_pin(0);
    gu::set_bits(out, 64);
    out.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  auto o           = small_opts();
  o.min_nodes      = 1;
  o.max_nodes      = 1;
  o.max_pattern_ge = 1;
  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(0u, st.patterns);
  EXPECT_EQ(1u, st.oversize_skipped);
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::Mult));
}

// A packed-array word select commonly lowers to a very wide dynamic right
// shift followed by a narrow constant slice.  The two-node body is text-neutral
// after accounting for its ports, but synthesizing hundreds of copies is very
// expensive.  Bounded mining deliberately cuts before the widening SHL and
// admits this one shape through the text-profit guard.
TEST(ColorReduce, WideShiftSliceBypassesTextProfitGuard) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_wide_shift_slice");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 7);
  }
  auto g = gio->create_graph();

  auto mask = gu::create_const(*g, *Dlop::create_integer((int64_t{1} << 20) - 1));
  auto zero = gu::create_const(*g, *Dlop::create_integer(0));
  for (int i = 0; i < 3; ++i) {
    auto data = g->get_input_pin(std::string{"in"} + std::to_string(2 * i));
    auto amt  = g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1));
    gu::set_bits(data, 1024);
    gu::set_bits(amt, 7);

    auto shift = create_typed_node(*g, Ntype_op::SRA);
    data.connect_sink(shift.create_sink_pin(0));
    amt.connect_sink(shift.create_sink_pin(1));
    auto shifted = shift.create_driver_pin(0);
    gu::set_bits(shifted, 1024);

    auto slice = create_typed_node(*g, Ntype_op::Get_mask);
    shifted.connect_sink(slice.create_sink_pin(0));
    mask.connect_sink(slice.create_sink_pin(1));
    auto word = slice.create_driver_pin(0);
    gu::set_bits(word, 20);

    auto widen = create_typed_node(*g, Ntype_op::SHL);
    word.connect_sink(widen.create_sink_pin(0));
    zero.connect_sink(widen.create_sink_pin(1));
    auto packed = widen.create_driver_pin(0);
    gu::set_bits(packed, 1024);
    packed.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  auto o      = small_opts();
  o.max_nodes = 2;
  o.min_win   = 1;
  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::SRA));
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::Get_mask));
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::SHL));
  EXPECT_EQ(3u, subs_of(g.get()).size());
}

// One very wide dynamic shift is already an expensive synthesis cone. Three
// structurally identical placements therefore share one body even though a
// one-node pattern cannot satisfy the source-text profitability estimate.
TEST(ColorReduce, WideDynamicShiftBypassesTextProfitGuard) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_wide_dynamic_shift");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 7);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto value = g->get_input_pin(std::string{"in"} + std::to_string(2 * i));
    auto amt   = g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1));
    gu::set_bits(value, 20);
    gu::set_bits(amt, 9);
    auto shift = create_typed_node(*g, Ntype_op::SHL);
    value.connect_sink(shift.create_sink_pin(0));
    amt.connect_sink(shift.create_sink_pin(1));
    auto out = shift.create_driver_pin(0);
    gu::set_bits(out, 1024);
    out.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  auto o      = small_opts();
  o.max_nodes = 2;
  o.min_win   = 1;
  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::SHL));
  EXPECT_EQ(3u, subs_of(g.get()).size());
}

// Bounded mining pulls one address-arithmetic producer into a wide shift's
// cone.  This is the shape emitted for packed-array reads in Rob; sharing only
// singleton shifts leaves each 30k+-bit barrel network to expand separately.
TEST(ColorReduce, WideDynamicShiftWithAddressProducerBypassesTextProfitGuard) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_wide_dynamic_shift_address");
  for (int i = 0; i < 6; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 7);
  }
  auto g   = gio->create_graph();
  auto one = gu::create_const(*g, *Dlop::create_integer(1));

  for (int i = 0; i < 3; ++i) {
    auto value = g->get_input_pin(std::string{"in"} + std::to_string(2 * i));
    auto index = g->get_input_pin(std::string{"in"} + std::to_string(2 * i + 1));
    gu::set_bits(value, 32768);
    gu::set_bits(index, 9);

    auto address = create_typed_node(*g, Ntype_op::Sum);
    index.connect_sink(address.create_sink_pin(0));
    one.connect_sink(address.create_sink_pin(0));
    auto amount = address.create_driver_pin(0);
    gu::set_bits(amount, 9);

    auto shift = create_typed_node(*g, Ntype_op::SRA);
    value.connect_sink(shift.create_sink_pin(0));
    amount.connect_sink(shift.create_sink_pin(1));
    auto out = shift.create_driver_pin(0);
    gu::set_bits(out, 32768);
    out.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  auto o      = small_opts();
  o.max_nodes = 2;
  o.min_win   = 1;
  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(1u, st.patterns);
  EXPECT_EQ(3u, st.occurrences);
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::Sum));
  EXPECT_EQ(0u, count_ops(g.get(), Ntype_op::SRA));
  EXPECT_EQ(3u, subs_of(g.get()).size());
}

// The two-member wide-shift exception is for an ADDRESS-ARITHMETIC producer.
// A companion that carries real mapping cost of its own (here a 256-bit Mult,
// 65536 GE against the shift's ~12k) must still face the GE ceiling: the root
// shift does not license an arbitrarily expensive shared body.
TEST(ColorReduce, WideDynamicShiftWithExpensiveProducerKeepsGeGuard) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_reduce_test");
  auto  gio = lib.create_io("red_wide_dynamic_shift_expensive");
  for (int i = 0; i < 9; ++i) {
    gio->add_input(std::string{"in"} + std::to_string(i), i + 1);
  }
  for (int i = 0; i < 3; ++i) {
    gio->add_output(std::string{"y"} + std::to_string(i), i + 10);
  }
  auto g = gio->create_graph();

  for (int i = 0; i < 3; ++i) {
    auto a     = g->get_input_pin(std::string{"in"} + std::to_string(3 * i));
    auto b     = g->get_input_pin(std::string{"in"} + std::to_string(3 * i + 1));
    auto index = g->get_input_pin(std::string{"in"} + std::to_string(3 * i + 2));
    gu::set_bits(a, 256);
    gu::set_bits(b, 256);
    gu::set_bits(index, 8);

    auto mult = create_typed_node(*g, Ntype_op::Mult);
    a.connect_sink(mult.create_sink_pin(0));
    b.connect_sink(mult.create_sink_pin(0));
    auto value = mult.create_driver_pin(0);
    gu::set_bits(value, 256);

    auto shift = create_typed_node(*g, Ntype_op::SRA);
    value.connect_sink(shift.create_sink_pin(0));
    index.connect_sink(shift.create_sink_pin(1));
    auto out = shift.create_driver_pin(0);
    gu::set_bits(out, 256);
    out.connect_sink(g->get_output_pin(std::string{"y"} + std::to_string(i)));
  }

  auto o           = small_opts();
  o.max_nodes      = 2;
  o.max_pattern_ge = 20000;  // the shipped pass.color default
  Reduce_stats st;
  hhds::Graph* defs[] = {g.get()};
  ASSERT_TRUE(color_reduce(defs, o, &st));

  EXPECT_EQ(0u, st.patterns);
  EXPECT_EQ(1u, st.oversize_skipped);
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::Mult));
  EXPECT_EQ(3u, count_ops(g.get(), Ntype_op::SRA));
}
