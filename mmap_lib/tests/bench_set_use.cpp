//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <strings.h>
#include <unistd.h>

#include <iostream>
#include <vector>

//#include "bm.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "flat_hash_map.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"
#include "mmap_vset.hpp"  // including the visitor set
#include "robin_hood.hpp"

//#define BENCH_OUT_SIZE 20000 // try bigger sizes
//#define BENCH_INN_SIZE 30000 // try bigger sizes

//#define BENCH_OUT_SIZE 500
//#define BENCH_INN_SIZE 500

#define BENCH_OUT_SIZE 100
#define BENCH_INN_SIZE 100

//#define ABSEIL_USE_MAP
//#define USE_MAP_FALSE

void random_std_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_std_set_" + std::to_string(max));

  std::unordered_set<uint32_t> map;

  // INSERT/ERASE DENSE TEST
  // runs about (500 * 200) times
  // each run:
  //   --> generate rand num, insert that num into the map
  //   --> generate another rand num, erase that num from the map
  //   --> generate another rand num, if this num is not the end of the map, erase
  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
      map.insert(pos);
      pos = rng.max(max);
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
    }
  }
  b.sample("insert/erase dense");

  int conta = 0;

  // TRAVERSAL SPARSE TEST
  // runs (200) times
  // each run:
  //   -->
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
      conta++;
    }
    map.insert(rng.max(max));
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
  }
  b.sample("traversal sparse");
  printf("inserts random %d\n", conta);

  conta = 0;
  for (int i = 0; i < max; ++i) {
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
      conta++;
    }
  }
  b.sample("traversal dense");
  printf("inserts random %d\n", conta);
}

void random_robin_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_robin_set_" + std::to_string(max));

  robin_hood::unordered_map<uint32_t, bool> map;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
      map[pos] = true;
#ifdef USE_MAP_FALSE
      map[rng.max(max)] = false;
      pos               = rng.max(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.max(max);
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
    map[rng.max(max)] = true;
#ifdef USE_MAP_FALSE
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map[pos] = false;
#else
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
#else
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n", conta);
}

void random_mmap_set(int max, std::string_view name) {
  Lrand<int> rng;

  std::string type_test("mmap.map_set");
  if (name.empty())
    type_test += "_effemeral_" + std::to_string(max);
  else
    type_test += "_persistent_" + std::to_string(max);

  Lbench b(type_test);

  mmap_lib::map<uint32_t, bool> map(name.empty() ? "" : "lgdb_bench", name);

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
      map.set(pos, true);
#ifdef USE_MAP_FALSE
      map[rng.max(max)] = false;
      pos               = rng.max(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.max(max);
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
    map.set(rng.max(max), true);
#ifdef USE_MAP_FALSE
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.set(pos, false);
#else
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map.set(rng.max(max), false);
    map.set(rng.max(max), false);
    map.set(rng.max(max), false);
    map.set(rng.max(max), false);
#else
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n", conta);
}

void random_abseil_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_abseil_set_" + std::to_string(max));

#ifdef ABSEIL_USE_MAP
  absl::flat_hash_map<uint32_t, bool> map;
#else
  absl::flat_hash_set<uint32_t> map;
#endif

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
#ifdef ABSEIL_USE_MAP
      map[pos] = true;
#else
      map.insert(pos);
#endif

      pos = rng.max(max);
#ifdef USE_MAP_FALSE
      map[pos] = false;
      pos      = rng.max(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
#ifdef ABSEIL_USE_MAP
    map[rng.max(max)] = true;
#ifdef USE_MAP_FALSE
    map[rng.max(max)] = false;
    auto pos          = rng.max(max);
    if (map.find(pos) != map.end())
      map[pos] = false;
#else
    map.erase(rng.max(max));
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
#else
    map.insert(rng.max(max));
    map.erase(rng.max(max));
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
#else
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n", conta);
}

void random_ska_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_ska_set_" + std::to_string(max));

  ska::flat_hash_map<uint32_t, bool> map;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos  = rng.max(max);
      map[pos] = true;
#ifdef USE_MAP_FALSE
      pos      = rng.max(max);
      map[pos] = false;
      pos      = rng.max(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.max(max);
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
#ifdef USE_MAP_FALSE
    map[rng.max(max)] = false;
#else
    map.erase(rng.max(max));
#endif
    map[rng.max(max)] = true;
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
#else
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n", conta);
}

void random_vector_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_vector_set_" + std::to_string(max));

  std::vector<bool> map;
  map.resize(max);

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos  = rng.max(max);
      map[pos] = true;
      pos      = rng.max(max);
      map[pos] = false;
      pos      = rng.max(max);
      if (map[pos] == true)
        map[pos] = false;
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto e : map) {
      if (e)
        conta++;
    }
    map[rng.max(max)] = true;
    auto pos          = rng.max(max);
    if (map[pos])
      map[pos] = false;
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
    map[rng.max(max)] = false;
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto e : map) {
      if (e)
        conta++;
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n", conta);
}

/* Creates a vset from mmap_lib namespace
 */
