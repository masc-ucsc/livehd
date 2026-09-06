// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_partition.hpp"

#include <string>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

// A depth cutoff must not turn shared fan-in into an exponential tree walk.
// Two allocations of the same anonymous graph must also name boundaries alike.
TEST(PartitionNames, DeepReconvergentProducerCones) {
  namespace gu = livehd::graph_util;
  std::vector<std::vector<std::string>> runs;
  for (int run = 0; run < 2; ++run) {
    auto& lib = livehd::Hhds_graph_library::instance("partition_deep_src_" + std::to_string(run));
    auto  io  = lib.create_io("deep");
    io->add_input("a", 1);
    io->set_bits("a", 4);
    io->add_input("b", 2);
    io->set_bits("b", 4);
    io->add_output("y", 3);
    io->set_bits("y", 4);
    auto g = io->create_graph();
    // Perturb node IDs without changing the live graph.
    if (run != 0) {
      for (int i = 0; i < 7; ++i) {
        auto unused = gu::create_typed_node(*g, Ntype_op::Not, 4);
        unused.del_node();
      }
    }
    auto prev  = g->get_input_pin("a");
    auto older = g->get_input_pin("b");
    for (int i = 0; i < 650; ++i) {
      auto node = gu::create_typed_node(*g, Ntype_op::Xor, 4);
      gu::set_color(node, i / 20 + 1);
      auto result = node.create_driver_pin(0);
      gu::set_ubits(result, 4);
      node.create_sink_pin(0).connect_driver(prev);
      node.create_sink_pin(0).connect_driver(older);
      older = prev;
      prev  = result;
    }
    prev.connect_sink(g->get_output_pin("y"));
    auto&                    out = livehd::Hhds_graph_library::instance("partition_deep_dst_" + std::to_string(run));
    std::vector<std::string> names;
    ASSERT_TRUE(Pass_partition::build_decomposition({g}, &out, "deep", false, [&](const livehd::partition::Region_body& body) {
      for (const auto& port : body.inputs) {
        names.push_back(port.name);
      }
      for (const auto& port : body.outputs) {
        names.push_back(port.name);
      }
    }));
    EXPECT_GT(names.size(), 60U);
    runs.push_back(std::move(names));
  }
  EXPECT_EQ(runs[0], runs[1]);
}
