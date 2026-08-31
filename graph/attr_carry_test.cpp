// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Fidelity of the table-driven attribute carrier (graph/attr_carry.hpp).
//
// The point of the header is that a graph->graph rebuild cannot silently drop
// an attribute. So the test stamps EVERY carried tag onto a source node/pin,
// carries, and demands each one come back. The carriers expand the same
// LIVEHD_FOR_EACH_ATTR_TAG list graph/cell.cpp registers from, so a new tag is
// carried without touching this header; the static_asserts below then fail
// until the test stamps and checks it too.

#include "attr_carry.hpp"

#include <string>

#include "ann_place.hpp"
#include "attrs.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace la = livehd::attrs;

namespace {

// Count what the test actually exercises, so "I stamped every tag" is checked
// against the count the header DERIVES from the tag list rather than by
// eyeball: a tag added to LIVEHD_FOR_EACH_ATTR_TAG moves one of these and the
// static_assert below says which value is missing here.
constexpr std::size_t kNodeTagsStamped = gu::kNodeAttrTagCount;
constexpr std::size_t kPinTagsStamped  = gu::kPinAttrTagCount;

}  // namespace

TEST(AttrCarry, EveryNodeAttributeSurvivesARebuild) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_attr_carry_node");
  auto  gio = lib.create_io("carry_node");
  auto  g   = gio->create_graph();

  auto src = gu::create_typed_node(*g, Ntype_op::Or);
  auto dst = gu::create_typed_node(*g, Ntype_op::Or);

  // One value per carried node-kind tag (see attr_carry.hpp).
  src.attr(hhds::attrs::name).set(std::string{"carried_name"});
  src.attr(la::color).set(int32_t{7});
  src.attr(la::synth_region).set(std::string{"reg_A"});
  src.attr(la::synth_region_id).set(uint32_t{11});
  src.attr(la::resynth).set(la::resynth_t::value_type{});
  src.attr(la::native_comb_boundary).set(la::native_comb_boundary_t::value_type{});
  src.attr(la::place).set(Ann_place{1.0F, 2.0F, 3.0F, 4.0F});
  src.attr(la::coloring_info).set(std::string{"info_blob"});
  src.attr(la::proven).set(uint32_t{3});
  src.attr(la::runtime_check).set(uint32_t{5});
  src.attr(la::memory_async_reset).set(uint32_t{1});
  src.attr(la::aggregate_origin).set(std::string{"origin"});
  src.attr(la::aggregate_source_index).set(uint32_t{2});
  src.attr(la::aggregate_lane_ordinal).set(uint32_t{4});
  src.attr(la::aggregate_bit_offset).set(uint32_t{8});
  src.attr(la::aggregate_bit_width).set(uint32_t{16});
  src.attr(la::aggregate_extent).set(uint32_t{32});
  src.attr(la::const_value).set(std::string{"0xdeadbeef"});
  src.attr(la::lut).set(std::string{"1010"});
  src.attr(la::legalize_inlined).set(std::string{"callee.split"});
  gu::set_match(src, 9);  // the NODE overload of the dual-role `match`
  static_assert(kNodeTagsStamped == 21, "a node-kind tag was added to LIVEHD_FOR_EACH_ATTR_TAG: stamp and check it here");

  gu::carry_node_attrs(src, dst);

  EXPECT_EQ(dst.attr(hhds::attrs::name).get(), "carried_name");
  EXPECT_EQ(dst.attr(la::color).get(), 7);
  EXPECT_EQ(dst.attr(la::synth_region).get(), "reg_A");
  EXPECT_EQ(dst.attr(la::synth_region_id).get(), 11u);
  EXPECT_TRUE(dst.attr(la::resynth).has());
  EXPECT_TRUE(dst.attr(la::native_comb_boundary).has());
  EXPECT_TRUE(dst.attr(la::place).has());
  EXPECT_EQ(dst.attr(la::place).get(), Ann_place(1.0F, 2.0F, 3.0F, 4.0F));
  EXPECT_EQ(dst.attr(la::coloring_info).get(), "info_blob");
  EXPECT_EQ(dst.attr(la::proven).get(), 3u);
  EXPECT_EQ(dst.attr(la::runtime_check).get(), 5u);
  EXPECT_EQ(dst.attr(la::memory_async_reset).get(), 1u);
  EXPECT_EQ(dst.attr(la::aggregate_origin).get(), "origin");
  EXPECT_EQ(dst.attr(la::aggregate_source_index).get(), 2u);
  EXPECT_EQ(dst.attr(la::aggregate_lane_ordinal).get(), 4u);
  EXPECT_EQ(dst.attr(la::aggregate_bit_offset).get(), 8u);
  EXPECT_EQ(dst.attr(la::aggregate_bit_width).get(), 16u);
  EXPECT_EQ(dst.attr(la::aggregate_extent).get(), 32u);
  EXPECT_EQ(dst.attr(la::const_value).get(), "0xdeadbeef");
  EXPECT_EQ(dst.attr(la::lut).get(), "1010");
  EXPECT_EQ(dst.attr(la::legalize_inlined).get(), "callee.split");
  EXPECT_EQ(gu::match_of(dst), 9u) << "node-level match (semdiff stamps it, lhd tool reads it) must ride the node";
}

