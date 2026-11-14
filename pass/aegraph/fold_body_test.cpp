//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// livehd::fold_body (fold_body.cpp) and the fold configuration (fold_engine.cpp): the e-graph engine
// reaches the body, folds it, and is counted under its site; the cache salt mixes the cost profile in.

#include "fold_body.hpp"

#include <string>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass.hpp"
#include "query.hpp"

namespace {
namespace gu = livehd::graph_util;

struct Engine_guard {
  Engine_guard() { livehd::fold_counters_reset(); }
  ~Engine_guard() {
    livehd::set_fold_default_cost(livehd::Fold_cost::synth);
    livehd::set_fold_rules_disable("");
  }
};

// o0 = (20 + 22) ^ i0, o1 = (i0 ^ i1) + (i1 ^ i0): a constant to fold and a pair to share.
std::shared_ptr<hhds::Graph> make(hhds::GraphLibrary& lib, const char* name) {
  auto io = lib.create_io(name);
  for (const char* n : {"i0", "i1"}) {
    io->add_input(n, io->get_input_pin_decls().size());
    io->set_bits(n, 8);
    io->set_unsign(n, true);
  }
  io->add_output("o0", 10);
  io->add_output("o1", 11);
  for (const char* n : {"o0", "o1"}) {
    io->set_bits(n, 9);
    io->set_unsign(n, true);
  }
  auto g = io->create_graph();
  const auto op = [&](Ntype_op kind, const hhds::Pin_class& a, const hhds::Pin_class& b, int bits) {
    auto n = gu::create_typed_node(*g, kind);
    a.connect_sink(gu::setup_sink_by_name(n, "as"));
    b.connect_sink(gu::setup_sink_by_name(n, "as"));
    gu::set_ubits(n.create_driver_pin(0), bits);
    return n.create_driver_pin(0);
  };
  const auto k  = [&](int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
  const auto s  = op(Ntype_op::Sum, k(20), k(22), 8);
  op(Ntype_op::Xor, s, g->get_input_pin("i0"), 8).connect_sink(g->get_output_pin("o0"));
  const auto x1 = op(Ntype_op::Xor, g->get_input_pin("i0"), g->get_input_pin("i1"), 8);
  const auto x2 = op(Ntype_op::Xor, g->get_input_pin("i1"), g->get_input_pin("i0"), 8);
  op(Ntype_op::Sum, x1, x2, 9).connect_sink(g->get_output_pin("o1"));
  return g;
}

size_t count_op(const std::shared_ptr<hhds::Graph>& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    n += gu::type_op_of(node) == op;
  }
  return n;
}

}  // namespace

TEST(FoldBody, FoldsTheBodyAndIsCountedUnderItsSite) {
  const Engine_guard   guard;
  hhds::GraphLibrary   lib_ref, lib;
  auto                 ref = make(lib_ref, "fold_ref");
  auto                 g   = make(lib, "fold_run");
  livehd::Fold_options options;
  options.site = "test.site";
  livehd::fold_body(g, options);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u) << "the constant Sum folds, the shared pair merges (x + x is x << 1 in the e-graph)";
  EXPECT_EQ(livehd::fold_count("test.site"), 1u);
  EXPECT_EQ(livehd::fold_count("other.site"), 0u);
  livehd::lec::Lec_options lec;
  lec.cones   = "false";
  lec.timeout = 10;
  EXPECT_EQ(livehd::lec::prove_equal(ref.get(), g.get(), lec).verdict, livehd::lec::Verdict::Proven);
}

TEST(FoldBody, PreflightFoldStampsEveryCellItCreates) {
  const Engine_guard   guard;
  hhds::GraphLibrary   lib;
  auto                 g = make(lib, "fold_preflight");
  livehd::Fold_options options;
  options.bitwidth_follows = false;
  options.site             = "test.preflight";
  livehd::fold_body(g, options);
  for (auto node : g->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (gu::is_builtin_node(node) || !Ntype::is_comb(op) || !node.has_out_edges()) {
      continue;
    }
    EXPECT_NE(gu::bits_of(node.create_driver_pin(0)), 0);
  }
}

TEST(FoldBody, FoldGraphsRunsTheEngineMethod) {
  const Engine_guard guard;
  static const bool  registered = [] {  // the kernel does this at startup: register the linked passes' EPRP methods
    for (const auto& [name, setup] : Pass_plugin::get_registry()) {
      setup();
    }
    return true;
  }();
  ASSERT_TRUE(registered);
  hhds::GraphLibrary lib;
  auto               g = make(lib, "fold_graphs");
  Eprp_var           var;
  var.graphs.push_back(g);
  livehd::fold_graphs(var, "test.graphs");
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u);
  EXPECT_EQ(livehd::fold_count("test.graphs"), 1u);
}

TEST(FoldBody, CacheSaltNamesTheEngineAndDiffersPerCost) {
  const Engine_guard guard;
  constexpr uint64_t salt = 0x1234abcdULL;
  EXPECT_EQ(livehd::fold_descriptor(), "engine=aegraph;cost=synth") << "the spelling caches were keyed with";
  const auto synth = livehd::fold_salt_mix(salt);
  livehd::set_fold_default_cost(livehd::Fold_cost::sim);
  const auto sim = livehd::fold_salt_mix(salt);
  EXPECT_NE(synth, salt);
  EXPECT_NE(synth, sim);
  EXPECT_EQ(livehd::fold_descriptor(), "engine=aegraph;cost=sim");
  livehd::set_fold_rules_disable("flatten");
  EXPECT_EQ(livehd::fold_descriptor(), "engine=aegraph;cost=sim;rules_disable=flatten");
  EXPECT_NE(livehd::fold_salt_mix(salt), sim) << "a rule turned off changes what a fold site produces";
}
