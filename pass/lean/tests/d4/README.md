# Direction 4 runtime-certificate sweep: target sets and provenance

The sweep driver is `scripts/run_d4_runtime_sweep.sh`.  It runs
`RTL --lhd--> <mod>.dcert --CertIO.loadCert--> runChecked` per block and never
elaborates a generated `_Lgraph.lean`.  Everything resolves relative to the
worktree the script lives in; required inputs are validated up front and the run
fails closed rather than reporting a short corpus.

## CORE-ET — 122 blocks, authoritative

`coreet_122.list` is generated from `pass/lean/SWEEP_b1-b2.tsv` column 1:

    tail -n +2 pass/lean/SWEEP_b1-b2.tsv | cut -f1 | sort > coreet_122.list

122 entries, 122 unique.  `VERIFIED_COMPILER_STATUS.md:378` names that TSV as the
record of "all 122 modules", and its verdict column accounts for every one:

    85  PROVEN
    24  NO_EMIT(compile)
    11  NO_EMIT(timeout)
     2  NO_EMIT(filelist)

So 37 of the 122 did not emit a certificate in the recorded sweep.  Those are
emission failures, and a runtime sweep must report them as such -- a coverage
percentage over "blocks that happened to emit" would be measuring the wrong
denominator.

RTL corpus: `COREET_ROOT`, default `/HOMEROOT/projects/core-et`.  External to
this repository, validated by the driver.

## CORE-ET 122-block runtime result

`coreet_122_runtime_rows.tsv` + `coreet_122_runtime_provenance.txt`, from
`run_d4_runtime_sweep.sh coreet coreet_122.list <out> 4` at the ordinary 8192 KB
stack.  122 rows, 122 unique modules, all 18 fields, `reconciled=YES`, 1 h 37 m
wall at 4 concurrent.  Classified by STAGE over the full 122 denominator:

     82  ran            emitted, parsed, compilesOk=true, runChecked returned
     37  emission       no .dcert  (24 compile + 11 timeout + 2 filelist)
      3  runtime        emitted and then panicked in the evaluator

The 37 emission failures are EXACTLY the 37 the recorded sweep
(`SWEEP_b1-b2.tsv`) lists as `NO_EMIT`, module for module.  All 85 historical
`PROVEN` blocks emitted here.

The three runtime failures are a real, reproducible finding, not a harness
artifact:

    minion_dcache_tensor_load    9349 sources
    vpu_ml                       4455 sources
    vpu_tensorquant              1262 sources

each dying with `INTERNAL PANIC: Nat.pow exponent is too big` from
`lean --run`.  **That table is the PRE-REPAIR state and is kept as the record of
the defect.**  The cause was confirmed and fixed -- see the next section -- and
all three now run.

## Runtime repair: `Op_SHL` with a runtime shift operand

The three panics were one defect.  `Op_SHL` was spelled

    mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat)

which is the right MEANING -- a left shift modulo `2 ^ w` -- but it builds
`2 ^ k` before `% 2 ^ w` can discard it, and `k` is a runtime value.  An
instrumented evaluator trace reached these exact residual shifts:

    minion_dcache_tensor_load  binding 1360  rshl width  66  exponent 8589934576
    vpu_ml                     binding 1786  rshl width 258  exponent 8589934368
    vpu_tensorquant            binding  730  rshl width 258  exponent 8589934496

A 33-bit exponent is past Lean's `Nat.pow` guard, so the process died before the
modulus ran.

The repair is `bv_shl_bounded` in `Translation/LGraphModel.lean`: when
`k >= w` the result is already zero modulo `2 ^ w`, so that case returns
`mk_bv w 0` WITHOUT constructing the power.  This is not a guard and not a catch
-- `bv_shl_bounded_eq` proves

    bv_shl_bounded w a k = mk_bv w (a * (2 : Int) ^ k)

for ALL `w`, `a` and `k`, with no side condition.  All three spellings of the
operator use it (`denote_op`, `eval_op`, and residual `rshlV`), so source and
target semantics stay aligned, and `denote_op_shl_eq_eager` /
`eval_op_shl_eq_eager` / `rshlV_eq_eager` each prove the new spelling equals the
old `shlEagerFold` pointwise.  `eval_op_correct` and `rshlV_correct` are
unchanged and still hold, and `OpBridge.shl_bridge` -- the BitVec bridge
`pass_lean.cpp:3460` emits for a two-input `Op_SHL` -- keeps its statement: only
its opening `show` moved to the bounded body, followed by `bv_shl_bounded_eq`.  No new axioms: every one of those theorems is
`[propext, Quot.sound]` or the standard `[propext, Classical.choice, Quot.sound]`
-- no `sorryAx`, no `Lean.ofReduceBool`.

Regression: `formal/lean/probes/d4_shl_regression.lean`, driven by
`pass/lean/tests/d4/shl_regression.sh`.  It is `#guard`s, not commented
`#eval`s, so a wrong answer fails the file, and it ships with two controls that
stop it passing vacuously: a mutated expectation must fail, and the pre-repair
spelling must still panic on the real exponent.  Covered: `w=8,a=1,k=7 -> 128`,
`k=8 -> 0`, a 33-bit `k -> 0` without panic, the three real width/exponent pairs,
and in-range exactness at width 66 so the repair cannot degenerate into
"return 0 more often".

### Replay of all 85 retained certificates

`coreet_85_replay_rows.tsv`, `coreet_85_replay_digest_compare.tsv`,
`coreet_85_replay_provenance.txt`, from

    scripts/replay_d4_certs.sh pass/lean/tests/d4/coreet_122_runtime_rows.tsv <out>

at the ordinary 8192 KB stack.  The replay holds the CERTIFICATES FIXED: each
one's SHA-256 is checked against the recorded table before it is run, and a
mismatch is refused rather than measured.  All 85 matched.  Result:

    85  rows, 85 declared, every row 18 fields, reconciled=YES
    85  PARSED / compilesOk=true / RAN
    82  digests identical to the recorded run
     3  new digests -- the three that could not run before
     0  digests changed
     0  certificates rejected

The three new ones:

    minion_dcache_tensor_load   9225711661718483716
    vpu_ml                     13992186387056089528
    vpu_tensorquant             6643065759084244567

Combined with the 37 `NO_CERT` rows of the recorded sweep -- unchanged, since
emission was not re-run and no front-end input moved -- the 122-block corpus now
stands at **85 RAN + 37 NO_CERT, and zero runtime failures**.

Two provenance notes.  The replay was run on the working tree that became the
commit carrying this section, so it records `d4_tracked_dirty=YES`.  And its
`worktree=` is the `/WORKROOT` spelling of the same directory the
earlier evidence records as `/HOMEROOT` -- one filesystem, two mount paths.

#### The gate was not closed; now it is

The first version of this replay counted `not_ran` and then LEFT IT OUT of the
exit predicate.  A review probe supplied a MALFORMED certificate carrying its
correct recorded SHA-256: the replay produced one well-shaped `PARSE_ERROR` /
`NOT_RUN` row, reconciled cleanly, reported `ran=0` -- and exited 0.  A replay
that measured nothing reported success.

There are now TWO verdicts, because they answer different questions:

    reconciled        SHAPE only: every declared certificate produced exactly
                      one 18-field row.  Says nothing about the contents.
    runtime_complete  RESULT: every declared certificate PARSED, compiled, and
                      RAN; none rejected; no recorded digest changed.

The command's exit status is `runtime_complete`.  Shape alone never passes.

`replay_gate_controls.sh` pins all four failure kinds separately, so they cannot
be confused with one another, plus a positive control.  It builds its own
certificate with `probes/d4_write_tiny_cert.lean` rather than borrowing one from
an untracked sweep directory.  All five pass:

    positive                a good certificate replays, exit 0
    malformed, SHA correct  exit 1 for the RUNTIME failure
                            (cert_rejected=0, reconciled=YES, runtime_complete=NO)
    certificate gone        exit 1 as CERT_MISSING
    recorded SHA wrong      exit 1 as CERT_SHA_MISMATCH, never run
    digest moved            exit 1 as CHANGED

### Provenance correction in the sweep driver

`coreet_122_runtime_provenance.txt` records
`lake_version=... (Lean version 4.34.1)`.  **That field was wrong.**  elan picks
the toolchain from the cwd, and the driver measured `--version` at the invocation
directory while every row is produced by `cd formal/lean && lake env lean`, where
`lean-toolchain` pins 4.31.0.  The recorded version therefore named a toolchain
that produced no row.  The driver now measures it in `$ROOT/formal/lean` and
records `lake_version_cwd=` alongside.  The rows themselves are unaffected -- the
replay reproduces 82 of 82 digests byte for byte.

**PREREQUISITE.**  `scripts/coreet_patch_srcs.py` must have been run: two
CORE-ET files use an identifier before declaring it, slang rejects that, and
without the substitution map 26 blocks that do emit are reported as emission
failures (measured: 63 `NO_CERT` without, 37 with).  The driver now fails closed
on the missing map rather than producing that wrong denominator.

## CVA6 — the canonical 30, and a six-block pilot that runs here

Full provenance is in `CVA6_MODULE_SET_PROVENANCE.md`.  In brief:

* The **canonical 30** exist and are tracked -- on
  `direction-3-translation-validation`, commit `7f5b1bd27`, as
  `pass/lean/manifests/cva6_30_frozen.tsv`.  They are the 30 blocks of the
  authoritative 78 that had a certificate at freeze time, ranked by node count.
  Copied here as `cva6_30.list`; the manifest's own `target30_sha256`
  recomputes exactly, and all 30 are inside `cva6_78.list`.
  **The earlier claim in this file that no 30-block list existed was wrong** --
  it was true of this branch, not of the repository.
* The **authoritative 78** come from `cva6_blocks.txt` at openhwgroup/cva6
  `6cb200105`, derived twice (that file's sections 1-9, and
  `CVA6_COVERAGE_PLAN.md` Phase 2) with an empty symmetric difference.

### The foundation is built here now, not borrowed

`scripts/build_cva6_foundation.sh` runs bender -> slang -> `gen_cva6_wrappers.py`
against ONE named CVA6 revision and ONE named config, and writes a manifest that
pins every input by SHA-256 (`cva6_foundation_provenance.txt`,
`cva6_foundation_wrappers.tsv`).  Nothing is read from
`/HOMEROOT/projects/livehd-new`.

    cva6_rev  6cb200105fb9441d170e45786125a737fab98e91  (tracked tree CLEAN)
    target    cv64a6_imafdc_sv39_hpdcache_wb
    filelist  353 lines      slang: 0 errors, whole core
    wrappers  52 of 78 generated | 26 not in this config | 0 failures
              30 of the canonical 30 generated (cva6_30_vs_foundation.tsv)

Two fail-closed properties, both of which a review probe defeated in the first
version and both of which now hold:

* **The published corpus is wholly fresh.**  Generation goes to a staging
  directory and a module's status comes from THIS invocation's explicit
  `GEN`/`SKIP` line -- never from "a file with that name exists".  A planted
  `ghost_gate.sv` used to survive a `SKIP ghost` and be reported GENERATED.
* **Every input is bound to ONE clean, named source state.**  The builder
  rejects tracked modifications in `CVA6_ROOT` (a dirty tree at R is not R), and
  before any block the sweep verifies canonical root path, revision,
  cleanliness, config hash, filelist hash, generator hash, wrapper directory,
  module list + hash, and the hash of the WHOLE wrapper corpus against one
  required foundation manifest.  A fake non-git root holding only a copy of the
  config package used to run to completion and write `reconciled=YES`.

The stale-corpus problem is now measured, not asserted: `alu` from the other
checkout's `temp/wrap_all` gives 429 sources / 436 nodes; regenerated here it
gives **6,137 / 6,597**, which is exactly what `cva6_30.tsv` records.

The sweep driver REQUIRES `CVA6_ROOT` and a foundation manifest for the cva6
corpus.  It had been pinning the filelist and the wrappers to this worktree while
`run_cva6_module_lean_stress.sh` silently took the CONFIG PACKAGE from a default
checkout -- a cross-revision build that would still have produced clean rows.

`cva6_foundation_controls.sh`, 10/10: unset root, root without the config, FAKE
ROOT WITH THE CONFIG, wrong revision in the manifest, one wrapper swapped
anywhere in the corpus, DIRTY tracked source at the builder, PLANTED STALE
wrapper, corpus self-consistency, list/manifest consistency (30/30), and a
positive control that still runs a block.

### Six-block pilot: 6/6, every block from the canonical 30

`pilot_cva6_six.list`, `cva6_pilot_six_rows.tsv`,
`cva6_pilot_six_provenance.txt`.  Every certificate was FRESHLY EMITTED by this
worktree's `lhd`; no `.lean` file and no converted certificate was reused.
Ordinary 8192 KB stack, 18 fields per row, `reconciled=YES`.

| block | sources | nodes | flops | mem | status | run_ms |
|---|---|---|---|---|---|---|
| `ariane_regfile` | 1,034 | 1,121 | 32 | 0 | PARSED / true / RAN | 20 |
| `instr_scan` | 1,235 | 1,264 | 0 | 0 | PARSED / true / RAN | 119 |
| `btb` | 1,784 | 1,782 | 64 | 0 | PARSED / true / RAN | 3,563 |
| `decoder` | 8,340 | 8,938 | 0 | 0 | PARSED / true / RAN | 364 |
| `scoreboard` | 9,396 | 9,637 | 243 | 0 | PARSED / true / RAN | 38,783 |
| `cva6_hpdcache_subsystem` | 97,741 | 108,612 | 542 | **12** | PARSED / true / RAN | 10,261 |

Shapes: combinational (`instr_scan`, `decoder`), sequential (`ariane_regfile`,
`btb`, `scoreboard`), large (`scoreboard`, and `cva6_hpdcache_subsystem` at
108,612 nodes), memory-bearing (`cva6_hpdcache_subsystem`, 12 memories).

`cva6_hpdcache_subsystem` is the block this file previously recorded as
producing NO certificate at all.  It emits, parses, compiles and runs from the
regenerated in-tree corpus, and its shape (97,741 / 108,612 / 542 / 12) is within
a few tenths of a percent of the frozen manifest's 97,774 / 108,666 / 542 / 12.

Reproducible, not just green: the pilot has been emitted from scratch THREE
times -- the third under the corrected fresh-publication / clean-source
foundation -- and all six produced BYTE-IDENTICAL certificates (same
`cert_sha256`) and identical digests every time.  (An earlier run used
`csr_buffer`, which is NOT one of the canonical 30; it was replaced by
`ariane_regfile` and the pilot re-run.)

The wrapper-corpus hashes in provenance are over `"<module> <digest>"` pairs, not
over `sha256sum <path>` output -- the latter folds the DIRECTORY into the value,
and this worktree is reachable as both `/HOMEROOT/...` and
`/WORKROOT/...`, so the same corpus would have hashed differently
depending on which spelling the operator used.

### Blockers found on this route, preserved as failures

| block | failure |
|---|---|
| `cva6_hpdcache_wrapper` | **CLOSED** -- and it was never an RTL problem.  See "The last canonical-30 blocker" below. |
| `cva6_tlb` | emission exceeded 32 GB RSS and was stopped by hand as a shared-machine courtesy.  Exploratory only, not a pilot block, and NOT a measured result. |

CLOSED since the previous commit: `csr_regfile` and `id_stage`, the last two
canonical-30 gaps.  Their `sdtrig_decoder_action_{o,i}` port is
`logic [63:0] p [0:0]`, a ONE-element unpacked array; the generator now carries
the packed element at the gate boundary and wires a one-element internal array to
the DUT, in the port's direction.  Deliberately narrow -- `[1:0]`, `[3:0]`,
`[1:1]`, unknown element types and nested unpacked elements are all refused,
because flattening a multi-element unpacked array would silently change the
design.  Both now run:

    csr_regfile  32,853 / 34,897  136 flops  0 mem   PARSED / true / RAN
    id_stage     10,503 / 11,204   30 flops  0 mem   PARSED / true / RAN

`cva6_unpacked_bridge_rows.tsv`; controls in `cva6_unpacked_bridge_controls.sh`
(both directions, six refused shapes, and the rows themselves).

Fixed on this route: `instr_queue`, `scoreboard` and `perf_counters` all died
with *use of undeclared identifier* because `gen_cva6_wrappers.py` mapped ports
through its `recon` rename on the scalar path but not on the packed-array path.
All three emit and run after the fix; `cva6_foundation_controls.sh` check 3
detects a planted regression.

### Canonical 30: fresh authoritative run — 30/30 accepted

`cva6_30_rows.tsv`, `cva6_30_provenance.txt`, `cva6_30_classification.txt`,
`cva6_30_cgroup_records.tsv`, `cva6_30_static_gates.tsv`,
`cva6_30_verification.txt`.  Run once, in a fresh output directory, from clean
committed HEAD `53136031b`, against a foundation rebuilt from scratch
immediately beforehand.  **No earlier run was reused, resumed or merged into
this one.**

    jobs=1   stack 8192 KB   EMIT_TIMEOUT=3600   RUN_TIMEOUT=1800
    EMIT_MAX_RSS_KB=8388608  (cgroup MemoryMax, emission child only)
    lake named explicitly    20 m 36 s wall

    30 rows, 30 unique, canonical order, 18 fields each, reconciled=YES

    RAW RUNTIME OBSERVATIONS   30 of 30   PARSED / compiles_ok=true / RAN
    ACCEPTED COVERAGE          30 of 30   ...and emitter rc explicitly 0, and
                                          both static gates clean

    NO_CERT 0   NO_VERDICT 0   resource-limit 0   timeout 0
    NO_ROW  0   PARSE_ERROR 0  REFUSED 0

30 of 30 emitted certificates retained with their SHA-256, all recomputed.

**Every block's evidence is authoritative, not merely absent.**  All 30 records
carry `emit_rc=0`, `counters_readable=1`, a `phase=exit` terminal snapshot,
`oom_kill=0`, `oom_group_kill=0` and `cleanup=clean`.  No scope was left behind
and the live-scope query succeeded, so "none left" rests on a successful query.
The largest emission peaked at 754,116 KB against the 8,388,608 KB ceiling, so
the ceiling never bound a legitimate block.

**The differential half ran for all 30.**  For every row promoted as accepted
translation validation, `cert_lgraph_diff.py` was given `--verilog` from
`lhd compile lg:<that row's retained lgdb> --recipe O0`, with
`--require-verilog`.  `cva6_30_static_gates.tsv` records, per module, the
`vc_gates` result, the invariant-only result, the full differential result, and
the SHA-256 of the Verilog, the DCERT1 and the LGraph Lean file.

    vc_gates            30 PASS
    cert_invariant      30 PASS
    cert_differential   30 PASS   (0 NOT_RUN)

Largest blocks:

| block | sources | nodes | flops | mem | run_ms |
|---|---|---|---|---|---|
| `cva6_hpdcache_subsystem` | 97,741 | 108,612 | 542 | **12** | 10,351 |
| `cva6_hpdcache_wrapper` | 96,438 | 107,147 | 515 | **12** | 11,090 |
| `csr_regfile` | 32,853 | 34,897 | 136 | 0 | 46,623 |
| `fpu_wrap` | 24,963 | 28,576 | 275 | 0 | 1,968 |
| `issue_stage` | 14,686 | 15,654 | 332 | 0 | 39,462 |

Provenance pins the run to one source state: `d4_head=53136031b`,
`d4_tracked_dirty=no`, `cva6_root_rev=6cb200105`, `cva6_tracked_clean=yes`,
foundation `fbf1b38b…`, filelist `9ae886ff…`, declared wrappers `ce48a103…`,
`lhd` `07ecf6f5…`, Lean 4.31.0.  Every one of those is RECOMPUTED by
`verify_d4_run.sh`, which passes: *30 rows, 30 accepted of 30 raw RAN*.

> SUPERSEDED, AND NOT MERGED.  An earlier canonical-30 run reached 29 raw
> observations and 28 accepted rows; two blocks were then repaired and each
> re-run alone.  Those focused rows were never added to that table to
> manufacture a 30.  This section reports ONE fresh sweep, and it replaces the
> earlier numbers rather than combining with them.

### The GetMask constant-mask defect, and the fail-closed repair

`Get_mask(a, -1)` is LiveHD's zext idiom, so the mask is deliberately
materialised at `max(src_w, out_w)`.  That widening was applied to CONCRETE
positive patterns too, and `max(src_w, out_w)` can be SMALLER than the value
needs.  `cva6_hpdcache_subsystem` shipped

    SourceDesc.const 2 8    -> Op_GetMask origin 516164    mk_bv 2 8   = 0
    SourceDesc.const 6 124  -> Op_GetMask origin 516384    mk_bv 6 124 = 60

so two masks selected the wrong source bits.  `cert_dep_id` had labelled them
with the caller's `expected_w` and never consulted the strict fit check that
`pin_width` already performs.

Repair: the mask pin's own `pin_width` joins the max, at BOTH the certificate
and fast-model sites, so widening can never truncate and the mask constant is
subject to the existing invariant instead of bypassing it.  Neither gate was
weakened and no constant was normalised or truncated.

Re-emitted with the fixed emitter (`cva6_hpdcache_subsystem_refixed_*`):

    emit_rc=0   97,741 / 108,612   542 flops   12 mem   PARSED / true / RAN
    const 2 8   -> const 4 8        const 6 124 -> const 7 124
    vc_gates.py PASS   cert_lgraph_diff FULL GATE PASS   0 ill-fitting constants
    verdict=OK  counters_readable=1  terminal_snapshot=1  cleanup=clean
    digest 7121494964126454710 -- UNCHANGED

The unchanged digest is worth stating plainly: under the sampled stimulus the
truncated masks happened to produce the same observable outputs.  The
certificate was still false about the design, which is exactly why the static
gates exist and why a digest is not a correctness argument.

`getmask_const_width_regress.sh` checks the invariant AT THE OPERAND -- for
every `Op_GetMask`, the mask constant's declared width must hold its value
(43,231 nodes checked, 0 violations) -- and plants the pre-fix shape by index to
prove both that check and `cert_lgraph_diff.py` still reject it.

### The differential half, run for real, and what it found

`cert_lgraph_diff.py` compares the certificate's constants against the
constants an INDEPENDENT extractor (`inou.cgen.verilog`) emits.  Every
invocation here had been `--quiet` with no `--verilog`, so only the
invariant half ran while the output was reported as a gate pass.  That claim
was unsupported.

Run properly, with Verilog generated from THE SAME retained LGraph:

    lhd compile lg:<block>/lgdb --recipe O0 --emit verilog:hpd.v
    cert_lgraph_diff.py <block>_Lgraph.lean --verilog hpd.v --require-verilog

it exits 1 on three wide constants:

    562949953421280     49 bits
    54043195528445952   56 bits
    71494644084506624   56 bits

DIAGNOSIS (measured, not assumed).  Each is consumed by `Op_GetMask` nodes
whose BOTH operands are constant -- eight such nodes in total.  cgen
constant-folds those nodes and emits only the folded RESULT, never the operand
literals, so the certificate legitimately holds values the Verilog never
contains.  The folded results are `0` and `1`, and both ARE in the Verilog.

REFINEMENT, not relaxation.  The differential exempts a constant only when
EVERY consumer of EVERY source slot holding it is a constant-folded
`Op_GetMask`, and only when the folded results it feeds are themselves present
in the Verilog.  Each exemption is printed, never silent.  The
positive-constant fit invariant (check 1) is untouched.

**Eligibility is MONOTONE and ORDER-INDEPENDENT**, which the first version was
not.  It aggregated by numeric value with `setdefault(v, None)`, which cannot
overwrite an existing entry: a qualifying slot visited FIRST left a result set
that a later disqualifying slot could not remove, so a value with one folded
use and one ORDINARY use was exempted anyway.  Disqualification is now
collected separately and applied last, so a later qualifying occurrence can
never revive a disqualified value.

**Direct descriptor uses count.**  A source read by an `OutputDesc.slot`, a
`FlopDesc` `din`/`enable`/`resetPin`, or a `MemoryDesc.nextImg` is used by the
design and is never folded away; any such use disqualifies the value.  A source
with no modelled use at all also fails closed, because nothing shows it was
folded away.

`differential_exemption_controls.sh` pins all of it on synthetic fixtures, 8/8:
qualifying-first-then-ordinary (the exact reported shape), disqualifying-first,
ordinary-consumer-only, direct-output use, folded-result-absent, the positive
case, `--require-verilog`, and the fit invariant.  Checked against a scratch
copy of the PRE-FIX logic: it exits 0 with `EXEMPT` + `PASS` on the
qualifying-first fixture, where the corrected gate exits 1.

`--require-verilog` makes omission of the cgen artifact a FAILURE, so an
invariant-only invocation can no longer be reported as the full gate.

Evidence: `cva6_hpdcache_subsystem_differential.txt`, which records the exact
command, the lgdb graph, and SHA-256 of the Verilog, the certificate and the
LGraph Lean file.

### The last canonical-30 blocker: reversed packed dimensions

`cva6_hpdcache_wrapper` was the one canonical-30 block that never emitted, and
this file called it "an RTL/elaboration problem, not a harness one".  That was
wrong.  The generator rebuilt a nested packed typedef with its dimensions in
the wrong order:

    typedef logic [63:0] [0:0] hpdcache_req_data_t__r;   WRONG
    typedef logic [0:0] [63:0] hpdcache_req_data_t__r;   correct

SystemVerilog reads packed dimensions **outer first**: `logic [0:0][63:0] x`
makes `x[0]` a 64-bit element, while `logic [63:0][0:0] x` makes it ONE BIT.
So `rdata[0]` was a single bit and slang CORRECTLY rejected `[63:32]` of it at
`cva6_hpdcache_if_adapter.sv:332`.  The diagnostic was right; the input was
ours.

`render_field_type` recursed into the element and appended its own range AFTER
the inner text, reversing the order for every nested packed array.  **The total
width was right either way**, which is why no width check ever saw it.
Dimensions are now collected outermost-first and emitted after the base element
type.

Focused result (`cva6_hpdcache_wrapper_focused*`), ordinary 8192 KB stack,
`EMIT_MAX_RSS_KB=8388608`, jobs=1:

    EMITTED  PARSED  compilesOk=true  RAN   18 fields   reconciled=YES
    96,438 sources / 107,147 nodes / 515 flops / 12 memories   run 10,043 ms
    digest c58c3dd5...-hashed certificate retained
    emit_rc=0  counters_readable=1  terminal_snapshot=1  oom_kill=0  cleanup=clean

    vc_gates.py                               PASS
    cert_lgraph_diff, INVARIANT half only     PASS
    cert_lgraph_diff, FULL DIFFERENTIAL       PASS  (--verilog bound to cgen
      output from the same retained LGraph, --require-verilog; the same three
      constant-folded-GetMask exemptions as the subsystem)

**This was ONE focused block, not a canonical-30 sweep**, and it was never
merged into the canonical table.  It has since been superseded by the fresh
30-row sweep documented above, in which this block appears as an ordinary
accepted row (96,438 / 107,147 / 515 / 12).  The focused evidence is kept
because it is where the defect was diagnosed and repaired.

`packed_dim_order_controls.sh` pins the renderer on six AST shapes (declaration
order AND total width), checks the published corpus reproduces byte-for-byte
from the AST (so nothing was hand-edited), and asserts the concrete
`hpdcache_req_data_t__r` shape.  Against the pre-fix corpus it fails, naming 31
reversed declarations.

