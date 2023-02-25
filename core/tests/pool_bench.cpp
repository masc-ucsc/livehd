
#include "benchmark/benchmark.h"
#include "concurrentqueue.hpp"
#include "mpmc.hpp"
#include "spmc.hpp"
#include "spool_ptr.hpp"
#include "spsc.hpp"

static void BM_mpmc(benchmark::State& state) {
  for (auto _ : state) {
    mpmc<int> queue(1024);

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(queue.enqueue(j));
      auto data = queue.dequeue(data);
      assert(*data == j);
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_spmc(benchmark::State& state) {
  for (auto _ : state) {
    spmc256<int> queue;

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(queue.enqueue(j));
      auto data = queue.dequeue();
      assert(*data == j);
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

#if 0
static void BM_spsc(benchmark::State& state) {

  for (auto _ : state) {
    spsc<int> queue(1024);

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(queue.enqueue(j));
      int data;
      benchmark::DoNotOptimize(queue.dequeue(data));
      assert(data==j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}
#endif

static void BM_spsc256(benchmark::State& state) {
  for (auto _ : state) {
    spsc256<int> queue;

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(queue.enqueue(j));
      auto data = queue.dequeue();
      assert(*data == j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_moodycamel(benchmark::State& state) {
  for (auto _ : state) {
    moodycamel::ConcurrentQueue<int> queue(256);

    for (int j = 0; j < state.range(0); ++j) {
      benchmark::DoNotOptimize(queue.try_enqueue(j));
      int data;
      benchmark::DoNotOptimize(queue.try_dequeue(data));
      assert(data == j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

class My_data {
public:
  int val;
  // This two must be provided as public
  uint32_t shared_count;
  void     reconstruct(int a) { val = a; }
};

static void BM_make_shared_pool(benchmark::State& state) {
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      auto ptr = spool_ptr<My_data>::make(j);
      assert(ptr->val == j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_make_shared_ptr(benchmark::State& state) {
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      auto ptr = std::make_shared<int>(j);
      benchmark::DoNotOptimize(*ptr = j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_make_unique_ptr(benchmark::State& state) {
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      auto ptr = std::make_unique<int>(j);
      benchmark::DoNotOptimize(*ptr = j);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

#ifndef NDEBUG
BENCHMARK(BM_mpmc)->Arg(512);
BENCHMARK(BM_spmc)->Arg(512);
// BENCHMARK(BM_spsc)->Arg(512);
BENCHMARK(BM_spsc256)->Arg(512);
BENCHMARK(BM_moodycamel)->Arg(512);
BENCHMARK(BM_make_shared_pool)->Arg(512);
BENCHMARK(BM_make_shared_ptr)->Arg(512);
BENCHMARK(BM_make_unique_ptr)->Arg(512);
#else
BENCHMARK(BM_mpmc)->Arg(512);
BENCHMARK(BM_spmc)->Arg(512);
// BENCHMARK(BM_spsc)->Arg(512);
BENCHMARK(BM_spsc256)->Arg(512);
BENCHMARK(BM_moodycamel)->Arg(512);
BENCHMARK(BM_make_shared_pool)->Arg(512);
BENCHMARK(BM_make_shared_ptr)->Arg(512);
BENCHMARK(BM_make_unique_ptr)->Arg(512);
#endif

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
