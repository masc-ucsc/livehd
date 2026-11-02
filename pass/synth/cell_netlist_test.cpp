// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_netlist.hpp"

#include "gtest/gtest.h"

namespace livehd::synth {
namespace {

struct Buffer_tree : testing::Test {
  Cell_library library;
  uint32_t     buffer = library.add({"BUF", {"A"}, "Y", 1, 0xAAAAAAAAAAAAAAAAULL});
  uint32_t     invert = library.add({"INV", {"A"}, "Y", 1, 0x5555555555555555ULL});
  Cell_netlist net;

  Buffer_tree() {
    net.signals.resize(5);
    net.sources = {0};
    // Deliberately reverse the creation order: correctness cannot depend on
    // cells being sorted, even when two leaves share the intermediate buffer.
    net.cells   = {
        {buffer, {2}, 4},
        {buffer, {2}, 3},
        {buffer, {1}, 2},
        {buffer, {0}, 1}
    };
    net.outputs = {3, 4};
  }
};

TEST_F(Buffer_tree, RemovesCompleteTerminalTree) {
  const auto aliases = net.boundary_buffer_aliases(library);
  EXPECT_EQ(aliases, (std::vector<uint32_t>{Cell_netlist::kNone, 0, 1, 2, 2}));
}

TEST_F(Buffer_tree, PreservesSharedDriverOfRealLogic) {
  net.cells.front().type = invert;
  const auto aliases     = net.boundary_buffer_aliases(library);
  EXPECT_EQ(aliases,
            (std::vector<uint32_t>{Cell_netlist::kNone, Cell_netlist::kNone, Cell_netlist::kNone, 2, Cell_netlist::kNone}));
}

TEST_F(Buffer_tree, LatchInputsAreTerminalBoundaries) {
  net.outputs.clear();
  net.latches = {
      {0, 3, Cell_netlist::Init::zero}
  };
  const auto aliases = net.boundary_buffer_aliases(library);
  EXPECT_EQ(aliases, (std::vector<uint32_t>{Cell_netlist::kNone, 0, 1, 2, 2}));
}

TEST_F(Buffer_tree, KeepsEveryUpstreamBufferOfAnInternalSink) {
  net.cells.front().type = invert;
  net.outputs            = {1, 2, 3, 4};
  const auto aliases     = net.boundary_buffer_aliases(library);
  EXPECT_EQ(aliases[1], Cell_netlist::kNone);
  EXPECT_EQ(aliases[2], Cell_netlist::kNone);
  EXPECT_EQ(aliases[3], 2);
}

}  // namespace
}  // namespace livehd::synth
