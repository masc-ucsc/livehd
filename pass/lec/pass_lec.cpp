// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include <print>
#include <string>
#include <string_view>

#include "diag.hpp"
#include "job_manager.hpp"
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
                       "workers, take the first trustworthy verdict — ind-Proven=PROVEN-full, bmc-Refuted=REFUTED-cex, "
                       "a bmc pass=PROVEN-bounded(n)) | bmc | ind | ic3. Each engine is trustworthy in ONE direction only: "
                       "ind (inductive flop-cut miter) proves unbounded, but its single-step counterexample starts from "
                       "an ARBITRARY equal state that may be UNREACHABLE from reset, so a forced ind that cannot clear "
                       "one reports REFUTED-unchecked (exit 7), never a disproof; bmc's REFUTED-cex is definitive (a "
                       "reachable trace) but its pass is only PROVEN-bounded(n) (deeper cycles unproven). On a purely "
                       "COMBINATIONAL pair the inductive miter IS the combinational miter, so ind's counterexample is a "
                       "genuine REFUTED-cex (auto drops bmc there)",
                       "auto");
  m.add_label_optional("solver",
                       "equivalence backend: cvc5 (default, in-process SMT) | bitwuzla (in-process SMT) | "
                       "lgyosys (debug comparison: default native LEC plus yosys/lgcheck)",
                       "cvc5");
  m.add_label_optional("assume_check",
                       "true|false check every non-top assumption before use. Top-level IO assumes cannot be "
                       "checked and are treated as disclosed assume_nocheck constraints. false keeps every "
                       "assume active but treats it as assume_nocheck",
                       "true");
  m.add_label_optional("normalize_split_ports",
                       "lgcheck debug comparison only: true packs/unpacks cgen's escaped dotted aggregate-port leaves "
                       "to the reference's packed-vector top interface, using the cached RTLIL port lists; "
                       "interface preparation is outside the equivalence timeout",
                       "false");
  m.add_label_optional("descend_on_inconclusive",
                       "lgcheck debug comparison only: true recursively checks direct child definitions when a top is "
                       "undecided, reusing the already-read RTLIL; every child gets its own equivalence "
                       "timeout, and any child counterexample refutes the run",
                       "false");
  m.add_label_optional("gold_x",
                       "reference-side X semantics (cvc5 engines; the analogue of yosys miter "
                       "-ignore_gold_x): ignore (default; a ref constant's ?/X bits are don't-care — "
                       "excluded from the compare, any impl value accepted) | zero (legacy: ?/X bits "
                       "silently concretized to 0 on both sides)",
                       "ignore");
  m.add_label_optional("bound", "BMC / induction depth bound k", "6");
  m.add_label_optional("timeout",
                       "wall limit in seconds for LEC and explicit property proof effort (0 = unbounded). Queueing, "
                       "encoding, checks, retries and witness confirmation share one allowance; parallel arms charge "
                       "elapsed wall time once. Sampled efforts start sampling immediately and use this as their "
                       "backstop. Legacy property verification retains its soft solver-time budget and per-unit floors",
                       "120");
  m.add_label_optional("hard_timeout_mult",
                       "additional backstop multiplier for legacy property verification (0 disables that backstop). "
                       "It never extends LEC or explicit proof effort's shared formal.timeout wall limit; killed workers "
                       "report TIMEOUT",
                       "3");
  m.add_label_optional("witness", "print the counterexample/witness on Refuted (and gate simfail/simfail_run)", "true");
  m.add_label_optional("simfail",
                       "true|false — with --workdir, write Pyrope simulation tests for REFUTED formal results. "
                       "`formal verify` writes one simfail_<formal-test>.prp per refuted test; `formal lec` writes "
                       "simfail_<resolved-top>.prp. Default true; gated by formal.witness",
                       "true");
  m.add_label_optional("simfail_run",
                       "true|false — run the generated simfail test through `lhd sim --set sim.vcd=true` and write "
                       "the same-basename VCD under --workdir. Default true; gated by formal.simfail",
                       "true");
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
  m.add_label_optional("trust",
                       "trusted-module assume-equal blackbox: comma-separated def names ASSUMED equivalent "
                       "WITHOUT a proof — the escape hatch for a cell the encoder cannot model yet (a Latch: "
                       "'sequential op latch not supported yet'). The bottom-up driver SKIPS proving these "
                       "defs and force-blackboxes their instances (outputs become free UF(inputs), inputs "
                       "stay compare points), keeping them boxed even through a refute's flat-confirm so a "
                       "real counterexample OUTSIDE the trusted cones is still reported. Boxing "
                       "over-approximates: a PROVEN under trust is a real proof modulo the trusted internals, and "
                       "a top proven with a non-empty trust list is DISCLOSED ('PROVEN under N trusted "
                       "def(s)'), never a silent unconditional pass. Trusting the top itself is refused",
                       "");
  m.add_label_optional("ignore_memory",
                       "memories EXCLUDED from the comparison: comma-separated names (full hier, canonical, or "
                       "bare leaf -- one entry covers both sides). An ignored memory is BLACKBOXED: each read "
                       "dout becomes ONE SHARED free symbol per (memory, port, cycle) across ref and impl and no "
                       "next-state array is built, so the logic AROUND it is still proved but NOTHING is claimed "
                       "about what it stores. A disclosed ASSUMPTION, like `trust` for a def. This is the escape "
                       "hatch for a memory the encoder refuses to model -- today, one whose ports do not all "
                       "commit on the same clock edge (the language allows that shape, so it parses and "
                       "regenerates; formal refuses it by name)",
                       "");
  m.add_label_optional("hier",
                       "hierarchical decomposition (default true): topo-order the module-def DAG and LEC each def "
                       "with its children collapsed into boxes (see hier_order). false = flat single LEC",
                       "true");
  m.add_label_optional("hier_order",
                       "which way the hierarchy is proved. top_down (default) = prove EVERY def with EVERY child "
                       "BOXED, then discharge each premise from the same pass's other entries (the module DAG is "
                       "well-founded, so this is an induction). No def waits on another def's verdict, so the "
                       "hierarchy runs fully in parallel and every miter is at its minimum size; a refuting block is "
                       "absorbed, and an undecided block is discharged in context, by re-proving its immediate "
                       "PARENT with that block inlined; if the parent proves, nothing higher is re-solved | "
                       "bottom_up = legacy leaves-first, boxing only a child "
                       "already proven and FLATTENING one that is not",
                       "top_down");
  m.add_label_optional("hier_refute",
                       "bottom-up driver: what a REFUTED block means for the run. escalate (default) = inline that "
                       "block into its caller (its PROVEN siblings stay boxed) and prove the caller anyway — a "
                       "module boundary is not part of the specification, functionality moves across it between "
                       "front-ends, so a block-boundary counterexample is not a disproof of the design and only the "
                       "requested top's boundary is contractual | fail = DEBUG: stop at the first differing block "
                       "and taint its ancestors (fast localization); the block counterexample is not a design "
                       "verdict, so the run is REFUTED-unchecked (exit 7): the option forbade the escalation that "
                       "would decide the top",
                       "escalate");
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
                       "re-solves once; a bounded bmc proof under them is accepted only after a pair-free retry "
                       "from detected reset or tracked-? no-reset initialization also proves, otherwise only an "
                       "unbounded inductive proof (self-certifying) accepts them; a proof persists the pairs as "
                       "entity-keyed pair hints under --workdir. false disables (renamed state stays unmatched)",
                       "true");
  m.add_label_optional("partitions",
                       "input-space case-split: max parallel cube-workers for a purely COMBINATIONAL miter "
                       "(default 4; <2 disables). Each worker sweeps a disjoint slice of the auto-picked control "
                       "input's values, every value pinned to a constant so control-dependent wide operators (a "
                       "variable barrel shift / mux selector) fold — turning one intractable miter into many trivial "
                       "cubes. A decisive case-split verdict is used; otherwise it falls back to the monolithic solve",
                       "4");
  m.add_label_optional("profile",
                       "solver profile for a single engine: internal|lazy|eager-kissat|eager-cms|intblast|witness",
                       "internal");
  m.add_label_optional("portfolio", "proof engine:profile list; auto chooses the measured eligible arms", "auto");
  m.add_label_optional("effort",
                       "LEC effort preset: quick-check (256 input vectors), test "
                       "(2048, the default) or long-check (16384); verify (proof-only, no default timeout); proof "
                       "(proof-only, timeout default 120). Unset with a non-cvc5 solver, a proof-only engine or a "
                       "phase other than after_reset falls back to proof. Sampled presets immediately "
                       "check random input vectors against the miter, bounded by timeout, and report TESTED <level>, "
                       "never PROVEN",
                       "");
  m.add_label_optional("min_timeout",
                       "minimum per-unit solver allowance for legacy property's soft budget and hierarchical scheduling. "
                       "A requested floor is always clamped by LEC or explicit proof effort's shared wall deadline",
                       "20");
  m.add_label_optional("spec_mining_timeout",
                       "independent budget (seconds, 0 = off) for the SPECULATIVE post-run phase, never drawn from "
                       "`timeout`: the unproven-def straggler list, the cvc5 timeout-CORE diagnosis (which subset of "
                       "still-undecided obligations is jointly toxic), and P3 invariant mining (see formal.mine, which "
                       "is inert unless this is set). All three are diagnosis: they cost solver time and can never "
                       "change a verdict",
                       "0");
  m.add_label_optional("report",
                       "`lhd formal verify` machine-readable run report: a JSON file written into the workdir on "
                       "EVERY run (all verdicts, undecided ones included) with per-obligation verdicts/cycles/solve times, "
                       "assume classes, the structured timeout core, and witness artifact paths — the agent-loop "
                       "feedback channel. Value: a filename (default formal_report.json); ''/'false'=off",
                       "formal_report.json");
  m.add_label_optional("mine",
                       "`lhd formal verify` invariant-mining tier (runs under the formal.spec_mining_timeout budget; "
                       "spec_mining_timeout=0 disables mining entirely): '' (default) reports only INDUCTIVE survivors — genuine "
                       "invariants, also emitted as a paste-ready formal block (formal_mined.prp) | speculative "
                       "(additionally report base-proven candidates the induction step dropped)",
                       "");
  m.add_label_optional("reuse",
                       "`lhd formal verify --workdir` cross-version reuse, a comma list: hash (default; per-property "
                       "cone-key verdict cache) | equiv (old property model sequentially equivalent: keep its answer) | "
                       "cex (replay the old counterexample) | inv (old PDR invariant, Houdini-filtered) | all | none",
                       "hash");
  m.add_label_optional("abc", "`lhd formal verify`: ABC bmc3 pre-pass before cvc5 (true|false)", "false");
  m.add_label_optional("pdr", "`lhd formal verify`: also try an all-cycles PDR (IC3) proof in the ABC pre-pass (true|false)",
                       "false");
  m.add_label_optional("pdr_timeout", "`lhd formal verify`: PDR seconds per property", "20");
  m.add_label_optional("sec_timeout", "`lhd formal verify`: reuse=equiv seconds per property", "3");
  m.add_label_optional("abc_mem",
                       "`lhd formal verify`: let a design with memories into the ABC pre-pass, memory reads "
                       "abstracted (proofs only; refutes stay with cvc5) (true|false)",
                       "false");
  m.add_label_optional("abc_budget", "`lhd formal verify`: ABC pre-pass seconds in total (0 = a quarter of timeout)", "0");
  m.add_label_optional("split",
                       "case-split control input: auto (default; pick the small-width input feeding the widest "
                       "variable shift-amount / mux-selector pins) | <input-name> (force) | none (disable)",
                       "auto");
  m.add_label_optional("retry",
                       "verdict-cache TIMEOUT policy: changed (default; an unchanged def that already came back "
                       "TIMEOUT-time at this budget skips the re-attempt, reported \"TIMEOUT-time (skipped: known "
                       "timeout)\") | all (re-attempt every known timeout)",
                       "changed");
  m.add_label_optional("cross", "also run lgcheck and assert agreement (bring-up only)", "false");
  m.add_label_optional("decompose",
                       "split the cut/output miter into per-cut focused queries: auto (default; sweep, then "
                       "fall back to the monolithic solve on any cut that does not discharge — fast when it "
                       "proves, definitive otherwise) | true (sweep ONLY, report the hard residue, no "
                       "monolithic solve — the diagnostic mode) | false (monolithic only)",
                       "auto");
  m.add_label_optional("cones",
                       "register-cone decomposition: bit-blast each per-cut obligation into an AIG and "
                       "discharge it with abc, subtracting every cone abc proves from the cvc5 obligation "
                       "(abc only subtracts; cvc5 still owns every verdict and witness): auto (default) | "
                       "true (also report each cone's outcome) | false (off)",
                       "auto");
  m.add_label_optional("conelimit", "per-cone abc SAT backtrack budget; 0 = abc's own default", "10000");
  m.add_label_optional("box_model",
                       "how a PROVEN collapsed child is modelled in its parent: seq (default) = a sequence "
                       "transducer — prove its inputs equal (bbin obligations) and share one free symbol per "
                       "(instance, port, cycle) for its outputs, with no state cut and no uninterpreted function, "
                       "so comb and stateful children are the same object; uf = the legacy Comb_box/State_box "
                       "encoding (UF(inputs), or UF_out(state)+UF_next(inputs,state) with a threaded state cut), "
                       "which forces QF_AUFBV and disables cvc5's eager bit-blaster",
                       "seq");
  m.add_label_optional("int_blast",
                       "cvc5 int-blasting (solve-bv-as-int): auto (default) = solve bit-vector-first, and a "
                       "a solver give-up earns ONE int-blasted re-solve at the formal.min_timeout budget "
                       "(arithmetic-rewrite miters — reassociated/distributed/commuted multiplies — that "
                       "bit-blasting cannot finish prove in <1s as unbounded integers, while mask/extract-heavy "
                       "cones stay undecided, so the retry only ever adds information) | off = never | "
                       "iand | sum | bitwise | bv = force that cvc5 mode from the first solve. Verdicts stay "
                       "sound (the translation is equisatisfiable); only the main lec solve path is affected",
                       "auto");
  m.add_label_optional("preflight_engine",
                       "INTERNAL cprop engine of the LEC preflight (the value-preserving re-simplify after hierarchy "
                       "inlining): classic (default) | aegraph. aegraph stamps every cell it creates or the preflight "
                       "falls back to classic for that graph. Salted into //lhd:formal_salt",
                       "classic");
  m.add_label_optional("phase_sched",
                       "encode a latch / negedge / gated-clock design over FOUR ordered microsteps per source "
                       "period (close-low, rise, close-high, fall) instead of rewriting it with pass.single_edge "
                       "first. Read-only analysis, so it composes across hierarchy; default true. false falls "
                       "back to the M8 graph rewrite, which refuses a latch or negedge flop inside a def",
                       "true");
  m.add_label_optional("stats",
                       "print a cvc5 solve-insight report: problem size (atoms, input atoms, clause literals), "
                       "conflicts / learned clauses, decisions, propagations, restarts, theory lemmas, resource "
                       "units and time — aggregated over the whole run (every cvc5 solver it spawned, every "
                       "hierarchical def). `--stats` is the CLI sugar for the same knob. WARNING: it also "
                       "registers a cvc5 plugin (the only way to see learned-clause CONTENT) that makes the "
                       "solve ~8x SLOWER, so this is for DIAGNOSIS, never for timing a run",
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
  o.engine            = std::string{var.get("engine", "auto")};
  o.solver            = std::string{var.get("solver", "cvc5")};
  o.assume_check      = parse_bool(var.get("assume_check", "true"));
  o.gold_x            = std::string{var.get("gold_x", "ignore")};
  o.bound             = str_tools::to_i(var.get("bound", "6"));
  o.timeout           = str_tools::to_i(var.get("timeout", "120"));
  o.hard_timeout_mult = str_tools::to_i(var.get("hard_timeout_mult", "3"));
  o.witness           = parse_bool(var.get("witness", "true"));
  o.phase             = std::string{var.get("phase", "after_reset")};
  o.reset_cycles      = str_tools::to_i(var.get("reset_cycles", "2"));
  o.reset             = std::string{var.get("reset", "")};
  o.match             = lec::parse_match_pairs(var.get("match", ""));  // inline pairs (@FILE only via `lhd lec`)
  o.decompose         = std::string{var.get("decompose", "auto")};
  o.cones             = std::string{var.get("cones", "auto")};
  o.conelimit         = str_tools::to_i(var.get("conelimit", "10000"));
  o.phase_sched       = parse_bool(var.get("phase_sched", "true"));
  o.int_blast         = std::string{var.get("int_blast", "auto")};
  o.box_seq           = std::string_view{var.get("box_model", "seq")} != "uf";
  o.semdiff           = lec::lec_canon_semdiff(var.get("semdiff", "structural"));
  o.partitions        = str_tools::to_i(var.get("partitions", "4"));
  o.split             = std::string{var.get("split", "auto")};
  o.stats             = parse_bool(var.get("stats", "false"));  // cvc5 solve-insight accounting (~8x slower)
  // Default `test` (see lec::kDefaultLecEffort); an unset effort with options a
  // sampled preset cannot serve falls back to the proof-only defaults.
  o.profile                              = std::string{var.get("profile", "internal")};
  o.portfolio                            = std::string{var.get("portfolio", "auto")};
  const std::string_view effort_label    = var.get("effort", "");
  const bool             effort_explicit = !effort_label.empty();
  try {
    o.effort = lec::parse_effort(effort_explicit ? effort_label : lec::kDefaultLecEffort);
  } catch (const std::invalid_argument& error) {
    livehd::diag::err("pass.lec", "bad-effort", "io").msg("{}", error.what()).fatal();
    return;
  }
  if (!effort_explicit && (o.solver != "cvc5" || (o.engine != "auto" && o.engine != "bmc") || o.phase != "after_reset")) {
    o.effort = lec::Effort::Legacy;
  }
  if (o.effort == lec::Effort::Verify) {
    o.timeout = str_tools::to_i(var.get("timeout", "0"));
  }
  if (lec::is_sampled(o.effort)) {
    if (o.solver != "cvc5" || (o.engine != "auto" && o.engine != "bmc")) {
      livehd::diag::err("pass.lec", "bad-effort", "io").msg("sampled effort requires cvc5 and engine=auto or bmc").fatal();
      return;
    }
  }
  // formal.lec.collapse: comma-separated proven-module def names to force-blackbox.
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
  // formal.lec.trust: comma-separated def names ASSUMED equal without a proof.
  // This low-level single-pair path has no bottom-up driver, so it cannot skip
  // proving a def or keep a box through a flat-confirm — trust degenerates to the
  // same encoder blackbox as collapse here (the driver semantics live in
  // lhd_kernel_formal.cpp's lec_hierarchical). Boxing over-approximates, so this
  // is sound: a PASS is real, a refute at a box boundary can only be spurious.
  if (std::string ms{var.get("ignore_memory", "")}; !ms.empty()) {
    size_t pos = 0;
    while (pos < ms.size()) {
      size_t c   = ms.find(',', pos);
      size_t end = c == std::string::npos ? ms.size() : c;
      if (end > pos) {
        o.ignore_memory.emplace_back(ms.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }
  if (std::string ts{var.get("trust", "")}; !ts.empty()) {
    size_t pos = 0;
    while (pos < ts.size()) {
      size_t c   = ts.find(',', pos);
      size_t end = c == std::string::npos ? ts.size() : c;
      if (end > pos) {
        o.trust.emplace_back(ts.substr(pos, end - pos));
      }
      pos = end + 1;
    }
    o.collapse.insert(o.collapse.end(), o.trust.begin(), o.trust.end());
  }

  if (auto e = lec::lec_options_range_error(o); !e.empty()) {
    livehd::diag::err("pass.lec", "bad-bound", "io").msg("pass.lec: {}", e).fatal();
    return;
  }

  auto ref  = var.graphs[0];
  auto impl = var.graphs[1];
  auto mod  = std::string{impl->get_name()};

  if (auto error = lec::match_names_error(ref.get(), impl.get(), o.match); !error.empty()) {
    livehd::diag::err("pass.lec", "unresolved-match", "usage").msg("{}", error).fatal();
    return;
  }
  if (o.timeout > 0) {
    o._deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.timeout);
  }
  lec::Query_result r;
  if (lec::is_sampled(o.effort)) {
    auto co       = o;
    co.engine     = "bmc";
    co.partitions = 1;
    co.split      = "none";
    r             = lec::prove_equal_isolated(ref.get(), impl.get(), co);
  } else {
    r = lec::prove_equal_isolated(ref.get(), impl.get(), o);
    r = lec::int_blast_retry(ref.get(), impl.get(), o, std::move(r));
  }

  // BEFORE the verdict: the refuted arms below are .fatal(), so a report
  // printed after them would be lost in exactly the cases where knowing how hard
  // the solver worked matters most. Without this, `pass.lec stats:true` used as a
  // direct pipe step would pay the ~8x plugin cost and print nothing at all.
  if (o.stats) {
    livehd::lec::report_cvc5_stats("lec", r.cvc5);
  }

  // The same outcome mapping as `lhd lec` (owner rulings 2026-10-14). This
  // single-pair path runs no escalation of its own, so its result IS final.
  //   PROVEN-* / TESTED-sampled   print, exit 0
  //   TIMEOUT-time / -memory      print + warning, exit 0
  //   REFUTED-cex                 error, code lec-refuted-cex: the kernel maps
  //                               it to the counterexample class (exit 10)
  //   REFUTED-<other>             error, class unsupported (exit 7); this
  //                               includes REFUTED-vacuous (nothing compared,
  //                               or a contradictory assume set)
  const bool        engine_forced = o.engine != "auto" || o.portfolio != "auto";
  const auto        outcome       = lec::query_outcome(r, engine_forced);
  const auto        info          = lec::outcome_info(outcome);
  const std::string word          = lec::outcome_word(outcome, lec::query_outcome_depth(r, outcome));
  const std::string code          = "lec-" + std::string{info.json};
  if (lec::is_proven(outcome) || outcome == lec::Outcome::Tested_sampled || lec::is_timeout(outcome)) {
    if (outcome == lec::Outcome::Tested_sampled) {
      std::print("lec: '{}' {} {} ({} input vectors, no mismatch; equivalence not proven)\n",
                 mod,
                 word,
                 lec::effort_name(r.sampling.reached),
                 r.sampling.vectors);
    } else {
      std::print("lec: '{}' {} ({})\n", mod, word, r.detail);
    }
    if (info.warn) {
      livehd::diag::warn("pass.lec", code, "unsupported")
          .msg("{}: '{}' {}",
               word,
               mod,
               outcome == lec::Outcome::Timeout_memory ? "ran out of memory before a verdict (no proof, no counterexample)"
                                                       : "hit a time limit before a verdict (no proof, no counterexample)")
          .hint(outcome == lec::Outcome::Timeout_memory ? "run on a host with more memory, or decompose the check"
                                                        : "raise --set formal.timeout, or decompose the check")
          .emit();
    }
    return;
  }
  if (outcome == lec::Outcome::Refuted_cex) {
    // NOTE: the error class of a pass diagnostic is mapped by the kernel
    // (map_diag_category); the code `lec-refuted-cex` is what names it a design
    // counterexample.
    std::string w = r.witness.empty() ? std::string{"(no witness)"} : r.witness;
    livehd::diag::err("pass.lec", code, "internal")
        .msg("{}: '{}' is NOT equivalent; counterexample: {}", word, mod, w)
        .fatal();
    return;
  }
  if (outcome == lec::Outcome::Refuted_vacuous) {
    const std::string_view why = r.cause_detail.empty() ? std::string_view{r.detail} : std::string_view{r.cause_detail};
    const bool contradictory   = why.find("CONTRADICTORY") != std::string_view::npos || why.find("contradictory") != std::string_view::npos;
    livehd::diag::err("pass.lec", code, "unsupported")
        .msg("{}: '{}' {}, so the check proved nothing ({})",
             word,
             mod,
             contradictory ? "was compared under a CONTRADICTORY assume set" : "had nothing to compare",
             r.detail)
        .hint(contradictory ? "fix the conflicting assume statements so at least one input satisfies them all"
                            : "select a top that drives at least one output or state cell on both sides")
        .fatal();
    return;
  }
  livehd::diag::err("pass.lec", code, "unsupported").msg("{}: '{}' equivalence was not decided ({})", word, mod, r.detail).fatal();
}
