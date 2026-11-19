// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_boundary.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <print>

#include "satopt_detail.hpp"
#include "specialize.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;
constexpr int kMaxWidth = 4096;  // wider values are left alone
constexpr int kAttempts = 3;     // masked queries per value: a counterexample drops the bits it falsified
// Per-query caps, below the budget's own: a boundary constant worth having is shallow (a mask, a tie-off, a decoded
// enable), and through instances a cone is otherwise the whole design under it. An XiangShan CtrlBlock instance input
// spent 15 minutes in one query at the budget defaults (cone_max 50000, budget_k 256).
constexpr int kConeMax  = 4096;
constexpr int kBudgetK  = 32;

int color_of(const Node& n) { return gu::has_color(n) ? gu::color_of(n) : 0; }

// A value the stage may prove bits of: the output of a combinational cell
// (wiring included: a crossing bus is usually a slice or a splice). State,
// instance and memory outputs are free words to the prover.
bool producer(const Node& n) {
  if (n.is_invalid() || gu::is_builtin_node(n)) {
    return false;
  }
  const auto op = gu::type_op_of(n);
  return Ntype::is_comb(op) && op != Ntype_op::Sub && op != Ntype_op::Clock_cell && op != Ntype_op::Memory;
}
// A reader whose pin may be handed a rebuilt value: combinational cells only
// (a clock, reset or instance pin keeps its driver).
bool reader(const Node& n) { return producer(n); }

struct Target {
  Pin                             pin;
  std::map<int, std::vector<Pin>> sinks;  // reader color -> its sink pins
};

std::vector<Target> targets_of(hhds::Graph& g, Meter& meter) {
  std::vector<Target> out;
  for (const auto n : g.body().nodes(hhds::Node_order::forward)) {
    if (!meter.work(1)) {
      break;
    }
    if (!producer(n)) {
      continue;
    }
    const int                        mine = color_of(n);
    absl::flat_hash_map<Pin, size_t> index;
    for (const auto& e : n.out_edges()) {
      if (e.driver.is_const() || gu::is_graph_output_pin(e.sink)) {
        continue;
      }
      const auto sn = e.sink.get_master_node();
      if (!reader(sn)) {
        continue;
      }
      const int theirs = color_of(sn);
      if (theirs == mine) {
        continue;
      }
      const int w = gu::bits_of(e.driver);
      if (w <= 0 || w > kMaxWidth) {
        continue;
      }
      auto [it, added] = index.try_emplace(e.driver, out.size());
      if (added) {
        out.push_back({e.driver, {}});
      }
      out[it->second].sinks[theirs].push_back(e.sink);
    }
  }
  return out;
}

struct Fact {
  size_t target = 0;
  Dlop   mask, value;  // the proven bits and what they carry
  int    bits = 0;
};

// One graph's simulator and prover, created on first use and dropped when the graph is rewritten.
struct Session {
  hhds::Graph&                  g;
  bool                          shared;
  Meter&                        meter;
  Stage_report&                 rep;
  std::optional<Word_sim>       sim;
  std::optional<formal::Prover> prover;
  uint64_t                      sim_charged = 0, solver_charged = 0;
  bool                          stop = false;  // out of budget

