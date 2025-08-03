
#include "attribute.hpp"

#include "gtest/gtest.h"

TEST(AttributeTest, BasicOperations) {
  Attribute<std::string> map;

  // Insert elements
  map.insert(1, "One");
  map.insert(2, "Two");
  map.insert(3, "Three");

  // Check if elements are inserted
  EXPECT_TRUE(map.contains(1));
  EXPECT_TRUE(map.contains(2));
  EXPECT_TRUE(map.contains(3));

  // Check values
  EXPECT_EQ(map.get(1), "One");
  EXPECT_EQ(map.get(2), "Two");
  EXPECT_EQ(map.get(3), "Three");

  // Erase element
  map.erase(2);

  // Check if element is erased
  EXPECT_FALSE(map.contains(2));
}

TEST(AttributeTest, SwitchToSparseAndBack) {
  Attribute<std::string> map;

  // Insert elements
  map.insert(1, "One");
  map.insert(2, "Two");
  map.insert(3, "Three");

  // Switch to sparse
  map.switch_to_sparse();

  // Check if elements are still there
  EXPECT_TRUE(map.contains(1));
  EXPECT_TRUE(map.contains(2));
  EXPECT_TRUE(map.contains(3));

  // Check values
  EXPECT_EQ(map.get(1), "One");
  EXPECT_EQ(map.get(2), "Two");
  EXPECT_EQ(map.get(3), "Three");

  // Switch back to dense
  map.switch_to_dense();

  // Check if elements are still there
  EXPECT_TRUE(map.contains(1));
  EXPECT_TRUE(map.contains(2));
  EXPECT_TRUE(map.contains(3));

  // Check values
  EXPECT_EQ(map.get(1), "One");
  EXPECT_EQ(map.get(2), "Two");
  EXPECT_EQ(map.get(3), "Three");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
