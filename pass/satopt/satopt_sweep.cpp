// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_sweep.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <print>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "satopt_detail.hpp"
#include "satopt_salt.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;

constexpr int      kSweepCacheVersion = 4;
constexpr int      kMaxWidth          = 4096;  // wider outputs are left alone
constexpr int      kMaxTries          = 3;     // earlier values tried per output
constexpr uint64_t kMaxBucket         = 64;    // surviving representatives per signature bucket
constexpr size_t   kMaxWindow         = 16;    // cells in an observability window (G)
constexpr int      kMaxOdc            = 256;   // contextual rewrites per graph and run (G)
constexpr int      kMaxOdcWidth       = 64;    // wider outputs are left alone (G)
constexpr size_t   kMaxDivisors       = 24;    // one-bit values a resubstitution may read (J)
constexpr int      kDivisorDepth      = 3;     // fanin levels they come from (J)

// F: the operation cells whose output may be replaced.
bool target_op(Ntype_op op) { return sweep_target(op); }
}  // namespace

bool sweep_target(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum:
    case Ntype_op::Mult:
    case Ntype_op::Div:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Not:
    case Ntype_op::Ror:
    case Ntype_op::Rxor:
    case Ntype_op::Popcount:
    case Ntype_op::EQ:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::SHL:
    case Ntype_op::SRA:
    case Ntype_op::Mux:
    case Ntype_op::Hotmux: return true;
    default: return false;
  }
}

namespace {

std::string_view kind_name(Sweep k) {
  switch (k) {
    case Sweep::constants: return "constant";
    case Sweep::equiv: return "equal";
    case Sweep::complement: return "complement";
    case Sweep::odc: return "odc";
    case Sweep::resub: return "resub";
  }
  return "?";
}

// A proven fact about output 0 of `node`: it always carries `value`
// (constants; with a `mask`, only the masked bits do), or equals /
// complements the value `(rep_node, rep_pid)` drives.
struct Fact {
  uint64_t    node = 0, rep_node = 0;
  int         rep_pid = 0;
  std::string value;  // satopt_constant_key of the constant
  std::string mask;   // satopt_constant_key of the constant bits (E); empty = every bit
  // J: node := gate(rep, rep2) with gate And/Or/Xor/And-not (rep & ~rep2).
  uint64_t    rep2_node = 0;
  int         rep2_pid  = 0;
  int         gate      = 0;
};
struct Row {
  std::string       source, options;
  bool              complete = false;
  uint64_t          work = 0, queries = 0;  // what the search cost (Meter::replay)
  std::vector<Fact> facts;
};
std::map<std::string, Row> saved;
std::mutex                 saved_mutex;

Row read_row(const std::string& path) {
  Row row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != kSweepCacheVersion
      || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("options") || !doc["options"].IsString()
      || !doc.HasMember("complete") || !doc["complete"].IsBool() || !doc.HasMember("work") || !doc["work"].IsUint64()
      || !doc.HasMember("queries") || !doc["queries"].IsUint64() || !doc.HasMember("facts") || !doc["facts"].IsArray()) {
    return row;
  }
  for (const auto& f : doc["facts"].GetArray()) {
    if (!f.IsArray() || f.Size() != 8 || !f[0].IsUint64() || !f[1].IsUint64() || !f[2].IsInt() || !f[3].IsString()
        || !f[4].IsString() || !f[5].IsUint64() || !f[6].IsInt() || !f[7].IsInt()) {
      return {};
    }
    row.facts.push_back({f[0].GetUint64(),
                         f[1].GetUint64(),
                         f[2].GetInt(),
                         f[3].GetString(),
                         f[4].GetString(),
                         f[5].GetUint64(),
                         f[6].GetInt(),
                         f[7].GetInt()});
  }
  row.source   = doc["source"].GetString();
  row.options  = doc["options"].GetString();
  row.complete = doc["complete"].GetBool();
  row.work     = doc["work"].GetUint64();
  row.queries  = doc["queries"].GetUint64();
  return row;
}
void write_row(const std::string& path, const Row& row) {
  if (path.empty()) {
    return;
  }
  auto text = std::format(R"({{"version":{},"source":"{}","options":"{}","complete":{},"work":{},"queries":{},"facts":[)",
                          kSweepCacheVersion,
                          json_util::escape(row.source),
                          json_util::escape(row.options),
                          row.complete ? "true" : "false",
                          row.work,
                          row.queries);
  for (size_t i = 0; i < row.facts.size(); ++i) {
    const auto& f  = row.facts[i];
    text          += std::format(R"({}[{},{},{},"{}","{}",{},{},{}])",
                        i ? "," : "",
                        f.node,
                        f.rep_node,
                        f.rep_pid,
                        f.value,
                        f.mask,
                        f.rep2_node,
                        f.rep2_pid,
                        f.gate);
  }
  write_atomic(path, text + "]}\n");
}

Dlop constant_of(const std::string& key) {
  std::string bytes;
  for (size_t i = 0; i + 1 < key.size(); i += 2) {
    bytes += static_cast<char>(std::stoi(key.substr(i, 2), nullptr, 16));
  }
  return *Dlop::unserialize(bytes);
}

// The low `width` bits of v, complemented when `flip`.
Dlop low_bits(const Dlop& v, int width, bool flip) {
  const auto mask = Dlop::get_mask_value(width);
  return flip ? *v.xor_op(*mask)->and_op(*mask) : *v.and_op(*mask);
}
uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ULL + (h >> 17); }
uint64_t word_hash(const Dlop& v) {
  const auto bytes = v.serialize();
  uint64_t   h     = 0xcbf29ce484222325ULL;
  for (unsigned char c : bytes) {
    h = (h ^ c) * 0x100000001b3ULL;
  }
  return h;
}