  Session(hhds::Graph& graph, bool sh, Meter& m, Stage_report& r) : g(graph), shared(sh), meter(m), rep(r) {}
  void fresh_sim() {
    sim.emplace(sim_options(meter.budget(), /*descend=*/true, shared));
    sim_charged = 0;
  }
  void reset() {
    sim.reset();
    prover.reset();
    sim_charged = solver_charged = 0;
  }
  // The bits of `p` (w wide) that are one constant whatever the inputs: simulation nominates them, one masked query
  // proves them, a counterexample drops what it falsified. `whole_only`: nothing short of every bit counts.
  std::optional<Fact> constant_bits(const Pin& p, int w, bool whole_only) {
    if (!sim) {
      fresh_sim();
    }
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
      const auto* v = sim->values(p);
      if (v == nullptr && sim->stored() > Word_sim::kMaxStored / 2) {
        fresh_sim();  // the graph is larger than one simulator holds: start over from this value's cone
        v = sim->values(p);
      }
      const bool afford = meter.work(sim->work() - sim_charged);
      sim_charged       = sim->work();
      if (!afford) {
        ++rep.budget_skips;
        stop = true;
        return std::nullopt;
      }
      if (v == nullptr) {
        return std::nullopt;
      }
      // Bits some column sets (any) and bits every column sets (all).
      Dlop any = *Dlop::create_integer(0);
      Dlop all = *Dlop::create_integer(-1);
      for (const auto& x : *v) {
        any = *any.or_op(x);
        all = *all.and_op(x);
      }
      Fact f;
      f.mask  = *Dlop::create_integer(0);
      f.value = *Dlop::create_integer(0);
      for (int b = 0; b < w;) {
        const bool one = all.bit_test(b);
        if (any.bit_test(b) && !one) {
          ++b;
          continue;
        }
        int hi = b;
        while (hi + 1 < w && all.bit_test(hi + 1) == one && (one || !any.bit_test(hi + 1))) {
          ++hi;
        }
        const auto run = *Dlop::get_mask_value(hi, b);
        f.mask         = *f.mask.or_op(run);
        if (one) {
          f.value = *f.value.or_op(run);
        }
        f.bits += hi - b + 1;
        b       = hi + 1;
      }
      if (f.bits == 0 || (whole_only && f.bits != w)) {
        ++rep.sim_rejects;
        return std::nullopt;
      }
      if (attempt == 0) {
        ++rep.candidates;
      }
      if (!meter.query()) {
        ++rep.budget_skips;
        stop = true;
        return std::nullopt;
      }
      ++rep.queries;
      if (!prover) {
        // Through instances (virtual flat); a callee it cannot enter is a free word, not an unprovable cone.
        auto capped             = meter.budget();
        capped.cone_max         = std::min(capped.cone_max, kConeMax);
        capped.budget_k         = std::min(capped.budget_k, kBudgetK);
        auto options            = prove_options(capped, /*descend=*/true, shared);
        options.subs_as_symbols = true;
        prover.emplace(&g, options);
      }
      const auto out = prover->masked_const(p, w, f.mask, f.value);
      meter.work(prover->work() - solver_charged);
      solver_charged = prover->work();
      if (out.verdict == formal::Verdict::Proven) {
        ++rep.proven;
        return f;
      }
      if (out.verdict != formal::Verdict::Refuted) {
        ++rep.unknown;
        return std::nullopt;
      }
      ++rep.refuted;
      if (out.model.empty() || !sim->add_model(out.model)) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }
};

Pin rebuild(hhds::Graph& g, const Pin& t, const Fact& f, int color, const Node& like, int width = 0) {
  const int  w    = width > 0 ? width : gu::bits_of(t);
  const auto node = [&](Ntype_op op) {
    auto n = gu::create_typed_node(g, op);
    if (color != 0) {
      gu::set_color(n, color);
    }
    if (gu::has_hier_color(like)) {
      gu::set_hier_color(n, gu::hier_color_of(like));
    }
    gu::carry_srcid(like, n);
    return n;
  };
  if (f.bits == w) {
    auto v = f.value;
    if (!gu::is_unsign(t)) {
      v = *v.sext_op(*Dlop::create_integer(w - 1));
    }
    return gu::create_const(g, v);
  }
  // Or(And(t, keep), value): unsigned values only (the caller filtered).
  const auto keep   = *Dlop::get_mask_value(w)->and_op(*f.mask.not_op());
  auto       masked = node(Ntype_op::And);
  t.connect_sink(gu::setup_sink_pid(masked, 0));
  gu::create_const(g, keep).connect_sink(gu::setup_sink_pid(masked, 0));
  auto out = masked.create_driver_pin(0);
  gu::set_ubits(out, w);
  if (!f.value.is_known_zero()) {
    auto joined = node(Ntype_op::Or);
    out.connect_sink(gu::setup_sink_pid(joined, 0));
    gu::create_const(g, f.value).connect_sink(gu::setup_sink_pid(joined, 0));
    out = joined.create_driver_pin(0);
    gu::set_ubits(out, w);
  }
  return out;
}
}  // namespace

bool has_colors(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  for (const auto& g : graphs) {
    if (!g) {
      continue;
    }
    for (const auto n : g->body().nodes()) {
      if (gu::has_color(n) && gu::color_of(n) != 0) {
        return true;
      }
    }
  }
  return false;
}

