
#include "fmt/format.h"
#include "benchmark/benchmark.h"

#include "graph_core.hpp"

static void BM_create_chain_1M(benchmark::State& state) {

  size_t total_bytes = 0;

  for (auto _ : state) {

    for (int j = 0; j < state.range(0); ++j) {
      Graph_core gc("lg_create_chain", "create_chain");

      auto first_id = gc.create_node();
      auto last_id = first_id;
      for(auto i=0;i<1000000;++i) {
        auto new_id = gc.create_node();
        gc.add_edge(last_id, new_id);
        gc.add_edge(first_id, new_id);

        last_id = new_id;
      }

      total_bytes += gc.size_bytes();
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
  state.counters["bytes"] = benchmark::Counter(total_bytes / (state.iterations() * state.range(0)), benchmark::Counter::kIsRate);
}

static void BM_create_chain_100K(benchmark::State& state) {

  size_t total_bytes = 0;

  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      Graph_core gc("lg_create_chain", "create_chain");

      auto first_id = gc.create_node();
      auto last_id = first_id;
      for(auto i=0;i<100000;++i) {
        auto new_id = gc.create_node();
        gc.add_edge(last_id, new_id);
        gc.add_edge(first_id, new_id);

        last_id = new_id;
      }

      total_bytes += gc.size_bytes();
      //fmt::print("size:{}\n", gc.size_bytes());
      benchmark::DoNotOptimize(last_id);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
  state.counters["bytes"] = benchmark::Counter(total_bytes / (state.iterations() * state.range(0)), benchmark::Counter::kIsRate);
}

static void BM_delete_chain_100K(benchmark::State& state) {

  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      Graph_core gc("lg_create_chain", "create_chain");

      auto first_id = gc.create_node();
      auto last_id = first_id;
      for(auto i=0;i<100000;++i) {
        auto new_id = gc.create_node();
        gc.add_edge(last_id, new_id);
        gc.add_edge(first_id, new_id);

        last_id = new_id;
      }
      uint32_t id = 0;
      int n_nodes = 0;
      while(true) {
        auto next_id = gc.fast_next(id);
        gc.del_node(id);
        id = next_id;
        if (id==0)
          break;
        n_nodes++;
      }
      benchmark::DoNotOptimize(n_nodes);
    }

  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_traverse_chain_1M(benchmark::State& state) {

  Graph_core gc("lg_create_chain", "create_chain");

  auto first_id = gc.create_node();
  auto last_id = first_id;
  for(auto i=0;i<1000000;++i) {
    auto new_id = gc.create_node();
    gc.add_edge(last_id, new_id);
    gc.add_edge(first_id, new_id);

    last_id = new_id;
  }
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      uint32_t id = 0;
      int n_nodes = 0;
      while(true) {
        auto next_id = gc.fast_next(id);
        id = next_id;
        if (id==0)
          break;
        n_nodes++;
      }
      benchmark::DoNotOptimize(n_nodes);
    }

  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

//--------------------------------------------------------------------

BENCHMARK(BM_create_chain_1M)->Arg(32);
BENCHMARK(BM_create_chain_100K)->Arg(32);
BENCHMARK(BM_delete_chain_100K)->Arg(32);
BENCHMARK(BM_traverse_chain_1M)->Arg(32);

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
