// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Replica expansion on hand-built graphs. Hand-built on purpose: expansion is
// the escape hatch every occurrence-blind consumer (LEC, ABC, timing, Verilog
// emission) relies on, so what it produces has to be pinned independently of
// whatever shape the front end happens to roll today.
//
// The property under test is that a compact node expands to exactly the graph
// the source unroll would have produced: `count` ordinary instances, invariant
// inputs shared, one index constant per ordinal, a carry chain threaded
// ordinal-to-ordinal, and external readers bound to the last occurrence.

#include "occurrence_materialize.hpp"

#include <format>
#include <string>
#include <vector>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

using gu::materialize_occurrences;

namespace {

// Callee interface: x (invariant, pid 0), idx (index, pid 1),
// acc_in (carry destination, pid 2); acc_out (carry source, pid 3),
// q (final-only output, pid 4).
constexpr hhds::Port_id kPidX      = 0;
constexpr hhds::Port_id kPidIdx    = 1;
constexpr hhds::Port_id kPidAccIn  = 2;
constexpr hhds::Port_id kPidAccOut = 3;
constexpr hhds::Port_id kPidQ      = 4;

std::shared_ptr<hhds::GraphIO> make_body_io(hhds::GraphLibrary& lib, const std::string& name) {
  auto io = lib.create_io(name);
  io->add_input("x", kPidX);
  io->add_input("idx", kPidIdx);
  io->add_input("acc_in", kPidAccIn);
  io->add_output("acc_out", kPidAccOut);
  io->add_output("q", kPidQ);
  io->set_bits("x", 8);
  io->set_bits("idx", 8);
  io->set_bits("acc_in", 12);
  io->set_bits("acc_out", 12);
  io->set_bits("q", 8);
  return io;
}

std::vector<hhds::Node_class> subs_of(hhds::Graph* g) {
  std::vector<hhds::Node_class> v;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      v.emplace_back(n);
    }
  }
  return v;
}

// The driver feeding `pid` of `n`, or an invalid pin.
hhds::Pin_class driver_of(const hhds::Node_class& n, hhds::Port_id pid) {
  for (const auto& e : n.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      return e.driver;
    }
  }
  return {};
}

// Orders the expanded occurrences by the constant on their index input, so the
// test does not depend on node-creation order leaking through body().nodes().
std::vector<hhds::Node_class> subs_by_index(hhds::Graph* g) {
  auto v = subs_of(g);
  std::ranges::sort(v, [](const hhds::Node_class& a, const hhds::Node_class& b) {
    const auto da = driver_of(a, kPidIdx);
    const auto db = driver_of(b, kPidIdx);
    return gu::hydrate_const(da).to_just_i64() < gu::hydrate_const(db).to_just_i64();
  });
  return v;
}

struct Fixture {
  hhds::GraphLibrary*            lib = nullptr;
  std::shared_ptr<hhds::GraphIO> body_io;
  std::shared_ptr<hhds::Graph>   parent;
  hhds::Node_class               compact;
};

// A parent holding ONE compact replicated Sub:
//   x        <- parent input `px`     (invariant)
//   acc_in   <- parent input `seed`   (carry initial value)
//   acc_out  -> parent output `total` (carried result)
//   q        -> parent output `last`  (final-only result)
Fixture build_compact(const std::string& lgdb, uint64_t count, int64_t first = 0, int64_t step = 1) {
  Fixture f;
  f.lib     = &livehd::Hhds_graph_library::instance(lgdb);
  f.body_io = make_body_io(*f.lib, "body");
  (void)f.body_io->create_graph();

  auto pio = f.lib->create_io("parent");
  pio->add_input("px", 0);
  pio->add_input("seed", 1);
  pio->add_output("total", 2);
  pio->add_output("last", 3);
  pio->set_bits("px", 8);
  pio->set_bits("seed", 12);
  f.parent = pio->create_graph();

  auto px   = f.parent->get_input_pin("px");
  auto seed = f.parent->get_input_pin("seed");
  gu::set_bits(px, 8);
  gu::set_bits(seed, 12);

  f.compact = gu::create_typed_node(*f.parent, Ntype_op::Sub);
  f.compact.set_subnode(f.body_io);
  f.compact.set_name("lane");
  px.connect_sink(f.compact.create_sink_pin(kPidX));
  seed.connect_sink(f.compact.create_sink_pin(kPidAccIn));
  f.compact.create_driver_pin(kPidAccOut).connect_sink(f.parent->get_output_pin("total"));
  f.compact.create_driver_pin(kPidQ).connect_sink(f.parent->get_output_pin("last"));

  hhds::Subnode_loop d;
  d.first       = first;
  d.step        = step;
  d.count       = count;
  d.index_input = kPidIdx;
  f.compact.set_subnode(f.body_io, d);
  f.compact.create_driver_pin(kPidAccOut).connect_sink(f.compact.create_sink_pin(kPidAccIn));
  return f;
}

}  // namespace