### A nonzero emitter result is now a failure

The driver looked for a certificate BEFORE consulting the emitter's own result,
so a `.dcert` on disk overrode a failed emission.  It now fails closed on any
nonzero `emit_rc`, after the kernel verdict and before certificate discovery,
emitting `EMIT_FAILED / NOT_RUN`; a written certificate is kept as diagnostic
evidence and never executed or counted.  Timeouts stay distinct
(`emitter-failed: emission timeout Ns (rc=124)`).

**This immediately exposed a second, older problem.**  `null_vpu` -- used as the
positive control in two suites -- is DEGENERATE: its certificate has no nodes,
so the emitter's own gates reject it ("no nodes emitted") and it has ALWAYS
exited 3.  It was being published as EMITTED/RAN.  Both suites now use a
corpus-independent `selftest` emission instead.

> CONSEQUENCE FOR CORE-ET, SINCE MEASURED: the 85-RAN CORE-ET figure had been
> produced by the old fail-open driver, so it could include blocks whose emitter
> failed.  Two did -- `null_vpu` and `minion_dcache_texsend`.  That corpus was
> then re-run and re-gated under the fail-closed driver, and the fresh baseline
> in the next section supersedes it: 83 raw RAN of 122, not 85.

### Not claimed

The route is NOT complete.  CVA6's canonical 30 is now **30 of 30 accepted**,
from one fresh sweep with full differentials.  CORE-ET stands at **83 accepted
of 122**: the fresh fail-closed run recorded 83 raw runtime observations, one of
them gate-rejected, plus 28 front-end failures, 9 resource-limit blocks and 2
emission timeouts; the gate-rejected row was afterwards traced to a FALSE
REJECTION in the checker and accepted without re-running anything (see "CORE-ET
post-baseline" below).  The historical 85 is superseded -- it was 2 blocks
optimistic, both published while their emitter returned a nonzero status.

**ZERO canonical CVA6 blocks remain.**  CORE-ET is the only coverage gap left on
this route: 39 of its 122 blocks do not yet reach accepted coverage, every one
of them for want of a runtime observation rather than a failed gate.

## CORE-ET: fresh fail-closed 122 baseline — 82 accepted, 83 raw RAN

`coreet_122_fresh_rows.tsv`, `_provenance.txt`, `_classification.txt`,
`_cgroup_records.tsv`, `_gates.tsv`, `_reconciliation.txt`.  One run, fresh
output directory, from clean D4 `e4fb8c82d` and pinned clean CORE-ET
`c72b6a2fe`, `COREET_TECH=generic`, jobs=4, 8192 KB stack, 3600/1800 s
timeouts, `EMIT_MAX_RSS_KB=8388608`.  1 h 19 m wall.

    122 rows, 122 unique, exact declared order, 18 fields each, reconciled=YES

    ACCEPTED (RAN + emit_rc 0 + vc_gates + invariant + full differential)   82
    front-end failure (emitter rc != 0)                                     28
    resource-limit (positive kernel kill at the ceiling)                     9
    emission timeout (3600 s)                                                2
    RAN but GATE-REJECTED                                                    1

    RAW RUNTIME OBSERVATIONS  83 of 122
    ACCEPTED COVERAGE         82 of 122

`reconciled=YES` is schema completeness, not coverage.

**Evidence, recomputed.**  122/122 per-block cgroup records, every one with an
explicit integer `emit_rc` and `cleanup=clean`; 113 `OK` verdicts all carrying a
`phase=exit` terminal snapshot, and 9 `LIMIT_HIT` resting on positive kernel
counters.  All 83 retained certificate SHA-256s recomputed and matching.  Gates
were run per artifact with cgen Verilog generated from that block's own retained
`lgdb_norm` and `--require-verilog`, so the DIFFERENTIAL half ran — 82 PASS,
1 FAIL, 0 invariant-only substitutions.  `scripts/verify_d4_run.sh` was NOT
used: its expectations are hard-coded for CVA6.  `_reconciliation.txt` states
every check that was recomputed.

### Divergence from the historical 85 / 37, block by block

Fourteen blocks changed classification, and every change is explained:

| change | blocks | why |
|---|---|---|
| EMITTED → RAN | `minion_dcache_tensor_load`, `vpu_ml`, `vpu_tensorquant` | the three `Nat.pow` panics, fixed by `bv_shl_bounded` |
| RAN → front-end | `minion_dcache_texsend`, `null_vpu` | **both were published as RAN while their emitter returned 3.**  `texsend` fails its static gates; `null_vpu` is degenerate ("no nodes emitted").  The fail-closed `emit_rc` check unmasked them. |
| timeout → resource-limit | the nine `txfma*` blocks | previously ran unbounded until the old timeout; they now hit the 8 GiB ceiling first.  Same blocks fail, with an accurate reason: memory-hungry, not merely slow. |

So the historical 85 was 2 blocks optimistic, and the historical "37 front-end"
split into 28 front-end + 9 resource-limit + 2 timeout.

### The one gate-rejected row

`vpu_tensorquant` RAN, but its retained artifact fails the full differential:

    FAIL: cert holds constant 256 (9 bits) that cgen_verilog never emits

Diagnosed, not excused: both slots holding 256 are the **amount operand of
`Op_Sext`** (dep index 1) on 256-bit sign-extends, which `pass_lean.cpp`
deliberately widens to encode the sign position.  cgen expresses sign extension
structurally, so it has no reason to emit that literal.  That makes it a
*candidate* for the same class as the constant-folded-`Op_GetMask` exemption,
but extending the exemption is a REPAIR, and this unit is a baseline.  The row
is therefore **not accepted coverage** and is left for audit.  (It was repaired
in the next unit -- see "CORE-ET post-baseline" below.  The numbers in this
section are left as they were measured.)

### Not addressed here

The 28 front-end failures (24 of them `pass.single_edge` refusing multi-clock
blocks, rc=7), the 9 resource-limit blocks, the 2 timeouts, and the one
gate-rejected row.  None was repaired in this unit.

## CORE-ET post-baseline: the structural `Op_Sext` amount — 83 of 122

The one gate-rejected row above is now ACCEPTED, by repairing the CHECKER rather
than by re-running anything.  The baseline tables are left exactly as they were
measured; this section is the delta.

**The defect was in the gate, not the certificate.**  `Op_Sext`'s operand 1 is
not data -- it carries the SIGN POSITION, and `pass_lean.cpp` deliberately
widens that constant so it can hold its value instead of truncating it to the
pin width.  `inou.cgen.verilog` renders sign extension STRUCTURALLY (replication
of the sign bit, a bit-select), so the number has no reason ever to appear as a
sized Verilog literal.  The differential half demanded it anyway, which is a
false rejection of a correct certificate.  In `vpu_tensorquant` both slots
holding 256 are consumed by exactly one arity-2 `Op_Sext` of width 256 whose
dependency 0 is a 256-bit `flopQ`, i.e. `256 = min(256, 256)`:

    Op_Sext origin=3604 out_w=256 dep0=18 (flopQ 18 256) amount_slot=665 (const 9 256)
    Op_Sext origin=3600 out_w=256 dep0=17 (flopQ 17 256) amount_slot=666 (const 9 256)

**The exemption is narrow and FAIL-CLOSED.**  It is not an exemption for 256,
nor for constants that happen to be absent.  A value qualifies only when EVERY
source slot carrying it:

  * is not read directly by an `OutputDesc.slot`, a `FlopDesc`
    `din`/`enable`/`resetPin`, or a `MemoryDesc.nextImg`;
  * has at least one consumer; and
  * has EVERY consumer an `Op_Sext` of arity exactly 2 reading the slot at
    dependency index 1, with the value equal to `min(out_w, dep0_w)`.

Disqualification is collected separately and applied last, so ordering cannot
revive a disqualified value -- the same monotonicity the GetMask exemption had
to be repaired to get.  Slot-width derivation is TOTAL over every `SourceDesc`
constructor (`input`, `const`, `flopQ`, `flopQAsync`, `memImg`, `memConst`) and
yields "unknown" for anything unrecognised, which DISQUALIFIES rather than
exempts.  A wrong amount is a real disagreement and still fails.  Every
exemption is printed, never silent.

#### Dependency shape is checked against the dense index

The first version of the exemption trusted `int()` on a dependency token, and
review found the hole: **`int("-1")` is a valid Python index**, so
`deps := #[-1, 0]` reached the LAST source and could supply a width that
satisfied `min(out_w, dep0_w)` by accident.  The checker printed the exemption
and exited 0 on a malformed certificate.  Width lookup likewise accepted a self
or forward node reference whose recorded width happened to fit.

`parse_cert_graph` now owns both preconditions, and either one failing means NO
value in that certificate is exempt:

  * **every dependency of node `j` is a canonical nonnegative natural strictly
    below `sources.size + j`.**  Negative, non-canonical, out-of-range, self and
    forward references are rejected instead of indexed.
  * **the node array parses IN FULL.**  The old `(\S+?)` op pattern could not
    match a parameterised op, so `Op_Sum 1`, `Op_Sum 2` and `Op_MemWriteBE 1`
    were silently dropped -- 30 of 5118 nodes in `intpipe_alu` alone.  A dropped
    node is invisible in the consumer map, which turns a DISQUALIFYING use into
    no use at all, and it shifts every later dense index.  This affected the
    GetMask exemption too, so both now share the one fail-closed parse.

Measured, not assumed: across the 85 retained certificates the parse covers
every node, and all 361,116 dependency tokens are canonical and strictly
earlier.  Enforcement costs the real corpus nothing.

`sext_amount_exemption_controls.sh` pins it on synthetic fixtures, 19/19:
full-extension and truncating positives (the amount absent from the Verilog), a
wrong amount, use at dependency 0, mixed Sext/non-Sext use, direct output, flop
`din` and memory `nextImg` uses, no consumer at all, arity 3, duplicate-value
ordering, the fit invariant, the exact negative-index bypass, self, forward and
out-of-range dependencies, and a POSITIVE whose dependency 0 is a valid earlier
node.  Two checks keep it from being vacuous: a copy with only the dense-index
guards neutered still EXEMPTS the negative-index fixture, and the CONCRETE
`vpu_tensorquant` artifact run through a copy with the exemption neutered still
fails on constant 256.  The GetMask controls were not weakened and still pass
8/8 and 4/4.

**Replayed, not re-run.**  `coreet_sext_replay.sh` writes
`coreet_122_sext_replay.tsv`.  No emitter, no `lhd`, no cgen: the same retained
certificates, Lean files and cgen Verilog, with their SHA-256s recomputed and
compared against the baseline table before any verdict is taken.

The driver is fail-closed ON ITS OWN ADVERTISED RESULT, which review also found
it was not: it checked only "no regression, bytes unchanged, nothing missing,
n=83", so a checker that repaired NOTHING would have exited 0 while printing
"REPLAY OK: 82 of 83".  It now requires all 83 rows to pass all three gates AND
the verdict delta to be EXACTLY one row -- `vpu_tensorquant`,
`cert_differential`, `FAIL -> PASS` -- naming the column and the direction, so a
change in another module, another column, an extra change, zero changes, a
moved hash or a short baseline each exit nonzero.

**It authenticates MODULE IDENTITY before it replays anything**, which review
found was the remaining hole: the driver counted rows, and `n == 83` is not 83
distinct modules.  A baseline that replaces the `vpu_trans` row with a second
GENUINE `intpipe_alu` row keeps 83 rows, genuine hashes and the intended
tensorquant delta -- and the previous driver reported `REPLAY OK: 83 of 83` for
it while silently replaying `intpipe_alu` twice and `vpu_trans` never.  The
expected keys now live in `coreet_83_modules.txt`, derived once from the
committed baseline and deliberately NOT overridable from the environment: the
verdict baseline is overridable so the controls can drive the driver, and if a
doctored baseline could also restate WHICH modules were replayed, authenticating
it would be circular.  The consumed baseline must match that manifest as an
EXACT SEQUENCE -- duplicate, omitted, unknown or reordered keys abort with
nothing replayed and no table written -- and the produced table is re-checked
for 83 distinct keys.

`replay_driver_controls.sh` drives the real driver over the real retained bytes
and pins all of that, 12/12, including the pre-repair checker producing 82 of 83
and zero changes, and the exact duplicate/omission substitution above.

    replayed                 83 of 83 retained artifacts
    all three gates PASS     83
    retained bytes unchanged 83 of 83   (moved: 0, missing: 0)
    verdict changes          1   (regressions: 0)

    vpu_tensorquant   cert_differential   FAIL -> PASS

    RAW RUNTIME OBSERVATIONS  83 of 122   (unchanged)
    ACCEPTED COVERAGE         83 of 122   (was 82)

**TRUST BOUNDARY.**  Runtime rows, cgroup records, artifact hashes, gates, and
runtime digests are empirical translation validation, not a generic formal proof
of the C++ LGraph-to-DCERT1 transcription.  This unit moves one block from
gate-rejected to accepted by correcting a checker that was wrong about how cgen
renders sign extension; it establishes nothing new about the emitter.

**Still not addressed:** the 28 front-end failures (24 of them `pass.single_edge`
refusing multi-clock blocks, rc=7), the 9 resource-limit blocks and the 2
emission timeouts -- 39 of 122 blocks, none repaired here.

## CORE-ET front end: Memory commit slots at P=2

The baseline's 24 rc=7 front-end refusals split EXACTLY two ways in the retained
`single_edge.log`s: **17 `memory-unsupported`** and **7 `multi-clock-no-ratio`**.
This unit works only on the memory family. The seven unknown-ratio refusals are
CORRECT -- a second clock domain with no known integer ratio cannot be slotted
without guessing -- and the probe below requires all seven to still refuse for
the same reason.

### What was wrong

`pass.single_edge` refused any design holding a `Memory` the moment it needed a
phase divider: "memory ... would commit on every sub-step under a phase
divider", hint "slot enables are not wired into the Memory cell yet". The
refusal was honest. Under a divider one source period is P normalized steps and
an untouched Memory writes on EVERY one of them, so a single source write
becomes P writes.

### What is lowered, and what still declines

Each WRITE port's `enable` is now gated with the memory's OWN slot predicate, so
the image updates exactly once per source period. Two properties carry the
whole repair, and each has a negative control:

  * **the lane mask.** A write enable is `wensize` bits wide -- 32, 35 and 69 on
    the measured CORE-ET blocks. The gate is a MUX between the full mask and
    zero, never `mask & predicate`: a 1-bit AND zero-extends and keeps lane 0
    alone, silently dropping every other byte lane. That would be a worse
    corruption than the over-commit it replaces.
  * **the slot.** It is derived from the memory's own (net, edge) commit class
    exactly like a flop's, NOT assumed to be slot 0, and every write port's
    clock is resolved and required to be the reference clock. The cell-global
    polarity and EACH PORT'S OWN cone parity are combined PER PORT; ports that
    end up on opposite edges cannot share one slot and the cell is refused.

After the slot is fixed, every write port's `clock_pin` is REBOUND to the
resolved reference clock, exactly as the flops are. The slot predicate carries
all of the timing, so a derived cone left on the pin is not redundant but
actively harmful: after normalization one clock tick is one SUB-step. This is
not cosmetic -- the yosys reader routinely hands a memory a width-masked clock
(`get_mask(clk)`), and on `vpu_tensortmp_rf` the normalized netlist went from
`.wr_clock_0(get_mask_168)` to `.wr_clock_0(clk_i)`.

READ ports are left completely alone. At `type` 0 a read is `assign rd_dout =
d_fwd` -- combinational -- and `rd_enable` qualifies that TERM (`rd_enable ?
data[addr] : x`), not a register; gating it would fabricate an `x` at the
non-commit sub-step. `fwd`/`undef` are never touched, so collision and
forwarding behaviour is carried through unchanged.

Everything else declines BY NAME, before any mutation:

| code | shape |
|---|---|
| `memory-sync-read-unsupported` | `type` 1: the dout register is `always @(posedge clk) rd_dout <= d_fwd` and has NO enable pin anywhere in the cell model. Ungated it captures on both sub-steps (at the boundary it then shows this period's writes, a full period early); gating `rd_enable` instead captures `x`. Neither is the source's timing. |
| `memory-array-type-unsupported` | `type` 2, the inline reg-array form. |
| `memory-whole-array-unsupported` | the `update` bus is driven. cgen tests `update` BEFORE `type`, so such a cell is a bulk-updated reg array whose `update_enable`/`reset` the per-port gate cannot reach. |
| `memory-off-reference-clock` | a write port commits on a net that is not the reference clock. |
| `memory-gated-clock` | the memory clock is a gate cone; its enables have no representation in a slot gate. |
| `memory-mixed-edge` | the `Memory_posclk_mixed` sentinel: the ports do not all commit on the same edge, so no single slot represents the cell. |
| `memory-mixed-port-edge` | the sentinel is absent but the ports still disagree once each one's OWN clock-cone parity is applied. See the correction below. |
| `memory-unresolved-{clock,edge,type,wensize,port-role}`, `memory-write-no-enable` | the shape cannot be read out of the cell at all. |

All of these fire ONLY at P>1. At P=1 there is no slot to commit at and a memory
is not this pass's business, exactly as before -- pinned by a control.

### Measured: 10 of the 17 clear, 7 refuse for a named reason

`coreet_frontend_refusal_probe.sh` → `coreet_frontend_refusals.tsv`. It re-runs
ONLY the front-end pass on each block's retained `lgdb_raw`: no emitter, no
certificate, no Lean, no corpus re-run.

    blocks probed              118
    baseline REFUSALS (rc=7)    24
      now NORMALIZE (rc 0)      10
      refuse, SAME code          7   (all seven multi-clock-no-ratio)
      refuse, NEW named code     7
    baseline non-refusals that now REFUSE   0

**Cleared (10):** `minion_dcache_buffer_array`, `minion_dcache_metadata_array`,
`minion_dcache_replay_queue`, `minion_dcache_tlb_array`, `minion_tlb`,
`vpu_lane_tima`, `vpu_tensora_rf`, `vpu_tensorb_rf`, `vpu_tensorc_rf`,
`vpu_tensortmp_rf`.

**Still refusing (7), each by name:** `minion_dcache_128x64_1r1w_lram`,
`minion_dcache_128x72_1r1w_lram`, `minion_dcache_data_array`
(`memory-sync-read-unsupported`); `minion_frontend`,
`minion_frontend_thread_buffer` (`memory-array-type-unsupported`);
`minion_dcache_top` (`memory-gated-clock`); `vpu_rf`
(`memory-off-reference-clock` -- its `gen_latch.u_rf.rf_q` is a latch-based
register file whose write clock is a derived net).

The zero in the last row is as load-bearing as the ten: no block that used to
pass the front end now refuses.

### Controls

`lhd/tests/single_edge_memory_slot_test.sh`, 13/13. Four designs read through
the yosys front end; the semantic legs run under ICARUS VERILOG, with the
SOURCE side being the ORIGINAL RTL, not LiveHD's own emission of the
unnormalized graph -- `inou.cgen.verilog` cannot express a negedge memory write
at all (the shipped wrapper only has `always @(posedge wr_clock_0)`), so for the
negedge design its own round trip is not a faithful reference. Measured, not
assumed.

  * a 16-lane byte-enabled memory written from slot-0 flops, read
    combinationally and through both a posedge and a negedge flop: source vs
    normalized agree over 39 periods at period boundaries;
  * a NEGEDGE-written memory fed by POSEDGE-committed inputs, so its slot is
    observable: it is gated on the phase-1 predicate and agrees over 39 periods;
  * refusal legs for the synchronous-read and off-reference-clock shapes, and a
    P=1 leg proving the same synchronous-read memory is untouched without a
    divider.

Four negative controls, each required to FAIL: the normalized side clocked in a
P=1 time base; the write enable UNGATED; the 16-lane mask COLLAPSED to one lane
(the `mask & predicate` bug); and the negedge memory moved to slot 0. A vacuity
guard runs first -- the UNnormalized design must already round-trip faithfully
against its own RTL, or a green normalized leg would mean nothing.

The pre-existing suite is unchanged and green: `single_edge_four_classes`,
`single_edge_icg`, `single_edge_hier_latch`, `single_edge_scope`,
`mem_ordering`, and the five latch tests -- 11 of 11.

### Focused pilot, not coverage

`coreet_memory_pilot_gates.sh` → `coreet_memory_pilot_gates.tsv`, over a FRESH
5-block run (`EMIT_MAX_RSS_KB=8388608`, jobs=1, the pinned generic-tech
foundation) covering three materially different memories: a small RF
(`vpu_tensortmp_rf`), lane-masked arrays (`minion_dcache_metadata_array`, 4
memories at `wensize` 35; `minion_tlb` at 69) and a FIFO
(`minion_dcache_replay_queue`).

    4 of 5 blocks EMITTED and RAN, emitter rc 0, 18-field PARSED / true / RAN rows
    terminal phase=exit cgroup records, oom_kill=0 and oom_group_kill=0 under memory_max=8 GiB
    vc_gates PASS, invariant PASS, and the REAL differential (cgen Verilog
      regenerated from each block's own lgdb_norm, --require-verilog) PASS: 4 of 4

The fifth, `vpu_rf`, refuses with `memory-off-reference-clock` -- correct, and
counted as a refusal above.

**A FOCUSED PILOT IS NOT 122-BLOCK COVERAGE.** The committed 122 baseline and
the replay tables are untouched by this unit, and the accepted-coverage figure
stays **83 of 122**. What the ten cleared blocks buy is a front end that no
longer refuses them; whether they emit, run and gate across the whole corpus is
a measurement that has not been taken here.

### Correction: the per-port edge was being OR-ed into one flag

Review found the first version of this lowering accumulated ONE `inverted`
boolean with `||` across every write-port clock cone and stored ONE
`Mem_elem::slot` for the whole cell. A legal graph with cell-global `posclk` =
1, two write ports on the SAME resolved reference clock, port 0 driven by `clk`
and port 1 through a `Not`, therefore normalized with
`applied=1 error=0 slots=2 mems_slotted=1` -- both write masks gated on the
FALLING slot, silently mistiming the direct-clock port. Neither the
off-reference guard (same net) nor the mixed-edge sentinel (absent) fired.

Reproduced independently before any change, then fixed: the parity is now
combined with the global polarity PER PORT, and a disagreement refuses the whole
graph with `memory-mixed-port-edge` before anything is mutated.

**The control is a direct LGraph build, not a shell fixture.** No `.v` source
reaches this shape -- the yosys reader canonicalizes a simple clock inversion
into the cell-global `Memory_posclk_mixed` sentinel, which was already refused.
`pass/single_edge/pass_single_edge_test.cpp` constructs the 2-write/1-read
async memory by hand, 4/4: the reported shape refuses; the MIRROR shape (port 0
inverted, port 1 direct) also refuses, which an order-dependent "fix" that
looked only at the first port would not do; and both agreeing cases -- both
ports direct, and both ports inverted -- still lower with `mems_slotted=1`, so
the refusal is not a blanket "two write ports are too hard". Against the
pre-fix pass the two refusal cases FAIL with `mems_slotted=1`.

**The semantic test now fails closed on its prerequisites.** Missing `iverilog`,
`vvp` or `ware/rtl` used to print `PASS` after skipping the Icarus legs -- which
are the test; everything above them compares the pass against itself. Verified
by running under a PATH that shadows `iverilog`: the script exits 1 naming the
missing prerequisite.

**Re-measured after the correction.** The 118-block front-end probe is
BYTE-IDENTICAL on every data row (10 cleared, 7 renamed, 7 unchanged, 0
regressions), and the pilot keeps the same five identities and statuses with 4
of 4 passing all three gates. The four pilot CERTIFICATE HASHES moved, and that
is the clock rebind, not noise: two runs of the same binary reproduce them
exactly, so the change is attributable to the code. Pre-fix those four carried a
derived clock cone into the normalized design.

**TRUST BOUNDARY.** The iverilog differential, the retained hashes, the gate
results, the cgroup records and the runtime rows are empirical translation
validation, not a generic formal proof of the C++ LGraph-to-DCERT1
transcription -- and the memory lowering itself is validated against an
independent simulator over 39 periods, not proved.


## CORE-ET prelaunch: binding the external corpus

CORE-ET is the only coverage gap left, and its historical 85/122 predates the
fail-closed emitter check.  Before that corpus is re-measured, the run has to be
bound to the checkout it is actually measuring.

`scripts/coreet_preflight.sh` runs before any CORE-ET block and its output IS
the corpus provenance (`coreet_prelaunch_provenance.txt`):

    coreet_rev                      c72b6a2fe4aaad5a9eef437776864ff71f4b55d7
    coreet_root_canonical           resolved once, then used
    coreet_tech                     generic
    coreet_relevant_paths           hw dv mk
    coreet_relevant_inputs_clean    yes
    coreet_outside_scope_tracked    0
    coreet_outside_scope_untracked  1
    coreet_patch_script_sha256      2cce0480...
    coreet_derived_map_sha256       e94a6d33...
    coreet_derived_aggregate_sha256 ee945429...   (deterministic, over the four)
    coreet_derived_mode             regenerated in place, then verified against
                                    fresh staging

Three gaps closed here, each found by review:

**`mk/` was outside the cleanliness scope, and the report mislabelled state.**
Every `dv/rtlcosim/*/Makefile` includes `mk/rtlcosim.mk`, which includes
`mk/prim.mk`, to derive `NEW_RTL_FINAL`.  A tracked edit under `mk` therefore
passed -- and was then counted under a field whose NAME said "untracked".  The
checked set is now `hw dv mk`, for tracked modifications AND untracked files,
and out-of-scope state is reported with tracked and untracked counted
SEPARATELY.

**Nothing proved the derived files came from the recorded patcher.**  The gate
checked only the 16-hex source-hash header the generator embeds, so a derived
BODY could be edited with that header left intact and the gate still passed.
Every canonical sweep now regenerates the four files INTO THE EXACT DIRECTORY
the filelist consumes (`--publish`), then derives a second time into fresh
project-local staging and compares BYTE FOR BYTE.  The header check remains as
a second line of defence; it is no longer what catches a tampered body.

**`COREET_TECH` was an ambient, unrecorded corpus input.**  It selects which
tech cells the filelist picks -- `generic` takes
`tech_generic/prim_clk_gate{,_n}`, `xilinx` takes the `tech_xilinx` files -- and
both succeed.  The canonical flow is pinned to `generic`, passed EXPLICITLY to
the emitter rather than inherited, and recorded.  An ambient non-generic value
is REFUSED, not silently overridden.

**The cleanliness claim is deliberately narrow, and says so.**  The checkout
carries an untracked `core_et_blocks.txt` at its root: unrelated retained
evidence.  It is counted, reported, never deleted, and the checkout as a whole
is **not** claimed porcelain-clean.

The four derived sources are verified, not assumed: exactly those four plus
`map.tsv` and nothing else; a map of four unique two-field pairs with sources
inside the resolved checkout and outputs inside the directory the filelist
consumes; every file readable; and each file byte-identical to a fresh
derivation.

**And the map itself is authenticated against the generator's map**, not merely
against structural predicates.  Structure alone is not enough: a clean,
committed, BYTE-IDENTICAL copy of a mapped source at another path inside the
checkout satisfies every structural check -- four unique two-field rows,
readable, contained, matching source-hash header -- while touching no generated
body.  That is not the generator's association, and it matters concretely,
because `coreet_filelist.sh::subst_of` looks up the EXACT original filelist
path: a forged key silently skips the substitution and the block compiles the
unpatched source.  The consumed map is now compared with the freshly staged
map, normalising only the output-directory prefix, so the exact source paths and
their source-to-output associations must match.  The structural checks run
first, so a malformed or duplicated row still gets its own specific diagnosis.

`coreet_preflight_controls.sh`, 21/21, all against COPIES under this worktree --
the real checkout is never written to:

    consistent mirror                     accepted, all four hashes recorded
    missing / extra / stale derived file  refused
    derived BODY mutated, header intact   refused
    clean byte-identical source alias     refused as not the generator's map
    malformed map line                    refused
    duplicate map entry                   refused
    map source outside the checkout       refused
    tracked or untracked under hw/        refused
    tracked or untracked under mk/        refused
    out-of-scope tracked AND untracked    counted separately, 1 and 1
    untracked file OUTSIDE the scope      accepted, counted, file untouched
    dir inside another repo               refused
    --publish into the consumed dir       derives there, then verify passes
    ambient COREET_TECH=xilinx            refused
    explicit COREET_TECH=generic          accepted and recorded
    the sweep, unpinnable root            FATAL before any block, no rows

Wiring confirmed on one block (`debug_breakpoint`, 165 nodes, 18 fields,
PARSED / true / RAN, `emit_rc=0`, terminal counters readable and zero, clean
cleanup), and the sweep refuses outright under `COREET_TECH=xilinx`.

The substitutions and the pinned tech are confirmed in the EMITTED filelist, not
merely in provenance.  For `intpipe_mul_div_top`, which consumes the patched
`intpipe_mul_div_ctl.sv`:

    derived substitution used              patched_src/intpipe_mul_div_ctl.sv
    references to the unpatched original   0
    tech_generic refs 9                    tech_xilinx refs 0

That block then fails emission at `pass.single_edge` -- "2 clock nets and no
known integer ratio between them" -- and is reported honestly as `EMIT_FAILED`
rc=7.  It is one of the 37 historical front-end blockers and is NOT addressed
here.

**All of the above are wiring smokes, not coverage**; the 122-block sweep has
not been run.

## Digest caution

A matching digest is not certificate equality.  Emitting `alu` from the stale
wrapper corpus gave the SAME digest as the 6,597-node certificate while being a
436-node design: both compute the same outputs under the same stimulus.  (That
corpus is no longer used -- see `CVA6_MODULE_SET_PROVENANCE.md` -- but the point
about digests stands.)
The digest witnesses agreement on the observable cone, which is what
`DIRECTION4_INCREMENTAL.md` Part II already says about dead nodes.

## CORE-ET: authoritative 122 run from 49cf29864 — 93 accepted

A NEW RESULT, not a rewrite. The 82/122 baseline tables
(`coreet_122_fresh_*`) and the `coreet_122_sext_replay` tables are left exactly
as the earlier binary produced them; this run is recorded alongside them as
`coreet_122_r2_*`. A later binary does not get to relabel what an earlier one
did.

One fresh run, fresh output directory, from committed `49cf29864` with no
tracked modifications. Pinned CORE-ET `c72b6a2fe`, `COREET_TECH=generic`,
authenticated generated-source map, the canonical 122-module manifest
(`coreet_122.list`, sha256 `021bc79d...`) in its declared order, ordinary 8192
KB stack, 3600 s emission / 1800 s runtime timeouts, the kernel-enforced 8 GiB
per-emission cgroup ceiling, jobs=4. 03:41:51 to 05:01:33 — 1 h 19 m 42 s,
the same wall time as the baseline run.

    122 rows, 122 unique, EXACT declared order, 18 fields each, reconciled=YES

    ACCEPTED (row + certificate hash + all three gates)   93
    front-end failure (emitter rc != 0)                   18
    resource-limit (positive kernel kill at the ceiling)   9
    emission timeout (3600 s)                              2

    ACCEPTED COVERAGE   93 of 122      (the baseline measured 82)

`reconciled=YES` is schema completeness, not coverage.

### The evidence each number rests on

**Provenance binds the run before the first block.** 19 required bindings are
present and the preflight dies on any of them: `d4_head=49cf29864...`,
`d4_tracked_dirty=no`, `lhd_sha256=a608cbd5...`, the Lake/Lean version line,
the module manifest and its sha256, `coreet_rev=c72b6a2fe...`,
`coreet_tech=generic`, `coreet_relevant_inputs_clean=yes`, the patch script's
sha256, the derived map's sha256, the derived-bodies aggregate sha256, and
every configured resource limit.

**122 complete cgroup/emitter records** (`coreet_122_r2_cgroup_records.tsv`),
one per block, checked by `coreet_run_cgroup_records.sh`:

    blocks with NO record                                    0
    OK verdicts lacking readable TERMINAL counters           0
    LIMIT_HIT verdicts without a positive kernel counter     0
    non-integer emit_rc                                      0
    non-clean scope teardown                                 0

`memory.events` `oom_kill`/`oom_group_kill` remain the ONLY authoritative
limit-hit signal; systemd's `Result` and the child's rc are diagnostics and
decide nothing. All nine resource-limit blocks rest on a positive counter.

**93 of 93 candidate RAN rows accepted** (`coreet_122_r2_gates.tsv`), by
`coreet_run_gates.sh`: each row's own `cert_sha256` recomputed from the retained
certificate and required to match, then `vc_gates.py`, the invariant half, and
the REAL `--verilog --require-verilog` differential with cgen Verilog
regenerated from that block's own `lgdb_norm`. 0 gate failures, 0 hash
mismatches. Coverage is the INTERSECTION: emission alone is not coverage, and a
runtime row alone is not either.

### Every transition, named

`coreet_122_r2_reconciliation.txt`. Eleven blocks moved; all eleven are gains
and **nothing regressed**.

| block | baseline | now |
|---|---|---|
| `minion_dcache_buffer_array`, `minion_dcache_metadata_array`, `minion_dcache_replay_queue`, `minion_dcache_tlb_array`, `minion_tlb`, `vpu_lane_tima`, `vpu_tensora_rf`, `vpu_tensorb_rf`, `vpu_tensorc_rf`, `vpu_tensortmp_rf` | front-end-failure | **ACCEPTED** |
| `vpu_tensorquant` | gate-rejected | **ACCEPTED** |

All TEN of the formerly front-end-blocked memory designs are now accepted end
to end -- not merely past the front end, but emitted, run, hash-matched and
through all three gates. `vpu_tensorquant` is the structural `Op_Sext` amount
repair finally showing up in a corpus run rather than a replay.

### The remaining 29

    multi-clock-no-ratio                    7   second clock domain, no known ratio
    memory-sync-read-unsupported            3   minion_dcache_data_array, 128x64/128x72 lram
    memory-array-type-unsupported           2   minion_frontend, minion_frontend_thread_buffer
    memory-off-reference-clock              1   vpu_rf (latch-based RF on a derived write clock)
    memory-gated-clock                      1   minion_dcache_top
    other front-end (no single_edge refusal) 4  null_vpu (degenerate), minion_dcache_texsend
                                                (static gates), txfma_adder, txfma_top_fake (slang rc=1)
    resource-limit at the 8 GiB ceiling     9   the txfma family
    emission timeout 3600 s                 2   minion_top, vpu_top

None was repaired in this unit.

**TRUST BOUNDARY.** Corpus authentication, hashes, cgroup counters, runtime
rows and the static/differential gates are empirical translation validation,
not a generic formal proof of the C++ LGraph-to-DCERT1 transcription. CVA6
remains at its accepted 30/30 and was neither rerun nor modified here.

### Correction: the three result drivers could not fail

Review found all three reusable drivers exiting 0 on inputs they were meant to
reject. Each one's EXIT STATUS is its product -- a caller that checks it would
have read a broken corpus as a clean run -- so each defect is a fail-open, not
a cosmetic one. All four were reproduced against the exact committed scripts
before anything changed, and this is a DRIVER AND CONTROL correction: no result
table (`coreet_122_fresh_*`, `coreet_122_sext_replay*`, `coreet_122_r2_*`) was
rewritten and no corpus was re-run.

| defect | was | now |
|---|---|---|
| `coreet_run_gates.sh` reported gate failures and exited **0** | only a hash mismatch could fail the run | nonzero on any cgen/artifact failure, any vc/invariant/differential failure, any hash mismatch, `accepted != candidates`, **or an empty candidate set** -- "0 of 0 accepted" is an empty measurement, never a pass. The per-block table is written BEFORE the verdict, so a failing run keeps its evidence |
| `coreet_run_cgroup_records.sh` accepted `verdict=UNKNOWN` and exited **0** | only `OK` and `LIMIT_HIT` were examined; everything else fell through unchecked | only those two states are supported and anything else is rejected by name. `OK` now also requires ZERO kill counters (an OK carrying a kill is a contradiction); `LIMIT_HIT` requires a positive kernel counter AND a runtime row that is non-running and classified `resource-limit`. Records are bound to UNIQUE runtime-row identities, not a count |
| `coreet_run_reconcile.py` reported a 19-field row as "18 on every row" | `dict(zip(header, fields))` silently discards extras, and the shape check ran on the dict | every line is RAW-validated against the header and the declared contract before any dict exists |
| a deleted gate row became the block-level outcome `ran-ungated` | a missing gate quietly looked like an honest non-acceptance | gate tables are checked one-to-one against the runtime rows -- missing, duplicate, unknown or reordered is INFRASTRUCTURE INCOMPLETE: `reconciled=NO`, no coverage figure is printed at all, exit nonzero. The same raw validation and identity checks run on the BASELINE inputs, which carry the transition claims |

`run_driver_controls.sh` invokes the real entrypoints with real arguments,
18/18, and every leg asserts an exact exit status:

    REJECTED, exit 1   gate FAIL on a candidate; an empty candidate set;
                       verdict=UNKNOWN; an OK carrying a kill counter;
                       a LIMIT_HIT whose row still RAN; a LIMIT_HIT with zero
                       counters; a 19-field runtime row; a missing gate row;
                       a duplicate/omitted gate substitution; a reordered gate
                       table; an unknown gate module
    ACCEPTED, exit 0   a well-formed OK record; a well-formed LIMIT_HIT record;
                       the committed R2 tables

It also asserts the missing gate row does NOT degrade to a block-level outcome,
and that the genuine result is unmoved: `ACCEPTED COVERAGE 93 of 122`, `blocks
that moved 11`, `newly ACCEPTED 11`, `no longer ACCEPTED 0`, `of the ten,
ACCEPTED 10`.

**The hardened drivers were re-run against the real R2 run**, writing to scratch
so no committed table was touched: `coreet_run_cgroup_records.sh` exit 0 and
`coreet_run_gates.sh` exit 0 (`93 of 93 candidate rows accepted`), both
producing data rows BYTE-IDENTICAL to the committed tables.

### Second correction: evidence that was tested but not authenticated

Three further fail-open paths, all reproduced against the exact committed
scripts before anything changed. Still a DRIVER AND CONTROL correction: every
`coreet_122_fresh_*`, `coreet_122_sext_replay*` and `coreet_122_r2_*` table is
byte-unchanged and no corpus was re-run.

**A counter must be AUTHENTICATED, not merely tested for positivity.** This
record was accepted, exit 0:

    verdict=OK  counters_readable=1  terminal_snapshot=1
    oom_kill=not-an-integer  oom_group_kill=0

`oom_kill=not-an-integer` is not "not positive", it is NO EVIDENCE -- a bare
positivity test reads it as zero. Both kill fields must now be canonical
nonnegative decimals (no sign, no leading zeros, no whitespace, not empty)
whenever counter evidence is used: `OK` requires both well-formed AND exactly
zero, `LIMIT_HIT` requires both well-formed AND at least one positive, so "at
least one positive" is never decided from a field that was never read.

**The gate schema is now exact, and authentication never defaults to success.**
`GATE_REQUIRED` had omitted `cert_hash_matches_row` while `outcome` defaulted an
absent value to `yes`, so deleting that ENTIRE COLUMN from the genuine R2 gate
table left a run that exited 0, printed `reconciled=YES` and still claimed
93/122. Both gate tables are now pinned to their exact canonical headers by
name and order -- 11 columns for a run table, 10 for the baseline's older shape
-- and a missing authentication field is missing evidence, never success.

**The BASELINE rows are held to the same canonical identity as the new run's.**
Replacing the ungated `intpipe_csr_file` baseline row with a duplicate
`core_top` row preserved 122 rows of 18 fields; reconciliation exited 0 and
FABRICATED a twelfth transition (`intpipe_csr_file absent -> front-end-failure`),
because the dropped module read as "absent". Both row tables must now be the
declared 122-module sequence exactly -- same modules, once each, in order --
before any transition is computed, and the baseline gate subset must cover its
candidate set exactly once each, in relative order, with no rows for
non-candidates.

`run_driver_controls.sh` grows to 24/24, adding real-entrypoint negative
controls for a malformed OK counter, a malformed LIMIT_HIT counter, the deleted
authentication COLUMN (not merely a row), and the duplicate/omitted baseline
row -- plus two assertions that nothing downstream is computed from unreconciled
input: no coverage figure when the gate schema is incomplete, and no transition
when the baseline identity fails.

**Re-verified against the real R2 run**, output to scratch so no committed table
was touched: `coreet_run_cgroup_records.sh` exit 0 (122 complete records) and
`coreet_run_gates.sh` exit 0 (`93 of 93 candidate rows accepted`), both with
data rows BYTE-IDENTICAL to the committed tables, and reconciliation still
`93 of 122` with `blocks that moved 11`.

### Third correction: the counter DOMAIN, shared by every D4 path

A cgroup kill counter is an UNBOUNDED decimal written by the kernel, but Bash's
`[[ -gt ]]` / `[[ -ne ]]` are fixed width. `18446744073709551616` (2^64) is
canonical, nonzero, and evaluates to **exactly zero** in Bash arithmetic --
`$((18446744073709551616))` prints `0`. Every positivity test built on `-gt 0`
therefore read that value as "no kill". Reproduced on the real entrypoints: an
OK record with `oom_kill=18446744073709551616` passed
`coreet_run_cgroup_records.sh` with exit 0 and "every verdict backed by its own
evidence", and `d4_scope_verdict 18446744073709551616 0 0 success 1 1` returned
`OK`.

This was never one driver's bug: the same decimal domain is read when the
evidence is COLLECTED (`scope_merge` took maxima with `-gt`), when the verdict
is DECIDED (`d4_scope_verdict`), when a run is ACCEPTED
(`coreet_run_cgroup_records.sh`), and when it is VERIFIED
(`scripts/verify_d4_run.sh`). A huge positive counter could be lost at every
one of them.

**One policy now, in the sourceable file the sweep itself uses** -- validate the
canonical-decimal SYNTAX first, then decide zero/nonzero TEXTUALLY, and compare
magnitudes by length then lexicographic order. No counter is ever handed to Bash
arithmetic; ordinary loop and tally arithmetic is untouched.

    d4_counter_is_canonical   ASCII canonical nonneg decimal: no sign, no
                              leading zeros, no whitespace, not empty
    d4_counter_is_zero        textual: a canonical decimal is zero iff it is "0"
    d4_counter_is_nonzero     canonical AND not "0"
    d4_counter_gt             length, then lexicographic under LC_ALL=C;
                              returns 2 when either side is not canonical, so
                              "not greater" can never be read as "well formed"

`LC_ALL=C` in the syntax check is load-bearing, not hygiene, and this was found
by writing the control rather than assumed: under a UTF-8 locale `[0-9]` matches
other Unicode decimal digits, so a FULLWIDTH ONE (U+FF11) passed validation and
then went on to be compared textually against ASCII digits. A counter is a byte
string written by the kernel and is validated as one.

`counter_domain_controls.sh`, 12/12, drives the real paths: it first asserts
that Bash really does wrap 2^64 (so the controls are not vacuous), then pins the
primitives (including `9` vs `10`, which length-blind lexicographic order would
get wrong, and the rejected forms `""`, `" "`, `-1`, `+1`, `007`, `0x10`, `1e3`,
U+FF11); then `d4_scope_verdict` returning LIMIT_HIT for a 2^64 counter on
either field and UNKNOWN for a non-canonical one; `scope_merge` preserving a
2^64 kill against a later `3` and an unbounded `memory_peak`, and the merged
value still declaring a LIMIT_HIT; and the result driver REJECTING an OK with a
2^64 counter while ACCEPTING a LIMIT_HIT with one whose runtime row is a
non-running resource limit.

**The genuine evidence is unmoved.** Re-run to scratch, no committed table
touched: `coreet_run_cgroup_records.sh` exit 0 over the retained R2 run with
rows BYTE-IDENTICAL to the committed table, and reconciliation exit 0 at
`93 of 122`, `blocks that moved 11`, `no longer ACCEPTED 0`. All eight affected
control suites stay green: driver 24/24, emission-ceiling 17/17, verify 9/9,
replay-gate 5/5, differential-exemption 8/8, GetMask 4/4, Sext 19/19, replay
driver 12/12.

## The two source-only certificates: a stale duplicated gate, not a failure

`null_vpu` (10 sources, 0 nodes) and `minion_dcache_texsend` (7 sources, 0
nodes) are SOURCE-ONLY designs: every output names a constant source, so zero
COMPUTED nodes is the correct shape, not an empty certificate. Both already
passed the canonical `vc_gates.py` and the certificate invariant. They were
rejected by a second, divergent copy of the rules living inline in
`scripts/run_coreet_module_lean.sh`:

    [[ "$n_nod" -gt 0 ]] || FAIL: no nodes emitted

The canonical gate draws the line in the right place -- zero nodes WITH at least
one source is valid; no sources AND no nodes is an empty certificate and fails
-- and it had the two blocks named in its own comment. The emission path now
CALLS that gate instead of restating it. That is strictly more gate, not less:
delegating also gains the constant-width-fit and ROM-entry-fit checks the inline
copy never had, and every other fail-closed check in the script is untouched.

**A real weakness found by writing the control.** The canonical gate's
MULTI-LINE "no ResidualProgram in a theorem statement" detector required `:=` to
begin its own line, so it missed the shape the emitter actually produces:

    theorem T_step_correct : forall inp st,
        T_residual inp st = ... :=

Measured on three realistic shapes, two were missed. A theorem's statement now
runs from the `theorem` keyword to the first `:=` across however many lines and
is scanned that way; the conservative single-line rule is kept alongside it.
Re-checked against every retained artifact -- 95 from the R2 run and 85 from the
accepted baseline run -- **0 rejected**, so the stricter detector costs the real
corpus nothing.

`static_gate_policy_controls.sh`, 19/19, pins both halves. It asserts
structurally that the emission path delegates, that no executable node-count
rule remains in that branch, and that a failing gate still sets `gate_status`;
then it drives the policy itself: a well-formed source-only design PASSES, a
truly empty certificate FAILS, each of the four required declarations FAILS when
absent (renamed by PREFIX -- a `_RENAMED` suffix still contains the original and
tests nothing), `maxRecDepth` and `maxHeartbeats` FAIL when absent, a
single-line and a MULTI-LINE theorem naming a ResidualProgram both FAIL, a
`sorry` FAILS, a `def <Top>_residual` still PASSES (the documented carve-out),
and both gained checks FAIL on an over-wide constant and an over-wide ROM entry.
Its last two legs run the REAL retained source-only artifacts through the
canonical gate.

**Both blocks measured end to end** (`coreet_srconly_*`), rerun alone from the
accepted worktree and authenticated CORE-ET foundation at the existing 8192 KB
stack and 8 GiB emission ceiling:

    null_vpu               EMITTED  sources=10 nodes=0  PARSED/true/RAN  digest 4546364551384081875
    minion_dcache_texsend  EMITTED  sources=7  nodes=0  PARSED/true/RAN  digest 4836969857988060788

emitter rc 0, terminal `phase=exit` cgroup records with `oom_kill=0` and
`oom_group_kill=0`, `cleanup=clean`, and certificate hashes BYTE-STABLE against
the ones the R2 run had written but never executed. Verilog regenerated from
each block's own resulting LGraph: `vc_gates`, the invariant and the real
`--verilog --require-verilog` differential pass **2 of 2**.

**WHAT THIS IS AND IS NOT.** These two designs are translation-validated
EMPIRICALLY, exactly like every other accepted block: the certificate was
executed, its hash and shape authenticated, and its constants checked against
independently regenerated cgen Verilog. It is not a generic formal proof of the
C++ LGraph-to-DCERT1 transcription, and a source-only design exercises a
narrower slice of that transcription than one with computed nodes -- there are
no operator semantics to disagree about. What the unit establishes is that the
harness no longer rejects a valid shape, not that the shape proves more than it
does.

The historical `coreet_122_fresh_*`, `coreet_122_sext_replay*` and
`coreet_122_r2_*` tables are byte-unchanged; this run is recorded separately and
the 122-block corpus was not relaunched.

## Type-1 synchronous memory read: the registered dout, re-expressed

Three CORE-ET blocks refused with `memory-sync-read-unsupported`. Their exact
shapes, measured from each block's own retained `lgdb_raw` rather than
generalized from one instance:

| block | cells | shape | BITS | SIZE | WENSIZE | FWD | UNDEF | rd_enable | wr/rd clock |
|---|---|---|---|---|---|---|---|---|---|
| `minion_dcache_128x64_1r1w_lram` | 2 | 1rd/1wr, `LATENCY_0=1` | 32 | 128 | 32 | 1 | absent (0) | const 1 | both `clk_i`, rising |
| `minion_dcache_128x72_1r1w_lram` | 2 | 1rd/1wr, `LATENCY_0=1` | 36 | 128 | 36 | 1 | absent (0) | const 1 | both `clk_i`, rising |
| `minion_dcache_data_array` | 8 | 1rd/1wr, `LATENCY_0=1` | 32 | 128 | 32 | 1 | absent (0) | const 1 | both `clk_i`, rising |

`WENSIZE == BITS` throughout, so the write mask is bit-granular. Every port
resolves to the SAME reference clock through a width-mask cone, and `FWD=1`
means a read-during-write sees the NEW data. The composed `data_array` is eight
instances of the same shape, not a different one.

### What the lowering does

`type` 1 emits `always @(posedge rd_clock) rd_dout <= d_fwd` INSIDE the cell,
and that register has no enable pin anywhere in the Memory model. Ungated it
captures on both sub-steps; gating `rd_enable` is not a fix, because
`rd_enable` is the DATA QUALIFIER on `d_fwd` (`rd_enable ? data[addr] : x`) and
gating it fabricates an `x`.

So the register is MOVED OUT. The cell becomes `type` 0 -- cgen then emits
`assign rd_dout = d_fwd`, the same expression with the same `rd_enable`
qualifier and the same `fwd`/`undef` collision logic -- and an ordinary `Flop`
on the reference clock, enabled by that cell's own slot predicate,
re-registers it. One capture per source period, at the source's edge.

Read-during-write survives because the read is captured in the SAME step the
write commits in and a step reads before it commits, so the combinational
`d_fwd` sees the write inputs the source's `always` block saw at that edge.
That is why the READ port's effective edge is now resolved on exactly the same
terms as a write's and required to AGREE with it; a read on the opposite edge
would be moved relative to the write it forwards, and refuses.

Refusals, all before any mutation: a read clock that is unresolved, constant or
off-reference (`memory-sync-read-unsupported`), a read whose effective edge
differs from the writes' (same code, or `memory-mixed-edge` when the cell
carries the mixed sentinel), a gated read clock (`memory-gated-clock`). Type-2
/ whole-array, multi-clock-no-ratio, resource-limit, timeout and Slang cases are
untouched.

### Regressions

`pass_single_edge_test.cpp` grows to 7 direct-LGraph cases. The three new ones
build a 1wr/1rd `type` 1 cell by hand: the POSITIVE proves the cell ends up
`type` 0 with exactly one enabled flop registering its dout
(`mem_reads_registered=1`, `mems_slotted=1`, P=2), and the two NEGATIVES -- read
on a second clock net, and read reached through a `Not` so its effective edge is
opposite -- prove refusal with NO PARTIAL MUTATION: the cell is still `type` 1
and no read flop was created. The four per-port write-edge cases are unchanged.

`single_edge_memory_slot_test.sh` grows to 21 legs, all fail-closed on
iverilog/vvp/`ware/rtl`. Two new type-1 designs are compared against their
ORIGINAL RTL over 39 periods with a stimulus that walks read-during-write,
write-elsewhere, read-only with a changing address, and the other no-collision
order -- `memt1f` (FWD=1, the mode all three blockers use) and `memt1o` (FWD=0),
each behind its own vacuity guard. Structurally, the normalized cell must be
`LATENCY_0=0` and `rd_enable` must come through untouched, with an explicit
check that the slot predicate was NOT substituted for it.

**The read-slot negative controls run on `memt1o`, and that is not arbitrary.**
With FWD=1 and a single write port the read's capture slot is NOT observable at
a period boundary: forwarding already makes the slot-0 reading equal the slot-1
one, and a non-colliding read is unaffected either way. Measured -- the ungate
control passed against `memt1f`. With FWD=0 the two readings genuinely differ on
a collision, so ungating the re-registered read and moving it to the other slot
both FAIL there, as they must.

### Measured

Front-end replay over all 118 blocks carrying a diagnostic: exactly the three
named blocks moved, `memory-sync-read-unsupported -> single-edge-applied`, and
nothing else changed. 13 of the 24 baseline refusals now normalize, the seven
`multi-clock-no-ratio` refusals still refuse for the same reason, and **0**
prior non-refusals regressed.

End to end from the authenticated foundation at the existing 8192 KB stack and
8 GiB ceiling (`coreet_type1_*`):

    minion_dcache_128x64_1r1w_lram  EMITTED  sources=88  nodes=90   PARSED/true/RAN  digest 16967442078651432297
    minion_dcache_128x72_1r1w_lram  EMITTED  sources=88  nodes=90   PARSED/true/RAN  digest 12661031863931189286
    minion_dcache_data_array        EMITTED  sources=472 nodes=509  PARSED/true/RAN  digest 10932018382330941000

emitter rc 0, terminal `phase=exit` records with `oom_kill=0`,
`oom_group_kill=0`, `cleanup=clean`. Verilog regenerated from each resulting
`lgdb_norm`: `vc_gates`, the certificate invariant and the real
`--verilog --require-verilog` differential pass **3 of 3**.

**TRUST BOUNDARY.** The direct graph tests, the Icarus traces, the
authenticated corpus inputs, the hashes, the runtime rows, the Lean execution
and the static/differential gates are empirical translation validation, not a
generic formal proof of the C++ LGraph-to-DCERT1 transcription. The iverilog
comparison covers 39 periods of one stimulus per design, not all behaviours; it
is an independent oracle, not exhaustive.

Historical `coreet_122_fresh_*`, `coreet_122_sext_replay*` and
`coreet_122_r2_*` tables are byte-unchanged and the 122-block sweep was not
relaunched.

### Correction: the disabled read, compared end to end

Review found the type-1 Icarus designs had no read-enable input at all --
`rd_enable` imported as the constant 1 and the stimulus toggled only the WRITE
enable, so the structural "the slot predicate was not substituted for
`rd_enable`" check was not backed by an end-to-end comparison of disabled-read
behaviour. Correct, and now fixed in the two places the behaviour actually
lives.

**A measured front-end fact, pinned rather than assumed.** No Verilog source
reachable through this importer yields a TYPE-1 cell whose `rd_enable` is
anything but the constant 1. Four forms were tried:

| source form | imported as |
|---|---|
| `qs <= re ? mem[ra] : 'x` | type 1, but `re` is **entirely unused** -- the `x` arm lets the optimiser drop it |
| `if (we) …; qs <= re ? mem[ra] : 'x` (same block) | type 1, `re` unused |
| `always @(posedge clk) if (re) qs <= mem[ra]` | **async** cell + a separate enabled flop |
| the same with the write in one block, or via an intermediate `rdata` | **async** cell + a separate enabled flop |

So the disabled-read semantics are compared end to end on the shape the
importer actually produces, and the type-1 cell keeping its OWN qualifier is
pinned where that shape is reachable -- at the LGraph level.

**End to end (`memre`).** A design with a real `re`, whose imported shape the
test ASSERTS (`LATENCY_0=0` plus a live `re` in the netlist) rather than
assumes, with a matching assertion that the two type-1 designs' `rd_enable`
really is constant 1 -- so neither leg can quietly start testing something
else. `re` is toggled across 39 periods through enabled read-during-write,
DISABLED read-during-write, an address change WHILE DISABLED, disable→enable
read-back, enabled no-collision, and enable→disable, compared against the
original RTL with FOUR-STATE equality (`!==`, so a hold cannot be satisfied by
"both sides are eventually known"), behind its own vacuity guard. A new
negative control ties `re` high on the normalized side only and must FAIL --
without it, a green run would not prove the disabled cases were ever observed.

**At the LGraph level (`SyncReadKeepsItsOwnEnableQualifier`).** A type-1 cell
is built with its read-port `enable` driven from an INPUT. After normalization
the test asserts that the SAME input still drives the Memory's read port, that
the re-registered flop's enable is NOT that input, and that it is an `EQ`
comparing the `single_edge_phase` divider. That pins the C++ rewrite's
separation of the read-data QUALIFIER from the normalization SLOT GATE directly,
independent of what the Verilog importer canonicalizes.

`pass_single_edge_test.cpp` is now 8 direct-LGraph cases and
`single_edge_memory_slot_test.sh` 26 legs; the FWD=0/FWD=1 traces, the
wrong-slot and ungate controls, and the off-reference and opposite-edge
refusals are unchanged. Both targets pass on a forced rerun. The three-block
pilot and the 122-block corpus were NOT rerun -- their retained evidence is
already authenticated and every historical table is byte-unchanged.

## Memory `type` is the RD_CLK_ENABLE mask, not an enum

**The previous unit's classification was wrong and is withdrawn.** It read
`type` as an enum, called everything outside {0,1} a combinational "array", and
SKIPPED those cells. `inou/yosys/lgyosys_tolg.cpp` sets that pin directly from
`$mem_v2`'s `RD_CLK_ENABLE` parameter -- a PER-READ-PORT BITMASK, one bit per
read ordinal, set when that read is CLOCKED -- and the same importer wires
`clock_pin` for every read and write port. 0 means "every read is
asynchronous" and 1 means "read ordinal 0 is clocked", which is why a one-read
synchronous cell reads as `type == 1` and looks like an enum. It is not one.

`24` is `0b11000`: read ordinals **3 and 4 are clocked**, on a cell that also
has **two clocked write ports**. Skipping it left real clocked state outside the
P=2 phase plan.

The "cgen contract" used to justify the skip is itself a multi-read bug:
`inou/cgen/cgen_verilog.cpp` recognizes only an exact 0 or 1 as a memory wrapper
and sends every other mask down its `always_comb` zero-filled array branch (and
then declares those douts `wire` while assigning them procedurally). A rendering
produced by that branch is not evidence about what the cell means, so the oracle
built on it -- `comb_array_oracle.sh` -- and the `coreet_combarray_*` tables are
**removed**, not kept.

### What the pass does now

`type` is decoded as a mask. For each read ordinal whose bit is SET, that read's
clock cone and effective edge are validated on the same terms as a write's and
its output is re-expressed as an external slot-enabled `Flop`; a CLEAR bit is an
asynchronous read and is left completely alone. Every write port is validated
and slot-gated independently. Once every clocked read is externalized the cell's
own mask is rewritten to 0, and every clocked port's clock is rebound to the
reference. The whole-array `update` refusal still comes first.

Refusals before any mutation: a non-constant or negative mask, a mask naming a
read ordinal the cell does not have, an unresolved/off-reference/gated clock, or
a clocked read whose effective edge differs from the writes'.

### Controls

`pass_single_edge_test.cpp` is 14 direct-LGraph cases. Six are new and build a
five-read / two-write cell with mask 24: the POSITIVE proves only ordinals 3 and
4 gain slot-enabled flops, ordinals 0-2 stay combinational, both write enables
are gated, every clocked port's clock is rebound, and the cell's mask becomes 0;
an all-async mask leaves every read alone while still gating the writes; and
four NEGATIVES -- opposite-edge read, off-reference read, a mask naming a
nonexistent ordinal, and a driven whole-array bus -- each refuse with nothing
mutated (the mask unchanged, no flop created). The dead `if (false)` block left
by the withdrawn unit is gone.

### Measured: the two blocks are GATED-CLOCK, and still refuse

With the mask decoded correctly, `minion_frontend` and
`minion_frontend_thread_buffer` are revealed as clocked state on a GATED clock
-- which the pinned CORE-ET source says directly:

    always_ff @(posedge clock_gated)
      if (f0_buffer_pc_en[buffer]) buffer_pc[buffer] <= f0_pc;

Both now refuse with **`memory-gated-clock`**: "is clocked through a gate cone,
whose enables have no representation in a slot gate". That is an accurate,
specific, pre-existing refusal class, and repairing it is explicitly out of this
unit.

So **these two blocks do not clear, and this unit does not claim they do.** The
118-block replay returns to 13 cleared with the two blocks RENAMED from
`memory-unsupported` to `memory-gated-clock`; all prior non-refusals stay
non-refusals, the seven `multi-clock-no-ratio` refusals and every other named
refusal keep their exact codes, and **0** regressed. No runtime-certificate run
was attempted for them: a refused block has nothing to run. Accepted coverage is
unchanged.

What this unit buys is a correct classification -- the cells are clocked state
on a gated clock, not combinational arrays -- and a mask decoder that will
handle them the moment the gated-clock class is addressed.

**TRUST BOUNDARY.** The direct graph tests, the authenticated corpus inputs, the
hashes, the runtime rows, the Lean execution and the static/differential gates
are empirical translation validation, not a generic formal proof of the C++
LGraph-to-DCERT1 transcription.

### Correction: the mask reader is now total

Review found the mask reader still had three ways to misread rather than refuse,
all inherited from `const_i64` -- a helper written for comptime fields that
legitimately have a DEFAULT:

  * an **absent** `type` pin returned the default as a SUCCESS, so a cell with
    no mask at all was declared "every read asynchronous". The mask was
    MISSING, not zero -- and cgen's own undriven default is 2, not 0.
  * it called `to_just_i64()` after only an `is_const_pin` test. That method
    ASSERTS unless `is_just_i64()` holds, so an **X-bearing** constant, or one
    **wider** than the API can return, ABORTED the pass instead of refusing.
  * it narrowed to `int`, so a known i64 outside `int` -- `1 << 40`, say --
    **truncated to zero** before any range check could see it, and was then
    accepted as all-asynchronous.

`type` is now read on its own strict path (`read_rd_clk_mask`), separate from
the defaulted fields. Missing, non-constant, non-`is_just_i64`, negative, and
out-of-representable-range all return `memory-unresolved-type` by name, with no
assertion, no coercion to zero, no narrowing cast, and nothing mutated.

`pass_single_edge_test.cpp` is 20 direct-LGraph cases. Six new ones drive each
boundary from a real P=2 graph -- absent pin, dynamic driver, `Dlop::unknown`
constant, a 200-bit `from_binary` constant, a negative mask, and `1 << 40` --
and every one asserts the exact named refusal AND that the graph is untouched:
the mask unchanged, zero inserted read flops, and both write enables and clocks
still on their original drivers. The nonexistent-read-ordinal and
whole-array-priority cases are retained.

Re-measured: both retained blocks still refuse with **`memory-gated-clock`**
(rc 7), and the committed 118-row classification is **byte-identical**. No
corpus was rerun and no runtime row was invented for a refused block.

#### The type-edge controls were vacuous; now they are load-bearing

The six boundary controls checked `Result` counters, inserted read flops and
the write enables/clocks -- but never the raw `type` edge (pid 7) they were
supposed to be proving was preserved. One was worse than silent:

    ASSERT_TRUE(mem_driver_of(*g->fast_class().begin(), 7).is_invalid() || true);

`fast_class()`'s first node is not guaranteed to be the Memory, and `|| true`
made the assertion pass unconditionally. The suite would have stayed green if
the refusal path had deleted, replaced or rewritten that edge.

Each boundary now snapshots the edge BEFORE `run()` and compares it after:
whether it is driven at all, by WHICH pin, whether that pin is constant, whether
it reads back as a plain integer, and with what value. Per case: the absent
driver must be invalid before AND after (and must not be MINTED by the refusal);
the dynamic case must still be the exact original `we` pin and still
non-constant; the X-bearing and over-wide constants must keep their exact pin
identity and still be non-`is_just_i64`; the negative and out-of-range integers
must keep their exact values. `memory_node_of` finds the Memory properly instead
of trusting iteration order. Every existing check -- the exact refusal reason,
zero read-flop and slot counters, write enable/clock identity -- is kept, not
replaced.

**Proved load-bearing, not assumed.** Injecting a single `drop_sink_pid(n, 7)`
into the negative-mask refusal path makes the suite fail with exactly the right
message -- "the raw `type` edge (pid 7) changed across the refusal (driven
true->false, const true->false, just_i64 true->false, value -8->0)" -- and the
injection was reverted.

20 of 20 cases pass; both retained blocks still refuse with
`memory-gated-clock`; the committed 118-row classification is untouched by this
unit.

## Inline clock gates on a memory's timed ports

The last measured front-end blockers were `minion_frontend` and
`minion_frontend_thread_buffer` (and, unannounced until it was probed,
`minion_dcache_top`). All three hold a cell whose write ports -- and, in the
frontend pair, two of whose five read ports -- are clocked through an inlined
`prim_clk_gate`:

    always_ff @(posedge clock_gated)
      if (f0_buffer_pc_en[buffer]) buffer_pc[buffer] <= f0_pc;

A GATED CLOCK IS A COMMIT ENABLE: the port commits on the reference clock's edge
iff its enables hold there. That is exactly what a slot predicate can carry and
a gated CLOCK is not, so each TIMED port's gate is folded into THAT PORT'S
commit predicate, on the same terms a gated flop's is.

| port | after the fold |
|---|---|
| write `b` | `wr_enable_b = (slot & gate_b) ? <full lane mask> : 0` |
| clocked read `r` | the externalized read flop commits on `(slot & gate_r)` |
| async read | untouched: no clock, no commit, nothing to gate, and its constant `clock_pin` is NOT rebound |
| `rd_enable` | untouched: it is the read-DATA qualifier, never a commit gate |

PER PORT, never cell-wide. Each Memory port carries its own `clock_pin`, so one
port can be gated and another not, and two gated ports need not share a cone.
`Mem_elem::port_icg` is a map from port block to that port's enables; hoisting
one port's gate to the cell would suppress writes the source made, which is the
same class of defect the per-port EDGE computation already exists to prevent.
The gate goes into the Mux SELECTOR, never into the mask -- `mask & gate`
zero-extends a 1-bit gate and keeps lane 0 alone.

### What still refuses, and two new names

Nothing was weakened to make the corpus blocks pass. The gate is folded only
for a representable inline `<reference clock> & <enables>` cone; everything else
refuses BEFORE any port, mask, read output or clock is touched:

| code | shape |
|---|---|
| `memory-clock-cell-unsupported` | **new.** A MATERIALIZED `Clock_cell` carries the glitch-free sampling contract -- its enable is sampled on the inactive phase before its own active edge -- and a slot lowering has no sub-period to sample in. Refused on exactly the terms a gated FLOP's is |
| `memory-clock-divider-unsupported` | **new.** A 2^N division is a different PERIOD, not an enable. The inline path never produces one today, so this is a total guard rather than a live branch |
| `memory-gated-clock` | retained, now specific: an `And` cone whose clock operand is AMBIGUOUS (no clock operand, or more than one), or a gate enable that does not resolve to a pin |
| `memory-off-reference-clock` | a gate rooted on a second clock net |
| `memory-mixed-port-edge` | two gated write ports on OPPOSITE edges of the reference |
| `memory-whole-array-unsupported`, `memory-write-no-enable`, every `memory-unresolved-*` | unchanged |

`stop_at_clock_cell` is load-bearing in the first of those. WITHOUT it
`resolve_phase` walks THROUGH the cell to `clk_ref`, the port then compares
EQUAL to the reference clock, and the gate is silently dropped -- the cell would
be slot-gated, its clock rebound to the ungated reference, and every suppressed
write would happen.

### The enable-latch bypass had never fired on a Verilog design

A real ICG holds its enable in a latch transparent on the opposite phase, so the
endpoint sees the value the latch is passing THROUGH, not its held Q. The pass
already had that rule for flops -- and it had never fired for a design read
through Verilog, because `resolve_icg` records the RAW `And` operand and both
readers wrap a latch output in a `Get_mask` first. The rule is now one function,
`bypass_enable_latches`, used by the flop fold and the memory fold alike, and it
descends the two true identity wrappers (`Get_mask`, `Sext`) and nothing else: a
`Not` or a `Mux` is not an identity, and following one would drop an inversion
out of an enable.

Reading the transparent value is then DECIDED, never guessed:

| `din` | transparent value |
|---|---|
| not a mux (raw yosys D/EN, Pyrope, a constant, a port) | `din` |
| a HOLD MUX `window ? d : q` -- one arm is the latch's own Q | the other arm |
| `<the latch's OWN enable cone> ? d : <anything>` | `d` -- **the measured CORE-ET shape**: slang gives `prim_clk_gate`'s latch `din = ~clk ? (en_i \| scanmode) : 0` with `enable = ~clk` |
| anything else, or an active-LOW window | invalid: the enable is left exactly as found |

`lc::latch_transparent_arm` cannot make that distinction -- it returns the first
arm that is not the latch's Q, so on the third shape it hands back whichever arm
it visits first. Measured on `minion_dcache_reduce`: that was the CONSTANT arm,
and folding it dropped the gate off all 58 of that block's gated flops, i.e. the
fold became "commit every cycle". Hence a local reader rather than that helper,
and hence the two injection experiments below.

**BLAST RADIUS, measured, not asserted** (`coreet_gate_bypass_ab.tsv`). The
front end was re-run on every retained block of the authenticated 122 corpus
with the bypass change present and absent, and the cgen Verilog hashed both
ways (emission was first checked deterministic: same binary twice, three blocks,
identical hashes).

    identical 99   both-refused 12   emission-changed 11   acceptance-changed 0   refusal-code-changed 0

The eleven are the three newly-cleared blocks plus `minion_dcache_cache_op_unit`,
`..._l2`, `minion_dcache_reduce`, `minion_dcache_tensor_load`, `txfmafrac_top`,
`txfma_top`, `vpu_lane_tima`, `vpu_trans`. Every change is the same one and was
read off the netlist: on `minion_dcache_reduce` all 58 gated flops move from
`if (get_mask_8980_u)` -- `{1'b0, u_clk_gate.en_latch}`, the latch's HELD Q --
to `if (\u_clk_gate.en_i )`, the enable the gate actually samples. **Those nine
blocks' recorded emissions predate this fix**; the historical tables are
byte-unchanged and no corpus was rerun. Whether to re-measure them is not this
unit's call.

**A SEPARATE, UNFIXED DEFECT, reported not patched.** In the same blocks the ICG
latch is itself retyped into a Flop that keeps its `~clk` transparency cone as
an enable, so in the emitted Verilog it never commits at a `posedge clk`.
`commit_class_of` classifies those latches as DATA-gated because `resolve_phase`
stops at the reduction node the reader puts in the enable cone. After this fix
nothing reads that flop -- its only consumer is the now-dangling `clock_gated`
net -- so it is dead logic rather than a live wrong value, but the latch path
itself is still wrong for every Verilog-sourced ICG. Fixing it means touching
`graph/latch_contract.cpp` and a different refusal family, so it is left alone
and named here.

### Controls

`pass_single_edge_test.cpp` is now **34** direct-LGraph cases. The new fixture
builds the measured shape by hand -- 2 write ports, 5 reads, `RD_CLK_ENABLE` 24
(ordinals 3 and 4 clocked, 0-2 async), a live `rd_enable` on ordinal 3, and up
to three DIFFERENT gate enables on different ports -- because no `.v` source
produces that combination on demand.

* a gated write keeps its FULL lane mask, with a predicate that is exactly
  `{slot, its own gate}`; the ungated port beside it gets exactly `{slot}`;
* each clocked read flop commits on `{slot, its own gate}`; the ungated clocked
  read beside it on `{slot}`;
* three gates on three ports, each checked for the ABSENCE of the other two --
  a cell-wide gate list produces cross-contamination, not a missing term;
* async reads keep their constant `clock_pin`, their `rd_enable`, and get no
  flop; every TIMED clock is rebound to the reference; the mask becomes 0 only
  on success;
* the enable-latch bypass, parameterised over all three readable latch shapes,
  each asserting the fold took the transparent value and NOT the latch's Q;
* the UNREADABLE shape is left alone -- the fold must not reach past it;
* no-mutation negatives for a materialized `Clock_cell`, an off-reference gate
  root and two gates on opposite edges, each comparing a full before/after
  snapshot of the mask and every port's clock and enable driver.

**Both halves proved load-bearing by injection, then reverted.** Making the
reader never recognize the own-window shape fails the `WindowMux` case with
"the gated write took the enable LATCH's own output -- a full cycle late";
making case 4 GUESS an arm fails `UnreadableEnableLatchIsLeftAlone` with "the
fold reached past an unreadable latch to the enable behind it".

### The end-to-end oracle, and why the usual vacuity guard is unavailable

`lhd/tests/single_edge_memory_gate_test.sh` compares the PINNED ORIGINAL gated
RTL against the normalized emission under Icarus, over 38 periods, with the
normalized side clocked twice per source period and both sides sampled at period
boundaries: gate enable/disable transitions, writes while the gate is SHUT,
addresses moving while shut, both clocked reads, all three async reads, and
read-during-write, compared with FOUR-STATE equality.

It does NOT use LiveHD's own emission of the unnormalized cell, and that is
measured rather than assumed: cgen renders a cell whose `RD_CLK_ENABLE` is
neither 0 nor 1 through a branch that declares each dout as a `wire` and then
assigns it procedurally, so **the raw round trip does not even elaborate**. The
test asserts that, by its exact iverilog error, and fails if it ever starts
elaborating -- at which point the ordinary round-trip guard should be reinstated.

In its place the non-vacuity argument is five load-bearing negative controls,
each breaking exactly one thing the fold owns:

| control | errors |
|---|---|
| drop the GATE term from write port 0 (keep the slot) | 20 |
| drop the SLOT term from write port 0 (keep the gate) | 38 |
| ungate write port 0 entirely | 58 |
| drop the gate from a re-registered read flop | 9 |
| clock the normalized side in a 1x time base | 140 |

The same source is then re-read through **`--reader slang`** -- the reader the
CORE-ET corpus uses -- which gives a different cell (WENSIZE 1, the clocked
reads lifted out of the memory entirely, both write ports still gated) and is
compared against the same original RTL, with its own drop-gate control (20
errors). Two refusal legs close it: two gated write ports on opposite edges
(`memory-mixed-port-edge`) and a gate rooted on a second clock net
(`memory-off-reference-clock`), both from real Verilog sources.

### Measured: the front end clears three blocks; the emitter then hits the ceiling

**Front-end replay over all 118 blocks carrying a diagnostic**
(`coreet_frontend_refusals.tsv`, re-run on each block's RETAINED `lgdb_raw`):

    blocks probed              118
    baseline REFUSALS (rc=7)   24
      now NORMALIZE (rc 0)     16   (was 13)
      refuse, SAME code         7   (every multi-clock-no-ratio, unchanged)
      refuse, NEW named code    1   (vpu_rf, unchanged)
    baseline non-refusals that now REFUSE   0

Three blocks moved, `memory-gated-clock -> single-edge-applied`:
`minion_frontend`, `minion_frontend_thread_buffer` and -- not named in the
unit, found by probing -- `minion_dcache_top`. The seven `multi-clock-no-ratio`
refusals still refuse for the same reason, and **0** prior non-refusals
regressed.

**Runtime pilot, all three blocks** (`coreet_gate_pilot_*`), from the
authenticated foundation at the standing 8192 KB stack, 3600 s / 1800 s
timeouts and 8 GiB emission ceiling -- the SAME limits every earlier pilot used;
nothing was raised to make a block fit:

    minion_frontend                 compile 0  single_edge 0   NO_CERT  resource-limit
    minion_frontend_thread_buffer   compile 0  single_edge 0   NO_CERT  resource-limit
    minion_dcache_top               compile 0  single_edge 0   NO_CERT  resource-limit

**The blocker MOVED; it did not disappear.** Each block's own `emit.log` records
`compile exit=0` and `single_edge exit=0` -- the refusal this unit removes is
gone -- and the scope was then killed inside `pass.lean`, the certificate
emitter. All three cgroup records are complete and every verdict rests on its
own evidence: `LIMIT_HIT`, `oom_kill=1`, `oom_group_kill=0`, `memory_peak_bytes`
at the 8 GiB cap, `cleanup=clean`, with 0 malformed counters and 0 LIMIT_HIT
verdicts lacking a positive kernel counter.

So this unit does **not** produce a green runtime row for any of the three. The
gates driver is recorded saying exactly that -- `candidate RAN rows 0`, `GATES
FAILED: no candidate RAN rows at all; there is nothing to accept and nothing was
verified` -- rather than being skipped, and **no runtime row was invented for a
block that did not emit**. Whether the 8 GiB ceiling should move for designs of
this size is a separate question and not this unit's to answer.

Historical `coreet_122_fresh_*`, `coreet_122_sext_replay*`, `coreet_122_r2_*`,
`coreet_type1_*` and `coreet_srconly_*` tables are byte-unchanged; the 122-block
sweep was not relaunched.

**TRUST BOUNDARY.** The direct graph tests, the finite original-source Icarus
differentials, the authenticated corpus inputs, the cgroup observations, the
runtime rows, the Lean execution and the static/differential gates are empirical
translation validation, not a generic formal proof of the C++ LGraph-to-DCERT1
transcription. The Icarus comparison covers 38 periods of one stimulus per
reader, not all behaviours; it is an independent oracle, not an exhaustive one.

## Correction: the enable-latch bypass now fails closed

Review found two fail-open contracts in the bypass the previous unit shared
between the memory fold and the ordinary gated-endpoint fold. Both are real,
both were reproduced before being fixed, and both are now refusals.

**1. An unreadable enable latch was accepted with its Q.** `bypass_enable_latches`
returned `void`: when `latch_transparent_value` could not prove a transparent
value it silently left the latch's Q in the enable list, and `plan_one_memory`
rejected only an *invalid* pin -- so that Q went into `port_icg`, the memory was
rewritten, and the pass reported success. The previous unit's
`UnreadableEnableLatchIsLeftAlone` test codified exactly that result. It is not
a conservative fallback: in this pass the enable latch is itself retyped into a
slot-enabled Flop, so folding its Q makes the endpoint commit on the PREVIOUS
period's enable -- the full-cycle-late state the readable cases exist to avoid.

**2. The transparency WINDOW was never checked.** The bypass validated the
latch's `din` shape and nothing about when the latch was open. The reviewer's
probe, reproduced here verbatim:

    always @(clk or ce) if (clk) en_latch <= ce;   // transparent HIGH
    assign gclk = clk & en_latch;
    always @(posedge gclk) if (we) mem[wa] <= d;

normalized to the write predicate `single_edge_phase == 0 & ce`. That latch is
open ACROSS the endpoint's own rising edge, so `ce` moving while the clock is
already high produces a source `gclk` edge no such predicate can represent.

### What is proved before a gate is folded

`bypass_enable_latches` now returns `Bypass_status::{Ok, Unsafe}`, and an
`Unsafe` result fails the complete plan. There is no latch-Q fallback at either
caller. For every enable that resolves (through the permitted width wrappers) to
a latch, the SAMPLING RELATIONSHIP is established first -- an ICG is glitch-free
only because its enable latch CLOSES exactly when the endpoint SAMPLES -- and
only then is the value read:

| the latch | result |
|---|---|
| window does not resolve to a root net, or is not a recognized clock | **refuse** -- it is data-gated and has no relationship to any clock edge |
| window is on a net other than the gate's own reference clock | **refuse** -- no fixed sampling relationship |
| window closes on the OPPOSITE edge from the endpoint's commit | **refuse** -- it is transparent ACROSS that edge |
| `din` is a raw D, a hold mux, or its own-window mux | bypass to the transparent value |
| anything else | **refuse** -- the value cannot be read, and guessing an arm is how the constant arm gets folded |

The window is asked of `latch_contract::commit_class_of`, the repo's ONE
classifier for "which net, which edge does this element commit on", rather than
a second resolver living in the pass: a latch COMMITS when its window deasserts,
so `cc.rising` IS its closing edge, with the `posclk` enable polarity and any
cone inversion already folded in. Every refusal names what it found -- the
window's root, the latch, both edges -- because a refusal that says only "some
other net" costs a debugging session.

### Both callers, and the refusal precedence

The helper is shared, so the result is shared: a memory timed port refuses with
`memory-gated-clock`, an ordinary gated endpoint with the new
`gated-clock-latch-unsupported`. Fixing only the memory would leave a design
free to normalize its gated FLOPS through the very latch Q its memory refuses --
one gate read two ways in one netlist, with only half the problem visible.

The endpoint refusal is DEFERRED behind the existing multi-clock guard, exactly
as the memory refusal already was. A latch windowed on a second clock net IS a
state element on that net, so such a design has two clock domains; it should be
told that, not handed a gate detail. Deferring costs nothing in safety --
`build_plan` mutates nothing and the refusal is raised before `normalize`
touches an edge -- and the element's cone is cleared on the spot so no later
code can fold what was not proved. Without the deferral, four `multi-clock-no-ratio`
refusals would have started reporting a gate detail instead; measured, then
fixed.

### The resolver had to learn one node

With the window check in place and nothing else changed, EVERY Verilog-read ICG
refused -- including this test suite's own end-to-end oracle. `resolve_phase`
stops at a reduce-OR, and both readers spell a boolean control built from a net
that way: `always @(clk or en) if (!clk) …` arrives as `Not(Ror(Get_mask(clk)))`.
So every such window resolved to a nameless internal node, `is_clock` said no,
and every clock gate classified as DATA-gated. (That is the same root cause as
the retyped-ICG-latch defect reported in the previous unit.)

`resolve_phase` now descends a reduce-OR **whose operand is one bit wide**,
which is an exact identity: `|x` is "any bit of x is set", which equals `x`
precisely when `x` has one bit. A wider operand is a real computation and is not
followed; so is an operand whose pin carries no width attribute, because
"probably one bit" is not something a soundness rule may assume. The paired
`RorWindow` / `WideRorWindow` controls pin both halves, and removing the width
test makes the wide one accept.

### Measured

**Blast radius against the audited commit `ce5fdfc7a`**
(`coreet_gate_latch_failclosed_ab.tsv`; front end + cgen only, no certificate,
no Lean, no corpus run; emission was first checked deterministic):

    identical 86   both-refused 12   emission-changed 20   acceptance-changed 4
    refusal-code-changed 0

The twenty emission changes are the resolver fix: those blocks' ICG latches now
classify on their real clock, which changes the latch retype path. The four
acceptance changes are the fail-closed rule doing its job, and three of them
undo this unit's predecessor:

| block | now | why |
|---|---|---|
| `minion_frontend` | `coincident-commit-edge` | two ICG enable latches commit at the same edge and one reads the other combinationally |
| `minion_frontend_thread_buffer` | `coincident-commit-edge` | the same shape |
| `minion_dcache_top` | `gated-clock-latch-unsupported` | an enable latch closes on the RISING edge of `clk_i` while its endpoint commits on the FALLING edge |
| `minion_dcache_tensor_load` | `gated-clock-latch-unsupported` | the same shape -- and this block is in the historical accepted set |

`minion_dcache_tensor_load` is the one that matters most to state plainly: it
normalized before this correction and refuses now. Its gated flop commits on
the FALL while its enable latch closes on the RISE, so the enable that flop
actually sees is the value latched half a period EARLIER, not the one the
transparent-value fold would hand it. The fold cannot represent that, so it
declines. **Its recorded runtime row predates this correction.**

**Front-end classification over the 118 diagnosed blocks**
(`coreet_frontend_refusals.tsv`):

    blocks probed              118
    baseline REFUSALS (rc=7)   24
      now NORMALIZE (rc 0)     13
      refuse, SAME code         7   (every multi-clock-no-ratio, unchanged)
      refuse, NEW named code    4
    baseline non-refusals that now REFUSE   1   (minion_dcache_tensor_load)

So this unit **removes** the three front-end clearances its predecessor claimed
and one that predated both. That is the correction, not a side effect: those
designs' clock gates are not, today, provably safe for this lowering, and the
previous commit normalized them by folding a latch output it had not proved.

### Controls

`pass_single_edge_test.cpp` is now **44** direct-LGraph cases across both
callers. `UnreadableEnableLatchIsLeftAlone` is gone; in its place:

* a parameterised memory-port refusal over the unreadable-value, same-phase and
  data-window latches, each asserting the exact named reason AND a full
  before/after snapshot of the mask and every port's clock and enable driver;
* the SAME three on a new ordinary-gated-FLOP fixture with no memory in it at
  all, each asserting the endpoint's clock and enable did not move and the
  latch was not retyped;
* off-reference windows on both paths, each pinning that the PRIOR multi-clock
  guard fires and that nothing was mutated, so the precedence cannot drift into
  an acceptance;
* the positives kept and extended: RawD, HoldMux and the measured WindowMux on
  the memory port; RawD and the reduce-OR window on the ordinary endpoint; plus
  the per-port mask/read/qualifier controls, the materialized `Clock_cell`
  refusal and the whole mask-24 fixture, unchanged.

**Proved load-bearing by injection, each reverted:**

| injection | what fails |
|---|---|
| drop the window PHASE check | both same-phase controls, and the end-to-end `bufsame` leg |
| restore the latch-Q fallback | both unreadable-value controls |
| descend a reduce-OR of any width | the wide-reduce-OR control |

### The oracle

`single_edge_memory_gate_test.sh` keeps every leg -- the pinned original gated
RTL versus the normalized emission on BOTH readers, the five load-bearing
negatives, the slang drop-gate negative, and the "the raw round trip does not
even elaborate" assertion -- and gains the reviewer's same-phase probe as a
refusal leg, which must refuse with `memory-gated-clock` AND name the sampling
relationship it broke. `bufoff` now pins the prior multi-clock guard for the
same reason the direct controls do.

All twelve latch, clock, ICG and single-edge shell suites pass, including
`latch_contract_check_test`, `lec_latch_polarity_test` and
`lec_clock_blindness_test`, which are the ones the `resolve_phase` change could
have disturbed.

Historical runtime tables are byte-unchanged and no corpus was relaunched. The
three-block resource-limit pilot from the previous unit stands as recorded: it
has ZERO RAN rows and is not runtime coverage.

**TRUST BOUNDARY.** The direct graph checks, the finite original-source Icarus
differentials, the authenticated artifacts, the cgroup observations, the runtime
rows, the Lean execution and the gates are empirical translation validation, not
a generic formal proof of the C++ LGraph-to-DCERT1 transcription.

### Correction: the reduce-OR rule failed open on a multi-driver `Ror`

`Ror` is not a single-driver operator. `graph/cell.cpp` gives it ONE sink,
`as` at pid 0, and marks it MULTI-DRIVER; cgen, LEC and Lean all reduce every
incoming edge. The rule added above chose the lowest-pid edge and checked only
that operand's width -- and since every `Ror` input sits on pid 0, a legal
two-edge `Ror(clk, data)` with both operands one bit kept the first edge and
dropped the second, resolving the whole reduction to `clk`. In a latch window
that promotes a DATA-DEPENDENT condition to a clock-only transparency window,
which is the exact silent sampling-contract loss the fail-closed rule exists to
reject. The committed `WideRorWindow` control could not catch it: a single wide
operand exercises the width half of the rule, never the count. Nor could a
Verilog probe -- yosys folds a concatenation into ONE wide operand, so the
shape is only reachable by building the graph directly.

`resolve_phase` now descends a `Ror` only when it has **exactly one** incoming
edge whose operand is provably one bit through the permitted wrappers. Zero
edges, two or more edges, an unprovable width and a width above one all leave
the `Ror` as the root. The wrapper walk underneath was tightened the same way:
`Get_mask`/`Sext` carry their value on a single-driver pid 0, so a second driver
there stops the walk rather than being silently ignored.

**Controls, on both callers.** `MultiRorWindow` puts a two-operand
`Ror(clk, ce1)` -- the reference clock and an independent one-bit data net,
each separately connected to `as` -- in the enable latch's window, on the
memory-port fixture AND on the ordinary gated-endpoint fixture. Each asserts
the refusal happens before mutation, with the full before/after snapshot the
other negatives use, and each pins the reason by the WINDOW ROOT: the message
must name `ror`, not `clk` (which would mean the defect accepted) and not `ce1`
(which would mean it refused for the wrong reason). `MultiRorWindowSwapped`
connects the same two operands in the other order, so an implementation that
follows whichever edge the traversal returns first fails one of the pair
whichever order that turns out to be. The one-edge one-bit POSITIVE is kept and
extended to the memory path, and the single-wide-operand negative is kept.

**Proved load-bearing.** Replacing the count with "take the first edge" makes
both `MultiRorWindow` controls fail -- "the shape normalized anyway" on the
memory port and on the ordinary endpoint -- while `MultiRorWindowSwapped`
passes, which is precisely the order-dependence the pair exists to remove. The
injection was reverted.

### Correction: the evidence tables were bound to a binary that could not have produced them

The previous commit recorded `lhd_after_sha256` in both front-end tables by
hashing `bazel-bin/lhd/lhd`. That is not provenance. **`bazel-bin` is a
convenience symlink that follows the LAST-BUILT CONFIGURATION in the output
base**, and this machine runs concurrent bazel sessions against one output base
-- `bazel-out/k8-dbg/bin/lhd/lhd` and `bazel-out/k8-fastbuild/bin/lhd/lhd` both
exist here. Review found the recorded hash belonged to a binary that still
contained text this work had already removed and that failed the commit's own
positive oracle, so the hash authenticated nothing.

Both binaries are now identified by CONTENT and by BEHAVIOUR, not by a path:

| | baseline (ce5fdfc7a) | this correction |
|---|---|---|
| resolved via | `bazel cquery --output=files` + `bazel info execution_root` | the same |
| `transparent across the endpoint's own edge` | absent | present |
| `window rooted at` | absent | present |
| `is clocked through a gate cone carrying an enable that does not resolve to a pin` | present | absent |

Each is then copied to a fixed path, and every measurement runs against that
copy with its SHA-256 asserted BEFORE and AFTER the run -- the A/B driver
re-checks both binaries when it finishes, and the shell suites run through a
wrapper that does the same. The probe now records the full hash, the byte size
and the mtime of the exact file it used, not a truncated hash. A detached copy
cannot run the yosys reader (lhd finds its scripts relative to its own path), so
the source-level shell oracles run against the build artifact itself, with its
hash asserted around each run and checked byte-identical to the copy.

The current binary was also rebuilt from the restored tree and came out
byte-identical, so the build is reproducible from this commit. The `lhd_path`
line in each table names the fixed COPY a run used; that copy is scratch and is
deleted afterwards, so the authority is the full SHA-256 plus the content
markers above, and `pass/lean/tests/d4/coreet_frontend_ab.sh`,
`with_authenticated_lhd.sh` and `verify_frontend_tables.py` are committed so
both tables can be regenerated and re-checked from scratch.

The probe's own guard reports `PROBE FAILED` on this run, and that is correct:
it fails whenever a block that used to pass the front end refuses, and
`minion_dcache_tensor_load` now does. The regression is intended, named and
accounted for below; the guard is left strict rather than taught to tolerate
it.

### Coverage accounting

This correction makes `minion_dcache_tensor_load` refuse at the front end, and
that block is one of the 93 accepted rows of the authoritative R2 table. That
table stays exactly as it is -- it is a historical result and is byte-unchanged
-- but **one of its accepted rows is no longer current-valid**: the recorded
runtime row predates this correction and the block does not reach the emitter
today. With no new runtime success in this unit, the review's cumulative
focused CORE-ET evidence is **97/122, not 98/122**. CVA6 remains 30/30. The
three-block resource-limit pilot still has ZERO RAN rows and is not runtime
coverage.

### Correction: the evidence chain, not just its header

Review found the previous fix had repeated the original trust failure in a new
form. Both findings are reproduced here before being corrected.

**The committed B hash did not rebuild from the committed tree.** Resolving the
artifact from this tree gave `88c733e2…`, while the tables claimed
`eab3df54…`. So the sentence "the build is reproducible from this commit" was
not independently true.

**The marker table was impossible.** It claimed `gate cone carrying an enable
that does not resolve to a pin` was present in A and absent in B. That
diagnostic is at `pass/single_edge/pass_single_edge.cpp:871` in ce5fdfc7a,
f9d8cdeed and f2f377c5f alike. It cannot distinguish anything. Extracting every
string literal of both trees and differencing them shows why: **the correction
only ADDED diagnostics, so there is no A-only marker to point at.** The marker
set is now B-only -- `gated-clock-latch-unsupported`, `… enable latch closes on
the `, `… transparency window rooted at \`` -- which establishes that B contains
the correction and A does not, and claims nothing more.

**The producer and the verifier failed open.** The producer printed hashes to
stdout and left the table's header as hand-written prose; it globbed the corpus,
ignored cgen's exit status, accepted an empty emission, and never enforced the
canonical identity set. The verifier reported counts rather than requiring them
-- review fed it a fabricated one-row table with no binary metadata and it
exited 0.

#### What reproducibility actually is here

Measured three ways, not asserted:

| experiment | result |
|---|---|
| delete the artifact, relink, twice | same hash both times |
| delete one object file, recompile, twice | same object AND same binary both times |
| build B, build A from ce5fdfc7a (196 actions re-executed), rebuild B | B returns to the same hash |

So **within a fixed bazel configuration this build is byte-reproducible**. The
differing hashes seen across sessions came from the artifact PATH being shared
between configurations, not from a non-deterministic build -- which is the same
hazard as the original `bazel-bin` symlink, one level down. The configuration
is therefore part of the provenance now, and a path is never treated as an
identity.

#### The chain

`d4_evidence_provenance.sh` holds one provenance contract; both producers write
it INTO the table, atomically, and the verifier requires it. A table is written
to a temporary file and renamed only on success, so an aborted run publishes
nothing.

Each producer: resolves and hashes both binaries (regular files, never
symlinks), requires the source outside the evidence directory to be clean,
requires the baseline's own commit to be NAMED (`A_SOURCE_COMMIT`), **iterates
the canonical 122-name manifest** rather than globbing, and treats as fatal a
manifest name with no retained input, a duplicate name, a retained block the
manifest does not name, an `lhd` exit status outside {0,7}, a refusal with no
diagnostic code, a cgen exit status other than 0, and an acceptance whose cgen
produced no non-empty Verilog. Both hashes are re-asserted when the run ends.
The header carries the source commit and tree, the bazel version, the exact
configuration, the compiler, the manifest and its hash, the corpus root and a
corpus identity hash over every file read, both binaries' full hash, size and
mtime, the asserted marker results, the time span, the row count and the
totals.

`verify_frontend_tables.py` requires, rather than reports: the schema; every
provenance field, well formed (64-hex hashes, 40-hex git ids, positive byte
counts); the manifest the table names still hashing to what the table recorded;
the identity set being EXACTLY the manifest (for the classification table, rows
plus explicitly-declared skipped identities, so a skipped block cannot simply
disappear); exact row counts and field widths; allowed domains for every
column; an accepted side carrying a real emission hash and a refused side
carrying none; **every verdict recomputed from its own row**; and **every total
recomputed from the rows**.

#### Load-bearing negatives

`frontend_evidence_controls.sh` reproduces each way of getting this wrong and
requires every one to fail and to publish nothing:

| control | must |
|---|---|
| a one-row table naming a block outside the manifest | verifier rejects |
| provenance removed, from either table | verifier rejects |
| the manifest hash altered | verifier rejects |
| one identity dropped from the body | verifier rejects |
| a verdict that contradicts its own row | verifier rejects |
| totals that do not follow from the rows | verifier rejects |
| an accepted row with no emission hash | verifier rejects |
| a classification identity neither a row nor skipped | verifier rejects |
| a manifest naming an unretained block | producer aborts, publishes nothing |
| a manifest omitting a retained block | producer aborts, publishes nothing |
| A and B the same binary | producer aborts |
| a B binary without the discriminating markers | producer aborts |
| cgen exiting nonzero | producer aborts |
| an accepted block whose cgen emits no Verilog | producer aborts |
| a baseline binary with no named source commit | producer aborts |

The two emission controls use a wrapper that forwards `pass` to the real binary
and breaks only cgen, with the marker strings embedded so it reaches the step
under test -- which is itself a demonstration that the marker assertion runs.

**The new assertion caught a bug in itself on its first run.** `d4_marker_state`
used `strings … | grep -q`; under `pipefail` the `-q` exits on first match and
SIGPIPEs `strings`, so the pipeline reports failure and a marker that IS present
reads as absent. The producer refused to publish, naming the marker. It now uses
`grep -c`, and the dump is cached once per binary hash under the project.

#### What iterating the manifest immediately found

The first fail-closed run aborted on `minion_top`: `lhd pass single_edge` exited
**4**, "lg: input … holds no graphs". Its retained `lgdb_raw` contains **zero
files** -- and so do three others. Four of the 122 retained inputs are empty:

    minion_top   txfma_adder   txfma_top_fake   vpu_top

The previous, glob-based producer had recorded all four as ordinary
`BOTH-REFUSE-SAME` rows -- rc 4 on both sides, no codes, no emission hashes --
so four unusable inputs were counted as refusals. That is why the earlier table
said `both-refused 12`: **eight blocks genuinely refused the same way and four
were never measured at all.** The classification probe had been dropping the
same four silently, under a glob that simply never produced a row for them, so
its "118 blocks probed" was 118 of 122 with the other four unaccounted.

They are now a verdict of their own, `INPUT-UNUSABLE`, excluded from every other
total and DECLARED BY IDENTITY in both tables' headers, with the verifier
requiring that the declaration and the rows agree. Both sides must fail that way
for the same reason, or it is fatal after all. It is not made fatal outright
because the retained corpus is authenticated and repairing it is not this
script's business -- but it is no longer data, and it can no longer disappear.

#### The regenerated tables

Produced by the chain above from `A = ce5fdfc7a` (`b232afb6…`) and
`B = this tree` (`5d8c031f…`), both resolved by
`resolve_lhd_artifact.sh` and re-hashed when the runs ended:

    A/B over the canonical 122:
      identical 86   emission-changed 20   acceptance-changed 4
      both-refused 8   refusal-code-changed 0   input-unusable 4

    classification:
      118 rows + 0 skipped + 4 unusable = 122 manifest identities
      24 baseline refusals -> 13 normalize, 7 unchanged, 4 renamed
      1 prior non-refusal now refuses (minion_dcache_tensor_load)

`verify_frontend_tables.py` passes on both; `frontend_evidence_controls.sh`
passes all sixteen negatives. The five focused targets -- the 50-case direct
target and the memory-gate, latch-contract, latch-polarity and clock-blindness
shells -- were rerun against the authenticated artifact with its hash asserted
around each run.

The `A_path`/`B_path` lines name the fixed COPIES a run used; those copies are
scratch and are deleted afterwards, so the authority is the full SHA-256 plus
the asserted markers, and `resolve_lhd_artifact.sh` reproduces the same hash
from this tree.

#### Coverage, unchanged by this unit

Cumulative focused CORE-ET evidence remains **97/122**; CVA6 remains 30/30. The
R2 93/122 table is historical and byte-unchanged, with
`minion_dcache_tensor_load` no longer current-valid. The three-block pilot still
has zero RAN rows. What this unit changes is not the measurement but what can be
established about it -- and the discovery that four of the corpus's retained
inputs were never measurable in the first place.

### Correction: the verifier was still failing open

Review broke the previous verifier six ways. Each is reproduced below as a
control, because a checker that has never been shown to reject anything is a
checker nobody has tested.

| what it accepted | why it could |
|---|---|
| a `BOTH-REFUSE-SAME` row with `a_code=b_code="-"` | the refused-side code was enforced in the producer only |
| an all-zero `source_commit`, an unrelated `source_tree`, invented `corpus_identity` and `B_sha256`, non-numeric `rows`/`manifest_count`, `B_mtime` of `not-a-time` | numeric fields were guarded with `.isdigit()`, so a NON-number SKIPPED the check; hashes and commits were checked for SHAPE and never against anything |
| a corpus whose bytes had changed | `d4_corpus_identity` hashed relative paths and SIZES, so two fixtures of the same layout and different contents collided -- and the verifier never recomputed even that |
| an invented historical verdict, with the self-reported totals adjusted to match | the baseline columns come from each block's `logs/single_edge.log`, which lives outside `lgdb_raw` and so outside the corpus identity; nothing bound the row to the log |
| an `input_unusable` declaration naming a block whose row said otherwise | the declaration was checked one way only |
| -- | and `frontend_evidence_controls.sh` built `ab_badsha.tsv` and never ran it, so the wrong-binary negative did not exist |

#### What is required now

*Identities are recomputed, not trusted.* `d4_corpus_identity` hashes every
file's CONTENTS under each block's `lgdb_raw`, sorted by relative path, and the
verifier recomputes it against the named corpus -- 465 files and 100 MB, about a
second, so there was never a reason to approximate it. A new
`d4_baseline_identity` does the same for the retained `logs/single_edge.log`
files a classification table quotes.

*The baseline columns are re-derived, not just digested.* Hashing the logs stops
the logs being edited; it does not stop the TABLE being edited. So each row's
`baseline_refused` and `baseline_code` are recomputed from the log that row
names -- first `"code"`, second one when the first is `normalize-refused`, and
`"severity":"error"` for the refusal flag -- and must match.

*Nothing is skipped for being unparseable.* Counts must be canonical integers,
timestamps must parse, hashes must be 64 hex, git ids 40 hex, a duplicated
provenance key is a failure, every named commit must EXIST, and `source_tree`
must be `source_commit^{tree}`.

*The binaries' bytes are re-read.* Both sides are retained CONTENT-ADDRESSED
under `temp/d4_evidence_bin/<sha256>.lhd`, the table records that path, and the
verifier re-hashes the file and checks its size. **An absent artifact is
reported as absent and fails -- never labelled independently verified.**
`recheck_evidence_binaries.sh` is the other route: it rebuilds either side from
the commit the table names, with the configuration the table records, and
compares.

*The unusable declaration is checked both ways.* The declared set must EQUAL the
set of rows whose verdict is `INPUT-UNUSABLE`, and each declared block's
`lgdb_raw` must really hold no files.

#### Load-bearing negatives

`frontend_evidence_controls.sh` now runs twenty-eight, each exercising the real
producer or the real verifier and each required to exit nonzero and publish
nothing. The eight new ones are exactly review's probes: an altered B hash; a
`B_path` pointing at the other binary; a source commit that does not exist; a
`source_tree` that is not its commit's tree; a non-numeric count; an
unparseable timestamp; a duplicated provenance key; a refused row with no code;
an extra `input_unusable` declaration; a same-size corpus mutation; an edited
baseline log; and an invented historical verdict with the totals adjusted to
match. The corpus mutations run against a COPY, so the authenticated corpus is
never touched.

**Two of them failed on their first run, and both failures were mine.** The
refused-side code check used `CODE.match(c)` against `^[a-z0-9-]+$` -- which a
bare `-` MATCHES -- so the very row shape review reported was still being
accepted; it is now excluded by name, on both tables' code columns. And the
edited-baseline-log control read `NR==1` from the table body, which is the
HEADER row, so it had been reporting "could not find a baseline log to edit"
rather than testing anything. A control that has never rejected anything is not
a control, which is the whole reason these are run rather than described.

#### The regenerated tables

Produced by the corrected chain from `A = ce5fdfc7a` (`b232afb6…`) and
`B = 97a7e0397` (`5d8c031f…`), both retained content-addressed and re-hashed by
the verifier:

    A/B over the canonical 122:
      identical 86   emission-changed 20   acceptance-changed 4
      both-refused 8   refusal-code-changed 0   input-unusable 4

    classification:
      118 rows + 0 skipped + 4 unusable = 122 manifest identities
      24 baseline refusals -> 13 normalize, 7 unchanged, 4 renamed
      1 prior non-refusal now refuses (minion_dcache_tensor_load)

Both verify; all twenty-eight controls pass; the five focused targets were rerun
against the authenticated artifact with its hash asserted around each run.
Coverage is unchanged by this unit: 97/122 current-focused CORE-ET, 30/30 CVA6,
`minion_dcache_tensor_load` no longer current-valid in the historical R2 table,
and the three-block pilot still has zero RAN rows. All historical runtime tables
are byte-unchanged.

### Correction: a hash is not a provenance

Review's counterexample is the whole finding in one object. A **419-byte shell
wrapper**, carrying the expected marker strings and forwarding every command to
the *other* binary, was handed to the producer as B. It passed `d4_bin_check`
(it is a regular executable file). It passed every marker assertion (the strings
were in it). Every row it produced was real -- the real binary produced them.
The producer exited 0, retained the WRAPPER's hash, wrote
`B_source_commit = 45371d872`, and the verifier accepted the table.

Everything the chain had learned to check was about what the bytes ARE. Nothing
was about where they came from. `d4_provenance_head` simply copied `HEAD` into
the header for whatever executable it was handed, and `A_SOURCE_COMMIT` for the
other side: attribution by fiat.

#### The binding

`d4_build_binding.sh build <commit> <out> [config]` checks the **complete tree**
of an exact commit out into a throwaway worktree under `temp/d4_build/`, asserts
that worktree's `HEAD^{tree}` really is that commit's tree and that it is clean,
builds `//lhd:lhd` there **with its own output base**, resolves the artifact
with `cquery` + `info execution_root` *in that worktree*, retains it
content-addressed, and writes an attestation:

    # schema   = d4-binary-binding/1
    commit     = <40 hex>          tree     = <commit^{tree}>
    config     = <exact bazel flags>
    bazel      = ...               compiler = ...
    sha256     = <the binary>      bytes    = ...
    retained   = <content-addressed path>

Nothing is built from a hand-picked subset of directories, and **the active D4
worktree is never modified** -- which is why `recheck_evidence_binaries.sh` is
deleted rather than fixed: it checked `graph pass core inou lhd ware` out into
the live tree, so its result could mix a named commit with current-HEAD sources
everywhere else. `d4_build_binding.sh check <attestation>` is the replacement
and rebuilds the whole tree. The isolated output base is torn down afterwards;
a disk cache is shared between commits, which is keyed by action and so cannot
substitute one commit's output for another's.

#### What now has to hold before anything is published

`d4_require_binding` runs in the producer, before a single row is written: the
attestation must exist, be a `d4-binary-binding/1`, and name **this side's
commit**, **this run's configuration**, and **these exact bytes and size**. A
binary with no binding, or with a binding for another commit, or whose hash the
binding does not attest, aborts the run and publishes nothing.

The verifier requires the same thing independently: the binding file must be
present, hash to what the table records, and agree with the table on the
commit, the configuration, the binary hash, the byte count, and the commit's
own tree. An absent or mismatched binding is a failure, not a note.

#### Load-bearing negatives

Five more, all exercising the real producer or verifier:

| control | must |
|---|---|
| review's marker-bearing wrapper forwarding to the other binary | producer aborts, publishes nothing |
| a real binary paired with an existing but unrelated commit | producer aborts |
| a binary with no binding at all | producer aborts |
| a table whose B binding is the A side's | verifier rejects |
| a table naming a binding file that is not there | verifier rejects |

The wrapper's marker lines are written through a QUOTED heredoc and then
checked, because one needle ends in a backtick: an interpreted heredoc mangles
it, and the control would then abort on a missing marker instead of on the
missing binding it exists to test.

**The binding script fail-opened on its own first use, and the attestation it
wrote is the evidence.** `build_one` runs in a process substitution, so a
`d4_die` inside it exits only that subshell: `read` then sees EOF and leaves the
hash, the byte count and the retained path EMPTY -- and the script wrote the
attestation anyway. Interrupting a build produced a `d4-binary-binding/1` file
with `sha256 =` and `bytes =` blank. Every field an attestation asserts is now
checked before anything is written: a 64-hex hash, a positive byte count, a
retained file that exists and hashes to that value. A failed or interrupted
build publishes nothing.

#### The attested binaries

Neither side's hash matches what the previous tables recorded, and that is the
point: those binaries were built inside the active worktree, these are built
from the complete tree of their own commit in an isolated one, and the exec-root
path is part of a debug build's bytes.

| side | commit | sha256 | bytes |
|---|---|---|---|
| A | `ce5fdfc7a` | `aa67170d01ab0445…` | 1263107488 |
| B | `45371d872` | `65424883d2a8ea65…` | 1263118400 |

Both tables were regenerated against exactly those two binaries, each bound to
its commit by an attestation the producer checked before writing a row and the
verifier re-checks afterwards.

#### Where the artifacts live, and what that means

Both retained binaries and both attestations live under `temp/`, which is
scratch by design. That is deliberate rather than careless: if they are cleaned
away, the verifier says so and **fails** -- `B_path … is not present, so
B_sha256 is NOT verified`, `B_binding … is not present, so B is NOT bound to a
commit` -- instead of quietly passing on a hash nobody can check.
`d4_build_binding.sh build` regenerates either side from its named commit, and
`d4_build_binding.sh check` rebuilds and compares.

The five focused targets are a separate matter from the tables. They run the
yosys reader, which needs its runfiles, so they cannot run against a detached
copy; they use the ACTIVE worktree's artifact (`5d8c031f…`), with its hash
asserted around each run. That is the same committed source as attested B --
the exec-root path is what makes a debug build's bytes differ -- and it is said
here rather than left for a reader to work out.

    50 direct cases                     PASS
    single_edge_memory_gate_test        PASS
    latch_contract_check_test           PASS
    lec_latch_polarity_test             PASS
    lec_clock_blindness_test            PASS
    34 evidence controls                PASS

Coverage is unchanged by this unit: 97/122 current-focused CORE-ET, 30/30 CVA6,
`minion_dcache_tensor_load` no longer current-valid in the historical R2 table,
the three-block pilot still with zero RAN rows. All historical runtime tables
are byte-unchanged.


### Correction: the binding was still a self-authored assertion

Review mounted the forwarding-wrapper attack one layer deeper, with the
committed entrypoints. A 166-byte wrapper forwards to the genuine binary. A
HAND-WRITTEN `d4-binary-binding/1` names the expected commit, that commit's real
tree, the recorded configuration, and the wrapper's own hash and size. The real
producer accepted the wrapper as a build of the current commit and published a
row; substituted into the genuine table, the real verifier printed
`BOTH TABLES VERIFY`.

Nothing in that is surprising once stated plainly: **an attestation is an
unsigned editable text file**, and the previous chain only checked that its
claims agreed with the table's claims. Two self-asserted objects agreeing is not
authentication. `d4_build_binding.sh check` -- the one step that would have
settled it -- sat outside both acceptance paths.

#### The channel is closed, not guarded

The producers no longer take binaries. **They take COMMITS and build each side
themselves**, from the complete tree, in an isolated worktree with its own
output base, and measure with exactly what came out:

    coreet_frontend_ab.sh <A-commit> <B-commit> <out.tsv>
    COMMIT=<commit> coreet_frontend_refusal_probe.sh <out.tsv>

There is no caller-supplied executable to substitute and no caller-supplied
attestation to forge, because neither is an input any more. A path to an
executable where a commit is expected is simply not a commit, and never reaches
a build.

Two guards in the producer -- cgen exiting nonzero, and an acceptance whose cgen
emitted no Verilog -- are consequently no longer reachable from outside, since
the only binaries it ever runs are ones it built. They stay as defence in depth,
and the observable negative moved to the verifier, where a row claiming an
acceptance with no emission hash is rejected.

#### Verification rebuilds, and cannot report success without it

`verify_frontend_tables.py` now runs `d4_build_binding.sh check` for every
binding a table names, as part of its result. That check validates an EXACT
schema first -- canonical commit and tree, the tree really being that commit's,
the exact configuration, a 64-hex hash, a positive byte count, a retained
artifact that exists and hashes to the claim, and no missing, duplicated or
unknown claim -- and then rebuilds the named commit's complete tree and compares
the hash AND the size.

A rebuild is the whole 6997-action graph in a throwaway worktree with its own
output base -- not an incremental check against whatever the session last
built. It is nonetheless cheap, because the output base is fresh but the DISK
CACHE is shared: the last three ran in 25 s, 29 s and 16 s with `4845 disk cache
hit, 2152 internal` each. That sharing cannot launder one commit's bytes into
another's build, since the cache is keyed by action, and an action's key covers
its inputs.

`--skip-rebuild` exists, and it exits **2** with
`TABLES VERIFY -- but the BINARIES ARE NOT AUTHENTICATED`. Two distinct non-zero
outcomes matter: every table-shape negative in the control suite now requires
exit **exactly 1**, so a skipped rebuild can never stand in for a rejection, and
the suite's single positive runs the whole path including the rebuild.

#### The negative that was missing

Review's exact attack is now a control: a forwarding wrapper plus a hand-written
binding naming the right commit, the right tree, the right configuration and the
wrapper's own hash and size, substituted into the real table. The control first
asserts that the forgery is **internally consistent** -- it must pass the shape
checks under `--skip-rebuild`, or the control would be proving that a sloppy
forgery is caught rather than that the rebuild catches a careful one -- and then
requires the full verifier to exit 1.

#### Two defects the new machinery found in itself

**The binding script fail-opened on its first use.** `build_one` runs in a
process substitution, so a `d4_die` inside it exits only that subshell: `read`
sees EOF, leaves the hash, byte count and retained path EMPTY, and the
attestation was written anyway. Interrupting a build produced a
`d4-binary-binding/1` file with `sha256 =` and `bytes =` blank. Every asserted
field is now checked before anything is written, and a failed or interrupted
build publishes nothing.

**The verifier rejected the first regenerated pair, correctly.** A binding
carried `built_at`, so when the classification probe rebuilt B -- after the A/B
table had already recorded `B_binding_sha256` -- the file's bytes changed and
the recorded hash no longer matched. A binding is now a pure function of
(commit, configuration, build result): two producers naming the same commit
produce byte-identical files, so the order they run in cannot matter. Build time
is still visible through the retained artifact's mtime and each table's
`started`/`finished`.

#### The regenerated tables

Both sides built by the producers themselves, from their own complete trees:

| side | commit | sha256 |
|---|---|---|
| A | `ce5fdfc7a` | `aa67170d01ab0445…` |
| B | `e918d0127` | `65424883d2a8ea65…` |

`aa67170d…` is the same hash the previous unit's isolated build of `ce5fdfc7a`
produced, from a separate worktree and a separate output base -- an independent
confirmation that the build is reproducible within a fixed configuration.
`65424883…` is the same binary as the `45371d872` build, which is what should
happen: `e918d0127` changed only `pass/lean/tests/d4/`, which nothing compiles.

The measured rows are unchanged: A/B identical 86, emission-changed 20,
acceptance-changed 4, both-refused 8, refusal-code-changed 0, input-unusable 4;
classification 118 rows + 0 skipped + 4 unusable = 122, with 13 of 24 baseline
refusals normalizing, 7 unchanged multi-clock refusals, 4 renamed and 1 prior
non-refusal now refusing.

    pass_single_edge_test               PASS (50 tests)
    single_edge_memory_gate_test        PASS
    latch_contract_check_test           PASS
    lec_latch_polarity_test             PASS
    lec_clock_blindness_test            PASS
    29 evidence controls                PASS (4 exact-tree rebuilds)
    verify_frontend_tables.py           BOTH TABLES VERIFY, and both
                                        binaries rebuild from their
                                        named commits

Coverage is unchanged by this unit: 97/122 current-focused CORE-ET, 30/30 CVA6,
`minion_dcache_tensor_load` no longer current-valid in the historical R2 table,
the three-block pilot still with zero RAN rows. All historical runtime tables
are byte-unchanged.

### The opposite-phase held enable: `minion_dcache_tensor_load`

#### 1. The shape, pinned before anything was changed

`minion_dcache_tensor_load` and `minion_dcache_top` were the unit's two
`gated-clock-latch-unsupported` refusals. Both are one shape, two levels apart
(`minion_dcache_top` reaches it through `gen_offload.u_tensor_load_0`), and it
comes from two tech-generic primitives:

    prim_clk_gate.sv:33   always @(clk_i or en_i or dft_i.scanmode)
                            if (!clk_i) en_latch <= en_i | dft_i.scanmode;
    prim_clk_gate.sv:40   assign clk_o = clk_i & en_latch;

    prim_rf_...preview.sv:28  always_latch if (!preview_clk_i)
                                wr_data_en_1p_q <= wr_data_en_1p_next_i;
    prim_rf_...preview.sv:36  always_ff @(negedge rf_clk_i)
                                if (wr_data_en_1p_q) wr_data_del_q <= wr_data_i;
    prim_rf_...preview.sv:42  always_ff @(posedge rf_clk_i)
                                if (wr_en_i) rf_q <= wr_data_del_q;

`minion_dcache_tensor_load.sv:256` instantiates the gate as `clkgt`, and
lines 586-587 wire BOTH `preview_clk_i` and `rf_clk_i` to it.

Read off the retained `lgdb_raw` (11505 nodes; 109 `Flop`, exactly 2 `Latch`):

| | |
|---|---|
| reference net | `clk_i`, one clock domain |
| `latch_5116` | `u_clk_gate.en_latch`; window `get_mask_48428 <- eq_15744 <- not_7192 <- ror_29708 <- clk_i` -- transparent while `clk_i` is LOW, so it **closes on the RISE** |
| `and_5236` | `clkgt`, the multi-driver `And` of `clk_i` and `latch_5116`'s Q: the recognized `<clock> & <enables>` cone |
| `flop_13208` | `gen_tmp_data_rf.gen_tmp_data_blk[0].u_tmp_data.wr_data_del_q`; `clock_pin <- and_5236.clkgt`, `din <- mux_16396`, NO `enable` pin, `posclk` const-false -- it **commits on the FALL**. Sixteen of them |
| `latch_7124` | `wr_data_en_1p_q`, deduped across the 16 blocks; window `!clkgt`, so it also closes on the RISE |

So the enable latch closes on the gate's own active edge and the endpoint
commits on the **following** edge, one slot later. The endpoint's enable is
that latch's HELD Q.

#### 2. The defect, measured on the A side

`ce5fdfc7a` ACCEPTED this block, and what it produced is the reason the unit
exists. Its emission, for every one of the sixteen endpoints:

    always @(posedge clk_i) if ((eq_60240 & \u_clk_gate.en_i ))   // phase==1
      wr_data_del_q <= ...;

`\u_clk_gate.en_i` is the latch's TRANSPARENT arm -- the live `en_i | scanmode`
cone -- read at slot 1, half a period after the latch that gates it closed. The
source cannot see that value through a closed latch. The same binary also left
the gate's own enable latch as `if ((!clk_i) & phase==0)`, which at a posedge is
`0 & ...`: the retyped flop never commits at all. (`ce5fdfc7a` has no `Ror`
branch in `resolve_phase` and a fail-open `bypass_enable_latches`, so the latch
classified as DATA-gated and kept its window. `e918d0127` resolves it and
refuses instead -- which is why the block reads REGRESSED.)

The correct emission, and what the pass now produces:

    always @(posedge clk_i) if (eq_60236)                          // phase==0
      \u_clk_gate.en_latch  <= \u_clk_gate.en_i ;
    always @(posedge clk_i) if ((eq_60240 & get_mask_25716_u))     // phase==1
      wr_data_del_q <= ...;          //  get_mask_25716_u = {1'b0, en_latch}

#### 3. The rule: the gate's ACTIVE edge, not "the edges differ"

The previous rule required the latch to close on the edge the endpoint commits
on, and refused everything else. That conflates two different shapes, and it is
worth being exact about why, because the fix is one predicate:

A cone `clk & en` is PINNED INACTIVE for the whole phase its clock operand is
low. The enable may move all it likes there and the output cannot follow. That
phase, and only that phase, is where an enable latch may be transparent, so its
closing edge must be the gate's ACTIVE edge -- the RISE for `clk & en`, the FALL
for `~clk & en`. A latch transparent while the gate is LIVE moves the gate's own
output and is refused, whichever edge the endpoint commits on.

Once that holds, the endpoint's own edge picks the lowering:

| endpoint commits | the latch is | the enable becomes |
|---|---|---|
| on the gate's ACTIVE edge | closing at that instant | its TRANSPARENT arm (US7624363 L1->buffer) |
| on the gate's OTHER edge | opaque for the whole phase | its HELD Q, read one slot later |

`Icg_cone` gained `gate_active_rising` for this. It is NOT `!clock_inverted`:
`clock_inverted` folds in every inversion between the `And` and the pin that was
resolved, because what a flop's `clock_pin` needs to know is which edge the
element commits on. A latch's window pin is routinely `~(clk & en)`, which makes
`clock_inverted` true while the gate underneath is an ordinary `clk & en`.
`gate_active_rising` asks only about the `And`.

#### 4. What the held case needed besides the rule

Three things the same-edge fold never exercised, each found by running the real
block and each a refusal or a silent wrong answer before it was fixed:

**Rule 4 was asking about a cone the lowering removes.** `check_rule4` read the
raw `din`, and a clock-role latch's hold-mux SELECTOR is the gate -- which the
retype absorbs into the slot. With both enable latches now resolving to
`clk_i`'s rise, `latch_5116` and `latch_7124` shared a commit class and the only
path between them was that selector, so the block refused with
`coincident-commit-edge`. Rule 4 now asks `latch_transparent_value`, the same
definition the bypass and the rewrite use, so it cannot clear a cone the rewrite
keeps or reject one it drops.

**The retype kept a window-gated mux on `din`.** It replaced `din` only for a
HOLD mux (`window ? d : q`). Slang and yosys both spell this latch as
`din = Mux(window, <nothing>, d)` -- no hold arm at all -- so `has_hold_mux` was
false, the mux survived, and the retyped flop sampled the window's FALSE arm at
the very edge it commits on. It now installs `latch_transparent_value` for every
mux shape.

**An undriven mux arm is not a reason to refuse.** `latch_transparent_value`
required both arms. The cell samples `din` only WHEN ITS WINDOW IS TRUE, so the
arm the window never selects is unreachable by the latch's own semantics; only
the hold-mux case, which has to tell which arm is the Q, needs both.

#### 5. The predecessor proof

`bypass_enable_latches` runs during element collection, before the reference
clock is chosen and before any slot exists, so it RECORDS each held pair and
`build_plan` discharges the proof once P and every slot are final -- still
before `normalize` has rewritten anything. The latch must be planned, be a
latch, be clock-role, be on the reference clock, be scheduled in
`slot_of_edge(its own closing edge)`, and that slot must be the immediate
predecessor of the endpoint's in a P=2 period.

Stated plainly: at P=2 every one of those arms is a TOTAL GUARD, not a live
branch. The latch's slot and the recorded closing edge both come from
`commit_class_of` on the same node, so they cannot disagree; a held pair puts
one of its two members on the falling edge, so P is 2 by construction; and the
role/net arms are already refused earlier with their own diagnostics. They are
here for the same reason the `/N` divider guard is -- `slot_of_edge` and
`is_immediate_successor` are now the ONE place the slot rule is written down, and
if either ever moves, this is what fails instead of a corpus block.

#### 6. Controls, and the injection that proves them load-bearing

`build_gated_flop` is parameterised by a `Gate_order` (which edge the gate goes
active on; whether the endpoint commits on that edge or the one a half period
later), so the same vocabulary now builds all four combinations. Twelve new
direct-LGraph cases, 62 in the file:

* **2 positives**, one per polarity. Each asserts, off the normalized graph:
  the latch is a `Flop` committing in its own slot, holding the value its window
  passed through; the endpoint commits in the slot IMMEDIATELY after, on the
  latch's held Q and NOT on its transparent arm; neither carries the other's
  slot predicate; and no `Latch` cell survives.
* **9 paired negatives**: same-phase on both polarities, data window on both,
  the two multi-driver `Ror` orderings, the wide `Ror`, and an unreadable
  transparent arm on both -- each refusing with nothing mutated.
* **1** off-reference case, pinning that it still lands on the PRIOR multi-clock
  guard rather than a gate detail.

Reverting one thing at a time, rebuilding, and re-running only the held cases:

| injection | fails |
|---|---|
| no held branch (the pre-unit rule) | both positives, both unreadable-arm negatives |
| gate check keyed on "the edges differ" | both same-phase negatives -- the glitching gate is accepted |
| held endpoint reads the transparent D | both positives |
| retype keeps the window-gated mux on `din` | both positives |

Each injection fails a different, specific subset; none fails nothing.

#### 7. The semantic oracle

`lhd/tests/single_edge_held_enable_test.sh` is the original-source-versus-
normalized argument. The source is the measured shape reduced to its two
primitives, with the SAME-EDGE endpoint (`q`) in the same module as the HELD one
(`del_q`), so a lowering that confuses the two has both in front of it at once.

The stimulus is what makes it an oracle rather than a shape check: `ce` and
`scan` move at **37+20p** -- strictly inside the source's high phase (rise
30+20p, fall 40+20p) and strictly after the normalized slot-0 edge (35+20p) that
samples them. The source's enable latch is opaque across that entire window, so
the commit at the fall must use the value from before the rise; a lowering that
read the transparent arm at slot 1 would use the new one.

A round-trip-at-1x leg runs first: the UNNORMALIZED design emitted straight back
from its own LGraph must match its own source, so a mismatch on the normalized
side is attributable to the lowering and not to the reader, the emitter or the
harness. Then 39 periods, four-state, on both readers. Six negative controls:

    substituting the transparent D for the held Q      FAILS (errs=42)
    reversing the slot ordering                        FAILS (errs=36)
    dropping either term of the endpoint's predicate   FAILS (errs=24, 24)
    moving the enable latch out of its own slot        FAILS (errs=27)
    the normalized side forced to a 1x time base       FAILS
    dropping the held enable, slang path               FAILS (errs=24)

A NAMED GAP, measured rather than assumed. The slang leg reads the ICG and its
held endpoint ALONE. Slang spells the INNER latch's window (`!gclk`) as
`Mux(Eq(And(clk, en_latch), 0), 0, 1)`, which `resolve_phase` does not resolve to
a clock root; that latch then classifies as DATA-gated, keeps its window as an
enable, and -- since the window reads the clock as data and one normalized tick
is one sub-step -- never commits. That is a PRE-EXISTING classification gap and
not this lowering's: the corpus blocks arrive with the same window in the
`Not(Ror(...))` spelling, which does resolve, so `wr_data_en_1p_q` is clock-role
there and its window IS absorbed -- visible in the emission above. It is left
alone deliberately; widening `resolve_phase` would re-classify blocks far
outside this unit.

    pass_single_edge_test                 PASS (62 tests, 12 new)
    latch_contract_test                   PASS
    single_edge_held_enable_test          PASS (new)
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS
    //graph/... //pass/...                67/68 PASS

`//pass/lec:lec_stats_test` is the one failure, and it fails identically on
`d79bd1c85` with every file in this unit reverted: the cvc5 plugin's histograms
do not survive the fork. Pre-existing, unrelated, untouched.

One string moved: the same-phase refusal now reads `transparent while the gate
is LIVE` rather than `transparent across the endpoint's own edge`, because the
refusal is now about the GATE and not about the endpoint. The two needles in
`pass_single_edge_test.cpp` and the one in `single_edge_memory_gate_test.sh`
follow it; what those controls assert is unchanged.

#### 8. The regenerated front-end evidence

Both sides built by the producers from their own complete trees, and this is the
first pair where the producer's own schema check actually ran (see the commit
that moved `d4_binding_validate` into the shared library):

