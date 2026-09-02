// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "analyze.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "color_acyclic.hpp"
#include "color_common.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace livehd::analyze {

namespace gu = livehd::graph_util;

const char* to_string(Loop_kind k) {
  switch (k) {
    case Loop_kind::memory  : return "memory";
    case Loop_kind::sub     : return "sub";
    case Loop_kind::set_mask: return "set_mask";
    case Loop_kind::get_mask: return "get_mask";
    case Loop_kind::other   : return "other";
  }
  return "other";
}

const char* to_string(Clock_kind k) {
  switch (k) {
    case Clock_kind::implicit        : return "implicit";
    case Clock_kind::plain_input     : return "plain_input";
    case Clock_kind::plain_internal  : return "plain_internal";
    case Clock_kind::gated_inline    : return "gated_inline";
    case Clock_kind::gated_chain     : return "gated_chain";
    case Clock_kind::gate_cell       : return "gate_cell";
    case Clock_kind::from_sub        : return "from_sub";
    case Clock_kind::gates_child_port: return "gates_child_port";
    case Clock_kind::inverted        : return "inverted";
    case Clock_kind::divided         : return "divided";
    case Clock_kind::unresolved      : return "unresolved";
  }
  return "unresolved";
}

const char* to_string(Color_defect d) {
  switch (d) {
    case Color_defect::uncolored  : return "uncolored";
    case Color_defect::cyclic_dag : return "cyclic_dag";
    case Color_defect::state_spans: return "state_spans";
  }
  return "uncolored";
}

bool sim_can_lower(Clock_kind k) {
  switch (k) {
    case Clock_kind::implicit:
    case Clock_kind::plain_input:
    case Clock_kind::plain_internal:
    case Clock_kind::gated_inline:
    case Clock_kind::gated_chain:
    // A Sub whose def is a RECOGNIZED ICG cell is inlined by
    // `inline_clock_gate_cells` before scheduling, so it folds like any inline
    // gate. Reporting it as unlowerable was this pass's own first answer and it
    // was wrong by 372 sites — every one of them emits clean.
    case Clock_kind::gate_cell     : return true;
    default                        : return false;
  }
}

namespace {

// ---------------------------------------------------------------------------
// CHECK 1 — which comb nodes sit on a word-level cycle.
//
// Deliberately the SAME peel `graph/split_selfref.cpp` uses (Kahn from the
// sources; whatever never reaches in-degree 0 is on a cycle), and with the same
// non-comb exclusion set, so a finding here means the emitter's scheduler would
// see a cycle too. Reimplemented rather than called because split_selfref
// REWRITES as it goes and returns a count, not the node set.
// TWO scheduling models, and the DIFFERENCE between them is the whole point.
//
//   settled  — a Memory's read reflects STORED state and a Sub is a callable
//              boundary, so both CUT the dependency. This is what an
//              event-driven simulator does (verilator settles; a register file
//              read is not a comb path back to its own write) and what
//              `graph/split_selfref.cpp` assumes.
//   strict   — only flops and latches cut. A Memory and a Sub are ordinary
//              nodes that must be ordered exactly once. This is what
//              `inou.cgen.sim`'s single-pass `forward_class` walk does.
//
// A cycle present in STRICT but absent in SETTLED is a FALSE loop: the design is
// fine and the single-pass schedule is what cannot order it. That verdict used
// to require running verilator on the original RTL to establish; here it falls
// out of one pass over the graph.
bool is_comb(const hhds::Node_class& n, bool strict) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::IO) {
    return false;
  }
  // TRUE state cuts in BOTH models: a flop's q is last period's value.
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
    return false;
  }
  // A Memory and a Sub are exactly what the two models disagree about, so they
  // are ordinary nodes in STRICT and cut in SETTLED.
  //
  // Do NOT reach for `graph_util::is_type_register` here, however natural it
  // looks: it counts Memory as a register, so a single call would cut memories
  // in BOTH models and make the clause below dead code. That was the bug in the
  // first version of this pass, and it was not a small one — the entire MEMORY
  // loop class went unreported (`vpu_tensorfma` and `vpu_ctrl` read 0 findings
  // while cgen_sim refused both on a Memory), so the survey under-counted the
  // loop work by one of its two classes.
  if (op == Ntype_op::Memory || op == Ntype_op::Sub) {
    return strict;
  }
  return true;
}

