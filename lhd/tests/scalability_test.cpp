// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Scalability contract for the passes that must be linear in their input:
// pass.upass (the LNAST micropasses), lnast.tolg, pass.aegraph, pass.bitwidth
// and inou.cgen.verilog. Synthesis, LEC and coloring are not linear by contract
// and are not measured here.
//
// The inputs are generated in C++, not parsed: a synthetic LNAST unit (fed to
// the whole LNAST -> LGraph -> Verilog chain) and a synthetic LGraph (fed to
// aegraph, bitwidth and cgen directly). Every section of each grows with N
// while nesting depth stays bounded, so a linear pass costs ~4x per 4x size.
// Each input is run at N, 4N and 16N; a pass whose time grows more than kLimit
// per 4x step (quadratic is 16x) fails the test.
//
// Tag `scalability`: only `bazel test --config=all` runs it (tools/regression.md).
// LHD_SCALE_N overrides the base size; LHD_SCALE_VERBOSE prints every phase.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <print>
#include <string>
#include <vector>

#include "cell.hpp"
#include "eprp_var.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "inou_cgen.hpp"
#include "lnast.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass_aegraph.hpp"
#include "pass_bitwidth.hpp"
#include "pass_upass.hpp"
#include "upass_tolg.hpp"

namespace {

namespace fs = std::filesystem;
namespace gu = livehd::graph_util;
using N      = Lnast_ntype;

constexpr double kLimit = 8.0;    // per 4x step: twice linear, half quadratic
constexpr double kMinMs = 150.0;  // a phase below this at 16x is noise, not scaling

int base_n(int dflt) {
  const char* v = std::getenv("LHD_SCALE_N");
  return v != nullptr ? std::atoi(v) : dflt;
}

std::string fresh_dir(std::string_view tag) {
  static int  serial = 0;
  const char* tmp    = std::getenv("TEST_TMPDIR");
  auto        path   = fs::path(tmp ? tmp : fs::temp_directory_path().string()) / std::format("scale_{}_{}", tag, serial++);
  fs::remove_all(path);
  fs::create_directories(path);
  return path.string();
}

void setup_passes() {
  static bool done = false;
  if (!done) {
    Pass_upass::setup();
    Pass_aegraph::setup();
    Pass_bitwidth::setup();
    Inou_cgen::setup();
    done = true;
  }
}

using Times = std::map<std::string, double>;  // phase -> ms

class Clock {
public:
  explicit Clock(Times& t, std::string phase) : t_(t), phase_(std::move(phase)), t0_(std::chrono::steady_clock::now()) {}
  ~Clock() { t_[phase_] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count(); }

private:
  Times&                                t_;
  std::string                           phase_;
  std::chrono::steady_clock::time_point t0_;
};

// Fails when a measured phase grows faster than linear across the 3 sizes.
void check_linear(std::string_view what, const std::vector<int>& sizes, const std::vector<Times>& runs) {
  std::map<std::string, std::vector<double>> by_phase;
  for (const auto& run : runs) {
    for (const auto& [phase, ms] : run) {
      by_phase[phase].push_back(ms);
    }
  }
  const bool verbose = std::getenv("LHD_SCALE_VERBOSE") != nullptr;
  for (const auto& [phase, t] : by_phase) {
    ASSERT_EQ(t.size(), runs.size()) << phase;
    const double r1       = t[1] / std::max(t[0], 1.0);
    const double r2       = t[2] / std::max(t[1], 1.0);
    const bool   enforced = t[2] >= kMinMs;
    if (verbose || (enforced && std::max(r1, r2) > kLimit)) {
      std::println("{} {:22s} N={}/{}/{}: {:8.0f} {:8.0f} {:8.0f} ms  x{:.2f} x{:.2f}{}",
                   what,
                   phase,
                   sizes[0],
                   sizes[1],
                   sizes[2],
                   t[0],
                   t[1],
                   t[2],
                   r1,
                   r2,
                   enforced ? "" : "  (too small to enforce)");
    }
    if (enforced) {
      EXPECT_LE(std::max(r1, r2), kLimit) << what << " " << phase << " grows faster than linear: " << t[0] << " / " << t[1] << " / "
                                          << t[2] << " ms at N=" << sizes[0] << "/" << sizes[1] << "/" << sizes[2];
    }
  }
}

// ── synthetic LNAST ─────────────────────────────────────────────────────────

struct Lnast_gen {
  std::shared_ptr<Lnast> ln;
  Lnast_nid              body;
  int                    serial = 0;

