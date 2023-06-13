
#include "benchmark/benchmark.h"
#include "lconst.hpp"

static void BM_uint64_add(benchmark::State& state) {
  for (auto _ : state) {
    uint64_t total = 0;

#if 0
    state.PauseTiming();
    // Insert code here for things not to measure
    state.ResumeTiming();
#endif

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(total += j);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

static void BM_lconst_add(benchmark::State& state) {
  for (auto _ : state) {
    Lconst total;

#if 0
    state.PauseTiming();
    // Insert code here for things not to measure
    state.ResumeTiming();
#endif

    for (int j = 0; j < state.range(0); ++j) {
      total = total + Lconst(j);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

BENCHMARK(BM_uint64_add)->Arg(512);
BENCHMARK(BM_lconst_add)->Arg(512);

int main(int argc, char* argv[]) {
  std::string lgdb("lconst_bench");

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
