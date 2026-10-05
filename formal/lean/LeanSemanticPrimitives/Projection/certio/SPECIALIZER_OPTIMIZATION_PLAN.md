# Specializer optimization: four options, and what has to be measured first

Companion to `PERFORMANCE_INVESTIGATION_PLAN.md`, which owns priority (coverage
first) and the evidence rules.  This document is only about the four candidate
representation changes and how to sequence them.

## 0. Status

Two `rt_alu_gate` probes were terminated by the operator after 2 d 13 h and
2 d 12 h, both still inside specialization, both `-O0`.  **Terminated, not
failed**: no result, and they add nothing to any count.  Their question -- "how
slow is the unoptimized path on the ALU?" -- was answered more cheaply and
under control by the P1 ladder.

## 1. What is measured, and what is not

MEASURED (P1, both backends in one process, same source/flags/input/budgets,
`-O2`, residual sizes identical at every point):

| | exponent in SOURCE count, nodes fixed at 64 |
|---|---|
| proved `mixDriver` | **2.02** -- quadratic |
| fork `ProtoFast.mixDriver` | **1.17** -- near-linear |

MEASURED (section 7.3): the ALU's total slot-read walk is **8,179,105 steps**.

MEASURED: the fork specialized the ALU in **1,226 s**; the proved path at `-O2`
took **61,267 s**.

**NOT MEASURED, and this is the crux.**  8.18M steps at O(1) each is well under
a second.  So the positional walk **cannot** be what costs the fork 1,226 s.
It dominates the PROVED path only because each step there costs O(spine)
instead of O(1).

> **Consequence: the premise behind option P-A is currently unverified for the
> fork.**  P-A removes the walk.  On the proved path that is the quadratic.  On
> the fork -- the lineage any promoted optimization would actually build on --
> the walk is under a second of an 1,226 s run, so P-A as justified today would
> optimize something that is already negligible there.
>
> Nothing below should be implemented until P0 says what the fork's time is.

## 2. P0 -- the blocking measurement

Two experiments, both bounded, neither a multi-hour real-design run.

**P0.1 -- profile the FORK.**  A `perf` sample WITH call chains (`-g`), on a
fixture large enough to sample properly but small enough to finish in a few
minutes.  The P1 ladder gives the sizing: fork at `nsrc=4096, nodes=64` is
2.6 s, so scale nodes up until the run is 60-120 s.  Required: call chains, so
helper time can be attributed to a caller -- the earlier ALU sample had none,
which is why it could only say three functions were *active*.
Record inclusive and self costs separately; never sum overlapping inclusive
values.

