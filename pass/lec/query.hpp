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
#include "phase_sched.hpp"
#include "solve_stats.hpp"

namespace livehd::lec {

// L1 relational query API. v1 covers the combinational equivalence client:
// prove_equal(ref, impl) under assume_equal(primary inputs). prove_distinct /
// is_sat are the duals/relatives (added as the clients land).
enum class Verdict { Proven, Refuted, Unknown };

// Machine-readable counterexample trace: the reproducible input sequence a
// REFUTED BMC run found, uncapped and grouped by cycle (the display `witness`
// string caps its input tokens for readability). `lhd lec` turns this into a
// self-contained Pyrope testbench (simfail_<top>.prp) that drives BOTH designs with
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
  // inherits), for the machine-readable simfail JSON and the source-mapped root
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

  // The ENCODER refused a cell/shape it does not model (Encoded::unsupported) —
  // e.g. a Latch, an unknown op. Like oversize_refused (and unlike a solver
  // give-up) this decided NOTHING and no extra budget can change it, so a driver
  // must exit non-zero regardless of formal.strict: an exit-0 "inconclusive"
  // here is read downstream as "verified", which makes every gate built on this
  // run vacuous (2f-latch M0).
  bool unsupported = false;

  // The miter was EMPTY: not one (output, cycle) pair entered the comparison —
  // either side has no outputs/state at all, or no output could be paired across
  // the two sides. Like `unsupported` (and unlike a solver give-up) this decided
  // NOTHING, and no extra budget can change it, so a driver must exit non-zero
  // regardless of formal.strict. Before this flag existed the empty miter was
  // reported as `Proven` with detail "no comparable outputs": `lhd lec` on an
  // empty module printed "PROVEN equivalent", status "pass", exit 0, with ZERO
  // warnings. A check that compares nothing is not a proof of anything.
  bool nothing_compared = false;

  // The Proven verdict is BOUNDED: BMC found no counterexample up to
  // `formal.bound`, which says nothing about deeper cycles. An INDUCTIVE proof
  // leaves this false -- that one is unbounded.
  //
  // It is a verdict QUALIFIER, not a verdict: the CLI reports a bounded proof
  // as INCONCLUSIVE (exit 7 -- "could not decide", NEVER exit 10, since no
  // counterexample was found), and `formal.strict=false` is the single escape.
  // It also propagates through hierarchical composition: a bounded
  // child must NOT discharge a parent's box premise, because the box contract
  // (see box_model=seq) is explicitly "from reset, identical input sequences
  // produce identical output sequences" -- unbounded.
  bool bounded = false;

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

