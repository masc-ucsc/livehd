// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The lgraph-compare incremental-synthesis cache (2opt-incr A+C). The contract
// under test: a region reuses its cached mapped netlist iff its pre-ABC logic is
// STRUCTURALLY IDENTICAL to the cached one (semdiff) AND the resolved ABC recipe
// matches -- across a recompile that shifts nids. A changed region, a changed
// recipe, or a reuse-ineligible boundary must MISS, never reuse the wrong thing.

#include "region_cache.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "region_qor.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::synth::Region_cache;
using livehd::synth::Region_qor;
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
  hhds::GraphLibrary*            slib = nullptr;
  std::string                    src_name;
};

Fixture make_region(const char* srcdir, hhds::GraphLibrary& outlib, const char* name, Ntype_op op = Ntype_op::Xor,
                    const char* flop_name = "st", int bits = 8, std::string_view port_tag = {}, bool permute_inputs = false,
                    bool move_output = false) {
  Fixture    f;
  const auto a_name = std::string{"a"} + std::string{port_tag};
  const auto b_name = std::string{"b"} + std::string{port_tag};
  const auto c_name = std::string{"c"} + std::string{port_tag};
  const auto y_name = std::string{"y"} + std::string{port_tag};

  // --- pre-ABC logic in its own library (doubles as the pre-body) ---
  auto& slib = livehd::Hhds_graph_library::instance(srcdir);
  f.slib     = &slib;
  f.src_name = name;
  auto sgio  = slib.create_io(name);
  sgio->add_input(a_name, 1);
  sgio->add_input(b_name, permute_inputs ? 3 : 2);
  sgio->add_input(c_name, permute_inputs ? 2 : 3);
  sgio->add_output(y_name, move_output ? 7 : 4);
  auto g  = sgio->create_graph();
  f.src   = g;
  auto ia = g->get_input_pin(a_name);
  auto ib = g->get_input_pin(b_name);
  auto ic = g->get_input_pin(c_name);
  set_bits(ia, bits);
  set_bits(ib, bits);
  set_bits(ic, bits);

  auto x = create_typed_node(*g, op);
  ia.connect_sink(livehd::graph_util::setup_sink_pid(x, 0));
  ib.connect_sink(livehd::graph_util::setup_sink_pid(x, 0));
  auto xd = x.create_driver_pin(0);
  set_bits(xd, bits);

  auto an = create_typed_node(*g, Ntype_op::And);
  xd.connect_sink(livehd::graph_util::setup_sink_pid(an, 0));
  ic.connect_sink(livehd::graph_util::setup_sink_pid(an, 0));
  auto ad = an.create_driver_pin(0);
  set_bits(ad, bits);

  auto fl = create_typed_node(*g, Ntype_op::Flop);
  ad.connect_sink(fl.create_sink_pin(3));  // din
  if (flop_name[0] != '\0') {
    fl.attr(hhds::attrs::name).set(std::string{flop_name});
  }
  auto q = fl.create_driver_pin(0);
  set_bits(q, bits);
  q.connect_sink(g->get_output_pin(y_name));
  g->commit();  // the pre-body must be committed for the structural compare to read it

  f.nodes = {fl, x, an};  // loop-breaks first, like the partitioner
  f.in.push_back({.name = a_name, .src_driver = ia, .bits = bits, .sign = false});
  f.in.push_back({.name = b_name, .src_driver = ib, .bits = bits, .sign = false});
  f.in.push_back({.name = c_name, .src_driver = ic, .bits = bits, .sign = false});
  f.out.push_back({.name = y_name, .src_driver = q, .bits = bits, .sign = false});

  // --- stand-in "mapped" body in outlib with the same IO ---
  auto mgio = outlib.create_io(name);
  mgio->add_input(a_name, 1);
  mgio->add_input(b_name, permute_inputs ? 3 : 2);
  mgio->add_input(c_name, permute_inputs ? 2 : 3);
  mgio->add_output(y_name, move_output ? 7 : 4);
  auto m   = mgio->create_graph();
  f.mapped = m;
  auto mk  = create_typed_node(*m, Ntype_op::And);  // one marker gate
  m->get_input_pin(a_name).connect_sink(livehd::graph_util::setup_sink_pid(mk, 0));
  m->get_input_pin(b_name).connect_sink(livehd::graph_util::setup_sink_pid(mk, 0));
  auto md = mk.create_driver_pin(0);
  set_bits(md, bits);
  md.connect_sink(m->get_output_pin(y_name));
  m->commit();

  f.rb.body           = m.get();
  f.rb.src            = g.get();
  f.rb.pre_body       = g.get();
  f.rb.pre_lib        = &slib;
  f.rb.pre_name       = name;
  f.rb.color          = 1;
  f.rb.module_name    = name;
  f.rb.reuse_eligible = true;
  f.rb.inputs         = f.in;
  f.rb.outputs        = f.out;
  f.rb.nodes          = std::span<const hhds::Node_class>(f.nodes.data(), f.nodes.size());
  return f;
}

