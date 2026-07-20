// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::lec {

// L1 relational query API. v1 covers the combinational equivalence client:
// prove_equal(ref, impl) under assume_equal(primary inputs). prove_distinct /
// is_sat are the duals/relatives (added as the clients land).
enum class Verdict { Proven, Refuted, Unknown };

// Machine-readable counterexample trace: the reproducible input sequence a
// REFUTED BMC run found, uncapped and grouped by cycle (the display `witness`
// string caps its input tokens for readability). `lhd lec` turns this into a
// self-contained Pyrope testbench (lecfail.prp) that drives BOTH designs with
// the sequence and dumps a VCD, so the divergence is visualized / re-run.
// Only the BMC engine fills it — its CEX is reachable from reset; the inductive
// engine's single-step CEX may be an unreachable step-case, so `ind` leaves it
// empty (and the `auto` portfolio only trusts a BMC-Refuted, so a REFUTED verdict
// always carries a BMC trace).
struct Witness_in {
  std::string name;       // primary-input name (implicit `clock`/`reset` included)
  std::string value;      // satisfying value, decimal (unsigned magnitude)
  int         width = 1;  // symbol bit-width
};
struct Witness_cycle {
  bool                    reset_asserted = false;  // a reset-hold prologue cycle
  std::vector<Witness_in> inputs;                  // every primary input this cycle
};
struct Witness_trace {
  int                        reset_cycles  = 0;   // leading reset-hold cycles
  int                        diverge_cycle = -1;  // index into `cycles` of the first output divergence
  std::vector<std::string>   diverge_outputs;     // "name(ref=X impl=Y)" tokens at diverge_cycle
  std::vector<Witness_cycle> cycles;              // driven sequence (reset prologue first)
  // F7 root cut — the FIRST diverging STATE cut (the state the diverging output
  // inherits), for the machine-readable lecfail.json and the source-mapped root
  // clause. `root_src` is "file:line" of the flop's declaration (empty if the
  // node carried no source id). All empty when the trace has no state cut.
  std::string                root_key;            // canonical flop key (display-stripped in the clause)
  int                        root_cycle = -1;     // checked step of the diverging cut
  std::string                root_ref, root_impl; // paired current values (unsigned-magnitude decimal)
  std::string                root_src;            // "file:line" of the flop decl, or ""
  bool                       empty() const { return cycles.empty(); }
};

struct Query_result {
  Verdict     verdict = Verdict::Unknown;
  std::string witness;  // satisfying input assignment when Refuted
  std::string detail;   // engine / bound / encode error, for diagnostics

  // The design-size gate refused this design (too large to encode as one unit,
  // lec.allow_oversize unset). Distinct from a solver-inconclusive UNKNOWN: it is
  // a hard admission failure, so a driver must exit non-zero regardless of
  // lec.strict, exactly as pass.abc does. See Lec_options::allow_oversize.
  bool oversize_refused = false;

  // Structured, uncapped counterexample trace for witness reproduction (empty
  // unless a BMC REFUTE built one). See Witness_trace.
  Witness_trace trace;

  // Which engine produced this verdict + how long it took (for the per-block
  // progress/info record). Normally just the requested engine; under the `auto`
  // portfolio it is the engine that reached the trustworthy verdict FIRST
  // (inductive-Proven or BMC-Refuted) — and for an inconclusive auto run it lists
  // the attempted engines. `elapsed_ms` is that engine's wall-clock (-1 = unset).
  std::string engine;
  long long   elapsed_ms = -1;

  // The case-split selector that produced this verdict ("" = no split). The
  // structured twin of the "case-split <name>[Nb]" detail text: the verdict
  // cache persists it as a strategy hint (keyed by def entity name) and replays
  // it next run via lec.split=<name>, so a known-good split is tried first.
  std::string split_used;

  // BMC bound bookkeeping for the `auto` portfolio's bounded-Proven policy: the
  // checked-window size and the number of (output,cycle) comparisons actually run.
  // A BMC `Proven` with output_checks>0 is a BOUNDED proof (no CEX <= bound), which
  // `auto` reports as PASS (transparently labelled) rather than inconclusive —
  // deeper-than-bound cycles are out of scope by design.
  int checked_steps = 0;
  int output_checks = 0;