  // cvc5 solve-insight accounting (formal.stats / --stats). Empty (solvers == 0)
  // when stats are off or no cvc5 query ran. Summed, never assigned, at every
  // point that merges two results (the auto portfolio, the case split, the
  // `full` phase pair) — a losing racer really did burn that CPU.
  //
  // TAIL field of the fork wire codec: solving happens in FORKED CHILDREN that
  // _exit(0), so anything not carried by put_cvc5_stats/get_cvc5_stats comes
  // back ALL ZEROS in the parent with NO warning. See the codec comment block at
  // query.cpp:340-351 (and its three repeats) for the four times that exact bug
  // shipped. Add a member here => extend BOTH codec halves.
  Cvc5_stats cvc5;
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
  // cvc5 int-blasting (solve-bv-as-int): translate the BV encoding to unbounded
  // integer arithmetic inside cvc5 (VMCAI'22 "iand" lazy refinement). Measured
  // split: arithmetic-rewrite miters (reassociated / distributed / commuted
  // multiplies) that neither abc nor BV bit-blasting finish in 60s prove in
  // ~0.1s as integers, while mask/extract/memory-heavy cones degrade to Unknown
  // (nonlinear integer arithmetic is undecidable). Hence:
  //   auto (default) — solve BV-first; a solver-give-up Unknown earns ONE
  //     int-blasted re-solve at the formal.min_timeout budget (int_blast_retry,
  //     called by the drivers — never by the engines, so the portfolio/tier-2
  //     recursion inside prove_equal retries at most once per driver query);
  //   off — never int-blast;
  //   iand | sum | bitwise | bv — force that cvc5 mode from the first solve.
  // Verdicts stay sound either way (the translation is equisatisfiable).
  // Main lec solve path only (the verify engine never int-blasts).
  std::string int_blast = "auto";
  int         bound   = 6;       // BMC / induction depth
  // `timeout` is a SOFT TOTAL budget, not a per-query cap: overshooting it is
  // fine, silently checking nothing is not. The hier driver spends it as total
  // WALL clock over the proof DAG (defs run concurrently under `jobs`, so a
  // summed budget would drain jobs-times faster than real time); the verify
  // engine spends it as total SOLVER time across the run. Either way one
  // `formal.timeout=T` is an actual total, not T per def / per obligation (the
  // D×T hazard). 0 = unbounded. Accounting is ON iff `timeout > 0 && rlimit == 0`
  // — `rlimit` is the deterministic CI path and owns the bound by itself, which
  // is why there is no separate mode knob.
  int         timeout = 0;
  // FLOOR, in seconds, under the soft total: once `timeout` is spent, a unit
  // that has NO verdict yet still gets at least this much solver time so it
  // earns a real Unknown/CEX instead of a silent "not checked". A "unit" is one
  // obligation (verify) or one def (hier lec).
  //
  // This is deliberately allowed to overshoot `timeout` — that is the point.
  // The bound is `timeout + (unsettled units x min_timeout)`, and what keeps
  // the second term at ONE floor per unit (not one per BMC cycle) is the FREEZE
  // in prove_properties: once the budget is spent, a unit that already has a
  // bounded proof stops deepening, and a unit with none takes its single floored
  // attempt and then latches `unknown_at`, which the cycle loop skips. Removing
  // that freeze turns this into units x cycles x floor.
  //
  // Default 20s (user ruling 2026-07-28). It was 1s — the value both floors
  // were hardcoded to before it became a knob — and 1s is far too small for a
  // unit to earn a real verdict on anything but a trivial def.
  //
  // The measured symptom on a wide design: minion_lec took
  // `minion_dcache_miss_handler` to REFUTED in 2253ms on one run and to UNKNOWN
  // on the next, because most defs land past the soft total and draw the floor
  // (`budget 120s target / 485s actual over 116 def(s) solved, 92 on the 1s
  // floor`). A verdict that flips run to run is unusable as an oracle to debug
  // against, and a 1s floor mints Unknowns that read as design problems when
  // they are pure scheduling.
  //
  // Lower it to trade Unknowns back for a tighter overrun; the bound is still
  // `timeout + (unsettled units x min_timeout)` and the run always reports
  // target/actual/units/floored, so the overrun is never silent.
  int         min_timeout = 20;
  // HARD wall backstop on a forked proof worker, as a multiple of `timeout`
  // (0 = off, the pre-2026-08-03 behavior). `timeout` is armed as cvc5
  // `tlimit-per`, which the ResourceManager can only check at a spendResource
  // point — so it cannot preempt ONE long call. A flat (box-free) miter takes
  // the eager bit-blaster path, where the whole query IS one CaDiCaL solve
  // inside BVSolverBitblast::postCheck, and there the limit never fires:
  // measured, dino PipelinedDualIssueCPU ran 71s of wall at `formal.timeout=5`
  // (14x) and minion `intpipe_csr_file` ran past 45 MINUTES at 120s (>20x, at
  // 13.7 GB RSS). Collapsed miters are unaffected — the lazy solver returns
  // between checkSats, so their legs land within a few percent of the cap.
  //
  // The multiplier is NOT slack for one query: an isolated worker runs the
  // whole ind->bmc ladder IN-PROCESS (`_isolated_worker`: no nested forks), so
  // its LEGITIMATE wall is one `timeout` per leg. 3 covers the 2-leg auto
  // ladder plus a margin; a single-engine worker simply never reaches it.
  //
  // Enforced by the PARENT (spawn_isolated_worker), which owns the child pid —
  // the only place a runaway can actually be stopped. Killing a worker can only
  // LOSE information, so the degrade is a witness-free Unknown, never a
  // verdict: sound by construction, and it SAYS the backstop fired.
  int         hard_timeout_mult = 3;
  // Independent budget (seconds, 0 = off) for the SPECULATIVE post-run phase:
  // the hier straggler list, the cvc5 timeout-CORE diagnosis (which subset of
  // still-Unknown obligations is jointly toxic), and P3 invariant MINING. All
  // three are diagnosis — they cost solver time and can never change a verdict —
  // which is why they share one budget that is never drawn from `timeout`
  // (mining's induction rung must be able to open even when the main run is
  // exhausted, which is the normal mining scenario).
  int         spec_mining_timeout = 0;
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
  bool        strict = true;     // treat an inconclusive UNKNOWN (no counterexample, the
                                 // solver merely could not complete the proof) as a hard
                                 // failure. DEFAULT TRUE: an inconclusive run PROVED
                                 // NOTHING, and exiting 0 makes it indistinguishable from
                                 // a real proof to any gate built on top of it. Opting out
                                 // (`--set formal.strict=false`) downgrades it to a
                                 // deferred warning that exits cleanly -- a deliberate
                                 // choice the caller makes, never the default.

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
  // s0 constraints can mask a real bounded CEX — a false PASS). prove_equal
  // drops the speculative pairs and retries BMC through the existing prologue:
  // a detected reset establishes state normally; with no reset, otherwise-
  // uninitialized reference state starts as tracked '?' (or canonical zero
  // under gold_x=zero), matching hardware's unspecified power-on contract.
  // Only that pair-free bounded result, or an unbounded inductive Proven, is
  // accepted. The latter is self-certifying (any inductive, output-implying,
  // initially-true relation certifies) PROVIDED the paired reset/init values
  // are equal, which the producer guarantees and validate_uncertain_pairs
  // re-checks on hint replay.
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