[[nodiscard]] size_t node_count(hhds::Graph* g) {
  size_t c = 0;
  for (auto n : g->body().nodes()) {
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

  Region_cache c1("lgdb_p2_cache", 7);
  Region_qor q;
  q.gates       = 5;
  q.area        = 2.0;
  q.delay       = 1.5;
  q.logic_depth = 3;
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, q, "R", &out1));
  c1.save();

  // A fresh run: new libraries + nids, same logic.
  auto& out2 = livehd::Hhds_graph_library::instance("lgdb_p2_o2");
  auto  f2   = make_region("lgdb_p2_s2", out2, "top__c1");
  auto* pre2 = f2.src.get();
  ASSERT_NE(pre2, nullptr);

  Region_cache c2("lgdb_p2_cache", 7);  // reloads the saved cache
  auto       res = c2.lookup_compare(f2.rb, pre2, "R");
  ASSERT_TRUE(res.hit) << "identical region under new nids must reuse";
  ASSERT_NE(res.row, nullptr);
  EXPECT_EQ(res.row->logic_depth, 3);
  EXPECT_EQ(c2.hits(), 0);
  ASSERT_TRUE(c2.reuse_hit(f2.rb, res, &out2));
  EXPECT_EQ(c2.hits(), 1);
  EXPECT_GT(node_count(f2.rb.body), 0) << "reuse fills the region body from the cache";
}

