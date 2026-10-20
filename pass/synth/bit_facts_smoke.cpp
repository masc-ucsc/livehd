// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// bit_facts: which bits of a word-level value are provably 0, on graphs built by hand.
#include <vector>

#include "bit_facts.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;
namespace bf = livehd::synth::bit_facts;

hhds::Pin_class constant(hhds::Graph& g, int64_t v) { return gu::create_const(g, *Dlop::create_integer(v)); }

std::shared_ptr<hhds::Graph> make_graph(const char* lib_name) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_name);
  auto  io  = lib.create_io("top");
  io->add_input("d", 1);
  io->set_bits("d", 8);
  io->add_input("c", 2);
  io->set_bits("c", 1);
  io->add_output("o", 3);
  auto graph = io->create_graph();
  gu::set_ubits(graph->get_input_pin("d"), 8);
  gu::set_ubits(graph->get_input_pin("c"), 1);
  return graph;
}

hhds::Pin_class cell(hhds::Graph& g, Ntype_op op, int bits, std::initializer_list<hhds::Pin_class> inputs, const char* sink = "as") {
  auto node = gu::create_typed_node(g, op);
  for (const auto& in : inputs) {
    gu::setup_sink_by_name(node, sink).connect_driver(in);
  }
  auto out = node.create_driver_pin(0);
  gu::set_ubits(out, bits);
  return out;
}

std::vector<bool> expect(const char* msb_first) {  // 'z' = known zero
  std::vector<bool> v;
  for (const char* p = msb_first; *p != '\0'; ++p) {
    v.insert(v.begin(), *p == 'z');
  }
  return v;
}

TEST(BitFacts, ConstantWindowOfAValueIsZeroAboveTheWindow) {
  auto g    = make_graph("lgdb_bit_facts_window");
  auto get  = gu::create_get_mask(*g, g->get_input_pin("d"), 2, 5);
  auto pin  = get.create_driver_pin(0);
  gu::set_ubits(pin, 3);
  // read 8 wide: bits 3..7 are above the 3-bit window
  EXPECT_EQ(bf::known_zero_bits(pin, 8), expect("zzzzz???"));
}

TEST(BitFacts, ProductAndSumKeepTheLowZerosOfTheirConstantFactors) {
  auto g    = make_graph("lgdb_bit_facts_lowzeros");
  auto mult = cell(*g, Ntype_op::Mult, 13, {g->get_input_pin("d"), constant(*g, 32)});
  EXPECT_EQ(bf::known_zero_bits(mult, 13), expect("????????zzzzz"));
  auto sum = cell(*g, Ntype_op::Sum, 14, {mult, constant(*g, 32)});
  EXPECT_EQ(bf::known_zero_bits(sum, 14), expect("?????????zzzzz"));
}

TEST(BitFacts, OneBitConditionTimesAConstantIsZeroWhereTheConstantIs) {
  auto g    = make_graph("lgdb_bit_facts_cond");
  auto mult = cell(*g, Ntype_op::Mult, 8, {g->get_input_pin("c"), constant(*g, 0x2a)});
  EXPECT_EQ(bf::known_zero_bits(mult, 8), expect("zz?z?z?z"));
}

TEST(BitFacts, HotmuxWithoutDefaultIsZeroWhereEveryValueIs) {
  auto g    = make_graph("lgdb_bit_facts_hotmux");
  auto hot  = gu::create_typed_node(*g, Ntype_op::Hotmux);
  g->get_input_pin("c").connect_sink(hot.create_sink_pin(0));
  constant(*g, 0x0c).connect_sink(hot.create_sink_pin(1));
  g->get_input_pin("c").connect_sink(hot.create_sink_pin(2));
  constant(*g, 0x06).connect_sink(hot.create_sink_pin(3));
  auto out = hot.create_driver_pin(0);
  gu::set_ubits(out, 8);
  EXPECT_EQ(bf::known_zero_bits(out, 8), expect("zzzz???z"));
}

TEST(BitFacts, DeclaredWidthOfAnUnsignedValueBoundsIt) {
  auto g = make_graph("lgdb_bit_facts_width");
  EXPECT_EQ(bf::known_zero_bits(g->get_input_pin("d"), 12), expect("zzzz????????"));
  EXPECT_EQ(bf::known_zero_bits(g->get_input_pin("c"), 3), expect("zz?"));
}

}  // namespace