  // `formal.ignore_memory` — memories EXCLUDED from the comparison by the user.
  // Matched by full hier name, canonical name, or bare leaf name (the two
  // front-ends spell a memory differently, so one entry covers both sides).
  //
  // An ignored memory is BLACKBOXED: each read dout becomes ONE SHARED free
  // symbol per (memory, port, cycle) across ref and impl, and no next-state
  // array is built. The logic AROUND it is still proved; nothing is claimed
  // about what it STORES. This is a disclosed ASSUMPTION in exactly the sense
  // `formal.lec.trust` is for a def, and it is the escape hatch for a memory
  // the encoder refuses to model — today, one with PER-PORT clock edge
  // polarity (Ntype::Memory_posclk_mixed).
  std::vector<std::string> ignore_memory;
  // Proven-module black-box collapse (`lhd lec --collapse <def>` / lec.collapse):
  // module-def names the driver has ALREADY proven equivalent in isolation, which
  // are FORCED to the sound black-box path even when they could be flattened (a
  // combinational def supplied via --lib). Their outputs become shared UF(inputs)
  // symbols and their inputs become miter compare points, so the parent proof
  // stops re-solving the leaf's internals ("equal inputs => equal outputs", already
  // discharged when the leaf was proven). Matched case-sensitively (name policy).
  // The bottom-up hierarchical driver fills this with its per-round proven set.
  std::vector<std::string> collapse;

  // Trusted-module assume-equal blackbox (`lhd lec --trust <def>` / lec.trust):
  // module-def names ASSUMED equivalent WITHOUT a proof — the escape hatch for a
  // cell the encoder cannot yet model (a Latch: "sequential op 'latch' not
  // supported yet"). The bottom-up driver skips proving these defs entirely and
  // force-blackboxes their instances in every parent (it seeds them into the
  // effective `collapse` set, so the encoder treats them exactly like a proven
  // collapse — outputs become shared UF(inputs), inputs stay miter compare
  // points), and — unlike a proven collapse — KEEPS them boxed through the
  // flat-confirm re-solve of a refute elsewhere, so a real counterexample in an
  // untrusted cone is still reported (never re-flattened into the unmodeled
  // cell). Boxing over-approximates, so a PASS under trust is a real PASS
  // (modulo the trusted internals) while a refute can only be spurious at a box
  // boundary, never a missed bug outside the trusted cones. The encoder never
  // reads this field — only the driver does; it is a DISCLOSED assumption, so a
  // top proven with a non-empty trust list is reported "PROVEN under N trusted
  // def(s)", never a silent unconditional pass. Matched by entity name (or
  // either side's full spelling), like collapse.
  std::vector<std::string> trust;

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

