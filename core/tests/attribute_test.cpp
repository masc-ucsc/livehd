//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "attribute.hpp"
#include "lgbench.hpp"
#include "lgedge.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_attr_test : public ::testing::Test {
protected:
  void SetUp() override {
    srand(rseed++);
    mkdir("lgdb_attr", 0755);
  };

  void TearDown() override {
  };
};

TEST_F(Setup_attr_test, test1) {

  unlink("lgdb_attr/lgraph_wname1_id2wd_attr");

  LGBench b("attr_test1");

  Sttribute<Index_ID> wname1("lgdb_attr","wname1");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  for(int i=0;i<1000000;i++) {
    wname1.set(i, fix[i%fix.size()]);
  }

  b.sample("inserts");
  wname1.sync();
  b.sample("sync");
  for(int i=0;i<1100000;i++) {
    EXPECT_EQ(wname1.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=0;i<1000000;i++) {
    auto name = wname1.get(i);
    EXPECT_EQ(name,fix[i%fix.size()]);
  }
  b.sample("get");

  EXPECT_TRUE(true);
}

TEST_F(Setup_attr_test, test2) {

  LGBench b("attr_test2"); // Reload state

  Sttribute<Index_ID> wname1("lgdb_attr","wname1");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  b.sample("inserts");
  wname1.sync();
  b.sample("sync");
  for(int i=0;i<1100000;i++) {
    EXPECT_EQ(wname1.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=0;i<1000000;i++) {
    auto name = wname1.get(i);
    EXPECT_EQ(name,fix[i%fix.size()]);
  }
  b.sample("get");

  wname1.clear();

  for(int i=0;i<1000000;i++) {
    EXPECT_FALSE(wname1.has(i));
  }
  b.sample("check2");

  EXPECT_TRUE(true);
}

TEST_F(Setup_attr_test, test3) {

  Sttribute<Index_ID> wname1("lgdb_attr","wname1");

  for(int i=0;i<1000000;i++) {
    EXPECT_FALSE(wname1.has(i));
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_attr_test, test4) {

  unlink("lgdb_attr/lgraph_wname2_id2wd_attr");

  LGBench b("attr_test1");

  Sttribute<Index_ID> wname2("lgdb_attr","wname2");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  for(int i=0;i<1000000;i++) {
    std::string str = absl::StrCat(fix[i%fix.size()], std::to_string(i));
    wname2.set(i, str);
  }

  b.sample("inserts");
  wname2.sync();
  b.sample("sync");
  for(int i=0;i<1100000;i++) {
    EXPECT_EQ(wname2.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=0;i<1000000;i++) {
    auto name = wname2.get(i);
    std::string str = absl::StrCat(fix[i%fix.size()], std::to_string(i));
    EXPECT_EQ(name,str);
  }
  b.sample("get");

  EXPECT_TRUE(true);

  // wname1 was cleared
  ASSERT_EQ(access("lgdb_attr/lgraph_wname1_id2wd_attr", F_OK),-1);
  ASSERT_EQ(access("lgdb_attr/lgraph_wname1_names_attr", F_OK),-1);

  // wname2 was kept
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_id2wd_attr", F_OK),-1);
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_names_attr", F_OK),-1);
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_names_attr_map", F_OK),-1);
}

