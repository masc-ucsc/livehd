
#include "benchmark/benchmark.h"
#include "absl/container/flat_hash_set.h"

#include <vector>
#include <algorithm>
#include <random>

static constexpr int n=19;
#define TYPE_SEARCH uint16_t

template <size_t Size>
size_t find_word(const std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
    for (size_t i = 0u; i < Size; ++i)
        if (arr[i] == b)
            return i;
    return 0;
}

template <size_t Size>
bool has_word(const std::vector<TYPE_SEARCH> &arr , TYPE_SEARCH b) {
    int found=0;
    for (size_t i = 0u; i < Size; ++i)
        found |= (arr[i] == b);
    return found;
}

size_t branchless_binary_search(const std::vector<TYPE_SEARCH>& v, TYPE_SEARCH val) {
    size_t window = n;
    size_t offset = 0;
    while (window > 1) {
      size_t half = window / 2;
      offset += (v[offset + half] < val) ? half : 0;
      window -= half;
    }
    if (offset == 0) {
      return v.size() > 0 && v[0] < val ? 1 : 0;
    }
    return offset + 1;
}

static void abseil_set(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  absl::flat_hash_set<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);

  TYPE_SEARCH num;
  for (int i = 0; i < n; ++i) {
    num = elem_dist(gen);
    v.insert(num);
  }
  //std::sort(v.begin(), v.end());

  for (auto _ : state) {
    benchmark::DoNotOptimize(v.contains(num));
  }
}

static void LinearSearchCustom(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  auto num=v[n-1];
  //std::sort(v.begin(), v.end());

  for (auto _ : state) {
    benchmark::DoNotOptimize(find_word<n>(v, num));
  }
}

static void LinearSearchHas(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  auto num=v[n-1];
  //std::sort(v.begin(), v.end());

  for (auto _ : state) {
    benchmark::DoNotOptimize(find_word<n>(v, num));
  }
}

static void LinearSearch(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  auto num=v[n-1];
  //std::sort(v.begin(), v.end());

  for (auto _ : state) {
    benchmark::DoNotOptimize(std::find(v.begin(), v.end(), num));
  }
}

static void BinarySearchBranchless(benchmark::State& state) {
  // Code before the loop is not measured
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  std::sort(v.begin(), v.end());
  auto num=v[n-1];

  for (auto _ : state) {
    benchmark::DoNotOptimize(branchless_binary_search(v, num));
  }
}
static void BinarySearch(benchmark::State& state) {
  // Code before the loop is not measured
  std::vector<TYPE_SEARCH> v;

  std::mt19937 gen(std::random_device{}());
  // fix a seed for fair comparison
  gen.seed(0xDEADC0DE);
  std::uniform_int_distribution<int> elem_dist(0, 4096000);
  for (int i = 0; i < n; ++i) {
    v.push_back(elem_dist(gen));
  }
  std::sort(v.begin(), v.end());
  auto num=v[n-1];

  for (auto _ : state) {
    benchmark::DoNotOptimize(std::lower_bound(v.begin(), v.end(), num));
  }
}

BENCHMARK(LinearSearch);
BENCHMARK(LinearSearchCustom);
BENCHMARK(LinearSearchHas);
BENCHMARK(BinarySearch);
BENCHMARK(abseil_set);
BENCHMARK(BinarySearchBranchless);

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