TEST(ReplicaExpand, PredicateOnlyTrueForDescriptorCarrier) {
  auto f = build_compact("lgdb_rexp_pred", 4);
  EXPECT_TRUE(f.compact.is_loop_subnode());

  f.compact.set_subnode(f.body_io);
  EXPECT_FALSE(f.compact.is_loop_subnode()) << "an ordinary Sub is never replicated by inference";
}

TEST(ReplicaExpand, ExpandsToOneInstancePerOrdinal) {
  auto f = build_compact("lgdb_rexp_basic", 4);
  ASSERT_EQ(subs_of(f.parent.get()).size(), 1u);

  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_by_index(f.parent.get());
  ASSERT_EQ(reps.size(), 4u);
  for (std::size_t r = 0; r < reps.size(); ++r) {
    EXPECT_FALSE(reps[r].is_loop_subnode()) << "an expanded occurrence is an ordinary instance";
    EXPECT_EQ(gu::node_name_of(reps[r]), std::format("lane__li{}", r))
        << "each occurrence carries its ordinal, matching what the unroller stamps";
  }
}

// pass.color runs long before pass.partition/pass.abc copy the design into the
// scratch they materialize, and both then key their regions off the color. An
// uncolored replica would silently join the color-0 region instead.
TEST(ReplicaExpand, ColorRidesEveryOccurrence) {
  auto f = build_compact("lgdb_rexp_color", 4);
  gu::set_color(f.compact, 7);

  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_by_index(f.parent.get());
  ASSERT_EQ(reps.size(), 4u);
  for (std::size_t r = 0; r < reps.size(); ++r) {
    EXPECT_EQ(gu::color_of(reps[r]), 7) << "ordinal " << r << " must stay in the region the compact node belonged to";
  }
}

TEST(ReplicaExpand, MultipleSitesShareModuleOrdinalSpace) {
  auto f      = build_compact("lgdb_rexp_multi_site", 4);
  auto second = gu::create_typed_node(*f.parent, Ntype_op::Sub);
  second.set_name("tail");
  second.set_subnode(f.body_io, hhds::Subnode_loop{.first = 0, .step = 1, .count = 2, .index_input = kPidIdx});

  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 2);
  std::vector<std::string> names;
  for (const auto& sub : subs_of(f.parent.get())) {
    names.emplace_back(gu::node_name_of(sub));
  }
  std::ranges::sort(names);
  EXPECT_EQ(names, (std::vector<std::string>{"lane__li0", "lane__li1", "lane__li2", "lane__li3", "tail__li4", "tail__li5"}));
}

TEST(ReplicaExpand, IndexConstantPerOrdinal) {
  auto f = build_compact("lgdb_rexp_idx", 4, /*first=*/10, /*step=*/3);
  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_by_index(f.parent.get());
  ASSERT_EQ(reps.size(), 4u);
  const int64_t want[] = {10, 13, 16, 19};
  for (std::size_t r = 0; r < reps.size(); ++r) {
    const auto d = driver_of(reps[r], kPidIdx);
    ASSERT_FALSE(d.is_invalid()) << "ordinal " << r << " has no index driver";
    EXPECT_EQ(gu::hydrate_const(d).to_just_i64(), want[r]);
  }
}