  // Structural-correspondence report (so `lhd lec` can ITERATE instead of bailing
  // on the first unmatched cut point). When the two designs don't expose the same
  // set of state/outputs, the miter still runs over the COMMON ones and these list
  // what is unmatched on each side (human-readable, control prefixes stripped).
  // Non-empty `unmatched_*` ⇒ the verdict cannot be a definitive Proven (the
  // correspondence is incomplete); the engine reports Unknown but `detail`/`witness`
  // still carry the matched-portion result + the per-output divergences found.
  std::vector<std::string> unmatched_ref;   // in ref, no corresponding impl signal
  std::vector<std::string> unmatched_impl;  // in impl, no corresponding ref signal

  // Uncertain (tier-2) pairs that were APPLIED for this verdict — copied from
  // Lec_options::uncertain_match by the discipline wrapper; empty when the
  // verdict came from the pair-free confirming retry. On an ind-PROVEN the
  // proof itself validates them: the driver promotes exactly this list to
  // entity-keyed pair hints in the persistent cache.
  std::vector<std::pair<std::string, std::string>> uncertain_pairs_used;

  // Cone digests (cone_digest) this run PROVED for the first time. The engine
  // may live in a forked worker, so it cannot touch the driver's verdict cache
  // itself: it reports what it proved and the driver persists it. See
  // Lec_options::_cone_cache for the read direction.
  std::vector<std::string> cone_proven;
};

struct Monitor;

// Discharge / engine knobs (filled from the lec.* set-options).
struct Lec_options {
  // Reference-side X semantics — the cvc5 analogue of yosys `miter
  // -ignore_gold_x`. "ignore" (default): a ref constant's '?' bits source an
  // undef bit-plane; the miters exclude ref-unknown output/next-state bits from
  // the compare, and the shared state hypothesis binds ref X-state to the
  // impl's value (any impl choice is a legal resolution of a ref don't-care).
  // "zero": legacy behavior — '?' bits silently concretized to 0 on BOTH sides.
  std::string gold_x = "ignore";

  std::string engine  = "bmc";   // bmc | ind (k-induction) | ic3 | auto (portfolio). This is
                                 // the programmatic-API fallback (a single, fork-free engine for
                                 // in-pass design-queries); the `lhd lec` CLI defaults to `auto`.
  std::string solver  = "cvc5";  // cvc5 | bitwuzla (not yet built)
  int         bound   = 6;       // BMC / induction depth
  int         timeout = 0;       // per-query seconds (0 = none). NOTE: for the hierarchical
                                 // driver this is the TOTAL wall-clock budget for the whole
                                 // command (2f-fcore §6 scheduler), sliced across escalating
                                 // rounds; for a single def / the flat path it is the per-query cap.
  // Budget scheduler (2f-fcore §6): how the hier driver spends `timeout`.
  //   "wall"   — `timeout` is a TOTAL wall-clock budget; the driver runs escalating
  //              rounds (small slices first, survivors re-tried with bigger slices)
  //              until every def settles or the deadline passes. This makes one
  //              `formal.timeout=T` an actual total, not `T` per def (the D×T hazard).
  //   "rlimit" — no wall-clock rounds; each query is bounded by the deterministic
  //              `rlimit` counter instead (the compile tier / CI-repro path). A no-op
  //              scheduler, so builds stay reproducible across machines/build modes.
  std::string budget_mode = "wall";
  // Independent budget (seconds, 0 = off) for a diagnosis/straggler phase after the
  // final round: name the still-unproven defs so a timed-out run's OUTPUT is
  // actionable. Reserved for the mining/timeout-core work; currently emits the
  // straggler list. (2f-fcore §6 `formal.minetimeout`.)
  int         minetimeout = 0;
  // Deterministic per-query budget (cvc5 `rlimit-per`): a machine-/wall-clock-
  // independent internal resource counter, so the SAME config yields the SAME
  // verdict on every machine and build mode. The compile tier (2f-formal) sets
  // this (with timeout=0) so a verdict that ELIDES a runtime check is
  // reproducible across `-c dbg`/`-c opt` binaries; the verify/lec CLIs default
  // to wall-clock `timeout`. 0 = off. Both may be set (each bounds a checkSat).
  int         rlimit  = 0;
  bool        witness = true;    // print counterexample on Refuted
  std::string decompose = "auto"; // prove each cut/output diff as a separate UNSAT query
                                 // instead of one monolithic OR-miter. Same proof (an OR
                                 // is UNSAT iff every disjunct is), but each query is a
                                 // small focused cone, so the easy cuts discharge instantly
                                 // and only the genuinely-hard one is the bottleneck.
                                 //   auto  (default): run the per-cut sweep AND fall back
                                 //          to the monolithic solve on any cut that does not
                                 //          discharge — fast when it proves, definitive (+a
                                 //          witness on a real diff) otherwise. The intended
                                 //          everyday mode: same verdict as monolithic, but it
                                 //          turns a 60s monolithic miter into ~1s when the cuts
                                 //          are easy (a name-matched register-correspondence
                                 //          proof of two large front-end netlists is the case).
                                 //   true  : decompose ONLY — report "N/M cuts PROVEN" + the
                                 //          unresolved residue as Unknown, NEVER run the
                                 //          monolithic solve (the diagnostic mode to isolate the
                                 //          hard cone fast, e.g. a wide ALU/barrel-shift cone
                                 //          that needs SAT-sweeping). LEC_DECOMP_LOG=1 logs each
                                 //          cut's PROVEN/DIFF/unknown verdict.
                                 //   false : monolithic OR-miter only (one big checkSat).
                                 // on/1 == true, off/0 == false.
  std::string cones = "auto";    // register-cone decomposition: before cvc5 sees the induction
                                 // step, try to discharge each per-cut obligation by bit-blasting
                                 // it into an AIG and proving it with ABC (see cone_abc.hpp). Every
                                 // cut ABC proves is SUBTRACTED from the cvc5 obligation, so only
                                 // the cones ABC could not settle are ever handed to the SMT
                                 // solver. This is the classic compare-point decomposition: cutting
                                 // at name-matched registers makes each next-state cone an
                                 // independent combinational miter, which is what a bit-level
                                 // engine is good at (cvc5 chokes on the monolithic OR of a
                                 // tech-mapped pipeline). ABC NEVER decides a verdict on its own:
                                 // only Proven subtracts; a refuted/unknown/unsupported cone stays
                                 // with cvc5, which owns every verdict and witness exactly as
                                 // before.
                                 //   auto (default): subtract what ABC proves, cvc5 does the rest.
                                 //   true : same, and report each cone's outcome (diagnostic).
                                 //   false: skip the ABC pass entirely.
  int         conelimit = 10000;   // per-cone ABC SAT conflict budget (0 = ABC's own default).
                                   // Bounds a hard cone so it falls back to cvc5 instead of hanging.
  bool        strict = false;    // treat an inconclusive UNKNOWN (no counterexample, the
                                 // solver merely could not complete the proof) as a hard
                                 // failure. Default false: REFUTED (a real counterexample)
                                 // hard-fails, but a witness-free UNKNOWN is a deferred
                                 // warning that exits cleanly (it disproves nothing).

