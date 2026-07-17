// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Absorb: fold below-min defs into their parents (todo/livehd/2c-color-size.html
// subtask D). The end-to-end proof that absorb preserves behaviour is the LEC in
// lhd/tests/lhd_color_absorb_test.sh; these pin the DECISIONS -- which defs go,
// which are spared, and what the parent looks like afterwards -- which a LEC on a
// whole design cannot localize.

#include "color_absorb.hpp"

#include <cstdint>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::absorb_small_defs;
using livehd::color::Absorb_stats;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
using livehd::graph_util::type_op_of;

namespace {

using Gid2Graph = absl::flat_hash_map<hhds::Gid, hhds::Graph*>;

// An `a & b -> y` leaf def, `bits` wide (so its weight is `bits` GE).
std::shared_ptr<hhds::GraphIO> make_leaf_io(hhds::GraphLibrary& lib, const char* name, int bits) {
  auto io = lib.create_io(name);
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_output("y", 2);
  io->set_bits("a", bits);
  io->set_bits("b", bits);
  io->set_bits("y", bits);
  return io;
}

void fill_leaf(hhds::Graph* g, int bits) {
  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  set_bits(ia, bits);
  set_bits(ib, bits);
  auto n = create_typed_node(*g, Ntype_op::And);
  ia.connect_sink(n.create_sink_pin(0));
  ib.connect_sink(n.create_sink_pin(1));
  auto d = n.create_driver_pin(0);
  set_bits(d, bits);
  d.connect_sink(g->get_output_pin("y"));
}

size_t count_op(hhds::Graph* g, Ntype_op op) {
  size_t k = 0;
  for (auto n : g->fast_class()) {
    if (type_op_of(n) == op) {
      ++k;
    }
  }
  return k;
}

Gid2Graph map_of(std::initializer_list<hhds::Graph*> gs) {
  Gid2Graph m;
  for (auto* g : gs) {
    m[g->get_gid()] = g;
  }
  return m;
}

}  // namespace

// The multi-parent case, and the reason absorb costs area: a tiny def
// instantiated twice becomes TWO copies of its logic. ABC used to map it once.
// The report must own that number rather than hide it.
TEST(ColorAbsorb, InlinesEverySiteAndReportsTheDuplication) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_multi");
  auto  cio = make_leaf_io(lib, "leaf", 8);
  auto  cg  = cio->create_graph();
  fill_leaf(cg.get(), 8);

  auto pio = lib.create_io("parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 8);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 8);

  auto s0 = create_typed_node(*pg, Ntype_op::Sub);
  s0.set_subnode(cio);
  ix.connect_sink(s0.create_sink_pin(0));
  ix.connect_sink(s0.create_sink_pin(1));

  auto s1 = create_typed_node(*pg, Ntype_op::Sub);
  s1.set_subnode(cio);
  s0.create_driver_pin(2).connect_sink(s1.create_sink_pin(0));
  ix.connect_sink(s1.create_sink_pin(1));
  s1.create_driver_pin(2).connect_sink(pg->get_output_pin("z"));

  ASSERT_EQ(count_op(pg.get(), Ntype_op::Sub), 2u);

  Absorb_stats st;
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get(), cg.get()}), 1000, &st));

  EXPECT_EQ(st.defs_absorbed, 1u) << "one def was below min";
  EXPECT_EQ(st.sites_inlined, 2u) << "both instances of it";
  EXPECT_EQ(st.ge_duplicated, 8u) << "the second copy is 8 GE that did not exist before";
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 0u) << "no instance survives";
  EXPECT_EQ(count_op(pg.get(), Ntype_op::And), 2u) << "the leaf's gate, once per site";
}

// A def at or above min keeps its boundary: it is big enough to be its own ABC
// region, and inlining it would only duplicate logic for nothing.
TEST(ColorAbsorb, BigDefKeepsItsBoundary) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_big");
  auto  cio = make_leaf_io(lib, "big_leaf", 64);
  auto  cg  = cio->create_graph();
  // 64-bit Mult => 4096 GE, comfortably over a 1000-GE floor.
  {
    auto ia = cg->get_input_pin("a");
    auto ib = cg->get_input_pin("b");
    set_bits(ia, 64);
    set_bits(ib, 64);
    auto m = create_typed_node(*cg, Ntype_op::Mult);
    ia.connect_sink(m.create_sink_pin(0));
    ib.connect_sink(m.create_sink_pin(1));
    auto d = m.create_driver_pin(0);
    set_bits(d, 64);
    d.connect_sink(cg->get_output_pin("y"));
  }

  auto pio = lib.create_io("big_parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 64);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 64);
  auto s = create_typed_node(*pg, Ntype_op::Sub);
  s.set_subnode(cio);
  ix.connect_sink(s.create_sink_pin(0));
  ix.connect_sink(s.create_sink_pin(1));
  s.create_driver_pin(2).connect_sink(pg->get_output_pin("z"));

  Absorb_stats st;
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get(), cg.get()}), 1000, &st));

  EXPECT_EQ(st.defs_absorbed, 0u);
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 1u) << "a def over min stays an instance";
}

