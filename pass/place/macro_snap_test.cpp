// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_snap.hpp"

#include <limits>

#include "gtest/gtest.h"
using namespace livehd::placement;
TEST(MacroSnap, ScopeAndSizeSurviveFlattening) {
  auto classes = hierarchy_macro_classes(
      {
          "sub_1.cluster0.wrapper0.ram",
          "sub_2.cluster0.wrapper1.ram",
          "cluster0.other",
          "cluster1.a",
          "sub_3.cluster1.b",
          "gate",
          "cluster0.fixed"
  },
      {{4, 4}, {4, 4}, {8, 4}, {4, 4}, {4, 4}, {4, 4}, {4, 4}},
      {1, 1, 1, 1, 1, 0, 1},
      6);
  EXPECT_EQ(classes[0], classes[1]);
  EXPECT_NE(classes[0], classes[2]);
  EXPECT_NE(classes[0], classes[3]);
  EXPECT_EQ(classes[3], classes[4]);
  EXPECT_EQ(classes[5], -1);
  EXPECT_EQ(classes[6], -1);
  EXPECT_EQ(original_hierarchy_path("sub_42.top.sub_real.sub_8.ram"), "top.sub_real.ram");
  EXPECT_EQ(original_hierarchy_path("__flat___sub_42.top.__flat___sub_8.ram"), "top.ram");
}
TEST(MacroSnap, FlatWrapperSiblingsAndNestedBanks) {
  auto classes = hierarchy_macro_classes(
      {
          "wrap0.ram",
          "wrap1.ram",
          "bank0.u0.a",
          "bank0.u0.b",
          "bank1.u0.a",
          "bank1.u0.b"
  },
      {{4, 4}, {4, 4}, {4, 4}, {4, 4}, {4, 4}, {4, 4}},
      {1, 1, 1, 1, 1, 1},
      6);
  EXPECT_EQ(classes[0], classes[1]);
  EXPECT_EQ(classes[2], classes[3]);
  EXPECT_EQ(classes[4], classes[5]);
  EXPECT_NE(classes[0], classes[2]);
  EXPECT_NE(classes[2], classes[4]);
  EXPECT_THROW(hierarchy_macro_classes({"a"}, {}, {1}, 1), std::invalid_argument);
}
TEST(MacroSnap, RejectInvalidGeometryBeforeGrouping) {
  for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(hierarchy_macro_classes(
                     {
                         "a",
                         "b"
    },
                     {{invalid, 4}, {4, 4}},
                     {1, 1},
                     2),
                 std::invalid_argument);
    EXPECT_THROW(hierarchy_macro_classes(
                     {
                         "a",
                         "b"
    },
                     {{4, invalid}, {4, 4}},
                     {1, 1},
                     2),
                 std::invalid_argument);
  }
}