// The nodes on ANY word-level cycle. `boundary` also collects the Sub/Memory
// nodes adjacent to the cycle: those are excluded from the peel (they are
// sequential or atomic), but they are exactly what tells a Memory back-edge
// apart from a plain packed-slice loop, so the classifier needs them.
void on_cycle_nodes(hhds::Graph* g, bool strict, absl::flat_hash_set<hhds::Node_class>& in_cycle,
                    absl::flat_hash_set<hhds::Node_class>& boundary) {
  std::vector<hhds::Node_class>                                        comb_nodes;
  absl::flat_hash_map<hhds::Node_class, int>                           indeg;
  absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> succ;

  for (auto n : g->body().nodes()) {
    if (is_comb(n, strict)) {
      comb_nodes.push_back(n);
      indeg.try_emplace(n, 0);
    }
  }
  for (auto& n : comb_nodes) {
    for (auto e : n.inp_edges()) {
      auto d = e.driver;
      if (d.is_invalid() || d.is_const()) {
        continue;
      }
      auto m = d.get_master_node();
      if (!is_comb(m, strict) || !indeg.contains(m)) {
        continue;  // state / Sub / IO / a boundary master fast_class never lists
      }
      ++indeg[n];
      succ[m].push_back(n);
    }
  }
  std::vector<hhds::Node_class> q;
  for (auto& [n, d] : indeg) {
    if (d == 0) {
      q.push_back(n);
    }
  }
  absl::flat_hash_set<hhds::Node_class> removed;
  while (!q.empty()) {
    auto n = q.back();
    q.pop_back();
    removed.insert(n);
    auto it = succ.find(n);
    if (it == succ.end()) {
      continue;
    }
    for (auto& s : it->second) {
      if (--indeg[s] == 0) {
        q.push_back(s);
      }
    }
  }
  for (auto& n : comb_nodes) {
    if (!removed.contains(n)) {
      in_cycle.insert(n);
    }
  }
  // Non-comb neighbours of the cycle. A Memory whose read output feeds the cycle
  // AND whose write inputs are fed by it is the memory class; a Sub in the same
  // position is the false-loop-through-instance class.
  if (in_cycle.empty()) {
    return;
  }
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Memory && op != Ntype_op::Sub) {
      continue;
    }
    if (in_cycle.contains(n)) {
      boundary.insert(n);  // strict model: the cell IS on the cycle
      continue;
    }
    bool feeds = false;
    bool fed   = false;
    for (auto e : n.inp_edges()) {
      if (!e.driver.is_invalid() && in_cycle.contains(e.driver.get_master_node())) {
        fed = true;
      }
    }
    for (auto e : n.out_edges()) {
      if (!e.sink.is_invalid() && in_cycle.contains(e.sink.get_master_node())) {
        feeds = true;
      }
    }
    if (feeds && fed) {
      boundary.insert(n);
    }
  }
}

Loop_kind classify_loop(const absl::flat_hash_set<hhds::Node_class>& in_cycle,
                        const absl::flat_hash_set<hhds::Node_class>& boundary, std::vector<Loop_kind>& kinds,
                        hhds::Node_class& rep) {
  bool has_mem = false;
  bool has_sub = false;
  bool has_set = false;
  bool has_get = false;
  for (const auto& n : boundary) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      has_mem = true;
      if (rep.is_invalid()) {
        rep = n;
      }
    } else if (op == Ntype_op::Sub) {
      has_sub = true;
      if (rep.is_invalid()) {
        rep = n;
      }
    }
  }
  for (const auto& n : in_cycle) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Set_mask) {
      has_set = true;
    } else if (op == Ntype_op::Get_mask) {
      has_get = true;
    }
    if (rep.is_invalid()) {
      rep = n;
    }
  }
  // Most specific first: a memory or an instance on the cycle explains it
  // regardless of what packed ops ride along.
  if (has_mem) {
    kinds.push_back(Loop_kind::memory);
  }
  if (has_sub) {
    kinds.push_back(Loop_kind::sub);
  }
  if (has_set) {
    kinds.push_back(Loop_kind::set_mask);
  }
  if (has_get) {
    kinds.push_back(Loop_kind::get_mask);
  }
  if (kinds.empty()) {
    kinds.push_back(Loop_kind::other);
  }
  return kinds.front();
}

