//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Random-DAG differential fuzzer (todo/livehd/2a-aegraph.html, milestone C).
//
// Each iteration generates a random combinational DAG over the comb vocabulary
// in unbounded integers (no truncation: every interior cell is stamped with a
// sound upper bound), including '?' literals. The DAG is copied twice with
// hhds::GraphLibrary::copy_from, one copy is optimized by classic cprop and the
// other by the aegraph (pass.aegraph's per-body path), and both are judged by
// the shared Dlop vector oracle (pass/cprop/cprop_test_eval.hpp):
//
//   For every input vector, the outputs of an optimized graph under SOME value
//   of its '?' literals must be among the outputs of the ORIGINAL graph under
//   ALL values of ITS literals. Each literal is ONE value at all its uses (the
//   P7 contract), so a split literal shows up as an output the original cannot
//   produce.
//
// prove_equal runs only to confirm a vector mismatch (the failure message says
// whether the prover agrees), plus once per run on one random literal-free
// iteration. LHD_AEG_FUZZ_ITERS raises the iteration count, LHD_AEG_FUZZ_SEED fixes the
// seed (the failure message prints both).
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "cprop.hpp"
#include "cprop_test_eval.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_aegraph.hpp"
#include "query.hpp"

namespace {
namespace gu = livehd::graph_util;
using livehd::cprop_testing::dlop_eval;
using livehd::cprop_testing::Dlop_memo;
using livehd::cprop_testing::Literal_values;
using livehd::cprop_testing::pin_key;
using Pin = hhds::Pin_class;

constexpr int kInputs  = 4;
constexpr int kOutputs = 2;
constexpr int kMaxBits = 56;  // interior magnitudes stay far below the 64-bit output port

uint64_t env_u64(const char* name, uint64_t fallback) {
  const char* v = std::getenv(name);
  return v == nullptr ? fallback : std::strtoull(v, nullptr, 0);
}

struct Val {
  Pin  pin;
  int  bits;  // upper bound on the signed magnitude bits
  bool literal = false;
};

// One random DAG. Everything is decided by `rng`, so a seed reproduces a graph.
struct Dag {
  std::mt19937_64 rng;
  std::shared_ptr<hhds::Graph> graph;
  std::vector<Val> pool;
  bool             has_literal = false;

  explicit Dag(uint64_t seed) : rng(seed) {}

  size_t pick(size_t n) { return static_cast<size_t>(rng() % n); }
  bool   chance(unsigned percent) { return rng() % 100 < percent; }

  Pin konst(int64_t v) { return gu::create_const(*graph, *Dlop::create_integer(v)); }

  const Val& operand() {
    // Prefer recent values so the DAG gets deep, but keep inputs reachable.
    if (chance(60) && pool.size() > kInputs) {
      const size_t window = std::min<size_t>(pool.size(), 6);
      return pool[pool.size() - 1 - pick(window)];
    }
    return pool[pick(pool.size())];
  }

  static int bits_of_const(int64_t v) {
    const uint64_t magnitude = v < 0 ? ~static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    return std::bit_width(magnitude) + 1;
  }

  void push(const hhds::Node_class& node, int bits, bool is_bool = false) {
    bits     = std::min(bits, kMaxBits);
    auto out = node.create_driver_pin(0);
    if (is_bool) {
      gu::set_ubits(out, 1);
    } else {
      gu::set_sbits(out, bits + 1);
    }
    pool.push_back({out, is_bool ? 1 : bits});
  }

  // A fresh constant operand (a known one, or a '?' literal), as a pool value.
  Val fresh_constant() {
    if (chance(25)) {
      const int n = 1 + static_cast<int>(pick(2));
      has_literal = true;
      return {gu::create_const(*graph, *Dlop::unknown(n)), n + 1, true};
    }
    static constexpr int64_t kInteresting[] = {0, 1, -1, 2, 3, 5, 7, 15, 255, -8, 100};
    const int64_t            v
        = chance(70) ? kInteresting[pick(std::size(kInteresting))] : static_cast<int64_t>(pick(200)) - 100;
    return {konst(v), bits_of_const(v), false};
  }

  Val operand_or_constant() {
    if (chance(20)) {
      return fresh_constant();
    }
    return operand();
  }