// The cells deleted if `t` lost its consumers (a lower bound, at most `cap`).
absl::flat_hash_set<Node> mffc_set(const Node& t, int cap);
int                       mffc(const Node& t, int cap) { return static_cast<int>(mffc_set(t, cap).size()); }
absl::flat_hash_set<Node> mffc_set(const Node& t, int cap) {
  absl::flat_hash_set<Node> in{t};
  std::vector<Node>         work{t};
  while (!work.empty() && static_cast<int>(in.size()) < cap) {
    const auto n = work.back();
    work.pop_back();
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        if (d.is_const() || gu::is_graph_input_pin(d)) {
          continue;
        }
        const auto m = d.get_master_node();
        if (in.contains(m) || !Ntype::is_comb(gu::type_op_of(m))) {
          continue;
        }
        bool only = true;
        for (const auto& e : m.out_edges()) {
          only &= in.contains(e.sink.get_master_node());
        }
        if (only) {
          in.insert(m);
          work.push_back(m);
        }
      }
    }
  }
  return in;
}

// J: the gate a resubstitution builds, one bit per column.
enum Gate : int { gate_and = 1, gate_or = 2, gate_xor = 3, gate_andnot = 4 };
bool gate_bit(int gate, bool a, bool b) {
  switch (gate) {
    case gate_and: return a && b;
    case gate_or: return a || b;
    case gate_xor: return a != b;
    default: return a && !b;
  }
}
// The gate's cells next to `near` (color and source location carried), its
// one-bit output, and the cells it created.
Pin build_gate(hhds::Graph& g, const Node& near, int gate, const Pin& a, const Pin& b, std::vector<Node>& made) {
  Arm_builder build{g, near};
  auto        rhs = b;
  if (gate == gate_andnot) {
    auto inv = build.node(Ntype_op::Xor);
    b.connect_sink(gu::setup_sink_pid(inv, 0));
    gu::create_const(g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(inv, 0));
    rhs = build.output(inv, 1);
    made.push_back(inv);
  }
  auto n = build.node(gate == gate_or ? Ntype_op::Or : gate == gate_xor ? Ntype_op::Xor : Ntype_op::And);
  a.connect_sink(gu::setup_sink_pid(n, 0));
  rhs.connect_sink(gu::setup_sink_pid(n, 0));
  made.push_back(n);
  return build.output(n, 1);
}
// One-bit unsigned values in t's fanin window (kDivisorDepth levels, at most
// kMaxDivisors) outside t's private cone: what a replacement may read.
std::vector<Pin> divisors_of(const Pin& t, const absl::flat_hash_set<Node>& cone) {
  std::vector<Pin>          out;
  absl::flat_hash_set<Pin>  seen;
  std::vector<Node>         frontier{t.get_master_node()};
  absl::flat_hash_set<Node> walked{t.get_master_node()};
  for (int level = 0; level < kDivisorDepth && !frontier.empty() && out.size() < kMaxDivisors; ++level) {
    std::vector<Node> next;
    for (const auto& n : frontier) {
      for (const auto& in_pin : n.inp_sorted_pins()) {
        for (const auto& d : in_pin.get_driver_pins()) {
          if (d.is_const() || !seen.insert(d).second) {
            continue;
          }
          const bool leaf = gu::is_graph_input_pin(d) || cut(d);
          if (gu::bits_of(d) == 1 && gu::is_unsign(d) && (leaf || !cone.contains(d.get_master_node()))
              && out.size() < kMaxDivisors) {
            out.push_back(d);
          }
          if (!leaf && walked.insert(d.get_master_node()).second) {
            next.push_back(d.get_master_node());
          }
        }
      }
    }
    frontier = std::move(next);
  }
  return out;
}

// The latch contract keeps a latch's hold mux and its Q a direct arm of it
// (satopt_mux.cpp): never replace a value a latch reads or that reads a latch
// Q through a mux arm, and never read a latch Q in a replacement.
bool latch_bound(const Pin& p) {
  const auto n = p.get_master_node();
  for (const auto& e : n.out_edges()) {
    if (gu::type_op_of(e.sink.get_master_node()) == Ntype_op::Latch) {
      return true;
    }
  }
  if (const auto op = gu::type_op_of(n); op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        if (!d.is_const() && !gu::is_graph_input_pin(d) && gu::type_op_of(d.get_master_node()) == Ntype_op::Latch) {
          return true;
        }
      }
    }
  }
  return false;
}
// Every consumer only rewires bits: rebuilding the value from its constant
// bits gains nothing (and a rebuilt value's consumers are exactly such
// slices, which keeps the sweep idempotent).
bool wiring_only(const Pin& p) {
  bool any = false;
  for (const auto& e : p.get_master_node().out_edges()) {
    if (e.driver != p) {
      continue;
    }
    any           = true;
    const auto op = gu::type_op_of(e.sink.get_master_node());
    if (op != Ntype_op::Get_mask && op != Ntype_op::Concat && op != Ntype_op::Sext && op != Ntype_op::Set_mask) {
      return false;
    }
  }
  return any;
}
// Maximal runs of bits: kind -1 = live, 0/1 = the constant bit.
struct Run {
  int lo, hi, kind;
};
std::vector<Run> runs_of(int width, const Dlop& mask, const Dlop& value) {
  std::vector<Run> runs;
  for (int b = 0; b < width; ++b) {
    const int kind = mask.bit_test(b) ? (value.bit_test(b) ? 1 : 0) : -1;
    if (!runs.empty() && runs.back().kind == kind) {
      runs.back().hi = b + 1;
    } else {
      runs.push_back({b, b + 1, kind});
    }
  }
  return runs;
}
bool is_latch_q(const Pin& p) {
  return !p.is_const() && !gu::is_graph_input_pin(p) && gu::type_op_of(p.get_master_node()) == Ntype_op::Latch;
}

