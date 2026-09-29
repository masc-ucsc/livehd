// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_ctrl.hpp"

#include <algorithm>
#include <array>
#include <numeric>

#include "satopt_detail.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;
constexpr size_t kDivisors    = 12;
constexpr size_t kCone        = 48;
constexpr int    kDepth       = 4;
constexpr int    kAttempts    = 320;  // support sets per control, including rejected sets
constexpr int    kRefinements = 8;

bool bit(const Pin& p) { return !p.is_invalid() && !p.is_const() && gu::bits_of(p) == 1 && gu::is_unsign(p); }
// Only Boolean logic is traversed or charged as removable gain. Comparisons,
// reductions and wide arithmetic are boundaries: their one-bit results may
// be reused, but we do not rebuild their datapaths.
bool logic(const Pin& p) {
  if (!bit(p) || gu::is_graph_input_pin(p)) {
    return false;
  }
  const auto op = gu::type_op_of(p.get_master_node());
  if (op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Not && op != Ntype_op::Mux) {
    return false;
  }
  int inputs = 0;
  for (const auto& s : p.get_master_node().inp_sorted_pins()) {
    int drivers = 0;
    for (const auto& d : s.get_driver_pins()) {
      ++drivers;
      if (d.is_const()) {
        const auto v = gu::const_of(d);
        if (v.has_unknowns() || (!v.is_known_eq(*Dlop::create_integer(0)) && !v.is_known_eq(*Dlop::create_integer(1)))) {
          return false;
        }
      } else if (!bit(d)) {
        return false;
      }
    }
    if (drivers != 1) {
      return false;
    }
    if (++inputs > 8) {
      return false;
    }
  }
  return inputs > 0 && (op != Ntype_op::Mux || inputs == 3);
}

struct Target {
  Pin              pin;
  std::vector<Pin> sinks;
};
std::vector<Target> targets(hhds::Graph& g, Meter& meter) {
  std::vector<Target>              out;
  absl::flat_hash_map<Pin, size_t> index;
  for (const auto n : g.body().nodes()) {
    if (!meter.work(1)) {
      break;
    }
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Mux && op != Ntype_op::Hotmux && op != Ntype_op::Flop && op != Ntype_op::Memory) {
      continue;
    }
    const auto hot_limit = op == Ntype_op::Hotmux ? gu::hotmux_control_end(n) : 0;
    for (const auto& s : n.inp_sorted_pins()) {
      if (!meter.work(1)) {
        return out;
      }
      const auto pid     = s.get_port_id();
      const bool control = (op == Ntype_op::Mux && pid == 0) || (op == Ntype_op::Hotmux && pid % 2 == 0 && pid < hot_limit)
                           || (op == Ntype_op::Flop && pid == 4) || (op == Ntype_op::Memory && (pid % 16 == 4 || pid == 13));
      if (!control) {
        continue;
      }
      std::vector<Pin> drivers;
      for (const auto& d : s.get_driver_pins()) {
        drivers.push_back(d);
      }
      if (drivers.size() != 1 || !logic(drivers[0])) {
        continue;
      }
      const auto d        = drivers[0];
      auto [it, inserted] = index.try_emplace(d, out.size());
      if (inserted) {
        out.push_back({d, {}});
      }
      out[it->second].sinks.push_back(s);
    }
  }
  return out;
}

