//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"


#include <vector>

#include "lrand.hpp"
#include "lbench.hpp"
#include "absl/container/node_hash_map.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"

#include "mmap_map.hpp"
#include "mmap_vector.hpp"

#include <type_traits>

#define BENCHSIZE 10000

#define TESTNUMS 5

/* 
 * Creates an unordered map from std namespace
 * puts some elements in it, then clears the map a bunch of times
 */
void random_std_map(int max) {
  Lrand<int> rng;
  Lbench b("mmap.random_std_map_" + std::to_string(max));

  std::unordered_map<uint32_t,uint32_t> map;
  
  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < TESTNUMS; ++i) {
      int pos = rng.max(max);
      map[pos] = i;
      pos = rng.max(max);
      map.erase(pos);
    }
  }
}


/*
 * Using a map from mmap_lib
 * FOCUS on this one
 */
void random_mmap_map(int max) {
  Lrand<int> rng;

  {
    Lbench b("mmap.random_mmap_map_persistent_" + std::to_string(max));

	// declaring a map from mmap_lib (persistent)
	// details: map<some type (key?), some type (value?)>
	// * two new arguments after map("x", "y"), used onlr for persistent
	//   persistent means saving data even after program ends
    mmap_lib::map<uint32_t,uint32_t> map("lgdb_bench","bench_map_use_mmap.data");
    map.clear();
    
	std::cout << "max is: " << max << std::endl;
   
   //Run the test 3 times
  	for (int n = 1; n < 4; ++n) {
      
	  // i from 0 to 10:
	  //   generate random pos with max(MAX) -> returns a rand number in range of MAX
	  //   insert i to map[pos]
	  //   regenerate pos with max() --> why? maybe just to be more diverse
	  //   clear map[pos] 
      for (int i = 0; i < TESTNUMS; ++i) {
      	uint32_t pos = rng.max(max);
	    
		std::cout << "first pass pos is: " << pos;
      	
		map.set(pos,i);

      	pos = rng.max(max);
	    
		std::cout << " second pass pos is: " << pos << std::endl;
      	
		map.erase(pos);
      }
    }
  }
  //----------
  {
    Lbench b("mmap.random_mmap_map_effemeral_" + std::to_string(max));

	// declaring a map from mmap_lib (effemeral)
	// effemeral means destroy after program ends
    mmap_lib::map<uint32_t,uint32_t> map;
    map.clear();

    for (int n = 1; n < 100; ++n) {
      for (int i = 0; i < BENCHSIZE; ++i) {
        uint32_t pos = rng.max(max);
        map.set(pos,i);
        pos = rng.max(max);
        map.erase(pos);
      }
    }
  }
}

/*
 * Testing mmap_vector
 */
void random_mmap_vector(int max) {
  Lrand<int> rng;
  {
    Lbench b("mmap.random_mmap_vector_persistent_" + std::to_string(max));
    mmap_lib::vector<uint32_t> map("lgdb_bench","bench_map_use_vector.data");
    map.reserve(max);
    for (int n = 1; n < 100; ++n) {
      for (int i = 0; i < BENCHSIZE; ++i) {
        int pos = rng.max(max);
        map.set(pos, i);
        pos = rng.max(max);
        map.set(pos, 0);
      }
    }
  }
  //----------------
  {
    Lbench b("mmap.random_mmap_vector_effemeral_" + std::to_string(max));
    mmap_lib::vector<uint32_t> map;
    map.reserve(max);
    for (int n = 1; n < 100; ++n) {
      for (int i = 0; i < BENCHSIZE; ++i) {
        int pos = rng.max(max);
        map.set(pos,i);
        pos = rng.max(max);
        map.set(pos,0);
      }
    }
  }
}
/*
 * Using a robinhood map
 */
void random_robin_map(int max) {
  Lrand<int> rng;
  Lbench b("mmap.random_robin_map_" + std::to_string(max));
  // declaring unordered map from robin hood
  robin_hood::unordered_map<uint32_t,uint32_t> map;
  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < BENCHSIZE; ++i) {
      int pos = rng.max(max);
      map[pos] = i;
      pos = rng.max(max);
      map.erase(pos);
    }
  }
}

void random_abseil_map(int max) {
  Lrand<int> rng;
  Lbench b("mmap.random_abseil_map_" + std::to_string(max));
  absl::flat_hash_map<uint32_t,uint32_t> map;
  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < BENCHSIZE; ++i) {
      int pos = rng.max(max);
      map[pos] = i;
      pos = rng.max(max);
      map.erase(pos);
    }
  }
}

void random_ska_map(int max) {
  Lrand<int> rng;
  Lbench b("mmap.random_ska_map_" + std::to_string(max));
  ska::flat_hash_map<uint32_t,uint32_t> map;
  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < BENCHSIZE; ++i) {
      int pos = rng.max(max);
      map[pos] = i;
      pos = rng.max(max);
      map.erase(pos);
    }
  }
}

void random_vector_map(int max) {
  Lrand<int> rng;
  Lbench b("mmap.random_vector_map_" + std::to_string(max));
  std::vector<uint32_t> map;
  map.resize(max);
  for (int n = 1; n < 100; ++n) {
    for (int i = 0; i < BENCHSIZE; ++i) {
      int pos = rng.max(max);
      map[pos] = i;
      pos = rng.max(max);
      map[pos] = 0;
    }
  }
}


int main(int argc, char **argv) {
  
  //std::cout << "I'm here\n";
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

  //const std::vector<int> nums = {100000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000};
  const std::vector<int> nums = {10000, 100000};

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

