//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bus_name.hpp"

#include "gtest/gtest.h"

namespace bn = livehd::bus_name;

TEST(Bus_name, MintsVerilogStyle) {
  EXPECT_EQ(bn::bit("id_q", 3), "id_q[3]");
  EXPECT_EQ(bn::bit("a.b.q", 0), "a.b.q[0]");  // hierarchy separator unchanged
  EXPECT_EQ(bn::entry("mem", 12), "mem[12]");
  EXPECT_EQ(bn::entry_bit("mem", 2, 7), "mem[2][7]");  // outermost dimension first
  EXPECT_EQ(bn::bit("x[2]", 5), "x[2][5]");            // an already-bracketed base gains a dimension
}

TEST(Bus_name, ParseRoundTrip) {
  for (int64_t i : {0, 1, 9, 10, 15, 123456789}) {
    auto name = bn::bit("rr_state_internal.last_grant", i);
    auto p    = bn::parse_bus_piece(name);
    ASSERT_TRUE(p.has_value()) << name;
    EXPECT_EQ(p->base, "rr_state_internal.last_grant");
    EXPECT_EQ(p->index, i);
    EXPECT_TRUE(p->suffix.empty());
  }
  auto p = bn::parse_bus_piece(bn::entry_bit("m", 3, 4));
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->base, "m[3]");
  EXPECT_EQ(p->index, 4);
  auto q = bn::parse_bus_piece(p->base);
  ASSERT_TRUE(q.has_value());
  EXPECT_EQ(q->base, "m");
  EXPECT_EQ(q->index, 3);
}

TEST(Bus_name, ModelSuffix) {
  // A Liberty cell model inlined under the per-bit instance: `x[3].flop_16`.
  EXPECT_FALSE(bn::parse_bus_piece("x[3].flop_16").has_value());
  auto p = bn::parse_bus_piece("top.x[3].flop_16", true);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->base, "top.x");
  EXPECT_EQ(p->index, 3);
  EXPECT_EQ(p->suffix, "flop_16");
  auto iq = bn::parse_bus_piece("x[0].IQ", true);
  ASSERT_TRUE(iq.has_value());
  EXPECT_EQ(iq->suffix, "IQ");
  // Only ONE trailing segment, directly after the index.
  EXPECT_FALSE(bn::parse_bus_piece("x[3].u.flop_16", true).has_value());
  EXPECT_FALSE(bn::parse_bus_piece("x[3]_flop_16", true).has_value());
  EXPECT_FALSE(bn::parse_bus_piece("x[3].", true).has_value());
}

TEST(Bus_name, CellStateOwner) {
  EXPECT_EQ(bn::cell_state_owner("u.pop_valid.flop_16").value_or("-"), "u.pop_valid");
  EXPECT_EQ(bn::cell_state_owner("pop_valid_cgen1.flop_16").value_or("-"), "pop_valid");  // cgen uniquifier undone
  EXPECT_EQ(bn::cell_state_owner("q.IQ").value_or("-"), "q");
  EXPECT_EQ(bn::cell_state_owner("x_cgen.IQ").value_or("-"), "x_cgen");  // no digits: a real name
  EXPECT_FALSE(bn::cell_state_owner("q").has_value());
  EXPECT_FALSE(bn::cell_state_owner("q.").has_value());
  EXPECT_FALSE(bn::cell_state_owner("x.y[3]").has_value());  // a bus piece is parse_bus_piece's
}

TEST(Bus_name, RejectsNonStandard) {
  for (const char* s : {"x", "x_3", "[3]", "x[]", "x[03]", "x[-1]", "x[3:0]", "x[a]", "x[3]y", "x[1234567890]", "x3]"}) {
    EXPECT_FALSE(bn::parse_bus_piece(s).has_value()) << s;
    EXPECT_FALSE(bn::parse_bus_piece(s, true).has_value()) << s;
  }
}

TEST(Bus_name, FlattenedSeparator) {
  // LEC's canonical key flattens '.' to '_': `a.x[3].flop_16` -> `a_x[3]_flop_16`.
  auto p = bn::parse_bus_piece("a_x[3]_flop_16", true, '_');
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->base, "a_x");
  EXPECT_EQ(p->index, 3);
  EXPECT_EQ(p->suffix, "flop_16");
  EXPECT_FALSE(bn::parse_bus_piece("a_x[3]_u.q", true, '_').has_value());
  EXPECT_FALSE(bn::parse_bus_piece("a_x[3]flop", true, '_').has_value());
}
