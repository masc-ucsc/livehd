// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// bit_facts: which bits of a word-level value are provably 0, on graphs built by hand.
#include <string>
#include <vector>

#include "bit_facts.hpp"
#include "color_boundary.hpp"
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

hhds::Pin_class cell(hhds::Graph& g, Ntype_op op, int bits, std::initializer_list<hhds::Pin_class> inputs,
                     const char* sink = "as") {
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
  auto g   = make_graph("lgdb_bit_facts_window");
  auto get = gu::create_get_mask(*g, g->get_input_pin("d"), 2, 5);
  auto pin = get.create_driver_pin(0);
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
  auto g   = make_graph("lgdb_bit_facts_hotmux");
  auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux);
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

// ---- known ones, word selects and the color-boundary rewrites ----

std::string text(const bf::Known& k) {  // MSB first: '0' / '1' known, '?' not
  std::string out;
  for (size_t i = k.zero.size(); i-- > 0;) {
    out.push_back(k.zero[i] ? '0' : k.one[i] ? '1' : '?');
  }
  return out;
}

TEST(BitFacts, KnownOnesComeThroughOrAndAndConstantWindows) {
  auto g = make_graph("lgdb_bit_facts_ones");
  auto d = g->get_input_pin("d");
  auto v = cell(*g, Ntype_op::Or, 8, {cell(*g, Ntype_op::And, 8, {d, constant(*g, 0xF0)}), constant(*g, 0x05)});
  EXPECT_EQ(text(bf::known_bits(v, 8)), "????0101");
  auto get = gu::create_get_mask(*g, v, 0, 3);
  auto low = get.create_driver_pin(0);
  gu::set_ubits(low, 3);
  EXPECT_EQ(text(bf::known_bits(low, 4)), "0101");  // zero above the 3-bit window
}

// v[idx] over four 4-bit lanes, each `(x & 0b1000) | 0b0001`: whatever lane the index picks, bit 0 is 1 and bits 2..1
// are 0. The amount is the affine `idx * 4` of a word select.
TEST(BitFacts, AWordSelectKeepsWhatEveryLaneAgreesOn) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bit_facts_wordsel");
  auto  io  = lib.create_io("top");
  io->add_input("v", 1);
  io->set_bits("v", 16);
  io->add_input("idx", 2);
  io->set_bits("idx", 2);
  io->add_output("o", 3);
  auto g = io->create_graph();
  gu::set_ubits(g->get_input_pin("v"), 16);
  gu::set_ubits(g->get_input_pin("idx"), 2);
  auto packed = cell(*g,
                     Ntype_op::Or,
                     16,
                     {cell(*g, Ntype_op::And, 16, {g->get_input_pin("v"), constant(*g, 0x8888)}), constant(*g, 0x1111)});
  auto amount = cell(*g, Ntype_op::Mult, 4, {g->get_input_pin("idx"), constant(*g, 4)});
  auto shift  = gu::create_typed_node(*g, Ntype_op::SRA);
  gu::setup_sink_by_name(shift, "a").connect_driver(packed);
  gu::setup_sink_by_name(shift, "b").connect_driver(amount);
  auto shifted = shift.create_driver_pin(0);
  gu::set_ubits(shifted, 16);
  auto get  = gu::create_get_mask(*g, shifted, 0, 4);
  auto lane = get.create_driver_pin(0);
  gu::set_ubits(lane, 4);
  EXPECT_EQ(text(bf::known_bits(lane, 4)), "?001");
}

TEST(BitFacts, KnownPinSpellsOutTheKnownBits) {
  auto g       = make_graph("lgdb_bit_facts_known_pin");
  auto d       = g->get_input_pin("d");
  auto v       = cell(*g, Ntype_op::Or, 8, {cell(*g, Ntype_op::And, 8, {d, constant(*g, 0xF0)}), constant(*g, 0x05)});
  // Reading the rebuilt value tells the same facts through one And and one Or.
  auto rebuilt = bf::known_pin(*g, v, bf::known_bits(v, 8));
  EXPECT_EQ(gu::type_op_of(rebuilt.get_master_node()), Ntype_op::Or);
  EXPECT_EQ(text(bf::known_bits(rebuilt, 8)), "????0101");
  // Every bit known: the constant itself.
  auto all = cell(*g, Ntype_op::And, 8, {d, constant(*g, 0)});
  EXPECT_TRUE(bf::known_pin(*g, all, bf::known_bits(all, 8)).is_const());
}

// Color 1 computes an 8-bit value; color 2 reads only its low four bits. The reader is handed `value & 0x0F` (a
// color-1 cell), so color 1 exports four constant zeros; and the half of the value that is a known constant is read as
// one by color 2 through a cell of its own.
TEST(ColorBoundary, UnreadBitsAreClearedAndKnownBitsAreReadAsConstants) {
  auto       g       = make_graph("lgdb_color_boundary");
  auto       d       = g->get_input_pin("d");
  const auto colored = [](const hhds::Pin_class& p, int color) {
    gu::set_color(p.get_master_node(), color);
    return p;
  };
  // unread bits
  auto wide = colored(cell(*g, Ntype_op::Xor, 8, {d, constant(*g, 0xFF)}), 1);
  auto get  = gu::create_get_mask(*g, wide, 0, 4);
  gu::set_color(get, 2);
  auto low = get.create_driver_pin(0);
  gu::set_ubits(low, 4);
  // known bits: (d & 0xF0) | 1, read whole by a color-2 Xor
  auto known
      = colored(cell(*g, Ntype_op::Or, 8, {colored(cell(*g, Ntype_op::And, 8, {d, constant(*g, 0xF0)}), 1), constant(*g, 1)}), 1);
  auto reader = colored(cell(*g, Ntype_op::Xor, 8, {known, low}), 2);
  reader.connect_sink(g->get_output_pin("o"));

  const auto stats = livehd::synth::trim_color_boundaries(g.get());
  // `wide` (four unread bits) and `known` (color 2 reads its four constant bits as constants, so asks color 1 for
  // the other four only)
  EXPECT_EQ(stats.unread_values, 2U);
  EXPECT_EQ(stats.unread_bits, 8U);
  EXPECT_EQ(stats.constant_values, 1U);
  EXPECT_EQ(stats.constant_bits, 4U);  // the four known bits of `known`; `wide` has none
  // the Get_mask now reads a color-1 And of the wide value
  const auto handed = gu::get_driver_of_sink_name(get, "a");
  EXPECT_NE(handed, wide);
  EXPECT_EQ(gu::type_op_of(handed.get_master_node()), Ntype_op::And);
  EXPECT_EQ(gu::color_of(handed.get_master_node()), 1);
  // the color-2 Xor reads the known value through color-2 cells
  bool rebuilt = false;
  for (const auto& in_pin : reader.get_master_node().inp_sorted_pins()) {
    for (const auto& drv : in_pin.get_driver_pins()) {
      if (drv != low) {
        EXPECT_NE(drv, known);
        EXPECT_EQ(gu::color_of(drv.get_master_node()), 2);
        EXPECT_EQ(text(bf::known_bits(drv, 8)), "????0001");
        rebuilt = true;
      }
    }
  }
  EXPECT_TRUE(rebuilt);
  // a second run finds nothing new
  const auto again = livehd::synth::trim_color_boundaries(g.get());
  EXPECT_EQ(again.unread_values, 0U);
  EXPECT_EQ(again.constant_values, 0U);
}

}  // namespace
