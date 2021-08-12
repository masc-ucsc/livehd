//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include <unistd.h>

#include "absl/container/flat_hash_map.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lrand.hpp"

#include "mmap_bimap.hpp"
#include "mmap_map.hpp"
#include "mmap_str.hpp"

class Setup_mmap_map_test : public ::testing::Test {
protected:
  void SetUp() override {
    mmap_lib::str::setup();
  }

  void TearDown() override {}
};

TEST_F(Setup_mmap_map_test, string_data) {
  Lrand<int> rng;

  bool zero_found = false;
  while (!zero_found) {
    mmap_lib::map<uint32_t, mmap_lib::str> map;
    map.clear();
    absl::flat_hash_map<uint32_t, std::string> map2;

    int conta = 0;
    for (int i = 0; i < 10000; i++) {
      int         key     = rng.max(0xFFFF);
      std::string key_str = std::to_string(key) + "foo";

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key), 1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key, mmap_lib::str(key_str));
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key), 0);
      map2[key] = key_str;
      EXPECT_EQ(map2.count(key), 1);
    }

    map.ref_lock();
    for (const auto &it : map) {
      if (it.getFirst() == 0)
        zero_found = true;
      const auto &key = it.first;
      EXPECT_TRUE(map.has(key));

      const auto val = it.second;
      EXPECT_EQ(val, mmap_lib::str(std::to_string(it.first) + "foo"));
      conta--;
    }
    map.ref_unlock();
    for (const auto &it : map2) {
      if (!map.has(it.first))
        std::cout << "HI\n";
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta, 0);

    fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());
  }
}

TEST_F(Setup_mmap_map_test, string_data_persistance) {
  Lrand<int> rng;

  absl::flat_hash_map<uint32_t, std::string> map2;

  unlink("lgdb_bench/mmap_map_test_sview_data");
  EXPECT_EQ(access("lgdb_bench/mmap_map_test_sview_data", F_OK), -1);

  int conta;
  for (int n = 0; n < 3; n++) {
    mmap_lib::map<uint32_t, mmap_lib::str> map("lgdb_bench", "mmap_map_test_sview_data");
    map.set(3, mmap_lib::str("test"));
    EXPECT_EQ(map.get(3), "test");
    map.clear();
    map2.clear();

    conta = 0;
    for (int i = 0; i < 10000; i++) {
      int         key     = rng.max(0xFFFF);
      std::string key_str = std::to_string(key) + "foo";

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key), 1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key, mmap_lib::str(key_str));
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key), 0);
      map2[key] = key_str;
      EXPECT_EQ(map2.count(key), 1);
    }
  }

  EXPECT_EQ(access("lgdb_bench/mmap_map_test_sview_data", F_OK), 0);

  {
    mmap_lib::map<uint32_t, mmap_lib::str> map("lgdb_bench", "mmap_map_test_sview_data");

    map.ref_lock(); // iteators should work with and without ref_lock before
    for (const auto &it : map) {
      //fmt::print("it.first:{} it.second:{} ref_lock:{}\n",it.first, it.second.to_s(), map.get_lock_num());
      auto txt1 = it.second;
      auto txt2 = map.get(it.first);
      auto it2  = map.find(it.first);
      EXPECT_NE(it2, map.end());
      auto txt4 = it2->second;
      EXPECT_EQ(txt1, txt2);
      EXPECT_EQ(txt1, txt4);

      auto val = map.get(it.first).to_s();
      EXPECT_EQ(val, std::to_string(it.first) + "foo");
      conta--;
    }
    map.ref_unlock();

    for (const auto &it : map2) {
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta, 0);

    fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());
  }
}