void prove_boundary_bits(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Profile profile, Stage_report& rep,
                         Meter& meter) {
  const bool shared = profile == Profile::shared;
  uint64_t   values = 0, rebuilt = 0;
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    if (meter.exhausted()) {
      ++rep.budget_skips;
      break;
    }
    auto&      g        = *graph;
    const auto targets  = targets_of(g, meter);
    values             += targets.size();
    if (targets.empty()) {
      continue;
    }
    Session           session(g, shared, meter, rep);
    std::vector<Fact> facts;
    for (size_t ti = 0; ti < targets.size() && !session.stop; ++ti) {
      const auto& p = targets[ti].pin;
      if (auto f = session.constant_bits(p, gu::bits_of(p), /*whole_only=*/!gu::is_unsign(p))) {
        f->target = ti;
        facts.push_back(std::move(*f));
      }
    }
    // Every rewrite keeps every value's function, so they hold together.
    session.reset();
    for (const auto& f : facts) {
      const auto& t = targets[f.target];
      for (const auto& [color, sinks] : t.sinks) {
        const auto replacement = rebuild(g, t.pin, f, color, sinks.front().get_master_node());
        for (const auto& sink : sinks) {
          gu::drop_drivers(sink);
          replacement.connect_sink(sink);
        }
        ++rebuilt;
      }
      ++rep.applied;
      rep.bits += static_cast<uint64_t>(f.bits);
    }
  }
  std::print("[pass.satopt] boundary: {} crossing value(s), {} candidate(s), {} proven ({} bit(s)), {} reader color(s) rewired\n",
             values,
             rep.candidates,
             rep.proven,
             rep.bits,
             rebuilt);
}

namespace {
// The known bits of one port: `mask` the constant ones, `value` what they carry.
struct Port_bits {
  Dlop               mask  = *Dlop::create_integer(0);
  Dlop               value = *Dlop::create_integer(0);
  [[nodiscard]] bool empty() const { return mask.is_known_zero(); }
  // Bits both agree on.
  void               meet(const Port_bits& o) {
    mask  = *mask.and_op(o.mask)->and_op(*value.xor_op(o.value)->not_op());
    value = *value.and_op(mask);
  }
  // Bits of `o` this does not have yet; merges them in.
  bool gain(const Port_bits& o) {
    const auto fresh = *o.mask.and_op(*mask.not_op());
    if (fresh.is_known_zero()) {
      return false;
    }
    mask  = *mask.or_op(o.mask);
    value = *value.or_op(*o.value.and_op(o.mask));
    return true;
  }
};

int count_bits(const Dlop& mask, int w) {
  int n = 0;
  for (int b = 0; b < w; ++b) {
    n += mask.bit_test(b);
  }
  return n;
}

// Every reader of `pin` reads Or(And(pin, keep), value) instead (a constant when every bit is known).
bool read_with(hhds::Graph& g, const Pin& pin, const Port_bits& bits, int w) {
  if (pin.is_invalid() || pin.out_edges().empty()) {
    return false;
  }
  std::vector<Pin> sinks;
  for (const auto& e : pin.out_edges()) {
    if (e.driver == pin) {
      sinks.push_back(e.sink);
    }
  }
  if (sinks.empty()) {
    return false;
  }
  Fact f;
  f.mask                 = bits.mask;
  f.value                = bits.value;
  f.bits                 = count_bits(bits.mask, w);
  // The reader's color comes from specialize::inherit_colors afterwards.
  const auto like        = gu::is_graph_input_pin(pin) ? sinks.front().get_master_node() : pin.get_master_node();
  const auto replacement = rebuild(g, pin, f, 0, like, w);
  for (const auto& sink : sinks) {  // collected before the rebuild: its own And is not among them
    sink.del_sink(pin);
    replacement.connect_sink(sink);
  }
  return true;
}
}  // namespace