| side | commit | sha256 | bytes |
|---|---|---|---|
| A | `ce5fdfc7a` | `aa67170d01ab0445…` | 1263107488 |
| B | `391b1eb90` | `7f8f4ee3348c0de9…` | 1263347744 |

    A/B             identical 86   emission-changed 22   acceptance-changed 2
                    both-refused 8   refusal-code-changed 0   input-unusable 4
    classification  118 rows + 0 skipped + 4 unusable = 122
                    24 baseline refusals -> 14 cleared, 7 unchanged, 3 renamed
                    0 regressed, 94 still-ok          PROBE OK

The probe's own strict guard -- which fails whenever a block that used to pass
the front end refuses -- passes for the first time since the Ror correction:
`minion_dcache_tensor_load` is STILL-OK again, and `minion_dcache_top` moves
from RENAMED to CLEARED. The two remaining ACCEPTANCE-CHANGED rows are
`minion_frontend` and `minion_frontend_thread_buffer` on `coincident-commit-edge`,
which this unit was told not to tackle.

Against the committed tables exactly **22 rows differ**, and every one was read
off the netlist:

* **2 are the acceptance recoveries.** `minion_dcache_tensor_load`
  (`gated-clock-latch-unsupported` -> `single-edge-applied`, emission
  `7a86e4219dcc2478`) and `minion_dcache_top` (-> `7ec51f9c44cacd00`).