TEST(AttrCarry, EveryPinAttributeSurvivesARebuild) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_attr_carry_pin");
  auto  gio = lib.create_io("carry_pin");
  auto  g   = gio->create_graph();

  auto src = gu::create_typed_node(*g, Ntype_op::Or).create_driver_pin(0);
  auto dst = gu::create_typed_node(*g, Ntype_op::Or).create_driver_pin(0);

  src.attr(la::bits).set(int32_t{13});
  src.attr(la::pin_signed).set(la::pin_signed_t::value_type{});
  src.attr(la::pin_delay).set(float{2.5});
  src.attr(la::pin_const_value).set(std::string{"0b1011"});
  src.attr(la::match).set(uint32_t{42});
  src.attr(la::pin_name).set(std::string{"wire_x"});
  src.attr(la::pin_offset).set(int32_t{6});
  src.attr(la::time_range).set(la::time_range_t::value_type{.min = 0, .max = 3});
  src.attr(la::pending_time).set(la::pending_time_t::value_type{.min = 1, .max = 2});
  static_assert(kPinTagsStamped == 9, "a pin-kind tag was added to LIVEHD_FOR_EACH_ATTR_TAG: stamp and check it here");

  gu::carry_pin_attrs(src, dst);

  EXPECT_EQ(dst.attr(la::bits).get(), 13);
  EXPECT_TRUE(dst.attr(la::pin_signed).has());
  EXPECT_FLOAT_EQ(dst.attr(la::pin_delay).get(), 2.5F);
  EXPECT_EQ(dst.attr(la::pin_const_value).get(), "0b1011");
  EXPECT_EQ(dst.attr(la::match).get(), 42u);
  EXPECT_EQ(dst.attr(la::pin_name).get(), "wire_x");
  EXPECT_EQ(dst.attr(la::pin_offset).get(), 6);
  EXPECT_EQ(dst.attr(la::time_range).get().max, 3);
  EXPECT_EQ(dst.attr(la::pending_time).get().min, 1);
}

// An absent attribute must stay absent -- a carrier that stamps defaults would
// turn "no color" into "color 0", which pass.partition reads as a real region.
TEST(AttrCarry, AbsentAttributesAreNotMaterialized) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_attr_carry_absent");
  auto  gio = lib.create_io("carry_absent");
  auto  g   = gio->create_graph();

  auto src = gu::create_typed_node(*g, Ntype_op::Or);
  auto dst = gu::create_typed_node(*g, Ntype_op::Or);
  src.attr(la::color).set(int32_t{4});  // the only one set

  gu::carry_node_attrs(src, dst);

  EXPECT_EQ(dst.attr(la::color).get(), 4);
  EXPECT_FALSE(dst.attr(hhds::attrs::name).has());
  EXPECT_FALSE(dst.attr(la::proven).has());
  EXPECT_FALSE(dst.attr(la::lut).has());
  EXPECT_FALSE(dst.attr(la::coloring_info).has());
}