// A body-less def -- a liberty cell, external IP, an fproperty marker -- has
// nothing to inline and is a black box to ABC by design. It must be skipped, not
// crashed on.
TEST(ColorAbsorb, BlackboxIsSkipped) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_bbox");
  auto  bio = make_leaf_io(lib, "bbox", 8);  // declared, never given a body

  auto pio = lib.create_io("bbox_parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 8);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 8);
  auto s = create_typed_node(*pg, Ntype_op::Sub);
  s.set_subnode(bio);
  ix.connect_sink(s.create_sink_pin(0));
  ix.connect_sink(s.create_sink_pin(1));
  s.create_driver_pin(2).connect_sink(pg->get_output_pin("z"));

  Absorb_stats st;
  // The blackbox is not in the gid map at all -- exactly what an unresolvable def
  // looks like to the pass.
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get()}), 1000, &st));

  EXPECT_EQ(st.defs_absorbed, 0u);
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 1u) << "a black box keeps its boundary";
}

// Stateful is not special: a def full of flops is inlined like any other, and its
// registers land in the parent. `synth` cuts at state afterwards, so they still
// end up on region boundaries -- just the parent's.
TEST(ColorAbsorb, StatefulTinyDefIsAbsorbed) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_seq");
  auto  cio = lib.create_io("reg_leaf");
  cio->add_input("d", 0);
  cio->add_output("q", 1);
  cio->set_bits("d", 8);
  cio->set_bits("q", 8);
  auto cg = cio->create_graph();
  {
    auto id = cg->get_input_pin("d");
    set_bits(id, 8);
    auto f = create_typed_node(*cg, Ntype_op::Flop);
    id.connect_sink(f.create_sink_pin(3));  // din
    auto q = f.create_driver_pin(0);
    set_bits(q, 8);
    q.connect_sink(cg->get_output_pin("q"));
  }

  auto pio = lib.create_io("seq_parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 8);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 8);
  auto s = create_typed_node(*pg, Ntype_op::Sub);
  s.set_subnode(cio);
  ix.connect_sink(s.create_sink_pin(0));
  s.create_driver_pin(1).connect_sink(pg->get_output_pin("z"));

  Absorb_stats st;
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get(), cg.get()}), 1000, &st));

  EXPECT_EQ(st.defs_absorbed, 1u);
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 0u);
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Flop), 1u) << "the child's register is now the parent's";
}

// Children-first, and the weight that decides is the POST-inline one. A 3-level
// chain must collapse whole: if `mid` were weighed at its own body only, a def
// that is actually 2x over the floor could be absorbed.
TEST(ColorAbsorb, GrandchildCollapsesChildrenFirst) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_deep");
  auto  gio = make_leaf_io(lib, "gc", 8);
  auto  gg  = gio->create_graph();
  fill_leaf(gg.get(), 8);

  auto mio = make_leaf_io(lib, "mid", 8);
  auto mg  = mio->create_graph();
  {
    auto ia = mg->get_input_pin("a");
    auto ib = mg->get_input_pin("b");
    set_bits(ia, 8);
    set_bits(ib, 8);
    auto s = create_typed_node(*mg, Ntype_op::Sub);
    s.set_subnode(gio);
    ia.connect_sink(s.create_sink_pin(0));
    ib.connect_sink(s.create_sink_pin(1));
    s.create_driver_pin(2).connect_sink(mg->get_output_pin("y"));
  }

  auto pio = lib.create_io("deep_parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 8);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 8);
  auto s = create_typed_node(*pg, Ntype_op::Sub);
  s.set_subnode(mio);
  ix.connect_sink(s.create_sink_pin(0));
  ix.connect_sink(s.create_sink_pin(1));
  s.create_driver_pin(2).connect_sink(pg->get_output_pin("z"));

  Absorb_stats st;
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get(), mg.get(), gg.get()}), 1000, &st));

  EXPECT_EQ(st.defs_absorbed, 2u) << "both mid and gc are below min";
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 0u) << "the whole chain collapsed into the top";
  EXPECT_EQ(count_op(pg.get(), Ntype_op::And), 1u) << "the grandchild's gate arrived intact";
}

// min=0 means "no floor": nothing is below it, so nothing is absorbed. This is
// the escape hatch that keeps `pass.color synth --set color.min=0` a pure
// annotation pass.
TEST(ColorAbsorb, ZeroMinAbsorbsNothing) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_absorb_off");
  auto  cio = make_leaf_io(lib, "off_leaf", 8);
  auto  cg  = cio->create_graph();
  fill_leaf(cg.get(), 8);

  auto pio = lib.create_io("off_parent");
  pio->add_input("x", 0);
  pio->add_output("z", 1);
  pio->set_bits("x", 8);
  auto pg = pio->create_graph();
  auto ix = pg->get_input_pin("x");
  set_bits(ix, 8);
  auto s = create_typed_node(*pg, Ntype_op::Sub);
  s.set_subnode(cio);
  ix.connect_sink(s.create_sink_pin(0));
  ix.connect_sink(s.create_sink_pin(1));
  s.create_driver_pin(2).connect_sink(pg->get_output_pin("z"));

  Absorb_stats st;
  ASSERT_TRUE(absorb_small_defs(pg.get(), map_of({pg.get(), cg.get()}), 0, &st));

  EXPECT_EQ(st.defs_absorbed, 0u);
  EXPECT_EQ(count_op(pg.get(), Ntype_op::Sub), 1u);
}
