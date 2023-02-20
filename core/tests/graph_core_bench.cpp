
#include "fmt/format.h"
#include "absl/container/flat_hash_set.h"
#include "benchmark/benchmark.h"
#include "hash_set8.hpp"

#include "graph_core.hpp"

static void BM_create_flops_100K(benchmark::State& state) {

  for (auto _ : state) {

    for (int j = 0; j < state.range(0); ++j) {
      Graph_core gc("lg_create_chain", "create_chain");

      auto inputs = gc.create_node();
      auto clk_id = gc.create_pin(inputs, 1);
      auto rst_id = gc.create_pin(inputs, 2);
      auto din_id = gc.create_pin(inputs, 3);

      auto outputs  = gc.create_node();
      auto dout_id  = gc.create_pin(outputs, 1);

      auto last_id     = gc.create_node();
      auto last_clk_id = gc.create_pin(last_id, 1); // clk
      auto last_rst_id = gc.create_pin(last_id, 2); // reset
      gc.set_type(last_id, 33); // made up type id

      gc.add_edge(clk_id, last_clk_id);
      gc.add_edge(rst_id, last_rst_id);
      gc.add_edge(din_id, last_id);

      for(auto i=0;i<100000;++i) {
        auto new_id     = gc.create_node();
        auto new_clk_id = gc.create_pin(new_id, 1); // clk
        auto new_rst_id = gc.create_pin(new_id, 2); // reset
        gc.set_type(new_id, 33); // made up type id

        (void)new_clk_id;
        (void)new_rst_id;
        //gc.add_edge(clk_id , new_clk_id);
        //gc.add_edge(rst_id , new_rst_id);
        gc.add_edge(last_id, new_id);

        last_id = new_id;
      }

      gc.add_edge(last_id, dout_id);

      //fmt::print("size:{}\n", gc.size_bytes());
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_create_flops_100Kemhash(benchmark::State& state) {


  for (auto _ : state) {

    for (int j = 0; j < state.range(0); ++j) {
      emhash8::HashSet<uint32_t> clk_set;
      emhash8::HashSet<uint32_t> rst_set;

      Graph_core gc("lg_create_chain", "create_chain");

      auto inputs = gc.create_node();
      auto clk_id = gc.create_pin(inputs, 1);
      auto rst_id = gc.create_pin(inputs, 2);
      auto din_id = gc.create_pin(inputs, 3);

      auto outputs  = gc.create_node();
      auto dout_id  = gc.create_pin(outputs, 1);

      auto last_id     = gc.create_node();
      auto last_clk_id = gc.create_pin(last_id, 1); // clk
      auto last_rst_id = gc.create_pin(last_id, 2); // reset
      gc.set_type(last_id, 33); // made up type id

      (void)clk_id;
      (void)rst_id;
      clk_set.insert(last_clk_id);
      rst_set.insert(last_rst_id);
      gc.add_edge(din_id, last_id);

      for(auto i=0;i<100000;++i) {
        auto new_id     = gc.create_node();
        auto new_clk_id = gc.create_pin(new_id, 1); // clk
        auto new_rst_id = gc.create_pin(new_id, 2); // reset
        gc.set_type(new_id, 33); // made up type id

        //(void)new_clk_id;
        //(void)new_rst_id;
        clk_set.insert(new_clk_id);
        rst_set.insert(new_rst_id);
        gc.add_edge(last_id, new_id);

        last_id = new_id;
      }

      gc.add_edge(last_id, dout_id);

      //fmt::print("size:{}\n", gc.size_bytes());
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}
static void BM_create_flops_100Kabsl(benchmark::State& state) {

  for (auto _ : state) {

    for (int j = 0; j < state.range(0); ++j) {
      absl::flat_hash_set<uint32_t> clk_set;
      absl::flat_hash_set<uint32_t> rst_set;

      Graph_core gc("lg_create_chain", "create_chain");

      auto inputs = gc.create_node();
      auto clk_id = gc.create_pin(inputs, 1);
      auto rst_id = gc.create_pin(inputs, 2);
      auto din_id = gc.create_pin(inputs, 3);

      auto outputs  = gc.create_node();
      auto dout_id  = gc.create_pin(outputs, 1);

      auto last_id     = gc.create_node();
      auto last_clk_id = gc.create_pin(last_id, 1); // clk
      auto last_rst_id = gc.create_pin(last_id, 2); // reset
      gc.set_type(last_id, 33); // made up type id

      (void)clk_id;
      (void)rst_id;
      clk_set.insert(last_clk_id);
      rst_set.insert(last_rst_id);
      gc.add_edge(din_id, last_id);

      for(auto i=0;i<100000;++i) {
        auto new_id     = gc.create_node();
        auto new_clk_id = gc.create_pin(new_id, 1); // clk
        auto new_rst_id = gc.create_pin(new_id, 2); // reset
        gc.set_type(new_id, 33); // made up type id

        //(void)new_clk_id;
        //(void)new_rst_id;
        clk_set.insert(new_clk_id);
        rst_set.insert(new_rst_id);
        gc.add_edge(last_id, new_id);

        last_id = new_id;
      }

      gc.add_edge(last_id, dout_id);

      //fmt::print("size:{}\n", gc.size_bytes());
    }
  }
  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}

static void BM_create_chain_100K(benchmark::State& state) {

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

      //fmt::print("size:{}\n", gc.size_bytes());
      benchmark::DoNotOptimize(last_id);
    }
  }

  state.counters["speed"] = benchmark::Counter(state.iterations() * state.range(0), benchmark::Counter::kIsRate);
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

BENCHMARK(BM_create_flops_100K)->Arg(32);
BENCHMARK(BM_create_flops_100Kabsl)->Arg(32);
BENCHMARK(BM_create_flops_100Kemhash)->Arg(32);
BENCHMARK(BM_create_chain_100K)->Arg(32);
BENCHMARK(BM_delete_chain_100K)->Arg(32);
BENCHMARK(BM_traverse_chain_1M)->Arg(32);

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
