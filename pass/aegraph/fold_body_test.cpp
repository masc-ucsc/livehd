//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// livehd::fold_body (fold_body.cpp) and the process-wide fold engine (fold_engine.cpp): every engine
// reaches the body, folds it, and is counted under its site; the cache salt mixes the engine in.

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
    livehd::set_fold_engine(livehd::Fold_engine::classic);
    livehd::set_fold_default_cost(livehd::Fold_cost::synth);
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

TEST(FoldBody, EveryEngineFoldsTheBodyAndIsCountedUnderItsSite) {
  const Engine_guard guard;
  for (const auto engine : {livehd::Fold_engine::classic, livehd::Fold_engine::aegraph, livehd::Fold_engine::aegraph_classic}) {
    hhds::GraphLibrary lib_ref, lib;
    auto               ref = make(lib_ref, "fold_ref");
    auto               g   = make(lib, "fold_run");
    livehd::Fold_options options;
    options.site = "test.site";
    livehd::fold_body(g, engine, options);
    EXPECT_EQ(count_op(g, Ntype_op::Sum), engine == livehd::Fold_engine::classic ? 1u : 0u)
        << livehd::fold_engine_name(engine) << ": the constant Sum folds, the shared pair merges (x + x is x << 1 in the e-graph)";
    EXPECT_EQ(livehd::fold_count("test.site", engine), 1u) << livehd::fold_engine_name(engine);
    livehd::lec::Lec_options lec;
    lec.cones   = "false";
    lec.timeout = 10;
    EXPECT_EQ(livehd::lec::prove_equal(ref.get(), g.get(), lec).verdict, livehd::lec::Verdict::Proven)
        << livehd::fold_engine_name(engine);
  }
  EXPECT_EQ(livehd::fold_count("test.site"), 3u);
  EXPECT_EQ(livehd::fold_count("other.site"), 0u);
}

TEST(FoldBody, ProcessWideEngineIsTheDefaultAndClassicUntilSet) {
  const Engine_guard guard;
  EXPECT_EQ(livehd::fold_engine(), livehd::Fold_engine::classic);
  hhds::GraphLibrary lib;
  auto               g = make(lib, "fold_global");
  livehd::Fold_options options;
  options.site = "test.global";
  livehd::fold_body(g, options);
  EXPECT_EQ(livehd::fold_count("test.global", livehd::Fold_engine::classic), 1u);

  livehd::set_fold_engine(livehd::Fold_engine::aegraph);
  auto h = make(lib, "fold_global2");
  livehd::fold_body(h, options);
  EXPECT_EQ(livehd::fold_count("test.global", livehd::Fold_engine::aegraph), 1u);
  EXPECT_EQ(count_op(h, Ntype_op::Sum), 0u);  // the shared pair merges into x + x, which the e-graph writes as x << 1
}

TEST(FoldBody, PreflightFoldStampsEveryCellItCreates) {
  const Engine_guard guard;
  hhds::GraphLibrary lib;
  auto               g = make(lib, "fold_preflight");
  livehd::Fold_options options;
  options.bitwidth_follows = false;
  options.site             = "test.preflight";
  livehd::fold_body(g, livehd::Fold_engine::aegraph, options);
  for (auto node : g->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (gu::is_builtin_node(node) || !Ntype::is_comb(op) || op == Ntype_op::Mux || !node.has_out_edges()) {
      continue;
    }
    EXPECT_NE(gu::bits_of(node.create_driver_pin(0)), 0);
  }
}

TEST(FoldBody, FoldGraphsRunsTheMethodsOfTheEngine) {
  const Engine_guard guard;
  static const bool registered = [] {  // the kernel does this at startup: register the linked passes' EPRP methods
    for (const auto& [name, setup] : Pass_plugin::get_registry()) {
      setup();
    }
    return true;
  }();
  ASSERT_TRUE(registered);
  for (const auto engine : {livehd::Fold_engine::classic, livehd::Fold_engine::aegraph, livehd::Fold_engine::aegraph_classic}) {
    hhds::GraphLibrary lib;
    auto               g = make(lib, "fold_graphs");
    Eprp_var           var;
    var.graphs.push_back(g);
    livehd::fold_graphs(var, engine, "test.graphs");
    EXPECT_EQ(count_op(g, Ntype_op::Sum), engine == livehd::Fold_engine::classic ? 1u : 0u) << livehd::fold_engine_name(engine);
    EXPECT_EQ(livehd::fold_count("test.graphs", engine), 1u);
  }
}

TEST(FoldBody, CacheSaltIsUnchangedForClassicAndDistinctPerEngineAndCost) {
  const Engine_guard guard;
  constexpr uint64_t salt = 0x1234abcdULL;
  EXPECT_EQ(livehd::fold_salt_mix(salt), salt) << "classic keys must not move";
  EXPECT_TRUE(livehd::fold_descriptor().empty());
  livehd::set_fold_engine(livehd::Fold_engine::aegraph);
  const auto aeg = livehd::fold_salt_mix(salt);
  livehd::set_fold_engine(livehd::Fold_engine::aegraph_classic);
  const auto both = livehd::fold_salt_mix(salt);
  livehd::set_fold_default_cost(livehd::Fold_cost::sim);
  const auto both_sim = livehd::fold_salt_mix(salt);
  EXPECT_NE(aeg, salt);
  EXPECT_NE(aeg, both);
  EXPECT_NE(both, both_sim);
  EXPECT_EQ(livehd::fold_descriptor(), "engine=aegraph+classic;cost=sim");
}

TEST(FoldBody, EngineNamesRoundTrip) {
  for (const auto engine : {livehd::Fold_engine::classic, livehd::Fold_engine::aegraph, livehd::Fold_engine::aegraph_classic}) {
    EXPECT_EQ(livehd::parse_fold_engine(livehd::fold_engine_name(engine)), engine);
  }
  EXPECT_FALSE(livehd::parse_fold_engine("bogus").has_value());
}
