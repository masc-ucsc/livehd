//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "fmt/format.h"

#include "absl/container/flat_hash_map.h"
#include "mmap_map.hpp"
#include "mmap_bimap.hpp"
#include "rng.hpp"

using testing::HasSubstr;

class Setup_mmap_map_test : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
  }
};

TEST_F(Setup_mmap_map_test, string_data) {
  Rng rng(123);

  bool zero_found = false;
  while(!zero_found) {
    mmap_lib::map<uint32_t, std::string_view> map;
    map.clear();
    absl::flat_hash_map<uint32_t, std::string> map2;

    int conta = 0;
    for(int i=0;i<10000;i++) {
      int key = rng.uniform<int>(0xFFFF);
      std::string key_str = std::to_string(key)+"foo";

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key),1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key,key_str);
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key),0);
      map2[key] = key_str;
      EXPECT_EQ(map2.count(key),1);
    }

    for(auto it:map) {
      (void)it;
      if(it.getFirst() == 0)
        zero_found = true;
      auto key = map.get_key(it);
      EXPECT_TRUE(map.has(key));

      std::string_view val1 = map.get_sview(it.second);
      std::string_view val  = map.get_sview(it);
      EXPECT_EQ(val1, val);
      EXPECT_EQ(val, std::to_string(it.first) + "foo");
      conta--;
    }
    for(auto it:map2) {
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta,0);

    fmt::print("load_factor:{} conflict_factor:{} txt_size:{}\n",map.load_factor(), map.conflict_factor(),map.txt_size());
  }
}

TEST_F(Setup_mmap_map_test, string_data_persistance) {
  Rng rng(123);

  absl::flat_hash_map<uint32_t, std::string> map2;

  unlink("lgdb_bench/mmap_map_test_sview_data");
  EXPECT_EQ(access("lgdb_bench/mmap_map_test_sview_data", F_OK), -1);

  int conta;
  for(int i=0;i<3;i++) {
    mmap_lib::map<uint32_t, std::string_view> map("lgdb_bench", "mmap_map_test_sview_data");
    auto it = map.set(3,"test");
    EXPECT_EQ(it->first,3);
    EXPECT_NE(it->second,0);
    EXPECT_EQ(map.get(it->first),"test");
    map.clear();
    map2.clear();

    conta = 0;
    for(int i=0;i<10000;i++) {
      int key = rng.uniform<int>(0xFFFF);
      std::string key_str = std::to_string(key)+"foo";

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key),1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key,key_str);
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key),0);
      map2[key] = key_str;
      EXPECT_EQ(map2.count(key),1);
    }
  }

  EXPECT_EQ(access("lgdb_bench/mmap_map_test_sview_data", F_OK), 0);
  EXPECT_EQ(access("lgdb_bench/mmap_map_test_sview_datatxt", F_OK), 0);

  {
    mmap_lib::map<uint32_t, std::string_view> map("lgdb_bench", "mmap_map_test_sview_data");
    for(auto it:map) {
      auto txt1 = map.get_sview(it);
      auto txt2 = map.get(it.first);
      auto txt3 = map.get_sview(it);
      auto it2 = map.find(it.first);
      EXPECT_NE(it2, map.end());
      auto txt4 = map.get_sview(it2);
      EXPECT_EQ(txt1,txt2);
      EXPECT_EQ(txt1,txt3);
      EXPECT_EQ(txt1,txt4);
      std::string_view val = map.get_sview(it);
      EXPECT_EQ(val, std::to_string(it.first) + "foo");
      conta--;
    }
    for(auto it:map2) {
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta,0);

    fmt::print("load_factor:{} conflict_factor:{} txt_size:{}\n",map.load_factor(), map.conflict_factor(),map.txt_size());
  }
}

TEST_F(Setup_mmap_map_test, string_key) {
  Rng rng(123);

  for(int i=0;i<4;++i) {
    mmap_lib::map<std::string_view,uint32_t> map;
    map.clear();
    absl::flat_hash_map<std::string, uint32_t> map2;

    int conta = 0;
    for(int i=0;i<10000;i++) {
      int sz = rng.uniform<int>(0xFFFF);
      std::string sz_str = std::to_string(sz)+"foo";
      std::string_view key{sz_str};

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key),1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key, sz);
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key),0);
      map2[key] = sz;
      EXPECT_EQ(map2.count(key),1);
    }

    for(const auto it:map) {
      (void)it;
      EXPECT_EQ(map.get_key(it), std::to_string(it.second) + "foo");
      EXPECT_EQ(map2.count(map.get_key(it)), 1);
      conta--;
    }
    for(auto it:map2) {
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta,0);

    fmt::print("load_factor:{} conflict_factor:{} txt_size:{}\n",map.load_factor(), map.conflict_factor(),map.txt_size());
  }
}