* **20 are emission-only**, and all twenty are the SAME change. On
  `minion_dcache_reduce` the whole diff is eight lines: the retyped ICG latch's
  `din` moves from `mux_4252` -- `(!clk_i) ? en_i : 0`, which at a `posedge
  clk_i` is identically 0 -- to `\u_clk_gate.en_i `. Those blocks are at P=1, so
  the flop has no slot predicate and simply latched 0 forever.

That is **the separate, unfixed defect this README reported and did not patch**
when the same-edge fold landed: "the ICG latch is itself retyped into a Flop
that keeps its `~clk` transparency cone as an enable, so in the emitted Verilog
it never commits at a `posedge clk`". It is fixed here as a consequence of the
retype installing the transparent value for every mux shape. It remains dead
logic in those twenty blocks -- `clkgt` has no consumer once the gate is folded
-- so what changed is a dead register's value, not an observable one.

#### 9. The focused runtime certificate: what actually blocks it

Acceptance criterion 7 expected `minion_dcache_tensor_load` to run again and
carry focused CORE-ET coverage from 97/122 to 98/122. **It does not, and the
blocker is not this lowering.** One block, same limits every earlier pilot used
-- 8192 KB stack, 3600 s / 1800 s timeouts, 8 GiB emission ceiling, jobs 1;
nothing was raised to make it fit:

    minion_dcache_tensor_load   EMIT_FAILED   compile exit=7
      pass.latch: module `minion_dcache_tensor_load` violates the latch contract
      (rule C, simultaneously-transparent pair): latches `latch_5116` and
      `latch_7124` are transparent at the SAME time and there is a combinational
      path between them