  explicit Lnast_gen(std::string_view name) : ln(std::make_shared<Lnast>(name)) {
    auto root = ln->set_root(N::create_top());
    body      = ln->add_child(root, N::create_stmts());
    ln->set_lambda_kind("mod");
    ln->set_skip_timecheck(true);
  }
  std::string tmp() { return std::format("%g{}", serial++); }
  void        in(std::string_view n, int bits) {
    ln->io_meta().inputs.push_back(Lnast_io_entry{.name = std::string(n), .bits = bits, .is_signed = false});
  }
  void out(std::string_view n, int bits) {
    ln->io_meta().outputs.push_back(Lnast_io_entry{.name = std::string(n), .bits = bits, .is_signed = false});
  }
  void leaf(const Lnast_nid& p, std::string_view s) {
    const bool is_const = !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) != 0);
    (void)ln->add_child(p, is_const ? Lnast_node::create_const(s) : Lnast_node::create_ref(s));
  }
  // dst = op(a, b) in `at`.
  std::string op(const Lnast_nid& at, N::Lnast_ntype_int kind, std::string_view a, std::string_view b, std::string dst = {}) {
    if (dst.empty()) {
      dst = tmp();
    }
    auto s = ln->add_child(at, kind);
    (void)ln->add_child(s, Lnast_node::create_ref(dst));
    leaf(s, a);
    leaf(s, b);
    return dst;
  }
  void store(const Lnast_nid& at, std::string_view dst, std::string_view rhs) {
    auto s = ln->add_child(at, N::create_store());
    (void)ln->add_child(s, Lnast_node::create_ref(dst));
    leaf(s, rhs);
  }
  // `mode name:u16 = init`.
  void declare(std::string_view name, std::string_view mode, std::string_view init) {
    auto d = ln->add_child(body, N::create_declare());
    (void)ln->add_child(d, Lnast_node::create_ref(name));
    auto t = ln->add_child(d, N::create_prim_type_int());
    (void)ln->add_child(t, Lnast_node::create_const("65535"));
    (void)ln->add_child(t, Lnast_node::create_const("0"));
    (void)ln->add_child(d, Lnast_node::create_const(mode));
    store(body, name, init);
  }
};

// One module whose every section scales with n.
std::shared_ptr<Lnast> synth_lnast(int n) {
  Lnast_gen g("scale.top");
  g.in("a", 16);
  g.in("b", 16);
  g.in("s", 16);
  g.in("c", 16);
  for (int i = 0; i < 5; ++i) {
    g.out(std::format("o{}", i), 16);
  }
  const auto& B = g.body;
  // 1. a decoder: one elif chain over one selector (bitwidth arm restore and
  //    guards, the runner's if walk, tolg lower_if, aegraph Mux_sharing).
  g.declare("x", "mut", "0");
  {
    std::vector<std::string> conds;
    for (int i = 0; i < n; ++i) {
      conds.push_back(g.op(B, N::create_eq(), "s", std::to_string(i)));
    }
    auto node = g.ln->add_child(B, N::create_if());
    for (int i = 0; i < n; ++i) {
      g.leaf(node, conds[i]);
      auto arm = g.ln->add_child(node, N::create_stmts());
      g.store(arm, "x", g.op(arm, N::create_bit_xor(), "a", std::to_string(i)));
    }
  }
  g.store(B, "o0", "x");
  // 2. guarded subtractions: `if a > k { y = a - k }` chains (bitwidth guard facts).
  g.declare("y", "mut", "0");
  {
    std::vector<std::string> conds;
    std::vector<std::string> ks;
    for (int i = 0; i < n / 4; ++i) {
      ks.push_back(g.op(B, N::create_bit_xor(), "b", std::to_string(i)));
      conds.push_back(g.op(B, N::create_gt(), "a", ks.back()));
    }
    auto node = g.ln->add_child(B, N::create_if());
    for (size_t i = 0; i < conds.size(); ++i) {
      g.leaf(node, conds[i]);
      auto arm = g.ln->add_child(node, N::create_stmts());
      g.store(arm, "y", g.op(arm, N::create_minus(), "a", ks[i]));
    }
  }
  g.store(B, "o1", "y");
  // 3. many small if/else nests (depth 2): merges, scopes, symbol tables.
  g.declare("acc", "mut", "0");
  for (int i = 0; i < n / 4; ++i) {
    const auto t = std::format("t{}", i);
    g.declare(t, "mut", g.op(B, N::create_bit_xor(), "a", std::to_string(i)));
    const auto c1    = g.op(B, N::create_ne(), g.op(B, N::create_bit_and(), "c", std::to_string(1 << (i % 16))), "0");
    const auto c2    = g.op(B, N::create_ne(), g.op(B, N::create_bit_and(), "c", std::to_string(1 << ((i + 7) % 16))), "0");
    auto       outer = g.ln->add_child(B, N::create_if());
    g.leaf(outer, c1);
    auto arm = g.ln->add_child(outer, N::create_stmts());
    g.store(arm, t, g.op(arm, N::create_bit_or(), t, std::to_string(i & 0xFF)));
    auto inner = g.ln->add_child(arm, N::create_if());
    g.leaf(inner, c2);
    auto th = g.ln->add_child(inner, N::create_stmts());
    g.store(th, t, g.op(th, N::create_bit_xor(), t, "b"));
    auto el = g.ln->add_child(inner, N::create_stmts());
    g.store(el, t, g.op(el, N::create_bit_and(), t, "b"));
    g.store(B, "acc", g.op(B, N::create_bit_xor(), "acc", t));
  }
  g.store(B, "o2", "acc");
  // 4. a wide expression chain (tree aegraph flatten/CSE, LGraph aegraph).
  g.declare("e", "mut", "a");
  for (int i = 0; i < n / 2; ++i) {
    const auto k  = g.op(B, N::create_bit_xor(), "b", std::to_string(i));
    const auto op = i % 3 == 0 ? N::create_bit_xor() : (i % 3 == 1 ? N::create_bit_or() : N::create_bit_and());
    g.store(B, "e", g.op(B, op, "e", k));
  }
  g.store(B, "o3", "e");
  // 5. a register chain.
  for (int i = 0; i < n / 8; ++i) {
    const auto r = std::format("r{}", i);
    g.declare(r, "reg", "0");
    g.store(B, r, g.op(B, N::create_bit_xor(), i == 0 ? std::string("a") : std::format("r{}", i - 1), std::to_string(i)));
  }
  g.store(B, "o4", std::format("r{}", n / 8 - 1));
  return g.ln;
}