// Every live value in topological order (graph inputs first): the order the
// earlier-value rule and the simulation use.
std::vector<Pin> live_values(hhds::Graph& g) {
  std::vector<Pin>         out;
  absl::flat_hash_set<Pin> seen;
  for (const auto& d : g.get_io()->get_input_pin_decls()) {
    const auto p = g.get_input_pin(d.name);
    if (!p.is_invalid() && seen.insert(p).second) {
      out.push_back(p);
    }
  }
  for (const auto n : g.body().nodes(hhds::Node_order::forward)) {
    std::vector<Pin> pins;
    for (const auto& e : n.out_edges()) {
      if (seen.insert(e.driver).second) {
        pins.push_back(e.driver);
      }
    }
    std::sort(pins.begin(), pins.end(), [](const Pin& a, const Pin& b) { return a.get_port_id() < b.get_port_id(); });
    out.insert(out.end(), pins.begin(), pins.end());
  }
  return out;
}

// G: the cells `depth` levels into t's fanout (at most kMaxWindow), in
// topological order, and the exits: every window output read outside it
// (truncation included) and every window output that drives a Hotmux control
// inside it. nullopt when t itself is read by anything but a combinational
// cell, or is a Hotmux control.
struct Window {
  std::vector<Node> nodes;
  std::vector<Pin>  exits;
};
bool window_cell(const Node& n) {
  const auto op = gu::type_op_of(n);
  return !gu::is_builtin_node(n) && Ntype::is_comb(op) && op != Ntype_op::Clock_cell && op != Ntype_op::Sub;
}
std::optional<Window> window_of(const Pin& t, int depth) {
  absl::flat_hash_set<Node> in;
  std::vector<Pin>          frontier{t};
  for (int level = 0; level < depth && !frontier.empty(); ++level) {
    std::vector<Pin> next;
    for (const auto& p : frontier) {
      for (const auto& e : p.get_master_node().out_edges()) {
        if (e.driver != p) {
          continue;
        }
        const auto c = e.sink.get_master_node();
        if (p == t && gu::type_op_of(c) == Ntype_op::Hotmux) {
          // t itself is a Hotmux control: the shared profile's `unique if`
          // obligation reads it, and synthesis lowers a Hotmux as an AND-OR
          // cover that needs its controls exclusive, while the proof reads
          // priority -- raising a control could make them overlap.
          for (const auto& [control, value] : gu::hotmux_inputs(c).arms) {
            (void)value;
            if (control == t) {
              return std::nullopt;
            }
          }
        }
        if (in.contains(c)) {
          continue;
        }
        if (!window_cell(c) || in.size() >= kMaxWindow) {
          if (p == t) {
            return std::nullopt;
          }
          continue;  // p becomes an exit below
        }
        in.insert(c);
        for (const auto& ce : c.out_edges()) {
          next.push_back(ce.driver);
        }
      }
    }
    frontier = std::move(next);
  }
  if (in.empty()) {
    return std::nullopt;
  }
  Window w;
  // Topological order inside the window (Kahn over its own edges).
  absl::flat_hash_map<Node, int> pending;
  for (const auto& n : in) {
    int count = 0;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        count += !d.is_const() && !gu::is_graph_input_pin(d) && in.contains(d.get_master_node());
      }
    }
    pending[n] = count;
  }
  std::vector<Node> ready;
  for (const auto& [n, c] : pending) {
    if (c == 0) {
      ready.push_back(n);
    }
  }
  std::sort(ready.begin(), ready.end(), [](const Node& a, const Node& b) { return a.get_debug_nid() > b.get_debug_nid(); });
  while (!ready.empty()) {
    const auto n = ready.back();
    ready.pop_back();
    w.nodes.push_back(n);
    for (const auto& e : n.out_edges()) {
      const auto c = e.sink.get_master_node();
      if (in.contains(c) && --pending[c] == 0) {
        ready.push_back(c);
      }
    }
  }
  if (w.nodes.size() != in.size()) {
    return std::nullopt;  // a loop inside the window
  }
  absl::flat_hash_set<Pin> exits;
  for (const auto& n : w.nodes) {
    for (const auto& e : n.out_edges()) {
      if (!in.contains(e.sink.get_master_node()) && exits.insert(e.driver).second) {
        w.exits.push_back(e.driver);
      }
    }
    if (gu::type_op_of(n) == Ntype_op::Hotmux) {
      // The controls must not change: the `unique if` obligation reads them
      // (shared), and the synthesis AND-OR cover needs them exclusive.
      for (const auto& [control, value] : gu::hotmux_inputs(n).arms) {
        (void)value;
        if (!control.is_const() && !gu::is_graph_input_pin(control) && in.contains(control.get_master_node())
            && exits.insert(control).second) {
          w.exits.push_back(control);
        }
      }
    }
  }
  if (w.exits.empty()) {
    return std::nullopt;  // nothing observes the window: dead logic, not ours
  }
  return w;
}

