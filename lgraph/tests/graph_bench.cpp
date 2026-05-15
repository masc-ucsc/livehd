//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Side-by-side micro-benchmark comparing the local Lgraph (lgedge-based)
// implementation against hhds::Graph. The lgraph data structures are being
// migrated to hhds; this bench tracks the perf gap on a few simple patterns.
//
// Three graph shapes are exercised:
//   * chain     — linked list: N nodes, N-1 edges
//   * fat_pin0  — fully connected, every incoming edge lands on sink pin 0
//   * fat_pins  — fully connected, every incoming edge lands on a distinct sink pin
//
// For each shape we measure five operations: create / fast traverse /
// forward traverse / delete_pin (forward iteration, explicit per-edge
// del + del_node — stresses the harder case of mutating while walking
// forward) / delete_node (fast iteration, rely on del_node to bulk-remove
// edges).

#include <string>

#include "benchmark/benchmark.h"
#include "cell.hpp"
#include "graph.hpp"
#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

namespace {

constexpr int kChainLen = 10000;
constexpr int kFatLen   = 500;  // O(N^2) edges → ~125k

// Unique graph name per Lgraph build so the shared Graph_library does not
// reject duplicates across benchmark invocations.
static int next_gen() {
  static int gen = 0;
  return gen++;
}

// ---------------------------------------------------------------------------
// Lgraph builders
// ---------------------------------------------------------------------------

static Lgraph* build_chain_lg(const std::string& name, int n) {
  auto* lib = Graph_library::instance("lgdb_graph_bench");
  auto* g   = lib->create_lgraph(name, "graph_bench");

  Node prev = g->create_node(Ntype_op::Or, 1);
  for (int i = 1; i < n; ++i) {
    Node cur = g->create_node(Ntype_op::Or, 1);
    prev.setup_driver_pin().connect_sink(cur.setup_sink_pin());
    prev = cur;
  }
  return g;
}

static Lgraph* build_fat_pin0_lg(const std::string& name, int n) {
  auto* lib = Graph_library::instance("lgdb_graph_bench");
  auto* g   = lib->create_lgraph(name, "graph_bench");

  for (int i = 0; i < n; ++i) {
    Node node = g->create_node(Ntype_op::Or, 1);
    for (auto prev : g->fast()) {
      if (prev == node) {
        break;
      }
      prev.setup_driver_pin().connect_sink(node.setup_sink_pin_raw(0));
    }
  }
  return g;
}

static Lgraph* build_fat_pins_lg(const std::string& name, int n) {
  auto* lib = Graph_library::instance("lgdb_graph_bench");
  auto* g   = lib->create_lgraph(name, "graph_bench");

  for (int i = 0; i < n; ++i) {
    Node    node = g->create_node(Ntype_op::Mux, 1);  // unlimited_sink
    Port_ID j    = 0;
    for (auto prev : g->fast()) {
      if (prev == node) {
        break;
      }
      prev.setup_driver_pin().connect_sink(node.setup_sink_pin_raw(j));
      ++j;
    }
  }
  return g;
}

// ---------------------------------------------------------------------------
// hhds builders
// ---------------------------------------------------------------------------

struct HhdsBundle {
  hhds::GraphLibrary             lib;
  std::shared_ptr<hhds::GraphIO> gio;
  std::shared_ptr<hhds::Graph>   graph;
};

static std::unique_ptr<HhdsBundle> make_hhds_bundle(const char* name) {
  auto b = std::make_unique<HhdsBundle>();
  b->gio = b->lib.create_io(name);
  b->gio->add_input("in", 0);
  b->gio->add_output("out", 0);
  b->graph = b->gio->create_graph();
  return b;
}

// hhds::Graph reserves nid < 16 for built-in IO/Const singletons, but its
// fast_class() / forward_class() iterators already start past them, so user
// code does not need to filter them out.

static std::unique_ptr<HhdsBundle> build_chain_hhds(int n) {
  auto b = make_hhds_bundle("chain_hhds");
  auto g = b->graph;
  if (n <= 0) {
    return b;
  }
  auto prev = g->create_node();
  for (int i = 1; i < n; ++i) {
    auto cur = g->create_node();
    prev.create_driver_pin().connect_sink(cur.create_sink_pin());
    prev = cur;
  }
  return b;
}

static std::unique_ptr<HhdsBundle> build_fat_pin0_hhds(int n) {
  auto b = make_hhds_bundle("fat_pin0_hhds");
  auto g = b->graph;
  for (int i = 0; i < n; ++i) {
    auto node = g->create_node();
    // Every incoming edge lands on the same sink pin (port 0).
    auto spin = node.create_sink_pin(static_cast<hhds::Port_id>(0));
    for (auto prev : g->fast_class()) {
      if (prev == node) {
        break;
      }
      prev.create_driver_pin().connect_sink(spin);
    }
  }
  return b;
}

static std::unique_ptr<HhdsBundle> build_fat_pins_hhds(int n) {
  auto b = make_hhds_bundle("fat_pins_hhds");
  auto g = b->graph;
  for (int i = 0; i < n; ++i) {
    auto          node = g->create_node();
    hhds::Port_id j    = 0;
    for (auto prev : g->fast_class()) {
      if (prev == node) {
        break;
      }
      auto spin = node.create_sink_pin(j);
      prev.create_driver_pin().connect_sink(spin);
      ++j;
    }
  }
  return b;
}

// ---------------------------------------------------------------------------
// Lgraph benchmarks (build / fast / forward / delete) × (chain / fat_pin0 / fat_pins)
// ---------------------------------------------------------------------------

#define BENCH_LG_BUILD(NAME, BUILDER)                                                                              \
  void BM_##NAME##_build_lgraph(benchmark::State& state) {                                                         \
    for (auto _ : state) {                                                                                         \
      auto* g = BUILDER(std::string(#NAME "_lg_") + std::to_string(next_gen()), static_cast<int>(state.range(0))); \
      benchmark::DoNotOptimize(g);                                                                                 \
    }                                                                                                              \
  }

#define BENCH_LG_FAST(NAME, BUILDER)                                                                                  \
  void BM_##NAME##_fast_lgraph(benchmark::State& state) {                                                             \
    auto* g = BUILDER(std::string(#NAME "_lg_fast_") + std::to_string(next_gen()), static_cast<int>(state.range(0))); \
    for (auto _ : state) {                                                                                            \
      int n_nodes = 0;                                                                                                \
      for (auto node : g->fast()) {                                                                                   \
        benchmark::DoNotOptimize(node);                                                                               \
        ++n_nodes;                                                                                                    \
      }                                                                                                               \
      benchmark::DoNotOptimize(n_nodes);                                                                              \
    }                                                                                                                 \
  }

#define BENCH_LG_FORWARD(NAME, BUILDER)                                                                              \
  void BM_##NAME##_forward_lgraph(benchmark::State& state) {                                                         \
    auto* g = BUILDER(std::string(#NAME "_lg_fwd_") + std::to_string(next_gen()), static_cast<int>(state.range(0))); \
    for (auto _ : state) {                                                                                           \
      int n_edges = 0;                                                                                               \
      for (auto node : g->forward()) {                                                                               \
        for (auto& e : node.out_edges()) {                                                                           \
          benchmark::DoNotOptimize(e);                                                                               \
          ++n_edges;                                                                                                 \
        }                                                                                                            \
      }                                                                                                              \
      benchmark::DoNotOptimize(n_edges);                                                                             \
    }                                                                                                                \
  }

#define BENCH_LG_DELETE_PIN(NAME, BUILDER)                                                                              \
  void BM_##NAME##_delete_pin_lgraph(benchmark::State& state) {                                                         \
    for (auto _ : state) {                                                                                              \
      state.PauseTiming();                                                                                              \
      auto* g = BUILDER(std::string(#NAME "_lg_delp_") + std::to_string(next_gen()), static_cast<int>(state.range(0))); \
      state.ResumeTiming();                                                                                             \
      for (auto node : g->forward()) {                                                                                  \
        for (auto& e : node.out_edges()) {                                                                              \
          e.del_edge();                                                                                                 \
        }                                                                                                               \
        for (auto& e : node.inp_edges()) {                                                                              \
          e.del_edge();                                                                                                 \
        }                                                                                                               \
        node.del_node();                                                                                                \
      }                                                                                                                 \
    }                                                                                                                   \
  }

#define BENCH_LG_DELETE_NODE(NAME, BUILDER)                                                                             \
  void BM_##NAME##_delete_node_lgraph(benchmark::State& state) {                                                        \
    for (auto _ : state) {                                                                                              \
      state.PauseTiming();                                                                                              \
      auto* g = BUILDER(std::string(#NAME "_lg_deln_") + std::to_string(next_gen()), static_cast<int>(state.range(0))); \
      state.ResumeTiming();                                                                                             \
      for (auto node : g->fast()) {                                                                                     \
        node.del_node();                                                                                                \
      }                                                                                                                 \
    }                                                                                                                   \
  }

BENCH_LG_BUILD(chain, build_chain_lg)
BENCH_LG_FAST(chain, build_chain_lg)
BENCH_LG_FORWARD(chain, build_chain_lg)
BENCH_LG_DELETE_PIN(chain, build_chain_lg)
BENCH_LG_DELETE_NODE(chain, build_chain_lg)

BENCH_LG_BUILD(fat_pin0, build_fat_pin0_lg)
BENCH_LG_FAST(fat_pin0, build_fat_pin0_lg)
BENCH_LG_FORWARD(fat_pin0, build_fat_pin0_lg)
BENCH_LG_DELETE_PIN(fat_pin0, build_fat_pin0_lg)
BENCH_LG_DELETE_NODE(fat_pin0, build_fat_pin0_lg)

BENCH_LG_BUILD(fat_pins, build_fat_pins_lg)
BENCH_LG_FAST(fat_pins, build_fat_pins_lg)
BENCH_LG_FORWARD(fat_pins, build_fat_pins_lg)
BENCH_LG_DELETE_PIN(fat_pins, build_fat_pins_lg)
BENCH_LG_DELETE_NODE(fat_pins, build_fat_pins_lg)

// ---------------------------------------------------------------------------
// hhds benchmarks (build / fast / forward / delete) × (chain / fat_pin0 / fat_pins)
// ---------------------------------------------------------------------------

#define BENCH_HHDS_BUILD(NAME, BUILDER)                   \
  void BM_##NAME##_build_hhds(benchmark::State& state) {  \
    for (auto _ : state) {                                \
      auto b = BUILDER(static_cast<int>(state.range(0))); \
      benchmark::DoNotOptimize(b);                        \
    }                                                     \
  }

#define BENCH_HHDS_FAST(NAME, BUILDER)                  \
  void BM_##NAME##_fast_hhds(benchmark::State& state) { \
    auto b = BUILDER(static_cast<int>(state.range(0))); \
    auto g = b->graph;                                  \
    for (auto _ : state) {                              \
      int n_nodes = 0;                                  \
      for (auto node : g->fast_class()) {               \
        benchmark::DoNotOptimize(node);                 \
        ++n_nodes;                                      \
      }                                                 \
      benchmark::DoNotOptimize(n_nodes);                \
    }                                                   \
  }

#define BENCH_HHDS_FORWARD(NAME, BUILDER)                  \
  void BM_##NAME##_forward_hhds(benchmark::State& state) { \
    auto b = BUILDER(static_cast<int>(state.range(0)));    \
    auto g = b->graph;                                     \
    for (auto _ : state) {                                 \
      int n_edges = 0;                                     \
      for (auto node : g->forward_class()) {               \
        for (auto& e : node.out_edges()) {                 \
          benchmark::DoNotOptimize(e);                     \
          ++n_edges;                                       \
        }                                                  \
      }                                                    \
      benchmark::DoNotOptimize(n_edges);                   \
    }                                                      \
  }

#define BENCH_HHDS_DELETE_PIN(NAME, BUILDER)                  \
  void BM_##NAME##_delete_pin_hhds(benchmark::State& state) { \
    for (auto _ : state) {                                    \
      state.PauseTiming();                                    \
      auto b = BUILDER(static_cast<int>(state.range(0)));     \
      auto g = b->graph;                                      \
      state.ResumeTiming();                                   \
      for (auto node : g->forward_class()) {                  \
        for (auto& e : node.out_edges()) {                    \
          e.del_edge();                                       \
        }                                                     \
        for (auto& e : node.inp_edges()) {                    \
          e.del_edge();                                       \
        }                                                     \
        node.del_node();                                      \
      }                                                       \
    }                                                         \
  }

#define BENCH_HHDS_DELETE_NODE(NAME, BUILDER)                  \
  void BM_##NAME##_delete_node_hhds(benchmark::State& state) { \
    for (auto _ : state) {                                     \
      state.PauseTiming();                                     \
      auto b = BUILDER(static_cast<int>(state.range(0)));      \
      auto g = b->graph;                                       \
      state.ResumeTiming();                                    \
      for (auto node : g->fast_class()) {                      \
        node.del_node();                                       \
      }                                                        \
    }                                                          \
  }

BENCH_HHDS_BUILD(chain, build_chain_hhds)
BENCH_HHDS_FAST(chain, build_chain_hhds)
BENCH_HHDS_FORWARD(chain, build_chain_hhds)
BENCH_HHDS_DELETE_PIN(chain, build_chain_hhds)
BENCH_HHDS_DELETE_NODE(chain, build_chain_hhds)

BENCH_HHDS_BUILD(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_FAST(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_FORWARD(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_DELETE_PIN(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_DELETE_NODE(fat_pin0, build_fat_pin0_hhds)

BENCH_HHDS_BUILD(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_FAST(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_FORWARD(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_DELETE_PIN(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_DELETE_NODE(fat_pins, build_fat_pins_hhds)

}  // namespace

// chain: 10k nodes (linear)
BENCHMARK(BM_chain_build_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_build_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_fast_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_fast_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_forward_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_forward_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_pin_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_pin_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_node_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_node_hhds)->Arg(kChainLen);

// fat_pin0: 500 nodes, fully connected, all incoming on pin 0
BENCHMARK(BM_fat_pin0_build_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_build_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_fast_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_fast_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_forward_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_forward_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_pin_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_pin_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_node_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_node_hhds)->Arg(kFatLen);

// fat_pins: 500 nodes, fully connected, distinct sink pin per edge
BENCHMARK(BM_fat_pins_build_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_build_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_fast_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_fast_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_forward_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_forward_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_pin_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_pin_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_node_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_node_hhds)->Arg(kFatLen);

BENCHMARK_MAIN();
