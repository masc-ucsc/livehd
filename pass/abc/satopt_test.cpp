// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <cstdlib>
#include <stdexcept>

#include "abc_blast.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace {
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, s;
  hhds::Node_class             mux;
  Fixture(std::string_view name) {
    auto io = lib.create_io(name);
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    g = io->create_graph();
    x = g->get_input_pin("x");
    gu::set_ubits(x, 8);
    auto red = gu::create_typed_node(*g, Ntype_op::Ror);
    x.connect_sink(red.create_sink_pin(0));
    s = red.create_driver_pin(0);
    gu::set_ubits(s, 1);
    gu::set_color(red, 1);
    mux = gu::create_typed_node(*g, Ntype_op::Mux);
    gu::set_color(mux, 2);
    s.connect_sink(mux.create_sink_pin(0));
    x.connect_sink(mux.create_sink_pin(1));
    gu::create_const(*g, *Dlop::create_integer(255)).connect_sink(mux.create_sink_pin(2));
    auto y = mux.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(g->get_output_pin("y"));
  }
};
bool has(const livehd::abc::Satopt_result& result, const hhds::Node_class& n, int arm, int bit, livehd::abc::Mux_fact::Kind kind) {
  auto it = result.mux.find(static_cast<uint64_t>(n.get_debug_nid()));
  if (it == result.mux.end()) {
    return false;
  }
  for (const auto& f : it->second) {
    if (f.arm == arm && f.bit == bit && f.kind == kind) {
      return true;
    }
  }
  return false;
}
}  // namespace
TEST(Satopt, CrossRegionConditionalZerosAndExactReuse) {
  Fixture    f("satopt_conditional");
  const auto dir    = std::string(std::getenv("TEST_TMPDIR")) + "/satopt";
  auto       result = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_GT(result->survivors, 0);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_TRUE(livehd::abc::satopt(f.g.get(), dir)->reused);
  // Change a producer across the boundary while leaving the mux untouched.
  auto red = f.s.get_master_node();
  gu::set_type_op(red, Ntype_op::Not);
  auto changed = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_FALSE(changed->reused);
  EXPECT_FALSE(has(*changed, f.mux, 0, 7, livehd::abc::Mux_fact::Kind::zero));
}
TEST(Satopt, InRegionFactsAreNotCandidates) {
  Fixture f("satopt_one_region");
  gu::set_color(f.s.get_master_node(), 2);
  auto result = livehd::abc::satopt(f.g.get());
  EXPECT_EQ(result->candidates, 0);
  auto explicit_run = livehd::abc::satopt(f.g.get(), {}, true);
  EXPECT_GT(explicit_run->proven, 0);
}
TEST(Satopt, IndependentFreeInputsDoNotBecomeConstants) {
  Fixture f("satopt_free");
  for (const auto& e : f.mux.create_sink_pin(0).inp_edges()) {
    e.del_edge();
  }
  f.x.connect_sink(f.mux.create_sink_pin(0));
  // A nonzero 8-bit selector must see bit 7, not just bit 0.
  auto result = livehd::abc::satopt(f.g.get(), {}, true);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_FALSE(has(*result, f.mux, 1, 7, livehd::abc::Mux_fact::Kind::zero));
}

TEST(Satopt, ComplementedArmsAreProven) {
  Fixture f("satopt_complement");
  auto    inv = gu::create_typed_node(*f.g, Ntype_op::Not);
  f.x.connect_sink(inv.create_sink_pin(0));
  auto value = inv.create_driver_pin(0);
  gu::set_ubits(value, 8);
  gu::set_color(inv, 1);
  for (const auto& e : f.mux.create_sink_pin(2).inp_edges()) {
    e.del_edge();
  }
  value.connect_sink(f.mux.create_sink_pin(2));
  auto result = livehd::abc::satopt(f.g.get());
  for (int bit = 0; bit < 8; ++bit) {
    EXPECT_TRUE(has(*result, f.mux, 1, bit, livehd::abc::Mux_fact::Kind::complement));
  }
}

TEST(Satopt, SharedHotmuxBlasterUsesEveryControlBit) {
  Fixture f("satopt_hotmux_control");
  for (const auto& e : f.mux.inp_edges()) {
    e.del_edge();
  }
  gu::set_type_op(f.mux, Ntype_op::Hotmux);
  f.x.connect_sink(f.mux.create_sink_pin(0));
  gu::create_const(*f.g, *Dlop::create_integer(255)).connect_sink(f.mux.create_sink_pin(1));
  gu::create_const(*f.g, *Dlop::create_integer(0)).connect_sink(f.mux.create_sink_pin(2));
  struct Boolean {
    int        values[2] = {0, 1};
    const int* zero() const { return &values[0]; }
    const int* one() const { return &values[1]; }
    const int* inv(const int* a) const { return &values[*a ^ 1]; }
    const int* and_(const int* a, const int* b) const { return &values[*a & *b]; }
    const int* or_(const int* a, const int* b) const { return &values[*a | *b]; }
    const int* xor_(const int* a, const int* b) const { return &values[*a ^ *b]; }
  } ops;
  for (int input = 0; input < 256; ++input) {
    std::vector<const int*> slots(8);
    auto                    read = [&](const hhds::Pin_class& p, int bit) {
      const bool value = p.is_const() ? gu::const_of(p).bit_test(bit) : ((input >> bit) & 1) != 0;
      return value ? ops.one() : ops.zero();
    };
    auto refuse = [](const hhds::Node_class&,
                     std::string_view,
                     std::string_view,
                     std::string_view,
                     std::string_view       = {},
                     const hhds::Pin_class& = {},
                     std::string_view       = {}) { throw std::runtime_error("unsupported test cell"); };
    auto shift  = [](const hhds::Node_class&, std::string_view, const Dlop&, const hhds::Pin_class&) {
      throw std::runtime_error("unsupported test shift");
    };
    livehd::abc::blast_comb(f.mux, 8, slots, ops, read, {}, {}, {}, refuse, shift);
    for (const int* value : slots) {
      EXPECT_EQ(*value, input != 0) << input;
    }
  }
}