// ---------------------------------------------------------------------------
// CHECK 2 — where a state element's clock comes from.

// Count the gate cells between `clock_pin` and the reference clock. `clock_op_of`
// returns the whole chain folded into one cone, so depth is recovered by walking
// the And spine ourselves. 1 == a single gate.
int gate_chain_depth(const hhds::Pin_class& p, int budget = 16) {
  auto cur = p;
  int  d   = 0;
  for (int i = 0; i < budget && !cur.is_invalid(); ++i) {
    if (gu::is_graph_input_pin(cur) || cur.is_const()) {
      break;
    }
    auto n  = cur.get_master_node();
    auto op = gu::type_op_of(n);
    if (op == Ntype_op::Clock_cell) {
      ++d;
      cur = gu::get_driver_of_sink_name(n, "clk_ref");
      continue;
    }
    if (op == Ntype_op::And) {
      // The gate spine: descend into the operand that is itself a gate or the
      // clock. A width-mask And (`x & 1`) is an identity and does not count.
      hhds::Pin_class next;
      int             n_real = 0;
      for (const auto& e : n.inp_edges()) {
        if (e.driver.is_const()) {
          continue;
        }
        ++n_real;
        if (next.is_invalid()) {
          next = e.driver;
        }
      }
      if (n_real <= 1) {
        cur = next;  // pure mask: identity
        continue;
      }
      ++d;
      // Follow whichever operand leads further toward a clock; the deepest wins.
      int best = 0;
      for (const auto& e : n.inp_edges()) {
        if (e.driver.is_const()) {
          continue;
        }
        best = std::max(best, gate_chain_depth(e.driver, budget - 1));
      }
      return d + best;
    }
    if (op == Ntype_op::Not || op == Ntype_op::Get_mask || op == Ntype_op::Sext || op == Ntype_op::Or) {
      cur = gu::first_value_driver(n);
      continue;
    }
    break;
  }
  return d;
}

// Does the callee CLOCK anything on the input port at `pid`? A gate output
// crossing a boundary is only a defect when the child treats that port as a
// clock; feeding a gate's output to a child as ordinary DATA is legal and
// common (an enable fanning out to logic), and reporting it would be noise.
bool port_is_child_clock(hhds::Graph* def, hhds::Port_id pid) {
  if (def == nullptr) {
    return false;
  }
  auto gio = def->get_io();
  if (!gio) {
    return false;
  }
  hhds::Pin_class port;
  for (const auto& d : gio->get_input_pin_decls()) {
    if (d.port_id == pid) {
      port = def->get_input_pin(d.name);
      break;
    }
  }
  if (port.is_invalid()) {
    return false;
  }
  for (auto sn : def->body().nodes()) {
    const auto sop = gu::type_op_of(sn);
    if (!gu::is_type_register(sn) && sop != Ntype_op::Latch && sop != Ntype_op::Memory) {
      continue;
    }
    auto cd = (sop == Ntype_op::Memory) ? hhds::Pin_class{} : gu::get_driver_of_sink_name(sn, "clock_pin");
    if (sop == Ntype_op::Memory) {
      for (const auto& e : sn.inp_edges()) {
        const auto pn = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(e.sink.get_port_id()));
        if (pn.size() >= 9 && pn.substr(pn.size() - 9) == "clock_pin") {
          cd = e.driver;
          break;
        }
      }
    }
    if (cd.is_invalid()) {
      continue;
    }
    const auto root = livehd::latch_contract::control_root(cd);
    if (!root.net.is_invalid() && root.net.get_class_index() == port.get_class_index()) {
      return true;
    }
  }
  return false;
}