// Repeated subblocks can land in different color/module names in one compile.
// The canonical digest discovers them, then the exact structural comparison
// proves the reuse before the mapped body is copied under the new name.
TEST(AbcIncr, CrossNameStructuralReuse) {
  auto& out = livehd::Hhds_graph_library::instance("lgdb_p2x_o");
  auto  f1  = make_region("lgdb_p2x_s1", out, "top__c1", Ntype_op::Xor, "st", 8, "_left");
  auto  f2  = make_region("lgdb_p2x_s2", out, "top__c2", Ntype_op::Xor, "st", 8, "_right");

  Region_cache cache("lgdb_p2x_cache", 7);
  ASSERT_TRUE(cache.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R", &out));
  auto res = cache.lookup_compare(f2.rb, f2.src.get(), "R");
  ASSERT_TRUE(res.hit) << "same logic and boundary under another color must reuse";
  ASSERT_NE(res.row, nullptr);
  EXPECT_EQ(res.row->module, "top__c1");
  ASSERT_TRUE(cache.reuse_hit(f2.rb, res, &out));
  EXPECT_GT(node_count(f2.rb.body), 0);
  EXPECT_TRUE(f2.rb.body->get_io()->has_input("a_right"));
  EXPECT_TRUE(f2.rb.body->get_io()->has_output("y_right"));
}

// Structural equality by IO name is insufficient for an in-place body copy:
// numeric port IDs must still bind those names to the same parent signals.
TEST(AbcIncr, SameNamesWithChangedInputOrOutputPortIdsDoNotReuse) {
  hhds::GraphLibrary original_out;
  auto               original = make_region("lgdb_port_layout_original", original_out, "region");
  {
    Region_cache cache("lgdb_port_layout_cache", 7, true);
    ASSERT_TRUE(cache.store(original.rb, *original.slib, original.src_name, Region_qor{}, "R", &original_out));
    cache.save();
  }
  for (bool input_permutation : {false, true}) {
    hhds::GraphLibrary output;
    auto               changed = make_region(input_permutation ? "lgdb_port_layout_inputs" : "lgdb_port_layout_outputs",
                                             output,
                                             "region",
                                             Ntype_op::Xor,
                                             "st",
                                             8,
                                             {},
                                             input_permutation,
                                             !input_permutation);
    Region_cache         cache("lgdb_port_layout_cache", 7, true);
    EXPECT_FALSE(cache.lookup_compare(changed.rb, changed.src.get(), "R").hit);
    EXPECT_TRUE(cache.lookup_compare(original.rb, original.src.get(), "R").hit);
  }
}

TEST(AbcIncr, ChangedNameOwnerDoesNotEvictThePreviousDefinitionBeforeRenamedReuse) {
  hhds::GraphLibrary original_out;
  auto               original = make_region("lgdb_name_owner_original", original_out, "lane", Ntype_op::Xor);
  Region_qor         original_qor;
  original_qor.area                 = 17;
  original_qor.hook_evidence = std::make_shared<const std::string>("original witness");
  {
    Region_cache cache("lgdb_name_owner_cache", 7, true);
    ASSERT_TRUE(cache.store(original.rb, *original.slib, original.src_name, original_qor, "R", &original_out));
    cache.save();
  }
  hhds::GraphLibrary current_out;
  auto               changed = make_region("lgdb_name_owner_changed", current_out, "lane", Ntype_op::Or);
  auto               renamed = make_region("lgdb_name_owner_renamed", current_out, "lane_p1", Ntype_op::Xor);
  Region_cache         cache("lgdb_name_owner_cache", 7, true);
  ASSERT_FALSE(cache.lookup_compare(changed.rb, changed.src.get(), "R").hit);
  Region_qor changed_qor;
  changed_qor.area                 = 23;
  changed_qor.hook_evidence = std::make_shared<const std::string>("changed witness");
  ASSERT_TRUE(cache.store(changed.rb, *changed.slib, changed.src_name, changed_qor, "R", &current_out));
  auto hit = cache.lookup_compare(renamed.rb, renamed.src.get(), "R");
  ASSERT_TRUE(hit.hit);
  ASSERT_NE(hit.row, nullptr);
  EXPECT_EQ(hit.row->area, 17);
  ASSERT_TRUE(cache.read_evidence(*hit.row));
  EXPECT_EQ(*cache.read_evidence(*hit.row), "original witness");
  EXPECT_TRUE(cache.reuse_hit(renamed.rb, hit, &current_out));
  EXPECT_FALSE(cache.lookup_compare(renamed.rb, renamed.src.get(), "different-recipe").hit);
  auto current = cache.lookup_compare(changed.rb, changed.src.get(), "R");
  ASSERT_TRUE(current.hit);
  EXPECT_EQ(current.row->area, 23);
  cache.freeze_pending();
  // Freezing overwrites the mapped library's old owner. A later store must
  // not resurrect old disk metadata and pair it with that new mapped body.
  ASSERT_TRUE(cache.store(changed.rb, *changed.slib, changed.src_name, changed_qor, "R", &current_out));
  EXPECT_FALSE(cache.lookup_compare(renamed.rb, renamed.src.get(), "R").hit);
  ASSERT_TRUE(cache.store(renamed.rb, *renamed.slib, renamed.src_name, original_qor, "R", &current_out));
  cache.save();
  Region_cache reloaded("lgdb_name_owner_cache", 7, true);
  auto       old_result = reloaded.lookup_compare(renamed.rb, renamed.src.get(), "R");
  auto       new_result = reloaded.lookup_compare(changed.rb, changed.src.get(), "R");
  ASSERT_TRUE(old_result.hit);
  ASSERT_TRUE(new_result.hit);
  EXPECT_EQ(old_result.row->module, "lane_p1");
  EXPECT_EQ(new_result.row->module, "lane");
  EXPECT_EQ(old_result.row->area, 17);
  EXPECT_EQ(new_result.row->area, 23);
}

// A different resolved recipe must never share a cached netlist.
TEST(AbcIncr, RecipeMismatchMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2b_o1");
  auto  f1   = make_region("lgdb_p2b_s1", out1, "top__c1");
  ASSERT_NE(f1.src.get(), nullptr);
  Region_cache c1("lgdb_p2b_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R_add", &out1));
  c1.save();

  auto&      out2 = livehd::Hhds_graph_library::instance("lgdb_p2b_o2");
  auto       f2   = make_region("lgdb_p2b_s2", out2, "top__c1");
  auto*      pre2 = f2.src.get();
  Region_cache c2("lgdb_p2b_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R_mul").hit) << "recipe gate";
  EXPECT_TRUE(c2.lookup_compare(f2.rb, pre2, "R_add").hit) << "same recipe hits";
}