TEST_F(Setup_mmap_map_test, string_key) {
  Lrand<int> rng;

  for (int n = 0; n < 4; ++n) {
    mmap_lib::map<mmap_lib::str, uint32_t> map;
    map.clear();
    absl::flat_hash_map<std::string, uint32_t> map2;

    int conta = 0;
    for (int i = 0; i < 10000; i++) {
      int              sz     = rng.max(0xFFFFF);
      std::string      sz_std = "base" + std::to_string(sz) + "foo";
      std::string_view key{sz_std};
      mmap_lib::str    key_str(key);

      if (map.has(key_str)) {
        EXPECT_EQ(map2.count(key), 1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key_str));
      map.set(key_str, sz);
      EXPECT_TRUE(map.has(key_str));

      EXPECT_EQ(map2.count(key), 0);
      map2[key] = sz;
      EXPECT_EQ(map2.count(key), 1);
    }

    map.ref_lock();
    for (const auto &it : map) {
      (void)it;
      EXPECT_EQ(it.first, mmap_lib::str("base" + std::to_string(it.second) + "foo"));
      EXPECT_EQ(map2.count(it.first.to_s()), 1);
      conta--;
    }
    map.ref_unlock();
    for (const auto &it : map2) {
      EXPECT_TRUE(map.has(mmap_lib::str(it.first)));
      EXPECT_EQ(it.second, map.get(mmap_lib::str(it.first)));
    }

    EXPECT_EQ(conta, 0);

    fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());
  }
}

TEST_F(Setup_mmap_map_test, string_key_persistance) {
  Lrand<int> rng;

  mkdir("lgdb_bench", 0755);
  int fd = open("lgdb_bench/mmap_map_test_str", O_WRONLY | O_CREAT, 0600);  // Try to create a bogus mmap
  if (fd >= 0) {
    write(fd, "bunch of garbage", strlen("bunch of garbage"));
    close(fd);
  }

  absl::flat_hash_map<std::string, uint32_t> map2;

  int conta;
  {
    mmap_lib::map<mmap_lib::str, uint32_t> map("lgdb_bench", "mmap_map_test_str");
    map.clear();  // Clear the garbage from before

    conta = 0;
    for (int i = 0; i < 10000; i++) {
      int              sz     = rng.max(0xFFFF);
      std::string      sz_std = std::to_string(sz) + "foo";
      std::string_view key{sz_std};
      mmap_lib::str    key_str(key);

      if (map.has(key_str)) {
        EXPECT_EQ(map2.count(key), 1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key_str));
      map.set(key_str, sz);
      EXPECT_TRUE(map.has(key_str));

      EXPECT_EQ(map2.count(key), 0);
      map2[key] = sz;
      EXPECT_EQ(map2.count(key), 1);
    }
  }

  {
    mmap_lib::map<mmap_lib::str, uint32_t> map("lgdb_bench", "mmap_map_test_str");

    map.ref_lock();
    for (const auto &it : map) {
      (void)it;
      EXPECT_EQ(it.first, mmap_lib::str::concat(it.second, "foo"));
      EXPECT_EQ(map2.count(it.first.to_s()), 1);
      conta--;
    }
    map.ref_unlock();
    for (const auto &it : map2) {
      EXPECT_TRUE(map.has(mmap_lib::str(it.first)));
      EXPECT_EQ(it.second, map.get(mmap_lib::str(it.first)));
    }

    EXPECT_EQ(conta, 0);

    fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());
  }
}

class Big_entry {
public:
  int  f0;
  int  f1;
  int  f2;
  int  f3;
  bool operator==(const Big_entry &o) const { return f0 == o.f0 && f1 == o.f1 && f2 == o.f2 && f3 == o.f3; }
  bool operator!=(const Big_entry &o) const { return f0 != o.f0 || f1 != o.f1 || f2 != o.f2 || f3 != o.f3; };
  Big_entry() {
    f0 = 0;
    f1 = 0;
    f2 = 0;
    f3 = 0;
  }
  Big_entry(int x) : f0(x), f1(x + 1), f2(x + 2), f3(x + 3) {}
  Big_entry(int x, int y) : f0(x), f1(y), f2(y + 1), f3(y + 2) {}
  Big_entry(const Big_entry &o) {
    f0 = o.f0;
    f1 = o.f1;
    f2 = o.f2;
    f3 = o.f3;
  }
#if 1
  Big_entry &operator=(const Big_entry &o) = delete;
  // Big_entry(const Big_entry &o) = delete;
#else
  Big_entry &operator=(const Big_entry &o) {
    f0 = o.f0;
    f1 = o.f1;
    f2 = o.f2;
    f3 = o.f3;
    return *this;
  }
#endif