// The clock sink of a state element. A Memory carries it on a PER-PORT sink
// (`port<N>_clock_pin`), everything else on the Flop-shaped `clock_pin`.
hhds::Pin_class clock_driver_of(const hhds::Node_class& n) {
  if (gu::type_op_of(n) == Ntype_op::Memory) {
    for (const auto& e : n.inp_edges()) {
      const auto pn = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(e.sink.get_port_id()));
      if (pn.size() >= 9 && pn.substr(pn.size() - 9) == "clock_pin") {
        return e.driver;
      }
    }
    return {};
  }
  return gu::get_driver_of_sink_name(n, "clock_pin");
}

Clock_finding classify_clock(const hhds::Node_class& n, const livehd::latch_contract::Design_clocks& clocks,
                             std::string_view def_name) {
  Clock_finding f;
  f.def     = std::string{def_name};
  f.element = std::string{gu::debug_name(n)};
  switch (gu::type_op_of(n)) {
    case Ntype_op::Flop  : f.op = "Flop"; break;
    case Ntype_op::Fflop : f.op = "Fflop"; break;
    case Ntype_op::Latch : f.op = "Latch"; break;
    case Ntype_op::Memory: f.op = "Memory"; break;
    default              : f.op = "state"; break;
  }

  auto d = clock_driver_of(n);
  if (d.is_invalid()) {
    f.kind = Clock_kind::implicit;
    return f;
  }
  if (gu::is_graph_input_pin(d)) {
    // A graph input is a clock PORT. Whether it is plain or gated is a property
    // of the PARENT, which this definition cannot see — that difference is the
    // whole reason a hierarchical analysis exists. Reported as plain here; the
    // hierarchical sweep upgrades it to from_port when a caller drives it with a
    // gate.
    f.kind = Clock_kind::plain_input;
    f.root = std::string{gu::pin_name_of(d)};
    return f;
  }
  if (gu::type_op_of(d.get_master_node()) == Ntype_op::Sub) {
    auto def = d.get_master_node().get_subnode_graph();
    f.kind   = (def && livehd::latch_contract::match_icg_def(def.get())) ? Clock_kind::gate_cell : Clock_kind::from_sub;
    f.root   = std::string{gu::debug_name(d.get_master_node())};
    return f;
  }

  if (auto cone = livehd::latch_contract::clock_op_of(d, clocks)) {
    f.n_guards    = static_cast<int>(cone->enables.size());
    f.chain_depth = std::max(1, gate_chain_depth(d));
    if (!cone->clock.is_invalid()) {
      f.root = gu::is_graph_input_pin(cone->clock) ? std::string{gu::pin_name_of(cone->clock)}
                                                   : std::string{gu::debug_name(cone->clock.get_master_node())};
    }
    if (cone->div != 1) {
      f.kind = Clock_kind::divided;
    } else if (cone->clock_inverted) {
      f.kind = Clock_kind::inverted;
    } else if (f.chain_depth > 1) {
      f.kind = Clock_kind::gated_chain;
    } else {
      f.kind = Clock_kind::gated_inline;
    }
    return f;
  }

  // Not a gate. An identity wrapper around a clock root is fine; anything else
  // is a derived clock nothing here can name.
  const auto root = livehd::latch_contract::control_root(d);
  if (!root.net.is_invalid() && clocks.is_clock(root.net) && root.net.get_class_index() != d.get_class_index()) {
    f.kind = root.inverted ? Clock_kind::inverted : Clock_kind::plain_internal;
    f.root = gu::is_graph_input_pin(root.net) ? std::string{gu::pin_name_of(root.net)}
                                              : std::string{gu::debug_name(root.net.get_master_node())};
    return f;
  }
  f.kind = Clock_kind::unresolved;
  f.root = std::string{gu::debug_name(d.get_master_node())};
  return f;
}

