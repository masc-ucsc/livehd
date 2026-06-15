// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "query.hpp"

#include <cvc5/cvc5.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "encode.hpp"
#include "node_util.hpp"

namespace livehd::lec {

Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts) {
  Query_result res;
  res.detail = "solver=" + opts.solver + " (cvc5 direct, flop-cut inductive miter)";

  if (opts.solver != "cvc5") {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; solver backend '" + opts.solver + "' not built (cvc5 only)";
    return res;
  }

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_ABV");  // bit-vectors + arrays (M4 memory cut)
  if (opts.witness) {
    solver.setOption("produce-models", "true");
  }

  // Shared current-state ARRAY symbol per memory cut key, across both designs
  // (the "collapse corresponding memories" assumption). Built once; the encoder
  // reuses the symbol for the matching memory in each design.
  auto build_shared_mems = [&](std::string_view tag) {
    absl::flat_hash_map<std::string, cvc5::Term> sm;
    absl::flat_hash_map<std::string, int>        occ;
    auto                                         add = [&](hhds::Graph* g) {
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
          continue;
        }
        Mem_sig sig = read_mem_sig(node);
        if (sig.bits <= 0 || sig.size <= 0) {
          continue;
        }
        std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits) + ":r" + std::to_string(sig.n_rd)
                       + "w" + std::to_string(sig.n_wr);
        std::string key = mem_state_key(sig, occ[sg]++);
        if (sm.count(key)) {
          continue;
        }
        cvc5::Sort asort = tm.mkArraySort(tm.mkBitVectorSort(static_cast<uint32_t>(sig.addr_w)),
                                          tm.mkBitVectorSort(static_cast<uint32_t>(sig.bits)));
        sm[key]          = tm.mkConst(asort, std::string(tag) + key);
      }
    };
    add(ref);
    add(impl);
    return sm;
  };

  // ── BMC engine: unroll N cycles from the reset state ──────────────────────
  // The single-step inductive miter (below) assumes an arbitrary equal current
  // state, so it false-REFUTEs on UNREACHABLE states where the two front-ends
  // resolve a don't-care differently. BMC instead starts from the reset state
  // (each flop's constant `initial`, or a shared fresh symbol for a reset-less
  // flop) and chains each design's own next-state forward, so only states
  // reachable within N cycles are explored — sound for that bound (this is what
  // yosys' own lgcheck does with its bounded miter).
  if (opts.engine == "bmc") {
    const int N = opts.bound > 0 ? opts.bound : 20;
    Encoder   enc(tm);

    struct In {
      int  w;
      bool sgn;
    };
    absl::flat_hash_map<std::string, In> ins;
    auto                                 collect_ins = [&](hhds::Graph* g) {
      auto gio = g->get_io();
      for (const auto& d : gio->get_input_pin_decls()) {
        if (ins.count(d.name)) {
          continue;
        }
        auto pin = g->get_input_pin(d.name);
        int  w   = real_width_io(pin, *gio, d.name);
        if (w == 0) {
          w = 1;
        }
        bool sgn  = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
        ins[d.name] = In{w, sgn};
      }
    };
    collect_ins(ref);
    collect_ins(impl);

    // Reset-phase setup. A PRIMARY reset input is an input name that drives some
    // flop's reset_pin directly; its asserted level is 0 when that flop is
    // active-low (negreset), else 1. (Derived resets — driven by a mux, not a
    // primary input — are not directly controllable and are simply left free.)
    absl::flat_hash_map<std::string, bool> reset_negset;  // name -> negreset
    auto                                   collect_resets = [&](hhds::Graph* g) {
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto rst_d = graph_util::get_driver_of_sink_name(node, "reset_pin");
        if (rst_d.is_invalid() || !graph_util::is_graph_input_pin(rst_d)) {
          continue;
        }
        auto nm = std::string(graph_util::pin_name_of(rst_d));
        if (nm.empty() || !ins.count(nm)) {
          continue;
        }
        bool negreset = false;
        if (auto neg_d = graph_util::get_driver_of_sink_name(node, "negreset");
            !neg_d.is_invalid() && graph_util::is_const_pin(neg_d)) {
          negreset = !graph_util::hydrate_const(neg_d).is_known_false();
        }
        reset_negset[nm] = negreset;
      }
    };
    // Canonical reset-name test (token-aware to avoid matching "first"/"burst").
    // negreset (active-low) inferred from an _n / _ni / n suffix.
    auto reset_name_polarity = [](const std::string& nm, bool& negreset) -> bool {
      std::string lc = nm;
      for (auto& c : lc) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      bool tok_match = false;
      size_t start   = 0;
      for (size_t i = 0; i <= lc.size(); ++i) {
        if (i == lc.size() || lc[i] == '_') {
          std::string tok = lc.substr(start, i - start);
          if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset"
              || tok == "nrst" || tok == "nreset" || tok == "por") {
            tok_match = true;
          }
          start = i + 1;
        }
      }
      if (!tok_match) {
        return false;
      }
      auto ends = [&](std::string_view s) { return lc.size() >= s.size() && lc.compare(lc.size() - s.size(), s.size(), s) == 0; };
      negreset = ends("_n") || ends("_ni") || ends("_n_i") || ends("_ni_i") || lc == "rstn" || lc == "resetn" || ends("nrst")
                 || ends("nreset");
      return true;
    };

    const bool phase_reset = opts.phase == "reset";
    const bool phase_run   = opts.phase == "run";
    if (phase_reset || phase_run) {
      if (!opts.reset.empty()) {
        // Explicit override: authoritative list of reset inputs + polarity.
        std::string spec = opts.reset;
        size_t      p    = 0;
        while (p < spec.size()) {
          size_t      comma = spec.find(',', p);
          std::string tok   = spec.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
          p                 = comma == std::string::npos ? spec.size() : comma + 1;
          if (tok.empty()) {
            continue;
          }
          bool        negreset = false;
          std::string nm       = tok;
          if (auto colon = tok.find(':'); colon != std::string::npos) {
            nm           = tok.substr(0, colon);
            auto pol     = tok.substr(colon + 1);
            negreset     = (pol == "lo" || pol == "low" || pol == "n" || pol == "0");
          } else {
            reset_name_polarity(nm, negreset);  // infer from suffix
          }
          if (ins.count(nm)) {
            reset_negset[nm] = negreset;
          }
        }
      } else {
        // Auto: structural async resets (reset_pin-driven inputs)...
        collect_resets(ref);
        collect_resets(impl);
        // ...plus canonical reset-named inputs (sync resets folded into din).
        for (const auto& [name, info] : ins) {
          if (reset_negset.count(name)) {
            continue;
          }
          bool negreset = false;
          if (reset_name_polarity(name, negreset)) {
            reset_negset[name] = negreset;
          }
        }
      }
    }
    const int reset_hold = phase_run ? (opts.reset_cycles > 0 ? opts.reset_cycles : 1) : 0;
    const int total_cyc  = N + reset_hold;  // run: prologue + checked window

    // state[0]: reset initial per flop (shared equal across designs). In `run`
    // phase we instead start from a FRESH arbitrary equal state and let the
    // reset-hold prologue drive both designs into their reset state — so the
    // checked window genuinely exercises free-running behavior from reset.
    absl::flat_hash_map<std::string, Val> ref_state, impl_state;
    {
      absl::flat_hash_map<std::string, int> fw;
      absl::flat_hash_map<std::string, Val> init;
      auto                                  collect_flops = [&](hhds::Graph* g) {
        for (auto node : g->forward_class()) {
          if (graph_util::type_op_of(node) != Ntype_op::Flop) {
            continue;
          }
          auto q = node.get_driver_pin(0);
          if (q.is_invalid()) {
            continue;
          }
          auto key = flop_state_key(*g, node);
          int  w   = real_width(q);
          if (w == 0) {
            w = 1;
          }
          fw[key] = w;
          if (!init.count(key)) {
            if (auto iv = flop_initial(tm, node, w)) {
              init[key] = *iv;
            }
          }
        }
      };
      collect_flops(ref);
      collect_flops(impl);
      for (const auto& [key, w] : fw) {
        Val v;
        if (!phase_run && init.count(key)) {
          v = init.at(key);
        } else {
          v = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "s0_" + key), w, false};
        }
        ref_state[key]  = v;
        impl_state[key] = v;
      }
    }

    // Memory state[0]: one shared array per cut key (corresponding memories
    // collapse to the same initial contents); threaded forward like flop state.
    absl::flat_hash_map<std::string, cvc5::Term> ref_mem = build_shared_mems("m0_");
    absl::flat_hash_map<std::string, cvc5::Term> impl_mem = ref_mem;

    cvc5::Term bad;
    int        uid = 0;  // unique suffix for fresh per-cycle state vars
    for (int cyc = 0; cyc < total_cyc; ++cyc) {
      // `run` phase: the first `reset_hold` cycles hold reset asserted and are
      // NOT mitered (prologue); the rest are the checked window. `reset` phase:
      // reset asserted on every cycle and every cycle is checked.
      const bool checking = phase_run ? (cyc >= reset_hold) : true;
      // Current state for cycle i>0 is a FRESH symbol per flop, pinned to the
      // previous cycle's next-state by a flat equality assertion. Substituting
      // the next-state term directly instead would re-nest the whole transition
      // every cycle (a counter's state becomes (((q+1)+1)...) — cvc5's recursive
      // term walk then stack-overflows past ~13 steps). Fresh-var + assert keeps
      // every cycle's terms shallow; the unrolling lives in the assertion set.
      if (cyc > 0) {
        absl::flat_hash_map<std::string, Val> ns_ref, ns_impl;
        auto pin_state = [&](const absl::flat_hash_map<std::string, Val>& prev_next,
                             absl::flat_hash_map<std::string, Val>&       cur) {
          for (const auto& [key, pv] : prev_next) {
            cvc5::Term s = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(pv.width)), "st" + std::to_string(uid++));
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv.term}));
            cur[key] = Val{s, pv.width, pv.is_signed};
          }
        };
        pin_state(ref_state, ns_ref);
        pin_state(impl_state, ns_impl);
        ref_state  = std::move(ns_ref);
        impl_state = std::move(ns_impl);

        // Same fresh-var pinning for memory arrays (keeps each cycle's array
        // terms shallow; the unrolling lives in the equality assertions).
        absl::flat_hash_map<std::string, cvc5::Term> nm_ref, nm_impl;
        auto pin_mem = [&](const absl::flat_hash_map<std::string, cvc5::Term>& prev_next,
                           absl::flat_hash_map<std::string, cvc5::Term>&       cur) {
          for (const auto& [key, pv] : prev_next) {
            cvc5::Term s = tm.mkConst(pv.getSort(), "ma" + std::to_string(uid++));
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv}));
            cur[key] = s;
          }
        };
        pin_mem(ref_mem, nm_ref);
        pin_mem(impl_mem, nm_impl);
        ref_mem  = std::move(nm_ref);
        impl_mem = std::move(nm_impl);
      }

      absl::flat_hash_map<std::string, Val> sh_ref, sh_impl;
      for (const auto& [name, info] : ins) {
        cvc5::Term t = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(info.w)), "c" + std::to_string(cyc) + "_" + name);
        // Phase control: pin a primary reset input to its asserted level during
        // the reset phase / run-prologue, and to its deasserted level in the
        // run-checked window. (active-low reset -> asserted=0, deasserted=all-1.)
        if (auto rit = reset_negset.find(name); rit != reset_negset.end()) {
          const bool assert_reset = phase_reset || (phase_run && cyc < reset_hold);
          const bool negreset     = rit->second;
          // asserted level: 0 if active-low else all-ones; deasserted is the dual.
          const bool drive_zero = assert_reset ? negreset : !negreset;
          cvc5::Term lvl        = drive_zero ? tm.mkBitVector(static_cast<uint32_t>(info.w), 0)
                                             : tm.mkTerm(cvc5::Kind::BITVECTOR_NOT,
                                                         {tm.mkBitVector(static_cast<uint32_t>(info.w), 0)});
          solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {t, lvl}));
        }
        Val v{t, info.w, info.sgn};
        sh_ref[name]  = v;
        sh_impl[name] = v;
      }
      for (const auto& [k, v] : ref_state) {
        sh_ref[k] = v;
      }
      for (const auto& [k, v] : impl_state) {
        sh_impl[k] = v;
      }

      Encoded re = enc.encode(ref, &sh_ref, "r" + std::to_string(cyc) + "_", &ref_mem);
      if (!re.ok) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; ref encode failed: " + re.error;
        return res;
      }
      Encoded ie = enc.encode(impl, &sh_impl, "i" + std::to_string(cyc) + "_", &impl_mem);
      if (!ie.ok) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; impl encode failed: " + ie.error;
        return res;
      }
      for (const auto& [l, r] : re.equalities) {
        solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
      }
      for (const auto& [l, r] : ie.equalities) {
        solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
      }

      if (checking) {
        for (const auto& [name, rv] : re.outputs) {
          if (!name.empty() && name[0] == '\x01') {
            continue;  // next-state, not a primary output
          }
          auto it = ie.outputs.find(name);
          if (it == ie.outputs.end()) {
            continue;
          }
          int        w    = std::max(rv.width, it->second.width);
          cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {fit_to(tm, rv, w), fit_to(tm, it->second, w)});
          bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
        }
      }

      // Stash each design's next-state terms (built on this cycle's shallow
      // current-state symbols) for the next cycle's fresh-var pinning.
      absl::flat_hash_map<std::string, Val> rn, in;
      for (const auto& [name, rv] : re.outputs) {
        if (name.rfind("\x01nxt:", 0) == 0) {
          rn[name.substr(5)] = rv;
        }
      }
      for (const auto& [name, iv] : ie.outputs) {
        if (name.rfind("\x01nxt:", 0) == 0) {
          in[name.substr(5)] = iv;
        }
      }
      ref_state  = std::move(rn);
      impl_state = std::move(in);
      ref_mem    = std::move(re.next_mem);
      impl_mem   = std::move(ie.next_mem);
    }

    res.detail = "solver=cvc5 (bmc, phase=" + opts.phase + ", " + std::to_string(N) + " checked steps"
               + (reset_hold ? " after " + std::to_string(reset_hold) + " reset-hold" : "")
               + (reset_negset.empty() && opts.phase != "free" ? "; WARNING no primary reset input found" : "") + ")";
    if (bad.isNull()) {
      res.verdict = Verdict::Proven;
      return res;
    }
    solver.assertFormula(bad);
    cvc5::Result r = solver.checkSat();
    if (r.isUnsat()) {
      res.verdict = Verdict::Proven;
    } else if (r.isSat()) {
      res.verdict = Verdict::Refuted;
    } else {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; checkSat returned unknown";
    }
    return res;
  }

  // Shared primary inputs: one symbolic BV per input name (union of both
  // designs). Both encodings reuse these terms, so "equal inputs" is structural
  // (the miter constrains nothing on the inputs -> they range freely).
  absl::flat_hash_map<std::string, Val> shared;
  auto                                  add_inputs = [&](hhds::Graph* g) {
    auto gio = g->get_io();
    for (const auto& d : gio->get_input_pin_decls()) {
      if (shared.count(d.name)) {
        continue;
      }
      auto pin = g->get_input_pin(d.name);
      int  w   = real_width_io(pin, *gio, d.name);
      if (w == 0) {
        w = 1;
      }
      bool sgn       = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), d.name);
      shared[d.name] = Val{t, w, sgn};
    }
  };
  add_inputs(ref);
  add_inputs(impl);

  // Shared current-state symbols: one symbolic BV per corresponding flop, keyed
  // by its cross-design state key (source span preferred). Both encodings reuse
  // these, so the miter assumes equal current state and proves equal next state
  // + outputs (the M2 register-correspondence inductive step).
  auto add_flops = [&](hhds::Graph* g) {
    for (auto node : g->forward_class()) {
      if (graph_util::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      std::string key = flop_state_key(*g, node);
      if (shared.count(key)) {
        continue;
      }
      int w = real_width(q);
      if (w == 0) {
        w = 1;
      }
      bool       sgn = !graph_util::is_unsign(q);
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), key);
      shared[key]    = Val{t, w, sgn};
    }
  };
  add_flops(ref);
  add_flops(impl);

  // Shared current-state memory arrays (M4 cut): corresponding memories collapse
  // to one array symbol, so the step proves equal next-state contents + douts.
  absl::flat_hash_map<std::string, cvc5::Term> shared_mems = build_shared_mems("s_");

  Encoder enc(tm);
  Encoded re = enc.encode(ref, &shared, "", &shared_mems);
  if (!re.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; ref encode failed: " + re.error;
    return res;
  }
  Encoded ie = enc.encode(impl, &shared, "", &shared_mems);
  if (!ie.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; impl encode failed: " + ie.error;
    return res;
  }
  // Tie deferred memory-read douts to their select(array, addr) values.
  for (const auto& [l, r] : re.equalities) {
    solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
  }
  for (const auto& [l, r] : ie.equalities) {
    solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
  }

  // Miter over common outputs: bad = Or_i (ref_out_i != impl_out_i).
  cvc5::Term bad;
  for (const auto& [name, rv] : re.outputs) {
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in ref but not impl";
      return res;
    }
    int        w    = std::max(rv.width, it->second.width);
    cvc5::Term a    = fit_to(tm, rv, w);
    cvc5::Term b    = fit_to(tm, it->second, w);
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {a, b});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
  }
  // also flag impl-only outputs
  for (const auto& [name, iv] : ie.outputs) {
    if (!re.outputs.count(name)) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in impl but not ref";
      return res;
    }
  }
  // Memory next-state contents: corresponding memories must hold equal contents
  // after the step (DISTINCT over array sort). A memory present in only one side
  // is an unmatched cut -> sound Unknown.
  for (const auto& [key, rmem] : re.next_mem) {
    auto it = ie.next_mem.find(key);
    if (it == ie.next_mem.end()) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; memory '" + key + "' present in ref but not impl";
      return res;
    }
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rmem, it->second});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
  }
  for (const auto& [key, imem] : ie.next_mem) {
    if (!re.next_mem.count(key)) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; memory '" + key + "' present in impl but not ref";
      return res;
    }
  }
  if (bad.isNull()) {
    res.verdict  = Verdict::Proven;  // no outputs to compare -> vacuously equal
    res.detail  += "; no comparable outputs";
    return res;
  }

  // Combinational equivalence: the miter is UNSAT iff the designs agree on every
  // common output for all inputs. No state, no unrolling -- a single SMT query.
  // (opts.engine / opts.bound are sequential-model-checking knobs, unused here.)
  solver.assertFormula(bad);
  cvc5::Result r = solver.checkSat();

  if (r.isUnsat()) {
    res.verdict = Verdict::Proven;
  } else if (r.isSat()) {
    res.verdict = Verdict::Refuted;
    if (opts.witness) {
      // Name the diverging output(s) first — the actionable part of the CEX —
      // then the satisfying input/current-state assignment.
      std::string diffs;
      for (const auto& [name, rv] : re.outputs) {
        auto it = ie.outputs.find(name);
        if (it == ie.outputs.end()) {
          continue;
        }
        int        w    = std::max(rv.width, it->second.width);
        cvc5::Term rval = solver.getValue(fit_to(tm, rv, w));
        cvc5::Term ival = solver.getValue(fit_to(tm, it->second, w));
        if (!rval.isNull() && !ival.isNull() && rval.getBitVectorValue(10) != ival.getBitVectorValue(10)) {
          if (!diffs.empty()) {
            diffs += ", ";
          }
          diffs += name + "(ref=" + rval.getBitVectorValue(10) + " impl=" + ival.getBitVectorValue(10) + ")";
        }
      }
      std::string w;
      for (const auto& [name, v] : shared) {
        cvc5::Term val = solver.getValue(v.term);
        if (val.isNull()) {
          continue;
        }
        if (!w.empty()) {
          w += ", ";
        }
        w += name + "=" + val.getBitVectorValue(10);
      }
      res.witness = (diffs.empty() ? "" : "diff " + diffs + " @ ") + w;
    }
  } else {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; checkSat returned unknown";
  }
  return res;
}

}  // namespace livehd::lec