  template <typename H>
  friend H AbslHashValue(H h, const Big_entry &s) {
    return H::combine(std::move(h), s.f0, s.f1, s.f2, s.f3);
  };
};

TEST_F(Setup_mmap_map_test, big_entry) {
  Lrand<int> rng;

  mmap_lib::map<uint32_t, Big_entry>       map("lgdb_bench", "mmap_map_test_se");
  absl::flat_hash_map<uint32_t, Big_entry> map2;
  auto                                     cap = map.capacity();

  for (auto n = 0; n < 1000; n++) {
    map.clear();
    map.clear();  // 2 calls to clear triggers a delete to map file

    EXPECT_EQ(map.has(33), 0);
    EXPECT_EQ(map.has(0), 0);
    map.set(0, 33);
    EXPECT_EQ(map.has(0), 1);
    EXPECT_EQ(map.has(33), 0);
    EXPECT_EQ(map.get(0).f0, 33);
    EXPECT_EQ(map.get(0).f1, 34);
    map.erase(0);
    EXPECT_EQ(map.has(0), 0);
    map.set(0, {13, 24});
    EXPECT_EQ(map.has(0), 1);
    EXPECT_EQ(map.get(0).f0, 13);
    EXPECT_EQ(map.get(0).f1, 24);
    EXPECT_EQ(map.get(0).f2, 25);

    EXPECT_EQ(map.capacity(), cap);  // No capacity degeneration
    map.erase(0);

    int conta = 0;
    for (int i = 1; i < rng.max(16); ++i) {
      int sz = rng.max(0xFFFFFF);
      if (!map.has(sz)) {
        EXPECT_EQ(map.get_lock_num(), 0);

        {
          map.ref_lock();
          EXPECT_EQ(map.find(sz), map.end());
          map.ref_unlock();
        }

        EXPECT_EQ(map.get_lock_num(), 0);
        map.erase(sz);
        EXPECT_EQ(map.get_lock_num(), 0);
      } else {
        map.ref_lock();
        EXPECT_NE(map.find(sz), map.end());
        map.ref_unlock();

        map.set(sz, {11, sz});
        EXPECT_EQ(map.has(sz), 1);
        conta++;
        EXPECT_EQ(map.get_lock_num(), 0);
      }
    }
    EXPECT_EQ(map.get_lock_num(), 0);

    map.ref_lock();
    for (const auto &it : map) {
      EXPECT_EQ(it.first, it.second.f1);
      conta--;
    }
    map.ref_unlock();

    EXPECT_EQ(map.get_lock_num(), 0);
    EXPECT_EQ(conta, 0);
  }
  map.clear();

  int conta = 0;
  for (int i = 0; i < 10000; i++) {
    int sz = rng.max(0xFFFFFF);

    if (map2.find(sz) == map2.end()) {
      EXPECT_EQ(map.has(sz), 0);
    } else {
      EXPECT_EQ(map.has(sz), 1);
    }

    map.set(sz, sz);

    if (map2.find(sz) == map2.end()) {
      conta++;
      const Big_entry &data = map.get(sz);
      map2.try_emplace(sz, data);
    }
  }

  map.ref_lock();
  for (const auto &it : map) {
    EXPECT_EQ(it.first + 0, it.second.f0);
    EXPECT_EQ(it.first + 1, it.second.f1);
    EXPECT_EQ(it.first + 2, it.second.f2);
    EXPECT_EQ(it.first + 3, it.second.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }
  map.ref_unlock();
  EXPECT_EQ(conta, 0);

  fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());

  map.clear();

  for (const auto &it : map) {
    (void)it;
    EXPECT_TRUE(false);
  }
}