// ---------------------------------------------------------------------------
// CHECK 3 — colouring invariants.
//
// I1 total colouring: every `is_partitionable` node carries a non-zero colour.
// I3 acyclic quotient: contract each colour to one vertex, keep cross-colour
//    edges, and require the result to be a DAG. This is the invariant the whole
//    "partitions can be emitted as independently callable methods" plan rests
//    on, and nothing asserted it before.
// The state check: a partition must not contain a state element together with a
//    node that reads that element's own output, because such a partition cannot
//    be scheduled as one atomic settle.
// `node_graph_cyclic` suppresses the DAG check: a def whose node graph already
// has a word-level cycle CANNOT have an acyclic quotient over any colouring, so
// reporting one there would blame the colourer for the loop check's finding and
// double-count the same defect. The DAG result is only evidence about
// Color_acyclic when the thing it partitioned was a DAG to begin with.
void check_colors(hhds::Graph* g, std::string_view def_name, bool node_graph_cyclic, Report& rep) {
  // CLEAR FIRST. `apply_coloring` treats an already-coloured graph as SEEDED and
  // keeps the existing ids (color_common.cpp:84, `has_seeded_coloring`), so
  // re-running the colourer on a graph that came out of a flow which already
  // coloured it validates the OLD colouring and reports it as this one's. That
  // is safe to do here only because `pass analyze` refuses `--emit-dir lg:` and
  // never saves the library — the colours it writes die with the process.
  livehd::color::clear_coloring(g);

  livehd::color::Color_opts copts;
  copts.hier    = false;
  copts.compact = true;
  livehd::color::Color_acyclic alg(copts, 1, /*merge_en=*/false);
  alg.label(g);

  absl::flat_hash_map<int, int>                      part_size;
  absl::flat_hash_map<int, absl::flat_hash_set<int>> succ;  // colour -> colours
  int                                                n_nodes = 0;

  for (auto n : g->body().nodes()) {
    if (!livehd::color::is_partitionable(n)) {
      continue;
    }
    ++n_nodes;
    const int c = gu::node_color_of(n);
    if (c == livehd::color::NO_COLOR) {
      Color_finding f;
      f.def    = std::string{def_name};
      f.defect = Color_defect::uncolored;
      f.detail = std::string{gu::debug_name(n)};
      rep.colors.push_back(std::move(f));
      continue;
    }
    ++part_size[c];
  }
  for (auto n : g->body().nodes()) {
    if (!livehd::color::is_partitionable(n)) {
      continue;
    }
    const int c = gu::node_color_of(n);
    if (c == livehd::color::NO_COLOR) {
      continue;
    }
    for (auto e : n.inp_edges()) {
      if (e.driver.is_invalid() || e.driver.is_const()) {
        continue;
      }
      auto m = e.driver.get_master_node();
      if (!livehd::color::is_partitionable(m)) {
        continue;
      }
      // A state element BREAKS the dependency: its q is last period's value, so
      // an edge out of one is not an ordering constraint. Counting it would
      // report every sequential loop as a cyclic partition DAG.
      if (gu::is_type_register(m) || gu::type_op_of(m) == Ntype_op::Memory) {
        continue;
      }
      const int pc = gu::node_color_of(m);
      if (pc != livehd::color::NO_COLOR && pc != c) {
        succ[pc].insert(c);
      }
    }
  }

  // Kahn over the quotient graph. Anything left is on a colour cycle.
  absl::flat_hash_map<int, int> indeg;
  for (const auto& [c, _n] : part_size) {
    indeg.try_emplace(c, 0);
  }
  for (const auto& [from, tos] : succ) {
    for (const int t : tos) {
      ++indeg[t];
    }
  }
  std::vector<int> q;
  for (const auto& [c, d] : indeg) {
    if (d == 0) {
      q.push_back(c);
    }
  }
  size_t seen = 0;
  while (!q.empty()) {
    const int c = q.back();
    q.pop_back();
    ++seen;
    auto it = succ.find(c);
    if (it == succ.end()) {
      continue;
    }
    for (const int t : it->second) {
      if (--indeg[t] == 0) {
        q.push_back(t);
      }
    }
  }
  if (seen < indeg.size() && !node_graph_cyclic) {
    std::string on_cycle;
    for (const auto& [c, d] : indeg) {
      if (d > 0) {
        on_cycle += (on_cycle.empty() ? "" : ",") + std::to_string(c);
      }
    }
    Color_finding f;
    f.def     = std::string{def_name};
    f.defect  = Color_defect::cyclic_dag;
    f.detail  = std::format("{} of {} colour(s) on a cycle: {}", indeg.size() - seen, indeg.size(), on_cycle);
    f.n_parts = static_cast<int>(part_size.size());
    f.n_nodes = n_nodes;
    rep.colors.push_back(std::move(f));
  }
}

}  // namespace