Times run_lnast_pipeline(int n) {
  setup_passes();
  Times                                       t;
  const auto                                  lib = fresh_dir("lnast");
  livehd::Hhds_graph_library::Scoped_instance instance(lib);
  Eprp_var                                    var;
  var.add(synth_lnast(n));
  {
    Clock c(t, "pass.upass");
    Pass::eprp.run_method_now("pass.upass",
                              var,
                              {
                                  {"constprop",    "1"},
                                  { "verifier", "true"},
                                  {      "dce", "mark"}
    });
  }
  {
    Clock c(t, "lnast.tolg");
    uPass_tolg::detect_lg_collisions(var.lnasts);
    for (const auto& ln : var.lnasts) {
      uPass_tolg::register_io(ln, lib, var.lnasts);
    }
    for (const auto& ln : uPass_tolg::lowering_order(var.lnasts)) {
      if (auto g = uPass_tolg::run(ln, lib, var.lnasts, "sync")) {
        var.add(g);
      }
    }
  }
  EXPECT_FALSE(var.graphs.empty()) << "tolg built no graph at N=" << n;
  {
    Clock c(t, "pass.aegraph");
    Pass::eprp.run_method_now("pass.aegraph", var, {});
  }
  {
    Clock c(t, "pass.bitwidth");
    Pass::eprp.run_method_now("pass.bitwidth", var, {});
  }
  {
    Clock c(t, "pass.aegraph");  // the after-bitwidth slot
    Pass::eprp.run_method_now("pass.aegraph",
                              var,
                              {
                                  {"after_bitwidth", "true"}
    });
  }
  {
    Clock c(t, "pass.bitwidth");
    Pass::eprp.run_method_now("pass.bitwidth", var, {});
  }
  {
    Clock c(t, "inou.cgen.verilog");
    Pass::eprp.run_method_now("inou.cgen.verilog",
                              var,
                              {
                                  {"odir", fresh_dir("lnast_v")}
    });
  }
  return t;
}

// ── synthetic LGraph ────────────────────────────────────────────────────────

