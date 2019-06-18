//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "absl/container/flat_hash_set.h"

#include "dense.hpp"

using testing::HasSubstr;

class Setup_map_test : public ::testing::Test {
protected:
  void SetUp() override {
  }

  void TearDown() override {
  }
};

TEST_F(Setup_map_test, ilist_to_set) {

  int vec_list[] = {1,3,5,7,9};

  absl::flat_hash_set<int> my_set;

  const std::vector<int> ilist(&vec_list[0],&vec_list[5]);

  my_set.insert(ilist.begin(),ilist.end());

  for(auto v:vec_list) {
    EXPECT_TRUE(my_set.contains(v));
  }
  EXPECT_FALSE(my_set.contains(100));
}

TEST_F(Setup_map_test, dense_to_set) {

  Dense<int> dense("persist");
  dense.clear();
  dense.emplace_back(1);
  dense.emplace_back(3);
  dense.emplace_back(5);
  dense.emplace_back(7);
  dense.emplace_back(9);

  absl::flat_hash_set<int> my_set;

  my_set.insert(dense.begin(),dense.end());
  dense.sync();

  for(auto v:{1,3,5,7,9}) {
    EXPECT_TRUE(my_set.contains(v));
  }
  EXPECT_FALSE(my_set.contains(100));
}

TEST_F(Setup_map_test, iter_test_set) {

  Dense<int> dense("persist");
  dense.reload(5);

  absl::flat_hash_set<int> my_set;

  my_set.insert(dense.begin(),dense.end());

  for(auto v:{1,3,5,7,9}) {
    EXPECT_TRUE(my_set.contains(v));
  }

  EXPECT_FALSE(my_set.contains(100));

  dense.resize(101);
  dense[100] = 100;
}