void analyze_def(hhds::Graph* g, const Opts& opts, Report& rep) {
  if (g == nullptr) {
    return;
  }
  const std::string def_name{g->get_name()};
  ++rep.n_defs;

  if (opts.loops) {
    // STRICT first (the emitter's model). If it is clean there is nothing to
    // report; the settled model is a subset of it by construction.
    absl::flat_hash_set<hhds::Node_class> strict_cycle;
    absl::flat_hash_set<hhds::Node_class> strict_bound;
    on_cycle_nodes(g, /*strict=*/true, strict_cycle, strict_bound);
    if (!strict_cycle.empty()) {
      absl::flat_hash_set<hhds::Node_class> settled_cycle;
      absl::flat_hash_set<hhds::Node_class> settled_bound;
      on_cycle_nodes(g, /*strict=*/false, settled_cycle, settled_bound);

      Loop_finding f;
      f.def        = def_name;
      f.n_on_cycle = static_cast<int>(strict_cycle.size());
      f.real       = !settled_cycle.empty();
      int n_comb   = 0;
      for (auto n : g->body().nodes()) {
        if (is_comb(n, /*strict=*/true)) {
          ++n_comb;
        }
      }
      f.n_comb = n_comb;
      hhds::Node_class rep_node;
      // Classify against whichever model still shows the cycle: for a REAL loop
      // the settled set is the honest evidence; for a false one the strict set
      // names the cell that manufactured it.
      f.kind = f.real ? classify_loop(settled_cycle, settled_bound, f.kinds, rep_node)
                      : classify_loop(strict_cycle, strict_bound, f.kinds, rep_node);
      f.node = rep_node.is_invalid() ? std::string{} : std::string{gu::debug_name(rep_node)};
      rep.loops.push_back(std::move(f));
    }
  }

  if (opts.clocks) {
    const livehd::latch_contract::Design_clocks clocks(g, /*hier=*/false);
    // THE DOWN DIRECTION, which no per-definition check can see from the child's
    // side: a gate lives HERE and its output crosses into a CHILD as that
    // child's clock port. Inside the child that port is an ordinary graph input,
    // so cgen_sim picks it as the reference clock and the child's state commits
    // EVERY tick with the gate as dead code. It is a silent miscompile, and it
    // is invisible from either definition alone — which is the whole argument
    // for surveying a library rather than a module.
    for (auto n : g->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto def = n.get_subnode_graph();
      for (const auto& e : n.inp_edges()) {
        if (e.driver.is_invalid() || e.driver.is_const() || gu::is_graph_input_pin(e.driver)) {
          continue;
        }
        // The gate can reach the child either as an INLINE cone in this body or
        // as the output of an instantiated gate CELL. Both are the same defect.
        int n_guards = 0;
        if (auto cone = livehd::latch_contract::clock_op_of(e.driver, clocks); cone && !cone->enables.empty()) {
          n_guards = static_cast<int>(cone->enables.size());
        } else if (gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sub) {
          auto gdef = e.driver.get_master_node().get_subnode_graph();
          if (!gdef || !livehd::latch_contract::match_icg_def(gdef.get())) {
            continue;
          }
          n_guards = 1;  // the cell's enable cone, which lives inside the cell
        } else {
          continue;
        }
        // ...and it must land on a port the CHILD actually clocks on. Without
        // this the check would fire on a gate output used as ordinary data,
        // which is legal and common (a clock-gate enable fanning out to logic).
        if (!def || !port_is_child_clock(def.get(), e.sink.get_port_id())) {
          continue;
        }
        Clock_finding f;
        f.def         = def_name;
        f.element     = std::string{gu::debug_name(n)};
        f.op          = "Sub";
        f.kind        = Clock_kind::gates_child_port;
        f.n_guards    = n_guards;
        f.chain_depth = std::max(1, gate_chain_depth(e.driver));
        f.root        = std::string{gu::debug_name(e.driver.get_master_node())};
        rep.clocks.push_back(std::move(f));
      }
    }
    for (auto n : g->body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (!gu::is_type_register(n) && op != Ntype_op::Latch && op != Ntype_op::Memory) {
        continue;
      }
      ++rep.n_state_elements;
      auto f = classify_clock(n, clocks, def_name);
      if (opts.verbose || !sim_can_lower(f.kind)) {
        rep.clocks.push_back(std::move(f));
      }
    }
  }

  if (opts.colors) {
    // Reuse the loop check's verdict when it ran; otherwise ask once.
    bool cyclic = false;
    if (opts.loops) {
      cyclic = !rep.loops.empty() && rep.loops.back().def == def_name;
    } else {
      absl::flat_hash_set<hhds::Node_class> c;
      absl::flat_hash_set<hhds::Node_class> b;
      on_cycle_nodes(g, /*strict=*/true, c, b);
      cyclic = !c.empty();
    }
    check_colors(g, def_name, cyclic, rep);
  }
}

