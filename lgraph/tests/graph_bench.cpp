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
// For each shape we measure four operations: create / fast traverse /
// forward traverse / delete (per-node edges then node).

#include <string>
#include <vector>

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

  std::vector<Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    Node node = g->create_node(Ntype_op::Or, 1);
    for (int j = 0; j < i; ++j) {
      nodes[j].setup_driver_pin().connect_sink(node.setup_sink_pin_raw(0));
    }
    nodes.push_back(node);
  }
  return g;
}

static Lgraph* build_fat_pins_lg(const std::string& name, int n) {
  auto* lib = Graph_library::instance("lgdb_graph_bench");
  auto* g   = lib->create_lgraph(name, "graph_bench");

  std::vector<Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    Node node = g->create_node(Ntype_op::Mux, 1);  // unlimited_sink
    for (int j = 0; j < i; ++j) {
      nodes[j].setup_driver_pin().connect_sink(node.setup_sink_pin_raw(static_cast<Port_ID>(j)));
    }
    nodes.push_back(node);
  }
  return g;
}

// ---------------------------------------------------------------------------
// hhds builders
// ---------------------------------------------------------------------------

struct HhdsBundle {
  hhds::GraphLibrary               lib;
  std::shared_ptr<hhds::GraphIO>   gio;
  std::shared_ptr<hhds::Graph>     graph;
};

static std::unique_ptr<HhdsBundle> make_hhds_bundle(const char* name) {
  auto b = std::make_unique<HhdsBundle>();
  b->gio = b->lib.create_io(name);
  b->gio->add_input("in", 0);
  b->gio->add_output("out", 0);
  b->graph = b->gio->create_graph();
  return b;
}

static std::unique_ptr<HhdsBundle> build_chain_hhds(int n) {
  auto                    b = make_hhds_bundle("chain_hhds");
  auto                    g = b->graph;
  std::vector<hhds::Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    nodes.push_back(g->create_node());
  }
  for (int i = 0; i + 1 < n; ++i) {
    nodes[i].create_driver_pin().connect_sink(nodes[i + 1].create_sink_pin());
  }
  return b;
}

static std::unique_ptr<HhdsBundle> build_fat_pin0_hhds(int n) {
  auto                    b = make_hhds_bundle("fat_pin0_hhds");
  auto                    g = b->graph;
  std::vector<hhds::Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    auto node = g->create_node();
    // Every incoming edge lands on the same sink pin (port 0).
    auto spin = node.create_sink_pin(static_cast<hhds::Port_id>(0));
    for (int j = 0; j < i; ++j) {
      nodes[j].create_driver_pin().connect_sink(spin);
    }
    nodes.push_back(node);
  }
  return b;
}

static std::unique_ptr<HhdsBundle> build_fat_pins_hhds(int n) {
  auto                    b = make_hhds_bundle("fat_pins_hhds");
  auto                    g = b->graph;
  std::vector<hhds::Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    auto node = g->create_node();
    for (int j = 0; j < i; ++j) {
      auto spin = node.create_sink_pin(static_cast<hhds::Port_id>(j));
      nodes[j].create_driver_pin().connect_sink(spin);
    }
    nodes.push_back(node);
  }
  return b;
}

// ---------------------------------------------------------------------------
// Lgraph benchmarks (build / fast / forward / delete) × (chain / fat_pin0 / fat_pins)
// ---------------------------------------------------------------------------

#define BENCH_LG_BUILD(NAME, BUILDER)                                                   \
  void BM_##NAME##_build_lgraph(benchmark::State& state) {                              \
    for (auto _ : state) {                                                              \
      auto* g = BUILDER(std::string(#NAME "_lg_") + std::to_string(next_gen()),         \
                       static_cast<int>(state.range(0)));                               \
      benchmark::DoNotOptimize(g);                                                      \
    }                                                                                   \
  }

#define BENCH_LG_FAST(NAME, BUILDER)                                                    \
  void BM_##NAME##_fast_lgraph(benchmark::State& state) {                               \
    auto* g = BUILDER(std::string(#NAME "_lg_fast_") + std::to_string(next_gen()),      \
                     static_cast<int>(state.range(0)));                                 \
    for (auto _ : state) {                                                              \
      int n_nodes = 0;                                                                  \
      for (auto node : g->fast()) {                                                     \
        benchmark::DoNotOptimize(node);                                                 \
        ++n_nodes;                                                                      \
      }                                                                                 \
      benchmark::DoNotOptimize(n_nodes);                                                \
    }                                                                                   \
  }

#define BENCH_LG_FORWARD(NAME, BUILDER)                                                 \
  void BM_##NAME##_forward_lgraph(benchmark::State& state) {                            \
    auto* g = BUILDER(std::string(#NAME "_lg_fwd_") + std::to_string(next_gen()),       \
                     static_cast<int>(state.range(0)));                                 \
    for (auto _ : state) {                                                              \
      int n_nodes = 0;                                                                  \
      for (auto node : g->forward()) {                                                  \
        benchmark::DoNotOptimize(node);                                                 \
        ++n_nodes;                                                                      \
      }                                                                                 \
      benchmark::DoNotOptimize(n_nodes);                                                \
    }                                                                                   \
  }

#define BENCH_LG_DELETE(NAME, BUILDER)                                                  \
  void BM_##NAME##_delete_lgraph(benchmark::State& state) {                             \
    for (auto _ : state) {                                                              \
      state.PauseTiming();                                                              \
      auto* g = BUILDER(std::string(#NAME "_lg_del_") + std::to_string(next_gen()),     \
                       static_cast<int>(state.range(0)));                               \
      state.ResumeTiming();                                                             \
      for (auto node : g->fast()) {                                                     \
        for (auto& e : node.out_edges()) {                                              \
          e.del_edge();                                                                 \
        }                                                                               \
        for (auto& e : node.inp_edges()) {                                              \
          e.del_edge();                                                                 \
        }                                                                               \
        node.del_node();                                                                \
      }                                                                                 \
    }                                                                                   \
  }

