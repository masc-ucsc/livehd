//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <type_traits>
#include <vector>

#include "flat_hash_map.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"
#include "mmap_vset.hpp"

#define BENCH_OUT_SIZE 100
#define BENCH_INN_SIZE 100

#define IED 1
#define TS 1
#define TD 1
#define PROBLEM 0


void random_mmap_set(int max, std::string_view name) {
  Lrand<int> rng;
  std::string type_test("mmap.map_set");
  if (name.empty())
    type_test += "_effemeral_" + std::to_string(max);
  else
    type_test += "_persistent_" + std::to_string(max);

  Lbench b(type_test);
  mmap_lib::map<uint32_t, bool> map(name.empty() ? "" : "lgdb_bench", name);
  int cntconta = 0, hitconta = 0, missconta = 0;

#if IED
  for (int n = 1; n < BENCH_OUT_SIZE; ++n) {
    for (int i = 0; i < BENCH_INN_SIZE; ++i) {
      auto pos = rng.max(max);
      map.set(pos, true);
      pos = rng.max(max);
      map.erase(pos);
      pos = rng.max(max);
      if (map.find(pos) != map.end()) {
        map.erase(pos); 
        ++hitconta;
      } else { ++missconta; }
    }
  }
  b.sample("insert/erase dense");
  
  printf("hit %d, missed %d\n", hitconta, missconta);
#endif

#if TS
  missconta = 0, hitconta = 0;
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
      cntconta++;
    }
    map.set(rng.max(max), true);
    auto pos = rng.max(max);
    if (map.find(pos) != map.end()) {
      ++hitconta;
      map.erase(pos);
    } else { ++missconta; }
  }
  b.sample("traversal sparse");

  printf("traversed %d, hit %d, miss %d\n", cntconta, hitconta, missconta);
#endif

#if TD
  cntconta = 0;
  for (int i = 0; i < max; ++i) {
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
    map.erase(rng.max(max));
  }

  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    for (auto it = map.begin(), end = map.end(); it != end; ++it) {
      cntconta++;
    }
  }
  b.sample("traversal dense");
  printf("traversed %d\n", cntconta);
#endif
}

//=====
template<typename k, typename v>
void test_vset_erase(mmap_lib::vset<k, v>& temp) {
  temp.clear();
}
//=====

/* Creates a vset from mmap_lib namespace
 */
void random_mmap_vset(int max, std::string_view name) {
  Lrand<int> rng;
  std::string type_test("mmap_vset ");
  if (name.empty()) {
    type_test += "_effemeral_" + std::to_string(max);
  } else {
    type_test += "_persistent_" + std::to_string(max);
  }

  Lbench b(type_test);
  mmap_lib::vset<uint32_t, uint32_t> set("lgdb_bench", name);

  set.clear();
  int hitconta = 0, missconta = 0, cntconta = 0;  // reset
#if IED
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
      if (set.find(pos) != set.end()) {
        ++hitconta;
        set.erase(pos);
      } else { ++missconta; }
    }
  }
  
  b.sample("insert/erase dense"); 
  
  printf("hit %d, missed %d\n", hitconta, missconta);
#endif  

#if TS
  hitconta = 0, missconta = 0;
  // TRAVERSAL SPARSE TEST
  // runs (200) times
  // each run:
  //   --> go through the whole map and increment a variable per map index
  //   --> insert a random number into map
  //   --> generate a random num, if num is not end of map, erase it
 
  
  for (int i = 0; i < BENCH_INN_SIZE; ++i) {
    #if PROBLEM
    for (auto it = set.begin(), end = set.end(); it != end; ++it) {
      cntconta++; 
    }
    #endif
    set.insert(rng.max(max));
    auto pos = rng.max(max);
    if (set.find(pos) != set.end()) {
      ++hitconta;
      set.erase(pos);
    } else { ++missconta; }
  }
  b.sample("traversal sparse");
  printf("traversed %d, hit %d, missed %d\n", cntconta, hitconta, missconta);
#endif

#if TD
  cntconta = 0, hitconta = 0, missconta = 0;

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
    #if PROBLEM
    for (auto it = set.begin(), end = set.end(); it != end; ++it) {
      cntconta++;
    }
    #endif
  }

  b.sample("traversal dense");
  printf("traversed %d\n", cntconta);
#endif
}

