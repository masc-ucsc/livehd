# M3 -- unbounded proofs on the projection simulator

M3 proves facts about every run of a design: an invariant on every reachable
cycle, or a refinement up to stuttering against a second design. Bounded runs
are not enough for that. The proofs go through the same residual simulator the
FP route executes. Each checker is proved sound once, for all designs (the
verified-validator pattern), and then run per design. There are no per-design
Lean proofs.

Lean: `pass/lean/tools/HDLProof/Sym/` (the proof tools, namespace
`HDLProof.Sym`; Mathlib-free, built by `formal/lean/.build-proj.sh` as a second
source root, every theorem under the failing axiom gate in
`HDLProof/Check/Audit.lean`). Drivers: `pass/lean/tools/drivers/sym_verify.lean` (refinement with
stuttering is now `sym_refmap.lean`, see below). Scripts: `pass/lean/tools/tests/sym_selftest.sh` (small
fixtures only), `pass/lean/tools/scripts/sym_observe.py`.

This repository holds the machinery only. Properties of a particular RTL
design, and their proof artifacts, live with that design, not here.

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

**Refinement with stuttering** is checked by `sym_refmap` with a refinement
map (`runRefMapOf_sound`; the format and targets are in
`pass/lean/tools/formats/refmap.md`). M3's own refinement driver,
`sym_refine` (k-induction over a lockstep or witness-stuttered pair of
designs with output pairs, `Sym.runRefKOf_sound_ref` in `Refine.lean`), is
covered by `sym_refmap` and has been removed with its checker. The two
literal helpers `sym_refmap` used from it, `eqBvE` and `advLit`, are now
`HDLProof/Sym/BvLit.lean`.

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
model and REPLAYED on the projection's simulator (fp's, whatever the
symbolic route).

A satisfiable STEP obligation only means the property is not k-inductive at
that k.

## Trust per verdict

- **F1 (the default).** The trust is the compiled evaluation of two things:
  - the specializer run (`projectMemFDesignAt`; `projectMemVarDesignAt` or
    `projectMemDesignAt` with `route:var` / `route:plain`), the same D1
    trusted native run every FP route makes;
  - the check, evaluated through `runInvKOf_of_obls`
    over the obligations the driver built for the solver. Those lemmas are
    definitional, so no evaluation is repeated and no trust is added.

  `rebuild` evaluates `runInvKOf` itself. `closed`
  (sym_verify only) evaluates the closed `runInvKF` (`runInvKV` / `runInvK`
  with `route:var` / `route:plain`), which re-specializes.
- **F2.** `sym_verify ... emit:<file>.lean` writes a theorem
  `invariant := runInvKF_sound verdict`, where `verdict` is proved by
  `native_decide`. It adds exactly one `_native` axiom; `sym_selftest`
  compiles one and checks its axiom list.
- **cadical is never trusted.** The exporter and the observer outputs
  (below) stay the semantic boundary.
- **No kernel control (found on Lean 4.31).** `decide +kernel` got stuck in
  `Std.Tactic.BVDecide.LRAT.check` even on a 3-clause CNF. A kernel
  control would need an enumeration over primary variables (`enumUnsat`),
  which is not implemented.

## Drivers

    sym_verify <cert> [!]<out>[,[!]<out>...] [asm:[!]<out>] [in:<input>=<v>]...
               [k:<n>[,<n>...]] [closed|rebuild] [sim:fp|ir|vc] [route:fused|var|plain]
               [emit:<file>] [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]

`sim:` picks the symbolic step: `vc` (the default), `ir` or `fp`. The F1/F2
discussion above is fp's. `closed` and `emit:` work on every route: on ir and
vc the closed check is `Sym.runInvKOn .ir` / `.vc` (`runInvKOn_sound`), which
rebuilds the route's step and uses its soundness theorem (`Sym.Sim.stepper_sound`:
`irStepper_sound`, `vcStepper_sound`) where fp uses the specializer's. `route:` is
fp's alone. A run naming `closed`, `emit:` or `route:` and no `sim:` uses fp.

- **One setup per run.** A design's route is set up once per run (fp's
  specialization, vc's compile). The candidates and the k values are then
  tried in turn (k-major), and the first PROVED wins.
- **Naming ports.** Ports are named through the `<top>_io.json` next to the
  certificate.
- **Native builds.**
  `OPT=-O2 ROOT=<dir> bash formal/lean/Futamura_Projection_Scripts/build-opt.sh ../../pass/lean/tools/drivers/sym_verify.lean ../../pass/lean/tools/drivers/sym_refmap.lean`.
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

## What each route evaluates symbolically

A route evaluates one step of the design on symbolic values. Where it has no symbolic semantics
it refuses: the run stops with an error, and never guesses. Each refusal message names a class.
`sym_probe <cert> sim:fp|ir|vc|all [census] [diag]` runs one symbolic step without SAT and prints
the class, so a design's coverage can be checked before a proof run.

**fp** (`Sym/Prim.lean`, `Sym/Dispatch.lean`, `Sym/Eval.lean`; each sound against `evalPrim` /
`Eval`):
- Integer primitives on symbolic operands: `addI`, `subI`, `mulI`, the comparisons, and `divI` /
  `modI`. `divI` / `modI` follow `evalPrim`: Euclidean division, and 0 when the divisor is 0.
- `bvBit` with a symbolic position; `bvGetMask` with a symbolic mask.
- `eqV` on two values whose bit vectors are normalized: every computed result and constant
  (`.nbv`). Comparing the raw value of an input, a held register or a stored memory word is
  refused (`unnormalized-bv`), unless the two shapes already decide the answer.
- `mkCtorP`, `ctorTagP`, `ctorFieldsP`; `ctorFieldsP` of a raw bit vector is refused
  (`unnormalized-bv`).
- `ite` whose two branches have different shapes gives a guarded union (`.alt`). A primitive or
  a `caseT` applied to a union is evaluated per branch, up to 6 nested unions, and the results
  are merged. For a primitive, a branch with concrete operands on which the concrete primitive
  fails is dropped (no successful run takes it). A union may have at most 64 leaves
  (`resource-limit`).
- Refused: a width or a constructor tag computed from data (`data-dependent-width`,
  `symbolic-tag`); an `ite` whose condition is not a Boolean.

**ir, vc** (`Sym/NativeBuilders.lean`, sound against `eval_op`): every operator the admission
check accepts, including `Op_Mult`, `Op_UDiv`, `Op_SDiv`, and `Op_GetMask` / `Op_SetMask` with a
mask that is not a constant (rank circuits). `Op_Sub`, `Op_Div`, `Op_LT` and `Op_GT` are refused
at admission; the exporter emits none of them.

## Scale and limits

- **Initial state.** Runs start from the all-zero state, and resets are
  inputs. So "while reset is held, X stays 0" is a statement about every run,
  with reset as an assumption. A property of the RESET state needs an
  assumption on the first cycles, or an observer.
- **Property language.** A property is one output (zero or nonzero), plus an
  optional output assumption and fixed input values. Anything richer is
  written as an observer output (`sym_observe.py`): sequential monitors,
  invariants over internal state, refinement relations.
- **Residual plumbing.** Reads of FLOP sources walk the dynamic
  flop list (see "Which residual"). This costs time, not soundness.
- **No kernel control.** See "Trust per verdict".
- **Out of scope.** Liveness. Stuttering is bounded by k: a witness that holds
  the specification for more than k consecutive cycles needs a larger k.