  void add_node() {
    switch (pick(15)) {
      case 0:
      case 1: {  // Sum with both banks
        auto n    = gu::create_typed_node(*graph, Ntype_op::Sum);
        int  bits = 0;
        const size_t count = 2 + pick(2);
        for (size_t i = 0; i < count; ++i) {
          const auto v = operand_or_constant();
          gu::setup_sink_by_name(n, i == 0 || chance(60) ? "as" : "bs").connect_driver(v.pin);
          bits = std::max(bits, v.bits);
        }
        push(n, bits + 2);
        break;
      }
      case 2: {  // Mult, kept small
        const auto a = operand_or_constant();
        const auto b = operand_or_constant();
        if (a.bits + b.bits > 40) {
          return;
        }
        auto n = gu::create_typed_node(*graph, Ntype_op::Mult);
        gu::setup_sink_by_name(n, "as").connect_driver(a.pin);
        gu::setup_sink_by_name(n, "as").connect_driver(b.pin);
        push(n, a.bits + b.bits);
        break;
      }
      case 3:
      case 4:
      case 5: {  // And / Or / Xor
        static constexpr Ntype_op kOps[] = {Ntype_op::And, Ntype_op::Or, Ntype_op::Xor};
        auto                      n      = gu::create_typed_node(*graph, kOps[pick(3)]);
        int                       bits   = 0;
        const size_t              count  = 2 + pick(2);
        for (size_t i = 0; i < count; ++i) {
          // Duplicated operands exercise multiplicity (Xor(a,a) is 0).
          const auto v = i == 1 && chance(15) ? pool.back() : operand_or_constant();
          gu::setup_sink_by_name(n, "as").connect_driver(v.pin);
          bits = std::max(bits, v.bits);
        }
        push(n, bits);
        break;
      }
      case 6: {  // Not
        const auto v = operand();
        auto       n = gu::create_typed_node(*graph, Ntype_op::Not);
        n.create_sink_pin(0).connect_driver(v.pin);
        push(n, v.bits);
        break;
      }
      case 7:
      case 8: {  // Mux (two arms, sometimes a constant selector)
        const auto s = chance(10) ? fresh_constant() : operand();
        const auto f = operand_or_constant();
        const auto t = chance(15) ? f : operand_or_constant();
        auto       n = gu::create_typed_node(*graph, Ntype_op::Mux);
        n.create_sink_pin(0).connect_driver(s.pin);
        n.create_sink_pin(1).connect_driver(f.pin);
        n.create_sink_pin(2).connect_driver(t.pin);
        push(n, std::max(f.bits, t.bits));
        break;
      }
      case 9: {  // EQ
        const auto a = operand();
        const auto b = chance(30) ? operand() : operand_or_constant();
        auto       n = gu::create_typed_node(*graph, Ntype_op::EQ);
        gu::setup_sink_by_name(n, "as").connect_driver(a.pin);
        gu::setup_sink_by_name(n, "as").connect_driver(chance(10) ? a.pin : b.pin);
        push(n, 1, true);
        break;
      }
      case 10: {  // LT / GT
        const auto a = operand();
        const auto b = operand_or_constant();
        auto       n = gu::create_typed_node(*graph, chance(50) ? Ntype_op::LT : Ntype_op::GT);
        gu::setup_sink_by_name(n, "as").connect_driver(a.pin);
        gu::setup_sink_by_name(n, "bs").connect_driver(chance(10) ? a.pin : b.pin);
        push(n, 1, true);
        break;
      }
      case 11: {  // SHL / SRA by a constant amount
        const auto v = operand();
        const int  k = static_cast<int>(pick(6));
        auto       n = gu::create_typed_node(*graph, chance(50) ? Ntype_op::SHL : Ntype_op::SRA);
        gu::setup_sink_by_name(n, "a").connect_driver(v.pin);
        gu::setup_sink_by_name(n, "b").connect_driver(konst(k));
        push(n, gu::type_op_of(n) == Ntype_op::SHL ? v.bits + k : v.bits);
        break;
      }
      case 12: {  // Sext
        const auto v = operand();
        const int  w = 1 + static_cast<int>(pick(10));
        auto       n = gu::create_typed_node(*graph, Ntype_op::Sext);
        gu::setup_sink_by_name(n, "a").connect_driver(v.pin);
        gu::setup_sink_by_name(n, "b").connect_driver(konst(w));
        push(n, w);
        break;
      }
      case 13: {  // Get_mask window
        const auto v  = operand();
        const int  lo = static_cast<int>(pick(6));
        const int  hi = lo + 1 + static_cast<int>(pick(8));
        auto       n  = gu::create_get_mask(*graph, v.pin, lo, hi);
        push(n, hi - lo + 1);
        break;
      }
      default: {  // a repeated operation: a hash-cons opportunity
        if (pool.size() <= kInputs) {
          return;
        }
        const auto& last = pool.back();
        const auto  src  = last.pin.get_master_node();
        const auto  op   = gu::type_op_of(src);
        if (op != Ntype_op::Sum && op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor) {
          return;
        }
        auto twin = gu::create_typed_node(*graph, op);
        // A snapshot: building the twin mutates the body the sorted-pin walk is over.
        for (const auto& operand : gu::inp_sink_drivers(src)) {
          gu::setup_sink_by_name(twin, Ntype::get_sink_name(op, operand.get_port_id())).connect_driver(operand.driver);
        }
        push(twin, last.bits);
        break;
      }
    }
  }