// Upstream Boolean values plus a few neighboring expressions whose complete
// support is already upstream. The latter cannot depend on the target and
// cannot introduce a combinational cycle.
std::vector<Pin> divisors(const Pin& t, Meter& meter) {
  std::vector<Pin>         out, frontier{t};
  absl::flat_hash_set<Pin> seen{t};
  for (int depth = 0; depth < kDepth && !frontier.empty(); ++depth) {
    std::vector<Pin> next;
    for (const auto& p : frontier) {
      for (const auto& s : p.get_master_node().inp_sorted_pins()) {
        for (const auto& d : s.get_driver_pins()) {
          if (!meter.work(1)) {
            return {};
          }
          if (!bit(d) || !seen.insert(d).second) {
            continue;
          }
          out.push_back(d);
          if (out.size() == kDivisors) {
            return out;
          }
          if (logic(d)) {
            next.push_back(d);
          }
        }
      }
    }
    frontier = std::move(next);
  }
  seen.erase(t);
  const auto upstream = out;
  for (const auto& p : upstream) {
    int fanouts = 0;
    for (const auto& e : p.get_master_node().out_edges()) {
      if (!meter.work(1)) {
        return {};
      }
      if (++fanouts > 16 || out.size() == kDivisors) {
        break;
      }
      if (e.driver != p) {
        continue;
      }
      const auto n = e.sink.get_master_node();
      if (n == t.get_master_node()) {
        continue;
      }
      for (const auto& d : n.out_sorted_pins()) {
        if (seen.contains(d) || !logic(d)) {
          continue;
        }
        bool safe = true;
        for (const auto& s : n.inp_sorted_pins()) {
          for (const auto& in : s.get_driver_pins()) {
            safe &= in.is_const() || seen.contains(in);
          }
        }
        if (safe && out.size() < kDivisors) {
          out.push_back(d);
          seen.insert(d);
        }
      }
    }
  }
  return out;
}

// Lower bound on cells that lose all readers. Selected divisors are retained.
// No wide cell, state, Hotmux obligation or other side effect counts as gain.
size_t gain(const Pin& t, const std::vector<Pin>& keep, Meter& meter) {
  absl::flat_hash_set<Node> cone{t.get_master_node()};
  std::vector<Pin>          work{t};
  while (!work.empty() && cone.size() < kCone) {
    const auto p = work.back();
    work.pop_back();
    for (const auto& s : p.get_master_node().inp_sorted_pins()) {
      for (const auto& d : s.get_driver_pins()) {
        if (!meter.work(1)) {
          return 0;
        }
        if (std::ranges::find(keep, d) != keep.end() || !logic(d) || cone.contains(d.get_master_node())) {
          continue;
        }
        bool only = true;
        for (const auto& e : d.get_master_node().out_edges()) {
          if (!meter.work(1)) {
            return 0;
          }
          only &= cone.contains(e.sink.get_master_node());
        }
        if (only) {
          cone.insert(d.get_master_node());
          work.push_back(d);
        }
      }
    }
  }
  return cone.size();
}

