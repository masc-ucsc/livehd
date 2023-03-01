
#include <array>
#include <vector>
#include <algorithm>
#include <random>

#include "benchmark/benchmark.h"
#include "absl/container/flat_hash_set.h"
#include "hash_set2.hpp" // emhash2
#include "hash_set3.hpp" // emhash7
#include "hash_set4.hpp" // emhash9
#include "hash_set8.hpp" // emhash8

#include "iassert.hpp"

// Zen2:
// After 32 entries, emhash7/emhash8 is best
// Under 32 entries, linear is best
//
// Under 100, emhash7
// Over 100, emhash8 unless very large (100K) emhash9
//
// Apple M1max
// After 40 entries, emhash7 is best
// Under 50 entries, linear is best
//
// emhash7 better always unless very large (10K) emhash9


static constexpr int n=32;
#define TYPE_SEARCH uint16_t

template <size_t Size>
bool linear_erase_word(std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
  for (size_t i = 0u; i < Size; ++i) {
    if (arr[i] == b) {
      arr[i] = 0;
      return true;
    }
  }
  return false;
}

template <size_t Size>
bool binary_erase_word(std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
  auto it = std::lower_bound(arr.begin(), arr.end(), b);
  if (*it != b)
    return false;

  auto len = std::distance(arr.end(), it);
  if (len>0) {
    auto pos   = std::distance(arr.begin(), it);
    void *ptr  = &arr[pos];
    void *ptr2 = &arr[pos+1];

    std::memmove(ptr, ptr2, sizeof(TYPE_SEARCH)*len);
  }

  arr.pop_back();

  return true;
}

template <size_t Size>
bool linear_insert_word(std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
  for (size_t i = 0u; i < Size; ++i) {
    if (arr[i] == 0) {
      arr[i] = b;
      return true;
    }
  }
  return false;
}

template <size_t Size>
bool binary_insert_word(std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
  auto it = std::lower_bound(arr.begin(), arr.end(), b);
  if (it!=arr.end() && *it == b)
    return true;

  if (arr.size()>Size)
    return false;

  arr.insert(it, b);

  return true;
}

static void emhash2_set_insert(benchmark::State& state) {

  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  TYPE_SEARCH num;
  for (int i = 0; i < n; ++i) {
    num = elem_dist(gen);
    v.emplace_back(num);
  }

#if 0
  emhash2::HashSet<TYPE_SEARCH> mem_set;
  std::cout << "1.emhash2::HashSet memory " << sizeof(mem_set) << "\n";
  for (int i = 0; i < n; ++i) {
    mem_set.insert(v[i]);
  }
  std::cout << "2.emhash2::HashSet memory " << sizeof(mem_set) << "\n";
#endif

  for (auto _ : state) {
    auto set = std::make_unique<emhash2::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    benchmark::DoNotOptimize(set);
  }
}

static void emhash2_set_erase(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    state.PauseTiming();
    auto set = std::make_unique<emhash2::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    state.ResumeTiming();

    for(auto i=0;i<n;++i)
      set->erase(v[i]);

    benchmark::DoNotOptimize(set);
  }
}

static void emhash9_set_insert(benchmark::State& state) {

  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  TYPE_SEARCH num;
  for (int i = 0; i < n; ++i) {
    num = elem_dist(gen);
    v.emplace_back(num);
  }

#if 0
  emhash9::HashSet<TYPE_SEARCH> mem_set;
  std::cout << "1.emhash9::HashSet memory " << sizeof(mem_set) << "\n";
  for (int i = 0; i < n; ++i) {
    mem_set.insert(v[i]);
  }
  std::cout << "2.emhash9::HashSet memory " << sizeof(mem_set) << "\n";
#endif

  for (auto _ : state) {
    auto set = std::make_unique<emhash9::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    benchmark::DoNotOptimize(set);
  }
}

static void emhash9_set_erase(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    state.PauseTiming();
    auto set = std::make_unique<emhash9::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    state.ResumeTiming();

    for(auto i=0;i<n;++i)
      set->erase(v[i]);

    benchmark::DoNotOptimize(set);
  }
}