  void build(hhds::GraphLibrary& lib, const std::string& name) {
    auto io = lib.create_io(name);
    int  pid = 0;
    for (int i = 0; i < kInputs; ++i) {
      io->add_input("i" + std::to_string(i), pid++);
      io->set_bits("i" + std::to_string(i), 8);
      io->set_unsign("i" + std::to_string(i), true);
    }
    for (int i = 0; i < kOutputs; ++i) {
      io->add_output("o" + std::to_string(i), pid++);
      io->set_bits("o" + std::to_string(i), 64);
      io->set_unsign("o" + std::to_string(i), false);
    }
    graph = io->create_graph();
    for (int i = 0; i < kInputs; ++i) {
      pool.push_back({graph->get_input_pin("i" + std::to_string(i)), 8});
    }
    const size_t nodes = 5 + pick(22);
    for (size_t i = 0; i < nodes; ++i) {
      add_node();
    }
    // Outputs: the last values built, so most of the DAG is live.
    for (int i = 0; i < kOutputs; ++i) {
      const size_t back = pool.size() > kInputs ? static_cast<size_t>(i) % (pool.size() - kInputs) : 0;
      const auto&  v    = pool[pool.size() - 1 - back];
      v.pin.connect_sink(graph->get_output_pin("o" + std::to_string(i)));
    }
  }
};

// ---- the oracle -------------------------------------------------------------

struct Literal {
  uint64_t         key;
  std::vector<int> unknown_bits;  // bit positions that are '?'
  Dlop             written;
};

void collect_literals(const Pin& p, std::set<uint64_t>& seen, std::vector<Literal>& out) {
  if (p.is_const()) {
    const auto d = gu::const_of(p);
    if (d.has_unknowns() && seen.insert(pin_key(p)).second) {
      Literal l{pin_key(p), {}, d};
      for (int bit = 0; bit < 24; ++bit) {
        if (d.unknown_bit_test(bit)) {
          l.unknown_bits.push_back(bit);
        }
      }
      out.push_back(std::move(l));
    }
    return;
  }
  if (!seen.insert(pin_key(p)).second) {
    return;
  }
  for (auto sink : p.get_master_node().inp_sorted_pins()) {
    for (auto driver : sink.get_driver_pins()) {
      collect_literals(driver, seen, out);
    }
  }
}

std::vector<Pin> output_drivers(const std::shared_ptr<hhds::Graph>& g) {
  std::vector<Pin> drivers;
  for (int i = 0; i < kOutputs; ++i) {
    drivers.push_back(g->get_output_pin("o" + std::to_string(i)).get_driver_pin());
  }
  return drivers;
}

// Every assignment of the graph's '?' literals (one value per literal node).
// An empty literal list gives exactly one (empty) assignment.
std::vector<Literal_values> assignments(const std::vector<Literal>& literals) {
  std::vector<Literal_values> all(1);
  for (const auto& l : literals) {
    std::vector<Literal_values> next;
    const uint64_t              combos = uint64_t{1} << l.unknown_bits.size();
    for (const auto& partial : all) {
      for (uint64_t c = 0; c < combos; ++c) {
        int64_t v = 0;
        for (size_t k = 0; k < l.unknown_bits.size(); ++k) {
          if (c & (uint64_t{1} << k)) {
            v |= int64_t{1} << l.unknown_bits[k];
          }
        }
        const auto concrete = *Dlop::create_integer(v);
        // The resolution assumes the known bits are zero (unknown(n)); a literal
        // whose written form rules the value out would be misjudged, so say so.
        if (l.written.eq_op(concrete)->is_known_false()) {
          ADD_FAILURE() << "literal " << l.written.to_pyrope() << " has known one bits; the oracle cannot enumerate it";
        }
        auto copy = partial;
        copy.emplace(l.key, concrete);
        next.push_back(std::move(copy));
      }
    }
    all = std::move(next);
  }
  return all;
}

// The outputs of `g` for one input vector under one assignment, as a string.
std::string run_vector(const std::shared_ptr<hhds::Graph>& g, const std::vector<int64_t>& in, const Literal_values& lits) {
  Dlop_memo memo;
  for (int i = 0; i < kInputs; ++i) {
    memo.emplace(pin_key(g->get_input_pin("i" + std::to_string(i))), *Dlop::create_integer(in[static_cast<size_t>(i)]));
  }
  std::string result;
  for (const auto& drv : output_drivers(g)) {
    result += dlop_eval(drv, memo, &lits).to_pyrope() + ";";
  }
  return result;
}

std::vector<Literal> literals_of(const std::shared_ptr<hhds::Graph>& g) {
  std::vector<Literal> out;
  std::set<uint64_t>   seen;
  for (const auto& drv : output_drivers(g)) {
    collect_literals(drv, seen, out);
  }
  std::sort(out.begin(), out.end(), [](const Literal& a, const Literal& b) { return a.key < b.key; });
  return out;
}

// "" when `opt` refines `orig` on every vector; else the first offending vector.
std::string refinement_failure(const std::shared_ptr<hhds::Graph>& orig, const std::shared_ptr<hhds::Graph>& opt,
                               const std::vector<std::vector<int64_t>>& vectors) {
  const auto orig_assign = assignments(literals_of(orig));
  const auto opt_assign  = assignments(literals_of(opt));
  for (const auto& vec : vectors) {
    std::set<std::string> allowed;
    for (const auto& a : orig_assign) {
      allowed.insert(run_vector(orig, vec, a));
    }
    bool ok = false;
    for (const auto& a : opt_assign) {
      if (allowed.contains(run_vector(opt, vec, a))) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      std::string text = "vector";
      for (const auto v : vec) {
        text += " " + std::to_string(v);
      }
      text += ": optimized outputs ";
      text += run_vector(opt, vec, opt_assign.front());
      text += " not among the original's {";
      for (const auto& s : allowed) {
        text += s + " ";
      }
      text += "}";
      return text;
    }
  }
  return {};
}

size_t count_nodes(const std::shared_ptr<hhds::Graph>& g) {
  size_t n = 0;
  for ([[maybe_unused]] auto node : g->body().nodes()) {
    ++n;
  }
  return n;
}

std::vector<std::vector<int64_t>> input_vectors(std::mt19937_64& rng) {
  std::vector<std::vector<int64_t>> vectors{{0, 0, 0, 0}, {255, 255, 255, 255}, {1, 2, 3, 4}};
  for (int i = 0; i < 8; ++i) {
    std::vector<int64_t> v;
    for (int k = 0; k < kInputs; ++k) {
      v.push_back(static_cast<int64_t>(rng() % 256));
    }
    vectors.push_back(std::move(v));
  }
  return vectors;
}

// One line per node, operands as `nid.pid` (or the constant), for a failure report.
std::string dump(const std::shared_ptr<hhds::Graph>& g) {
  std::string text;
  for (auto node : g->body().nodes()) {
    text += "    n" + std::to_string(node.get_debug_nid()) + " " + std::string{Ntype::get_name(gu::type_op_of(node))} + " <-";
    for (auto sink : node.inp_sorted_pins()) {
      text += " " + std::to_string(sink.get_port_id()) + "=";
      for (const auto& driver : sink.get_driver_pins()) {
        if (driver.is_const()) {
          text += "c" + gu::const_of(driver).to_pyrope();
        } else {
          text += "n" + std::to_string(driver.get_master_node().get_debug_nid());
        }
        text += ",";
      }
    }
    text += "\n";
  }
  return text;
}

std::string proof_text(const std::shared_ptr<hhds::Graph>& ref, const std::shared_ptr<hhds::Graph>& impl) {
  livehd::lec::Lec_options options;
  options.cones   = "false";
  options.timeout = 5;
  const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
  return result.verdict == livehd::lec::Verdict::Refuted ? "prove_equal: REFUTED" : "prove_equal: did not refute";
}

TEST(AegDiff, RandomDagsClassicAndAegraphRefineTheOriginal) {
  const uint64_t iters = env_u64("LHD_AEG_FUZZ_ITERS", 1000);
  const uint64_t seed  = env_u64("LHD_AEG_FUZZ_SEED", 0x5eed0001);
  std::mt19937_64 pick_rng(seed ^ 0x9e3779b97f4a7c15ULL);
  const uint64_t prove_iteration = pick_rng() % iters;
  bool           proved          = false;
  size_t         with_literals   = 0;
  size_t         nodes_total = 0, classic_removed = 0, aegraph_removed = 0;

  for (uint64_t it = 0; it < iters; ++it) {
    const uint64_t dag_seed = seed + it * 0x10001ULL;
    SCOPED_TRACE("LHD_AEG_FUZZ_SEED=" + std::to_string(seed) + " iteration=" + std::to_string(it)
                 + " (LHD_AEG_FUZZ_ITERS=" + std::to_string(iters) + ")");
    hhds::GraphLibrary lib_orig, lib_classic, lib_aegraph;
    Dag                dag(dag_seed);
    dag.build(lib_orig, "fz");
    with_literals += dag.has_literal;
    ASSERT_TRUE(lib_classic.copy_from(lib_orig, "fz"));
    ASSERT_TRUE(lib_aegraph.copy_from(lib_orig, "fz"));
    auto orig        = lib_orig.find_io("fz")->get_graph();
    auto classic_dag = lib_classic.find_io("fz")->get_graph();
    auto aegraph_dag = lib_aegraph.find_io("fz")->get_graph();

    const size_t nodes_before = count_nodes(orig);
    Cprop        classic_cprop{};
    classic_cprop.do_trans(classic_dag);
    Cprop aegraph_cprop{};
    (void)livehd::aegraph::optimize_body(aegraph_cprop,
                                         aegraph_dag,
                                         livehd::aegraph::Options{.check = livehd::aegraph::Options::Check::full});

    nodes_total += nodes_before;
    classic_removed += nodes_before - std::min(nodes_before, count_nodes(classic_dag));
    aegraph_removed += nodes_before - std::min(nodes_before, count_nodes(aegraph_dag));
    const auto vectors = input_vectors(dag.rng);
    for (const auto& [name, graph] : {std::pair{"classic", classic_dag}, std::pair{"aegraph", aegraph_dag}}) {
      const auto failure = refinement_failure(orig, graph, vectors);
      if (!failure.empty()) {
        ADD_FAILURE() << name << " is not a refinement of the original: " << failure << "\n  " << proof_text(orig, graph)
                      << "\n  original:\n"
                      << dump(orig) << "  optimized:\n"
                      << dump(graph);
      }
    }
    if (it == prove_iteration && !dag.has_literal) {
      proved = true;
      livehd::lec::Lec_options options;
      options.cones   = "false";
      options.timeout = 10;
      const auto result = livehd::lec::prove_equal(orig.get(), aegraph_dag.get(), options);
      EXPECT_NE(result.verdict, livehd::lec::Verdict::Refuted) << result.detail << '\n' << result.witness;
    }
  }
  // The random iteration may have drawn a graph with a literal (LEC would judge a
  // literal under another x semantics); settle on the first literal-free one.
  if (!proved) {
    for (uint64_t it = 0; it < 50 && !proved; ++it) {
      hhds::GraphLibrary lib_orig, lib_aegraph;
      Dag                dag(seed + 0xabcdefULL + it);
      dag.build(lib_orig, "fz");
      if (dag.has_literal) {
        continue;
      }
      ASSERT_TRUE(lib_aegraph.copy_from(lib_orig, "fz"));
      auto orig = lib_orig.find_io("fz")->get_graph();
      auto impl = lib_aegraph.find_io("fz")->get_graph();
      Cprop cp{};
      (void)livehd::aegraph::optimize_body(cp, impl, livehd::aegraph::Options{.check = livehd::aegraph::Options::Check::full});
      livehd::lec::Lec_options options;
      options.cones   = "false";
      options.timeout = 10;
      const auto result = livehd::lec::prove_equal(orig.get(), impl.get(), options);
      EXPECT_NE(result.verdict, livehd::lec::Verdict::Refuted) << result.detail << '\n' << result.witness;
      proved = true;
    }
  }
  std::printf("[aeg_diff_smoke] iterations=%llu with_literals=%zu nodes=%zu removed: classic=%zu aegraph=%zu\n",
              static_cast<unsigned long long>(iters), with_literals, nodes_total, classic_removed, aegraph_removed);
  EXPECT_TRUE(proved);
  EXPECT_GT(with_literals, 0u) << "the generator should reach '?' literals";
}

}  // namespace
