// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "proof_prep.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <string_view>

#include "diag.hpp"
#include "inline_sub.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "pass_single_edge.hpp"
#include "str_tools.hpp"

namespace livehd::single_edge {

// find, and a def with no gate at all is not touched in any way.
// 2f-latch M9 — RECOGNIZE instantiated clock gates as `Clock_cell`, everywhere
// the hierarchical driver will encode. Runs BEFORE the inline+fold below, and
// takes precedence over it: what this recognizes, the fold never sees.
//
// NO `is_boxed` FILTER, AND THAT IS THE POINT. Inlining a TRUSTED def is
// unsound -- it pulls internals the user declared out of scope into the
// compared cone, which is why `inline_clock_gate_cells` takes the predicate.
// Recognition is different in kind: nothing of the def's STATE crosses the
// boundary (the enable latch is replaced by the cell's sampling contract), only
// a pure combinational function of nets the parent ALREADY drives and already
// compares. So trust is respected rather than fought -- and since the instance
// is then gone, the def is no longer instantiated at all and its trust entry
// becomes a no-op, which is what lets `prim_clk_gate` leave the trust list.
int materialize_clock_cells_all(hhds::Graph* top, const std::vector<hhds::Graph*>& defs) {
  int                               n = livehd::latch_contract::materialize_clock_cells(top, "pass.single_edge");
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;  // ref and impl def lists share every --lib cell model: same Graph*
    }
    n += livehd::latch_contract::materialize_clock_cells(d, "pass.single_edge");
  }
  return n;
}

// Clock analysis runs before the encoder's ordinary combinational --lib
// expansion, so expose modeled gates on clock cones before scheduling edges.
void inline_clock_lib_cells(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* graph) {
  if (graph == nullptr || sub_lib.empty()) {
    return;
  }
  absl::flat_hash_set<hhds::Gid> combinational;
  for (const auto& [gid, model] : sub_lib) {
    if (model == nullptr) {
      continue;
    }
    bool pure = true;
    for (auto node : model->body().nodes()) {
      const auto op = livehd::graph_util::type_op_of(node);
      if (livehd::graph_util::is_type_register(node) || op == Ntype_op::Memory || op == Ntype_op::Sub
          || op == Ntype_op::Clock_cell) {
        pure = false;
        break;
      }
    }
    if (pure) {
      combinational.insert(gid);
    }
  }
  std::vector<hhds::Pin_class> pending;
  for (auto node : graph->body().nodes()) {
    const auto op = livehd::graph_util::type_op_of(node);
    if (op == Ntype_op::Memory) {
      livehd::graph_util::for_each_memory_clock_driver(node, [&](auto pin) { pending.push_back(pin); });
    } else if (livehd::graph_util::is_type_register(node)) {
      pending.push_back(livehd::graph_util::get_driver_of_sink_name(node, "clock_pin"));
    }
  }
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Node_class>          cells;
  while (!pending.empty()) {
    const auto pin = pending.back();
    pending.pop_back();
    if (pin.is_invalid() || pin.is_const() || livehd::graph_util::is_graph_input_pin(pin)) {
      continue;
    }
    auto node = pin.get_master_node();
    if (!seen.insert(node.get_class_index()).second || livehd::graph_util::is_type_register(node)) {
      continue;
    }
    if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
      if (!combinational.contains(node.get_subnode_gid())) {
        continue;
      }
      cells.push_back(node);
    }
    for (auto sink : node.inp_sorted_pins()) {
      // PLURAL: a compact loop's carry-in sink holds two drivers
      // (pass/legalize/legalize.cpp:301).
      for (const auto& drv : sink.get_driver_pins()) {
        pending.push_back(drv);
      }
    }
  }
  for (const auto& cell : cells) {
    // Explicit --lib models live outside the design library. Inline only the
    // clock cone so phase analysis sees buffer/inverter polarity and gates.
    if (!livehd::graph_util::inline_sub_instance(graph, cell, "pass.lec", sub_lib.at(cell.get_subnode_gid()))) {
      return;  // leave the unresolved clock for the existing fail-closed analysis
    }
  }
}

