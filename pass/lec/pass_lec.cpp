// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include <print>
#include <string>
#include <string_view>

#include "diag.hpp"
#include "query.hpp"
#include "str_tools.hpp"

using namespace livehd;

static Pass_plugin plugin("pass_lec", Pass_lec::setup);

Pass_lec::Pass_lec(const Eprp_var& var) : Pass("pass.lec", var) {}

namespace {
bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }
}  // namespace

void Pass_lec::setup() {
  Eprp_method m("pass.lec",
                "Relational equivalence: prove_equal(ref=graph0, impl=graph1) under assume_equal(inputs)",
                &Pass_lec::lec);
  m.add_label_optional("engine",
                       "discharge engine: auto (default; parallel portfolio: race ind+bmc as forked "
                       "workers, take the first trustworthy verdict — ind-Proven=PASS, bmc-Refuted=FAIL) | "
                       "bmc | ind (inductive flop-cut miter) | ic3",
                       "auto");
  m.add_label_optional("solver",
                       "equivalence backend: cvc5 (default, in-process SMT) | bitwuzla (in-process SMT) | "
                       "lgyosys (kernel-routed yosys/lgcheck; reads Verilog directly)",
                       "cvc5");
  m.add_label_optional("gold_reader",
                       "lgyosys backend only: reader for the REFERENCE side — verilog (default; yosys "
                       "read_verilog -sv) | slang (load the yosys-slang plugin and read_slang; needed for "
                       "SystemVerilog packed-struct sources like CIRCT/firtool output)",
                       "verilog");
  m.add_label_optional("gold_x",
                       "reference-side X semantics (cvc5 engines; the analogue of yosys miter "
                       "-ignore_gold_x): ignore (default; a ref constant's ?/X bits are don't-care — "
                       "excluded from the compare, any impl value accepted) | zero (legacy: ?/X bits "
                       "silently concretized to 0 on both sides)",
                       "ignore");
  m.add_label_optional("bound", "BMC / induction depth bound k", "6");
  m.add_label_optional("timeout",
                       "per-query cvc5 wall-clock seconds (0 = unbounded; default bounds the CLI so a hard miter degrades to "
                       "UNKNOWN instead of freezing)",
                       "120");
  m.add_label_optional("witness", "print the counterexample/witness on Refuted (and gate lec.prpfail/prpfailrun)", "true");
  m.add_label_optional("prpfail",
                       "`lhd lec` + --workdir only: on a REFUTED verdict, write a self-contained Pyrope testbench "
                       "that instantiates BOTH designs, drives the counterexample input sequence, and (with "
                       "prpfailrun) dumps a VCD. Value: a filename created under --workdir — default 'lecfail.prp' "
                       "when --workdir is set (else empty=off); 'true'='lecfail.prp'; ''/'false'=off. Gated by lec.witness",
                       "");
  m.add_label_optional("prpfailrun",
                       "run the generated lec.prpfail testbench through `lhd sim --set sim.vcd=true` to produce the "
                       "counterexample waveform (same basename, .vcd, in --workdir). Default true when --workdir is "
                       "set (else false); gated by lec.prpfail actually being written",
                       "");
  m.add_label_optional("phase",
                       "bmc reset phase: after_reset (default; hold reset N cycles, deassert, check "
                       "free-running) | just_reset (hold reset asserted, check during reset) | "
                       "free_toreset (reset input unconstrained) | full (just_reset AND after_reset)",
                       "after_reset");
  m.add_label_optional("reset_cycles", "after_reset phase: reset-hold prologue length before checking", "2");
  m.add_label_optional("reset", "explicit reset inputs: name[:lo|:hi], comma-separated (else auto-detect)", "");
  m.add_label_optional("match",
                       "explicit state correspondence: 'ref=impl' flop pairs (comma/newline-separated) or @FILE; "
                       "collapses differently-named registers onto one cut so the inductive miter compares them",
                       "");
  m.add_label_optional("collapse",
                       "proven-module black-box collapse: comma-separated def names forced to the sound "
                       "blackbox path even when --lib could flatten them (the bottom-up driver's proven set)",
                       "");
  m.add_label_optional("hierarchical",
                       "bottom-up hierarchical decomposition (default true): topo-order the module-def DAG, "
                       "LEC each def leaves-first under the engine, and collapse already-proven children into "
                       "their parents (a child unprovable in isolation stays flattened). false = flat single LEC",
                       "true");
  m.add_label_optional("semdiff",
                       "structural def-diff reduction (M3): structural (default; true/on = alias) | none. Run "
                       "pass.semdiff per module first; a def whose ref/impl are structurally identical "
                       "(and whose children are all proven) is dropped as proven with NO solver call. NOTE: "
                       "cross-front-end pairs (slang vs pyrope) never match structurally, so it only helps "
                       "same-source rebuilds",
                       "structural");
  m.add_label_optional("state_pairing",
                       "tier-2 speculative state correspondence (default true): when unmatched flops survive "
                       "tier-1 name pairing, run pass.semdiff's full-match (SRP/ERP) signature pass per def-pair "
                       "and inject the resulting pairs as UNCERTAIN — REFUTED under them drops all pairs and "
                       "re-solves once, a bounded bmc PASS is never claimed while they apply, and only an "
                       "unbounded inductive proof (self-certifying) accepts them; a PASS persists the pairs as "
                       "entity-keyed pair hints under --workdir. false disables (renamed state stays unmatched)",
                       "true");
  m.add_label_optional("partitions",
                       "input-space case-split: max parallel cube-workers for a purely COMBINATIONAL miter "
                       "(default 4; <2 disables). Each worker sweeps a disjoint slice of the auto-picked control "
                       "input's values, every value pinned to a constant so control-dependent wide operators (a "
                       "variable barrel shift / mux selector) fold — turning one intractable miter into many trivial "
                       "cubes. A decisive case-split verdict is used; otherwise it falls back to the monolithic solve",
                       "4");
  m.add_label_optional("jobs", "shared formal proof worker-pool size (default 4); bounds concurrent hierarchical def tasks", "4");
  m.add_label_optional("split",
                       "case-split control input: auto (default; pick the small-width input feeding the widest "
                       "variable shift-amount / mux-selector pins) | <input-name> (force) | none (disable)",
                       "auto");
  m.add_label_optional("cache",
                       "2f-fcore verdict cache under --workdir (formal_cache.json): a def-pair whose canonical "
                       "digests + verdict-relevant options match a stored PROVEN record is settled with no solver "
                       "call; false disables. Only active with a user --workdir",
                       "true");
  m.add_label_optional("retry",
                       "verdict-cache Unknown policy: changed (default; an unchanged def that already came back "
                       "Unknown at this budget skips the re-attempt, still reported inconclusive) | all (re-attempt "
                       "every Unknown)",
                       "changed");
  m.add_label_optional("cross", "also run lgcheck and assert agreement (bring-up only)", "false");
  m.add_label_optional("decompose",
                       "split the cut/output miter into per-cut focused queries: auto (default; sweep, then "
                       "fall back to the monolithic solve on any cut that does not discharge — fast when it "
                       "proves, definitive otherwise) | true (sweep ONLY, report the hard residue, no "
                       "monolithic solve — the diagnostic mode) | false (monolithic only)",
                       "auto");
  m.add_label_optional("strict",
                       "treat an inconclusive UNKNOWN (no counterexample, solver incomplete) as a hard "
                       "failure; default false (REFUTED fails, witness-free UNKNOWN is a deferred warning)",
                       "false");
  register_pass(m);
}

