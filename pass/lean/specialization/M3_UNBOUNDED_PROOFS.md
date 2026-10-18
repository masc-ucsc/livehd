# M3 -- unbounded proofs on the projection simulator

M3 proves facts about every run of a design: an invariant on every reachable
cycle, or a refinement up to stuttering against a second design. Bounded runs
are not enough for that. The proofs go through the same residual simulator the
FP route executes. Each checker is proved sound once, for all designs (the
verified-validator pattern), and then run per design. There are no per-design
Lean proofs.

Lean: `formal/lean/LeanSemanticPrimitives/Projection/Sym/` (17 modules,
Mathlib-free, built by `formal/lean/.build-proj.sh`, every theorem under the
failing axiom gate). Drivers: `formal/lean/scripts/sym_verify.lean`,
`sym_refine.lean`. Scripts: `pass/lean/scripts/sym_*.sh`, `sym_observe.py`.

## Which residual

Every theorem takes the residual through one interface, `Sym.MemResidual D R`:
`R` computes the memory route's step on every admissible input. Three
projections supply it, all proved and all with the same conclusion as
`projectMemDesignAt(W)_correct`:

| route | specializer | interpreter change | `MemResidual` from |
|---|---|---|---|
| `fused` (default) | `MemFused.projectMemFDesignAt` | `env0` inlined, and the node fold fused into its readers | `of_projF` |
| `var` | `MemVariant.projectMemVarDesignAt` | `env0` inlined | `of_projVar` |
| `plain` | `projectMemDesignAt` | none | `of_proj` |

**Why the interpreter is rewritten.** A dynamic `let` makes the specializer
residualize the value it binds. In `mainM`, two such bindings hold slot
lists:
- `env0` (the sources), and
- `env` (the folded nodes).

Every later read of those lists becomes a chain of `tl`s, so the plain
residual is quadratic in slot count. The two rewrites remove the bindings.
- **`MemVariant`** inlines `env0`. This is the scalar route's TOTAL-variant
  rewrite. `SEval_goInlineT` proves, for any body, that the rewrite
  preserves successful evaluation.
- **`MemFused`** runs `mkOutputs`/`flopNexts`/`memNexts` inside the fold's
  base case (`evalNodesThenM` -> `finishM`).
  - `fold_fuse`, by recursion on the fold's derivation;
  - `mainM_transferF`.

Both end in `memVar_iff(W)` / `memF_iff(W)`: on every admitted step, the
rewritten entry evaluates exactly as `hwMemP`'s does. No adequacy lemma of the
memory route is reproved.

| row | slots | plain: residual (specialize) | var | fused |
|---|---|---|---|---|
| B11 | 750 | 214k terms (19 s*) | 33k (0.95 s*) | 8.7k (0.05 s) |
| B12 Pico | 3.7k | 4.98M (70 s) | 1.14M (1.7 s) | (0.4 s) |
| B05 | 20k | unfinished after 5 h | 6.1M (14 s) | 192k (5 s) |
| B01 | 40.6k | unfinished after 8 h | 37M (108 s) | 3.3M (30 s) |
| B08 | 45.6k | unfinished after 5 h | 175M (634 s) | 17.7M (69 s) |

(\* interpreted; the rest native -O2.) What remains in a fused residual is
the reads of FLOP sources: the flop list is dynamic, so a read at index i
costs i `tl`s. For B08's 2,402 flops that is 5.8M of the 17.7M terms.

Admission is the plain-write route `admitsWB` (`HardwareMemWriteAdmit`). It
agrees with `admitsB` on every design without a plain `Op_MemWrite`
(`admitsWB_eq`).

## What a verdict means

**Invariant** (`Sym.runInvKOf_sound_ref`, `KInd.lean`). Suppose the
following hold:
- `MemResidual D R`, from the specializer run;
- `admitsWB D`;
- the checker `runInvKOf D R sp s0 pfB pfS` returns `true`.

Then for EVERY input sequence of the declared widths, and every cycle `n`
whose assumptions held on all cycles up to `n`, the property output of
`interpretDesign` is nonzero (or zero, for `!out`). Here `interpretDesign`
is the shared reference semantics, iterated from `denote s0` with every clock
firing.

The property has these parts:
- one output, nonzero or zero;
- assumptions: an optional output (`asm:`) and input values (`in:name=value`,
  e.g. a reset held);
- the initial state, which is all-zero.

