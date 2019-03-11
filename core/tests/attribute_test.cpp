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

TEST_F(Setup_attr_test, data_test1) {

  unlink("lgdb_attr/lgraph_dtest1_sparse_attr");
  unlink("lgdb_attr/lgraph_dtest1_dense_attr");
  unlink("lgdb_attr/lgraph_dtest1_dense_attr_max");
  unlink("lgdb_attr/lgraph_dtest1_dense_attr_size");

  unlink("lgdb_attr/lgraph_dtest2_sparse_attr");
  unlink("lgdb_attr/lgraph_dtest2_dense_attr");
  unlink("lgdb_attr/lgraph_dtest2_dense_attr_max");
  unlink("lgdb_attr/lgraph_dtest2_dense_attr_size");

  LGBench b("attr_data_test1");

  struct Data {
    int  a;
    char b;
  };
  Attr_data_raw<uint32_t,Data> dtest1("lgdb_attr","dtest1");
  Attr_data_raw<uint32_t,Data> dtest2("lgdb_attr","dtest2");

  EXPECT_TRUE(!dtest1.is_dense());
  EXPECT_TRUE(!dtest2.is_dense());

  b.sample("setup");

  for(int i=1;i<1000000;i++) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    dtest1.set(i, d);
  }

  b.sample("inserts inorder");

  for(int i=1000000-1;i>0;--i) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    dtest2.set(i, d);
  }

  b.sample("inserts reverse");

  dtest1.sync();
  dtest2.sync();

  b.sample("sync");

  for(int i=1;i<1100000;i++) {
    EXPECT_EQ(dtest1.has(i), i<1000000);
    EXPECT_EQ(dtest2.has(i), i<1000000);
  }

  b.sample("check");

  for(int i=1;i<1000000;i++) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    const auto &d1=dtest1.get(i);
    const auto &d2=dtest2.get(i);
    EXPECT_EQ(d.a,d1.a);
    EXPECT_EQ(d.b,d1.b);
    EXPECT_EQ(d.a,d2.a);
    EXPECT_EQ(d.b,d2.b);
  }
  b.sample("get");

  EXPECT_NE(access("lgdb_attr/lgraph_dtest1_dense_attr_max", F_OK),-1);
  EXPECT_NE(access("lgdb_attr/lgraph_dtest2_dense_attr_max", F_OK),-1);

  // both should be dense, not sparse
  EXPECT_NE(access("lgdb_attr/lgraph_dtest1_dense_attr", F_OK),-1);
  EXPECT_NE(access("lgdb_attr/lgraph_dtest1_dense_attr_size", F_OK),-1);
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest1_sparse_attr", F_OK),-1);

  EXPECT_NE(access("lgdb_attr/lgraph_dtest2_dense_attr", F_OK),-1);
  EXPECT_NE(access("lgdb_attr/lgraph_dtest2_dense_attr_size", F_OK),-1);
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest2_sparse_attr", F_OK),-1);

  dtest1.clear();
  dtest2.clear();

  EXPECT_EQ(access("lgdb_attr/lgraph_dtest1_dense_attr_max", F_OK),-1);
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest1_dense_attr", F_OK),-1);
  EXPECT_NE(access("lgdb_attr/lgraph_dtest1_dense_attr_size", F_OK),-1); // Kept to remember that dense mode is better
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest1_sparse_attr", F_OK),-1);

  EXPECT_EQ(access("lgdb_attr/lgraph_dtest2_dense_attr_max", F_OK),-1);
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest2_dense_attr", F_OK),-1);
  EXPECT_NE(access("lgdb_attr/lgraph_dtest2_dense_attr_size", F_OK),-1);
  EXPECT_EQ(access("lgdb_attr/lgraph_dtest2_sparse_attr", F_OK),-1);
}