It fails in **stage 1**, `RTL -> LGraph`, before `pass.single_edge` is reached at
all. The cgroup record settles what kind of failure it is: `verdict=OK`,
`oom_kill=0`, `oom_group_kill=0`, `memory_peak_bytes=105787392` (101 MB against
an 8 GiB ceiling), `emit_rc=7`, `cleanup=clean`. This is a semantic refusal, not
the resource limit that blocks `minion_dcache_top`. The gates driver is recorded
saying exactly that rather than being skipped -- `candidate RAN rows 0`,
`GATES FAILED: no candidate RAN rows at all` -- and **no runtime row was
invented for a block that did not emit**.

Two measurements place the blocker:

* **It predates this unit.** With `pass_single_edge.cpp`, `latch_contract.cpp`
  and `latch_contract.hpp` reverted to `d79bd1c85` and rebuilt, the same compile
  fails the same way, `exit=7`, `rule C`.
* **It was introduced by the ACCEPTED `Ror` correction.** Disabling just the
  `Ror` branch of `resolve_phase` and rebuilding, the same compile exits **0**.
  Rule C skips pairs whose cones do not resolve ("an UNRESOLVED cone is never
  claimed to be simultaneous"), and before that correction neither window
  resolved. Making them resolve is what let the front end see the real shape --
  and it is also what made rule C report this pair.

And the pair it reports is the same over-measurement `check_rule4` had. Rule C
walks `comb_reach` from a latch's raw `din`; `latch_7124`'s `din` is its
window-selected mux, so the walk leaves through the SELECTOR -- which is the
gate, not a data path. `latch_7124`'s captured value is `wr_data_en_1p_next_i`
and does not depend on `latch_5116` at all; what depends on it is WHEN
`latch_7124` closes, which `commit_class_of` already models and which rule C
then checks as a phase. Nothing borrows time across the pair.

**Not fixed here, deliberately.** `latch_contract::check` gates every `compile`
in the repository, and narrowing what it counts as a combinational path can only
make it reject less. Doing that responsibly needs a corpus-wide emission
re-measurement, and a 122-block rerun is explicitly outside this unit. The fix
shape is the one already applied one layer up: rules B and C should ask about
the value the lowering keeps rather than the raw `din`, so a clock-role latch's
window selector stops counting as a path between two latches.

So focused CORE-ET evidence stands at **97/122**, unchanged by this unit; the
historical R2 table keeps `minion_dcache_tensor_load` as an accepted row that is
still not current-valid. CVA6 remains 30/30. The three-block resource-limit
pilot still has zero RAN rows.

    coreet_tensor_load_rows.tsv              1 row, EMIT_FAILED, 18 fields
    coreet_tensor_load_provenance.txt        reconciled=YES, same limits as R2
    coreet_tensor_load_cgroup_records.tsv    1 complete record, OK, no kill
    coreet_tensor_load_gates.tsv             0 candidates, GATES FAILED

All historical runtime tables -- `coreet_122_fresh_*`, `coreet_122_r2_*`,
`coreet_122_runtime_*`, `coreet_122_sext_replay*`, `coreet_type1_*`,
`coreet_srconly_*`, `coreet_gate_pilot_*`, every `cva6_*` -- are byte-unchanged,
and no corpus sweep was relaunched.

Trust boundary unchanged: exact-tree rebuild authentication and these finite
front-end rows remain empirical translation validation, not a generic formal
proof of the C++ LGraph-to-DCERT1 transcription.

### Correction: "dead logic" was wrong, and a memory kept a derived clock

#### 1. The claim, and what was actually true

The previous unit reported all twenty emission-only rows as a change to a dead
register: "in those twenty blocks `clkgt` has no consumer once the gate is
folded, so what changed is a dead register's value, not an observable one."

That was arrived at by following one net one hop. On `minion_dcache_reduce`
`u_clk_gate.en_latch` feeds `get_mask_8980_u`, which feeds `clkgt` -- and
`clkgt` feeds `.wr_clock_0` on four `cgen_memory_multiclock_1rd_1wr` instances.
Review found it; the hop that was never taken is the one that mattered.

So the question is computed now. `pass/lean/tests/d4/emission_fanout.py` reads a
cgen netlist and reports, for a seed register, whether its transitive fanout
reaches a module OUTPUT, another register's next-state value or commit
condition (STATE), or a memory instance's clock, data or control port. **Its one
design rule is that a DEAD verdict cannot come from a parse miss**: every line
must be classified, and anything it cannot read poisons the module as
`UNPARSED` rather than contributing nothing to a fanout set. That rule paid for
itself twice while it was being written -- `minion_dcache_replay_queue` has the
memory wrapper's own module definition inlined into the same file, and
`txfma_top` reaches a single-clock wrapper's `.clk` rather than a
`.wr_clock_0` -- and both first appeared as a refusal to answer, not as DEAD.

Measured over the twenty rows at the audited commit:

| fanout | rows |
|---|---|
| STATE | 9 |
| DEAD, OUTPUT, STATE | 3 |
| DEAD | 3 |
| DEAD, MEM-CLOCK | 2 (`txfma_top`, `txfmafrac_top`) |
| MEM-CLOCK | 1 (`minion_dcache_reduce`) |
| DEAD, STATE | 2 |

**Seventeen of twenty are live**, and three of those reach a memory CLOCK. Only
`minion_dcache_cache_op_unit`, `..._l2` and `vpu_trans` were dead.

#### 2. Why a memory kept the gate as a clock

The pass folds every ordinary gated FLOP's clock into an enable and rebinds the
flop to the reference clock. Memories got that treatment only under a phase
divider: the whole memory rewrite sat inside `if (plan.slots > 1)`, on the
reasoning that at P=1 there is no slot to commit at and a memory is simply not
this pass's business.

At P=1 that leaves the SAME GATE as an enable on the flops and a CLOCK on the
memories. `minion_dcache_reduce` is P=1: all 58 of its gated flops moved to
`posedge clk_i` + `if (en_i)`, and four write ports stayed on `.wr_clock_0(clkgt)`
with `clkgt` the only net in the design still carrying the gate.

Nothing in the front-end table could see this -- it compares emission hashes --
and a strict RTL-versus-normalized LEC does not settle it either: review's
attempt exits inconclusive because the REFERENCE contains derived memory clocks
the encoder cannot model, naming `u_ba_alloc_fifo.mem`. An inconclusive LEC is a
refusal, not a validation, and is not counted as one here.

#### 3. What changed

At P=1 a memory is this pass's business exactly when it would otherwise keep a
derived clock, and then it gets the same two moves a flop gets:

* each representable write gate is folded into that port's write ENABLE -- a Mux
  between the full lane mask and zero, selected on the gate, so the mask's width
  survives;
* every timed port's `clock_pin` is rebound to the reference clock.

A cell already on the reference clock is untouched, exactly as before. The slot
half of the predicate is simply absent at P=1, so `port_pred` returns an invalid
pin for a port with neither slot nor gate and that port is left alone. A clocked
read is moved out of the cell only when its in-cell register cannot carry what
is needed -- the slot, or its own port's gate -- so an ungated clocked read at
P=1 keeps its register and just gains the rebound clock.

And it FAILS CLOSED. Every memory refusal used to be deferred to P>1 for the
same "not our business" reason, so an unplannable shape at P=1 was accepted and
the cell kept its derived clocks. `plan.mem_derived_code` now records an
unplannable cell that `mem_has_derived_clock` says would be left holding one,
and that refusal is raised at EVERY P.

`Result` gained `mem_clocks_rebound` so the P=1 case is visible in the pass's
own line rather than inferred from its absence.

#### 4. Controls

**Direct-LGraph**, three new cases (65 in the file). `Gate_spec` gained
`no_divider`, which makes the fixture's negedge flop a posedge one so the same
memory vocabulary builds a P=1 design:

* the POSITIVE asserts, off the normalized graph, that the write port's
  `clock_pin` IS the reference clock pin, that its enable is a Mux whose arm is
  the original lane mask at full width, that the selector contains the gate's
  transparent value and NOT the latch's Q, that the predicate is the gate ALONE
  (a second conjunct at P=1 would be something the source never had), and that
  the ungated port beside it was not wrapped at all;
* the NO-CHURN control asserts that a P=1 cell already on the root with no gate
  anywhere has every port's clock and enable driver unmoved;
* the FAIL-CLOSED control puts an unrepresentable gate on a P=1 cell with a
  derived clock and requires a refusal with nothing mutated.

**Trace oracle.** `lhd/tests/single_edge_memory_clock_test.sh` is an ICG, a
memory written through the gated clock with an asynchronous read so the array is
OBSERVABLE, and an ordinary gated flop beside it, at P=1. The structural leg
asserts no memory port is on a derived clock and -- using `emission_fanout.py`,
so the claim is computed -- that the gate net has no reader left. The trace leg
runs the pinned original RTL against the normalized emission for 39 four-state
periods with the ICG enable moving INSIDE BOTH PHASES, after a
round-trip-at-1x leg proves the harness itself sound. Three negatives must each
fail: an unconditional write enable (errs=8), the write gated on the latch's
HELD Q instead of its transparent arm (errs=14), and the ordinary flop's enable
dropped (errs=12). A P=2 leg then asserts the same cell gets `slot AND gate` on
the reference clock, so the two regimes are pinned together.

**The live-row change itself** is covered by a new negative in the held-enable
oracle, where the retyped latch is genuinely live because it gates the held
endpoint: putting its `din` back behind its own window -- the shape the retype
used to leave, which at a `posedge clk` samples the window's FALSE arm forever
-- FAILS with errs=78. That is the change behind sixteen of the twenty live
rows, and it is now a trace result rather than an assertion.

**Reverted injections**, rebuilt and re-run:

| injection | fails |
|---|---|
| memories are P>1 business again | the P=1 positive, and the memory-clock oracle |
| the memory refusal deferred to P>1 again | the P=1 fail-closed control, alone |

    pass_single_edge_test                 PASS (65 tests, 3 new)
    latch_contract_test                   PASS
    single_edge_memory_clock_test         PASS (new)
    single_edge_held_enable_test          PASS (one new negative)
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS
    //graph/... //pass/...                67/68 PASS

`//pass/lec:lec_stats_test` is the one failure and is the same pre-existing
cvc5-fork-statistics failure recorded in the previous section; it is unrelated
and untouched.

#### 5. Three corrections the audit of `0d2f64442` required

**The registered test could not find its analyzer.** `no-sandbox` is a
sandboxing choice and says nothing about what is staged for a test, so
`bazel test //lhd/tests:single_edge_memory_clock_test` exited immediately with
`FAIL: cannot find emission_fanout.py` even though the same script passes when
run directly. The tool is now a declared runfile through
`//pass/lean/tests/d4:emission_fanout`, which also gives the D4 evidence
directory its own package. The script still refuses to run rather than skipping
the structural leg if the tool is missing -- that is what turned a silent skip
into a visible failure in the first place.

**The analyzer broke its own fail-closed rule.** Its one contract is that a DEAD
verdict can never come from a parse miss, and review broke it on the first form
tried: for a module containing `assign out = seed;` it reported

    {"unparsed": 0, "seeds": {"seed": ["DEAD"]}}

because the LHS was parsed as a net literally called `assign out`, so the
module's real OUTPUT was never reached. Auditing the rest of the accepted forms
found three more of the same kind: a bit-select (`wide[2] = seed;`) recorded its
READER as a net called `wide[2]` rather than `wide`, so the edge out of it
vanished; a part-select did the same; and an indexed part-select
(`wide[din +: 2] = ...`) silently dropped `din`, which that form READS.

So `emission_fanout.py` now parses a left-hand side explicitly and refuses what
it cannot name. A select is stripped from the base name on both sides, anything
INSIDE a select is treated as a source, Verilog numeric literals are removed
before tokenizing so the `b0` in `1'b0` is not mistaken for a net, and an LHS
select is accepted only when it is CONSTANT. `pass/lean/tests/d4/emission_fanout_test.sh`
is the tool's own test -- nine supported forms that must preserve the actual
left-hand side, one genuinely dead seed that must still read DEAD, and three
unsupported forms that must come back UNPARSED. All of it is a registered bazel
target.

The 17-of-20 classification below was re-measured with the corrected analyzer.

**A read-only synchronous cell could lose its edge at P=1.** `max_slot`
deliberately ignores a memory with no write port, because a read-only cell
commits nothing and must not force a divider into existence. But a CLOCKED read
does commit, in the cell's own read register, and if its cone is inverted the
cell's slot is 1 in a design that stayed at P=1. The new P=1 path would then
rebind that read to the reference clock's RISING edge -- moving a falling-edge
read half a period, silently.

`build_plan` now refuses that cell before anything is mutated:
`memory-sync-read-unsupported`, "commits on the reference clock's FALLING edge,
and this design has no phase divider to carry a second slot". Two new controls,
and `build_mask_mem` gained `no_divider` (which replaces the fixture's negedge
flop with a GATED posedge one, so the design lands at P=1 and the pass still has
work to do): the inverted cell must refuse with the cell type, every read flop
and every clock driver unmoved, and the SAME cell on the reference edge must
normalize with nothing rebound and nothing externalized. Reverting the guard
makes the first one fail with `1 memory clock(s) rebound` -- the defect itself.

A fourth, smaller correction: the P=1 write-enable skip originally called
`port_pred` before the Mux was created, and `port_pred` creates `And` nodes, so
every node downstream was renumbered and `minion_dcache_top`'s emission hash
moved on a design this change does not otherwise touch. Measured: 32 diff lines,
0 once node numbers are stripped. The skip is now a predicate that creates
nothing, and that block's emission is byte-identical to the audited one again.

    pass_single_edge_test                 PASS (67 tests, 5 new in this unit)
    latch_contract_test                   PASS
    emission_fanout_test                  PASS (new, 12 controls)
    single_edge_memory_clock_test         PASS (as a bazel target)
    single_edge_held_enable_test          PASS
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS

All ten registered targets run under `bazel test`, not only as direct scripts.

#### 6. Two more analyzer fail-opens, and one in the inventory producer

**A declaration may not carry an initializer.** Review's next probe was

    wire forwarded = seed;

with `assign outp = forwarded;`. The declaration branch matched
`^\s*(reg|wire|logic)\b`, took the LAST identifier on the line as the declared
name -- `seed` -- and `continue`d, so the edge from `seed` to `forwarded` was
never written and a seed live at the module OUTPUT came back
`{"unparsed": 0, "seeds": {"seed": ["DEAD"]}}` with exit 0. The same branch
swallowed any malformed declaration the same way.

The branch is now a strict grammar: one bare declaration of ONE name, with an
optional signedness, an optional range before the name and an optional range
after it, and NO `=`. A comma-separated list, a declaration with no name, and a
declaration with an initializer all poison the module. The narrow safe option
was taken rather than parsing initializers, and the SAME connectivity written as
a bare declaration plus an `assign` is still supported -- which is what makes
the refusal about the form and not about the shape.

`emission_fanout_test.sh` now carries sixteen controls, and the three UNPARSED
ones additionally require a NONZERO EXIT and the absence of the string `DEAD`
anywhere in the output: a caller that reads only the exit code and a caller that
reads only the JSON must both come away knowing no verdict was reached.

Checked against the measured netlists before trusting the stricter rule: all
twenty of the corpus emissions still parse with zero unparsed lines, so the
grammar rejects forms cgen does not produce.

**The inventory producer compared the wrong pair.** Found here, not in review,
while the first authenticated run came back with every `changed_after` column
empty. `coreet_emission_fanout.sh` had

    local side="$1" m="$2" d="$W/$m/$side"

and bash expands `$side` and `$m` in that statement from the OUTER scope, not
from the assignments beside them. `side` was the leftover `N` from the binding
loop and `m` was empty, so side A's emission was written into side N's directory
and then overwritten by it: the "before" column was really N against B, and the
"after" column was N against itself, which is why it read 0 everywhere.

One `local` per name now, and -- because a silent directory collision is exactly
the kind of thing that produces a confident wrong table -- the producer asserts
that each side emitted to the path it was asked for and that no two sides share
a netlist.

    emission_fanout_test                  PASS (16 controls)
    pass_single_edge_test                 PASS (67)
    latch_contract_test                   PASS
    single_edge_memory_clock_test         PASS
    single_edge_held_enable_test          PASS
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS

All ten run as bazel targets with `--cache_test_results=no`.

#### 7. The authenticated inventory, and the regenerated tables

`coreet_emission_fanout.tsv`, `d4-emission-fanout/1`, produced by
`coreet_emission_fanout.sh` from three authenticated binaries built from their
own complete trees:

| side | commit | sha256 |
|---|---|---|
| A | `ce5fdfc7a` | `aa67170d01ab0445…` |
| before | `391b1eb90` | `7f8f4ee3348c0de9…` |
| after | `e01aa4575` | `e72b32f9a5b2ea60…` |

    modules                       20
    live BEFORE (not DEAD/NONE)   16
    live AFTER                    15
    memory CLOCK reached, BEFORE   3
    memory CLOCK reached, AFTER    0
    UNPARSED (no verdict claimed)  0

So the corrected answer to "were the twenty emission-only rows dead logic" is
**no: sixteen of twenty were live, and three of those reached a memory CLOCK**.
That is the number measured with the CORRECTED analyzer; the 17 quoted while
the analyzer still mis-parsed `assign` was never published as a result and is
superseded here. The one row that moves between the columns is
`minion_dcache_reduce`, MEM-CLOCK to DEAD; `txfma_top` and `txfmafrac_top` move
from MEM-CLOCK to STATE and pick up sixteen more changed registers each, which
are their memory write and read enables.

**A semantic control for every still-live row.** All fifteen carry the same
change, and it was read off the netlists rather than assumed: on `intpipe_rf`
and `vpu_tensora_rf` the single changed register is the register file's
`wr_data_en_1p` enable latch, whose `din` moves from `mux_NNNN` -- the
window-gated mux, identically 0 at a `posedge clk` -- to the transparent value
`wr_en_early`. That is exactly the mutation the held-enable oracle's
`window` negative restores, where the same latch is live because it gates the
held endpoint, and it FAILS the trace with errs=78. The memory side of
`txfma_top`, `txfmafrac_top` and `minion_dcache_reduce` is the P=1 memory-clock
oracle's three negatives (errs=8/14/12). No LEC result is counted anywhere: the
strict RTL-versus-normalized attempt on `minion_dcache_reduce` is inconclusive
because the REFERENCE holds derived memory clocks the encoder cannot model, and
an inconclusive LEC is a refusal.

**The 122-row tables**, A `ce5fdfc7a` -> B `e01aa4575`:

    A/B             identical 86   emission-changed 22   acceptance-changed 2
                    both-refused 8   refusal-code-changed 0   input-unusable 4
    classification  118 rows + 0 skipped + 4 unusable = 122
                    24 baseline refusals -> 14 cleared, 7 unchanged, 3 renamed
                    0 regressed, 94 still-ok          PROBE OK

Every total is identical to the audited tables, so the memory lowering and the
P=1 read-only refusal changed no verdict on the corpus. Exactly **three rows**
differ, and they are the three with a live memory clock:
`minion_dcache_reduce`, `txfma_top`, `txfmafrac_top`. `minion_dcache_top` is
byte-identical to the audited emission again, which is what the renumbering fix
was for.

33 evidence controls PASS, including the positive that runs the full verifier
WITH both exact-tree rebuilds. Every historical runtime table is byte-unchanged
and no corpus sweep was relaunched. Focused CORE-ET coverage remains **97/122**
and CVA6 **30/30**: this unit produced no new runtime row.

Trust boundary unchanged: exact-tree authenticated binaries, finite A/B
emissions, fanout classification and trace controls are empirical translation
validation, not a generic formal proof of the C++ LGraph-to-DCERT1
transcription.

#### 8. The inline `always` fail-open, and a producer that published too early

**`always_comb outp = seed;`** is legal SystemVerilog -- cgen emits exactly that
form inside the memory wrapper -- and the reader accepted ANY line beginning
`always` and `continue`d, so the statement vanished and a seed driving the
module OUTPUT came back `{"unparsed": 0, "seeds": {"seed": ["DEAD"]}}` with exit
0. The branch now matches only the exact block openers
(`always_comb begin`, `always_latch begin`, `always_ff @(...) begin`,
`always @(...) begin`); an `always*` statement that does not end in `begin` is an
inline body this reader does not follow, so it refuses. Both the inline
`always_comb` and the inline `always_ff` shapes are controls, and all four
openers are controls on the other side -- twenty-two in total. Checked against
the forty measured netlists before trusting the stricter rule: every one still
parses with zero unparsed lines.

**The producer ignored the analyzer's exit status.** Its embedded reader printed
`UNPARSED` or `TOOL-FAILED` into the row and exited 0, and the outer shell
appended that row without recording a failure -- so the producer could finish
successfully with a table full of non-verdicts. And it ran `mv` BEFORE checking
anything, so a run that did return nonzero still left the requested table on
disk.

Both are closed. Every bad outcome is now fatal in the ROW path -- a nonzero
analyzer exit, unreadable JSON, an UNPARSED verdict, a seed map that does not
match the seeds asked about, an unknown class, an empty class set, a
non-numeric count, a field that is not one field -- and nothing is published
until the whole table reconciles: twenty declared identities, exactly one
five-field row each, no duplicates, no extras, no failure token in any data
row, and every class cell a name the reader is allowed to produce. The `mv` is
the last statement.

**The controls drive the real producer, not a copy of it.** The analyzer is
addressed through `D4_FANOUT_TOOL`, so `coreet_emission_fanout_controls.sh` can
put a stub in its place and exercise THIS script's result handling. Eleven
controls: seven stub analyzers (nonzero exit, unreadable JSON, UNPARSED, a seed
it was not asked about, an EXTRA seed, an unknown class, an empty class set), a
list naming a block with no retained input, a list naming the same block twice,
the same commit on both compared sides, and the positive.

Each refusal pins its OWN diagnostic, and that is not decoration. The row path
and the publish gate are independent guards that both end in "nonzero and no
table", so the first version of these controls -- which checked only the
outcome -- passed with EITHER of them reverted. Measured, with the needles in
place:

| injection | fails |
|---|---|
| the analyzer's exit status ignored again | the three analyzer-status controls, by refusing for the wrong reason |
| `mv` moved back before the reconciliation | the duplicate-identity control, which is the one whose refusal comes from the publish gate: "exited 2 but PUBLISHED ... anyway" |

    emission_fanout_test                  PASS (22 controls)
    coreet_emission_fanout_controls       PASS (11 controls, real producer)
    pass_single_edge_test                 PASS (67)
    latch_contract_test                   PASS
    single_edge_memory_clock_test         PASS
    single_edge_held_enable_test          PASS
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS

The ten registered targets run under `bazel test --cache_test_results=no`;
`coreet_emission_fanout_controls.sh` is not one, for the same reason
`frontend_evidence_controls.sh` is not: it needs the untracked retained corpus.

#### 9. The inventory, regenerated through the fail-closed chain

`coreet_emission_fanout.tsv` regenerated with the corrected analyzer and the
corrected producer. `after_commit` is `cfe8c6136`, and its binary hashes to the
same `e72b32f9a5b2ea60…` as `e01aa4575`'s -- which is what should happen for a
commit that touches only evidence tooling, and is itself a check on the
attribution.

    modules                       20
    live BEFORE (not DEAD/NONE)   16
    live AFTER                    15
    rows                          20
    memory CLOCK reached, BEFORE   3
    memory CLOCK reached, AFTER    0

**Every data row and every count is identical to the table the audit
reconciled.** The only line that moved is the summary line that used to say
`UNPARSED (no verdict claimed) 0`, which is gone: an unparsed module now aborts
the run, so a count of them can only ever be zero and reporting it invited the
reader to believe the run would have continued.

That the numbers did not move is the expected result, and it was checked rather
than hoped for: all forty measured netlists parse under the stricter `always`
and declaration grammars with zero unparsed lines, so the forms those
fail-opens accepted do not occur in this corpus. What changed is that the
numbers now come from a chain that cannot report a verdict it did not reach --
the producer refuses on any analyzer failure, and publishes nothing until all
twenty identities reconcile.

The 122-row front-end tables are unchanged and were not rerun: this correction
is evidence tooling only and does not touch the pass, so the binary it measures
is byte-identical to the one those tables already attest.

Coverage is unchanged: **97/122** current-focused CORE-ET, **30/30** CVA6. Every
historical runtime table is byte-unchanged and no corpus sweep was relaunched.

Trust boundary unchanged: exact-tree authenticated binaries, finite A/B
emissions, fanout classification and trace controls are empirical translation
validation, not a generic formal proof of the C++ LGraph-to-DCERT1
transcription.

#### 10. The publication contract

Three fail-open paths, all in what the producer does with the path it was told
to write.

**A failed rerun could leave the PREVIOUS run's table there.** The producer only
ever created `$OUT.tmp.$$`; it never touched `$OUT` until the final `mv`. So an
analyzer, emission or reconciliation failure returned nonzero while an old table
sat at exactly the path the caller had asked THIS invocation to produce --
stale evidence looking current. Every negative in the control suite masked it,
because `must_refuse` removed the destination before running.

The destination is now settled BEFORE anything that can fail: it must be absent
or a regular file (not a symlink, not a directory), its directory must be
writable, and any previous table is removed and its removal verified. The
control for it deliberately does NOT use `must_refuse` -- removing the path
first is exactly what hid this -- and preseeds the requested path with a stale
sentinel before injecting an analyzer failure.

**Publication failure could return success.** `set -e` is not in use, and the
epilogue was `mv; tail; echo`, none of them checked. With `$OUT` a DIRECTORY the
`mv` deposited the temporary file INSIDE it, the `tail` failed, the `echo`
succeeded, and the producer exited 0 claiming it had written a table at a path
that held none. Every step is checked now: the rename, that `$OUT` is afterwards
a regular file, that it holds exactly the declared number of five-field rows,
that it carries its schema line and this producer's hash, and that it can be
read back. `wrote` is printed only after all of that.

**The table hashed the analyzer but not the producer.** `generated_by` was a
path, and a path cannot distinguish the corrected chain from the one that
published whatever it had -- yet the fail-closed behaviour that makes these
verdicts worth reading lives in the producer, not in the analyzer. The header
now carries `producer_sha256` beside `tool_sha256`, the epilogue refuses to
publish a table that does not carry it, and the positive control checks the
recorded hash against the producer's own bytes.

Thirteen controls against the real producer. The two new ones, measured:

    a failing run leaves no stale table at the requested path
    a non-publishable destination is refused with no 'wrote' claim and
      nothing deposited inside it

and each is load-bearing:

| injection | fails |
|---|---|
| the previous table no longer cleared up front | the stale-table control |
| the whole publication contract reverted -- no destination check, no early clear, the unchecked `mv; tail; echo` epilogue | the stale-table control AND "a directory destination was accepted" |

The directory control also had to be made to name its own guard. Without that
needle it passed with the entire contract reverted, because `rm -f "$OUT"` fails
on a directory too and reaches the same outcome by a different route -- the same
"two independent guards, one outcome" trap the analyzer controls hit earlier.

One honest limit: with the destination contract in place, `$OUT` can never be a
directory by the time the `mv` runs, so the EPILOGUE checks are total guards
rather than live branches -- like the `/N` divider guard elsewhere in this pass.
They are there so that if the contract ever moves, publication fails instead of
printing a false `wrote`.

    emission_fanout_test                  PASS (22 controls, bazel target)
    coreet_emission_fanout_controls       PASS (13 controls, real producer)

#### 11. The inventory, regenerated once more

Only the 20-row inventory and this section. The 122-row front-end tables are
untouched and were not rerun: this correction is evidence tooling and does not
reach the pass, so the binary they attest is byte-identical.

    modules                       20
    live BEFORE (not DEAD/NONE)   16
    live AFTER                    15
    rows                          20
    memory CLOCK reached, BEFORE   3
    memory CLOCK reached, AFTER    0

Every row value is unchanged. What the table gained is `producer_sha256`, so the
artifact now names both halves of the chain that produced it.

Coverage is unchanged: **97/122** current-focused CORE-ET, **30/30** CVA6. Every
historical runtime table is byte-unchanged, no corpus sweep was relaunched, and
no runtime certificate was rerun.

Trust boundary unchanged: exact-tree authenticated binaries, finite emissions,
fanout classification and differential controls are empirical translation
validation, not a generic formal proof of the C++ LGraph-to-DCERT1
transcription.

#### 12. Publication: validate the candidate, then rename

The previous shape renamed first and validated after:

    mv -f "$TMP" "$OUT" || d4_die ...
    [[ -f "$OUT" && ! -L "$OUT" ]] || d4_die ...
    published="$(awk ... "$OUT" | wc -l)" || d4_die ...
    ...
    tail -7 "$OUT" || d4_die ...

Every one of those `d4_die`s exits 2 with `$OUT` POPULATED -- the EXIT trap
removes only the work directory. So a short read, or any later failure, left a
table at the path attributed to this invocation while reporting failure: the
same prohibited state as a stale table, reached from the other direction. None
of the thirteen controls reached it, because the directory case is refused
before any work and the stale-table case dies during analyzer execution.

Every assertion is now made on the CANDIDATE -- row count, schema line,
producer hash, and the summary `tail`, which is printed from `$TMP` so that
reading it is one of the things that must succeed BEFORE `$OUT` exists. The
checked rename is the last fallible operation in the script; `echo "wrote $OUT"`
and `exit 0` follow it, and neither can fail the invocation. That is the
smaller pre-rename design rather than post-rename compensation: there is
nothing to compensate for.

**The boundary control is structural, because the region it is about now holds
nothing that can fail.** It reads the producer's own source: the text after
`mv -f "$TMP" "$OUT"` must be exactly `echo "wrote $OUT"` and `exit 0`, and the
pre-rename region must contain the schema, producer-hash and row-count
assertions and must not name `$OUT` at all. `D4_FANOUT_CTRL_STRUCTURAL_ONLY=1`
runs just that pair, which is how reverting the correction is demonstrated
without half an hour of emissions -- the committed control itself, not a copy of
its logic.

Measured, load-bearing: restoring the rejected ordering fails FOUR controls,
and the failure names the exact post-rename operations --

    after the rename: [[ -f "$OUT" && ! -L "$OUT" ]] ... | published="$(awk ...
    CONTROL FAILED: an operation that can fail the invocation follows the
                    publication rename
    CONTROL FAILED: the candidate is not checked for 'schema' before the rename
    CONTROL FAILED: the candidate is not checked for 'producer_sha256' ...
    CONTROL FAILED: the candidate is not checked for 'the candidate table holds' ...

**One bug of mine, caught by the needles added in the previous section.** The
structural check first stored the post-rename text in `$AFTER` -- which is this
control script's own AFTER-COMMIT argument. Every later control then invoked
the producer with that text where a commit belonged, and refused for the wrong
reason; the needles said so immediately (`refused, but not with 'empty class
set'`). Renamed to `BOUNDARY_*`. Without the per-control diagnostics added one
section earlier, those thirteen controls would have gone on passing.

    emission_fanout_test                  PASS (22 controls, bazel target)
    coreet_emission_fanout_controls       PASS (15 controls, real producer)

#### 13. The inventory, regenerated once more

`32011952a` landed while this audit was in flight and records the superseded
producer. It is left in history -- nothing is reset, amended or rebased -- and
superseded here by a regeneration against the corrected one. Row values are
unchanged; `producer_sha256` now names the producer that validates before it
publishes.

Coverage unchanged: **97/122** current-focused CORE-ET, **30/30** CVA6. No
runtime certificate and no 122-row front-end table was rerun; every historical
runtime table is byte-unchanged.

### Rules B and C ask about the value a latch passes, not its raw `din`

#### 1. The false positive

`minion_dcache_tensor_load` never reached `pass.single_edge` at all: it failed
in stage 1, `RTL -> LGraph`, on

    pass.latch: rule C, simultaneously-transparent pair -- latches `latch_5116`
    and `latch_7124` are transparent at the SAME time and there is a
    combinational path between them

`latch_7124`'s `din` is `Mux(!clkgt, _, wr_data_en_1p_next_i)`. The raw walk
left through the mux's SELECTOR -- `!clkgt`, which is `!(clk_i & en_latch)` --
and so reached `latch_5116`. What `latch_7124` actually CAPTURES is
`wr_data_en_1p_next_i`, which does not touch `latch_5116` at all.

A selector decides WHEN a latch closes. `commit_class_of` already models that,
and rule C then compares it as a phase; it is not a combinational data path
between two latches, and time borrowing is about values straddling two open
windows, not about one window gating the other.

So rules B and C now walk the value the latch passes. Rule A is untouched: a
self-timed gate is exactly a question about the ENABLE cone.

#### 2. Why not `latch_transparent_arm`

The obvious helper already exists, and this repository already measured it
wrong. `lc::latch_transparent_arm` returns the FIRST arm that is not the
latch's own Q, with no way to tell a hold mux from anything else; on a
`cond ? en : 0` din it hands back whichever arm it visits first, and on
`minion_dcache_reduce` that was the CONSTANT arm. In `pass.single_edge` that
produced a wrong gate. HERE it would be worse: walking the wrong cone makes the
contract MISS a violation.

`latch_held_data` recognizes the shape instead of guessing it, accepting the
same two forms `single_edge`'s own reader accepts -- a HOLD MUX (one arm IS the
Q, so the other is the value) and a WINDOW MUX (neither arm is the Q and the
selector IS this latch's own enable cone, the shape slang gives
`prim_clk_gate`) -- and declines an active-low latch, where the transparent arm
flips. Anything unrecognized returns an invalid pin and the caller falls back to
the whole `din`: the direction that can only reject more.

