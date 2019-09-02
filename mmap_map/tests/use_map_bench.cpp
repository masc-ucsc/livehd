//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"


#include <vector>

#include "rng.hpp"
#include "lgbench.hpp"
#include "absl/container/node_hash_map.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "mmap_map.hpp"
#include "robin_hood.hpp"

#include <type_traits>

using Rng = sfc64;

void random_std_map(int max) {
  Rng rng(123);

  LGBench b("random_std_map");

  std::unordered_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_robin_map(int max) {
  Rng rng(123);

  LGBench b("random_robin_map " + std::to_string(max));

  robin_hood::unordered_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_lgraph_map(int max) {
  Rng rng(123);

  LGBench b("random_lgraph_map " + std::to_string(max));

  mmap_map::map<uint32_t,uint32_t> map("use_map_bench_db");
  map.clear();

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100000; ++i) {
      uint32_t pos = rng.uniform<int>(max);
      map.set(pos,i);
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_abseil_map(int max) {
  Rng rng(123);

  LGBench b("random_abseil_map" + std::to_string(max));

  absl::flat_hash_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_ska_map(int max) {
  Rng rng(123);

  LGBench b("random_ska_map" + std::to_string(max));

  ska::flat_hash_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_vector_map(int max) {
  Rng rng(123);

  LGBench b("random_vector_map" + std::to_string(max));

  std::vector<uint32_t> map;
  map.resize(max);

  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < 100000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map[pos] = 0;
    }
  }
}

int main(int argc, char **argv) {

  bool run_random_std_map    = false;
  bool run_random_robin_map  = false;
  bool run_random_lgraph_map = false;
  bool run_random_abseil_map = false;
  bool run_random_ska_map    = false;
  bool run_random_vector_map = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_random_std_map = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_random_robin_map = true;
    else if (strcasecmp(argv[1],"lgraph")==0)
      run_random_lgraph_map = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_random_abseil_map = true;
    else if (strcasecmp(argv[1],"ska")==0)
      run_random_ska_map = true;
    else if (strcasecmp(argv[1],"vector")==0)
      run_random_vector_map = true;
  }else{
    run_random_std_map    = true;
    run_random_robin_map  = true;
    run_random_lgraph_map = true;
    run_random_abseil_map = true;
    run_random_ska_map    = true;
    run_random_vector_map = true;
  }


  std::vector<int> nums = {100000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000};

  //for(int i=100000;i<10000001;i*=10) {
  for(auto i:nums) {
    if (run_random_std_map)
      random_std_map(i);

    if (run_random_robin_map)
      random_robin_map(i);

    if (run_random_lgraph_map)
      random_lgraph_map(i);

    if (run_random_abseil_map)
      random_abseil_map(i);

    if (run_random_ska_map)
      random_ska_map(i);

    if (run_random_vector_map)
      random_vector_map(i);
    fmt::print("\n");
  }

  return 0;
}