static void emhash7_set_insert(benchmark::State& state) {

  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  TYPE_SEARCH num;
  for (int i = 0; i < n; ++i) {
    num = elem_dist(gen);
    v.emplace_back(num);
  }

#if 0
  emhash7::HashSet<TYPE_SEARCH> mem_set;
  std::cout << "1.emhash7::HashSet memory " << sizeof(mem_set) << "\n";
  for (int i = 0; i < n; ++i) {
    mem_set.insert(v[i]);
  }
  std::cout << "2.emhash7::HashSet memory " << sizeof(mem_set) << "\n";
#endif

  for (auto _ : state) {
    auto set = std::make_unique<emhash7::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    benchmark::DoNotOptimize(set);
  }
}

static void emhash7_set_erase(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    state.PauseTiming();
    auto set = std::make_unique<emhash7::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    state.ResumeTiming();

    for(auto i=0;i<n;++i)
      set->erase(v[i]);

    benchmark::DoNotOptimize(set);
  }
}

static void emhash8_set_insert(benchmark::State& state) {

  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  TYPE_SEARCH num;
  for (int i = 0; i < n; ++i) {
    num = elem_dist(gen);
    v.emplace_back(num);
  }

#if 0
  emhash8::HashSet<TYPE_SEARCH> mem_set;
  std::cout << "1.emhash8::HashSet memory " << sizeof(mem_set) << "\n";
  for (int i = 0; i < n; ++i) {
    mem_set.insert(v[i]);
  }
  std::cout << "2.emhash8::HashSet memory " << sizeof(mem_set) << "\n";
#endif

  for (auto _ : state) {
    auto set = std::make_unique<emhash8::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    benchmark::DoNotOptimize(set);
  }
}

static void emhash8_set_erase(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    state.PauseTiming();
    auto set = std::make_unique<emhash8::HashSet<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    state.ResumeTiming();

    for(auto i=0;i<n;++i)
      set->erase(v[i]);

    benchmark::DoNotOptimize(set);
  }
}

static void abseil_set_insert(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    auto set = std::make_unique<absl::flat_hash_set<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);

    benchmark::DoNotOptimize(set);
  }
}

static void abseil_set_erase(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  for (int i = 0; i < n; ++i) {
    auto num = elem_dist(gen);
    v.emplace_back(num);
  }

  for (auto _ : state) {
    state.PauseTiming();
    auto set = std::make_unique<absl::flat_hash_set<TYPE_SEARCH>>();
    for(auto e:v)
      set->insert(e);
    state.ResumeTiming();

    for(auto i=0;i<n;++i)
      set->erase(v[i]);

    benchmark::DoNotOptimize(set);
  }
}

static void linear_erase(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<TYPE_SEARCH> v2;
    for(auto i=0;i<n;++i)
      v2.emplace_back(v[i]);
    state.ResumeTiming();

    for(auto i=1;i<n;++i) {
      auto x=linear_erase_word<n>(v2, v[i]);
      I(x);
    }

    benchmark::DoNotOptimize(v2);
  }
}

static void linear_insert(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }

  for (auto _ : state) {
    std::vector<TYPE_SEARCH> v2(n,0);;
    for(auto i=0;i<n;++i)
      linear_insert_word<n>(v2,v[i]);

    benchmark::DoNotOptimize(v2);
  }
}

#if 0
static void binary_erase(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  std::sort(v.begin(), v.end());

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<TYPE_SEARCH> v2;
    for(auto i=0;i<n;++i)
      v2.emplace_back(v[i]);
    state.ResumeTiming();

    for(auto i=0;i<n;++i) {
      auto x = binary_erase_word<n>(v2, v[i]);
      I(x);
    }

    benchmark::DoNotOptimize(v2);
  }
}
#endif

static void binary_insert(benchmark::State& state) {
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }

  for (auto _ : state) {
    std::vector<TYPE_SEARCH> v2;
    for(auto i=0;i<n;++i)
      binary_insert_word<n>(v2,v[i]);

    benchmark::DoNotOptimize(v2);
  }
}



BENCHMARK(linear_insert);
BENCHMARK(binary_insert);
BENCHMARK(abseil_set_insert);
BENCHMARK(emhash2_set_insert);
BENCHMARK(emhash7_set_insert);
BENCHMARK(emhash8_set_insert);
BENCHMARK(emhash9_set_insert);

BENCHMARK(linear_erase);
//BENCHMARK(binary_erase);
BENCHMARK(abseil_set_erase);
BENCHMARK(emhash2_set_erase);
BENCHMARK(emhash7_set_erase);
BENCHMARK(emhash8_set_erase);
BENCHMARK(emhash9_set_erase);

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