TEST(ReplicaExpand, InvariantInputIsSharedAndCarryIsChained) {
  auto f    = build_compact("lgdb_rexp_chain", 4);
  auto px   = f.parent->get_input_pin("px");
  auto seed = f.parent->get_input_pin("seed");
  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_by_index(f.parent.get());
  ASSERT_EQ(reps.size(), 4u);

  for (std::size_t r = 0; r < reps.size(); ++r) {
    EXPECT_EQ(driver_of(reps[r], kPidX), px) << "invariant input is shared by ordinal " << r;
  }

  // Ordinal 0 takes the external initial value; every later ordinal takes the
  // previous occurrence's mapped output. This is the whole point of a carry.
  EXPECT_EQ(driver_of(reps[0], kPidAccIn), seed);
  for (std::size_t r = 1; r < reps.size(); ++r) {
    const auto drv = driver_of(reps[r], kPidAccIn);
    ASSERT_FALSE(drv.is_invalid());
    EXPECT_EQ(drv.get_master_node(), reps[r - 1]) << "ordinal " << r << " must read ordinal " << (r - 1);
    EXPECT_EQ(drv.get_port_id(), kPidAccOut);
  }
}

TEST(ReplicaExpand, ExternalReadersBindToTheLastOccurrence) {
  auto f = build_compact("lgdb_rexp_readers", 4);
  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_by_index(f.parent.get());
  auto last = reps.back();

  const auto total = driver_of(f.parent->get_output_node(), 2);
  const auto lastq = driver_of(f.parent->get_output_node(), 3);
  ASSERT_FALSE(total.is_invalid());
  ASSERT_FALSE(lastq.is_invalid());
  EXPECT_EQ(total.get_master_node(), last) << "a carried result is the value after count applications";
  EXPECT_EQ(total.get_port_id(), kPidAccOut);
  EXPECT_EQ(lastq.get_master_node(), last) << "a final-only result is the last replica's output";
  EXPECT_EQ(lastq.get_port_id(), kPidQ);
}

TEST(ReplicaExpand, SingleOrdinalIsAPlainInstance) {
  auto f = build_compact("lgdb_rexp_one", 1);
  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);

  auto reps = subs_of(f.parent.get());
  ASSERT_EQ(reps.size(), 1u);
  EXPECT_EQ(driver_of(reps[0], kPidAccIn), f.parent->get_input_pin("seed"))
      << "with one ordinal the carry is just the initial value";
}

TEST(ReplicaExpand, ZeroCountPassesCarriesThroughAndInstantiatesNothing) {
  auto f    = build_compact("lgdb_rexp_zero", 0);
  auto seed = f.parent->get_input_pin("seed");

  // `last` is a non-carried output, which a zero-count node cannot source; drop
  // that reader so the legal part of rule 10 can be checked on its own.
  {
    // out_edges() is a lazy view; snapshot before mutating.
    std::vector<hhds::Edge_class> snap;
    for (const auto& e : f.compact.out_edges()) {
      if (e.driver.get_port_id() == kPidQ) {
        snap.emplace_back(e);
      }
    }
    for (auto& e : snap) {
      e.del_edge();
    }
  }

  ASSERT_EQ(materialize_occurrences(f.parent.get(), "test"), 1);
  EXPECT_TRUE(subs_of(f.parent.get()).empty()) << "zero replicas instantiate nothing";
  EXPECT_EQ(driver_of(f.parent->get_output_node(), 2), seed) << "the carried result is its own initial value";
}

TEST(ReplicaExpand, NoDescriptorIsALeftAlone) {
  auto f = build_compact("lgdb_rexp_none", 4);
  f.compact.set_subnode(f.body_io);
  EXPECT_EQ(materialize_occurrences(f.parent.get(), "test"), 0);
  EXPECT_EQ(subs_of(f.parent.get()).size(), 1u) << "an ordinary Sub is untouched";
}

// ── activation ──────────────────────────────────────────────────────────────
//
// The loop-control lowering now mints activation and next-active roles. Pin the
// realized chain independently so a front-end change cannot mask a graph-level
// regression.