void random_mmap_vset(int max, std::string_view name) {
  Lrand<int> rng;

  std::string type_test("mmap_vset ");
  if (name.empty()) {
    type_test += "(effemeral)";
  } else {
    type_test += "(persistent)";
  }

  Lbench                             b(type_test);
  mmap_lib::vset<uint32_t, uint32_t> set("lgdb_bench", name);
  // mmap_lib::map<uint32_t, uint64_t> map("lgdb_bench", name);

  set.clear();

  int conta = 0;  // reset

  // INSERT/ERASE DENSE TEST
  // runs about (BIG * SMALL) times
  // each run:
  //   --> generate rand num, insert that num into the map
  //   --> generate another rand num, erase that num from the map
  //   --> generate another rand num, if this num is not the end of the map, erase
  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
      set.insert(pos);

      pos = rng.max(max);
      set.erase(pos);

      pos = rng.max(max);

      if (set.find(pos))
        set.erase(pos);
    }
  }

  b.sample("insert/erase dense");
  conta = 0;

  // TRAVERSAL SPARSE TEST
  // runs (200) times
  // each run:
  //   --> go through the whole map and increment a variable per map index
  //   --> insert a random number into map
  //   --> generate a random num, if num is not end of map, erase it
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = set.bucket_begin(), end = set.bucket_end(); it != end; ++it) {
      auto hold = set.bucket_get_val(it);
      for (auto j = 0; j < set.bucket_size(); ++j) {
        if ((hold & 1) == 1) {
          conta++;
        }
        hold = hold >> 1;
      }
    }
    set.insert(rng.max(max));

    auto pos = rng.max(max);
    set.erase(pos);
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  // TRAVERSAL DENSE TEST
  // --> First, for every num in domain of max, generate 4 random nums and erase them from map
  // --> runs (200) times -> each time go through whole map and incrememt number per map index
  for (int i = 0; i < max; ++i) {
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = set.bucket_begin(), end = set.bucket_end(); it != end; ++it) {
      auto hold = set.bucket_get_val(it);
      for (auto j = 0; j < set.bucket_size(); ++j) {
        if ((hold & 1) == 1) {
          conta++;
        }
        hold = hold >> 1;
      }
    }
  }
  b.sample("traversal dense");
  printf("inserts random %d\n", conta);
}

#if 0
void random_bm_set(int max) {
  Lrand<int> rng;

  Lbench b("mmap.random_bm_set_" + std::to_string(max));

  bm::bvector<> bm;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos = rng.max(max);
      bm.set_bit(pos);
      pos = rng.max(max);
      if (bm.get_bit(pos)) {
        bm.clear_bit(pos);
      }
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    unsigned value = bm.get_first();
    do {
      conta++;
      value = bm.get_next(value);
    } while(value);
    int pos = rng.max(max);
    bm.set_bit(pos);
    pos = rng.max(max);
    if (bm.get_bit(pos)) {
      bm.clear_bit(pos);
    }
  }

  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    bm.clear_bit(rng.max(max));
    bm.clear_bit(rng.max(max));
    bm.clear_bit(rng.max(max));
    bm.clear_bit(rng.max(max));
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    unsigned value = bm.get_first();
    do {
      conta++;
      value = bm.get_next(value);
    } while(value);
  }

  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
}
#endif

int main(int argc, char **argv) {
  bool run_random_std_set    = false;
  bool run_random_robin_set  = false;
  bool run_random_mmap_set   = false;
  bool run_random_abseil_set = false;
  bool run_random_ska_set    = false;
  bool run_random_vector_set = false;
  bool run_random_mmap_vset  = false;

  if (argc > 1) {
    if (strcasecmp(argv[1], "std") == 0)
      run_random_std_set = true;
    else if (strcasecmp(argv[1], "robin") == 0)
      run_random_robin_set = true;
    else if (strcasecmp(argv[1], "mmap") == 0)
      run_random_mmap_set = true;
    else if (strcasecmp(argv[1], "abseil") == 0)
      run_random_abseil_set = true;
    else if (strcasecmp(argv[1], "ska") == 0)
      run_random_ska_set = true;
    else if (strcasecmp(argv[1], "vector") == 0)
      run_random_vector_set = true;
    else if (strcasecmp(argv[1], "vset") == 0)
      run_random_mmap_vset = true;
  } else {
    run_random_std_set    = true;
    run_random_robin_set  = true;
    run_random_mmap_set   = true;
    run_random_abseil_set = true;
    run_random_ska_set    = true;
    run_random_vector_set = true;
    run_random_mmap_vset  = true;
  }

  for (int i = 1000; i < 1'000'001; i *= 1000) {
    printf("-----------------------------------------------\n");
    printf("        %d max\n", i);
    if (run_random_robin_set)
      random_robin_set(i);

    if (run_random_ska_set)
      random_ska_set(i);

    if (run_random_vector_set)
      random_vector_set(i);

    if (run_random_std_set)
      random_std_set(i);

    if (run_random_abseil_set)
      random_abseil_set(i);

    if (run_random_mmap_set) {
      random_mmap_set(i, "");
      random_mmap_set(i, "mmap_map_set.data");
    }

    if (run_random_mmap_vset) {
      random_mmap_vset(i, "");
      random_mmap_vset(i, "mmap_map_set.data");
    }
  }

  return 0;
}