The own-Q hold exemption in `comb_reach` is passed ONLY on that fallback. Once
the data arm has been extracted the structural hold mux is already gone, and
keeping the exemption there would hide a self-update written into the arm
itself.

#### 3. Controls, and what each injection breaks

Four new direct-LGraph contract controls, eleven in the file:

* **the measured shape is ACCEPTED** -- three latches in two commit classes
  (`l1` the ICG's enable latch, `l2` and `l3` sharing `!gclk`), where every
  connection between them is a mux selector. The test first ASSERTS that the
  pair it is about really does share a net and an edge, or rule C could not have
  fired either way and the control would pass for the wrong reason.
* **a real data path is still REJECTED by rule C** -- two latches on a PLAIN
  `!clk` window, one CAPTURING the other's Q. A plain window on purpose: in the
  ICG fixture `l2`'s selector reaches `l1` anyway, so a reader that walked
  selectors would reject it for the wrong reason and this control would not
  discriminate.
* **a self-update written into the TRANSPARENT ARM is still REJECTED by rule B**
  -- `din = window ? (q + 1) : _`.
* **raw-D forms keep their existing verdicts** -- no mux at all: `din = q + 1`
  rejected, `din = d` accepted.

| injection | fails |
|---|---|
| the raw `din` walk restored | the selector-only control, alone |
| the selector walked instead of the data arm | the selector-only control AND both genuine negatives, rule B and rule C |

#### 4. The block now clears stage 1

    lhd compile verilog ... --top minion_dcache_tensor_load   exit 0

against the same filelist and flags that produced the rule-C refusal.

    latch_contract_test                   PASS (11 tests, 4 new)
    pass_single_edge_test                 PASS (67)
    emission_fanout_test                  PASS (22)
    single_edge_held_enable_test          PASS
    single_edge_memory_clock_test         PASS
    single_edge_memory_gate_test          PASS
    single_edge_memory_slot_test          PASS
    latch_contract_check_test             PASS
    lec_latch_polarity_test               PASS
    lec_clock_blindness_test              PASS

All ten with `--cache_test_results=no`.

#### 5. The authenticated 122-block remeasurement

This contract gates every compile, so the front-end tables were remeasured in
full against the corrected one. A `ce5fdfc7a` -> B `a6b83110b`:

    A/B             identical 86   emission-changed 22   acceptance-changed 2
                    both-refused 8   refusal-code-changed 0   input-unusable 4
    classification  118 rows + 0 skipped + 4 unusable = 122
                    24 baseline refusals -> 14 cleared, 7 unchanged, 3 renamed
                    0 regressed, 94 still-ok          PROBE OK

**Every one of the 122 A/B rows and all 118 classification rows is BYTE-IDENTICAL
to the committed tables** -- zero acceptance changes, zero emission changes,
every identity accounted for. What moved is the attested binary:
`d09efb031482ab66…`, 1263363544 bytes, rebuilt from `a6b83110b`.

That is the expected result and worth stating precisely rather than leaving as
an absence. The front-end producers run `pass.single_edge` directly on each
block's retained `lgdb_raw`; `pass.latch` runs in `lhd compile`, which is the
stage that BUILDS an `lgdb_raw`, and the corpus inputs were retained long ago.
So the contract cannot move a row of these tables -- it decides whether a block
reaches the emitter at all, which is the certificate pipeline's stage 1 and what
the runtime section below is about. Remeasuring was still the right call: "it
cannot matter" is a prediction, and the tables are what settle it.

The four `input-unusable` identities and the eight `both-refused` ones remain
explicit refusals, unchanged; the two `acceptance-changed` rows are still
`minion_frontend` and `minion_frontend_thread_buffer` on
`coincident-commit-edge`, which this unit was told not to tackle.

33 evidence controls PASS, including the positive that runs the full verifier
WITH both exact-tree rebuilds: "the real tables verify, and both binaries
rebuild from their named commits".

#### 6. The focused runtime certificate: 97 -> 98

With stage 1 clear, the block was run at the limits every earlier pilot used --
8192 KB stack, 3600 s / 1800 s timeouts, 8 GiB emission ceiling, jobs 1.
Nothing was raised to make it fit.

    coreet  minion_dcache_tensor_load  EMITTED  PARSED  compilesOk=true  RAN
            run_ms 710   wall 28.5 s   peak_rss 6336772 KB
            digest 9225711661718483716
            cert   126b65af994d1c88...

**That digest is the one the historical R2 table records for this block**, which
is a real check and not a coincidence: the certificate this corrected pipeline
produces evaluates to the same value the previously accepted run did, through a
front end that now folds the gate into an enable and a contract that no longer
refuses the cell.

The cgroup record is complete and backs its own verdict: `OK`, `oom_kill=0`,
`oom_group_kill=0`, `memory_peak_bytes=105381888`, `emit_rc=0`,
`cleanup=clean`. The gates driver accepts it on all three:

    minion_dcache_tensor_load  EMITTED  RAN  PASS PASS PASS  accepted=yes
    GATES OK: 1 of 1 candidate rows accepted; 0 gate failures, 0 hash mismatches

`PASS PASS PASS` is the static shape gate, the invariant half of the
certificate differential, and the REAL differential with cgen Verilog
regenerated from this block's own `lgdb_norm` -- and the row's own
`cert_sha256` recomputed from the retained bytes.

So this block is current-valid again, and focused CORE-ET coverage moves from
**97/122 to 98/122**. CVA6 remains 30/30. The three-block resource-limit pilot
still has zero RAN rows, and `minion_dcache_top`'s 8 GiB blocker is untouched.

    coreet_tensor_load_rows.tsv              1 row, RAN, 18 fields
    coreet_tensor_load_provenance.txt        d4_tracked_dirty=no, reconciled=YES
    coreet_tensor_load_cgroup_records.tsv    1 complete record, OK, no kill
    coreet_tensor_load_gates.tsv             1 of 1 accepted

These four supersede the EMIT_FAILED set committed at `52e895b52`, when the same
block could not clear `pass.latch`; that result is in history and the section
above it still records why.

All historical runtime tables -- `coreet_122_fresh_*`, `coreet_122_r2_*`,
`coreet_122_runtime_*`, `coreet_type1_*`, `coreet_srconly_*`,
`coreet_gate_pilot_*`, every `cva6_*` -- are byte-unchanged, and no corpus sweep
was relaunched.

Trust boundary unchanged: exact-tree authenticated binaries, finite front-end
tables, this runtime certificate and the trace/differential controls are
empirical translation validation, not a generic formal proof of the C++
LGraph-to-DCERT1 transcription.

#### 7. Two corrections: a fail-open selector, and an immutable file

**The helper discarded a hold-mux SELECTOR without proving anything about it.**
`latch_held_data` returned the non-Q arm as soon as exactly one arm was this
latch's Q, and only consulted the selector afterwards, for the no-Q case. So on

    b.din = Mux(a.Q, b.Q, db)

-- `a.Q` deciding whether `b` holds or passes `db` while BOTH are transparent --
it handed back `db` and the one edge that mattered vanished. Rule C accepted a
genuine same-window dependency. The old raw walk traversed that selector, so
this was a regression the shortcut introduced, and review's direct-LGraph
reproducer is now a control that fails on the shortcut and passes on the repair.

A selector may be dropped only once it is PROVED to be this latch's own
transparency cone. That proof now runs first, in every shape, and the
`strip_width` it rests on fails closed: a wrapper with no value operand, or with
that operand multi-driven, returns an invalid pin rather than itself, because a
wrong answer there would prove a selector to be a window when it is not.

Three things are recognized once the selector is proved, and they differ only in
what sits on the arm the CLOSED window selects -- this latch's own Q, a
CONSTANT, or nothing. The constant case is not hypothetical: it is what slang
gives `prim_clk_gate` and what `minion_dcache_tensor_load` actually holds
(`window ? d : 0`), and requiring Q-or-absent re-refused the block. Arbitrary
live logic on the closed arm is not the hold idiom, so the shape is declined and
the whole `din` -- selector and both arms -- stays in the walk, with only the
direct own-Q arm exempted.

**`graph/latch_contract_test.cpp` is immutable**; every test or benchmark
filename containing `contract` is. It is restored byte-for-byte to its
`cf5734b50` contents, and the controls live in
`graph/latch_window_rules_smoke.cpp` -- a standalone check, so no `_test`
suffix, and no `contract` in the name.

Six controls there, all load-bearing:

| injection | fails |
|---|---|
| the raw `din` walk restored | the selector-only accept control, alone |
| the selector walked instead of the data arm | accept, the rule-C data-arm negative, the rule-B negative |
| the selector proof skipped for an own-Q hold mux (review's shortcut) | the hold-selector negative, alone |

and the set covers: the measured ICG shape accepted with BOTH closed-arm forms
(constant and unconnected); a real data-arm path rejected by rule C; a
hold-mux SELECTOR dependency rejected by rule C; a live CLOSED arm rejected by
rule C; a self-update through the transparent arm rejected by rule B; and
raw-D keeping its existing verdicts.

    latch_contract_test (restored)        PASS (7)
    latch_window_rules_smoke              PASS (6, new)
    pass_single_edge_test                 PASS (67)
    emission_fanout_test                  PASS (22)
    the seven focused shell targets       PASS

Eleven targets, `--cache_test_results=no`. `minion_dcache_tensor_load` still
clears stage 1: `lhd compile verilog ... --top minion_dcache_tensor_load`
exits 0.

#### 8. The 122-block evidence, remeasured against the corrected contract

A `ce5fdfc7a` -> B `43e637c6a`:

    A/B             identical 86   emission-changed 22   acceptance-changed 2
                    both-refused 8   refusal-code-changed 0   input-unusable 4
    classification  118 rows + 0 skipped + 4 unusable = 122
                    24 baseline refusals -> 14 cleared, 7 unchanged, 3 renamed
                    0 regressed, 94 still-ok          PROBE OK

Every one of the 122 A/B rows and all 118 classification rows is again
BYTE-IDENTICAL to the committed tables. The attested binary moves to
`fd63b6e689a8841d…`, 1263364016 bytes, rebuilt from `43e637c6a`.

The reason is the one recorded in the previous section and it still holds: the
front-end producers run `pass.single_edge` directly on each block's retained
`lgdb_raw`, while `pass.latch` runs inside `lhd compile`, the stage that BUILDS
an `lgdb_raw`. The contract decides whether a block reaches the emitter -- stage
1 -- not what the emitter then does with it. Remeasuring is still what settles
that, rather than the prediction.

33 evidence controls PASS, including the positive that runs the full verifier
WITH both exact-tree rebuilds.

#### 9. The focused runtime certificate, rerun against the corrected binary

Same block, same limits -- 8192 KB stack, 3600 s / 1800 s timeouts, 8 GiB
emission ceiling, jobs 1 -- against `lhd` rebuilt from the selector-proof tree.
Nothing was raised to make it fit, and no other block was run.

    coreet  minion_dcache_tensor_load  EMITTED  PARSED  compilesOk=true  RAN
            run_ms 708   wall 24.5 s   peak_rss 6330192 KB
            digest 9225711661718483716
            cert   126b65af994d1c88...

The certificate is BYTE-IDENTICAL to the one the pre-correction binary emitted
(`cmp` on the two `.dcert` files is silent), so `cert_sha256`,
`verilog_sha256` and `lean_sha256` are all unchanged, and the digest is still
the one the historical R2 table records for this block. That is the useful
reading of this rerun: proving the selector before dropping it changed WHICH
shapes the contract will accept, not what the emitter produces for the shape it
already accepted.

Across the four files only four things moved, and none of them is a
measurement:

    lhd_sha256        9b8d3fcc14828a29...  ->  3a2e6dac8720ac78...
    d4_head           32dceefda            ->  f1246f83f
    run directory     temp/w3/out          ->  temp/w4/out
    timings / RSS     710 ms, 28.5 s, 6336772 KB  ->  708 ms, 24.5 s, 6330192 KB
                      memory_peak 105381888 B     ->  105377792 B

The cgroup record is again complete and backs its own verdict: `OK`,
`oom_kill=0`, `oom_group_kill=0`, `emit_rc=0`, `cleanup=clean`. The gates
driver again accepts on all three:

    minion_dcache_tensor_load  EMITTED  RAN  PASS PASS PASS  accepted=yes
    GATES OK: 1 of 1 candidate rows accepted; 0 gate failures, 0 hash mismatches

With this and section 8 in, the corrected evidence is complete and focused
CORE-ET coverage stands at **98/122**, CVA6 at 30/30, on a binary built from
the tree that contains the selector proof. Every historical runtime table is
byte-unchanged and no corpus sweep was relaunched.

Trust boundary unchanged: exact-tree authenticated binaries, finite front-end
tables, this runtime certificate and the trace/differential controls are
empirical translation validation, not a generic formal proof of the C++
LGraph-to-DCERT1 transcription.

### Certificate churn for one adjacent RTL edit

`DIRECTION4_INCREMENTAL.md` section 6 item 6 has been open since the plan was
written:

> No ΔG computation exists anywhere, so "how big is a typical Δ?" -- the number
> that determines whether any of this pays -- is **entirely unmeasured**. A
> one-line RTL edit may well perturb 60 % of a flat post-`cprop` LGraph.

It is now measured, once, for one edit. **It perturbs 82 %.**

#### The pair

    A = 288a27cf4  Preserve SFENCE.VMA rd legality in VS mode (#3470)
    B = 6ee41edb2  Compute status SD bits from next dirty state (#3469)
    B^ == A, verified from the checkout, not assumed

    core/csr_regfile.sv | 4 ++--
    -  mstatus_d.sd  = (mstatus_q.xs  == riscv::Dirty) | (mstatus_q.fs  == riscv::Dirty);
    +  mstatus_d.sd  = (mstatus_d.xs  == riscv::Dirty) | (mstatus_d.fs  == riscv::Dirty);
    -  vsstatus_d.sd = (vsstatus_q.xs == riscv::Dirty) | (vsstatus_q.fs == riscv::Dirty);
    +  vsstatus_d.sd = (vsstatus_d.xs == riscv::Dirty) | (vsstatus_d.fs == riscv::Dirty);

**Only the FIRST hunk is live here, and the evidence says so rather than the
reasoning.** `cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv:36` sets
`CVA6ConfigHExtEn = 0`, so `CVA6Cfg.RVH` is a compile-time zero and the second
hunk is elaborated away: `vsstatus` appears **0** times in the emitted netlist
while `mstatus` appears **290**. The measured edit is ONE expression, and the
reading below is about one expression.

#### The experiment

One `lhd` artifact resolved for an EXPLICIT bazel configuration and asserted
before and after every emission; one wrapper generator; one target; one module
list; one recipe; one set of limits (8192 KB stack, 3600 s / 1800 s, 8 GiB
ceiling, jobs 1). Both foundations were built from scratch from their own
pinned CVA6 worktree, both tracked-clean, with identical submodule pointers.
The two filelists are identical modulo the source path, and
`csr_regfile_gate.sv` is **byte-identical** on both sides -- so the wrapper is
not a variable and the RTL revision is.

Each side passed independently before any comparison was made: EMITTED,
PARSED, `compilesOk=true`, RAN, its row's own `cert_sha256` recomputed from the
retained bytes, and all three gates -- static shape, the certificate invariant,
and the REAL differential against Verilog regenerated from that side's own
`lgdb`. A failed side would have been a failed experiment, not a Delta=100 % row,
and the producer refuses that case by name.

**An independent cross-check fell out of it.** Side B's certificate is
byte-identical to the one the accepted canonical-30 evidence records for
`csr_regfile` (`210cd4ac7bfc940d...`) -- a different run, a different
foundation, a different CVA6 revision downstream of B, and a different binary.

#### The result

    metric                         absolute     % of certificate
    ordinal churn                    55,643             81.8833
      same-slot changed              55,641
      inserted / deleted                  2 / 0
      common record prefix           11,991
      common record suffix                0
    structural churn                     18              0.0265
      only in A / only in B               8 / 10

    A  32,852 sources  34,896 nodes  68 outputs  136 flops  0 memories  1,354,019 bytes
    B  32,853 sources  34,897 nodes  68 outputs  136 flops  0 memories  1,354,058 bytes

Per section, same-slot changed:

    sources  20,541 of 32,852      nodes  33,894 of 34,896
    outputs      54 of 68          flops    136 of 136       memories 0 of 0

`cva6_csr_regfile_dg.tsv`.

#### What the two metrics say about Option B

Section 3's Option B split the FLAT SLOT SPACE across Lean modules so an edit
would rebuild only the modules it touched. Its own stated kill condition is in
section 7:

> If a small RTL edit perturbs most of the flat slot space, Option B is dead and
> Option A is the only answer.

82 % is most. **Flat-slot Option B does not deserve another experiment**, and it
does not need one: the mechanism is visible in the table rather than inferred.
The first 11,991 records are identical; then ONE source record is inserted, and
every slot after it shifts by one. The common record SUFFIX is 0 -- not a single
record at the end survives in place. 67,954 - 11,991 = 55,963 records sit after
the insertion point and 55,643 of them moved, the small shortfall being records
that happen to coincide after shifting.

So the 82 % is not 82 % of the design changing. It is one insertion renumbering
everything downstream of it. The structural metric prices the same edit at
**18 records, 0.0265 %**, and the controls show that metric is not simply blind:
retagging one live operator in this certificate moves 11,206 signatures and
repointing one live dependency moves 9,216, so a metric that reports 18 for this
edit is reporting a genuinely local change.

That gap -- 55,643 against 18 -- is the finding. It does not rescue Option B,
which was defined on the flat slot space that the insertion renumbers. It says
the thing Option B wanted is not impossible in principle, because the content
delta really is tiny; it is the flat SLOT NUMBERING that destroys reuse. A split
boundary that did not depend on absolute slot indices -- content-addressed, or
the hierarchy-preserving `DesignCert` section 7 item 4 budgets to Direction 3 --
is where that 0.0265 % could in principle be spent instead of 82 %. Nothing here
builds such a boundary or shows one is affordable.

#### What this is not

Stated plainly, because a single number is easy to over-read:

* **This is ONE edit on ONE block.** It is not a distribution. "A typical delta"
  remains unmeasured; this is one sample, chosen because it is small, real,
  behaviour-relevant, adjacent, and lands on a large block the pipeline already
  supports.
* **It does not make Option A or the loader incremental.** Nothing here reuses
  prior work. Both sides were emitted from scratch. The loader still parses a
  whole certificate every time, and the measurement changes nothing about that.
* **It is not a proof.** It is a measurement of two emitted artifacts, inside
  the same empirical translation-validation boundary as every other table here.
* **The digests do not distinguish the two designs.** Both sides run to
  `7303403973398102700` under the deterministic stimulus. The certificates
  differ and 18 structural records differ, so this says the edit is not
  observable under THAT stimulus -- not that it is a no-op.

#### A binary-identity defect found while running this

The first pass bound its evidence to `bazel-bin/lhd/lhd`, which is what
`resolve_lhd_artifact.sh` exists to warn about: a convenience symlink following
the last-built configuration in an output base shared with concurrent sessions.
Three different hashes came out of one tracked-clean tree during this unit --
`3a2e6dac`, `5209b4af`, `9942dc1d` -- so that path is not an identity, and
`run_d4_runtime_sweep.sh:89` hardcodes it.

Both sides were therefore re-emitted against an artifact resolved for an
explicit configuration, with its hash asserted before AND after each emission.
Every certificate came back BYTE-IDENTICAL to the first pass, so the churn
numbers are unchanged and are now bound to a binary that can be named. The
emitted artifact is reproducible across all three binaries even though the
binaries are not reproducible across builds.

**This is reported, not repaired.** `run_d4_runtime_sweep.sh` is load-bearing
for accepted evidence and this unit was told not to widen; the accepted
canonical-30 CVA6 evidence records `lhd=.../bazel-bin/lhd/lhd` and inherits the
same weakness.

#### Tools and controls

    dcert_delta.py                  read-only, fail-closed DCERT1 A/B analyzer
    dcert_mutate.py                 controls-only mutator (self-checked renderer)
    dcert_delta_controls.sh         20 controls, run against the REAL certificate
    cva6_csr_regfile_dg.sh          the authenticated producer
    cva6_csr_regfile_dg_controls.sh 11 controls on the producer
    cva6_run_gates.sh               the same three gates, CVA6 artifact layout

The analyzer reads the grammar `Compiler.CertIO.renderCert` defines, and refuses
rather than guesses: truncation, a count with no records behind it, a dependency
naming a slot outside the design, an unknown source tag, an undefined operator
code, a non-decimal token, a trailing token, a dependency cycle, a missing
magic, an unreadable file. **Every refusal exits non-zero and publishes
nothing**, because a reader that skipped what it could not parse would report a
SMALLER delta -- the direction that flatters this result.

The load-bearing controls are the ones that pin the two metrics apart:

    EVERY disjoint adjacent swap in the real certificate
                                 ordinal 28,641   structural 0
    one adjacent swap            ordinal      4   structural 0
    one operator retagged        ordinal      1   structural 11,206
    one live dependency moved    ordinal      1   structural  9,216
    one node duplicated          -                structural      1

A renumbering moves the ordinal metric by 28,641 and the structural metric by
nothing; an operator change moves the ordinal metric by 1 and the structural
metric by 11,206. No single metric passes both, which is what makes the 55,643 /
18 split above mean what it says. The duplicate row is its own control: the
added node's signature ALREADY EXISTS, so an analyzer that collapsed duplicates
into a set would report 0 for a design that grew -- it reports exactly 1.

The producer's controls cover the failures that attach a right number to the
wrong experiment: a side that never ran, a row whose `cert_sha256` does not
match its bytes, two different binaries, two different ceilings, a pair that is
not adjacent, the same revision twice, a side that failed a gate, a stale table
left by an earlier run, a directory destination, and an analyzer failure that
the producer must treat as fatal.

Both suites need real artifacts, so neither is a bazel target; they are run as
part of this unit, like `frontend_evidence_controls.sh`.

#### Coverage

Unchanged by this unit: **98/122** focused CORE-ET, **30/30** canonical CVA6.
This measurement produced no new runtime row and no corpus was re-run. The two
`csr_regfile` emissions here are a measurement pair at two CVA6 revisions, not
coverage, and neither is the accepted canonical-30 row -- though side B happens
to reproduce its certificate byte-for-byte.

### Correction: recording a hash is not checking it

Review found three fail-open holes in the unit above. The numbers were
reproduced independently and are unchanged -- 55,643 ordinal / 18 structural --
but the chain that was supposed to make them trustworthy had gaps in it. All
three are reproduced here before being closed.

**1. The producer printed the wrapper hashes and never compared them.** Lines
109-112 read `A_WRAP` and `B_WRAP` and lines 153-154 printed them. Replacing
only B's with `deadbeef` published, exit 0. "Equal wrappers are what licenses
calling the RTL revision the only variable" was a sentence in a comment, not a
check. The producer now ENFORCES equality of the wrapper corpus, the wrapper
GENERATOR (read from each side's foundation manifest, not the sweep
provenance), the config package, and the filelist after normalizing each side's
own root away -- and validates that both retained roots are still at their
recorded revisions and still tracked-clean, recording both verdicts in the
table.

**2. The gate tables were guessed, and the guess was wrong in the real run.**
The producer derived them as `$(dirname RUN)/gates.tsv` and accepted any row
that said PASS. During the actual finalization that picked up the FIRST pass's
tables -- header `# run = temp/dg/A/out` under lhd `5209b4af` -- while the
measurement was of `out2` under `9942dc1d`. The authenticated gate run existed
(`gates2.tsv`) and passed; it simply was not in the published chain. The gate
evidence was real evidence about a different run.

Gate tables are now explicit ARGUMENTS, and `cva6_run_gates.sh` records what is
needed to check one: schema, the run's ABSOLUTE path, the FULL `lhd` hash, the
FULL gate-tool hash, and each row's FULL certificate hash. The producer verifies
every one of those against that side's provenance and against the retained bytes
it is measuring, and requires both sides to have been gated by the same tool.

`cva6_run_gates.sh` also no longer defaults `LHD` to `bazel-bin/lhd/lhd`. It has
NO default and refuses that path even when named explicitly, because it follows
the last-built configuration in a shared output base -- the defect recorded in
the section above.

**3. The analyzer accepted records `renderCert` cannot emit.** Review supplied
three, all of which parsed and published JSON: an input index of `-1`, an
`Op_And` carrying payload `99`, and an `Op_Sum` carrying payload `-1` -- the
last being a record that `opOfCode` would push through `Int.toNat` and read as
a DIFFERENT design than the token says. Each would let the structural metric
separate two records that denote the same design, or merge two that do not.

Every field now has its class: Naturals cannot be negative, only `Op_Const`
carries a signed payload, `Op_Sum` and `Op_MemWriteBE` take Naturals, the other
twenty-six must carry exactly `0`, booleans must be `0`/`1`, and an absent flop
pin must use the canonical slot `0` the writer emits. Tokens must be canonical
decimals: no leading zeros, no `-0`.

Behind all of that is one check that does not depend on the list being complete:
**`read_cert` re-renders what it parsed and refuses unless the bytes match the
input exactly.** `render_cert` lives in the analyzer, is pure, and is now the
single renderer -- `dcert_mutate.py` imports it rather than keeping a copy that
could drift. It catches what no field rule enumerates, such as a double space
inside a record, and it passes on both real certificates.

That also removed a control that could no longer fire: the mutator used to
re-render its input and compare, but `read_cert` now guarantees that before it
returns, so the check was dead and a control that cannot fail is not a control.

#### What the correction did NOT change

The regenerated table is byte-identical to the audited one in **every data
row**. Same 55,643 ordinal churn (81.8833 %), same 18 structural (0.0265 %),
same prefix 11,991 and suffix 0, same counts, from the same retained `out2`
certificates. Only the provenance header changed, and it grew: schema
`d4-cert-delta/2` now carries the enforced wrapper, generator, config,
normalized-filelist and gate-tool hashes, both roots' clean-at-revision
verdicts, both gate table paths, and both runs' absolute paths.

Controls: **27** on the analyzer (up from 20) and **21** on the producer (up
from 11). The new producer negatives are the ones review asked for -- a gate
table about a different run, one produced with a different `lhd`, one from a
different gate tool, one that gated a different certificate, one with no run
line at all, and mismatched wrapper / config / filelist / revision / clean-tree
inputs. Every one exits non-zero and leaves no table.

Two of those controls caught my own errors while being written: a `sed` whose
`#` delimiter also appeared in the `# run` text deleted the header line instead
of repointing it, and a relative workdir made every staged table look like it
was about a different run. Both failed loudly, seventeen at a time, rather than
passing for the wrong reason.

One control had to be split. Corrupting only B's provenance `lhd_sha256` is
caught by the GATE binding, not by the cross-side comparison, because the gate
table still names the real binary. Pinning that case to "the lhd binary" would
have been a control passing on another guard's work, so there are now two: one
that moves the provenance alone, and one that moves the provenance AND the gate
header together so the cross-side check is the only thing left to catch it.

Coverage unchanged: 98/122 focused CORE-ET, 30/30 canonical CVA6.