namespace {

constexpr hhds::Port_id kPidAct  = 5;
constexpr hhds::Port_id kPidNext = 6;

// Same body as above plus an activation input and a next_active output.
std::shared_ptr<hhds::GraphIO> make_active_io(hhds::GraphLibrary& lib) {
  auto io = make_body_io(lib, "abody");
  io->add_input("act", kPidAct);
  io->add_output("next_act", kPidNext);
  io->set_bits("act", 1);
  io->set_bits("next_act", 1);
  return io;
}

}  // namespace

TEST(ReplicaExpand, ActivationBuildsCumulativeEnableAndCarryBypass) {
  // Each later carry is active[r-1] ? output[r-1] : input[r-1]. When a
  // next_active output exists, active itself advances cumulatively through an
  // And; without one, every occurrence reuses the external activation.
  for (const bool chained : {false, true}) {
    auto& lib = livehd::Hhds_graph_library::instance(chained ? "lgdb_rexp_act_chain" : "lgdb_rexp_act");
    auto  bio = make_active_io(lib);
    (void)bio->create_graph();

    auto pio = lib.create_io("parent_act");
    pio->add_input("en", 0);
    pio->add_input("seed", 1);
    pio->add_output("total", 2);
    pio->set_bits("en", 1);
    pio->set_bits("seed", 12);
    auto parent = pio->create_graph();

    auto en   = parent->get_input_pin("en");
    auto seed = parent->get_input_pin("seed");
    gu::set_bits(en, 1);
    gu::set_bits(seed, 12);

    auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
    compact.set_subnode(bio);
    compact.set_name("lane");
    en.connect_sink(compact.create_sink_pin(kPidAct));
    seed.connect_sink(compact.create_sink_pin(kPidAccIn));
    compact.create_driver_pin(kPidAccOut).connect_sink(parent->get_output_pin("total"));

    hhds::Subnode_loop d;
    d.count            = 3;
    d.step             = 1;
    d.index_input      = kPidIdx;
    d.activation_input = kPidAct;
    if (chained) {
      d.next_active_output = kPidNext;
    }
    compact.set_subnode(bio, d);
    compact.create_driver_pin(kPidAccOut).connect_sink(compact.create_sink_pin(kPidAccIn));

    ASSERT_EQ(materialize_occurrences(parent.get(), "test"), 1) << (chained ? "chained" : "unchained");
    auto reps = subs_by_index(parent.get());
    ASSERT_EQ(reps.size(), 3u);
    EXPECT_TRUE(compact.is_invalid());

    std::vector<hhds::Pin_class> carry_values;
    for (size_t r = 0; r < reps.size(); ++r) {
      auto carry = driver_of(reps[r], kPidAccIn);
      ASSERT_FALSE(carry.is_invalid());
      carry_values.push_back(carry);

      auto active = driver_of(reps[r], kPidAct);
      ASSERT_FALSE(active.is_invalid());
      if (!chained || r == 0) {
        EXPECT_EQ(active, en);
      } else {
        EXPECT_EQ(gu::type_op_of(active.get_master_node()), Ntype_op::And);
      }
    }

    EXPECT_EQ(carry_values[0], seed);
    for (size_t r = 1; r < reps.size(); ++r) {
      auto mux = carry_values[r].get_master_node();
      ASSERT_EQ(gu::type_op_of(mux), Ntype_op::Mux);
      EXPECT_EQ(driver_of(mux, 0), driver_of(reps[r - 1], kPidAct));
      EXPECT_EQ(driver_of(mux, 1), carry_values[r - 1]);
      EXPECT_EQ(driver_of(mux, 2).get_master_node(), reps[r - 1]);
      EXPECT_EQ(driver_of(mux, 2).get_port_id(), kPidAccOut);
    }
  }
}

