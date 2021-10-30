
#include "benchmark/benchmark.h"

#include "lnast_create.hpp"

static void BM_assign_const(benchmark::State& state) {

  std::set<int> data;
  for (auto _ : state) {
    Lnast_create ln;
    ln.new_lnast(mmap_lib::str::concat("t", state.range(0)));

#if 0
    state.PauseTiming();
    // Insert code here for things not to measure
    state.ResumeTiming();
#endif

    for (int j = 0; j < state.range(0); ++j) {
      ln.create_assign_stmts("tmp"_str, mmap_lib::str(j));
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

static void BM_assign_pyrope_const(benchmark::State& state) {

  std::set<int> data;
  for (auto _ : state) {
    Lnast_create ln;
    ln.new_lnast(mmap_lib::str::concat("t", state.range(0)));

#if 0
    state.PauseTiming();
    // Insert code here for things not to measure
    state.ResumeTiming();
#endif

    for (int j = 0; j < state.range(0); ++j) {
      Lconst val(j);
      ln.create_assign_stmts("tmp"_str, val.to_pyrope());
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

#ifndef NDEBUG
BENCHMARK(BM_assign_const)->Range(16,1<<10)->Threads(2);
BENCHMARK(BM_assign_pyrope_const)->Range(8,1<<10)->Threads(2);
#else
BENCHMARK(BM_assign_const)->Range(16,1<<18)->ThreadRange(1,2);
BENCHMARK(BM_assign_pyrope_const)->Range(8,1<<18)->ThreadRange(1,2);
#endif

#if 0
  // Sample multi arg
BENCHMARK(BM_SetInsert)
    ->Args({1<<10, 128})
    ->Args({2<<10, 128})
    ->Args({4<<10, 128})
    ->Args({2<<10, 512})
    ->Args({4<<10, 512})
    ->Args({8<<10, 512});
#endif

int main(int argc, char* argv[]) {
  mmap_lib::str lgdb("lnast_bench");

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}