int main(int argc, char **argv) {
  bool run_random_mmap_set   = false;
  bool run_random_mmap_vset  = false;

  if (argc > 1) {
    if (strcasecmp(argv[1], "mmap") == 0)
      run_random_mmap_set = true;
    else if (strcasecmp(argv[1], "vset") == 0)
      run_random_mmap_vset = true;
  } else {
    run_random_mmap_set   = true;
    run_random_mmap_vset  = true;
  }

  for (int i = 1000; i < 1'000'001; i *= 1000) {
    printf("-----------------------------------------------\n");
    printf("        %d max\n", i);

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



#if 0
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
	mmap_lib::vset<uint32_t, uint32_t> set("lgdb_bench", name);
	//mmap_lib::map<uint32_t, uint64_t> map("lgdb_bench", name);

	set.clear();
  
  /*
  for (auto j = 0; j < 50; ++j) { set.insert(j); }
  for (auto i = set.begin(); i != set.end(); ++i) { 
    //std::cout << i.iter_val() << " ";  
  }
  //std::cout << std::endl;
  for (auto i = 0; i < 49; ++i) {
    auto pos = rng.max(set.get_max());
    while (!(set.efind(pos))) {
      pos = rng.max(set.get_max());
    }
    set.erase(pos);
    for (auto i : set) { 
      //std::cout << i << " "; 
    }
    //std::cout << std::endl;
  }
  */

  //=========================

	int conta = 0;

  #if 0
	// counting
	std::cout << "Counting...  ";
	for (auto it = set.bucket_begin(), end = set.bucket_end(); it != end; ++it) {
		auto hold = set.bucket_get_val(it);
		for (auto j = 0; j < set.bucket_size(); ++j) {
			if ((hold & 1) == 1) { conta++; }
			hold = hold >> 1;
		}
	}

	std::cout << "Initial conta: " << conta << "\n\n";

	std::cout << "Inserting 0 - " << (SMALL_BENCH-1) << " into vset...\n";
	for(int i = 0; i < SMALL_BENCH; ++i) { set.insert(i); }

	std::cout << "Counting...  ";
	for (auto it = set.bucket_begin(), end = set.bucket_end(); it != end; ++it) {
		auto hold = set.bucket_get_val(it);
		for (auto j = 0; j < set.bucket_size(); ++j) {
			if ((hold & 1) == 1) { conta++; }
			hold = hold >> 1;
		}
	}
	std::cout << "After insert conta: " << conta << "\n\n";

	conta = 0; // reset

	std::cout << "Erasing 0 - " << (SMALL_BENCH-1) << " from vset...\n";
	for(int i = 0; i < SMALL_BENCH; ++i) { set.erase(i); }

	std::cout << "Counting...  ";
	for (auto it = set.bucket_begin(), end = set.bucket_end(); it != end; ++it) {
		auto hold = set.bucket_get_val(it);
		for (auto j = 0; j < set.bucket_size(); ++j) {
			if ((hold & 1) == 1) { conta++; }
			hold = hold >> 1;
		}
	}
	std::cout << "After erase conta: " << conta << std::endl;

	conta = 0; // reset

	std::cout << "sanity check (printing bucket_len): " << set.bucket_size() << std::endl;
  
	std::cout << "===== vset BENCH =====" << std::endl;
	std::cout << "===== Insert/Erase 0-299\n";
	for(int i = 0; i <= SMALL_BENCH; ++i) { set.insert(i); }
	
  std::cout << "Using iterator to traverse after inserts" << std::endl;
  for (auto it = set.begin(), e = set.end(); it != e; ++it) { std::cout << it.iter_val() << " ";}
  
  for(int i = 0; i < SMALL_BENCH; ++i) { set.erase(i); }
  
  std::cout << "Using iterator to traverse after deletes" << std::endl;
  for (auto it = set.begin(), e = set.end(); it != e; ++it) { std::cout << it.iter_val() << " ";}
  #endif


  /*
	std::cout << "===== vset BENCH =====" << std::endl;
	std::cout << "===== Insert/Erase Dense\n";
	// INSERT/ERASE DENSE TEST
	// runs about (BIG * SMALL) times
	// each run:
	//   --> generate rand num, insert that num into the map
	//   --> generate another rand num, erase that num from the map
	//   --> generate another rand num, if this num is not the end of the map, erase
  for (int n = 1; n < BIG_BENCH; ++n) {
    for (int i = 0; i < SMALL_BENCH; ++i) {
      auto pos = rng.max(max);
      set.insert(pos);
      pos = rng.max(max);
			set.erase(pos);
      pos = rng.max(max);
      if (set.find(pos) != set.end()) {
        set.erase(pos);
      }
    }
  }
  b.sample("insert/erase dense");
  conta = 0;

	std::cout << "===== Traversal Sparse\n";
	// TRAVERSAL SPARSE TEST
	// runs (200) times
	// each run:
	//   --> go through the whole map and increment a variable per map index
	//   --> insert a random number into map
	//   --> generate a random num, if num is not end of map, erase it
  for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = set.begin(), end = set.end(); it != end; ++it) {
      conta++;
    }
    set.insert(rng.max(max));
    auto pos = rng.max(max);
    if (set.find(pos) != set.end()) {
      set.erase(pos);
    }
  }
  b.sample("traversal sparse");

  printf("inserts random %d\n", conta);
  conta = 0;

  std::cout << "===== Traversal Dense\n";
  // TRAVERSAL DENSE TEST
  // --> First, for every num in domain of max, generate 4 random nums and erase them from map
  // --> runs (200) times -> each time go through whole map and incrememt number per map index
  for (int i = 0; i < max; ++i) {
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
    set.erase(rng.max(max));
  }

  for (int i = 0; i < SMALL_BENCH; ++i) {
    for (auto it = set.begin(), end = set.end(); it != end; ++it) {
      conta++;
    }
  }
  b.sample("traversal dense");
  printf("inserts random %d\n",conta);
  */

}

int main(int argc, char **argv) {
  bool run_mmap_vset = false;
	
  if (argc>1) {
    if (strcasecmp(argv[1],"vset") == 0) {
      run_mmap_vset = true;
    }
  } else {
    run_mmap_vset = true;
  }

  if (run_mmap_vset) {
    mmap_vset(1000, "bench_map_use_mmap.data");
  }

  return 0;
}
#endif
