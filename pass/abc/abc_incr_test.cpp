// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The lgraph-compare incremental-synthesis cache (2opt-incr A+C). The contract
// under test: a region reuses its cached mapped netlist iff its pre-ABC logic is
// STRUCTURALLY IDENTICAL to the cached one (semdiff) AND the resolved ABC recipe
// matches -- across a recompile that shifts nids. A changed region, a changed
// recipe, or a reuse-ineligible boundary must MISS, never reuse the wrong thing.

#include "abc_incr.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "abc_map.hpp"  // Region_qor
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::abc::Incr_cache;
using livehd::abc::Region_qor;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
using livehd::partition::Region_body;

namespace {

// A region y = flop((a OP b) & c) plus a stand-in "mapped" body with the same IO.
// `op` and `flop_name` let a test perturb the logic; `bits` the width. src holds
// the pre-ABC logic (what the compare rebuilds + hashes); `mapped` lives in
// `outlib` under `name` (what store snapshots and reuse fills).
struct Fixture {
  std::shared_ptr<hhds::Graph>   src;
  std::shared_ptr<hhds::Graph>   mapped;
  std::vector<hhds::Node_class>  nodes;
  std::vector<Region_body::Port> in, out;
  Region_body                    rb;
  // The pre-body is the region's original logic as a standalone graph. In
  // production the partitioner builds it (Region_body::pre_body); here `src` IS
  // that standalone graph, so the tests feed it straight to the cache API (which
  // is builder-agnostic -- it takes a pre-body Graph* + its library).
  hhds::GraphLibrary* slib = nullptr;
  std::string         src_name;
};

Fixture make_region(const char* srcdir, hhds::GraphLibrary& outlib, const char* name, Ntype_op op = Ntype_op::Xor,
                    const char* flop_name = "st", int bits = 8) {
  Fixture f;

  // --- pre-ABC logic in its own library (doubles as the pre-body) ---
  auto& slib   = livehd::Hhds_graph_library::instance(srcdir);
  f.slib       = &slib;
  f.src_name   = name;
  auto  sgio   = slib.create_io(name);
  sgio->add_input("a", 1);
  sgio->add_input("b", 2);
  sgio->add_input("c", 3);
  sgio->add_output("y", 4);
  auto g = sgio->create_graph();
  f.src  = g;
  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  auto ic = g->get_input_pin("c");
  set_bits(ia, bits);
  set_bits(ib, bits);
  set_bits(ic, bits);

  auto x = create_typed_node(*g, op);
  ia.connect_sink(x.create_sink_pin(0));
  ib.connect_sink(x.create_sink_pin(0));
  auto xd = x.create_driver_pin(0);
  set_bits(xd, bits);

  auto an = create_typed_node(*g, Ntype_op::And);
  xd.connect_sink(an.create_sink_pin(0));
  ic.connect_sink(an.create_sink_pin(0));
  auto ad = an.create_driver_pin(0);
  set_bits(ad, bits);

  auto fl = create_typed_node(*g, Ntype_op::Flop);
  ad.connect_sink(fl.create_sink_pin(3));  // din
  if (flop_name[0] != '\0') {
    fl.attr(hhds::attrs::name).set(std::string{flop_name});
  }
  auto q = fl.create_driver_pin(0);
  set_bits(q, bits);
  q.connect_sink(g->get_output_pin("y"));
  g->commit();  // the pre-body must be committed for the structural compare to read it

  f.nodes = {fl, x, an};  // loop-breaks first, like the partitioner
  f.in.push_back({.name = "a", .src_driver = ia, .bits = bits, .sign = false});
  f.in.push_back({.name = "b", .src_driver = ib, .bits = bits, .sign = false});
  f.in.push_back({.name = "c", .src_driver = ic, .bits = bits, .sign = false});
  f.out.push_back({.name = "y", .src_driver = q, .bits = bits, .sign = false});

  // --- stand-in "mapped" body in outlib with the same IO ---
  auto mgio = outlib.create_io(name);
  mgio->add_input("a", 1);
  mgio->add_input("b", 2);
  mgio->add_input("c", 3);
  mgio->add_output("y", 4);
  auto m   = mgio->create_graph();
  f.mapped = m;
  auto mk  = create_typed_node(*m, Ntype_op::And);  // one marker gate
  m->get_input_pin("a").connect_sink(mk.create_sink_pin(0));
  m->get_input_pin("b").connect_sink(mk.create_sink_pin(0));
  auto md = mk.create_driver_pin(0);
  set_bits(md, bits);
  md.connect_sink(m->get_output_pin("y"));
  m->commit();

  f.rb.body          = m.get();
  f.rb.src           = g.get();
  f.rb.color         = 1;
  f.rb.module_name   = name;
  f.rb.reuse_eligible = true;
  f.rb.inputs        = f.in;
  f.rb.outputs       = f.out;
  f.rb.nodes         = std::span<const hhds::Node_class>(f.nodes.data(), f.nodes.size());
  return f;
}

[[nodiscard]] size_t node_count(hhds::Graph* g) {
  size_t c = 0;
  for (auto n : g->fast_class()) {
    (void)n;
    ++c;
  }
  return c;
}

}  // namespace