namespace mmap_lib {
template <>
struct hash<Big_entry> {
  size_t operator()(Big_entry const &o) const {
    uint32_t h = o.f0;
    h          = (h << 2) ^ o.f1;
    h          = (h << 2) ^ o.f2;
    h          = (h << 2) ^ o.f3;
    return hash<uint32_t>{}(h);
  }
};
}  // namespace mmap_lib

TEST_F(Setup_mmap_map_test, big_key) {
  Lrand<int>  rng;
  Lrand<bool> rbool;

  mmap_lib::map<Big_entry, uint32_t> map("lgdb_bench", "mmap_map_test_be");
  map.clear();  // Remove data from previous runs
  absl::flat_hash_map<Big_entry, uint32_t> map2;

  int conta = 0;
  for (int i = 0; i < 10000; i++) {
    int       sz = rng.max(0xFFFFFF);
    Big_entry key(sz);

    if (rbool.any()) {
      map.set(key, sz);
    } else {
      map.set({sz}, sz);
    }

    if (map2.find(key) == map2.end()) {
      conta++;
      map2[key] = sz;
    }
  }

  for (const auto &it : map) {
    EXPECT_EQ(it.second + 0, it.first.f0);
    EXPECT_EQ(it.second + 1, it.first.f1);
    EXPECT_EQ(it.second + 2, it.first.f2);
    EXPECT_EQ(it.second + 3, it.first.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }

  EXPECT_EQ(conta, 0);

  fmt::print("load_factor:{} conflict_factor:{}\n", map.load_factor(), map.conflict_factor());
}

TEST_F(Setup_mmap_map_test, lots_of_strings) {
  const std::vector<std::string> roots = {"potato", "__t", "very_long_string", "a"};

  Lrand<int> rseed;
  auto       seed = rseed.any();
  {
    Lrand<int>                                  rng(seed);
    mmap_lib::bimap<uint32_t, mmap_lib::str> bimap("lgdb_bench", "mmap_map_large_sview");
    bimap.clear();  // Remove data from previous runs

    for (uint32_t i = 0; i < 60'000; ++i) {
      std::string str = roots[rng.max(roots.size())];
      str             = str + ":" + std::to_string(i);

      EXPECT_FALSE(bimap.has_key(i));
      EXPECT_FALSE(bimap.has_val(mmap_lib::str(str)));

      bimap.set(i, mmap_lib::str(str));

      EXPECT_TRUE(bimap.has_key(i));
      EXPECT_TRUE(bimap.has_val(mmap_lib::str(str)));

      auto str2 = bimap.get_val(i);
      auto i2   = bimap.get_key(mmap_lib::str(str));

      EXPECT_EQ(str, str2.to_s());
      EXPECT_EQ(i, i2);
    }
  }

  {
    Lrand<int>                                  rng(seed);  // same seed
    mmap_lib::bimap<uint32_t, mmap_lib::str> bimap("lgdb_bench", "mmap_map_large_sview");

    for (uint32_t i = 0; i < 60'000; ++i) {
      std::string str_std = roots[rng.max(roots.size())];
      mmap_lib::str str(str_std + ":" + std::to_string(i));

      EXPECT_TRUE(bimap.has_key(i));
      EXPECT_TRUE(bimap.has_val(str));

      auto        str2 = bimap.get_val(i);
      const auto &i2   = bimap.get_key(str);

      EXPECT_EQ(str, str2);
      EXPECT_EQ(i, i2);
    }
  }
}

TEST_F(Setup_mmap_map_test, serializable) {}