  bool        allow_oversize = false;  // skip the design-size gate (lec.allow_oversize). The
                                       // encoder materializes the whole flattened design (minus
                                       // opaque/collapsed subs) into one forward_hier vector; above
                                       // ~1M nodes that alone can exhaust memory, so a design that
                                       // large is refused as Unknown unless this is set. The
                                       // per-design size is opacity-aware, so a design checked in
                                       // small decomposed pieces is not falsely refused.

  // Reset-phase separation for the BMC engine (lec.phase). The reset-asserted
  // and the free-running behaviors are best checked SEPARATELY:
  //   after_reset (default): hold reset asserted for `reset_cycles` cycles (NO
  //           miter — just to drive both into their reset state), then DEASSERT
  //           it and miter the following `bound` cycles — free-running equivalence.
  //   just_reset : hold every primary reset input ASSERTED on every cycle and
  //           miter each cycle — proves the two designs agree DURING reset.
  //   free_toreset : reset input ranges freely; the unrolling mixes both (the
  //           solver may still assert reset, so it explores odd reset patterns).
  //   full : run BOTH just_reset and after_reset and require equivalence in each.
  // A primary reset input is one that drives some flop's reset_pin directly;
  // its asserted level follows the flop's negreset attribute (active-low -> 0).
  std::string phase        = "after_reset";  // after_reset | just_reset | free_toreset | full
  int         reset_cycles = 2;               // after_reset phase: reset-hold prologue length

  // Optional explicit reset-input spec (lec.reset), comma-separated, each
  // `name` (polarity inferred from a _n/_ni suffix) or `name:lo` / `name:hi`.
  // When set it is AUTHORITATIVE (replaces auto-detection). Auto-detection
  // otherwise finds (a) every primary input that drives a flop reset_pin
  // (async resets) and (b) canonical reset-named inputs like rst / rst_ni
  // (synchronous resets, which the front-end folds into din rather than a
  // reset_pin so they have no structural marker).
  std::string reset;  // e.g. "rst_ni,clr_i:hi"

