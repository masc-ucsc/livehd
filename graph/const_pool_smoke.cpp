// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace {
Dlop integer(int64_t v) { return *Dlop::create_integer(v); }
}  // namespace

TEST(ConstPin, CanonicalizesAtMint) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_const_pin_canon");
  auto  gio = lib.create_io("canon");
  auto  g   = gio->create_graph();

  // Boolean literals are the Integer -1 / 0 every consumer reads.
  EXPECT_EQ(gu::create_const(*g, *Dlop::create_bool(true)), gu::create_const(*g, integer(-1)));
  EXPECT_EQ(gu::create_const(*g, *Dlop::create_bool(false)), gu::create_const(*g, integer(0)));
  EXPECT_TRUE(gu::const_of(gu::create_const(*g, *Dlop::create_bool(true))).is_integer());

  // A wide-operand fold result and a literal are ONE constant.
  auto wide_one = Dlop::from_pyrope("0x1_0000_0000_0000_0001")->and_op(integer(1));
  EXPECT_EQ(gu::create_const(*g, *wide_one), gu::create_const(*g, integer(1)));

  // Unknown planes and strings are stored verbatim.
  auto unk = gu::create_const(*g, *Dlop::from_pyrope("0ub1?0"));
  EXPECT_TRUE(gu::const_of(unk).has_unknowns());
  auto str = gu::create_const(*g, *Dlop::from_pyrope("'abc'"));
  EXPECT_TRUE(gu::const_of(str).is_string());
  EXPECT_FALSE(str.is_known_false()) << "a String is never 'false'";

  // No value is not a constant.
  EXPECT_THROW((void)gu::create_const(*g, Dlop{}), std::invalid_argument);
  EXPECT_THROW((void)gu::create_const(*g, *Dlop::from_pyrope("nil")), std::invalid_argument);
}

TEST(ConstPin, ProbesAnswerFromThePool) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_const_pin_probe");
  auto  gio = lib.create_io("probe");
  gio->add_input("a", 4);
  auto g = gio->create_graph();

  auto zero = gu::create_const(*g, integer(0));
  auto five = gu::create_const(*g, integer(5));
  EXPECT_TRUE(zero.is_const() && five.is_const());
  EXPECT_TRUE(zero.is_known_false());
  EXPECT_FALSE(zero.is_known_true());
  EXPECT_TRUE(five.is_known_true());
  EXPECT_FALSE(five.is_known_false());
  EXPECT_EQ(gu::const_of(five).to_just_i64(), 5);

  // A live wire, a graph input, an invalid handle: never constant, never
  // "known" anything -- and never a silent 0.
  auto n = gu::create_typed_node(*g, Ntype_op::Sum);
  auto d = n.create_driver_pin(0);
  for (const auto& p : {d, g->get_input_pin("a"), hhds::Pin_class{}}) {
    EXPECT_FALSE(p.is_const());
    EXPECT_FALSE(p.is_known_false());
    EXPECT_FALSE(p.is_known_true());
    EXPECT_EQ(p.const_value(), nullptr);
  }
  EXPECT_DEATH((void)gu::const_of(d), "not a constant");

  // The reference into the pool survives further mints on the same graph.
  const Dlop& five_ref = gu::const_of(five);
  for (int64_t i = 100; i < 5000; ++i) {
    (void)gu::create_const(*g, integer(i));
  }
  EXPECT_EQ(&five_ref, five.const_value());
  EXPECT_EQ(five_ref.to_just_i64(), 5);
}

TEST(ConstPin, WideValuesSurviveGrowthAndThreadTeardown) {
  // Run in a short-lived thread so the test also exercises the TLS lifetime
  // boundary (Dlop word pools are thread_local) which a large Backend compile
  // reaches during process teardown: the graph outlives the minting thread.
  std::thread worker([] {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_const_pin_growth");
    auto  gio = lib.create_io("wide_constants");
    auto  g   = gio->create_graph();

    constexpr int                count = 32768;
    std::vector<hhds::Pin_class> pins;
    std::vector<std::string>     serialized;
    pins.reserve(count);
    serialized.reserve(count);

    // Every value needs more than one 64-bit word (pool-backed storage).
    for (int i = 0; i < count; ++i) {
      auto value = Dlop::from_pyrope(std::format("0x1{:016x}", i));
      ASSERT_TRUE(value);
      serialized.push_back(value->serialize());
      pins.push_back(gu::create_const(*g, *value));
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      EXPECT_EQ(gu::const_of(pins[i]).serialize(), serialized[i]);
    }
    // Dedup must recover the existing pin after the pool has grown large.
    for (const auto i : {size_t{0}, pins.size() / 2, pins.size() - 1}) {
      auto value = Dlop::unserialize(serialized[i]);
      ASSERT_TRUE(value);
      EXPECT_EQ(gu::create_const(*g, *value), pins[i]);
    }
  });
  worker.join();
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_const_pin_growth");
  auto  g   = lib.find_io("wide_constants")->get_graph();
  EXPECT_EQ(g->constant_count(), 32768u);
  EXPECT_TRUE(gu::const_of(gu::create_const(*g, *Dlop::from_pyrope("0x10000000000000007"))).has_unknowns() == false);
}