**Refinement with stuttering** (`Sym.runRefKOf_sound_ref`, `Refine.lean`).
Same hypotheses for both designs. On every cycle where the implementation's
witness output `adv` fires, each `pair:` of outputs agrees: the
implementation's `interpretDesign` outputs against the specification's.

The specification here is `refStut`: it steps only on witnessed cycles and
holds otherwise. Its inputs come from the implementation's through `inMap`.
With no `adv` the two run in lockstep. `stateq` also requires equal
(normalized) next states on every cycle, which an identity refinement needs
to be inductive.

**How it is checked.**
1. The residual is evaluated symbolically (`symEval_sound`). Every `let`
   introduces a cut variable (a Tseitin definition), so the bit-blaster sees
   a DAG and not a tree.
2. The k-induction obligations are built: BASE is k steps from `s0`; STEP is
   k+1 steps from a symbolic state of the design's layout.
3. Each obligation becomes CNF through Std's verified bit-blaster (`cnfOf`).
4. An UNTRUSTED cadical run produces a binary LRAT proof.
5. Lean's own LRAT checker checks it (`checkUnsat_sound`).

A satisfiable BASE obligation is a counterexample, decoded from the solver's
model and REPLAYED on the projection's simulators:
- `sym_verify` replays it on one simulator;
- `sym_refine` replays it on both, the specification stepping only on
  witnessed cycles, and reports the failing cycle and each disagreeing pair.

A satisfiable STEP obligation only means the property is not k-inductive at
that k.

## Trust per verdict

- **F1 (the default).** The trust is the compiled evaluation of two things:
  - the specializer run (`projectMemFDesignAt`; `projectMemVarDesignAt` or
    `projectMemDesignAt` with `route:var` / `route:plain`), the same D1
    trusted native run every FP route makes;
  - the check, evaluated through `runInvKOf_of_obls` / `runRefKOf_of_obls`
    over the obligations the driver built for the solver. Those lemmas are
    definitional, so no evaluation is repeated and no trust is added.

  `rebuild` evaluates `runInvKOf` / `runRefKOf` themselves. `closed`
  (sym_verify only) evaluates the closed `runInvKF` (`runInvKV` / `runInvK`
  with `route:var` / `route:plain`), which re-specializes.
- **F2.** `sym_verify ... emit:<file>.lean` writes a theorem
  `invariant := runInvKF_sound verdict`, where `verdict` is proved by
  `native_decide`. It adds exactly one `_native` axiom; `sym_selftest`
  compiles one and checks its axiom list.
- **cadical is never trusted.** The exporter and the observer outputs
  (below) stay the semantic boundary.
- **No kernel control on Lean 4.31.** `decide +kernel` gets stuck in
  `Std.Tactic.BVDecide.LRAT.check` even on a 3-clause CNF. The plan's kernel
  control would need an enumeration over primary variables (`enumUnsat`),
  which is not built.

## Drivers

    sym_verify <cert> [!]<out>[,[!]<out>...] [asm:[!]<out>] [in:<input>=<v>]...
               [k:<n>[,<n>...]] [closed|rebuild] [route:fused|var|plain] [emit:<file>]
               [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]
    sym_refine <impl> <spec> pair:<implOut>=<specOut>... [adv:[!]<implOut>]
               [map:<specIn>=<implIn>]... [asm:[!]<implOut>] [in:<implIn>=<v>]...
               [stateq] [rebuild] [route:fused|var|plain] [k:<n>[,<n>...]] [sf:..] [wf:..]
               [solver:..] [work:..]

- **One specialization per run.** A design is specialized once per run. The
  candidates and the k values are then tried in turn (k-major), and the first
  PROVED wins.
- **Naming ports.** Ports are named through the `<top>_io.json` next to the
  certificate. Specification inputs default to the implementation input of
  the same name.
- **Native builds.**
  `OPT=-O2 ROOT=<dir> bash formal/lean/scripts/build-opt.sh scripts/sym_verify.lean scripts/sym_refine.lean`.
  Pass `solver:` explicitly, or link the toolchain's cadical
  (`$(dirname $(elan which lean))/cadical`, run from `formal/lean`) into the
  build directory.

## Observers: naming what the RTL does not export

A refinement relation or a stutter witness usually depends on internal
signals. `sym_observe.py` writes a COPY of the RTL with added output ports:

    sym_observe.py in.v <module> out.v name:width=expr ... [--decl 'verilog;']