  // Explicit register/state CORRESPONDENCE (`lhd lec --match FILE` / lec.match):
  // {ref_state_name, impl_state_name} pairs that name the SAME state element when
  // the two front-ends chose unrelated names — e.g. a firtool stage register
  // `id_ex_ctrl.reg_ex_ctrl_aluop` vs a flat reimplementation `idex_aluop`. Each
  // side is canonicalized (canon_flop_name) and collapsed onto ONE shared cut
  // symbol, so the inductive miter shares their current state and compares their
  // next states directly. Names that already agree (after canon) need no entry;
  // the register-file flop-bank <-> Memory case is bridged structurally, no entry
  // needed there either.
  std::vector<std::pair<std::string, std::string>> match;

  // Tier-2 UNCERTAIN state correspondence (2f-lec; produced by pass/semdiff's
  // full-match signature pass or replayed pair hints): {ref,impl} pairs applied
  // to the flop name-alias exactly like `match`, but SPECULATIVE. prove_equal
  // enforces the uncertain discipline itself: REFUTED with pairs applied is
  // never final — drop ALL of them and re-solve once (a pair-free re-refute is
  // the real FAIL; anything else ceilings at Unknown, since dropping any pair
  // already makes the correspondence incomplete). A timeout/Unknown never
  // retries (the retry's ceiling is the Unknown the timeout already reports).
  // A BOUNDED bmc-Proven is never claimed while pairs are applied (the shared
  // s0 constraints can mask a real bounded CEX — a false PASS); only an
  // unbounded inductive Proven is accepted with pairs in force — it is
  // self-certifying (any inductive, output-implying, initially-true relation
  // certifies) PROVIDED the paired reset/init values are equal, which the
  // producer guarantees and validate_uncertain_pairs re-checks on hint replay.
  std::vector<std::pair<std::string, std::string>> uncertain_match;

  // Confident MEMORY correspondence (2f-lec diverged-use collapse guard; produced
  // by pass/semdiff's full-match signature pass, the mem entries of state_pairs).
  // {ref_mem_hier_name, impl_mem_hier_name}. Unlike flops, memories are NOT
  // name-aliased — they collapse by shape (size×bits) × RTL occurrence order. That
  // occurrence pairing is a false-PROVEN hazard when a shape bucket holds MORE THAN
  // ONE memory per side and the two front-ends emit them in a different order (the
  // wrong two memories then share one initial-contents array). build_shared_mems
  // uses this list (plus canon-name agreement) to CONFIRM an ambiguous bucket's
  // occurrence pairing before collapsing it; an unconfirmed ambiguous bucket is
  // kept UNCOLLAPSED (fresh per-design array symbols) — a sound degrade (worst
  // case Unknown/flat-refute, never a false PROVEN). Empty ⇒ rely on canon names
  // alone (still sound: renamed-and-reordered ambiguous mems stay uncollapsed).
  std::vector<std::pair<std::string, std::string>> mem_match;

  // Memories (raw hier names, either side) that semdiff flagged as GENUINELY
  // diverged — unpaired with a kind/init mismatch or no counterpart (NOT mere
  // symmetric ambiguity). Such a memory must NOT be force-collapsed by shape ×
  // occurrence: the two sides use/initialize it differently, so sharing one
  // current-state array would be unsound. build_shared_mems leaves any shape
  // bucket containing a diverged memory uncollapsed (fresh per-design arrays).
  std::vector<std::string> mem_diverged;

  // Proven-module black-box collapse (`lhd lec --collapse <def>` / lec.collapse):
  // module-def names the driver has ALREADY proven equivalent in isolation, which
  // are FORCED to the sound black-box path even when they could be flattened (a
  // combinational def supplied via --lib). Their outputs become shared UF(inputs)
  // symbols and their inputs become miter compare points, so the parent proof
  // stops re-solving the leaf's internals ("equal inputs => equal outputs", already
  // discharged when the leaf was proven). Matched case-sensitively (name policy).
  // The bottom-up hierarchical driver fills this with its per-round proven set.
  std::vector<std::string> collapse;

  // Structural def-diff reduction (`lec.semdiff`, M3): the semdiff matching
  // algorithm to run per module BEFORE the solver — `none` (default, always LEC)
  // or `structural` (pass/semdiff::structural_match). A def whose ref/impl are
  // structurally IDENTICAL (no unmatched node on either side) AND whose children
  // are all already proven needs NO solver call — it is dropped as proven. Only
  // the changed defs reach cvc5. The driver (lhd lec --set lec.hier=true)
  // consumes this; prove_equal itself ignores it.
  std::string semdiff = "none";