// Applies one odc fact: t's consumers read the constant, or t's low bits.
// The deleted cells and their driver pins are appended.
bool apply_odc(hhds::Graph& g, const Node& node, const Fact& f, bool shared, Stage_report& rep, std::vector<Node>& deleted,
               std::vector<Pin>& deleted_pins) {
  const auto t    = node.create_driver_pin(0);
  const int  w    = gu::bits_of(t);
  const auto mask = constant_of(f.mask);
  auto       v    = constant_of(f.value);
  if (w <= 0) {
    return false;
  }
  std::vector<Pin> sinks;
  for (const auto& e : node.out_edges()) {
    if (e.driver == t) {
      sinks.push_back(e.sink);
    }
  }
  Select_rewrite rw{g, {}, shared};
  Pin            replacement;
  if (mask.is_known_eq(*Dlop::get_mask_value(w))) {
    // The whole value: the constant, read with t's sign.
    if (!gu::is_unsign(t)) {
      v = *v.sext_op(*Dlop::create_integer(w - 1));
    }
    replacement = gu::create_const(g, v);
    rw.released.push_back(t);
  } else {
    const auto runs = runs_of(w, mask, v);
    if (!gu::is_unsign(t) || runs.size() != 2 || runs[0].kind >= 0 || runs[1].kind != 0) {
      return false;
    }
    Arm_builder build{g, node};
    replacement = build.slice(t, 0, runs[1].lo);
  }
  for (const auto& sink : sinks) {
    gu::drop_drivers(sink);
    replacement.connect_sink(sink);
  }
  rw.sweep();
  rep.nodes_removed += rw.removed;
  deleted.insert(deleted.end(), rw.deleted_nodes.begin(), rw.deleted_nodes.end());
  deleted_pins.insert(deleted_pins.end(), rw.deleted_pins.begin(), rw.deleted_pins.end());
  for (int b = 0; b < w; ++b) {
    rep.bits += mask.bit_test(b);
  }
  return true;
}

// J: t's consumers read `n` (proven equal); t's private cone is swept, its
// cells' pins appended to `stale` and the cells themselves to `deleted`.
void rewire_resub(hhds::Graph& g, const Pin& t, const Pin& n, bool shared, Stage_report& rep, std::vector<Pin>& stale,
                  std::vector<Node>* deleted = nullptr) {
  std::vector<Pin> sinks;
  for (const auto& e : t.get_master_node().out_edges()) {
    if (e.driver == t && e.sink.get_master_node() != n.get_master_node()) {
      sinks.push_back(e.sink);
    }
  }
  for (const auto& sink : sinks) {
    gu::drop_drivers(sink);
    n.connect_sink(sink);
  }
  Select_rewrite rw{g, {t}, shared};
  rw.sweep();
  rep.nodes_removed += rw.removed;
  stale.insert(stale.end(), rw.deleted_pins.begin(), rw.deleted_pins.end());
  if (deleted != nullptr) {
    deleted->insert(deleted->end(), rw.deleted_nodes.begin(), rw.deleted_nodes.end());
  }
}

struct Search {
  hhds::Graph&  g;
  Sweep         kind;
  bool          shared;
  Meter&        m;
  Stage_report& rep;
  Row&          row;
  Word_sim      sim;
  std::optional<formal::Prover> prover;
  uint64_t      sim_charged = 0, solver_charged = 0;

  Search(hhds::Graph& graph, Sweep k, bool sh, Meter& meter, Stage_report& report, Row& r)
      : g(graph), kind(k), shared(sh), m(meter), rep(report), row(r), sim(sim_options(meter.budget(), false, sh)) {}

  formal::Prover& solver() {
    // Most signatures reject without a query. ODC/resub have their own
    // mutation-scoped prover, so do not create unused primary-input terms.
    if (!prover) {
      prover.emplace(&g, prove_options(m.budget(), false, shared));
    }
    return *prover;
  }

  bool charge_sim() {
    const bool ok = m.work(sim.work() - sim_charged);
    sim_charged   = sim.work();
    return ok;
  }
  bool afford_work(uint64_t units) {
    if (m.work(units)) {
      return true;
    }
    ++rep.budget_skips;
    row.complete = false;
    return false;
  }
  void charge_solver() {
    m.work(prover->work() - solver_charged);
    solver_charged = prover->work();
  }
  // Out of budget: the rest of the search is skipped and the row is partial.
  bool afford_query() {
    if (m.query()) {
      ++rep.queries;
      return true;
    }
    ++rep.budget_skips;
    row.complete = false;
    return false;
  }
  void refuted(const formal::Query_out& out) {
    ++rep.refuted;
    if (!out.model.empty() && sim.add_model(out.model)) {
      charge_sim();
    }
  }

  bool target(const Pin& p) const {
    if (p.is_const() || gu::is_graph_input_pin(p) || p.get_port_id() != 0) {
      return false;
    }
    const int w = gu::bits_of(p);
    return target_op(gu::type_op_of(p.get_master_node())) && w > 0 && w <= kMaxWidth && !latch_bound(p);
  }
  static bool constant(const std::vector<Dlop>& v) {
    return std::all_of(v.begin(), v.end(), [&](const Dlop& x) { return x.is_known_eq(v.front()); });
  }

  void constants(const std::vector<Pin>& values) {
    for (const auto& p : values) {
      if (!target(p)) {
        continue;
      }
      const auto* v = sim.values(p);
      if (!charge_sim()) {
        ++rep.budget_skips;
        row.complete = false;
        return;
      }
      if (v == nullptr) {
        continue;
      }
      if (!constant(*v)) {
        constant_bits(p);
        continue;
      }
      ++rep.candidates;
      const Dlop value = v->front();
      if (!afford_query()) {
        return;
      }
      const int  w   = gu::bits_of(p);
      const auto out = solver().masked_const(p, w, *Dlop::get_mask_value(w), low_bits(value, w, false));
      charge_solver();
      if (out.verdict == formal::Verdict::Proven) {
        ++rep.proven;
        row.facts.push_back({static_cast<uint64_t>(p.get_master_node().get_debug_nid()), 0, 0, satopt_constant_key(value), {}});
      } else if (out.verdict == formal::Verdict::Refuted) {
        refuted(out);
      } else {
        ++rep.unknown;
      }
    }
  }