namespace {
std::string jstr(std::string_view s) {
  std::string o = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      o += ' ';
    } else {
      o += c;
    }
  }
  o += '"';
  return o;
}
}  // namespace

std::string report_json(const Report& rep) {
  std::string o;
  for (const auto& f : rep.loops) {
    std::string kinds;
    for (const auto k : f.kinds) {
      kinds += (kinds.empty() ? "" : ",");
      kinds += jstr(to_string(k));
    }
    o += std::format(R"({{"check":"loops","def":{},"kind":{},"kinds":[{}],"node":{},"on_cycle":{},"comb_nodes":{},"real":{}}})"
                     "\n",
                     jstr(f.def),
                     jstr(to_string(f.kind)),
                     kinds,
                     jstr(f.node),
                     f.n_on_cycle,
                     f.n_comb,
                     f.real ? "true" : "false");
  }
  for (const auto& f : rep.clocks) {
    o += std::format(
        R"({{"check":"clocks","def":{},"element":{},"op":{},"kind":{},"guards":{},"chain_depth":{},"root":{},"sim_ok":{}}})"
        "\n",
        jstr(f.def),
        jstr(f.element),
        jstr(f.op),
        jstr(to_string(f.kind)),
        f.n_guards,
        f.chain_depth,
        jstr(f.root),
        sim_can_lower(f.kind) ? "true" : "false");
  }
  for (const auto& f : rep.colors) {
    o += std::format(R"({{"check":"colors","def":{},"defect":{},"detail":{},"parts":{},"nodes":{}}})"
                     "\n",
                     jstr(f.def),
                     jstr(to_string(f.defect)),
                     jstr(f.detail),
                     f.n_parts,
                     f.n_nodes);
  }
  o += std::format(R"({{"check":"summary","defs":{},"state_elements":{},"loop_defs":{},"clock_findings":{},"color_findings":{}}})"
                   "\n",
                   rep.n_defs,
                   rep.n_state_elements,
                   rep.loops.size(),
                   rep.clocks.size(),
                   rep.colors.size());
  return o;
}

}  // namespace livehd::analyze
