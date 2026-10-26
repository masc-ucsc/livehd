// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "index_union_find.hpp"

namespace livehd::physical {
TEST(IndexUnionFind, MergesAndCountsComponents) {
  Index_union_find sets(4);
  EXPECT_EQ(sets.add(), 4);
  EXPECT_TRUE(sets.merge(0, 1));
  EXPECT_TRUE(sets.merge(3, 4));
  EXPECT_TRUE(sets.merge(1, 4));
  EXPECT_FALSE(sets.merge(0, 3));  // Already joined through 1 and 4.
  EXPECT_EQ(sets.find(0), sets.find(4));
  EXPECT_NE(sets.find(0), sets.find(2));
  EXPECT_EQ(sets.size(), 5);
}
TEST(IndexUnionFind, LongChainsStayIterative) {
  constexpr size_t size = 1000000;
  Index_union_find sets(size);
  for (size_t i = 1; i < size; ++i) {
    sets.merge(i, i - 1);
  }
  EXPECT_EQ(sets.find(0), sets.find(size - 1));
}
}  // namespace livehd::physical