  // E: an unsigned value whose top bits every column leaves 0, proven with
  // one masked query (a counterexample drops the bits it falsified and the
  // rest are tried again); its consumers then read the narrower slice. Other
  // constant runs are left alone: rebuilding a value from lanes hides the
  // arithmetic a consumer recognizes (an affine shift amount `(i << 5) + 32`
  // keeps its word select only while it stays one Sum) -- measured, it grew
  // a 4:1 word select 4x.
  void constant_bits(const Pin& p) {
    const int w = gu::bits_of(p);
    if (w < 2 || !gu::is_unsign(p) || wiring_only(p)) {
      return;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto* v = sim.values(p);
      if (v == nullptr) {
        return;
      }
      // These samples are already fitted unsigned words. Only the zero top
      // run is used, so one OR reduction replaces per-sample masks, inversions
      // and an unused all-ones intersection.
      Dlop observed = *Dlop::create_integer(0);
      for (const auto& x : *v) {
        observed = *observed.or_op(x);
      }
      if (!afford_work(v->size())) {
        return;
      }
      // Only the zero run at the top.
      int top = w;
      while (top > 0 && !observed.bit_test(top - 1)) {
        --top;
      }
      if (top == w || top == 0) {
        return;  // no zero top, or a whole constant (the word sweep's)
      }
      const auto mask = *Dlop::get_mask_value(w - 1, top);
      const auto zero = *Dlop::create_integer(0);
      ++rep.candidates;
      if (!afford_query()) {
        return;
      }
      const auto out = solver().masked_const(p, w, mask, zero);
      charge_solver();
      if (out.verdict == formal::Verdict::Proven) {
        ++rep.proven;
        row.facts.push_back({static_cast<uint64_t>(p.get_master_node().get_debug_nid()),
                             0,
                             0,
                             satopt_constant_key(zero),
                             satopt_constant_key(mask)});
        return;
      }
      if (out.verdict != formal::Verdict::Refuted) {
        ++rep.unknown;
        return;
      }
      refuted(out);
    }
  }

  // G: contextual constants, one proven rewrite at a time.
  void odc(const std::vector<Pin>& values) {
    std::optional<formal::Prover> local;
    uint64_t                      local_charged = 0;
    const auto                    fresh         = [&]() -> formal::Prover& {
      if (!local) {
        local.emplace(&g, prove_options(m.budget(), false, shared));
        local_charged = 0;
      }
      return *local;
    };
    absl::flat_hash_set<Pin> dead;  // pins of deleted cells: never touched again
    int                      rewrites = 0;
    for (const auto& t : values) {
      if (rewrites >= kMaxOdc) {
        return;
      }
      if (dead.contains(t) || t.is_const() || gu::is_graph_input_pin(t) || !target(t)) {
        continue;
      }
      const int w = gu::bits_of(t);
      if (w > kMaxOdcWidth) {
        continue;
      }
      const auto* tv = sim.values(t);
      if (!charge_sim()) {
        ++rep.budget_skips;
        row.complete = false;
        return;
      }
      if (tv == nullptr || constant(*tv)) {
        continue;  // a constant is the constants sweep's
      }
      const auto all  = *Dlop::get_mask_value(w);
      bool       done = false;
      for (int depth = 1; depth <= 2 && !done; ++depth) {
        const auto win = window_of(t, depth);
        if (!win) {
          break;
        }
        if (!m.work(win->nodes.size())) {
          ++rep.budget_skips;
          row.complete = false;
          return;
        }
        std::vector<std::pair<Dlop, Dlop>> tries{{all, *Dlop::create_integer(0)}, {all, all}};
        if (gu::is_unsign(t) && !wiring_only(t)) {
          // The widest top run no column observes, grown one bit at a time.
          int top = w;
          while (top > 1) {
            const auto ok
                = sim.unchanged_under(t, *Dlop::get_mask_value(w - 1, top - 1), *Dlop::create_integer(0), win->nodes, win->exits);
            if (!charge_sim()) {
              afford_work(0);
              return;
            }
            if (!ok || !*ok) {
              break;
            }
            --top;
          }
          if (top < w) {
            tries.emplace_back(*Dlop::get_mask_value(w - 1, top), *Dlop::create_integer(0));
          }
        }
        for (const auto& [mask, value] : tries) {
          const auto ok = sim.unchanged_under(t, mask, value, win->nodes, win->exits);
          if (!charge_sim()) {
            afford_work(0);
            return;
          }
          if (!ok) {
            continue;
          }
          if (!*ok) {
            ++rep.sim_rejects;
            continue;
          }
          ++rep.candidates;
          if (!afford_query()) {
            return;
          }
          auto&      prover_now = fresh();
          const auto out        = prover_now.unchanged_under(t, w, mask, value, win->nodes, win->exits);
          m.work(prover_now.work() - local_charged);
          local_charged = prover_now.work();
          if (out.verdict == formal::Verdict::Proven) {
            ++rep.proven;
            Fact f{static_cast<uint64_t>(t.get_master_node().get_debug_nid()), 0, 0, satopt_constant_key(value), satopt_constant_key(mask)};
            // The window's outputs change (its exits do not): collected
            // now, while every cell still exists.
            std::vector<Pin> stale{t};
            for (const auto& n : win->nodes) {
              for (const auto& e : n.out_edges()) {
                stale.push_back(e.driver);
              }
            }
            std::vector<Node> deleted;
            if (apply_odc(g, t.get_master_node(), f, shared, rep, deleted, stale)) {
              row.facts.push_back(f);
              ++rep.applied;
              ++rewrites;
              // Forget those values and every deleted cell's; encode afresh.
              sim.invalidate(stale);
              dead.insert(stale.begin(), stale.end());
              local.reset();
            }
            done = true;
            break;
          }
          if (out.verdict == formal::Verdict::Refuted) {
            refuted(out);
            continue;
          }
          ++rep.unknown;
        }
      }
    }
  }