The copy then goes through the normal flow (yosys-slang -> LGraph ->
`single_edge` -> DCERT2). Observers only add outputs, so the next-state
function is unchanged. `--decl` adds sequential observers: a register and
its `always` block, read by a port.

## Results

### 12-row table (`sym_bench12.sh`, one process per row)

Property: a candidate output stays 0 while the functional resets (B05: also
`dft_i`) are held asserted. The other inputs are free.

All 11 rows that have a certificate are proved, every one on the fused route
at k = 1, native -O2, one process per row. Evidence:
`pass/lean/specialization/tests/m3_bench12_results.tsv`.

| row | design | invariant | k | verdict | route | specialize s | symbolic eval s | step clauses | solver s | check s | wall s | peak GB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B01 | CORE-ET DCache | `s3_core_resp_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 25.4 | 225.5 | 48.8M | 32.5 | 137.7 | 650 | 24.7 |
| B02 | CVA6 HPDcache | `dcache_miss_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 23.3 | 28.7 | 63.0M | 32.9 | 450.8 | 592 | 33.1 |
| B03 | CORE-ET I-cache | `f0_l2_miss_req_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 22.3 | 46.1 | 144.6M | 90.3 | 725.7 | 1370 | 71.7 |
| B04 | CVA6 MMU/TLB/PTW | `dtlb_miss_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 7.4 | 32.0 | 61.9M | 35.9 | 456.9 | 594 | 32.4 |
| B05 | CORE-ET ET-Link->AXI | `axi_ar_valid_o = 0` while `rst_ni=0, noc_rst_ni=0, dft_i=0` | 1 | INVARIANT PROVED (k=1) | fused | 5.2 | 11.5 | 256.0M | 158.0 | 1232.4 | 2058 | 138.4 |
| B06 | CVA6 LSU | `load_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 11.7 | 62.5 | 71.6M | 40.7 | 467.8 | 660 | 36.5 |
| B07 | CORE-ET VPU | - | - | no certificate (legalization decision open) | | | | | | | | |
| B08 | CORE-ET IntPipe | `ex_dcache_req_valid = 0` while `rst_ni=0, rst_d_ni=0, rst_w_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 51.2 | 38.1 | 22.1M | 12.2 | 47.2 | 170 | 12.9 |
| B09 | CVA6 issue/scoreboard | `alu_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 3.3 | 13.3 | 290.6M | 154.9 | 1398.3 | 2011 | 142.6 |
| B10 | CVA6 frontend | `fetch_entry_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 3.1 | 6.2 | 59.6M | 35.0 | 452.4 | 576 | 30.1 |
| B11 | CORE-ET TLB | `ptw_req_valid_o = 0` while `rst_ni=0` | 1 | INVARIANT PROVED (k=1) | fused | 0.1 | 0.0 | 0.3M | 0.2 | 0.5 | 1 | 0.2 |
| B12 | PicoRV32 | `mem_valid = 0` while `resetn=0` | 1 | INVARIANT PROVED (k=1) | fused | 0.4 | 0.4 | 0.9M | 0.5 | 1.6 | 4 | 0.5 |

Columns:
- "solver" is cadical on both obligations together.
- "check" is Lean's verified LRAT check through `runInvKOf_of_obls`.
- "symbolic eval" builds both obligations (1 + 2 steps).

The BASE obligation is trivial (2 clauses) on every row, because the reset is
held from the all-zero state.

Earlier routes for comparison:
- B09 on the `var` route: symbolic evaluation 2868 s, wall 5945 s.
- B12 on the `plain` route: wall 743 s.

B08 and B09 ran with `cutLet` (a `let` that binds a variable read or a `tl`
is not re-cut). The other rows ran before it; their symbolic evaluation was
not the bottleneck. Without it, B08's did not finish in 30 min; with it,
38 s.

### PicoRV32 pipeline variants (`sym_pico_variants.sh`)

`base` (TWO_CYCLE_COMPARE=0) and `two_cycle_cmp` (TWO_CYCLE_COMPARE=1: the
branch compare is registered, adding one EXEC cycle per branch). Observers:
- `obs_pc = reg_pc` and `obs_state = cpu_state` on both;
- on `two_cycle_cmp`, the stutter witness
  `sym_adv = !(cpu_state == EXEC && alu_wait)`.

Fused route, native -O2. Evidence: `pass/lean/specialization/tests/m3_pico_variants_results.tsv`, and the replayed counterexample in `m3_pico_stutter_cex.txt`.