**P0.2 -- the combined ladder.**  Every ladder so far holds one axis fixed.
Scale sources AND nodes together, both backends, and fit the exponent.  This
settles whether the fork is still quadratic in design size (predicted) or
effectively linear (which would shrink P-A's payoff to nothing).

**Decision rule.**  Implement the option P0.1 names.  If P0.1 shows the fork's
cost is dominated by something none of P-A..P-D addresses, write that down and
propose a fifth option rather than implementing one of these because it is
already drafted.

## 3. The four options

Throughout: "proof obligation" means what has to be discharged before the
change can be on the proved path, not before it can be measured.  All four can
be prototyped in the fork, where nothing is proved, and measured there first.

### P-A -- an indexable case in `PVal`

**Mechanism.**  `PVal` gains `vec : Array PVal → Nat → PVal` (array plus start
offset), denoting the cons-chain `arr[start:]`.  `peelHd` is `arr[start]`,
`peelTl` is `(arr, start+1)`, `peelIsNil` is `start ≥ arr.size` -- all O(1).

**What it does and does not fix.**  It makes each peel step O(1).  It does NOT
by itself make a slot read O(1): `nthD` is an inline FUNCTION, so the
specializer unfolds it `k` times and performs `k` O(1) peels.  Getting O(1) per
READ needs the specializer to recognise a static index and jump -- which means
either making the slot read a primitive (that is P-D) or adding an ad-hoc rule
that recognises `nthD`'s unfolding shape.  **The second is fragile and is not
recommended.**

So P-A's honest claim is: it reduces per-step cost on the PROVED path from
O(spine) to O(1), i.e. it reproduces what `PRes.val` already achieves.  If the
fork already peels in O(1), **P-A adds nothing to the fork.**

**Two implementation hazards.**
1. `PVal.shift k (vec arr i)` is O(n) if it maps over the array.  It must carry
   a PENDING SHIFT in the constructor and push it into leaves on read, or the
   quadratic returns through shifting.
2. `evalNodes` appends one entry per node.  Append must be O(1) amortised
   (Lean `Array.push` under unique ownership) or the build becomes quadratic.

**Proof obligation.**  Same class as `PRes.val`: a `PVal` representation
bridge.  If designed alongside `PRes.val`, promoting fork+P-A is ONE bridge,
not two.  Specializer-only: the object language, `I_hw`, `interpretDesign` and
`IHwAdequate` are untouched.

**Acceptance.**  `--env0-ab` and `--inline-negative` still exit 0; residual
sizes byte-identical to the current fork on every ladder point and on at least
two real blocks; P1 ladders re-run with the exponent recorded.

### P-B -- de Bruijn LEVELS instead of indices

**Mechanism.**  A residual variable's identity stops depending on how many
binders enclose it, so `PVal.shift` disappears entirely rather than being
special-cased at zero.

**Predicted effect.**  Small on time: shifting was 61% before the zero-shift
fix and ~10% after.  The real argument is correctness hygiene -- it removes the
index-arithmetic bug class that already produced the `wrapLets` bug on this
branch.

**Proof obligation.**  Large.  Touches `Eval`, every lemma mentioning a
variable, `resolve`, and the encoders.  This is the single most invasive option.

**Recommendation.**  Do not do this for speed.  Consider it only if the bug-class
argument wins on its own, and never in the same increment as P-A -- both touch
`PVal` and shifting, and entangling them makes one proof out of two.

### P-C -- hoist constant sources out of the RESIDUAL

**Mechanism.**  `srcVal` of a `.const` resolves without occupying an
environment slot, so constants never appear in the residual's rebuilt spine.

**Evidence it is worth doing.**  Measured: the residual rebuilds the whole
environment, `consP = nSources + nNodes + nFlops + 1`.  And constants are
**92.7-100% of sources in every one of the 30 blocks**, a uniform **42.5-49.4%
of total spine entries**.  So this is a ~2x cut in residual size on every
design, not a one-off.

**What it does NOT do.**  It does not help specialization time.  A constant at
index 5,000 still costs a 5,000-step walk; constant-ness is not the issue,
positional addressing is.

**Proof obligation.**  It changes `I_hw`, so `IHwAdequate_proved` must be
re-established.  `main_agree` is a `refine` script over the body; changing
`mkSources`/`srcVal` changes the `mkSources_agree` step and the slot numbering
every later lemma depends on.  Non-trivial but bounded, and the B1/B2 work
showed how such a transport is done.

**Why it may still be first in value terms.**  It is the only option that moves
the project toward the plan's stated acceptance criterion -- a residual with
"no runtime slot environment at all" -- which no amount of specializer speed
achieves.

### P-D -- an object-language array with a static-index primitive

**Mechanism.**  `Val` gains an indexed case and the object language gains a
`slotGet`-style primitive whose static-index read the specializer resolves in
O(1).  `I_hw` uses it instead of `nthD`.

**Predicted effect.**  The only option that makes a slot READ O(1), so the only
one that removes the Σ-depth term outright.

**Proof obligation.**  The largest of the four.  A new `Val` constructor
touches the semantics, `evalPrim`, the encoder/decoder and round-trip proofs,
and every proof that cases on `Val` -- plus `IHwAdequate`.  It also changes the
residual's shape, which may conflict with the legacy straight-line-`let` goal.

**Relationship to P-A.**  **Alternatives, not complements.**  P-D subsumes
P-A's benefit.  Choose P-A if O(1) per STEP suffices; choose P-D only if P0
shows the Σ-depth term is what dominates.

## 4. Sequencing

    P0.1 profile the fork (call chains)        BLOCKING
    P0.2 combined sources+nodes ladder         BLOCKING
      |
      +-- if Σ-depth dominates the fork  -> P-D  (and P-A is subsumed)
      +-- if per-step cost dominates     -> P-A
      +-- if something else dominates    -> write it down, propose P-E
      |
    P-C independently, on its own merits (residual shape + 2x size)
      |
    P-B last, or never

One representation change at a time, per `PERFORMANCE_INVESTIGATION_PLAN.md`
step 4.  Each change: prototype in the fork, measure on both ladders and at
least two real blocks, confirm residual-size identity and reference agreement,
THEN consider the bridge.

## 5. What would make each option not worth doing

| option | abandon it if |
|---|---|
| P-A | P0.1 shows the fork already peels in O(1) and spends its time elsewhere -- then P-A only helps a path we are replacing |
| P-B | the bug-class argument does not hold up; the time win is ~10% and the blast radius is the whole variable treatment |
| P-C | measurement shows the residual rebuild is not a cost anyone pays (it is linear, and may simply not matter) |
| P-D | P0 shows Σ-depth is small, as the 8.18M-step figure already hints -- then its proof cost buys little |

## 6. Standing constraints

Nothing here is proved.  All four live in the fork lineage, which has no bridge
(obligation A).  None of them changes any count: coverage is
**8 differential, 8 with no known reset conflict**, and the sequential blocks
remain blocked on the exporter reset defect, which is a separate handoff and is
not affected by anything in this document.