  // Tier-2 speculative state pairing (lec.state_pairing, CLI default on): when
  // unmatched state survives tier-1 name correspondence, the DRIVER runs
  // pass/semdiff's full-match signature pass per def-pair (or replays a
  // persisted pair hint) and injects the surviving pairs as `uncertain_match`.
  // prove_equal itself ignores this flag — it only enforces the uncertain
  // discipline on whatever pairs it is handed.
  bool state_pairing = true;

  // Input-space case-split (lec.partitions / lec.split): prove the combinational
  // miter one CONTROL-cofactor at a time, in parallel. `partitions` caps the
  // number of forked workers (default 4; <2 disables). `split` names the control
  // input to case-split on ("auto" = pick the input feeding the widest control
  // operand — a variable barrel shift / mux selector — via graph_is_combinational's
  // caller; "" / "none" disables). Each worker sweeps a disjoint slice of the
  // selector's values; every value is pinned to a CONSTANT so cvc5 folds the
  // control-dependent wide operators (a 1088-bit variable ashr becomes a static
  // slice), turning one intractable monolithic miter into many trivial cubes.
  // Only applied to purely combinational pairs (no unreachable-state concern, so
  // any SAT cube is a genuine counterexample). v1: combinational only.
  int         partitions = 4;
  int         jobs       = 4;  // shared formal worker-pool bound (hier proof DAG)
  std::string split      = "auto";

  // Internal (set by run_case_split, not a user knob): when `_split_values` is
  // non-empty, prove_equal's ind path runs the cube sweep over exactly these
  // selector values of input `_split_name` instead of the monolithic solve.
  std::string           _split_name;
  std::vector<uint64_t> _split_values;

  // Cone digests already known PROVEN (from the driver's verdict cache). Read
  // side of the cone cache: the set is loaded ONCE by the driver and rides the
  // by-value Lec_options copy into every fork, so a worker never touches the
  // cache file -- it just checks membership. A hit skips abc for that cone.
  absl::flat_hash_set<std::string> _cone_cache;
  bool                  _isolated_worker = false;  // one global-pool child: no nested forks

  // Heuristic-only strategy replay from the persistent cache. `auto` tries a
  // previously winning ind/bmc engine first, then falls back to its complete
  // normal portfolio if the hint no longer settles the edited design.
  std::string _preferred_engine;

  // Impl-side formal helpers accepted by the lhd driver. Every monitor here
  // contains only `assume` properties: internal facts were independently
  // proven unbounded before insertion, input-only facts are disclosed
  // environment constraints, and unchecked facts carry a distinct warning and
  // verdict disclosure. `assumption_key` participates in the verdict-cache key.
  const std::vector<Monitor>* assumptions = nullptr;
  std::string                 assumption_key;
  int                         proven_helpers    = 0;
  int                         input_assumes     = 0;
  int                         unchecked_assumes = 0;

  // Compile tier (2f-formal): treat design `assume` fproperties as NO-OPs — never
  // asserted as constraints, never induction hypotheses (they still occupy an occ
  // slot / a Prop_result, but are neither checked nor used). Sound for the compile
  // tier because a PROVEN invariant assume is auto-satisfied by BMC-from-reset
  // (reachable states already satisfy it) and an UNPROVEN / input assume must
  // never prune an assert's proof (only proven assumes are hypotheses — that
  // discipline lives in the pass.formal driver, which proves assumes separately
  // and recovers assume-dependent elisions with the single-frame Prover). The
  // verify CLI leaves this false: there, an assume is a disclosed env constraint.
  bool ignore_assumes = false;

  // Verify-obligation cache hooks. The engine computes a rule-F key downstream
  // of encoding; the CLI supplies the persistent store without coupling this
  // reusable library to lhd/formal_cache.
  std::function<bool(std::string_view)> verify_cache_lookup;
  std::function<void(std::string)>      verify_cache_store;

  // P3 mining tier. Mining itself is gated by minetimeout>0 (it spends that
  // budget, never `timeout`). "" = report only the inductive survivors;
  // "speculative" = also report base-proven candidates the induction step
  // dropped (bounded facts an agent may still find suggestive).
  std::string mine;
};

// lec.decompose mode predicates (auto | true | false; on/1==true, off/0==false).
// `lec_decompose_try` = run the per-cut sweep; `lec_decompose_fallback` = on a cut
// that does not discharge, fall back to the monolithic solve for a definitive
// verdict + witness (only `auto`). `true` runs the sweep but never the monolithic
// solve (the diagnostic mode). See Lec_options::decompose.
inline bool lec_decompose_try(std::string_view m) { return m == "auto" || m == "true" || m == "on" || m == "1"; }
inline bool lec_decompose_fallback(std::string_view m) { return m == "auto"; }