// A tiny shared expression DAG. IDs 0/1 are constants, 2..4 are inputs;
// everything else costs one LGraph cell. Shannon expansion also recognizes
// AND/OR/XOR cofactors so a parity does not become a tree of muxes.
struct Expr {
  Ntype_op op;
  int      a, b, c;
  bool     operator==(const Expr&) const = default;
};
struct Plan {
  std::vector<Expr> nodes;
  int               root = 0;
  int               gate(Ntype_op op, int a, int b, int c = 0) {
    if (op != Ntype_op::Mux && a > b) {
      std::swap(a, b);
    }
    Expr e{op, a, b, c};
    auto it = std::ranges::find(nodes, e);
    if (it != nodes.end()) {
      return 5 + static_cast<int>(it - nodes.begin());
    }
    nodes.push_back(e);
    return 4 + static_cast<int>(nodes.size());
  }
  int inv(int a) { return a < 2 ? 1 - a : gate(Ntype_op::Xor, a, 1); }
  int derive(uint8_t truth, const std::vector<int>& vars) {
    const int  rows  = 1 << vars.size();
    const auto mask  = static_cast<uint8_t>((1u << rows) - 1);
    truth           &= mask;
    if (truth == 0 || truth == mask) {
      return truth != 0;
    }
    const int        v = vars.front();
    std::vector<int> rest(vars.begin() + 1, vars.end());
    uint8_t          lo = 0, hi = 0;
    for (int i = 0; i < rows / 2; ++i) {
      lo |= ((truth >> (2 * i)) & 1) << i;
      hi |= ((truth >> (2 * i + 1)) & 1) << i;
    }
    if (lo == hi) {
      return derive(lo, rest);
    }
    const int  s        = v + 2;
    const auto halfmask = static_cast<uint8_t>((1u << (rows / 2)) - 1);
    if (lo == 0 && hi == halfmask) {
      return s;
    }
    if (lo == halfmask && hi == 0) {
      return inv(s);
    }
    if (lo == 0) {
      return gate(Ntype_op::And, s, derive(hi, rest));
    }
    if (hi == 0) {
      return gate(Ntype_op::And, inv(s), derive(lo, rest));
    }
    if (lo == halfmask) {
      return gate(Ntype_op::Or, inv(s), derive(hi, rest));
    }
    if (hi == halfmask) {
      return gate(Ntype_op::Or, s, derive(lo, rest));
    }
    if (hi == (lo ^ halfmask)) {
      return gate(Ntype_op::Xor, s, derive(lo, rest));
    }
    return gate(Ntype_op::Mux, s, derive(lo, rest), derive(hi, rest));
  }
};
Plan plan(uint8_t truth, size_t size) {
  std::vector<int> order(size);
  std::iota(order.begin(), order.end(), 0);
  std::optional<Plan> best;
  do {
    uint8_t permuted = 0;
    for (int i = 0; i < (1 << size); ++i) {
      int original = 0;
      for (size_t j = 0; j < size; ++j) {
        original |= ((i >> j) & 1) << order[j];
      }
      permuted |= ((truth >> original) & 1) << i;
    }
    Plan p;
    p.root = p.derive(permuted, order);
    if (!best || p.nodes.size() < best->nodes.size()) {
      best = std::move(p);
    }
  } while (std::next_permutation(order.begin(), order.end()));
  return *best;
}
Pin build(hhds::Graph& g, const Pin& target, const std::vector<Pin>& support, const Plan& p) {
  Arm_builder      builder{g, target.get_master_node()};
  std::vector<Pin> pins{gu::create_const(g, *Dlop::create_integer(0)), gu::create_const(g, *Dlop::create_integer(1))};
  pins.insert(pins.end(), support.begin(), support.end());
  pins.resize(5);
  for (const auto& e : p.nodes) {
    auto n = builder.node(e.op);
    pins[e.a].connect_sink(gu::setup_sink_pid(n, 0));
    pins[e.b].connect_sink(gu::setup_sink_pid(n, e.op == Ntype_op::Mux ? 1 : 0));
    if (e.op == Ntype_op::Mux) {
      pins[e.c].connect_sink(gu::setup_sink_pid(n, 2));
    }
    pins.push_back(builder.output(n, 1));
  }
  return pins[p.root];
}

