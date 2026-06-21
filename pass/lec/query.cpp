// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "query.hpp"

#include <cvc5/cvc5.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "encode.hpp"
#include "node_util.hpp"

namespace livehd::lec {

namespace {
// Strip the encoder's internal control prefixes so an unmatched cut point / output
// reads cleanly in a diagnostic. Next-state outputs are "\x01nxt:<hier>", other
// synthetic keys lead with a control byte; primary outputs are already clean.
std::string display_name(std::string_view name) {
  if (name.rfind("\x01nxt:", 0) == 0) {
    return "nxt:" + std::string(name.substr(5));
  }
  if (!name.empty() && static_cast<unsigned char>(name.front()) < 0x20) {
    // "\x01n:foo" / "\x01m:..." etc. -> drop the control byte and the kind tag.
    auto rest = name.substr(1);
    if (rest.size() >= 2 && rest[1] == ':') {
      return std::string(rest.substr(2));
    }
    return std::string(rest);
  }
  return std::string(name);
}

// Join a (sorted) token list, capping the count so a 1000-flop bank doesn't bury
// the message; appends "(+N more)" when truncated.
std::string join_capped(std::vector<std::string> toks, size_t cap = 24) {
  std::sort(toks.begin(), toks.end());
  std::string out;
  size_t      shown = std::min(cap, toks.size());
  for (size_t i = 0; i < shown; ++i) {
    if (!out.empty()) {
      out += ", ";
    }
    out += toks[i];
  }
  if (toks.size() > cap) {
    out += ", (+" + std::to_string(toks.size() - cap) + " more)";
  }
  return out;
}
}  // namespace

std::vector<std::pair<std::string, std::string>> parse_match_pairs(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t                                           i = 0;
  while (i < text.size()) {
    size_t end = text.find_first_of(",;\n", i);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(i, end - i);
    i                     = end + 1;
    // trim surrounding whitespace
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
      line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
      line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    // split on '=' (preferred) or whitespace into exactly two names
    size_t sep = line.find('=');
    if (sep == std::string_view::npos) {
      sep = line.find_first_of(" \t");
    }
    if (sep == std::string_view::npos) {
      continue;  // a lone token is not a correspondence; ignore
    }
    auto trim = [](std::string_view s) {
      while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '=')) {
        s.remove_prefix(1);
      }
      while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '=')) {
        s.remove_suffix(1);
      }
      return s;
    };
    std::string_view a = trim(line.substr(0, sep));
    std::string_view b = trim(line.substr(sep + 1));
    if (!a.empty() && !b.empty()) {
      out.emplace_back(std::string(a), std::string(b));
    }
  }
  return out;
}

Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                         const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  Query_result res;
  res.detail = "solver=" + opts.solver + " (cvc5 direct, flop-cut inductive miter)";

  if (opts.solver != "cvc5") {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; solver backend '" + opts.solver + "' not built (cvc5 only)";
    return res;
  }

  // `full` = require equivalence in BOTH the during-reset (just_reset) and the
  // free-running (after_reset) windows. Run them as two self-contained sub-queries
  // (each builds its own cvc5 solver, so they are independent) and AND the
  // verdicts. Sequential + short-circuit: a bring-up bug usually trips just_reset
  // first, and skipping the after_reset solve once it has already failed is faster
  // than running both. They could run on two threads (hhds reads are thread-safe),
  // but cvc5's cross-instance thread-safety is unverified, so we keep it serial.
  if (opts.engine == "bmc" && opts.phase == "full") {
    Lec_options  o1 = opts;
    o1.phase        = "just_reset";
    Query_result r1 = prove_equal(ref, impl, o1, sub_lib);
    if (r1.verdict != Verdict::Proven) {
      r1.detail = "full / just_reset stage: " + r1.detail;
      return r1;
    }
    Lec_options  o2 = opts;
    o2.phase        = "after_reset";
    Query_result r2 = prove_equal(ref, impl, o2, sub_lib);
    r2.detail       = (r2.verdict == Verdict::Proven
                           ? "full: equivalent in BOTH just_reset and after_reset; after_reset stage: "
                           : "full / after_reset stage (just_reset already PROVEN): ")
                + r2.detail;
    return r2;
  }

  // Explicit state-correspondence (lec.match): canon(impl_name) -> canon(ref_name),
  // so a flop the user paired with a differently-named flop on the other design
  // collapses onto one shared cut key (the ref-side canon). `eff(hier)` is the
  // canonical key used everywhere a flop is keyed (shared symbols + encoder).
  Io_name_map<std::string> name_alias;
  for (const auto& [ref_name, impl_name] : opts.match) {
    std::string rc = canon_flop_name(ref_name);
    std::string ic = canon_flop_name(impl_name);
    if (!ic.empty() && ic != rc) {
      name_alias[ic] = rc;
    }
  }
  auto eff = [&](std::string_view hier) -> std::string {
    std::string c = canon_flop_name(hier);
    if (auto it = name_alias.find(c); it != name_alias.end()) {
      return it->second;
    }
    return c;
  };

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_ABV");  // bit-vectors + arrays (M4 memory cut)

  // Per-checkSat wall-clock bound (lec.timeout seconds; 0 = unbounded). Hard
  // nonlinear miters (a chain of two multiplies — associativity, distributivity,
  // 3-way commutativity at >=16 bits) make cvc5's bit-blast never return. Without
  // this, `lhd lec` freezes forever. A bound makes the timed-out checkSat come
  // back `unknown`, which both checkSat sites already map to Verdict::Unknown — a
  // SOUND degrade (never a false Proven/Refuted). tlimit-per resets per query, so
  // it bounds each decisive checkSat in the comb / inductive / BMC frames.
  if (opts.timeout > 0) {
    solver.setOption("tlimit-per", std::to_string(static_cast<long long>(opts.timeout) * 1000));
  }

  // Bit-blasting BV sub-solver. cvc5's default LAZY bitblast solver can return a
  // spurious SAT on these wide multi-output arithmetic miters: it reports SAT
  // while the underlying CaDiCaL is actually UNSAT, so the miter false-REFUTEs
  // and a witness query (getValue) then aborts inside CaDiCaL ("can only get
  // value in satisfied state"). The EAGER internal bit-blaster solves the same
  // miters correctly and keeps the SAT assignment, so witness extraction is
  // safe — but it has no theory of arrays, so restrict it to array-free queries.
  // Memory cuts (M4) introduce array symbols; those keep the default solver.
  bool has_mem = false;
  for (auto* g : {ref, impl}) {
    for (auto node : g->forward_class()) {
      if (graph_util::type_op_of(node) == Ntype_op::Memory) {
        has_mem = true;
        break;
      }
    }
    if (has_mem) {
      break;
    }
  }
  if (!has_mem) {
    solver.setOption("bv-solver", "bitblast-internal");
  }
  if (opts.witness) {
    solver.setOption("produce-models", "true");
  }

  // Shared current-state ARRAY symbol per memory cut key, across both designs
  // (the "collapse corresponding memories" assumption). Built once; the encoder
  // reuses the symbol for the matching memory in each design.
  auto build_shared_mems = [&](std::string_view tag) {
    Io_name_map<cvc5::Term> sm;
    Io_name_map<int>        occ;
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

  // Shared output symbols for BLACKBOX Sub instances (missing/empty defs). Each
  // blackbox output becomes one free symbol SHARED across both designs, keyed
  // "<defname>#<occ>:<port>" (matches the encoder). Built at the max output width
  // seen across the two designs (bit-width trap). A Sub is a blackbox unless the
  // resolution library has a purely-combinational def for it (mirrors encode.cpp).
  auto build_shared_bbox = [&]() {
    Io_name_map<Val>  bb;
    Io_name_map<int>  bw;   // key -> max real width
    Io_name_map<bool> bsg;  // key -> signed
    auto                                   add = [&](hhds::Graph* g) {
      Io_name_map<int> occ;
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        bool blackbox = true;
        if (sub_lib != nullptr) {
          if (auto git = sub_lib->find(node.get_subnode_gid()); git != sub_lib->end() && git->second != nullptr) {
            blackbox = false;
            for (auto dn : git->second->forward_class()) {
              auto dop = graph_util::type_op_of(dn);
              if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
                blackbox = true;
                break;
              }
            }
          }
        }
        if (!blackbox) {
          continue;
        }
        auto                                            sub_io = node.get_subnode_io();
        std::string                                     defname(sub_io->get_name());
        std::string                                     bk = defname + "#" + std::to_string(occ[defname]++);
        absl::flat_hash_map<hhds::Port_id, std::string> out_name;
        for (const auto& d : sub_io->get_output_pin_decls()) {
          out_name[sub_io->get_output_port_id(d.name)] = d.name;
        }
        for (const auto& e : node.out_edges()) {
          auto        dp   = e.driver;
          auto        nit  = out_name.find(dp.get_port_id());
          std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
          std::string key  = bk + ":" + port;
          int         w    = real_width(dp);
          if (w == 0) {
            w = 1;
          }
          if (auto it = bw.find(key); it == bw.end() || w > it->second) {
            bw[key]  = w;
            bsg[key] = !graph_util::is_unsign(dp);
          }
        }
      }
    };
    add(ref);
    add(impl);
    for (const auto& [key, w] : bw) {
      bb[key] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "bb:" + key), w, bsg[key]};
    }
    return bb;
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
    const int N = opts.bound > 0 ? opts.bound : 6;
    Encoder   enc(tm);
    enc.set_sub_lib(sub_lib);
    enc.set_name_alias(&name_alias);
    Io_name_map<Val> shared_bbox = build_shared_bbox();
    enc.set_shared_bbox(&shared_bbox);

    struct In {
      int  w;
      bool sgn;
    };
    Io_name_map<In> ins;
    auto                                 collect_ins = [&](hhds::Graph* g) {
      auto gio = g->get_io();
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin = g->get_input_pin(d.name);
        int  w   = real_width_io(pin, *gio, d.name);
        if (w == 0) {
          w = 1;
        }
        if (auto it = ins.find(d.name); it != ins.end() && it->second.w >= w) {
          continue;  // keep the max-width view across both designs (bit-width trap)
        }
        bool sgn    = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
        ins[d.name] = In{w, sgn};
      }
    };
    collect_ins(ref);
    collect_ins(impl);

    // Reset-phase setup. A PRIMARY reset input is an input name that drives some
    // flop's reset_pin directly; its asserted level is 0 when that flop is
    // active-low (negreset), else 1. (Derived resets — driven by a mux, not a
    // primary input — are not directly controllable and are simply left free.)
    Io_name_map<bool> reset_negset;  // name -> negreset
    auto                                   collect_resets = [&](hhds::Graph* g) {
      for (auto node : g->forward_hier()) {  // descend hierarchy: flops at every level
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

    const bool phase_reset = opts.phase == "just_reset";
    const bool phase_run   = opts.phase == "after_reset";
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
    Io_name_map<Val> ref_state, impl_state;
    {
      Io_name_map<int> fw;
      Io_name_map<Val> init;
      auto                                  collect_flops = [&](hhds::Graph* g) {
        for (auto node : g->forward_hier()) {  // descend hierarchy: cut flops at every level
          if (graph_util::type_op_of(node) != Ntype_op::Flop) {
            continue;
          }
          auto q = node.get_driver_pin(0);
          if (q.is_invalid()) {
            continue;
          }
          auto key = eff(node.get_hier_name());  // hier correspondence key (matches encode())
          int  w   = real_width(q);
          if (w == 0) {
            w = 1;
          }
          if (auto it = fw.find(key); it == fw.end() || w > it->second) {
            fw[key] = w;  // max-width across both designs (bit-width trap)
          }
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
    Io_name_map<cvc5::Term> ref_mem = build_shared_mems("m0_");
    Io_name_map<cvc5::Term> impl_mem = ref_mem;

    cvc5::Term bad;
    int        uid = 0;  // unique suffix for fresh per-cycle state vars

    // Witness recording: keep each checked cycle's per-output (ref,impl) terms and
    // each cycle's primary-input symbol, so a REFUTED BMC run can report WHICH
    // output diverges at WHICH step under WHAT input trace (the BMC engine, unlike
    // the inductive miter, otherwise emits no counterexample — leaving `lhd lec`
    // with nothing to iterate on). Populated only when witnesses are requested.
    struct Wit_out {
      int         cyc;
      std::string name;
      cvc5::Term  rv;
      cvc5::Term  iv;
    };
    struct Wit_in {
      int         cyc;
      std::string name;
      cvc5::Term  t;
    };
    std::vector<Wit_out> wit_outs;
    std::vector<Wit_in>  wit_ins;

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
        Io_name_map<Val> ns_ref, ns_impl;
        auto pin_state = [&](const Io_name_map<Val>& prev_next,
                             Io_name_map<Val>&       cur) {
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
        Io_name_map<cvc5::Term> nm_ref, nm_impl;
        auto pin_mem = [&](const Io_name_map<cvc5::Term>& prev_next,
                           Io_name_map<cvc5::Term>&       cur) {
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

      Io_name_map<Val> sh_ref, sh_impl;
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
        if (opts.witness) {
          wit_ins.push_back({cyc, name, t});
        }
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
          cvc5::Term rfit = fit_to(tm, rv, w);
          cvc5::Term ifit = fit_to(tm, it->second, w);
          cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rfit, ifit});
          bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
          if (opts.witness) {
            wit_outs.push_back({cyc, name, rfit, ifit});
          }
        }
      }

      // Stash each design's next-state terms (built on this cycle's shallow
      // current-state symbols) for the next cycle's fresh-var pinning.
      Io_name_map<Val> rn, in;
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
               + (reset_negset.empty() && opts.phase != "free_toreset" ? "; WARNING no primary reset input found" : "") + ")";
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
      // Build the counterexample: the EARLIEST checked step that diverges (the
      // origin of the bug — later steps just inherit it), the diverging outputs
      // there (ref vs impl decimal values), and the primary-input trace driving
      // them up to that step. This is what `lhd lec` surfaces so the user can
      // localize the mismatch and replay it.
      auto join_ordered = [](const std::vector<std::string>& toks, size_t cap) {
        std::string out;
        size_t      shown = std::min(cap, toks.size());
        for (size_t i = 0; i < shown; ++i) {
          if (!out.empty()) {
            out += ", ";
          }
          out += toks[i];
        }
        if (toks.size() > cap) {
          out += ", (+" + std::to_string(toks.size() - cap) + " more)";
        }
        return out;
      };
      int first_bad = -1;
      for (const auto& o : wit_outs) {
        cvc5::Term rval = solver.getValue(o.rv);
        cvc5::Term ival = solver.getValue(o.iv);
        if (rval.isNull() || ival.isNull()) {
          continue;
        }
        if (rval.getBitVectorValue(10) != ival.getBitVectorValue(10) && (first_bad < 0 || o.cyc < first_bad)) {
          first_bad = o.cyc;
        }
      }
      if (first_bad >= 0) {
        std::vector<std::string> dtoks;
        for (const auto& o : wit_outs) {
          if (o.cyc != first_bad) {
            continue;
          }
          cvc5::Term rval = solver.getValue(o.rv);
          cvc5::Term ival = solver.getValue(o.iv);
          if (rval.isNull() || ival.isNull()) {
            continue;
          }
          auto rs = rval.getBitVectorValue(10);
          auto is = ival.getBitVectorValue(10);
          if (rs != is) {
            dtoks.push_back(display_name(o.name) + "(ref=" + rs + " impl=" + is + ")");
          }
        }
        std::sort(dtoks.begin(), dtoks.end());
        std::vector<std::string> itoks;
        for (const auto& in : wit_ins) {
          if (in.cyc > first_bad) {
            continue;  // trace only up to the first diverging step
          }
          cvc5::Term v = solver.getValue(in.t);
          if (v.isNull()) {
            continue;
          }
          std::string lbl = in.cyc < reset_hold ? ("rst" + std::to_string(in.cyc))
                                                : ("s" + std::to_string(in.cyc - reset_hold + 1));
          itoks.push_back(lbl + "." + in.name + "=" + v.getBitVectorValue(10));
        }
        std::sort(itoks.begin(), itoks.end());
        res.witness = "first divergence at checked step " + std::to_string(first_bad - reset_hold + 1) + "/"
                    + std::to_string(N) + ": " + join_ordered(dtoks, 32);
        if (!itoks.empty()) {
          res.witness += " @ inputs " + join_ordered(itoks, 64);
        }
      }
    } else {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; checkSat returned unknown";
      if (opts.timeout > 0) {
        res.detail += " (hit lec.timeout=" + std::to_string(opts.timeout) + "s; raise --set lec.timeout)";
      }
    }
    return res;
  }

  // Shared primary inputs: one symbolic BV per input name (union of both
  // designs). Both encodings reuse these terms, so "equal inputs" is structural
  // (the miter constrains nothing on the inputs -> they range freely). The symbol
  // is built at the MAX real-width seen across the two designs: the readers can
  // disagree on a bus's width by a sign-bit slot (the "bit-width trap"), and
  // sharing at the max keeps the extra bit FREE (sound) — sharing at the min
  // would constrain it (false PROVE), truncating it in the encoder drops it
  // (false REFUTE, e.g. an input 0x80 -> 0).
  Io_name_map<Val> shared;
  auto                                  add_inputs = [&](hhds::Graph* g) {
    auto gio = g->get_io();
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      int  w   = real_width_io(pin, *gio, d.name);
      if (w == 0) {
        w = 1;
      }
      if (auto it = shared.find(d.name); it != shared.end() && it->second.width >= w) {
        continue;  // already have an equal-or-wider shared symbol
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
    for (auto node : g->forward_hier()) {  // descend hierarchy: cut flops at every level
      if (graph_util::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      std::string key = eff(node.get_hier_name());  // hier correspondence key (matches encode())
      int         w   = real_width(q);
      if (w == 0) {
        w = 1;
      }
      if (auto it = shared.find(key); it != shared.end() && it->second.width >= w) {
        continue;  // keep the wider shared state symbol (see input note above)
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
  Io_name_map<cvc5::Term> shared_mems = build_shared_mems("s_");
  Io_name_map<Val>        shared_bbox = build_shared_bbox();

  // ── Memory <-> flop-bank correspondence bridge ────────────────────────────
  // A behavioral memory (one Memory cell / SMT array) on one side often appears
  // as a synthesized BANK of identically-shaped flops "<base>_0..<base>_{N-1}" on
  // the other — the register file is the canonical case (Pyrope lowers a dynamic
  // `reg regs:[32]u64` to a Memory; firtool flattens RegisterFile to 32 flops
  // `registers.regs_N`). The inductive flop-cut miter can't match a flop against an
  // array, so when one side has an unmatched Memory of size N and the other an
  // unmatched COMPLETE bank of N same-width flops, collapse the bank onto the
  // array: every flop_i shares select(A, i) as current state, and the post-cycle
  // miter compares flop_i's next state against select(A_next, i). `bridges` is
  // resolved here (current-state wiring) and consumed after encode (next-state).
  struct Bridge {
    std::string              mem_key;     // shared-array cut key
    int                      addr_w = 1;  // index width
    int                      bits   = 0;  // array element width
    bool                     mem_in_impl = true;  // which design owns the Memory
    std::vector<std::string> flop_keys;   // canon flop key per index 0..N-1
    std::vector<int>         flop_w;       // real width per index
    std::vector<bool>        flop_sgn;
  };
  std::vector<Bridge> bridges;
  {
    struct FlopRec {
      int  w;
      bool sgn;
    };
    auto collect_flops = [&](hhds::Graph* g) {
      Io_name_map<FlopRec> out;
      for (auto node : g->forward_hier()) {
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto q = node.get_driver_pin(0);
        if (q.is_invalid()) {
          continue;
        }
        int w = real_width(q);
        if (w == 0) {
          w = 1;
        }
        out[eff(node.get_hier_name())] = FlopRec{w, !graph_util::is_unsign(q)};
      }
      return out;
    };
    struct MemRec {
      Mem_sig sig;
    };
    auto collect_mems = [&](hhds::Graph* g) {
      Io_name_map<MemRec> out;
      Io_name_map<int>    occ;
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
          continue;
        }
        Mem_sig sig = read_mem_sig(node);
        if (sig.bits <= 0 || sig.size <= 0) {
          continue;
        }
        std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits) + ":r" + std::to_string(sig.n_rd) + "w"
                       + std::to_string(sig.n_wr);
        std::string key = mem_state_key(sig, occ[sg]++);
        out[key]        = MemRec{sig};
      }
      return out;
    };
    auto ref_flops  = collect_flops(ref);
    auto impl_flops = collect_flops(impl);
    auto ref_mems   = collect_mems(ref);
    auto impl_mems  = collect_mems(impl);

    // Banks among the flops that have NO counterpart on the other side: group keys
    // by stripping a trailing "_<idx>"; a base with a contiguous 0..N-1 of uniform
    // width is a bank candidate.
    auto detect_banks = [](const Io_name_map<FlopRec>& flops, const Io_name_map<FlopRec>& other) {
      struct Elt {
        std::string key;
        int         w;
        bool        sgn;
      };
      absl::flat_hash_map<std::string, absl::flat_hash_map<int, Elt>> by_base;
      for (const auto& [key, rec] : flops) {
        if (other.count(key)) {
          continue;  // already matches a flop on the other side -> not a bridge bank
        }
        auto us = key.rfind('_');
        if (us == std::string::npos || us + 1 >= key.size()) {
          continue;
        }
        std::string idx = key.substr(us + 1);
        if (idx.empty() || !std::all_of(idx.begin(), idx.end(), [](unsigned char c) { return std::isdigit(c); })) {
          continue;
        }
        by_base[key.substr(0, us)][std::stoi(idx)] = Elt{key, rec.w, rec.sgn};
      }
      struct Bank {
        int                      n;
        int                      w;
        bool                     sgn;
        std::vector<std::string> keys;
      };
      std::vector<Bank> banks;
      for (auto& [base, elts] : by_base) {
        int  n        = static_cast<int>(elts.size());
        bool complete = n > 1;
        int  w        = elts.begin()->second.w;
        bool sgn      = elts.begin()->second.sgn;
        for (int i = 0; i < n; ++i) {
          auto it = elts.find(i);
          if (it == elts.end() || it->second.w != w) {
            complete = false;
            break;
          }
        }
        if (!complete) {
          continue;
        }
        Bank b{n, w, sgn, {}};
        for (int i = 0; i < n; ++i) {
          b.keys.push_back(elts.at(i).key);
        }
        banks.push_back(std::move(b));
      }
      return banks;
    };

    // Pair a bank (one side) with an unmatched Memory of equal size on the other.
    auto pair_side = [&](const Io_name_map<FlopRec>& bank_flops, const Io_name_map<FlopRec>& other_flops,
                         const Io_name_map<MemRec>& bank_mems, const Io_name_map<MemRec>& mem_mems, bool mem_in_impl) {
      for (auto& b : detect_banks(bank_flops, other_flops)) {
        for (const auto& [mkey, mrec] : mem_mems) {
          if (bank_mems.count(mkey)) {
            continue;  // memory matches a memory on the bank side -> not a bridge
          }
          if (mrec.sig.size != b.n) {
            continue;
          }
          if (!shared_mems.count(mkey)) {
            continue;
          }
          Bridge br;
          br.mem_key     = mkey;
          br.addr_w      = mrec.sig.addr_w;
          br.bits        = mrec.sig.bits;
          br.mem_in_impl = mem_in_impl;
          br.flop_keys   = b.keys;
          br.flop_w.assign(b.keys.size(), b.w);
          br.flop_sgn.assign(b.keys.size(), b.sgn);
          bridges.push_back(std::move(br));
          break;  // one bank -> one memory
        }
      }
    };
    pair_side(ref_flops, impl_flops, ref_mems, impl_mems, /*mem_in_impl=*/true);   // ref bank <-> impl memory
    pair_side(impl_flops, ref_flops, impl_mems, ref_mems, /*mem_in_impl=*/false);  // impl bank <-> ref memory

    // Wire each bank flop's CURRENT state to select(A, i) so the two designs start
    // the step from the same register-file contents.
    for (auto& br : bridges) {
      cvc5::Term A = shared_mems.at(br.mem_key);
      for (size_t i = 0; i < br.flop_keys.size(); ++i) {
        cvc5::Term sel = tm.mkTerm(cvc5::Kind::SELECT, {A, tm.mkBitVector(static_cast<uint32_t>(br.addr_w), static_cast<uint64_t>(i))});
        Val        cur = Val{fit_to(tm, Val{sel, br.bits, false}, br.flop_w[i]), br.flop_w[i], br.flop_sgn[i]};
        shared[br.flop_keys[i]] = cur;
      }
    }
  }

  Encoder enc(tm);
  enc.set_sub_lib(sub_lib);
  enc.set_name_alias(&name_alias);
  enc.set_shared_bbox(&shared_bbox);
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

  // ── Bridge miter: bank-flop next state vs select(memory_next, i) ──────────
  // Consume the memory<->flop-bank bridges resolved above. The bank-flop next
  // states and the paired memory's next array are pulled from whichever design
  // owns each, and these keys are recorded so the generic miter below does NOT
  // re-report them as unmatched cut points.
  absl::flat_hash_set<std::string> bridged_ref_out, bridged_impl_out, bridged_ref_mem, bridged_impl_mem;
  std::vector<cvc5::Term>          bridge_diffs;
  for (const auto& br : bridges) {
    const Encoded& bank_enc = br.mem_in_impl ? re : ie;  // design holding the flop bank
    const Encoded& mem_enc  = br.mem_in_impl ? ie : re;  // design holding the Memory
    auto&          bank_out_set = br.mem_in_impl ? bridged_ref_out : bridged_impl_out;
    auto&          mem_set      = br.mem_in_impl ? bridged_impl_mem : bridged_ref_mem;
    auto           mem_it       = mem_enc.next_mem.find(br.mem_key);
    if (mem_it == mem_enc.next_mem.end()) {
      continue;  // memory had no next-state (shouldn't happen) -> leave unmatched
    }
    mem_set.insert(br.mem_key);
    for (size_t i = 0; i < br.flop_keys.size(); ++i) {
      std::string nxt_key = std::string("\x01nxt:") + br.flop_keys[i];
      bank_out_set.insert(nxt_key);
      auto fit_it = bank_enc.outputs.find(nxt_key);
      if (fit_it == bank_enc.outputs.end()) {
        continue;
      }
      cvc5::Term sel = tm.mkTerm(cvc5::Kind::SELECT,
                                 {mem_it->second, tm.mkBitVector(static_cast<uint32_t>(br.addr_w), static_cast<uint64_t>(i))});
      int        w   = std::max(br.flop_w[i], br.bits);
      cvc5::Term bnk = fit_to(tm, fit_it->second, w);
      cvc5::Term mem = fit_to(tm, Val{sel, br.bits, false}, w);
      bridge_diffs.push_back(tm.mkTerm(cvc5::Kind::DISTINCT, {bnk, mem}));
    }
  }

  // ── Structural correspondence + miter over the COMMON outputs ─────────────
  // Rather than bail on the FIRST unmatched cut point (which made `lhd lec`
  // un-iterable when two designs don't yet expose the same state set), collect
  // ALL unmatched outputs / memories on each side and STILL miter the ones that
  // DO correspond. A non-empty unmatched set blocks a definitive Proven (the
  // correspondence is incomplete), but the matched-portion result + per-output
  // divergences are exactly the signal needed to drive the design — and the LEC's
  // own cut-correspondence — toward agreement.
  cvc5::Term bad;
  for (const auto& t : bridge_diffs) {  // bank<->memory next-state equalities
    bad = bad.isNull() ? t : tm.mkTerm(cvc5::Kind::OR, {bad, t});
  }
  for (const auto& [name, rv] : re.outputs) {
    if (bridged_ref_out.count(name)) {
      continue;  // bank-flop next state -> compared via the memory bridge above
    }
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.unmatched_ref.push_back(display_name(name));
      continue;
    }
    int        w    = std::max(rv.width, it->second.width);
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {fit_to(tm, rv, w), fit_to(tm, it->second, w)});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
  }
  for (const auto& [name, iv] : ie.outputs) {
    if (bridged_impl_out.count(name)) {
      continue;
    }
    if (!re.outputs.count(name)) {
      res.unmatched_impl.push_back(display_name(name));
    }
  }
  // Memory next-state contents: corresponding memories must hold equal contents
  // after the step (DISTINCT over array sort). A memory present on only one side
  // is an unmatched cut — unless it was paired with a flop bank above.
  for (const auto& [key, rmem] : re.next_mem) {
    if (bridged_ref_mem.count(key)) {
      continue;
    }
    auto it = ie.next_mem.find(key);
    if (it == ie.next_mem.end()) {
      res.unmatched_ref.push_back("mem:" + display_name(key));
      continue;
    }
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rmem, it->second});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
  }
  for (const auto& [key, imem] : ie.next_mem) {
    if (bridged_impl_mem.count(key)) {
      continue;
    }
    if (!re.next_mem.count(key)) {
      res.unmatched_impl.push_back("mem:" + display_name(key));
    }
  }

  const bool incomplete = !res.unmatched_ref.empty() || !res.unmatched_impl.empty();
  if (!res.unmatched_ref.empty()) {
    res.detail += "; " + std::to_string(res.unmatched_ref.size()) + " ref-only cut point(s) {" + join_capped(res.unmatched_ref) + "}";
  }
  if (!res.unmatched_impl.empty()) {
    res.detail += "; " + std::to_string(res.unmatched_impl.size()) + " impl-only cut point(s) {" + join_capped(res.unmatched_impl) + "}";
  }

  if (bad.isNull()) {
    // Nothing common to compare. Vacuously equal only if the sets also fully
    // matched (both empty); otherwise the comparison is empty AND incomplete.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; no COMMON outputs to compare (correspondence incomplete)";
    } else {
      res.verdict  = Verdict::Proven;
      res.detail  += "; no comparable outputs";
    }
    return res;
  }

  // Combinational / inductive miter: UNSAT iff the designs agree on every COMMON
  // output for all inputs (and assumed-equal current state). One SMT query.
  solver.assertFormula(bad);
  cvc5::Result r = solver.checkSat();

  // Witness = diverging COMMON outputs + the satisfying assignment. Built on every
  // SAT (useful for iteration even when the verdict is gated to Unknown by an
  // incomplete correspondence). Collect-then-sort: `re.outputs`/`shared` are
  // flat_hash_maps with run-varying order, which would make the text irreproducible.
  auto build_witness = [&]() -> std::string {
    if (!opts.witness) {
      return {};
    }
    std::vector<std::string> diff_toks;
    for (const auto& [name, rv] : re.outputs) {
      auto it = ie.outputs.find(name);
      if (it == ie.outputs.end()) {
        continue;
      }
      int        w    = std::max(rv.width, it->second.width);
      cvc5::Term rval = solver.getValue(fit_to(tm, rv, w));
      cvc5::Term ival = solver.getValue(fit_to(tm, it->second, w));
      if (!rval.isNull() && !ival.isNull() && rval.getBitVectorValue(10) != ival.getBitVectorValue(10)) {
        diff_toks.push_back(display_name(name) + "(ref=" + rval.getBitVectorValue(10) + " impl=" + ival.getBitVectorValue(10) + ")");
      }
    }
    std::sort(diff_toks.begin(), diff_toks.end());
    std::string diffs;
    for (const auto& t : diff_toks) {
      if (!diffs.empty()) {
        diffs += ", ";
      }
      diffs += t;
    }
    std::vector<std::string> toks;
    for (const auto& [name, v] : shared) {
      cvc5::Term val = solver.getValue(v.term);
      if (val.isNull()) {
        continue;
      }
      toks.push_back(display_name(name) + "=" + val.getBitVectorValue(10));
    }
    std::sort(toks.begin(), toks.end());
    std::string w;
    for (const auto& t : toks) {
      if (!w.empty()) {
        w += ", ";
      }
      w += t;
    }
    return (diffs.empty() ? "" : "diff " + diffs + " @ ") + w;
  };

  if (r.isUnsat()) {
    // COMMON outputs agree. Proven only if the correspondence is also complete.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
    } else {
      res.verdict = Verdict::Proven;
    }
  } else if (r.isSat()) {
    res.witness = build_witness();
    // A divergence on a COMMON output is a genuine refutation when the
    // correspondence is complete. With unmatched cut points the engine cannot
    // attribute the divergence soundly (a matched ref output may read a ref-only
    // flop), so it reports Unknown but still surfaces the witness for iteration.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; matched portion DIFFERS (witness below; may be an artifact of the unmatched cut points)";
    } else {
      res.verdict = Verdict::Refuted;
    }
  } else {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; checkSat returned unknown";
    if (opts.timeout > 0) {
      res.detail += " (hit lec.timeout=" + std::to_string(opts.timeout) + "s; raise --set lec.timeout)";
    }
  }
  return res;
}

}  // namespace livehd::lec
