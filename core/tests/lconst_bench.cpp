
#include "benchmark/benchmark.h"
#include "blop.hpp"
#include "dlop.hpp"
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

static void BM_blop_add1(benchmark::State& state) {
  for (auto _ : state) {
    int64_t total[1] = {0};
    int64_t one[1]   = {1};

    for (int j = 0; j < state.range(0); ++j) {
      one[0] = j;
      Blop::addn(total, 1, total, one);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

static void BM_blop_add2(benchmark::State& state) {
  for (auto _ : state) {
    int64_t total[2] = {0, 0};
    int64_t one[2]   = {1, 0};

    for (int j = 0; j < state.range(0); ++j) {
      Blop::extend(one, 2, j);
      Blop::addn(total, 2, total, one);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

static void BM_dlop_create(benchmark::State& state) {
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      auto v = Dlop::create_integer(j);
      benchmark::DoNotOptimize(v);
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_dlop_add(benchmark::State& state) {
  for (auto _ : state) {
    auto total = Dlop::create_integer(0);

    for (int j = 0; j < state.range(0); ++j) {
      auto v = Dlop::create_integer(j);
      total  = total->add_op(v);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_dlop_mut_add(benchmark::State& state) {
  for (auto _ : state) {
    auto total = Dlop::create_integer(0);

    for (int j = 0; j < state.range(0); ++j) {
      auto v = Dlop::create_integer(j);
      total->mut_add(v);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_test(benchmark::State& state) {
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      auto ptr = Dlop::alloc(16);
      Dlop::free(16, ptr);
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_dlop_mut_addi(benchmark::State& state) {
  for (auto _ : state) {
    auto total = Dlop::create_integer(0);

    for (int j = 0; j < state.range(0); ++j) {
      total->mut_add(j);
    }

    benchmark::DoNotOptimize(total);
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

BENCHMARK(BM_uint64_add)->Arg(512);
BENCHMARK(BM_lconst_add)->Arg(512);
BENCHMARK(BM_blop_add1)->Arg(512);
BENCHMARK(BM_blop_add2)->Arg(512);
BENCHMARK(BM_dlop_create)->Arg(512);
BENCHMARK(BM_dlop_add)->Arg(512);
BENCHMARK(BM_dlop_mut_add)->Arg(512);
BENCHMARK(BM_dlop_mut_addi)->Arg(512);
BENCHMARK(BM_test)->Arg(512);

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
  std::string lgdb("lconst_bench");

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