// A real logic edit (Xor -> Or) must miss.
TEST(AbcIncr, EditMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2c_o1");
  auto  f1   = make_region("lgdb_p2c_s1", out1, "top__c1", Ntype_op::Xor);
  ASSERT_NE(f1.src.get(), nullptr);
  Region_cache c1("lgdb_p2c_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R", &out1));
  c1.save();

  auto&      out2 = livehd::Hhds_graph_library::instance("lgdb_p2c_o2");
  auto       f2   = make_region("lgdb_p2c_s2", out2, "top__c1", Ntype_op::Or);  // edited op
  auto*      pre2 = f2.src.get();
  Region_cache c2("lgdb_p2c_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R").hit) << "an edited region must not reuse";
}

// A reuse-ineligible boundary (automorphic lanes) must miss, never guess a stitch.
TEST(AbcIncr, ReuseIneligibleMiss) {
  auto& out1 = livehd::Hhds_graph_library::instance("lgdb_p2d_o1");
  auto  f1   = make_region("lgdb_p2d_s1", out1, "top__c1");
  ASSERT_NE(f1.src.get(), nullptr);
  Region_cache c1("lgdb_p2d_cache", 7);
  ASSERT_TRUE(c1.store(f1.rb, *f1.slib, f1.src_name, Region_qor{}, "R", &out1));
  c1.save();

  auto& out2           = livehd::Hhds_graph_library::instance("lgdb_p2d_o2");
  auto  f2             = make_region("lgdb_p2d_s2", out2, "top__c1");
  f2.rb.reuse_eligible = false;  // partitioner refused the boundary
  auto*      pre2      = f2.src.get();
  Region_cache c2("lgdb_p2d_cache", 7);
  EXPECT_FALSE(c2.lookup_compare(f2.rb, pre2, "R").hit) << "reuse-ineligible region must not reuse";
}


// Publication-gated clients freeze before context-specific final transforms,
// then commit only after whole-design proof and output replacement succeed.
TEST(AbcIncr, FrozenPrivateRowsStayUnpublishedUntilSave) {
  auto&      out = livehd::Hhds_graph_library::instance("lgdb_frozen_out");
  auto       f   = make_region("lgdb_frozen_src", out, "top__c1");
  Region_cache pending("lgdb_frozen_cache", 7, true);
  ASSERT_TRUE(pending.store(f.rb, *f.slib, f.src_name, Region_qor{}, "R", &out));
  const auto frozen_nodes = node_count(f.rb.body);
  pending.freeze_pending();
  // A later whole-design transform must not contaminate reusable region rows.
  [[maybe_unused]] auto added = create_typed_node(*f.rb.body, Ntype_op::And);
  ASSERT_GT(node_count(f.rb.body), frozen_nodes);
  ASSERT_TRUE(pending.stage_snapshot("lgdb_frozen_staged"));
  {
    Region_cache next("lgdb_frozen_cache", 7, true);
    EXPECT_FALSE(next.lookup_compare(f.rb, f.src.get(), "R").hit);
  }
  {
    Region_cache staged("lgdb_frozen_staged", 7, true);
    EXPECT_TRUE(staged.lookup_compare(f.rb, f.src.get(), "R").hit);
  }
  pending.save();
  Region_cache committed("lgdb_frozen_cache", 7, true);
  auto       hit = committed.lookup_compare(f.rb, f.src.get(), "R");
  ASSERT_TRUE(hit.hit);
  ASSERT_TRUE(committed.reuse_hit(f.rb, hit, &out));
  EXPECT_EQ(node_count(f.rb.body), frozen_nodes);
}