// lec.cones mode predicates (auto | true | false). `lec_cones_try` = run the ABC
// cone pass; `lec_cones_report` = also disclose each cone's outcome. See
// Lec_options::cones.
inline bool lec_cones_try(std::string_view m) { return m == "auto" || m == "true" || m == "on" || m == "1"; }
inline bool lec_cones_report(std::string_view m) { return m == "true" || m == "on" || m == "1"; }

// Normalize a lec.semdiff value to the canonical {none, structural}. `true`/`on`/
// `1` are accepted as ergonomic aliases for `structural` (the only algorithm),
// everything else (false/off/0/none/empty) maps to `none`. Applied at the CLI
// read sites so downstream (the validator + the hierarchical driver) only ever
// sees the canonical pair.
inline std::string lec_canon_semdiff(std::string_view v) {
  if (v == "structural" || v == "true" || v == "on" || v == "1") {
    return "structural";
  }
  return "none";
}

// The BMC engine unrolls `bound` (+ `reset_cycles`) SMT copies of the design;
// an absurd value (e.g. from `--set lec.bound=2000000000`) builds billions of
// cycles and hangs. Callers validate against this ceiling and reject out-of-range
// values with a clean diagnostic. Real bounds are tiny (tests use <= 32).
inline constexpr int kLecMaxUnroll = 100000;

// "" if bound/reset_cycles are in [0, kLecMaxUnroll], else a human message.
inline std::string lec_options_range_error(const Lec_options& o) {
  if (o.bound < 0 || o.bound > kLecMaxUnroll) {
    return "lec.bound out of range (0.." + std::to_string(kLecMaxUnroll) + "), got " + std::to_string(o.bound);
  }
  if (o.reset_cycles < 0 || o.reset_cycles > kLecMaxUnroll) {
    return "lec.reset_cycles out of range (0.." + std::to_string(kLecMaxUnroll) + "), got " + std::to_string(o.reset_cycles);
  }
  if (o.phase != "after_reset" && o.phase != "just_reset" && o.phase != "free_toreset" && o.phase != "full") {
    return "lec.phase unknown '" + o.phase + "' (after_reset | just_reset | free_toreset | full)";
  }
  if (o.engine != "bmc" && o.engine != "ind" && o.engine != "ic3" && o.engine != "auto") {
    return "lec.engine unknown '" + o.engine + "' (bmc | ind | ic3 | auto)";
  }
  if (o.semdiff != "none" && o.semdiff != "structural") {
    return "lec.semdiff unknown '" + o.semdiff + "' (none | structural)";
  }
  if (o.decompose != "auto" && o.decompose != "true" && o.decompose != "false" && o.decompose != "on" && o.decompose != "off"
      && o.decompose != "1" && o.decompose != "0") {
    return "lec.decompose unknown '" + o.decompose + "' (auto | true | false)";
  }
  if (o.cones != "auto" && o.cones != "true" && o.cones != "false" && o.cones != "on" && o.cones != "off" && o.cones != "1"
      && o.cones != "0") {
    return "lec.cones unknown '" + o.cones + "' (auto | true | false)";
  }
  if (o.conelimit < 0) {
    return "lec.conelimit must be >= 0 (0 = ABC default), got " + std::to_string(o.conelimit);
  }
  return {};
}

// Prove the (combinational) outputs of `ref` and `impl` equal for all inputs,
// matching primary inputs by name. Returns Proven / Refuted(+witness) /
// Unknown(+detail).
//
// `sub_lib` (optional): name-hash Gid -> definition graph, used to flatten
// `Sub` instances inline during encoding (M5). Needed when a side is a
// hierarchical / standard-cell netlist (e.g. an ABC mapping whose cells resolve
// to gensim models); nullptr keeps the sound Sub -> Unknown.
Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts = {},
                         const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib = nullptr);

// Tuple-leaf <-> flat-bus port-shape divergence between a corresponding def
// pair: TRUE when at least one port is declared FLAT (`base`, W bits) on one
// side while the other side declares only LEAVES (`base.<field>` decls, any
// nesting depth, whose widths sum to W) — the Pyrope tuple-port vs
// SystemVerilog packed-bus lowering split. prove_equal() bridges the split
// with bundle compare points at its own top-level boundary, but a collapsed
// PROVEN child (a black box) is corresponded by port NAMES and cannot; the
// hierarchical driver uses this predicate to leave such a child OUT of the
// collapse set so it is descended (flattened into the parent) instead.
bool io_bundle_split(hhds::Graph* ref, hhds::Graph* impl);