void Pass_lec::lec(Eprp_var& var) {
  if (var.graphs.size() < 2) {
    livehd::diag::err("pass.lec", "lec-needs-pair", "internal")
        .msg("pass.lec needs two designs (ref, impl); got {} graph(s)", var.graphs.size())
        .fatal();
    return;
  }

  lec::Lec_options o;
  o.engine  = std::string{var.get("engine", "auto")};
  o.solver  = std::string{var.get("solver", "cvc5")};
  o.gold_x  = std::string{var.get("gold_x", "ignore")};
  o.bound   = str_tools::to_i(var.get("bound", "6"));
  o.timeout = str_tools::to_i(var.get("timeout", "120"));
  o.witness = parse_bool(var.get("witness", "true"));
  o.phase        = std::string{var.get("phase", "after_reset")};
  o.reset_cycles = str_tools::to_i(var.get("reset_cycles", "2"));
  o.reset        = std::string{var.get("reset", "")};
  o.match        = lec::parse_match_pairs(var.get("match", ""));  // inline pairs (@FILE only via `lhd lec`)
  o.decompose    = std::string{var.get("decompose", "auto")};
  o.strict       = parse_bool(var.get("strict", "false"));
  o.semdiff      = lec::lec_canon_semdiff(var.get("semdiff", "structural"));
  o.partitions   = str_tools::to_i(var.get("partitions", "4"));
  o.split        = std::string{var.get("split", "auto")};
  // lec.collapse: comma-separated proven-module def names to force-blackbox.
  if (std::string cs{var.get("collapse", "")}; !cs.empty()) {
    size_t pos = 0;
    while (pos < cs.size()) {
      size_t c   = cs.find(',', pos);
      size_t end = c == std::string::npos ? cs.size() : c;
      if (end > pos) {
        o.collapse.emplace_back(cs.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }

  if (auto e = lec::lec_options_range_error(o); !e.empty()) {
    livehd::diag::err("pass.lec", "bad-bound", "io").msg("pass.lec: {}", e).fatal();
    return;
  }

  auto ref  = var.graphs[0];
  auto impl = var.graphs[1];
  auto mod  = std::string{impl->get_name()};

  auto r = lec::prove_equal(ref.get(), impl.get(), o);

  switch (r.verdict) {
    case lec::Verdict::Proven : std::print("lec: '{}' PROVEN equivalent ({})\n", mod, r.detail); break;
    case lec::Verdict::Refuted: {
      std::string w = r.witness.empty() ? std::string{"(no witness)"} : r.witness;
      livehd::diag::err("pass.lec", "not-equivalent", "internal").msg("'{}' is NOT equivalent; counterexample: {}", mod, w).fatal();
      break;
    }
    case lec::Verdict::Unknown:
      livehd::diag::err("pass.lec", "lec-unknown", "unsupported").msg("'{}' equivalence is UNKNOWN ({})", mod, r.detail).fatal();
      break;
  }
}