// The same region, stored then rebuilt under different nids, reuses: the whole
// cache-across-recompiles premise.
TEST(AbcIncr, StructuralEqualReuse) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2_o1");
  auto  f1   = make_region("lgdb_p2_s1", out1, "top__c1");
  auto* pre1 = f1.src.get();
  ASSERT_NE(pre1, nullptr);

  Incr_cache c1("lgdb_p2_cache", 7);
  Region_qor q;
  q.gates = 5;
  q.area  = 2.0;
  q.delay = 1.5;
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, q, "R", &out1));
  c1.save();

  // A fresh run: new libraries + nids, same logic.
  auto& out2 = livehd::Hhds_graph_library::instance("lgdb_p2_o2");
  auto  f2   = make_region("lgdb_p2_s2", out2, "top__c1");
  auto* pre2 = f2.src.get();
  ASSERT_NE(pre2, nullptr);

  Incr_cache c2("lgdb_p2_cache", 7);  // reloads the saved cache
  auto       res = c2.lookup_compare(f2.rb, pre2, "R");
  ASSERT_TRUE(res.hit) << "identical region under new nids must reuse";
  EXPECT_EQ(c2.hits(), 0);
  ASSERT_TRUE(c2.reuse_hit(f2.rb, res, &out2));
  EXPECT_EQ(c2.hits(), 1);
  EXPECT_GT(node_count(f2.rb.body), 0) << "reuse fills the region body from the cache";
}

// A different resolved recipe must never share a cached netlist.
TEST(AbcIncr, RecipeMismatchMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2b_o1");
  auto  f1   = make_region("lgdb_p2b_s1", out1, "top__c1");
  ASSERT_NE(f1.src.get(), nullptr);
  Incr_cache c1("lgdb_p2b_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R_add", &out1));
  c1.save();

  auto& out2 = livehd::Hhds_graph_library::instance("lgdb_p2b_o2");
  auto  f2   = make_region("lgdb_p2b_s2", out2, "top__c1");
  auto* pre2 = f2.src.get();
  Incr_cache c2("lgdb_p2b_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R_mul").hit) << "recipe gate";
  EXPECT_TRUE(c2.lookup_compare(f2.rb, pre2, "R_add").hit) << "same recipe hits";
}

// A real logic edit (Xor -> Or) must miss.
TEST(AbcIncr, EditMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2c_o1");
  auto  f1   = make_region("lgdb_p2c_s1", out1, "top__c1", Ntype_op::Xor);
  ASSERT_NE(f1.src.get(), nullptr);
  Incr_cache c1("lgdb_p2c_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R", &out1));
  c1.save();

  auto& out2 = livehd::Hhds_graph_library::instance("lgdb_p2c_o2");
  auto  f2   = make_region("lgdb_p2c_s2", out2, "top__c1", Ntype_op::Or);  // edited op
  auto* pre2 = f2.src.get();
  Incr_cache c2("lgdb_p2c_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R").hit) << "an edited region must not reuse";
}

// A reuse-ineligible boundary (automorphic lanes) must miss, never guess a stitch.
TEST(AbcIncr, ReuseIneligibleMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2d_o1");
  auto  f1   = make_region("lgdb_p2d_s1", out1, "top__c1");
  ASSERT_NE(f1.src.get(), nullptr);
  Incr_cache c1("lgdb_p2d_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R", &out1));
  c1.save();

  auto& out2 = livehd::Hhds_graph_library::instance("lgdb_p2d_o2");
  auto  f2   = make_region("lgdb_p2d_s2", out2, "top__c1");
  f2.rb.reuse_eligible = false;  // partitioner refused the boundary
  auto* pre2 = f2.src.get();
  Incr_cache c2("lgdb_p2d_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R").hit) << "reuse-ineligible region must not reuse";
}