  // J: one-bit outputs re-expressed as one gate over two divisors.
  void resub(const std::vector<Pin>& values) {
    // Gates this search built: never divisors of a later rewrite, so every
    // fact names cells a replay of the row finds.
    absl::flat_hash_set<Node> made_here;
    absl::flat_hash_set<Pin>  dead;
    int                      rewrites = 0;
    std::optional<formal::Prover> local;
    uint64_t                      local_charged = 0;
    for (const auto& t : values) {
      if (rewrites >= kMaxOdc) {
        return;
      }
      if (dead.contains(t) || t.is_const() || gu::is_graph_input_pin(t) || !target(t) || gu::bits_of(t) != 1
          || !gu::is_unsign(t)) {
        continue;
      }
      const auto cone = mffc_set(t.get_master_node(), 64);
      if (cone.size() < 2) {
        continue;  // one cell: a gate saves nothing
      }
      const auto* tv = sim.values(t);
      if (!charge_sim()) {
        ++rep.budget_skips;
        row.complete = false;
        return;
      }
      if (tv == nullptr || constant(*tv)) {
        continue;
      }
      auto divs = divisors_of(t, cone);
      std::erase_if(divs, [&](const Pin& d) { return !gu::is_graph_input_pin(d) && made_here.contains(d.get_master_node()); });
      if (!m.work(divs.size())) {
        ++rep.budget_skips;
        row.complete = false;
        return;
      }
      std::vector<const std::vector<Dlop>*> dv;
      for (const auto& d : divs) {
        dv.push_back(sim.values(d));
      }
      if (!charge_sim()) {
        afford_work(0);
        return;
      }
      bool done = false;
      for (size_t i = 0; i < divs.size() && !done; ++i) {
        for (size_t k = 0; k < divs.size() && !done; ++k) {
          if (i == k || dv[i] == nullptr || dv[k] == nullptr || dv[i]->size() != tv->size() || dv[k]->size() != tv->size()) {
            continue;
          }
          for (int gate = gate_and; gate <= gate_andnot && !done; ++gate) {
            if (gate != gate_andnot && k < i) {
              continue;  // symmetric gates: each pair once
            }
            if (static_cast<int>(cone.size()) <= (gate == gate_andnot ? 2 : 1)) {
              continue;  // not cheaper than the cone it replaces
            }
            bool match = true;
            for (size_t j = 0; j < tv->size() && match; ++j) {
              match = (*tv)[j].bit_test(0) == gate_bit(gate, (*dv[i])[j].bit_test(0), (*dv[k])[j].bit_test(0));
            }
            if (!afford_work(tv->size())) {
              return;
            }
            if (!match) {
              continue;
            }
            ++rep.candidates;
            if (!afford_query()) {
              return;
            }
            // Proven from the pins alone, before any cell is built: a refuted
            // candidate leaves the graph (and its node ids) untouched.
            if (!local) {
              local.emplace(&g, prove_options(m.budget(), false, shared));
              local_charged = 0;
            }
            const auto cell = gate == gate_or ? Ntype_op::Or : gate == gate_xor ? Ntype_op::Xor : Ntype_op::And;
            const auto out  = local->bit_gate(t, cell, divs[i], divs[k], gate == gate_andnot);
            m.work(local->work() - local_charged);
            local_charged = local->work();
            if (out.verdict != formal::Verdict::Proven) {
              if (out.verdict == formal::Verdict::Refuted) {
                refuted(out);
              } else {
                ++rep.unknown;
              }
              continue;
            }
            ++rep.proven;
            std::vector<Node> made;
            const auto        n = build_gate(g, t.get_master_node(), gate, divs[i], divs[k], made);
            made_here.insert(made.begin(), made.end());
            local.reset();  // the rewrite deletes cells: encode afresh
            Fact f{static_cast<uint64_t>(t.get_master_node().get_debug_nid()),
                   static_cast<uint64_t>(divs[i].get_master_node().get_debug_nid()),
                   static_cast<int>(divs[i].get_port_id()),
                   {},
                   {},
                   static_cast<uint64_t>(divs[k].get_master_node().get_debug_nid()),
                   static_cast<int>(divs[k].get_port_id()),
                   gate};
            std::vector<Pin> stale{t};
            rewire_resub(g, t, n, shared, rep, stale);
            sim.invalidate(stale);
            dead.insert(stale.begin(), stale.end());
            row.facts.push_back(f);
            ++rep.applied;
            ++rewrites;
            done = true;
          }
        }
      }
    }
  }