// Stateful --lib models must contribute real state before the encoder cuts
// flops; an opaque instance would otherwise leave unrelated free symbols.
void inline_stateful_lib_cells(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* impl_g) {
  if (sub_lib.empty() || impl_g == nullptr) {
    return;
  }
  absl::flat_hash_set<hhds::Gid> stateful;
  for (const auto& [gid, gp] : sub_lib) {
    if (gp == nullptr) {
      continue;
    }
    for (auto dn : gp->body().nodes(hhds::Node_order::forward)) {
      const auto op = livehd::graph_util::type_op_of(dn);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory) {
        stateful.insert(gid);
        break;
      }
    }
  }
  if (stateful.empty()) {
    return;
  }
  std::set<std::string>         hit;    // sorted: the message must be deterministic
  std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
  for (auto sn : impl_g->body().nodes()) {
    if (livehd::graph_util::type_op_of(sn) != Ntype_op::Sub || stateful.count(sn.get_subnode_gid()) == 0) {
      continue;
    }
    insts.push_back(sn);
    if (auto sio = sn.get_subnode_io(); sio != nullptr) {
      hit.insert(std::string(sio->get_name()));
    }
  }
  const size_t n = insts.size();
  if (n == 0) {
    return;
  }
  std::string names;
  for (const auto& s : hit) {
    names += names.empty() ? "" : ", ";
    names += s;
  }
  // INLINE them instead of blackboxing. encode.cpp's `--lib` inline path is
  // combinational-only (a stateful model falls through to the blackbox path,
  // where it contributes NO state and nothing can correspond to the ref's native
  // flop — both engines then answer unknown in milliseconds). Splicing the cell
  // body into the impl turns its internal Flop into an ordinary body flop, which
  // the existing flop-cut machinery cuts and names after the instance — and
  // pass/abc/abc_map.cpp already names each mapped DFF instance after its source
  // register bit. Same move `inline_clock_gate_cells` makes for an ICG cell; the
  // only reason it could not reach these is that a `--lib` model is not in the
  // impl's own graph library, hence the explicit-def overload.
  size_t done = 0;
  for (const auto& inst : insts) {
    auto git = sub_lib.find(inst.get_subnode_gid());
    if (git == sub_lib.end() || git->second == nullptr) {
      continue;
    }
    // The cell's own instance name is the ONLY meaningful name the spliced state
    // can carry: a gensim cell model's internal flop has no `name` attr, so
    // Sub_inliner::carry_node_attrs leaves it unnamed and the flop cut ends up
    // keyed on a synthesized net name (`n1831`) that corresponds to nothing on
    // the ref side. Snapshot the existing flops, inline, then name whatever flop
    // appeared after the instance (`id_q_0`) — abc already named the instance
    // after the source register bit.
    // Only a SINGLE-flop model may take the instance name: stamping it on two
    // flops would fuse two distinct state cuts onto one key and silently drop a
    // compare point. A multi-flop cell keeps whatever the inliner produced.
    // Counted on the MODEL (a handful of nodes), and the naming itself happens
    // inside the inliner — re-walking the whole parent body once per instance
    // would be quadratic on a design with thousands of mapped cells.
    int model_flops = 0;
    for (auto dn : git->second->body().nodes(hhds::Node_order::forward)) {
      model_flops += livehd::graph_util::is_type_flop(dn) ? 1 : 0;
    }
    done += livehd::graph_util::inline_sub_instance(impl_g, inst, "pass.lec", git->second, model_flops == 1) ? 1 : 0;
  }
  if (done == n) {
    return;  // fully inlined: the cells are ordinary logic + flops now
  }
  livehd::diag::warn("pass.lec", "stateful-lib-cell", "unsupported")
      .msg("the impl instantiates {} STATEFUL library cell(s) ({}) — lec could inline only {} of them", n, names, done)
      .hint(
          "a cell model that stays a blackbox contributes no state, so nothing corresponds to the ref's native flop "
          "and the run is INCONCLUSIVE no matter the budget; re-synthesize with `--set pass.abc.register=false` to "
          "keep registers native")
      .emit();
}