TEST_F(Setup_mmap_map_test, string_key_persistance) {
  Rng rng(123);

  mkdir("lgdb_bench");
  int fd = open("lgdb_bench/mmap_map_test_str",O_WRONLY | O_CREAT,0600); // Try to create a bogus mmap
  if (fd>=0) {
    write(fd, "bunch of garbage", strlen("bunch of garbage"));
    close(fd);
  }

  absl::flat_hash_map<std::string, uint32_t> map2;

  int conta;
  {
    mmap_lib::map<std::string_view,uint32_t> map("lgdb_bench", "mmap_map_test_str");
    map.clear(); // Clear the garbage from before

    conta = 0;
    for(int i=0;i<10000;i++) {
      int sz = rng.uniform<int>(0xFFFF);
      std::string sz_str = std::to_string(sz)+"foo";
      std::string_view key{sz_str};

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key),1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map.set(key, sz);
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key),0);
      map2[key] = sz;
      EXPECT_EQ(map2.count(key),1);
    }
  }

  {
    mmap_lib::map<std::string_view,uint32_t> map("lgdb_bench", "mmap_map_test_str");

    for(auto it:map) {
      (void)it;
      EXPECT_EQ(map.get_key(it), std::to_string(it.second) + "foo");
      EXPECT_EQ(map2.count(map.get_key(it)), 1);
      conta--;
    }
    for(auto it:map2) {
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta,0);

    fmt::print("load_factor:{} conflict_factor:{} txt_size:{}\n",map.load_factor(), map.conflict_factor(),map.txt_size());
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
  Big_entry() {
    f0= 0;
    f1= 0;
    f2= 0;
    f3= 0;
  }
  Big_entry(int x) : f0(x), f1(x+1), f2(x+2), f3(x+3) {}
  Big_entry(int x,int y) : f0(x), f1(y), f2(y+1), f3(y+2) {}
  Big_entry(const Big_entry &o) {
    f0 = o.f0;
    f1 = o.f1;
    f2 = o.f2;
    f3 = o.f3;
  }
  Big_entry &operator=(const Big_entry &o) {
    f0 = o.f0;
    f1 = o.f1;
    f2 = o.f2;
    f3 = o.f3;
    return *this;
  }

  template <typename H>
  friend H AbslHashValue(H h, const Big_entry &s) {
    return H::combine(std::move(h), s.f0, s.f1, s.f2, s.f3);
  };
};

TEST_F(Setup_mmap_map_test, big_entry) {
  Rng rng(123);

  mmap_lib::map<uint32_t,Big_entry> map("lgdb_bench", "mmap_map_test_se");
  absl::flat_hash_map<uint32_t,Big_entry> map2;
	auto cap = map.capacity();

	for(auto i=0;i<1000;i++) {
		map.clear();
		map.clear(); // 2 calls to clear triggers a delete to map file

		EXPECT_EQ(map.has(33),0);
		EXPECT_EQ(map.has(0),0);
		map.set(0,33);
		EXPECT_EQ(map.has(0),1);
		EXPECT_EQ(map.has(33),0);
    EXPECT_EQ(map.get(0).f0, 33);
    EXPECT_EQ(map.get(0).f1, 34);
		map.erase(0);
		EXPECT_EQ(map.has(0),0);
		map.set(0,{13,24});
		EXPECT_EQ(map.has(0),1);
    EXPECT_EQ(map.get(0).f0, 13);
    EXPECT_EQ(map.get(0).f1, 24);
    EXPECT_EQ(map.get(0).f2, 25);

		EXPECT_EQ(map.capacity(),cap); // No capacity degeneration
    map.erase(0);

		int conta = 0;
		for(int i=1;i<rng.uniform<int>(16);++i) {
			int sz = rng.uniform<int>(0xFFFFFF);
			if (map.find(sz)!=map.end()) {
				map.erase(sz);
			}else{
				map.set(sz,{11,sz});
				EXPECT_EQ(map.has(sz),1);
				conta++;
			}
		}

		for(auto it:map) {
			EXPECT_EQ(it.first, it.second.f1);
			conta--;
		}
		EXPECT_EQ(conta,0);
	}
  map.clear();

  int conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);

    if (map2.find(sz) == map2.end()) {
      EXPECT_EQ(map.has(sz),0);
    }else{
      EXPECT_EQ(map.has(sz),1);
    }

    map.set(sz,sz);

    if (map2.find(sz) == map2.end()) {
      conta++;
      Big_entry data = map.get(sz);
      map2[sz] = data;
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


namespace mmap_lib {
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

TEST_F(Setup_mmap_map_test, big_key) {
  Rng rng(123);

  mmap_lib::map<Big_entry,uint32_t> map("lgdb_bench", "mmap_map_test_be");
	map.clear(); // Remove data from previous runs
  absl::flat_hash_map<Big_entry, uint32_t> map2;

  int conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);
    Big_entry key(sz);

    if (rng.uniform<bool>()) {
      map.set(key,sz);
    }else{
      map.set({sz},sz);
    }

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

TEST_F(Setup_mmap_map_test, lots_of_strings) {

  const std::vector<std::string> roots = {"potato", "__t", "very_long_string", "a"};

  {
    Rng                                rng(123);
    mmap_lib::bimap<uint32_t, std::string_view> bimap("lgdb_bench", "mmap_map_large_sview");
    bimap.clear(); // Remove data from previous runs

    for (uint32_t i = 0; i < 60'000; ++i) {
      std::string str = roots[rng.uniform<int>(roots.size())];
      str             = str + ":" + std::to_string(i);

      EXPECT_FALSE(bimap.has_key(i));
      EXPECT_FALSE(bimap.has_val(str));

      bimap.set(i, str);

      EXPECT_TRUE(bimap.has_key(i));
      EXPECT_TRUE(bimap.has_val(str));

      auto str2 = bimap.get_val_sview(i);
      auto i2   = bimap.get_key(str);

      EXPECT_EQ(str, str2);
      EXPECT_EQ(i, i2);
    }
  }

  {
    Rng                                rng(123); // Same seed
    mmap_lib::bimap<uint32_t, std::string_view> bimap("lgdb_bench", "mmap_map_large_sview");

    for (uint32_t i = 0; i < 60'000; ++i) {
      std::string str = roots[rng.uniform<int>(roots.size())];
      str             = str + ":" + std::to_string(i);

      EXPECT_TRUE(bimap.has_key(i));
      EXPECT_TRUE(bimap.has_val(str));

      auto str2 = bimap.get_val_sview(i);
      auto i2   = bimap.get_key(str);

      EXPECT_EQ(str, str2);
      EXPECT_EQ(i, i2);
    }
  }
}