  void relations(const std::vector<Pin>& values) {
    const bool complement = kind == Sweep::complement;
    // Signature buckets: (width, sign, hash of the canonical columns). The
    // complement sweep canonicalizes each word so bit 0 of column 0 is 0 and
    // pairs opposite polarities; the equal sweep pairs identical words.
    struct Member {
      Pin  pin;
      bool flip = false;
    };
    absl::flat_hash_map<std::tuple<int, bool, uint64_t>, std::vector<Member>> buckets;
    for (const auto& p : values) {
      if (p.is_const()) {
        continue;
      }
      const int w = gu::bits_of(p);
      if (w <= 0 || w > kMaxWidth || (shared && is_latch_q(p))) {
        continue;
      }
      const auto* v = sim.values(p);
      if (!charge_sim()) {
        ++rep.budget_skips;
        row.complete = false;
        return;
      }
      if (v == nullptr || constant(*v)) {
        continue;  // constants are the constants sweep's; they never pair here
      }
      const bool flip = complement && v->front().bit_test(0);
      uint64_t   h    = 0;
      // Keep the index stable as counterexamples append columns. The full
      // signature, including those columns, is checked before each proof.
      for (uint32_t j = 0; j < m.budget().samples; ++j) {
        h = mix(h, word_hash(low_bits((*v)[j], w, flip)));
      }
      if (!afford_work(m.budget().samples)) {
        return;
      }
      auto& members  = buckets[{w, gu::is_unsign(p), h}];
      bool  replaced = false;
      if (target(p) && (!complement || mffc(p.get_master_node(), 2) >= 2)) {
        int tries = 0;
        for (size_t r = 0; r < members.size() && tries < kMaxTries; ++r) {
          const auto& cand = members[r];
          if (complement && cand.flip == flip) {
            continue;
          }
          // Exact columns (a hash match nominates, it proves nothing), with
          // every counterexample column added so far.
          const auto* tv = sim.values(p);
          const auto* rv = sim.values(cand.pin);
          if (tv == nullptr || rv == nullptr || tv->size() != rv->size()) {
            continue;
          }
          bool same = true;
          for (size_t j = 0; j < tv->size() && same; ++j) {
            same = low_bits((*tv)[j], w, false).is_known_eq(low_bits((*rv)[j], w, complement));
          }
          if (!afford_work(tv->size())) {
            return;
          }
          if (!same) {
            ++rep.sim_rejects;
            continue;
          }
          ++rep.candidates;
          ++tries;
          if (!afford_query()) {
            return;
          }
          const auto out = solver().masked_relation(p, cand.pin, w, *Dlop::get_mask_value(w), complement);
          charge_solver();
          if (out.verdict == formal::Verdict::Proven) {
            ++rep.proven;
            replaced = true;
            row.facts.push_back({static_cast<uint64_t>(p.get_master_node().get_debug_nid()),
                                 static_cast<uint64_t>(cand.pin.get_master_node().get_debug_nid()),
                                 static_cast<int>(cand.pin.get_port_id()),
                                 {},
                                 {}});
            break;
          }
          if (out.verdict == formal::Verdict::Refuted) {
            refuted(out);
            continue;
          }
          ++rep.unknown;
          break;  // Unknown proves nothing: the next value is not cheaper to ask
        }
      }
      // Bound alternative representatives, not the number of targets. A
      // large equivalence class can all read its first surviving value.
      if (!replaced && members.size() < kMaxBucket) {
        members.push_back({p, flip});
      }
    }
  }
};

// Applies the proven facts; returns how many outputs were replaced.
uint64_t apply(hhds::Graph& g, Sweep kind, const std::vector<Fact>& facts, bool shared, Stage_report& rep) {
  absl::flat_hash_map<uint64_t, Node> nodes;
  for (const auto n : g.body().nodes()) {
    nodes.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
  }
  const auto pin_of = [&](uint64_t nid, int pid) -> Pin {
    for (const auto& d : g.get_io()->get_input_pin_decls()) {
      const auto p = g.get_input_pin(d.name);
      if (!p.is_invalid() && static_cast<uint64_t>(p.get_master_node().get_debug_nid()) == nid && static_cast<int>(p.get_port_id()) == pid) {
        return p;
      }
    }
    const auto it = nodes.find(nid);
    return it == nodes.end() ? Pin{} : it->second.create_driver_pin(static_cast<hhds::Port_id>(pid));
  };
  Select_rewrite rw{g, {}, shared};
  uint64_t       applied = 0;
  // Whole values first, swept: a value whose constant bits were proven may
  // have lost its reader to a whole-value rewrite, and then needs no rebuild.
  std::vector<const Fact*> order;
  for (const auto& f : facts) {
    if (f.mask.empty()) {
      order.push_back(&f);
    }
  }
  const size_t whole = order.size();
  for (const auto& f : facts) {
    if (!f.mask.empty()) {
      order.push_back(&f);
    }
  }
  for (size_t i = 0; i < order.size(); ++i) {
    const auto& f = *order[i];
    if (i == whole && whole != 0) {
      rw.sweep();
      nodes.clear();
      for (const auto n : g.body().nodes()) {
        nodes.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
      }
    }
    const auto it = nodes.find(f.node);
    if (it == nodes.end()) {
      continue;
    }
    const auto t = it->second.create_driver_pin(0);
    const int  w = gu::bits_of(t);
    std::vector<Pin> sinks;
    for (const auto& e : t.get_master_node().out_edges()) {
      if (e.driver == t) {
        sinks.push_back(e.sink);
      }
    }
    Pin replacement;
    if (kind == Sweep::constants && !f.mask.empty()) {
      // E: an unsigned value with a zero top run reads as its narrower slice
      // (the value itself stays: the slice reads it).
      const auto runs = runs_of(w, constant_of(f.mask), constant_of(f.value));
      if (!gu::is_unsign(t) || runs.size() != 2 || runs[0].kind >= 0 || runs[1].kind != 0) {
        continue;
      }
      Arm_builder build{g, it->second};
      replacement = build.slice(t, 0, runs[1].lo);
      for (const auto& sink : sinks) {
        gu::drop_drivers(sink);
        replacement.connect_sink(sink);
      }
      for (const auto& r : runs) {
        rep.bits += r.kind >= 0 ? static_cast<uint64_t>(r.hi - r.lo) : 0;
      }
      ++applied;
      continue;
    }
    if (kind == Sweep::constants) {
      replacement = gu::create_const(g, constant_of(f.value));
    } else {
      replacement = pin_of(f.rep_node, f.rep_pid);
      if (replacement.is_invalid() || replacement == t) {
        continue;
      }
      if (kind == Sweep::complement) {
        // ~r at t's width and sign: Not is exact for signed words, an Xor
        // with the width's mask for unsigned ones.
        const bool unsign = gu::is_unsign(t);
        auto       n      = gu::create_typed_node(g, unsign ? Ntype_op::Xor : Ntype_op::Not);
        if (gu::has_color(it->second)) {
          gu::set_color(n, gu::color_of(it->second));
        }
        gu::carry_srcid(it->second, n);
        replacement.connect_sink(gu::setup_sink_pid(n, 0));
        if (unsign) {
          gu::create_const(g, *Dlop::get_mask_value(w)).connect_sink(gu::setup_sink_pid(n, 0));
        }
        replacement = n.create_driver_pin(0);
        if (unsign) {
          gu::set_ubits(replacement, w);
        } else {
          gu::set_sbits(replacement, w);
        }
      }
    }
    for (const auto& sink : sinks) {
      gu::drop_drivers(sink);
      replacement.connect_sink(sink);
    }
    rw.released.push_back(t);
    ++applied;
  }
  rw.sweep();
  rep.nodes_removed += rw.removed;
  return applied;
}
// The resub facts of a reused row, in the order they were proven.
uint64_t replay_resub(hhds::Graph& g, const std::vector<Fact>& facts, bool shared, Stage_report& rep) {
  absl::flat_hash_map<uint64_t, Node> nodes;
  for (const auto n : g.body().nodes()) {
    nodes.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
  }
  const auto pin_of = [&](uint64_t nid, int pid) -> Pin {
    for (const auto& d : g.get_io()->get_input_pin_decls()) {
      const auto p = g.get_input_pin(d.name);
      if (!p.is_invalid() && static_cast<uint64_t>(p.get_master_node().get_debug_nid()) == nid && static_cast<int>(p.get_port_id()) == pid) {
        return p;
      }
    }
    const auto it = nodes.find(nid);
    return it == nodes.end() ? Pin{} : it->second.create_driver_pin(static_cast<hhds::Port_id>(pid));
  };
  uint64_t applied = 0;
  for (const auto& f : facts) {
    const auto it = nodes.find(f.node);
    const auto a  = pin_of(f.rep_node, f.rep_pid);
    const auto b  = pin_of(f.rep2_node, f.rep2_pid);
    if (it == nodes.end() || a.is_invalid() || b.is_invalid()) {
      continue;
    }
    std::vector<Node> made, deleted;
    std::vector<Pin>  stale;
    const auto        t = it->second.create_driver_pin(0);
    rewire_resub(g, t, build_gate(g, it->second, f.gate, a, b, made), shared, rep, stale, &deleted);
    for (const auto& n : deleted) {
      nodes.erase(static_cast<uint64_t>(n.get_debug_nid()));
    }
    ++applied;
  }
  return applied;
}