// Run one proof in a fork-isolated worker. Used by the Taskflow hierarchy DAG:
// one task owns one child process, so the solver-process count is bounded by
// formal.jobs and cvc5 instances never execute concurrently in threads.
Query_result prove_equal_isolated(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts = {},
                                  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib = nullptr);

// Parse a lec.match correspondence spec into {ref_name, impl_name} pairs. Pure (no
// file IO — a caller resolves any leading "@FILE" to its text first). Pairs are
// separated by commas / semicolons / newlines; the two names within a pair by "="
// or whitespace; blank lines and "#" comments are skipped.
std::vector<std::pair<std::string, std::string>> parse_match_pairs(std::string_view text);

// Validate tier-2 uncertain {ref,impl} pairs against the two designs before
// injection (pair-hint replay re-validation; fresh same-process semdiff pairs
// are valid by construction but pass through the same gate). A pair survives
// iff BOTH canonical names resolve to exactly one top-level flop on their own
// side, neither name collides with the other side's flop space or with an
// explicit `base.match` entry (the alias is applied to both designs' walks, so
// a collision would silently remap an unrelated flop), the pair target is not
// already taken by an earlier pair, and the reset/init values are equal (a
// both-init-less pair qualifies; init is never paired with init-less). Each
// dropped pair contributes one "ref<->impl: reason" line to `reasons`.
std::vector<std::pair<std::string, std::string>> validate_uncertain_pairs(
    hhds::Graph* ref, hhds::Graph* impl, const Lec_options& base,
    const std::vector<std::pair<std::string, std::string>>& pairs, std::vector<std::string>* reasons = nullptr);

// ── 2f-verify: single-design property BMC (`lhd formal verify`) ─────────────
//
// Per-property verdict from prove_properties. A property is one fproperty Sub
// (a user assert / assert_always / assume materialized by tolg). Cycle indices
// are ABSOLUTE unroll indices (the after_reset reset-hold prologue occupies
// 0..reset_hold-1; plain `assert` is checked only in the run window,
// `assert_always` at every cycle, and in just_reset every cycle is checked).
struct Prop_result {
  std::string kind;   // assert | assert_always | assume
  std::string loc;    // source location ("" when tolg carried none)
  std::string msg;    // user message ("")
  std::string block;  // formal-block dotted name ("" = an fproperty in the design itself)
  // kind==assume classification (P1 assume discipline; "" for asserts and under
  // ignore_assumes):
  //   "input"     — the cond's cone reaches primary inputs (or free blackbox
  //                 outputs) only: an environment constraint by nature (inputs
  //                 are free, nothing to prove). Asserted at EVERY cycle,
  //                 prologue included; verdict stays Unknown / cycles -1;
  //                 disclosed ("under N input assume(s)").
  //   "internal"  — the cond depends on design state / memory: a PROOF
  //                 OBLIGATION, prove-then-use. Checked per cycle like a plain
  //                 assert; only a just-PROVEN cycle's fact constrains later
  //                 obligations (rule A), and only an inductive survivor
  //                 constrains the step frame (rule E). REFUTED = a hard error;
  //                 Unknown = NOT used, disclosed as unproven.
  //   "unchecked" — assume_nocheck_formal: a free constraint by explicit user
  //                 fiat; warned per encounter, disclosed distinctly.
  std::string aclass;
  Verdict verdict = Verdict::Unknown;
  // V3 verdict ladder: a bounded-proven assert that also survives the
  // simultaneous-induction step is PROVEN UNBOUNDED — true at every cycle of
  // every bound (the conjunction of survivors is inductive; the BMC run is its
  // base case), eligible as an unconditional helper everywhere.
  bool unbounded = false;
  int proven_to  = -1;  // deepest checked cycle proven (every checked cycle <= it is UNSAT)
  int refuted_at = -1;  // first cycle with a reachable violation (SAT)
  int unknown_at = -1;  // first cycle where the solver gave up (timeout/unknown);
                        // later cycles were not attempted for this property
  // Cumulative cvc5 time spent on THIS obligation's checks (BMC per-cycle checks
  // + its induction-rung candidate checks; cache hits cost ~0). P2 agent-report
  // signal — the report ranks stragglers by it. Serialized by the wire codec.
  long long solve_ms = 0;
  std::string witness;  // per-cycle input assignment reaching the violation (Refuted)
  // Structured, uncapped input trace for witness reproduction (Refuted only):
  // the same shape the lec engine fills, so `lhd formal verify --workdir` can
  // emit a formalfail.prp testbench + VCD exactly like lec's lecfail.prp.
  Witness_trace trace;
};