  // FORMAL PHASE SCHEDULE (2f-lec / 2f-latch M10). When true (the default), a
  // design holding a latch, a negedge endpoint or a recognized clock gate is
  // encoded over FOUR ordered microsteps per source period instead of being
  // rewritten by `pass.single_edge` first. Read-only: no coloring, no graph
  // rewrite, no synthesized phase counter, no timing state threaded through
  // module ports — so it composes across hierarchy, which the M8 rewrite never
  // could (it refuses a latch or a negedge flop inside a def outright).
  //
  // Set false to fall back to the M8 preflight. That path stays a landed
  // standalone transform and the independent Icarus oracle either way; this flag
  // only decides which one FORMAL depends on.
  bool phase_sched = true;

  // BOX MODEL for a PROVEN collapsed child (formal.lec.box_model).
  //
  //   true  (seq, the default) — the child is a SEQUENCE TRANSDUCER and nothing
  //     more. Its proof says: from reset, identical input sequences produce
  //     identical output sequences. So the parent proves the INPUTS equal
  //     (`bbin:` obligations) and shares ONE free symbol per (instance, port,
  //     cycle) for the outputs. No state cut, no uninterpreted function, no
  //     congruence rule — comb and stateful children are the same object here,
  //     because from the caller's side they are.
  //   false (uf) — the legacy `Comb_box` / `State_box` encoding: UF(inputs) for
  //     a stateless child, UF_out(state)/UF_next(inputs,state) plus a threaded
  //     state cut for a stateful one. Kept as an escape hatch and for A/B.
  //
  // Why seq is better and not just simpler: the UF boxes force the query into
  // QF_AUFBV, which DISABLES cvc5's eager bit-blaster (the driver's flat retry
  // exists to work around exactly that); they need `boxcong` to recover
  // "equal inputs => equal next state"; the stateless variant emits NO `bbin:`
  // obligations at all, so a comb box's inputs were never checked; and the
  // stateful variant threads a per-instance state cut whose correspondence can
  // cross two interchangeable instances and refute two equivalent designs.
  // None of that is needed to justify "assume the outputs are equal".
  bool box_seq = true;

  // DESIGN-WIDE CLOCK FOREST (2f-lec "Clock-graph propagation"), resolved
  // TOP-DOWN once per design and carried BY VALUE so it survives the isolated
  // worker fork. See Clock_forest for what it fixes and why per-endpoint
  // bottom-up resolution cannot: an implicit clock has no cone to walk, and a
  // cone that stops at an opaque boundary names the CHILD's port. Empty means
  // every port is its own root — the pre-propagation behaviour.
  Clock_forest clock_forest;

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
  bool                             _isolated_worker = false;  // one global-pool child: no nested forks
  // Internal-only mode for the speculative-pair recovery leg. With a detected
  // reset it is inert. Without one, otherwise-uninitialized reference flop state
  // gets a full '?' plane under gold_x=ignore (implementation power-on remains
  // arbitrary); gold_x=zero instead gives both sides canonical zero. This avoids
  // an adversarial independent-state CEX while keeping the chosen no-reset/X
  // contract explicit in the verdict detail.
  bool                             _init_no_reset   = false;

  // formal.stats / --stats: capture + report cvc5 solve statistics (also
  // registers the cvc5::Plugin -- makes the solve ~8x slower). OFF by default
  // and strictly zero-cost when off: with stats false the engines pass a NULL
  // accumulator, so no Solve_probe impl is built, no plugin is registered and
  // no statistics snapshot is taken. A plain bool -- the expensive plugin tier
  // rides this same flag (user ruling); there is deliberately no "deep" value.
  bool stats = false;

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
  // verify CLI leaves this false: there, an assume is a proof obligation
  // (prove-then-use), and only assume_nocheck is a free env constraint.
  bool ignore_assumes = false;

  // Verify-obligation cache hooks. The engine computes a rule-F key downstream
  // of encoding; the CLI supplies the persistent store without coupling this
  // reusable library to lhd/formal_cache.
  std::function<bool(std::string_view)> verify_cache_lookup;
  std::function<void(std::string)>      verify_cache_store;