// Bring every INTEGRATED CLOCK GATE into a body the analyses can see, across
// the top AND each def the encoder will meet, and fold the defs that gained one.
// Returns {cells inlined, defs folded}.
//
// Inlining the top alone holds only for a design that instantiates its gate AT
// the top. A real one puts it further down (minion instantiates `prim_clk_gate`
// inside `minion_dcache_reduce`, `txfma_top`, `vpu_trans` and 8 more), and there
// the top body holds no cell at all -- so nothing was inlined, every gated flop
// kept an opaque `Sub` for a clock, and the encoder refused each of those defs.
//
// Folding is not optional once a def is inlined: the cell's enable latch lands
// in the def's body, and the def scan refuses ANY def holding a latch, so
// inlining alone would trade an encode refusal for a normalization refusal.
//
// P=1 IS THE WHOLE SAFETY ARGUMENT. A gate has ONE commit edge, so folding it
// into an enable is a pure retype -- no phase divider, no re-timing, hence none
// of the cross-module timing question that keeps the GENERAL per-def case (a
// genuine latch or a negedge flop, P>1) refusing. The dry run enforces exactly
// that: a def whose plan wants a divider is left untouched for the refusal to
std::pair<int, int> inline_clock_gates_and_fold(hhds::Graph* top, const std::vector<hhds::Graph*>& defs,
                                                       absl::flat_hash_set<hhds::Graph*>*             unfolded,
                                                       const std::function<bool(const hhds::Graph*)>& is_boxed) {
  int                               cells  = livehd::latch_contract::inline_clock_gate_cells(top, "pass.single_edge", is_boxed);
  int                               folded = 0;
  // Dedupe: the ref and impl def lists share every `--lib` cell model, and a def
  // reached twice is the same Graph*. Inlining is idempotent, but the COUNT
  // would double and read as twice the work.
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;
    }
    // STRICTLY ADDITIVE: a def that already holds a latch or a negedge flop is
    // one the def scan refuses TODAY, and that refusal is load-bearing (it is
    // what keeps a latch def from being silently blackboxed). Leave it exactly
    // as it was and let the scan speak. We only ever touch defs that pass the
    // scan today, so nothing that passes now can start failing.
    if (const auto pre = livehd::latch_contract::needs_single_edge(d); pre.n_latches > 0 || pre.n_negedge_flops > 0) {
      continue;
    }
    // PREDICT the fold failure instead of discovering it after mutating. The
    // inline is DESTRUCTIVE and has no undo, so a def whose fold then fails is
    // handed back holding an enable Latch it did NOT have when we found it —
    // and while `unfolded` keeps it out of the def SCAN, the ENCODER still
    // refuses a Latch, so a def that used to encode cleanly (an opaque gate
    // cell whose gated clock only crosses into a child) regresses from PROVEN
    // to UNKNOWN purely because this ran. "Nothing that passes now can start
    // failing" only holds for defs whose fold succeeds.
    //
    // The dominant failure is the documented one: resolve_icg folds only in a
    // SINGLE-clock design (a gate on a second domain has no reference clock to
    // be relative to), after which the orphaned latch wants a divider. That is
    // decidable BEFORE touching anything.
    if (livehd::latch_contract::Design_clocks(d).n_clock_inputs() > 1) {
      continue;
    }
    const int nd = livehd::latch_contract::inline_clock_gate_cells(d, "pass.single_edge", is_boxed);
    if (nd <= 0) {
      continue;  // no gate here: leave the def byte-for-byte as it was
    }
    cells += nd;
    // An EMPTY allow-list: this call normalizes the def's OWN body only. A
    // latch deeper still is not this call's business -- the caller's top-level
    // scan walks the whole instance tree and refuses there, as before.
    livehd::single_edge::Options dp;
    dp.dry_run                        = true;
    dp.quiet                          = true;  // a def we then decline to fold must not print a refusal
    const auto                   plan = livehd::single_edge::normalize(d, {}, dp);
    livehd::single_edge::Options ao;
    ao.quiet = true;
    if (!plan.error && plan.applied && plan.slots == 1) {
      if (const auto done = livehd::single_edge::normalize(d, {}, ao); done.applied && !done.error) {
        ++folded;
        continue;
      }
    }
    // Could not fold (a second clock net, or a plan wanting a divider). The
    // gate's enable latch is now in this def's body, which the def scan would
    // refuse -- turning what is today a single UNKNOWN def into a refusal of
    // the WHOLE run. So hand the def back to the caller to keep OUT of that
    // scan: the encoder then meets it exactly as it does today and returns the
    // same honest per-def UNKNOWN (`sequential op 'latch' not supported yet`
    // rather than `derived clock` -- same verdict, different sentence), while
    // every def that did fold is a def that now proves.
    // Residual (not predicted above): the def is now mutated and there is no
    // rollback, so at minimum say so instead of leaving a silent regression.
    if (const auto post = livehd::latch_contract::needs_single_edge(d); post.n_latches > 0) {
      livehd::diag::warn("pass.single_edge", "icg-inline-not-folded", "unsupported")
          .msg(
              "def '{}' had its clock-gate cell inlined but the fold did not apply, so it now holds an enable latch it "
              "did not have before; it will encode as UNKNOWN rather than refuse",
              d->get_name())
          .hint("flatten the design, or trust this def, to get a verdict for it")
          .emit();
    }
    if (unfolded != nullptr) {
      unfolded->insert(d);
    }
  }
  return {cells, folded};
}