// The odc facts of a reused row, in the order they were proven.
uint64_t replay_odc(hhds::Graph& g, const std::vector<Fact>& facts, bool shared, Stage_report& rep) {
  absl::flat_hash_map<uint64_t, Node> nodes;
  for (const auto n : g.body().nodes()) {
    nodes.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
  }
  uint64_t applied = 0;
  for (const auto& f : facts) {
    const auto it = nodes.find(f.node);
    if (it == nodes.end()) {
      continue;
    }
    std::vector<Node> deleted;
    std::vector<Pin>  deleted_pins;
    if (apply_odc(g, it->second, f, shared, rep, deleted, deleted_pins)) {
      ++applied;
    }
    for (const auto& n : deleted) {
      nodes.erase(static_cast<uint64_t>(n.get_debug_nid()));
    }
  }
  return applied;
}
}  // namespace

void sweep_values(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Sweep kind, Profile profile,
                  std::string_view cache_dir, Stage_report& rep, Meter& meter) {
  std::lock_guard lock(saved_mutex);
  const bool      shared  = profile == Profile::shared;
  const auto      options = meter.budget().proof_key();
  const auto      suffix  = std::format("-sweep-{}{}", kind_name(kind), shared ? "-shared" : "");
  uint64_t        applied = 0, reused = 0;
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    if (meter.exhausted()) {
      ++rep.budget_skips;
      break;
    }
    const auto source = source_key(graph.get(), false, kSatoptSrcSalt);
    const auto path   = cache_path(cache_dir, graph->get_name(), suffix);
    auto&      row    = saved[std::string(graph->get_name()) + suffix];
    const auto usable = [&] { return row.complete && row.source == source && row.options == options; };
    if (!usable()) {
      row = read_row(path);
    }
    std::vector<Fact> facts;
    bool              searched_odc = false;  // applied during the search
    const bool        had_complete = usable();
    if (had_complete && meter.replay(row.work, row.queries)) {
      reused      += row.facts.size();
      rep.reused  += row.facts.size();
      rep.proven  += row.facts.size();
      facts        = row.facts;
    } else {
      // A complete row this budget cannot replay stays for a later, larger
      // run: a partial search never replaces it.
      const auto prior  = had_complete ? row : Row{};
      const auto work0  = meter.work_done();
      const auto query0 = meter.queries_done();
      row               = {source, options, true, 0, 0, {}};
      Search     s(*graph, kind, shared, meter, rep, row);
      const auto values = live_values(*graph);
      if (!meter.work(values.size())) {
        ++rep.budget_skips;
        row.complete = false;
      } else if (kind == Sweep::constants) {
        s.constants(values);
      } else if (kind == Sweep::odc) {
        s.odc(values);
        searched_odc = true;
      } else if (kind == Sweep::resub) {
        s.resub(values);
        searched_odc = true;  // applied during the search too
      } else {
        s.relations(values);
      }
      if (meter.exhausted()) {
        row.complete = false;  // some charge failed: whatever it paid for went unsearched
      }
      row.work    = meter.work_done() - work0;
      row.queries = meter.queries_done() - query0;
      facts       = row.facts;
      if (row.complete || !had_complete) {
        write_row(path, row);
      } else {
        row = prior;
      }
    }
    if (searched_odc) {
      applied += facts.size();
      continue;
    }
    const auto n  = kind == Sweep::odc     ? replay_odc(*graph, facts, shared, rep)
                    : kind == Sweep::resub ? replay_resub(*graph, facts, shared, rep)
                                           : apply(*graph, kind, facts, shared, rep);
    applied      += n;
    rep.applied  += n;
  }
  std::print("[pass.satopt] {} sweep: {} candidate(s), {} proven ({} reused), {} output(s) replaced\n",
             kind_name(kind),
             rep.candidates,
             rep.proven,
             reused,
             applied);
}

}  // namespace livehd::satopt