  // P3 mining tier. Mining itself is gated by spec_mining_timeout>0 (it spends that
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
  if (o.int_blast != "auto" && o.int_blast != "off" && o.int_blast != "iand" && o.int_blast != "sum"
      && o.int_blast != "bitwise" && o.int_blast != "bv") {
    return "lec.int_blast unknown '" + o.int_blast + "' (auto | off | iand | sum | bitwise | bv)";
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

// int_blast=auto second leg: when the BV-first solve `first` came back a
// SOLVER-GIVE-UP Unknown (not unsupported/oversize/nothing-compared — those no
// budget can change), re-solve ONCE with cvc5's int-blasting (iand) at the
// formal.min_timeout budget and adopt the retry iff it SETTLES; a retry Unknown
// keeps `first` (its detail names the real bottleneck) with a note appended.
// DRIVER-level only — call it once per driver query, after any flat-confirm,
// never from inside the engines: prove_equal recurses (tier-2 discipline, the
// auto portfolio's per-engine attempts), and a retry in that recursion would
// fire once per inner attempt instead of once per query. `isolated` selects
// prove_equal_isolated for the re-solve (match how `first` was produced).
// No-op unless opts.int_blast == "auto".
Query_result int_blast_retry(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts, Query_result first,
                             const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib = nullptr,
                             bool isolated = false);

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
  std::string block;  // formal-block dotted name + "@instance" ("" = an fproperty in the design itself)
  // The obligation's ASSUME SCOPE: the formal block's dotted name WITHOUT the
  // "@instance" suffix, so every instance context of one authored block shares
  // it ("" = the design tier). Blocks are independent tests (user ruling,
  // 2026-07-25): an obligation is discharged under exactly its own scope's
  // assumes plus the always-in-force design-tier ones, never under a sibling
  // block's. See prove_properties' per-scope activation literals.
  std::string scope;
  // kind==assume classification (P1 assume discipline; "" for asserts and under
  // ignore_assumes). EVERY assume except "unchecked" is a PROOF OBLIGATION,
  // prove-then-use: checked per cycle like a plain assert; only a just-PROVEN
  // cycle's fact constrains later obligations (rule A), and only an inductive
  // survivor constrains the step frame (rule E). REFUTED = a hard error;
  // Unknown = NOT used, disclosed as unproven. The input/internal split is
  // DIAGNOSTIC only (same discipline, different hint on failure):
  //   "input"     — the cond's cone reaches primary inputs (or free blackbox
  //                 outputs) only. Over free inputs such a constraint can
  //                 never be proven unless it is a tautology, so a refute
  //                 earns the "spell it assume_nocheck" hint.
  //   "internal"  — the cond depends on design state / memory: a real claim
  //                 about the design; a refute means the design breaks it.
  //   "unchecked" — assume_nocheck (and the fcore spelling
  //                 assume_nocheck_formal): a free constraint by explicit user
  //                 fiat; never checked, disclosed distinctly.
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
  // R1 Phase 2 — antecedent (guard) diagnostics. `guarded` = the property was
  // written inside an `if`/`match` arm, so the obligation is `guard implies
  // cond`, not `cond`. `vacuous_guard` = that antecedent is UNSAT over every
  // checked cycle, i.e. the property held only because it was never exercised.
  //
  // This is STRICTLY DIAGNOSTIC and never moves a verdict: unlike a
  // contradictory assume set (which makes the proof unsound to rely on), a
  // vacuous antecedent leaves the obligation genuinely true — it just checked
  // nothing. So an inconclusive vacuity query leaves `vacuous_guard` false
  // rather than accusing (the opposite of the conservative direction the
  // per-scope check takes, and deliberately so).
  bool guarded       = false;
  bool vacuous_guard = false;
  std::string witness;  // per-cycle input assignment reaching the violation (Refuted)
  // Structured, uncapped input trace for witness reproduction (Refuted only):
  // the same shape the lec engine fills, so `lhd formal verify --workdir` can
  // emit a simfail_<formal-test>.prp testbench + VCD exactly like LEC.
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
  bool        unsupported      = false;  // encoder REFUSED a cell/shape (see Query_result::unsupported)
  int         checked_steps = 0;   // bound actually run
  int         reset_hold    = 0;   // after_reset prologue length (incl. pipeline flush)
  bool        reset_detected = false;  // a reset prologue actually pinned state[0] (a primary reset input
                                       // was found/applied). When false, the BMC starts from FREE flop
                                       // state, so a refute may rest on an unreachable initial state — the
                                       // compile tier must NOT treat it as reachable-from-reset (no hard error).
  int         n_assumes     = 0;   // user assumes in force (verdicts are conditional on them)
  // An assume set was contradictory, so the proofs it governed were vacuous.
  // With per-scope assume scoping this is now a PER-SCOPE property: `vacuous`
  // is the roll-up (true if ANY scope was contradictory) and `vacuous_scopes`
  // names them — "" for the design tier, else the formal block's dotted name.
  // A contradictory BLOCK voids only that block's obligations; a contradictory
  // DESIGN tier voids everything (every scope sits on the design frame).
  bool                     vacuous = false;
  std::vector<std::string> vacuous_scopes;
  // Soft-budget accounting (formal.timeout is a TARGET, not a cap). What was
  // actually spent against it, over how many units, and how many of those ran on
  // the formal.min_timeout floor — the floored ones ARE the overrun, so the two
  // numbers together say whether to raise the target or lower the floor.
  // budget_target_s == 0 means no budget was in force (unbounded / rlimit tier).
  int       budget_target_s = 0;
  long long budget_spent_ms = 0;
  int       budget_units    = 0;  // obligations (verify) actually put to the solver
  int       budget_floored  = 0;  // of those, how many ran on the floor
  int       budget_floor_s  = 0;
  std::vector<Prop_result> props;  // one entry per fproperty, walk order
  // STRUCTURED timeout core (formal.spec_mining_timeout): indices into `props` of the
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

  // cvc5 solve-insight accounting (formal.stats / --stats) — the verify twin of
  // Query_result::cvc5, with the same TAIL-of-the-wire-codec discipline: the F3
  // verify strategy race FORKS, so a field the serialize_verify /
  // deserialize_verify pair does not carry comes back ALL ZEROS in the parent
  // and the report silently prints a wall of zeros on the default (forking)
  // path while being correct under --workdir. See query.cpp:340-351 (and the
  // repeats at :585-592, :594-597, :606-610) for the four shipped instances of
  // exactly that bug. Add a member here => extend BOTH codec halves.
  Cvc5_stats cvc5;
};

// 2f-verify V2: a formal-block MONITOR — the block's property statements
// compiled (through the real Pyrope pipeline, so expression semantics never
// diverge) into a tiny comb module whose inputs are the design signals the
// block references. The engine encodes it per cycle with each input bound to
// the design's encoded value for that cycle, and its fproperty obligations
// join the verdict table under the block's dotted name.
struct Monitor {
  hhds::Graph* graph = nullptr;  // the compiled monitor comb (caller owns lifetime)
  std::string  block;            // dotted block name + "@instance" (filter/report handle)
  // Assume SCOPE: the block's dotted name with no "@instance" suffix. Every
  // instance context of one authored block shares a scope, so a block that
  // binds to N instances still has ONE assume set (all N in force together —
  // that is one test), while a DIFFERENT block never sees it. Leave empty only
  // for a monitor that should share the design tier's always-in-force assumes.
  std::string scope;
  // One input binding: the monitor's input port name <- a design signal.
  struct Bind {
    enum class Src { input, output, state };
    std::string ident;  // monitor input port
    Src         src = Src::state;
    std::string key;    // input/output port name, or the canon flop-state key
    // HISTORY sample: bind to the signal's value `delay` cycles EARLIER
    // (`past(x, delay)`), not this cycle's. 0 = the current cycle, the ordinary
    // case. The monitor itself stays COMBINATIONAL — a flop inside it would be
    // a fresh free symbol per step and silently refute tautologies, which is
    // why the CLI refuses a stateful property. History is resolved by the
    // ENGINE instead: prove_properties keeps each cycle's inputs/outputs/state
    // and indexes the unroll, so the term is the very one asserted at cycle
    // `cyc - delay`. An obligation is SKIPPED for cycles with cyc < delay
    // (there is no such history yet) rather than binding a free symbol, which
    // would manufacture counterexamples out of unconstrained history.
    int delay = 0;
  };
  std::vector<Bind> binds;
  // Generated-source line -> original "file:line" (fproperty locs point into
  // the generated monitor file; the report shows the user's formal block).
  absl::flat_hash_map<int, std::string> line2loc;
  // Generated-source lines holding an `assume_nocheck` (or the fcore spelling
  // `assume_nocheck_formal`) statement (the CLI rewrote the callee to `assume`
  // so the monitor compiles): the engine classifies these props "unchecked" —
  // a free constraint by user fiat, never a proof obligation, disclosed
  // distinctly. Every other `assume` is a proof obligation (prove-then-use).
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
