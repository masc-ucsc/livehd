//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"
#include "iassert.hpp"
#include <vector>
#include "lrand.hpp"
#include "lbench.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "flat_hash_map.hpp"
#include "robin_hood.hpp"
#include <type_traits>
#include "mmap_vset.hpp"

#define BIG_BENCH 500
#define SMALL_BENCH 300
#define TESTNUMS 5


/*
 * Creates a vset from mmap_lib namespace
 * And tests it a little
 */
void mmap_vset(int max, std::string_view name) {
  Lrand<int> rng;

  std::string type_test("mmap_vset ");
	if (name.empty()) { type_test += "(effemeral)"; } 
	else { type_test += "(persistent)"; }
  
	Lbench b(type_test);
	mmap_lib::vset<uint32_t, uint64_t> set("lgdb_bench", name);
	
	std::cout << "sanity check: ";
	std::cout << max << std::endl;	
	//set.wht();
	std::cout << "===== vset usage =====" << std::endl;
	
	std::cout << "===== Insert/Erase 0-299 =====\n";

	for(int i = 0; i < SMALL_BENCH; ++i) { set.insert(i); }
	/*
	for(int j = 0; j <= set.num_buckets(SMALL_BENCH); ++j) {
		std::cout << "map[" << j << "] = " << set.bucket_get(j) << std::endl;
	}
	*/
	for(int i = 0; i < SMALL_BENCH; ++i) { set.erase(i); }
	/*
	for(int j = 0; j <= set.num_buckets(SMALL_BENCH); ++j) {
		std::cout << "map[" << j << "] = " << set.bucket_get(j) << std::endl;
	}
	*/
	std::cout << "===== Insert/Erase Dense =====\n";
  for (int n = 1; n < BIG_BENCH; ++n) {
    for (int i = 0; i < SMALL_BENCH; ++i) {
      auto pos = rng.max(max);
      set.insert(pos);
      pos = rng.max(max); 
			set.erase(pos);
      pos = rng.max(max);
      if (set.find(pos) != set.is_end(pos))
        set.erase(pos);
    }
  }

  b.sample("insert/erase dense");
  int conta = 0;

	
	std::cout << "===== Traversal Sparse =====\n";
  for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = 0; !(set.is_end(it)); ++it) {
			conta++;
    }
    set.insert(rng.max(max));

    auto pos = rng.max(max);
    if (set.find(pos) != set.is_end(pos)) {
      set.erase(pos);
		}
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n",conta);
  conta = 0;

	std::cout << "===== Traversal Dense =====\n";
  for (int i = 0; i < max; ++i) {
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
  }

  for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = 0; !(set.is_end(it)); ++it) {
      conta++;
    }
  }
  b.sample("traversal dense");
  printf("inserts random %d\n",conta);
}


/* 
 * Creates an unordered map from std namespace
 * puts some elements in it, then clears the map a bunch of times
 */
void std_set(int max) {
  Lrand<int> rng;
  Lbench b("std_set");
	std::unordered_set<uint32_t> map; 
  
	// INSERT/ERASE DENSE TEST
	// runs about (500 * 200) times
	// each run:
	//   --> generate rand num, insert that num into the map
	//   --> generate another rand num, erase that num from the map
	//   --> generate another rand num, if this num is not the end of the map, erase
  for (int n = 1; n < BIG_BENCH; ++n) {
    for (int i = 0; i < SMALL_BENCH; ++i) {
      auto pos = rng.max(max);
      map.insert(pos);
      pos = rng.max(max); map.erase(pos);
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
	//   --> go through the whole map and increment a variable per map index
	//   --> insert a random number into map
	//   --> generate a random num, if num is not end of map, erase it
	for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
      conta++;
    }
    map.insert(rng.max(max));
    auto pos = rng.max(max);
    if (map.find(pos) != map.end())
      map.erase(pos);
  }
  b.sample("traversal sparse");
  //printf("inserts random %d\n",conta);
	conta = 0;

	// TRAVERSAL DENSE TEST
	// --> First, for every num in domain of max, generate 4 random nums and erase them from map
	// --> runs (200) times -> each time go through whole map and incrememt number per map index
  for (int i = 0; i < max; ++i) {
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
  }

  for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end;++it) {
      conta++;
    }
  }
  b.sample("traversal dense");
  //printf("inserts random %d\n",conta);
}


/*
 //Using a map from mmap_lib
 //FOCUS on this one
void random_mmap_map(int max) {
  Lrand<int> rng;

  {
  	Lbench b("random_mmap_map (persistent) " + std::to_string(max));

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
    Lbench b("random_mmap_map (effemeral) " + std::to_string(max));

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


//Testing mmap_vector
void random_mmap_vector(int max) {
  Lrand<int> rng;
  {
    Lbench b("random_mmap_vector (persistent)");
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
    Lbench b("random_mmap_vector (effemeral)");
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


//Using a robinhood map
void random_robin_map(int max) {
  Lrand<int> rng;
  Lbench b("random_robin_map " + std::to_string(max));
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
  Lbench b("random_abseil_map" + std::to_string(max));
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
  Lbench b("random_ska_map" + std::to_string(max));
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
  Lbench b("random_vector_map" + std::to_string(max));
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
*/

int main(int argc, char **argv) {
  
  std::cout << "I'm here\n";
  bool run_std_set = false;
  bool run_mmap_vset = false;
  //bool run_random_mmap_vector = false;
  //bool run_random_mmap_map    = false;
  //bool run_random_abseil_map  = false;
  //bool run_random_ska_map     = false;
  //bool run_random_vector_map  = false;

	
  if (argc>1) {
    if (strcasecmp(argv[1],"std")==0)
      run_std_set = true;
		else if (strcasecmp(argv[1],"robin")==0)
      run_mmap_vset = true;
	  /*
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
		*/
  }else{
    run_std_set = true;
    run_mmap_vset = true;
    //run_random_mmap_vector = true;
    //run_random_mmap_map    = true;
    //run_random_abseil_map  = true;
    //run_random_ska_map     = true;
    //run_random_vector_map  = true;
  }

  //const std::vector<int> nums = {100000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000};
  const std::vector<int> nums = {10000, 100000};

  for(auto i:nums) {
    if (run_std_set)
      std_set(i);
    if (run_mmap_vset)
      mmap_vset(i, "bench_map_use_mmap.data");
		/*
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
		*/
  }

  return 0;
}

