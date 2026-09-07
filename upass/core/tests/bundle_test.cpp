// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bundle.hpp"

#include <array>

#include "gtest/gtest.h"

TEST(BundleLookup, DensePositionalAndNestedFields) {
  Bundle values;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 256; ++col) {
      const std::array path{bundle_path::make_unnamed(row), bundle_path::make_unnamed(col)};
      values.set(path, *Dlop::create_integer(row * 256 + col));
    }
  }
  const auto& view = values;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 256; ++col) {
      const std::array path{bundle_path::make_unnamed(row), bundle_path::make_unnamed(col)};
      ASSERT_TRUE(view.has_trivial(path));
      EXPECT_TRUE(view.get_trivial(path).same_repr(*Dlop::create_integer(row * 256 + col)));
      values.set(path, *Dlop::create_integer(-col));
      EXPECT_TRUE(view.get_trivial(path).same_repr(*Dlop::create_integer(-col)));
    }
  }
}

TEST(BundleLookup, SparseInsertionOrderDoesNotAliasSlots) {
  Bundle values;
  for (int pos : {5, 0, 3, 1, 2, 4, 1000}) {
    const std::array path{bundle_path::make_unnamed(pos)};
    values.set(path, *Dlop::create_integer(pos + 100));
  }
  for (int pos : {0, 1, 2, 3, 4, 5, 1000}) {
    const std::array path{bundle_path::make_unnamed(pos)};
    ASSERT_TRUE(values.has_trivial(path));
    EXPECT_TRUE(values.get_trivial(path).same_repr(*Dlop::create_integer(pos + 100)));
    values.set(path, *Dlop::create_integer(-pos));
    EXPECT_TRUE(values.get_trivial(path).same_repr(*Dlop::create_integer(-pos)));
  }
  const std::array absent{bundle_path::make_unnamed(6)};
  EXPECT_FALSE(values.has_trivial(absent));
}

TEST(BundleLookup, LargeSparseInsertionAndCopies) {
  Bundle values;
  for (int i = 0; i < 257; ++i) {
    const int        pos = (i * 73) % 257;
    const std::array path{bundle_path::make_unnamed(pos)};
    values.set(path, *Dlop::create_integer(pos + 100));
  }
  Bundle copy = values;
  for (int pos = 0; pos < 257; ++pos) {
    const std::array path{bundle_path::make_unnamed(pos)};
    ASSERT_TRUE(copy.has_trivial(path));
    EXPECT_TRUE(copy.get_trivial(path).same_repr(*Dlop::create_integer(pos + 100)));
    copy.set(path, *Dlop::create_integer(-pos));
    EXPECT_TRUE(values.get_trivial(path).same_repr(*Dlop::create_integer(pos + 100)));
  }
  const std::array extra{bundle_path::make_unnamed(1000)};
  copy.set(extra, *Dlop::create_integer(99));
  EXPECT_TRUE(copy.has_trivial(extra));
  EXPECT_FALSE(values.has_trivial(extra));
}