// One side dropped part of the other's hierarchy: inline, into each definition
// the other side still has, every instance whose definition the other library
// no longer holds, so both sides expose comparable machine state.
//
// Two producers do this to a netlist. `pass color flat` fuses the WHOLE
// hierarchy into ONE abc region, so the impl is a single graph while the ref
// keeps every child; `pass color synth` (and `lhd synth`) colors a flat view
// and partitions its logic into regions that can span source definitions. In
// both shapes the ref top owns only its own flops (dino: pc, cycleCount) while
// the impl top owns `pipeA_if_id.reg_0` and friends, so those are impl-only
// unpaired state, no flop bijection exists, the flop-cut inductive miter is
// never built, and the def degrades to a whole-design BMC that times out and
// falls to the flat retry (dino: ~8 s wasted before the cone pass proves it).
//
// Inlining is semantics-preserving and gives each spliced flop its hierarchical
// name (`pipeA_if_id.reg_0`), which is exactly what the netlist calls it, so
// tier-1 name pairing resolves them and the collapsed hierarchical proof goes
// through with the kept defs still boxed. An instance whose def the impl DOES
// keep is left alone: that pair proves def by def, and flattening it would
// throw away the decomposition that makes the proof tractable.
//
// This is deliberately source-format agnostic. It covers mapped netlists, but
// also the ordinary Verilog-vs-Pyrope shape where one front-end retains helper
// modules and the other has already flattened them. A hierarchy boundary is
// not part of the equivalence contract and must never manufacture a REFUTED
// verdict. `sub_lib` definitions are real mapped-cell vocabulary and are never
// treated as absorbed design hierarchy. Returns how many instances were
// spliced on `side`.
size_t inline_instances_missing_from_other_side(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib,
                                                       const std::vector<std::shared_ptr<hhds::Graph>>&    side_graphs,
                                                       const std::vector<std::shared_ptr<hhds::Graph>>&    other_graphs,
                                                       hhds::Graph*                                        side_g) {
  if (side_g == nullptr) {
    return 0;
  }
  // The defs the other side still has, by full name AND by entity tail (a
  // Pyrope graph keeps `file.entity`; the tail covers a flat Verilog side).
  absl::flat_hash_set<std::string> other_defs;
  for (const auto& sp : other_graphs) {
    if (sp) {
      const std::string full{sp->get_name()};
      other_defs.insert(full);
      other_defs.insert(str_tools::canonical_entity_name(full));
    }
  }
  auto other_has_def = [&](std::string_view def_name) {
    const std::string full{def_name};
    return other_defs.contains(full) || other_defs.contains(str_tools::canonical_entity_name(full));
  };
  // Every definition the other side still has gets the treatment, the top
  // included: an absorbed def is inlined at ALL its sites, so a kept child may
  // hold absorbed grandchildren too.
  std::vector<hhds::Graph*> hosts{side_g};
  for (const auto& sp : side_graphs) {
    if (sp && sp.get() != side_g && other_has_def(sp->get_name()) && sub_lib.find(sp->get_gid()) == sub_lib.end()) {
      hosts.push_back(sp.get());
    }
  }
  // RUNAWAY GUARD ONLY -- it is not the termination argument. Termination comes
  // from the `spliced == 0` break below: a pass either inlines something or
  // stops. This exists solely so a hierarchy that somehow regenerates instances
  // fails loudly instead of spinning, so it must be UNREACHABLE for any real
  // design. It is NOT a bound on the instance count: a def instantiated N times
  // contributes N copies of its whole subtree, so the transitive total is a
  // PRODUCT down the hierarchy, not a sum. A first attempt used
  // (Sub count x 2) and fired at 292 splices on a legitimate `loop_roll_carry`
  // run that needed exactly that many -- turning a PROVEN into a hard error.
  size_t side_nodes = 0;
  for (const auto& sp : side_graphs) {
    if (!sp) {
      continue;
    }
    for ([[maybe_unused]] auto n : sp->body().nodes()) {
      ++side_nodes;
    }
  }
  const size_t splice_budget = 10000 + side_nodes * 100;

  size_t done = 0;
  for (auto* host : hosts) {
    // ONE SPLICE PER COLLECTION, then re-collect.
    //
    // `inline_sub_instance` MUTATES `host`, and every other handle in the
    // collected vector points into that same host. Splicing the whole batch
    // reused handles that the first splice had already invalidated, which
    // silently corrupted the model: with >=2 absorbed instances LEC reported a
    // counterexample that DOES NOT REPRODUCE IN SIMULATION. Swapping two
    // instantiation lines in the reference flipped REFUTED<->PROVEN with the
    // implementation byte-identical, while 20,052 random vectors showed zero
    // mismatch between source Verilog, ref netlist and impl netlist.
    // Collecting first is necessary (never mutate while walking) but NOT
    // sufficient -- the handles have to be re-derived after each mutation.
    //
    // Progress-bounded rather than a fixed round cap: the loop only continues
    // while a splice actually happened, so it cannot spin, and the old magic 64
    // silently truncated any host with more absorbed instances than that.
    while (true) {
      std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
      for (auto n : host->body().nodes()) {
        if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub || sub_lib.find(n.get_subnode_gid()) != sub_lib.end()) {
          continue;  // a Liberty cell is the impl's vocabulary, never an absorbed def
        }
        auto sio = n.get_subnode_io();
        if (sio == nullptr || other_has_def(sio->get_name())) {
          continue;  // the other side kept this def: it pairs def by def
        }
        // A genuine BLACKBOX -- an assertion/diagnostic marker instance, a
        // memory macro, any def whose body this side does not hold either --
        // has nothing to splice, and inline_sub_instance answers a bodyless or
        // replicated Sub with a FATAL internal error that kills the whole run.
        // This used to be unreachable because the caller only ran on a `--lib`
        // mapped-netlist comparison; now that ANY design reaches here, leave
        // such an instance a boundary (it is a boundary on both sides anyway).
        auto def_g = n.get_subnode_graph();
        if (n.is_loop_subnode() || !def_g) {
          continue;
        }
        // An instantiated CLOCK GATE is not absorbed design hierarchy either: it
        // is the one recognized clock operator, and materialize_clock_cells (run
        // further down) is what turns it into the `Clock_cell` the encoder can
        // model. Dissolving it here leaves a plain derived-clock cone, the
        // encoder REFUSES the def, and a real difference downstream of the gate
        // comes back UNKNOWN instead of REFUTED (clock_cell_test case 6b).
        if (livehd::latch_contract::match_icg_def(def_g.get())) {
          continue;
        }
        insts.push_back(n);
      }
      if (insts.empty()) {
        break;
      }
      if (done >= splice_budget) {
        livehd::diag::err("pass.lec", "inline-runaway", "internal")
            .msg("inlining absorbed defs into '{}' exceeded {} splices; refusing to continue",
                 std::string{host->get_name()},
                 splice_budget)
            .emit();
        break;
      }
      size_t spliced = 0;
      // Try candidates in order and STOP AT THE FIRST SUCCESS: that splice
      // invalidates every remaining handle, so the rest of `insts` is discarded
      // and re-derived on the next pass. A candidate that returns false did not
      // mutate anything (a real blackbox), so it is safe to try the next one --
      // otherwise one un-inlinable instance would mask every later one, which is
      // what the batch loop got right and a naive one-shot fix would lose.
      for (const auto& inst : insts) {
        if (spliced != 0) {
          break;
        }
        // pass.color names an absorbed region `<host>__c<N>` and inserts an
        // UNNAMED Sub at the host boundary.  Its fallback instance name
        // (`sub_<nid>`) is not source hierarchy and is unstable across the two
        // graphs; retaining it turns every otherwise preserved register name
        // into `sub_20.foo` and defeats flop correspondence after collapse.
        // Drop the prefix only for that reserved generated shape.  A real
        // named instance, and any ordinary helper definition, keeps its full
        // hierarchy so distinct occurrences cannot alias.
        bool synthetic_partition = false;
        if (livehd::graph_util::node_name_of(inst).empty()) {
          auto        sio      = inst.get_subnode_io();
          std::string host_ent = str_tools::canonical_entity_name(host->get_name());
          std::string def_ent  = sio == nullptr ? std::string{} : str_tools::canonical_entity_name(sio->get_name());
          std::string marker   = host_ent + "__c";
          if (def_ent.starts_with(marker) && def_ent.size() > marker.size()) {
            synthetic_partition = std::all_of(def_ent.begin() + static_cast<std::ptrdiff_t>(marker.size()),
                                              def_ent.end(),
                                              [](unsigned char c) { return std::isdigit(c); });
          }
        }
        spliced += livehd::graph_util::inline_sub_instance(host, inst, "pass.lec", nullptr, false, !synthetic_partition) ? 1 : 0;
      }
      if (spliced == 0) {
        break;  // nothing inlinable left (a real blackbox): stop rather than spin
      }
      done += spliced;
    }
  }
  return done;
}