void prove_instance_bits(const std::vector<std::shared_ptr<hhds::Graph>>& definitions, std::string_view top, Profile profile,
                         Stage_report& rep, Meter& meter) {
  constexpr int                                                         kRounds = 6;
  const bool                                                            shared  = profile == Profile::shared;
  absl::flat_hash_map<const hhds::Graph*, std::shared_ptr<hhds::Graph>> ours;
  for (const auto& g : definitions) {
    if (g && g->get_io()) {
      ours.emplace(g.get(), g);
    }
  }
  const auto callee_of = [&](const Node& node) -> hhds::Graph* {
    if (gu::type_op_of(node) != Ntype_op::Sub || node.subnode_loop()) {
      return nullptr;
    }
    auto callee = node.get_subnode_graph();
    return callee && ours.contains(callee.get()) ? callee.get() : nullptr;
  };
  // Children before parents.
  std::vector<hhds::Graph*>               post;
  absl::flat_hash_set<const hhds::Graph*> seen;
  std::function<void(hhds::Graph*)>       visit = [&](hhds::Graph* g) {
    if (!seen.insert(g).second) {
      return;
    }
    for (auto node : g->body().nodes()) {
      if (auto* callee = callee_of(node)) {
        visit(callee);
      }
    }
    post.push_back(g);
  };
  for (const auto& g : definitions) {
    if (g && g->get_io()) {
      visit(g.get());
    }
  }
  absl::node_hash_map<const hhds::Graph*, Session> sessions;
  const auto session_of = [&](hhds::Graph* g) -> Session& { return sessions.try_emplace(g, *g, shared, meter, rep).first->second; };
  // A graph about to be rewritten: its session goes, and the ids of the nodes it has now are returned for the
  // color inheritance of what the rewrite adds.
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<uint64_t>> existed;
  const auto                                                             touch = [&](hhds::Graph* g) {
    sessions.erase(g);
    auto [it, added] = existed.try_emplace(g);
    if (added) {
      for (auto node : g->body().nodes()) {
        it->second.insert(node.get_debug_nid());
      }
    }
  };
  bool       stop  = false;
  // The proven bits of a w-bit unsigned boundary value read from `drv` in `g`.
  const auto known = [&](hhds::Graph* g, const Pin& drv, int w) -> Port_bits {
    Port_bits out;
    if (drv.is_invalid() || w <= 0 || w > kMaxWidth || stop) {
      return out;
    }
    if (drv.is_const()) {
      const auto& c = gu::const_of(drv);
      if (c.has_unknowns() || c.is_negative()) {
        return out;
      }
      out.mask  = *Dlop::get_mask_value(w);
      out.value = *c.and_op(out.mask);
      return out;
    }
    const int dw = gu::bits_of(drv);
    if (dw <= 0 || dw > w || !gu::is_unsign(drv) || gu::is_graph_input_pin(drv)) {
      return out;  // an input of g itself is known only through g's own instances (applied inside g)
    }
    const auto op = gu::type_op_of(drv.get_master_node());
    if (op == Ntype_op::Flop || op == Ntype_op::Memory || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
      return out;
    }
    auto& s = session_of(g);
    if (auto f = s.constant_bits(drv, dw, false)) {
      out.mask  = f->mask;
      out.value = f->value;
    }
    stop |= s.stop;
    if (dw < w) {
      out.mask = *out.mask.or_op(*Dlop::get_mask_value(w - 1, dw));  // an unsigned value is 0 above its width
    }
    return out;
  };

  absl::flat_hash_map<std::pair<const hhds::Graph*, hhds::Port_id>, Port_bits>             in_applied;
  absl::flat_hash_map<std::tuple<const hhds::Graph*, hhds::Nid, hhds::Port_id>, Port_bits> out_applied;
  uint64_t                                                                                 in_bits = 0, out_bits = 0;
  int                                                                                      rounds = 0;
  for (int round = 0; round < kRounds && !stop && !meter.exhausted(); ++round) {
    bool                                                                                   any = false;
    // ---- outputs, children first: what a definition's output always carries, every instance's readers read
    absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_map<hhds::Port_id, Port_bits>> out_facts;
    const auto outputs_of = [&](hhds::Graph* callee) -> const absl::flat_hash_map<hhds::Port_id, Port_bits>& {
      auto [it, added] = out_facts.try_emplace(callee);
      if (added) {
        for (const auto& decl : callee->get_io()->get_output_pin_decls()) {
          const int w = static_cast<int>(decl.bits);
          if (w <= 0 || !decl.unsign) {
            continue;
          }
          auto bits = known(callee, callee->get_output_pin(decl.name).get_driver_pin(), w);
          if (!bits.empty()) {
            it->second.emplace(decl.port_id, std::move(bits));
          }
        }
      }
      return it->second;
    };
    for (auto* g : post) {
      std::vector<Node> subs;
      for (auto node : g->body().nodes()) {
        if (callee_of(node)) {
          subs.push_back(node);
        }
      }
      for (const auto& node : subs) {
        auto*      callee = callee_of(node);
        // copy: the rewrite below may drop sessions, never this table's rows, but keep the loop simple
        const auto facts  = outputs_of(callee);
        for (const auto& decl : callee->get_io()->get_output_pin_decls()) {
          auto f = facts.find(decl.port_id);
          if (f == facts.end()) {
            continue;
          }
          const auto out = node.get_driver_pin(decl.port_id);
          if (out.is_invalid() || out.out_edges().empty()) {
            continue;
          }
          auto& applied = out_applied[{g, node.get_debug_nid(), decl.port_id}];
          auto  merged  = applied;
          if (!merged.gain(f->second)) {
            continue;
          }
          touch(g);
          if (read_with(*g, out, merged, static_cast<int>(decl.bits))) {
            out_bits += static_cast<uint64_t>(count_bits(merged.mask, static_cast<int>(decl.bits))
                                              - count_bits(applied.mask, static_cast<int>(decl.bits)));
            applied   = merged;
            any       = true;
          }
        }
      }
    }
    // ---- inputs, parents first: the bits EVERY instance of a definition drives with one constant, the definition reads
    struct In_facts {
      bool                                          unknown = false;  // an instance the analysis cannot see
      uint64_t                                      count   = 0;
      absl::flat_hash_map<hhds::Port_id, Port_bits> bits;
    };
    absl::flat_hash_map<const hhds::Graph*, In_facts> in_facts;
    for (const auto& g : definitions) {  // a loop instance hides what it drives
      if (!g || !g->get_io()) {
        continue;
      }
      for (auto node : g->body().nodes()) {
        if (gu::type_op_of(node) == Ntype_op::Sub && node.subnode_loop()) {
          if (auto callee = node.get_subnode_graph(); callee && ours.contains(callee.get())) {
            in_facts[callee.get()].unknown = true;
          }
        }
      }
    }
    for (auto it = post.rbegin(); it != post.rend() && !stop; ++it) {
      auto* g = *it;
      // what its own instances agreed on (all of its parents came first)
      if (auto fi = in_facts.find(g); g->get_name() != top && fi != in_facts.end() && !fi->second.unknown && fi->second.count > 0) {
        for (const auto& decl : g->get_io()->get_input_pin_decls()) {
          auto f = fi->second.bits.find(decl.port_id);
          if (f == fi->second.bits.end() || f->second.empty()) {
            continue;
          }
          auto& applied = in_applied[{g, decl.port_id}];
          auto  merged  = applied;
          if (!merged.gain(f->second)) {
            continue;
          }
          touch(g);
          if (read_with(*g, g->get_input_pin(decl.name), merged, static_cast<int>(decl.bits))) {
            in_bits += static_cast<uint64_t>(count_bits(merged.mask, static_cast<int>(decl.bits))
                                             - count_bits(applied.mask, static_cast<int>(decl.bits)));
            applied  = merged;
            any      = true;
          }
        }
      }
      for (auto node : g->body().nodes()) {
        auto* callee = callee_of(node);
        if (callee == nullptr) {
          continue;
        }
        auto&                                   f = in_facts[callee];
        absl::flat_hash_map<hhds::Port_id, Pin> driver_of;
        for (const auto& sink : node.inp_sorted_pins()) {
          driver_of.emplace(sink.get_port_id(), sink.get_driver_pin());
        }
        const bool first = f.count++ == 0;
        for (const auto& decl : callee->get_io()->get_input_pin_decls()) {
          const int w = static_cast<int>(decl.bits);
          if (w <= 0 || !decl.unsign) {
            continue;
          }
          if (!first && !f.bits.contains(decl.port_id)) {
            continue;  // an earlier instance already knows nothing about it
          }
          auto      d    = driver_of.find(decl.port_id);
          Port_bits bits = d == driver_of.end() ? Port_bits{} : known(g, d->second, w);
          if (first) {
            if (!bits.empty()) {
              f.bits.emplace(decl.port_id, std::move(bits));
            }
          } else {
            auto have = f.bits.find(decl.port_id);
            have->second.meet(bits);
            if (have->second.empty()) {
              f.bits.erase(have);
            }
          }
        }
      }
    }
    if (!any) {
      break;
    }
    ++rounds;
    for (auto& [g, ids] : existed) {
      specialize::inherit_colors(*ours.at(g), &ids);
    }
    existed.clear();
  }
  rep.bits    += in_bits + out_bits;
  rep.applied += rounds;
  std::print(
      "[pass.satopt] boundary instances: {} definition input bit(s) and {} instance output bit(s) read as constants "
      "({} round(s), {} queries, {} proven)\n",
      in_bits,
      out_bits,
      rounds,
      rep.queries,
      rep.proven);
}

}  // namespace livehd::satopt
