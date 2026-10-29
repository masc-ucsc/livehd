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
`HDLProof/Check/Audit.lean`). Drivers: `pass/lean/tools/drivers/sym_verify.lean`,
`sym_refine.lean`. Scripts: `pass/lean/tools/tests/sym_selftest.sh` (small
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
- **No kernel control (found on Lean 4.31).** `decide +kernel` got stuck in
  `Std.Tactic.BVDecide.LRAT.check` even on a 3-clause CNF. A kernel
  control would need an enumeration over primary variables (`enumUnsat`),
  which is not implemented.

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
  `OPT=-O2 ROOT=<dir> bash formal/lean/Futamura_Projection_Scripts/build-opt.sh ../../pass/lean/tools/drivers/sym_verify.lean ../../pass/lean/tools/drivers/sym_refine.lean`.
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
