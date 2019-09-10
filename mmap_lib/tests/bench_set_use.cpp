//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>
#include <strings.h>

#include <vector>
#include <iostream>

//#include "bm.h"
#include "rng.hpp"
#include "lgbench.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"

#include "mmap_map.hpp"

using Rng = sfc64;

#define BENCH_OUT_SIZE 500
#define BENCH_INN_SIZE 200

//#define ABSEIL_USE_MAP
//#define USE_MAP_FALSE

void random_std_set(int max) {
  Rng rng(123);

  LGBench b("random_std_set");

  std::unordered_set<uint32_t> map;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.uniform<int>(max);
      map.insert(pos);
      pos = rng.uniform<int>(max); map.erase(pos);
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
      conta++;
    }
    map.insert(rng.uniform<int>(max));
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
      conta++;
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
}

void random_robin_set(int max) {
  Rng rng(123);

  LGBench b("random_robin_set");

  robin_hood::unordered_map<uint32_t,bool> map;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.uniform<int>(max);
      map[pos] = true;
#ifdef USE_MAP_FALSE
      map[rng.uniform<int>(max)] = false;
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.uniform<int>(max); map.erase(pos);
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
    map[rng.uniform<int>(max)] = true;
#ifdef USE_MAP_FALSE
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map[pos] = false;
#else
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
#else
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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

  printf("inserts random %d\n",conta);
}

void random_mmap_set(int max, std::string_view name) {
  Rng rng(123);

  std::string type_test("mmap_map_set ");
  if (name.empty())
    type_test += "(effemeral)";
  else
    type_test += "(persistent)";

  LGBench b(type_test);

  mmap_lib::map<uint32_t,bool> map(name.empty()?"":"lgdb_bench", name);

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.uniform<uint32_t>(max);
      map.set(pos,true);
#ifdef USE_MAP_FALSE
      map[rng.uniform<int>(max)] = false;
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.uniform<int>(max); map.erase(pos);
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
      else
        map.erase(it);
#else
      conta++;
#endif
    }
    map.set(rng.uniform<uint32_t>(max),true);
#ifdef USE_MAP_FALSE
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.set(pos,false);
#else
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map.set(rng.uniform<uint32_t>(max),false);
    map.set(rng.uniform<uint32_t>(max),false);
    map.set(rng.uniform<uint32_t>(max),false);
    map.set(rng.uniform<uint32_t>(max),false);
#else
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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

  printf("inserts random %d\n",conta);

}

void random_abseil_set(int max) {
  Rng rng(123);

  LGBench b("random_abseil_set");

#ifdef ABSEIL_USE_MAP
  absl::flat_hash_map<uint32_t, bool> map;
#else
  absl::flat_hash_set<uint32_t> map;
#endif

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.uniform<int>(max);
#ifdef ABSEIL_USE_MAP
      map[pos]=true;
#else
      map.insert(pos);
#endif

      pos = rng.uniform<int>(max);
#ifdef USE_MAP_FALSE
      map[pos]=false;
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map[pos]=false;
#else
      map.erase(pos);
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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
    map[rng.uniform<int>(max)] = true;
#ifdef USE_MAP_FALSE
    map[rng.uniform<int>(max)] = false;
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map[pos] = false;
#else
    map.erase(rng.uniform<int>(max));
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
#else
    map.insert(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    auto pos = rng.uniform<int>(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
#endif
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
#else
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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

  printf("inserts random %d\n",conta);
}

void random_ska_set(int max) {
  Rng rng(123);

  LGBench b("random_ska_set");

  ska::flat_hash_map<uint32_t,bool> map;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = true;
#ifdef USE_MAP_FALSE
      pos = rng.uniform<int>(max);
      map[pos] = false;
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map[pos] = false;
#else
      pos = rng.uniform<int>(max);
      map.erase(pos);
      pos = rng.uniform<int>(max);
      if (map.find(pos) != map.end())
        map.erase(pos);
#endif
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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
    map[rng.uniform<int>(max)] = false;
#else
    map.erase(rng.uniform<int>(max));
#endif
    map[rng.uniform<int>(max)] = true;
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
#ifdef USE_MAP_FALSE
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
#else
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
    map.erase(rng.uniform<int>(max));
#endif
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
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

  printf("inserts random %d\n",conta);
}

void random_vector_set(int max) {
  Rng rng(123);

  LGBench b("random_vector_set");

  std::vector<bool> map;
  map.resize(max);

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = true;
      pos = rng.uniform<int>(max);
      map[pos] = false;
      pos = rng.uniform<int>(max);
      if (map[pos] == true)
        map[pos] = false;
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto e:map) {
      if (e)
        conta++;
    }
    map[rng.uniform<int>(max)] = true;
    auto pos = rng.uniform<int>(max);
    if (map[pos])
      map[pos] = false;
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
    map[rng.uniform<int>(max)] = false;
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto e:map) {
      if (e)
        conta++;
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
}

#if 0
void random_bm_set(int max) {
  Rng rng(123);

  LGBench b("random_bm_set");

  bm::bvector<> bm;

  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      int pos = rng.uniform<int>(max);
      bm.set_bit(pos);
      pos = rng.uniform<int>(max);
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
    int pos = rng.uniform<int>(max);
    bm.set_bit(pos);
    pos = rng.uniform<int>(max);
    if (bm.get_bit(pos)) {
      bm.clear_bit(pos);
    }
  }

  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    bm.clear_bit(rng.uniform<int>(max));
    bm.clear_bit(rng.uniform<int>(max));
    bm.clear_bit(rng.uniform<int>(max));
    bm.clear_bit(rng.uniform<int>(max));
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
  bool run_random_mmap_set = false;
  bool run_random_abseil_set = false;
  bool run_random_ska_set    = false;
  bool run_random_vector_set = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_random_std_set = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_random_robin_set = true;
    else if (strcasecmp(argv[1],"mmap")==0)
      run_random_mmap_set = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_random_abseil_set = true;
    else if (strcasecmp(argv[1],"ska")==0)
      run_random_ska_set = true;
    else if (strcasecmp(argv[1],"vector")==0)
      run_random_vector_set = true;
  }else{
    run_random_std_set    = true;
    run_random_robin_set  = true;
    run_random_mmap_set = true;
    run_random_abseil_set = true;
    run_random_ska_set    = true;
    run_random_vector_set = true;
  }

  for(int i=1000;i<1'000'001;i*=1000) {
    printf("-----------------------------------------------\n");
    printf("        %d max\n",i);
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
      random_mmap_set(i,"");
      random_mmap_set(i,"mmap_map_set.data");
    }
  }

  return 0;
}

