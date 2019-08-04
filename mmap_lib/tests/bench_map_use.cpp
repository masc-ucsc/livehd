//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"


#include <vector>

#include "rng.hpp"
#include "lgbench.hpp"
#include "absl/container/node_hash_map.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"

#include "mmap_map.hpp"

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

void random_mmap_map(int max) {
  Rng rng(123);

  {
    LGBench b("random_mmap_map (persistent) " + std::to_string(max));

    mmap_lib::map<uint32_t,uint32_t> map("bench_map_use_mmap.data");
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
  {
    LGBench b("random_mmap_map (effemeral) " + std::to_string(max));

    mmap_lib::map<uint32_t,uint32_t> map;
    map.clear();

    for (int n = 1; n < 400; ++n) {
      for (int i = 0; i < 10'000; ++i) {
        uint32_t pos = rng.uniform<int>(max);
        map.set(pos,i);
        pos = rng.uniform<int>(max);
        map.erase(pos);
      }
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

void random_mmap_vector(int max) {
  Rng rng(123);

  {
    LGBench b("random_mmap_vector (persistent)");

    mmap_lib::vector<uint32_t> map("bench_map_use_vector.data");
    map.resize(max);

    for (int n = 1; n < 400; ++n) {
      for (int i = 0; i < 10'000; ++i) {
        int pos = rng.uniform<int>(max);
        map[pos] = i;
        pos = rng.uniform<int>(max);
        map[pos] = 0;
      }
    }
  }
  {
    LGBench b("random_mmap_vector (effemeral)");

    mmap_lib::vector<uint32_t> map;
    map.resize(max);

    for (int n = 1; n < 400; ++n) {
      for (int i = 0; i < 10'000; ++i) {
        int pos = rng.uniform<int>(max);
        map[pos] = i;
        pos = rng.uniform<int>(max);
        map[pos] = 0;
      }
    }
  }
}

int main(int argc, char **argv) {

  bool run_random_std_map     = false;
  bool run_random_robin_map   = false;
  bool run_random_mmap_vector = false;
  bool run_random_mmap_map    = false;
  bool run_random_abseil_map  = false;
  bool run_random_ska_map     = false;
  bool run_random_vector_map  = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_random_std_map = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_random_robin_map = true;
    else if (strcasecmp(argv[1],"mmap_vector")==0)
      run_random_mmap_vector = true;
    else if (strcasecmp(argv[1],"mmap_map")==0)
      run_random_mmap_map = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_random_abseil_map = true;
    else if (strcasecmp(argv[1],"ska")==0)
      run_random_ska_map = true;
    else if (strcasecmp(argv[1],"vector")==0)
      run_random_vector_map = true;
  }else{
    run_random_std_map     = true;
    run_random_robin_map   = true;
    run_random_mmap_vector = true;
    run_random_mmap_map    = true;
    run_random_abseil_map  = true;
    run_random_ska_map     = true;
    run_random_vector_map  = true;
  }

  const std::vector<int> nums = {100000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000};

  for(auto i:nums) {
    if (run_random_std_map)
      random_std_map(i);

    if (run_random_ska_map)
      random_ska_map(i);

    if (run_random_robin_map)
      random_robin_map(i);

    if (run_random_abseil_map)
      random_abseil_map(i);

    if (run_random_mmap_vector)
      random_mmap_vector(i);

    if (run_random_vector_map)
      random_vector_map(i);

    if (run_random_mmap_map)
      random_mmap_map(i);
  }

  return 0;
}