// Aggregate result of a prove_properties run: Refuted if any assert has a
// reachable counterexample; else Unknown if anything is unresolved (a
// per-property timeout, a contradictory assume set, an encode failure); else
// Proven — a BOUNDED verdict (no violation within `checked_steps` cycles).
struct Verify_result {
  Verdict     verdict = Verdict::Unknown;
  std::string detail;
  bool        oversize_refused = false;  // design-size gate refused (see Query_result::oversize_refused)
  int         checked_steps = 0;   // bound actually run
  int         reset_hold    = 0;   // after_reset prologue length (incl. pipeline flush)
  bool        reset_detected = false;  // a reset prologue actually pinned state[0] (a primary reset input
                                       // was found/applied). When false, the BMC starts from FREE flop
                                       // state, so a refute may rest on an unreachable initial state — the
                                       // compile tier must NOT treat it as reachable-from-reset (no hard error).
  int         n_assumes     = 0;   // user assumes in force (verdicts are conditional on them)
  bool        vacuous       = false;  // assume set contradictory: every "proof" was vacuous
  std::vector<Prop_result> props;  // one entry per fproperty, walk order
  // STRUCTURED timeout core (formal.minetimeout): indices into `props` of the
  // obligations cvc5 named as the toxic subset (the same set res.detail spells
  // in prose). Empty when the diagnosis is off / unavailable / found nothing.
  std::vector<int> timeout_core;
  // P3 mining output. Every entry passed the BASE proof (holds at every checked
  // BMC cycle, under the env assumes); `inductive` entries also survived the
  // joint Houdini induction step, so they are GENUINE invariants — safe to
  // paste as formal-block assumes (they re-prove on use; a wrong edit later
  // refutes them instead of corrupting a run). Non-inductive entries appear
  // only under mine=speculative. Candidate sources: solver learned literals
  // over state symbols, and range/equality templates over the registers in the
  // still-Unknown obligations' cones, seeded from a reachable model sample.
  struct Mined_invariant {
    std::string      pyrope;      // ready-to-paste block expression ("" = shape not expressible)
    std::string      smt2;        // solver-term rendering (debug / non-expressible shapes)
    std::string      provenance;  // "learned-literal@cyc N" | "template:range(key)" | ...
    std::vector<std::string> keys;     // state keys mentioned
    std::vector<int>         targets;  // indices of still-Unknown props whose cone overlaps
    bool                     inductive = false;
  };
  std::vector<Mined_invariant> mined;
  long long                    elapsed_ms = -1;
};

// 2f-verify V2: a formal-block MONITOR — the block's property statements
// compiled (through the real Pyrope pipeline, so expression semantics never
// diverge) into a tiny comb module whose inputs are the design signals the
// block references. The engine encodes it per cycle with each input bound to
// the design's encoded value for that cycle, and its fproperty obligations
// join the verdict table under the block's dotted name.
struct Monitor {
  hhds::Graph* graph = nullptr;  // the compiled monitor comb (caller owns lifetime)
  std::string  block;            // dotted block name (filter/report handle)
  // One input binding: the monitor's input port name <- a design signal.
  struct Bind {
    enum class Src { input, output, state };
    std::string ident;  // monitor input port
    Src         src = Src::state;
    std::string key;    // input/output port name, or the canon flop-state key
  };
  std::vector<Bind> binds;
  // Generated-source line -> original "file:line" (fproperty locs point into
  // the generated monitor file; the report shows the user's formal block).
  absl::flat_hash_map<int, std::string> line2loc;
  // Generated-source lines holding an `assume_nocheck_formal` statement (the
  // CLI rewrote the callee to `assume` so the monitor compiles): the engine
  // classifies these props "unchecked" — a free constraint by user fiat, never
  // a proof obligation, disclosed distinctly.
  absl::flat_hash_set<int> nocheck_lines;
};

// Prove the fproperty obligations of ONE design (plus any formal-block
// monitors) by BMC from reset: unroll cycle by cycle (same reset phases /
// prologue rules as prove_equal's bmc engine), check each obligation per cycle
// as a retractable checkSatAssuming, and re-assert every proven obligation as
// a fact ("frontier assume") so later cycles solve in the pruned space. Honors
// Lec_options bound / timeout / phase / reset_cycles / reset / witness; engine
// must be "bmc" (the only property engine). `sub_lib` resolves Sub instances
// exactly as in prove_equal.
Verify_result prove_properties(hhds::Graph* design, const Lec_options& opts = {},
                               const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib = nullptr,
                               const std::vector<Monitor>* monitors = nullptr);

}  // namespace livehd::lec
