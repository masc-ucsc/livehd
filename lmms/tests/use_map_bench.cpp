//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"


#include <vector>

#include "rng.hpp"
#include "lgbench.hpp"
#include "dense.hpp"
#include "absl/container/node_hash_map.h"
#include "absl/container/flat_hash_map.h"
#include "sparsehash/dense_hash_map"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"

#include <type_traits>

using Rng = sfc64;

void random_std_map(int max) {
  Rng rng(123);

  LGBench b("random_std_map");

  std::unordered_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_dense_map(int max) {
  Rng rng(123);

  LGBench b("random_dense_map");

  google::dense_hash_map<uint32_t,uint32_t> map;
  map.set_empty_key(0);

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      if (pos==0)
        pos = 33;
      map[pos] = i;
      pos = rng.uniform<int>(max);
      if (pos==0)
        pos = 33;
      map.erase(pos);
    }
  }
}

void random_robin_map(int max) {
  Rng rng(123);

  LGBench b("random_robin_map " + std::to_string(max));

  robin_hood::unordered_flat_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_abseil_map(int max) {
  Rng rng(123);

  LGBench b("random_abseil_map");

  absl::flat_hash_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_ska_map(int max) {
  Rng rng(123);

  LGBench b("random_ska_map");

  ska::flat_hash_map<uint32_t,uint32_t> map;

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map.erase(pos);
    }
  }
}

void random_vector_map(int max) {
  Rng rng(123);

  LGBench b("random_vector_map");

  std::vector<uint32_t> map;
  map.resize(max);

  for (int n = 1; n < 4'000; ++n) {
    for (int i = 0; i < 10'000; ++i) {
      int pos = rng.uniform<int>(max);
      map[pos] = i;
      pos = rng.uniform<int>(max);
      map[pos] = 0;
    }
  }
}

int main(int argc, char **argv) {

  bool run_random_std_map    = false;
  bool run_random_dense_map  = false;
  bool run_random_robin_map  = false;
  bool run_random_abseil_map = false;
  bool run_random_ska_map    = false;
  bool run_random_vector_map = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_random_std_map = true;
    else if (strcasecmp(argv[1],"dense")==0)
      run_random_dense_map = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_random_robin_map = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_random_abseil_map = true;
    else if (strcasecmp(argv[1],"ska")==0)
      run_random_ska_map = true;
    else if (strcasecmp(argv[1],"vector")==0)
      run_random_vector_map = true;
  }else{
    run_random_std_map    = true;
    run_random_dense_map  = true;
    run_random_robin_map  = true;
    run_random_abseil_map = true;
    run_random_ska_map    = true;
    run_random_vector_map = true;
  }

  for(int i=1000;i<10'000'001;i*=1000) {
    if (run_random_std_map)
      random_std_map(i);

    if (run_random_dense_map)
      random_dense_map(i);

    if (run_random_robin_map)
      random_robin_map(i);

    if (run_random_abseil_map)
      random_abseil_map(i);

    if (run_random_ska_map)
      random_ska_map(i);

    if (run_random_vector_map)
      random_vector_map(i);
  }

  return 0;
}

