//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <set>
#include <vector>

#include "lbench.hpp"
#include "lrand.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"

using testing::HasSubstr;

class Lgtuple_test : public ::testing::Test {
protected:
  std::vector<Node_pin> dpin;
  std::set<std::string> name_set;

  void SetUp() override {
    auto *lg = LGraph::create("lgdb_lgtest","constants","-");

    for(int i=0;i<100;++i) {
      dpin.emplace_back(lg->create_node_const(i).get_driver_pin());
    }

    name_set.clear();
  }

  std::string create_rand_name(int levels) {
    std::vector<std::string> pool = { "a", "xx", " ", "not_a_dot", "long_name", "zz", "not here", "foo:33", "potato", "__b", "nothing", "x", "Zoom" };

    Lrand<uint8_t> rng;

    std::string name;
    while(name.empty() || name_set.count(name)) {
      name.clear();

      for(int i=0;i<levels;++i) {
        std::string part= pool[rng.max(pool.size())];

        part = part + std::to_string(rng.max(32));

        if (name.empty()) {
          name = part;
        }else{
          name = name + "." + part;
        }
      }
    }

    name_set.insert(name);

    return name;
  }

  void TearDown() override {
  }
};


TEST_F(Lgtuple_test, flat1) {
  Lbench b("lgtuple_test.FLAT1");

  std::vector<std::string> names;

  Lgtuple tup("flat");

  for(auto i=0u;i<dpin.size();++i) {
    auto name = create_rand_name(1);

    names.emplace_back(name);

    tup.add(name, dpin[i]);
  }

  for(auto i=0u;i<dpin.size();++i) {
    EXPECT_TRUE(tup.has_dpin(names[i]));
    EXPECT_EQ(tup.get_dpin(names[i]), dpin[i]);
  }
}

TEST_F(Lgtuple_test, flat2) {
  Lbench b("lgtuple_test.FLAT2");

  std::vector<std::string> names;

  Lgtuple tup("flat");

  for(auto i=0u;i<dpin.size();++i) {
    tup.add(i, dpin[i]);
  }

  for(auto i=0u;i<dpin.size();++i) {
    EXPECT_TRUE(tup.has_dpin(i));
    EXPECT_EQ(tup.get_dpin(i) , dpin[i]);
  }
}

TEST_F(Lgtuple_test, flat3) {
  Lbench b("lgtuple_test.FLAT3");

  std::vector<std::string> names;

  Lgtuple tup("flat");

  for(auto i=0u;i<dpin.size();++i) {
    auto name = create_rand_name(1);

    names.emplace_back(name);

    tup.add(i, name, dpin[i]);
  }

  for(auto i=0u;i<dpin.size();++i) {
    EXPECT_TRUE(tup.has_dpin(names[i]));
    EXPECT_TRUE(tup.has_dpin(i));
    EXPECT_EQ(tup.get_dpin(names[i]) , dpin[i]);
    EXPECT_EQ(tup.get_dpin(i) , dpin[i]);
    EXPECT_EQ(tup.get_dpin(i,names[i]) , dpin[i]);
  }
}

TEST_F(Lgtuple_test, hier1) {
  Lbench b("lgtuple_test.HIER1");

  std::vector<std::string> names;

  Lgtuple tup("hier");

  for(auto i=0u;i<dpin.size();++i) {
    auto name = create_rand_name(3);

    names.emplace_back(name);

    tup.add(name, dpin[i]);
  }

  for(auto i=0u;i<dpin.size();++i) {
    EXPECT_TRUE(tup.has_dpin(names[i]));
    EXPECT_EQ(tup.get_dpin(names[i]) , dpin[i]);
  }
}

TEST_F(Lgtuple_test, hier2) {
  Lbench b("lgtuple_test.HIER2");

  std::vector<std::string> names;

  Lgtuple tup("hier");

  for(auto i=0u;i<dpin.size();++i) {
    auto name = create_rand_name(6);

    names.emplace_back(name);

    tup.add(i, name, dpin[i]);
  }

  for(auto i=0u;i<dpin.size();++i) {
    EXPECT_TRUE(tup.has_dpin(names[i]));
    EXPECT_TRUE(tup.has_dpin(i));
    EXPECT_EQ(tup.get_dpin(names[i]) , dpin[i]);
    EXPECT_EQ(tup.get_dpin(i) , dpin[i]);
    EXPECT_EQ(tup.get_dpin(i,names[i]) , dpin[i]);
  }
}

TEST_F(Lgtuple_test, nested1) {
  Lbench b("lgtuple_test.NESTED1");

  auto top = std::make_shared<Lgtuple>("top");
  auto ch1 = std::make_shared<Lgtuple>("ch1");
  auto ch2 = std::make_shared<Lgtuple>("ch2");

  // Tuples can have local fields
  ch1->add("c1_method", dpin[1]);
  top->add("ch1", ch1);

  EXPECT_EQ(top->get_dpin("ch1.c1_method"), dpin[1]);
  EXPECT_EQ(ch1->get_dpin("c1_method"), dpin[1]);

  // You can add a tuple to a subfield
  top->add("local_2", dpin[2]);
  ch1->add("local_3", dpin[3]);

  EXPECT_TRUE(top->has_dpin("local_2"));
  EXPECT_TRUE(ch1->has_dpin("local_3"));

  EXPECT_FALSE(top->has_dpin("local_3")); // copied before

  // Hierarchy of tuples can be added directly
  ch2->add("p3", dpin[3]);
  ch2->add("foo.p4", dpin[4]);
  ch2->add("foo.p5", dpin[5]);

  top->add("xxx", ch2);

  EXPECT_EQ(top->get_dpin("local_2"), dpin[2]);
  EXPECT_EQ(top->get_dpin("ch1.c1_method"), dpin[1]);
  EXPECT_EQ(top->get_dpin("xxx.p3"), dpin[3]);

  EXPECT_EQ(top->get_dpin("xxx.foo.p4"), dpin[4]);
  EXPECT_EQ(top->get_dpin("xxx.foo.p5"), dpin[5]);

  EXPECT_FALSE(top->has_dpin("xxx.foo")); // No dpin at foo

  // Overwriting a method, removes all the children
  top->add("xxx.foo", dpin[6]);
  EXPECT_EQ(top->get_dpin("local_2"), dpin[2]);
  EXPECT_EQ(top->get_dpin("ch1.c1_method"), dpin[1]);
  EXPECT_EQ(top->get_dpin("xxx.p3"), dpin[3]);

  EXPECT_NE(top->get_dpin("xxx.foo.p4"), dpin[4]);
  EXPECT_NE(top->get_dpin("xxx.foo.p5"), dpin[5]);

  EXPECT_TRUE(top->has_dpin("xxx.foo")); // dpin at foo
  EXPECT_FALSE(top->has_dpin("xxx.foo.p4"));
  EXPECT_FALSE(top->has_dpin("xxx.foo.p5"));
}