BENCH_LG_BUILD(chain, build_chain_lg)
BENCH_LG_FAST(chain, build_chain_lg)
BENCH_LG_FORWARD(chain, build_chain_lg)
BENCH_LG_DELETE(chain, build_chain_lg)

BENCH_LG_BUILD(fat_pin0, build_fat_pin0_lg)
BENCH_LG_FAST(fat_pin0, build_fat_pin0_lg)
BENCH_LG_FORWARD(fat_pin0, build_fat_pin0_lg)
BENCH_LG_DELETE(fat_pin0, build_fat_pin0_lg)

BENCH_LG_BUILD(fat_pins, build_fat_pins_lg)
BENCH_LG_FAST(fat_pins, build_fat_pins_lg)
BENCH_LG_FORWARD(fat_pins, build_fat_pins_lg)
BENCH_LG_DELETE(fat_pins, build_fat_pins_lg)

// ---------------------------------------------------------------------------
// hhds benchmarks (build / fast / forward / delete) × (chain / fat_pin0 / fat_pins)
// ---------------------------------------------------------------------------

#define BENCH_HHDS_BUILD(NAME, BUILDER)                                                 \
  void BM_##NAME##_build_hhds(benchmark::State& state) {                                \
    for (auto _ : state) {                                                              \
      auto b = BUILDER(static_cast<int>(state.range(0)));                               \
      benchmark::DoNotOptimize(b);                                                      \
    }                                                                                   \
  }

#define BENCH_HHDS_FAST(NAME, BUILDER)                                                  \
  void BM_##NAME##_fast_hhds(benchmark::State& state) {                                 \
    auto b = BUILDER(static_cast<int>(state.range(0)));                                 \
    auto g = b->graph;                                                                  \
    for (auto _ : state) {                                                              \
      int n_nodes = 0;                                                                  \
      for (auto node : g->fast_class()) {                                               \
        benchmark::DoNotOptimize(node);                                                 \
        ++n_nodes;                                                                      \
      }                                                                                 \
      benchmark::DoNotOptimize(n_nodes);                                                \
    }                                                                                   \
  }

#define BENCH_HHDS_FORWARD(NAME, BUILDER)                                               \
  void BM_##NAME##_forward_hhds(benchmark::State& state) {                              \
    auto b = BUILDER(static_cast<int>(state.range(0)));                                 \
    auto g = b->graph;                                                                  \
    for (auto _ : state) {                                                              \
      int n_nodes = 0;                                                                  \
      for (auto node : g->forward_class()) {                                            \
        benchmark::DoNotOptimize(node);                                                 \
        ++n_nodes;                                                                      \
      }                                                                                 \
      benchmark::DoNotOptimize(n_nodes);                                                \
    }                                                                                   \
  }

#define BENCH_HHDS_DELETE(NAME, BUILDER)                                                \
  void BM_##NAME##_delete_hhds(benchmark::State& state) {                               \
    for (auto _ : state) {                                                              \
      state.PauseTiming();                                                              \
      auto b = BUILDER(static_cast<int>(state.range(0)));                               \
      auto g = b->graph;                                                                \
      state.ResumeTiming();                                                             \
      for (auto node : g->fast_class()) {                                               \
        if (node.get_debug_nid() < 16) {                                                \
          continue;  /* built-in IO/Const nodes — cannot be deleted */                  \
        }                                                                               \
        for (auto& e : node.out_edges()) {                                              \
          e.del_edge();                                                                 \
        }                                                                               \
        for (auto& e : node.inp_edges()) {                                              \
          e.del_edge();                                                                 \
        }                                                                               \
        node.del_node();                                                                \
      }                                                                                 \
    }                                                                                   \
  }

BENCH_HHDS_BUILD(chain, build_chain_hhds)
BENCH_HHDS_FAST(chain, build_chain_hhds)
BENCH_HHDS_FORWARD(chain, build_chain_hhds)
BENCH_HHDS_DELETE(chain, build_chain_hhds)

BENCH_HHDS_BUILD(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_FAST(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_FORWARD(fat_pin0, build_fat_pin0_hhds)
BENCH_HHDS_DELETE(fat_pin0, build_fat_pin0_hhds)

BENCH_HHDS_BUILD(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_FAST(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_FORWARD(fat_pins, build_fat_pins_hhds)
BENCH_HHDS_DELETE(fat_pins, build_fat_pins_hhds)

}  // namespace

// chain: 10k nodes (linear)
BENCHMARK(BM_chain_build_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_build_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_fast_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_fast_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_forward_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_forward_hhds)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_lgraph)->Arg(kChainLen);
BENCHMARK(BM_chain_delete_hhds)->Arg(kChainLen);

// fat_pin0: 500 nodes, fully connected, all incoming on pin 0
BENCHMARK(BM_fat_pin0_build_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_build_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_fast_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_fast_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_forward_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_forward_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pin0_delete_hhds)->Arg(kFatLen);

// fat_pins: 500 nodes, fully connected, distinct sink pin per edge
BENCHMARK(BM_fat_pins_build_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_build_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_fast_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_fast_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_forward_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_forward_hhds)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_lgraph)->Arg(kFatLen);
BENCHMARK(BM_fat_pins_delete_hhds)->Arg(kFatLen);

BENCHMARK_MAIN();