TEST(ReplicaExpand, ZeroCountActivationPassesCarriesThrough) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_rexp_act_zero");
  auto  bio = make_active_io(lib);
  (void)bio->create_graph();

  auto pio = lib.create_io("parent_act_zero");
  pio->add_input("en", 0);
  pio->add_input("seed", 1);
  pio->add_output("total", 2);
  pio->set_bits("en", 1);
  pio->set_bits("seed", 12);
  auto parent = pio->create_graph();
  auto en     = parent->get_input_pin("en");
  auto seed   = parent->get_input_pin("seed");
  gu::set_bits(en, 1);
  gu::set_bits(seed, 12);

  auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
  compact.set_name("lane");
  en.connect_sink(compact.create_sink_pin(kPidAct));
  seed.connect_sink(compact.create_sink_pin(kPidAccIn));
  compact.create_driver_pin(kPidAccOut).connect_sink(parent->get_output_pin("total"));

  hhds::Subnode_loop d;
  d.count              = 0;
  d.step               = 1;
  d.index_input        = kPidIdx;
  d.activation_input   = kPidAct;
  d.next_active_output = kPidNext;
  compact.set_subnode(bio, d);
  compact.create_driver_pin(kPidAccOut).connect_sink(compact.create_sink_pin(kPidAccIn));

  ASSERT_EQ(materialize_occurrences(parent.get(), "test"), 1);
  EXPECT_TRUE(subs_of(parent.get()).empty());
  EXPECT_EQ(driver_of(parent->get_output_node(), 2), seed);
}

TEST(ReplicaExpand, UnsizedCalleeOutputIsRefusedNotSilentlyOneBit) {
  // An output declaration with no width leaves the created driver pin unsized,
  // which cgen prints as a bare 1-bit `wire`. For a carry chain that truncates
  // every ordinal, and it is invisible in the graph — so it must fail loudly.
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_rexp_unsized");
  auto  io  = lib.create_io("ubody");
  io->add_input("idx", kPidIdx);
  io->add_input("acc_in", kPidAccIn);
  io->add_output("acc_out", kPidAccOut);
  io->set_bits("acc_in", 12);
  // acc_out deliberately left with no declared width.
  (void)io->create_graph();

  auto pio = lib.create_io("parent_unsized");
  pio->add_input("seed", 0);
  pio->add_output("total", 1);
  pio->set_bits("seed", 12);
  auto parent = pio->create_graph();
  auto seed   = parent->get_input_pin("seed");
  gu::set_bits(seed, 12);

  auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
  compact.set_subnode(io);
  compact.set_name("lane");
  seed.connect_sink(compact.create_sink_pin(kPidAccIn));
  compact.create_driver_pin(kPidAccOut).connect_sink(parent->get_output_pin("total"));

  hhds::Subnode_loop d;
  d.count       = 3;
  d.step        = 1;
  d.index_input = kPidIdx;
  compact.set_subnode(io, d);
  compact.create_driver_pin(kPidAccOut).connect_sink(compact.create_sink_pin(kPidAccIn));

  EXPECT_EQ(materialize_occurrences(parent.get(), "test"), -1);
  // ATOMIC: the refusal must fire before anything is created. A half-expanded
  // body (the occurrences plus the still-present compact node) is worse than an
  // unexpanded one — a consumer that keeps going on the -1 emits count+1
  // physical instances instead of refusing.
  EXPECT_EQ(subs_of(parent.get()).size(), 1u) << "a refused expansion must leave the graph alone";
  EXPECT_TRUE(compact.is_loop_subnode());
}

TEST(ReplicaExpand, AbsurdCountIsRefusedBeforeAllocating) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_rexp_huge");
  auto  bio = make_body_io(lib, "hbody");
  (void)bio->create_graph();

  auto pio = lib.create_io("parent_huge");
  pio->add_input("seed", 0);
  pio->add_output("total", 1);
  pio->set_bits("seed", 12);
  auto parent = pio->create_graph();
  auto seed   = parent->get_input_pin("seed");
  gu::set_bits(seed, 12);

  auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
  compact.set_subnode(bio);
  compact.set_name("lane");
  seed.connect_sink(compact.create_sink_pin(kPidAccIn));
  compact.create_driver_pin(kPidAccOut).connect_sink(parent->get_output_pin("total"));

  hhds::Subnode_loop d;
  d.first = 0;
  d.step  = 1;
  d.count = 4000000000ull;  // validates cleanly: no int64 index overflow
  compact.set_subnode(bio, d);
  compact.create_driver_pin(kPidAccOut).connect_sink(compact.create_sink_pin(kPidAccIn));

  EXPECT_EQ(materialize_occurrences(parent.get(), "test"), -1) << "must refuse rather than try to build 4e9 nodes";
}