// Wide selections (binary Mux decoder chains and Hotmux trees), long n-ary and
// chained arithmetic, and a flop chain, all over sized inputs.
std::shared_ptr<hhds::Graph> synth_lgraph(hhds::GraphLibrary& lib, int n) {
  static int serial = 0;
  auto       io     = lib.create_io(std::format("scale_lg_{}", serial++));
  io->add_input("s", 0);
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("clock", 3);
  io->add_output("o0", 4);
  io->add_output("o1", 5);
  io->add_output("o2", 6);
  for (const auto* p : {"s", "a", "b", "o0", "o1", "o2"}) {
    io->set_bits(p, 16);
    io->set_unsign(p, true);
  }
  io->set_bits("clock", 1);
  auto       g     = io->create_graph();
  const auto s     = g->get_input_pin("s");
  const auto a     = g->get_input_pin("a");
  const auto b     = g->get_input_pin("b");
  const auto konst = [&](int v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
  // EQ/And/Or/Xor: both operands go in the one "as" bank.
  const auto bin   = [&](Ntype_op op, const hhds::Pin_class& x, const hhds::Pin_class& y, int bits) {
    auto node = gu::create_typed_node(*g, op);
    gu::setup_sink_by_name(node, "as").connect_driver(x);
    gu::setup_sink_by_name(node, "as").connect_driver(y);
    gu::set_ubits(node.create_driver_pin(0), bits);
    return node.create_driver_pin(0);
  };
  // 1. a Mux decoder chain: mux(s==0, mux(s==1, ... ), a^0).
  hhds::Pin_class chain = konst(0);
  for (int i = n - 1; i >= 0; --i) {
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
    mux.create_sink_pin(0).connect_driver(bin(Ntype_op::EQ, s, konst(i), 1));
    mux.create_sink_pin(1).connect_driver(chain);
    mux.create_sink_pin(2).connect_driver(bin(Ntype_op::Xor, a, konst(i), 16));
    gu::set_ubits(mux.create_driver_pin(0), 16);
    chain = mux.create_driver_pin(0);
  }
  chain.connect_sink(g->get_output_pin("o0"));
  // 2. a chained mixed bitwise/arith expression.
  hhds::Pin_class e = a;
  for (int i = 0; i < n; ++i) {
    const auto k = bin(Ntype_op::Xor, b, konst(i & 0xFFFF), 16);
    e            = bin(i % 3 == 0 ? Ntype_op::Xor : (i % 3 == 1 ? Ntype_op::Or : Ntype_op::And), e, k, 16);
  }
  e.connect_sink(g->get_output_pin("o1"));
  // 3. a flop chain.
  hhds::Pin_class q = a;
  for (int i = 0; i < n / 4; ++i) {
    auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
    gu::setup_sink_by_name(flop, "clock_pin").connect_driver(g->get_input_pin("clock"));
    gu::setup_sink_by_name(flop, "din").connect_driver(bin(Ntype_op::Xor, q, konst(i & 0xFFFF), 16));
    gu::set_ubits(flop.create_driver_pin(0), 16);
    q = flop.create_driver_pin(0);
  }
  q.connect_sink(g->get_output_pin("o2"));
  return g;
}

Times run_lgraph_pipeline(int n) {
  setup_passes();
  Times              t;
  hhds::GraphLibrary lib;
  Eprp_var           var;
  var.add(synth_lgraph(lib, n));
  {
    Clock c(t, "pass.aegraph");
    Pass::eprp.run_method_now("pass.aegraph", var, {});
  }
  {
    Clock c(t, "pass.bitwidth");
    Pass::eprp.run_method_now("pass.bitwidth", var, {});
  }
  {
    Clock c(t, "pass.aegraph");
    Pass::eprp.run_method_now("pass.aegraph",
                              var,
                              {
                                  {"after_bitwidth", "true"}
    });
  }
  {
    Clock c(t, "inou.cgen.verilog");
    Pass::eprp.run_method_now("inou.cgen.verilog",
                              var,
                              {
                                  {"odir", fresh_dir("lg_v")}
    });
  }
  return t;
}

// Best of two at the two smaller sizes (cheap), one run at 16x.
void measure(std::string_view what, int base, const std::function<Times(int)>& run) {
  const std::vector<int> sizes{base, 4 * base, 16 * base};
  std::vector<Times>     runs;
  for (const int n : sizes) {
    Times best = run(n);
    if (n < sizes.back()) {
      const Times again = run(n);
      for (auto& [phase, ms] : best) {
        if (const auto it = again.find(phase); it != again.end()) {
          ms = std::min(ms, it->second);
        }
      }
    }
    runs.push_back(std::move(best));
  }
  check_linear(what, sizes, runs);
}

TEST(Scalability, LnastToVerilogPassesAreLinear) { measure("lnast", base_n(3200), run_lnast_pipeline); }

TEST(Scalability, LgraphPassesAreLinear) { measure("lgraph", base_n(8000), run_lgraph_pipeline); }

}  // namespace