TEST_F(Setup_attr_test, data_test2) {

  // Similar to test1 but keep learned information (should go faster)

  LGBench b("attr_data_test2");

  struct Data {
    int  a;
    char b;
  };
  Attr_data_raw<uint32_t,Data> dtest1("lgdb_attr","dtest1");
  Attr_data_raw<uint32_t,Data> dtest2("lgdb_attr","dtest2");

  EXPECT_TRUE(dtest1.is_dense());
  EXPECT_TRUE(dtest2.is_dense());

  b.sample("setup");

  for(int i=1;i<1000000;i+=2) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    dtest1.set(i, d);
  }

  b.sample("inserts inorder");

  for(int i=1000000-1;i>0;i-=2) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    dtest2.set(i, d);
  }

  b.sample("inserts reverse");

  dtest1.sync();
  dtest2.sync();

  b.sample("sync");

  for(int i=1;i<1100000;i+=2) {
    EXPECT_EQ(dtest1.has(i), i<1000000);
    EXPECT_EQ(dtest2.has(i), i<1000000);
  }
  for(int i=2;i<1100000;i+=2) {
    EXPECT_FALSE(dtest1.has(i));
    EXPECT_FALSE(dtest2.has(i));
  }

  b.sample("check");

  for(int i=1;i<1000000;i+=2) {
    Data d;
    d.a = i;
    d.b = i&0xFF;
    const auto &d1=dtest1.get(i);
    const auto &d2=dtest2.get(i);
    EXPECT_EQ(d.a,d1.a);
    EXPECT_EQ(d.b,d1.b);
    EXPECT_EQ(d.a,d2.a);
    EXPECT_EQ(d.b,d2.b);
  }
  b.sample("get");

}

TEST_F(Setup_attr_test, sview_test1) {

  unlink("lgdb_attr/lgraph_wname1_id2wd_attr");

  LGBench b("attr_sview_test1");

  Attr_sview_raw<uint64_t> wname1("lgdb_attr","wname1");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  for(int i=1;i<1000000;i++) {
    wname1.set(i, fix[i%fix.size()]);
  }

  b.sample("inserts");
  wname1.sync();
  b.sample("sync");
  for(int i=1;i<1100000;i++) {
    EXPECT_EQ(wname1.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=1;i<1000000;i++) {
    auto name = wname1.get(i);
    EXPECT_EQ(name,fix[i%fix.size()]);
  }
  b.sample("get");
}

TEST_F(Setup_attr_test, sview_test2) {

  LGBench b("attr_test2"); // Reload state

  Attr_sview_raw<uint64_t> wname1("lgdb_attr","wname1");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  b.sample("inserts");
  wname1.sync();
  b.sample("sync");
  for(int i=1;i<1100000;i++) {
    EXPECT_EQ(wname1.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=1;i<1000000;i++) {
    auto name = wname1.get(i);
    EXPECT_EQ(name,fix[i%fix.size()]);
  }
  b.sample("get");

  wname1.clear();

  for(int i=1;i<1000000;i++) {
    EXPECT_FALSE(wname1.has(i));
  }
  b.sample("check2");

}

TEST_F(Setup_attr_test, sview_test3) {

  Attr_sview_raw<uint64_t> wname1("lgdb_attr","wname1");

  for(int i=1;i<1000000;i++) {
    EXPECT_FALSE(wname1.has(i));
  }

}

TEST_F(Setup_attr_test, sview_test4) {

  unlink("lgdb_attr/lgraph_wname2_id2wd_attr");

  LGBench b("attr_sview_test4");

  Attr_sview_raw<uint64_t> wname2("lgdb_attr","wname2");

  constexpr std::array fix = {"A","B","C","Q","Y","Z"};

  b.sample("setup");

  for(int i=1;i<1000000;i++) {
    std::string str = absl::StrCat(fix[i%fix.size()], std::to_string(i));
    wname2.set(i, str);
  }

  b.sample("inserts");
  wname2.sync();
  b.sample("sync");
  for(int i=1;i<1100000;i++) {
    EXPECT_EQ(wname2.has(i), i<1000000);
  }
  b.sample("check");
  for(int i=1;i<1000000;i++) {
    auto name = wname2.get(i);
    std::string str = absl::StrCat(fix[i%fix.size()], std::to_string(i));
    EXPECT_EQ(name,str);
  }
  b.sample("get");

  // wname1 was cleared
  ASSERT_EQ(access("lgdb_attr/lgraph_wname1_id2wd_attr", F_OK),-1);
  ASSERT_EQ(access("lgdb_attr/lgraph_wname1_names_attr", F_OK),-1);

  // wname2 was kept
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_id2wd_attr", F_OK),-1);
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_names_attr", F_OK),-1);
  ASSERT_NE(access("lgdb_attr/lgraph_wname2_names_attr_map", F_OK),-1);
}



