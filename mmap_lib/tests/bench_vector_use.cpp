//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"

#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "robin_hood.hpp"

#include "lbench.hpp"
#include "mmap_map.hpp"
#include "mmap_vector.hpp"

#define NITERS 10
#define NIT 2

void use_std_vector(int max) {

  Lbench b("std_vector" + std::to_string(max));

  std::vector<uint32_t> map;

  for (int n = 1; n < NITERS; ++n) {
    map.clear();
    int conta=0;
    for (int i = 0; i < max; ++i) {
      map.emplace_back(i);
      conta++;
    }
    conta *= NIT;
    for(int i=0;i<NIT;++i) {
      for(auto v:map) {
        (void)v;
        conta--;
      }
    }
    assert(conta==0);
  }
}

void use_robin_map(int max) {

  Lbench b("robin_map " + std::to_string(max));

  robin_hood::unordered_map<uint32_t,uint32_t> map;

  for (int n = 1; n < NITERS; ++n) {
    map.clear();
    int conta=0;
    for (int i = 0; i < max; ++i) {
      map[i] = i;
      conta++;
    }
    conta *= NIT;
    for(int i=0;i<NIT;++i) {
      for(auto v:map) {
        (void)v;
        conta--;
      }
    }
    assert(conta==0);
  }

  fmt::print("load factor {}\n", map.load_factor());
}

void use_mmap_map(int max) {

  Lbench b("mmap_map " + std::to_string(max));

  mmap_lib::map<uint32_t, uint32_t> map; // effemeral (no file backup)
  map.clear();

  for (int n = 1; n < NITERS; ++n) {
    map.clear();
    int conta=0;
    for (int i = 0; i < max; ++i) {
      map.set(i,i);
      conta++;
    }
    conta *= NIT;
    for(int i=0;i<NIT;++i) {
      for(auto v:map) {
        (void)v;
        conta--;
      }
    }
    assert(conta==0);
  }

  fmt::print("load factor {}\n", map.load_factor());
  assert(map.load_factor()>0.2); // Weird bug otherwise
}

void use_abseil_map(int max) {

  Lbench b("use_abseil_map " + std::to_string(max));

  absl::flat_hash_map<uint32_t,uint32_t> map;

  for (int n = 1; n < NITERS; ++n) {
    map.clear();
    int conta=0;
    for (int i = 0; i < max; ++i) {
      map[i] = i;
      conta++;
    }
    conta *= NIT;
    for(int i=0;i<NIT;++i) {
      for(auto v:map) {
        (void)v;
        conta--;
      }
    }
    assert(conta==0);
  }
}

void use_mmap_vector(int max) {

  Lbench b("mmap_vector " + std::to_string(max));

  mmap_lib::vector<uint32_t> map; // effemeral ("bench_vector_use_vector");

  for (int n = 1; n < NITERS; ++n) {
    map.clear();
    int conta=0;
    for (int i = 0; i < max; ++i) {
      map.emplace_back(i);
      conta++;
    }
    conta *= NIT;
    for(int i=0;i<NIT;++i) {
      for(auto v:map) {
        (void)v;
        conta--;
      }
    }
    assert(conta==0);
  }
}

int main(int argc, char **argv) {

  bool run_use_std_vector = false;
  bool run_use_robin_map  = false;
  bool run_use_abseil_map = false;
  bool run_use_mmap_map = false;
  bool run_use_mmap_vector     = false;

  if (argc>1) {
    if (strcasecmp(argv[1],"std_vector")==0)
      run_use_std_vector = true;
    else if (strcasecmp(argv[1],"robin")==0)
      run_use_robin_map = true;
    else if (strcasecmp(argv[1],"abseil")==0)
      run_use_abseil_map = true;
    else if (strcasecmp(argv[1],"mmap_map")==0)
      run_use_mmap_map = true;
    else if (strcasecmp(argv[1],"mmap_vector")==0)
      run_use_mmap_vector = true;
  }else{
    run_use_std_vector  = true;
    run_use_robin_map   = true;
    run_use_mmap_map    = true;
    run_use_abseil_map  = true;
    run_use_mmap_vector = true;
  }

  //const std::vector<int> nums = {100000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000};
  const std::vector<int> nums = {10000, 500000};
  for(auto i:nums) {
    if (run_use_std_vector)
      use_std_vector(i);

    if (run_use_robin_map)
      use_robin_map(i);

    if (run_use_abseil_map)
      use_abseil_map(i);

    if (run_use_mmap_map)
      use_mmap_map(i);

    if (run_use_mmap_vector)
      use_mmap_vector(i);
  }

  return 0;
}