| run | statement | verdict | wall s (plain route) | peak GB |
|---|---|---|---|---|
| inv_base | `base`: `trap = 0` while `resetn = 0` | INVARIANT PROVED, k=1 | 3.9 (720) | 0.5 |
| inv_two | `two_cycle_cmp`: `trap = 0` while `resetn = 0` | INVARIANT PROVED, k=1 | 3.6 (779) | 0.5 |
| refl_base | `base` refines `base`: 13 output pairs and equal next states (`stateq`) | REFINEMENT PROVED, k=1 | 63 (1545) | 3.3 |
| stutter_two_base | `two_cycle_cmp` refines `base`, witness `sym_adv`, memory interface + `trap` + `obs_pc` + `obs_state` paired | NOT K-INDUCTIVE at k = 1..7; COUNTEREXAMPLE at k = 8 | 560 (k = 1..6, 8), 290 (k = 7, 8 with replay) | 7.2 |

The k = 8 counterexample, decoded and replayed on both simulators
(`sym_refine`):
- cycle 3 fetches `0x98614363`, a BLT;
- the implementation enters its extra compare cycle (EXEC with `alu_wait`);
- `resetn` drops at cycle 6, on that stutter cycle, where `sym_adv = 0`, so
  the specification holds and never sees the reset;
- at cycle 7 the implementation is back in FETCH (`mem_valid = 0`,
  `obs_state = 64`) while the specification is still in EXEC
  (`mem_valid = 1`, `obs_state = 8`).

The relation needs a witness that also advances the specification on
reset, for example `sym_adv = !(cpu_state == EXEC && alu_wait) || !resetn`,
and then the strengthening below.

### Driving the variant refinement to a proof

The stutter refinement (`two_cycle_cmp` refines `base` with witness
`sym_adv`) checks one relation: the memory interface, `trap`, `obs_pc` and
`obs_state` agree on every witnessed cycle.

- **BASE holds up to k = 7.** There is no counterexample from the all-zero
  state within 7 cycles. At k = 8 there is the reset counterexample above.
- **STEP fails.** The step obligation starts from an ARBITRARY pair of
  states whose paired outputs agreed for k witnessed cycles. Everything the
  relation does not mention is unconstrained: the register file, `reg_op1`,
  `reg_op2`, `reg_out`, the `latched_*` flags, the decoder's `instr_*` and
  `decoded_*`, `mem_state` and `mem_do_*`, and the implementation-only
  `alu_wait` / `alu_out_0_q`. So the two machines can diverge in the next
  step. A larger k alone does not fix this, since memory handshakes can
  stall indefinitely.

Strengthening the relation means exposing and pairing more state with
`sym_observe.py`. One observer can carry a whole bank, for example
`obs_regs:1024={cpuregs[31], …, cpuregs[1], 32'b0}`.

Facts that hold only in the implementation (for example `alu_wait` implies
EXEC) are handled in two steps:
1. Prove the fact as an invariant with `sym_verify`.
2. Use it in `sym_refine` as an assumption (`asm:<observer>`).

`runRefKOf_sound_ref` gives the refinement on every cycle whose assumptions
held. A proved invariant holds on every run from the all-zero state, so the
two statements compose into the refinement on every such run.

Commands:

    STUTTER_K=1,2,3,4 SYM_NAT=<nat> sym_pico_variants.sh certs stutter
    <nat>/sym_refine <impl.dcert> <spec.dcert> pair:...  adv:sym_adv [asm:<obs>] k:<n>,...


## Scale and limits

- **Initial state.** Runs start from the all-zero state, and resets are
  inputs. So "while reset is held, X stays 0" is a statement about every run,
  with reset as an assumption. A property of the RESET state needs an
  assumption on the first cycles, or an observer.
- **Property language.** A property is one output (zero or nonzero), plus an
  optional output assumption and fixed input values. Anything richer is
  written as an observer output (`sym_observe.py`): sequential monitors,
  invariants over internal state, refinement relations.
- **Remaining residual plumbing.** Reads of FLOP sources walk the dynamic
  flop list (see "Which residual"). This costs time, not soundness.
- **No kernel control.** See "Trust per verdict".
- **Out of scope.** Liveness. Stuttering is bounded by k: a witness that holds
  the specification for more than k consecutive cycles needs a larger k.
- **B07** (CORE-ET VPU) has no certificate while its legalization decision
  is open.