Time_base prepare_time_base(const Cell_models& sub_lib, hhds::Graph* ref, std::vector<hhds::Graph*>& ref_defs, hhds::Graph* impl,
                            std::vector<hhds::Graph*>& impl_defs, bool quiet_decline,
                            const std::function<bool(const hhds::Graph*)>& is_boxed) {
  Time_base tb;
  // Either input may be a mapped netlist. Expand explicit state/clock models
  // symmetrically before collecting cuts, including cells inside retained defs.
  auto inline_lib_cells = [&](hhds::Graph* top, const std::vector<hhds::Graph*>& defs) {
    inline_stateful_lib_cells(sub_lib, top);
    inline_clock_lib_cells(sub_lib, top);
    for (auto* d : defs) {
      if (d != nullptr && d != top && sub_lib.find(d->get_gid()) == sub_lib.end()) {
        inline_stateful_lib_cells(sub_lib, d);
        inline_clock_lib_cells(sub_lib, d);
      }
    }
  };
  inline_lib_cells(ref, ref_defs);
  inline_lib_cells(impl, impl_defs);
  // CLOCK-GATE CELLS first. A real design instantiates its ICG
  // (`prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));`), so the gate
  // sits one module level away and the flop's clock_pin is an opaque Sub output
  // that nothing can recognize. Inlining just those cells brings the gate into
  // the body, where the M8 fold turns it into a flop enable. Semantics-
  // preserving on its own and idempotent, so it runs on BOTH sides before either
  // is probed -- symmetry matters here as much as anywhere.
  absl::flat_hash_set<hhds::Graph*> unfolded;
  auto                              note_gates = [&tb](std::string_view which, std::pair<int, int> r) {
    if (r.first > 0) {
      tb.recipe_steps.emplace_back(
          std::format("pass.single_edge inlined {} {} clock-gate cell(s), folded {} def(s)", r.first, which, r.second));
    }
  };
  // M9 recognition runs FIRST and on BOTH sides, since a gate recognized on one
  // side only would compare a Clock_cell against a Sub.
  if (const int mr = materialize_clock_cells_all(ref, ref_defs); mr > 0) {
    tb.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} ref clock gate(s) as Clock_cell", mr));
  }
  if (const int mi = materialize_clock_cells_all(impl, impl_defs); mi > 0) {
    tb.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} impl clock gate(s) as Clock_cell", mi));
  }
  note_gates("ref", inline_clock_gates_and_fold(ref, ref_defs, &unfolded, is_boxed));
  note_gates("impl", inline_clock_gates_and_fold(impl, impl_defs, &unfolded, is_boxed));
  if (!unfolded.empty()) {
    auto drop = [&unfolded](std::vector<hhds::Graph*>& v) {
      std::erase_if(v, [&unfolded](hhds::Graph* d) { return unfolded.contains(d); });
    };
    drop(ref_defs);
    drop(impl_defs);
  }
  // The rewrite is PREFERRED when it applies: it also normalizes a sync reset
  // into the enable/din shape on BOTH sides, which is what lets state pairing
  // match a flop whose reset the other front-end spells in the body. BOTH sides
  // or NEITHER: a one-sided lowering compares two designs in different time
  // bases, which is the failure a decline exists to prevent.
  auto probe_side = [&](hhds::Graph* side, const std::vector<hhds::Graph*>& defs) {
    Options po;
    po.dry_run = true;
    po.quiet   = quiet_decline;
    return normalize(side, defs, po);
  };
  const auto pr = probe_side(ref, ref_defs);
  const auto pi = probe_side(impl, impl_defs);
  if (pr.error || pi.error) {
    tb.declined       = true;
    tb.declined_ref   = pr.error;
    tb.decline_reason = pr.error ? pr.reason : pi.reason;
    if (quiet_decline) {
      tb.recipe_steps.emplace_back(
          std::format("pass.lec phase_sched: 4-microstep schedule (edge normalization declined: {})", tb.decline_reason));
    }
    return tb;
  }
  if (!pr.applied && !pi.applied) {
    return tb;  // nothing to lower on either side: already one time base
  }
  // ONE time base for both sides: the max P either side needs. A side with
  // nothing of its own to lower still gets the divider and slot 0 -- its P=1
  // behavior embedded in the P-slot time base -- which keeps an all-posedge ref
  // comparable against a negedge impl instead of the two counting time
  // differently.
  Options ao;
  ao.force_slots        = std::max(pr.slots, pi.slots);
  const auto rn         = normalize(ref, ref_defs, ao);
  // SAME GRAPH OBJECT on both sides (`--impl X --ref X`, the vacuity-guard
  // idiom): normalizing again would find nothing left to lower and report
  // "skipped" with no slot count and no reference clock, which the agreement
  // checks below would misread as a disagreement.
  const bool same_graph = ref == impl;
  const auto in         = same_graph ? rn : normalize(impl, impl_defs, ao);
  if (rn.error || in.error) {
    tb.error = std::format("lec: edge normalization failed after planning ({})", rn.error ? rn.reason : in.reason);
    return tb;
  }
  // Both agreement checks apply only when BOTH sides actually normalized; a side
  // that legitimately had nothing to lower reports slots=1 and no reference
  // clock, and force_slots already put the two in one time base.
  if (rn.applied && in.applied && rn.slots != in.slots) {
    tb.error      = std::format("lec: edge normalization produced P={} on the ref side and P={} on the impl side", rn.slots, in.slots);
    tb.error_hint = "the two designs mix clock edges differently; compare like against like";
    return tb;
  }
  if (rn.applied && in.applied && rn.ref_clock != in.ref_clock) {
    // Slots are RELATIVE to a reference clock, so two sides normalized against
    // different clocks are in different time bases. The encoder models a single
    // clock as "commits every step" with no notion of clock IDENTITY, so a latch
    // gated by `clk` and one gated by `clk2` would encode identically and come
    // back falsely PROVEN.
    tb.error      = std::format("lec: the ref side normalizes against clock '{}' but the impl side against '{}'",
                           rn.ref_clock.empty() ? "<none>" : rn.ref_clock,
                           in.ref_clock.empty() ? "<none>" : in.ref_clock);
    tb.error_hint = "the two designs are clocked by different nets, so their slots do not denote the same instants";
    return tb;
  }
  tb.applied      = true;
  tb.slots        = rn.slots;
  tb.ref_latches  = rn.latches_retyped;
  tb.impl_latches = in.latches_retyped;
  return tb;
}

}  // namespace livehd::single_edge
