// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <string>

#include "abc_fanin_lookup.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

TEST(AbcFaninLookup, WideSparseInterfaceAndNodeChanges) {
  // std::string(nullptr) is UB: TEST_TMPDIR only exists under `bazel test`, and
  // this binary is also run straight out of bazel-bin while debugging.
  const char* tmp = std::getenv("TEST_TMPDIR");
  auto&       lib = livehd::Hhds_graph_library::instance(std::string(tmp == nullptr ? "." : tmp) + "/fanin_lookup");
  auto        io  = lib.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  auto                    graph   = io->create_graph();
  auto                    a       = graph->get_input_pin("a");
  auto                    b       = graph->get_input_pin("b");
  auto                    wide_io = lib.create_io("wide");
  constexpr hhds::Port_id count   = 4096;
  for (hhds::Port_id i = 0; i < count; ++i) {
    wide_io->add_input("p" + std::to_string(i), 3 * i + 1);
  }
  auto wide = graph->create_node();
  wide.set_subnode(wide_io);
  for (hhds::Port_id i = 0; i < count; ++i) {
    (i % 2 ? b : a).connect_sink(wide.create_sink_pin(3 * i + 1));
  }
  auto small = livehd::graph_util::create_typed_node(*graph, Ntype_op::Not);
  b.connect_sink(small.create_sink_pin(0));
  auto empty = livehd::graph_util::create_typed_node(*graph, Ntype_op::Not);

  livehd::abc::Fanin_lookup lookup;
  for (int pass = 0; pass < 2; ++pass) {
    for (hhds::Port_id i = count; i-- > 0;) {
      EXPECT_EQ(lookup(wide, 3 * i + 1), i % 2 ? b : a);
    }
    EXPECT_TRUE(lookup(wide, 2).is_invalid());
    EXPECT_EQ(lookup(small, 0), b);
    EXPECT_TRUE(lookup(small, 1).is_invalid());
    EXPECT_TRUE(lookup(empty, 0).is_invalid());
  }

  // Folded operators may have several drivers on a sink port. Preserve the
  // importer policy of choosing the first edge, rather than overwriting it.
  auto folded = livehd::graph_util::create_typed_node(*graph, Ntype_op::Or);
  a.connect_sink(folded.create_sink_pin(0));
  b.connect_sink(folded.create_sink_pin(0));
  const auto first = folded.inp_edges().front().driver;
  EXPECT_EQ(lookup(folded, 0), first);
  EXPECT_TRUE(lookup(folded, 1).is_invalid());
}