TEST(AbcIncr, EvidenceFollowsExactRowThroughRenameAndPrivateSnapshot) {
  auto&      out     = livehd::Hhds_graph_library::instance("lgdb_evidence_out");
  auto       first   = make_region("lgdb_evidence_src", out, "original", Ntype_op::Xor, "st", 8, "_a");
  auto       renamed = make_region("lgdb_evidence_new", out, "renamed", Ntype_op::Xor, "st", 8, "_b");
  Region_cache cache("lgdb_evidence_cache", 19, true);
  Region_qor q;
  q.hook_evidence = std::make_shared<const std::string>("original decision and witnesses");
  ASSERT_TRUE(cache.store(first.rb, *first.slib, first.src_name, q, "R", &out));
  auto hit = cache.lookup_compare(renamed.rb, renamed.src.get(), "R");
  ASSERT_TRUE(hit.hit);
  ASSERT_EQ(cache.read_evidence(*hit.row), q.hook_evidence);
  ASSERT_TRUE(cache.stage_snapshot("lgdb_evidence_stage"));
  EXPECT_FALSE(std::filesystem::exists("lgdb_evidence_cache/abc_cache.json"));
  Region_cache staged("lgdb_evidence_stage", 19, true);
  hit = staged.lookup_compare(renamed.rb, renamed.src.get(), "R");
  ASSERT_TRUE(hit.hit);
  ASSERT_NE(staged.read_evidence(*hit.row), nullptr);
  EXPECT_EQ(*staged.read_evidence(*hit.row), *q.hook_evidence);
  EXPECT_EQ(hit.row->module, "original");
  const auto path = std::filesystem::path(staged.dir()) / hit.row->evidence_file;
  // Same-size corruption is detected, independently of the intact graph row.
  {
    std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
    corrupt << std::string(q.hook_evidence->size(), 'X');
  }
  EXPECT_EQ(staged.read_evidence(*hit.row), nullptr);
  std::filesystem::remove(path);
  EXPECT_EQ(staged.read_evidence(*hit.row), nullptr);
  auto invalid          = *hit.row;
  invalid.evidence_file = "../outside.json";
  EXPECT_EQ(staged.read_evidence(invalid), nullptr);
  // Repeated writes use distinct attachment generations; an earlier manifest
  // remains readable while a new private snapshot is assembled.
  cache.save();
  cache.save();
  Region_cache committed("lgdb_evidence_cache", 19, true);
  hit = committed.lookup_compare(renamed.rb, renamed.src.get(), "R");
  ASSERT_TRUE(hit.hit);
  ASSERT_NE(committed.read_evidence(*hit.row), nullptr);
  EXPECT_EQ(*committed.read_evidence(*hit.row), *q.hook_evidence);
  auto oversized           = *hit.row;
  oversized.evidence_bytes = Region_cache::max_evidence_bytes + 1;
  EXPECT_EQ(committed.read_evidence(oversized), nullptr);
  // A mixed snapshot copies a lazy existing attachment and a freshly mapped
  // row together. Neither may depend on files in the old cache after publish.
  Region_qor q2;
  q2.hook_evidence = std::make_shared<const std::string>("new decision");
  ASSERT_TRUE(committed.store(renamed.rb, *renamed.slib, renamed.src_name, q2, "R", &out));
  ASSERT_TRUE(committed.stage_snapshot("lgdb_evidence_mixed"));
  std::filesystem::remove_all("lgdb_evidence_cache");
  Region_cache mixed("lgdb_evidence_mixed", 19, true);
  auto       old_hit = mixed.lookup_compare(first.rb, first.src.get(), "R");
  auto       new_hit = mixed.lookup_compare(renamed.rb, renamed.src.get(), "R");
  ASSERT_TRUE(old_hit.hit);
  ASSERT_TRUE(new_hit.hit);
  ASSERT_NE(mixed.read_evidence(*old_hit.row), nullptr);
  ASSERT_NE(mixed.read_evidence(*new_hit.row), nullptr);
  EXPECT_EQ(*mixed.read_evidence(*old_hit.row), *q.hook_evidence);
  EXPECT_EQ(*mixed.read_evidence(*new_hit.row), *q2.hook_evidence);
}
