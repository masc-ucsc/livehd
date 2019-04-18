
#include <unistd.h>
#include <strings.h>

#include <vector>
#include <iostream>

#include "bm.h"
#include "rng.hpp"
#include "lgbench.hpp"
#include "dense.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"

using Rng = sfc64;

#define ABSEIL_USE_MAP
//#define USE_MAP_FALSE

void random_std_set(int max) {
  Rng rng(123);

  LGBench b("random_std_set");

  std::unordered_set<uint32_t> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.insert(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
    }
  }

#if 1
  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
      conta++;
    }
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    int pos = rng.uniform<int>(static_cast<uint64_t>(max)) % max;
    map.erase(pos);
  }

  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
      conta++;
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
#endif
}

void random_robin_set(int max) {
  Rng rng(123);

  LGBench b("random_robin_set");

  robin_hood::unordered_map<uint32_t,bool> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = true;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
    }
  }

#if 1
  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    int pos = rng.uniform<int>(static_cast<uint64_t>(max)) % max;
#ifdef USE_MAP_FALSE
    map[pos] = false;
#else
    map.erase(pos);
#endif
  }
#ifdef USE_MAP_FALSE
  for(auto it=map.begin(); it!= map.end(); ++it) {
    if (!it->second)
      map.erase(it);
  }
#endif

  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
#endif
}

void random_abseil_set(int max) {
  Rng rng(123);

  LGBench b("random_abseil_set");

#ifdef ABSEIL_USE_MAP
  absl::flat_hash_map<uint32_t, bool> map;
#else
  absl::flat_hash_set<uint32_t> map;
#endif

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
#ifdef ABSEIL_USE_MAP
      map[pos]=true;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
#else
      map.insert(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
#endif

#ifdef USE_MAP_FALSE
      map[pos]=false;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos]=false;
#else
      map.erase(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
#endif
    }
#if 1
    for (auto it = map.begin(), end = map.end(); it != end;) {
      map.erase(it++);
    }
#else
    while(map.empty()) {
      map.erase(map.begin());
    }
#endif
  }
#if 1
  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    int pos = rng.uniform<int>(static_cast<uint64_t>(max)) % max;
#ifdef USE_MAP_FALSE
    map[pos] = false;
#else
    map.erase(pos);
#endif
  }
#ifdef USE_MAP_FALSE
  for(auto it=map.begin(); it!= map.end(); ++it) {
    if (!it->second)
      map.erase(it);
  }
#endif

  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
#endif
}

void random_ska_set(int max) {
  Rng rng(123);

  LGBench b("random_ska_set");

  ska::flat_hash_map<uint32_t,bool> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = true;
#ifdef USE_MAP_FALSE
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = false;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = false;
#else
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map.erase(pos);
#endif
    }
  }

#if 1
  b.sample("insert/erase dense");
  int conta = 0;
  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

  for (int i = 0; i < max; ++i) {
    int pos = rng.uniform<int>(static_cast<uint64_t>(max)) % max;
#ifdef USE_MAP_FALSE
    map[pos] = false;
#else
    map.erase(pos);
#endif
  }
#ifdef USE_MAP_FALSE
  for(auto it=map.begin(); it!= map.end(); ++it) {
    if (!it->second)
      map.erase(it);
  }
#endif

  for (int i = 0; i < 10'000; ++i) {
    for(auto it=map.begin(); it!= map.end(); ++it) {
#ifdef USE_MAP_FALSE
      if (it->second)
        conta++;
#else
      conta++;
#endif
    }
  }
  b.sample("traversal dense");

  printf("inserts random %d\n",conta);
#endif
}

void random_vector_set(int max) {
  Rng rng(123);

  LGBench b("random_vector_set");

  std::vector<bool> map;
  map.resize(max);

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = true;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = false;
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      map[pos] = false;
    }
  }
}

void random_bm_set(int max) {
  Rng rng(123);

  LGBench b("random_bm_set");

  bm::bvector<> bm;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      bm.set_bit(pos,true);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      bm.clear_bit(pos);
      pos = rng.uniform<int>(static_cast<uint64_t>(n)) % max;
      bm.clear_bit(pos);
    }
  }
}

int main(int argc, char **argv) {

  bool run_random_std_set    = false;
  bool run_random_robin_set  = false;
  bool run_random_abseil_set = false;
  bool run_random_ska_set    = false;
  bool run_random_vector_set = false;
  bool run_random_bm_set     = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_random_std_set = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_random_robin_set = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_random_abseil_set = true;
    else if (strcasecmp(argv[1],"ska")==0)
      run_random_ska_set = true;
    else if (strcasecmp(argv[1],"vector")==0)
      run_random_vector_set = true;
    else if (strcasecmp(argv[1],"bm")==0)
      run_random_bm_set = true;
  }else{
    run_random_std_set    = true;
    run_random_robin_set  = true;
    run_random_abseil_set = true;
    run_random_ska_set    = true;
    run_random_vector_set = true;
    run_random_bm_set     = true;
  }

  for(int i=1000;i<10'000'001;i*=1000) {
    if (run_random_std_set)
      random_std_set(i);

    if (run_random_robin_set)
      random_robin_set(i);

    if (run_random_abseil_set)
      random_abseil_set(i);

    if (run_random_ska_set)
      random_ska_set(i);

    if (run_random_vector_set)
      random_vector_set(i);

    if (run_random_bm_set)
      random_bm_set(i);
  }

  return 0;
}