struct Search {
  hhds::Graph&             g;
  Stage_report&            report;
  Meter&                   meter;
  Word_sim                 sim;
  uint64_t                 sim_charged = 0;
  absl::flat_hash_set<Pin> deleted;
  int                      control_queries = 0;
  bool                     charge(uint64_t extra = 0) {
    const bool ok = meter.work(sim.work() - sim_charged + extra);
    sim_charged   = sim.work();
    return ok;
  }
  bool try_support(const Target& t, const std::vector<Pin>& support, std::optional<formal::Prover>& prover) {
    for (int retry = 0; retry < kRefinements; ++retry) {
      if (!charge()) {
        return false;
      }
      ++report.candidates;
      const auto*                           tv = sim.values(t.pin);
      std::vector<const std::vector<Dlop>*> sv;
      for (const auto& d : support) {
        sv.push_back(sim.values(d));
      }
      if (!charge(static_cast<uint64_t>(sim.columns()) * (support.size() + 1))) {
        return false;
      }
      if (tv == nullptr || std::ranges::find(sv, nullptr) != sv.end()) {
        return false;
      }
      std::array<int, 8> rows;
      rows.fill(-1);
      for (size_t j = 0; j < tv->size(); ++j) {
        int index = 0;
        for (size_t i = 0; i < sv.size(); ++i) {
          index |= (*sv[i])[j].bit_test(0) << i;
        }
        const int value = (*tv)[j].bit_test(0);
        if (rows[index] != -1 && rows[index] != value) {
          ++report.sim_rejects;  // this support cannot distinguish a required pair
          return false;
        }
        rows[index] = value;
      }
      uint8_t truth = 0;
      for (int i = 0; i < 8; ++i) {
        truth |= (rows[i] == 1) << i;  // unseen combinations propose zero; SAT checks them
      }
      const auto p      = plan(truth, support.size());
      const auto saving = gain(t.pin, support, meter);
      if (meter.exhausted() || p.nodes.size() >= saving || control_queries >= 16 || !meter.query()) {
        return false;
      }
      ++report.queries;
      ++control_queries;
      if (!prover) {
        auto opts     = prove_options(meter.budget(), false, true);
        opts.cone_max = std::min(opts.cone_max, 512);
        prover.emplace(&g, opts);
      }
      const auto before = prover->work();
      const auto result = prover->bit_function(t.pin, support, truth);
      meter.work(prover->work() - before);
      if (result.verdict == formal::Verdict::Unknown) {
        ++report.unknown;
        return false;
      }
      if (result.verdict == formal::Verdict::Refuted) {
        ++report.refuted;
        if (meter.exhausted() || result.model.empty() || !sim.add_model(result.model)) {
          return false;
        }
        continue;
      }
      ++report.proven;
      const auto replacement = build(g, t.pin, support, p);
      for (const auto& sink : t.sinks) {
        gu::drop_drivers(sink);
        replacement.connect_sink(sink);
      }
      // Always preserve obligations, including in synthesis. Only this
      // control's released Boolean cone is eligible for deletion.
      Select_rewrite rw{g, {t.pin}, true};
      rw.sweep();
      report.nodes_removed += rw.removed;
      ++report.applied;
      ++report.bits;
      // The sinks' cells now read the new cone: forget them and their fanout
      // too, or the next counterexample fails every value downstream.
      auto stale = rw.deleted_pins;
      for (const auto& sink : t.sinks) {
        for (const auto& e : sink.get_master_node().out_edges()) {
          stale.push_back(e.driver);
        }
      }
      sim.invalidate(stale, true);
      deleted.insert(rw.deleted_pins.begin(), rw.deleted_pins.end());
      return true;
    }
    return false;
  }
  void run() {
    const auto list = targets(g, meter);
    for (const auto& t : list) {
      if (!charge() || meter.exhausted()) {
        return;
      }
      if (deleted.contains(t.pin)) {
        continue;
      }
      // Do not rewrite a datapath reader or add a second implementation of
      // a control shared with one. All readers must be the selected controls.
      bool only = true;
      for (const auto& e : t.pin.get_master_node().out_edges()) {
        only &= e.driver == t.pin && std::ranges::find(t.sinks, e.sink) != t.sinks.end();
      }
      if (!only) {
        continue;
      }
      const auto divs = divisors(t.pin, meter);
      if (meter.exhausted()) {
        return;
      }
      std::optional<formal::Prover> prover;  // lazy, discarded after a rewrite
      control_queries = 0;
      int  tries      = 0;
      bool done       = try_support(t, {}, prover);
      // Smaller support first. The cap includes simulation rejects, so a
      // poorly matching control cannot enumerate a combinatorial search.
      for (size_t size = 1; size <= 3 && !done && !meter.exhausted() && control_queries < 16; ++size) {
        for (size_t i = 0; i < divs.size() && !done && tries < kAttempts && control_queries < 16; ++i) {
          for (size_t j = size > 1 ? i + 1 : i; j < divs.size() && !done && tries < kAttempts && control_queries < 16; ++j) {
            for (size_t k = size > 2 ? j + 1 : j; k < divs.size() && !done && tries < kAttempts && control_queries < 16; ++k) {
              std::vector<Pin> support{divs[i]};
              if (size > 1) {
                support.push_back(divs[j]);
              }
              if (size > 2) {
                support.push_back(divs[k]);
              }
              ++tries;
              done = try_support(t, support, prover);
              if (meter.exhausted()) {
                return;
              }
              if (size < 3) {
                break;
              }
            }
            if (size < 2) {
              break;
            }
          }
        }
      }
    }
  }
};
}  // namespace

void simplify_controls(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Stage_report& report, Meter& meter) {
  for (const auto& g : graphs) {
    if (!g || meter.exhausted()) {
      continue;
    }
    Search search{*g, report, meter, Word_sim(sim_options(meter.budget(), false, true)), 0, {}};
    search.run();
  }
  if (meter.exhausted()) {
    ++report.budget_skips;
  }
}
}  // namespace livehd::satopt
