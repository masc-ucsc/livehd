//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/container/flat_hash_map.h"
#include "maprobin.hpp"
#include "rng.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_robin_test : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
  }
};

TEST_F(Setup_robin_test, big_entry) {
  Rng rng(123);

  struct Big_entry {
    int f0;
    int f1;
    int f2;
    int f3;
    bool operator==(const Big_entry &o) const {
      return f0 == o.f0
          && f1 == o.f1
          && f2 == o.f2
          && f3 == o.f3;
    }
  };

  robin_hood::unordered_flat_map<uint32_t,Big_entry> map("potato");
  absl::flat_hash_map<uint32_t,Big_entry> map2;
  map.clear();

  map[1].f0=1;
  map[2].f0=2;
  map[3].f0=3;
  map[4].f0=4;
  int conta = 0;
  for(auto it:map) {
    (void)it;
    conta++;
  }
  EXPECT_EQ(conta,4);
  map.clear();

  conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);

    if (map2.find(sz) == map2.end()) {
      EXPECT_EQ(map.count(sz),0);
    }else{
      EXPECT_EQ(map.count(sz),1);
    }

    map[sz].f0 = sz;
    map[sz].f1 = sz+1;
    map[sz].f2 = sz+2;
    map[sz].f3 = sz+3;

    if (map2.find(sz) == map2.end()) {
      conta++;
      map2[sz] = map[sz];
    }
  }

  for(auto it:map) {
    EXPECT_EQ(it.first + 0, it.second.f0);
    EXPECT_EQ(it.first + 1, it.second.f1);
    EXPECT_EQ(it.first + 2, it.second.f2);
    EXPECT_EQ(it.first + 3, it.second.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }
  EXPECT_EQ(conta,0);

  fmt::print("load_factor:{} conflict_factor:{}\n",map.load_factor(), map.conflict_factor());

  map.clear();
  for(auto it:map) {
    (void)it;
    EXPECT_TRUE(false);
  }

}

class Big_entry {
public:
  int f0;
  int f1;
  int f2;
  int f3;
  bool operator==(const Big_entry &o) const {
    return f0 == o.f0
        && f1 == o.f1
        && f2 == o.f2
        && f3 == o.f3;
  }
  bool operator!=(const Big_entry &o) const {
    return f0 != o.f0
        || f1 != o.f1
        || f2 != o.f2
        || f3 != o.f3;
  };

  template <typename H>
  friend H AbslHashValue(H h, const Big_entry &s) {
    return H::combine(std::move(h), s.f0, s.f1, s.f2, s.f3);
  };
};

namespace robin_hood {
template <>
struct hash<Big_entry> {
  size_t operator()(Big_entry const &o) const {
    uint32_t h = o.f0;
    h = (h<<2) ^ o.f1;
    h = (h<<2) ^ o.f2;
    h = (h<<2) ^ o.f3;
    return hash<uint32_t>{}(h);
  }
};
}

TEST_F(Setup_robin_test, big_key) {
  Rng rng(123);

  robin_hood::unordered_flat_map<Big_entry,uint32_t> map("potato");
  absl::flat_hash_map<Big_entry, uint32_t> map2;

  int conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);
    Big_entry key;
    key.f0 = sz;
    key.f1 = sz+1;
    key.f2 = sz+2;
    key.f3 = sz+3;

    map[key] = sz;

    if (map2.find(key) == map2.end()) {
      conta++;
      map2[key] = sz;
    }
  }

  for(auto it:map) {
    EXPECT_EQ(it.second + 0, it.first.f0);
    EXPECT_EQ(it.second + 1, it.first.f1);
    EXPECT_EQ(it.second + 2, it.first.f2);
    EXPECT_EQ(it.second + 3, it.first.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }

  EXPECT_EQ(conta,0);

  fmt::print("load_factor:{} conflict_factor:{}\n",map.load_factor(), map.conflict_factor());
}

