# Phase 6 specialization performance — native diagnostic, then the measured cause

Raw profiler output, the exact commands, the build flags, the toolchain and the
repository revision are under `certio/perf-artifacts/`. Note the toolchain trap
recorded there: `lean` resolves PER DIRECTORY, `formal/lean/lean-toolchain`
pins v4.31.0, and the repository root gets elan's default (v4.34.1 here)
instead. Every number in this file is v4.31.0.

Reproduce:

    bash formal/lean/scripts/build-native.sh          # builds .native-dev/perf_probe
    ./formal/lean/.native-dev/perf_probe 64 128 256 512 1024
    valgrind --tool=callgrind --callgrind-out-file=cg.N ./.native-dev/perf_probe N

`scripts/perf_probe.lean` repeats `chainD` locally rather than importing
`Projection/Scaling.lean`, so the probe's dependency closure stops at
`ProjectedStep`; Scaling is 41 s of `#guard`s irrelevant to this measurement.

## 1. Native vs interpreter — a constant factor, not an exponent

    n       interpreted      native    speedup    residual size   bound    RSS
      64             --      209 ms         --             993     197   10.5 MB
     128             --     1163 ms         --            1953     389   10.2 MB
     256       14212 ms     7609 ms      1.87x            3873     773   12.3 MB
     512      101480 ms    54701 ms      1.86x            7713    1541   17.0 MB
    1024     >600000 ms   416600 ms      >1.4x           15393    3077   27.1 MB

Two things this settles:

* **It does not touch the TIME wall.** A flat ~1.87x. The native wall-time
  ratios per doubling are 5.6, 6.5, 7.2, 7.6 -- exponents 2.48, 2.70, 2.85,
  2.93, converging on CUBIC.
* **It is not memory.** 27 MB at n = 1024, growing linearly.

CORRECTION.  An earlier revision of this file said the interpreted `n = 1024`
run failed from stack depth.  It did not: it TIMED OUT at 600 s with no stack
error.  The `deep recursion was detected at 'interpreter'` failure was observed
on `rt_intpipe_alu` (5118 nodes), and the two were conflated.  The experiment
below was run to settle it properly.

### 1b. Interpreter stack: what was OBSERVED

A stack overflow fails in well under a second; a slow run does not. `chainD`,
interpreted, 120 s budget:

    n = 1024   TIMED OUT at 120 s      -- outcome UNKNOWN
    n = 2048   TIMED OUT at 120 s      -- outcome UNKNOWN
    n = 2560   stack overflow (deep recursion at 'interpreter')
    n = 3072   stack overflow
    n = 4096   stack overflow
    n = 8192   stack overflow

A TIMEOUT IS NOT A PASS.  The 1024 and 2048 rows say only that no stack error
appeared within 120 s; they do not say the run would have completed, and they
do not establish a threshold.  What is established is that `chainD` overflows
at 2560 and above.  Nothing here generalises to other designs: `chainD` is one
shape, and recursion depth depends on the design.

Natively, `chainD` n = 4096 ran 120 s with no stack error (again: observed
absence within a budget, not a completion).  `rt_intpipe_alu` DID produce the
interpreter stack error, which is the one real-design observation.

An earlier revision of this file said the interpreted n = 1024 run failed from
stack depth.  It did not -- it timed out.  The stack error was on
`rt_intpipe_alu`, and the two were conflated.

### 1c. A FOURTH wall: the hardcoded specialization fuel

`projectDesign` is `mixDriver 20000 200`.  Natively, with per-stage reporting
and a 25 s budget:

    n = 4096   no fuel error within 25 s   -- outcome UNKNOWN (timeout)
    n = 5118   no fuel error within 25 s   -- outcome UNKNOWN (timeout)
    n = 6144   no fuel error within 25 s   -- outcome UNKNOWN (timeout)
    n = 7168   projectDesign FAILED (MixError.outOfFuel) after 100 ms
    n = 8192   projectDesign FAILED (MixError.outOfFuel) after  93 ms

OBSERVED: `chainD` at 7168 and 8192 exhausts the hardcoded step fuel, fast and
honestly -- an `.error`, not a wrong answer.

NOT ESTABLISHED: that the ceiling lies between 6144 and 7168; the smaller rows
are timeouts and say nothing.  NOT ESTABLISHED: that `rt_intpipe_alu` cannot
hit the fuel limit.  Its node count is below 7168, but fuel consumption depends
on the design's shape and not on node count alone, and `chainD` does not
establish any other design's fuel behaviour.  What stopped `rt_intpipe_alu` in
the recorded run was the interpreter stack, before fuel was ever reached.

## 2. The concrete cause — call counts, not wall time

`callgrind`, exact call counts at three sizes, exponent per doubling:

    function                      n=32      n=64     n=128   e(32->64)  e(64->128)
    PVal.shift   (worker)        87804    527484   3512188       2.59        2.74
    PVal.toPRes  (worker)        43868    263772   1756252       2.59        2.74
    prepare      (worker)        40276    248340   1692564       2.62        2.77
    PVal.shift   (entry)         29906    147250    914418       2.30        2.63
    List.lengthTR                25478    137510    887910       2.43        2.69
    ---- everything else ----
    List.reverseAux              43248    120912    399120       1.48        1.72
    List.appendTR                20943     59471    197967       1.51        1.74
    mixTerm                      19772     57724    195068       1.55        1.76
    mixTerms                     13766     42630    149510       1.63        1.81
    mixPArgs                      6686     18814     61502       1.49        1.71
    PEnv.shiftBy                  7840     17440     48928       1.15        1.49
    evalPrim                      3854     11406     38798       1.57        1.77

A clean separation: five symbols grow at ~2.7 and rising; everything else is at
or below 1.81. `mixTerm` itself is 1.76 -- the term walk is NOT the problem.

`perf` self-time on the same run agrees: the only Lean symbols above 2% are
`prepare` (4.11%), `PVal.shift` (4.11%) and `PVal.toPRes` (2.06%), with the rest
of the profile in `lean_is_ctor` / `lean_ctor_get` / `lean_alloc_ctor`, i.e.
structural traversal and allocation inside exactly those three.

### Where it is, in the source

`PartialEvaluator.lean`, `prepare`'s cons case:

```
  | .cons a b =>
    match prepare a, prepare b with
    | ⟨[], va⟩, ⟨bb, vb⟩ => ⟨bb, .cons (PVal.shift bb.length va) vb⟩
    | ⟨ba, va⟩, ⟨[], vb⟩ => ⟨ba, .cons va (PVal.shift ba.length vb)⟩
    | _,        _        => ⟨[PRes.toCode (.cons a b)], .dyn 0⟩
  | .lets bs r =>
    let p := prepare r
    ⟨bs ++ p.binds, p.value⟩
```

Three compounding costs, all here:

* **(A) the second arm re-shifts the WHOLE TAIL.** The slot environment is
  `cons <new node> <old env>`. The new node is `.code t`, which prepares to
  `⟨[t], .dyn 0⟩` -- a NON-empty binding list -- while the old environment is
  all `.dyn` leaves and prepares to `⟨[], vb⟩`. So the first arm cannot match
  and the second does, shifting the entire accumulated environment by
  `ba.length` once per node. `PVal.shift` then walks an O(n) spine, n times.
  This is the dominant term and it is why `PVal.shift` and `prepare` grow
  together at the same exponent.
* **(B) `bb.length` / `ba.length` are recomputed at every spine level.** That
  is the 888k `List.lengthTR` calls at n = 128, growing at 2.69 -- a length
  recomputed inside the traversal it is driving.
* **(C) `bs ++ p.binds` in the `lets` case** is O(|bs|) per level, giving the
  `List.appendTR` term at 1.74.

`PVal.toPRes` tracks the same shape for a different reason: the `var` rule
reads an environment entry back as a result, and for this environment that
entry is the growing spine, so each variable read converts an O(n) structure.

### What this is NOT

* not `mixTerm`/`mixTerms` (1.76/1.81),
* not the request machinery -- `Divs.update` is FLAT (10705 at both n = 32 and
  n = 64), so `closeFM`'s chaotic iteration is converging immediately and is
  not re-specializing,
* not `evalPrim` (1.77), not `PEnv.shiftBy` (1.49),
* not memory.

## 2b. CORRECTION — call counts are not costs

The table above reports CALL COUNTS, and the first write-up of it treated the
`List.lengthTR` row (888k calls, exponent 2.69) as a cost driver.  That was
wrong, and the error was mine: a count was never converted to a cost.

`callgrind_annotate --inclusive=yes`, n = 128, 10,198,578,991 Ir total.  These
three are non-recursive, so their inclusive figures are exact:

    l_List_lengthTR___redArg        14,336,547    0.14%
    l_List_appendTR___redArg        10,037,924    0.10%
    l_List_reverseAux___redArg       8,596,877    0.08%
                                    ----------   ------
                                                  0.32%

The counts are high because they are called once per cons level; each call is
on a SHORT binding list, so the total is negligible.  **Fix (B) -- carrying the
binding count in `Prepared` -- is bounded above by 0.14%, and fix (C) by
0.10%.**  Neither touches the exponent.

Where the time actually is (inclusive; the entry wrappers, not the
self-recursive workers whose inclusive figures are recursion-inflated and shown
above 100%).

**INCLUSIVE COSTS OVERLAP AND MUST NOT BE SUMMED.** `prepare` at 39.90% and
`PVal.shift` at 27.11% share every instruction `PVal.shift` executes when
called from `prepare`; adding them double-counts. They are listed to rank the
candidates, not to partition the runtime. The SELF-cost figures below them are
the disjoint, addable ones.

    l_Projection_prepare            4,068,867,144   39.90%
    l_Projection_PVal_shift         2,765,326,736   27.11%
    l_Projection_PVal_toPRes        2,010,693,065   19.72%
    l_Projection_PEnv_shiftBy       1,229,996,466   12.06%
    l_Projection_PRes_toCode'2        960,671,716    9.42%

and the top of the SELF profile is the un-inlined Lean runtime accessors that
those functions call -- `lean_is_ctor` 14.9%, `lean_ptr_tag` 10.0%,
`lean_ctor_obj_cptr` 6.9%, `lean_ctor_num_objs` 6.5%, `lean_to_ctor` 6.1%,
`lean_ctor_get` 5.6%, `lean_ctor_set` 4.9% -- i.e. structural traversal and
allocation, roughly 55% of the program, performed inside exactly those four.

Two further facts from the same run:

* `collectFor`/`collectFrom`/`closeFM` 49.72% and `generateFrom` 49.71`%`: the
  specializer walks the term TWICE, once to discover requests and once to
  generate.  That is by design, and it is a factor of 2, not an exponent.
* `prepare` is entered 19,876 times at n = 128 -- about 1.2 n^2 -- and each
  entry walks ~85 `PRes` nodes.  The cubic is the PRODUCT of those two growing
  together, not any single list operation.

So the asymptotic cost is re-traversal and re-shifting of the partially-static
environment: indices are relative to the current binder depth, so every binder
owes a shift and every environment read owes a conversion.  Removing that needs
a representation change (de Bruijn LEVELS, or a suspended shift), not a
bookkeeping change.

## 4. What the smallest representation change would buy, from the real counts

The candidate, and the smallest one the evidence supports:

> let `PRes` carry a PURE `PVal` directly, so `prepare` and `PVal.toPRes` on it
> are O(1) instead of structurally rebuilding the spine.

### Which traversals it removes

The inner loop, as measured: each node does O(1) slot reads at depth Theta(n);
each read unrolls `nthD` Theta(n) times; and EVERY unroll step calls `prepare`
on the environment result, which for a spine of pure `dyn` leaves walks and
REBUILDS the whole spine.

  REMOVED
    * `prepare` on a pure spine, Theta(n) structural copy  ->  O(1)
    * `PVal.toPRes` on a pure spine, Theta(n) copy         ->  O(1)
      (the `var` rule stops converting and hands the `PVal` over as-is)

  NOT REMOVED
    * `PVal.shift`: indices stay relative to binder depth, so entering a binder
      still shifts.  That is the de Bruijn LEVELS change -- separate, larger.
    * the Theta(n) unrolling of `nthD` per slot read.  The RESIDUAL is already
      free of it (Phase 1); this is specialization-time cost only.

A SUSPENDED SHIFT WOULD REMOVE NEITHER of the two that are removed above.  It
targets `PVal.shift`, which is the 27% term, not the n^3 term.  That is why it
is not the proposal.

### The projected exponent, from measured counts

At n = 128: `prepare` is entered 19,876 times (~1.2 n^2) and each entry walks
~85 `PRes` nodes (~0.66 n).  So `prepare`'s own work is ~0.8 n^3 -- it IS the
cubic term.  Everything else measured is quadratic or below: `PVal.shift` is
~56 n^2 entries x ~3.8 steps, `mixTerm` ~11.9 n^2 steps.

Making `prepare` and `toPRes` O(1) per entry leaves 1.2 n^2 entries x O(1), so
the predicted result is

    n^3  ->  n^2

and, at the measured native constant, n = 1024 would fall from 417 s to the
order of a second, with `rt_intpipe_alu`'s 5118 nodes landing in tens of
seconds rather than the extrapolated ~14 hours.

THIS WAS A PROJECTION FROM CALL COUNTS.  Section 5 measures it, and REFUTES it.

`scripts/repr_prototype.lean` reported 0 ms (ms, not microseconds -- an earlier
revision of this file said "0 us", which was wrong) for both sides.  The
diagnosis given then -- that the calls were deleted -- was ALSO wrong.  The
generated C shows why: in `.native-dev/repr_prototype.c`, `l_bench` takes all
three timestamps (lines 1431, 1435, 1439) and only then calls `l_loop` (1450,
1452).  Lean reordered the pure work to AFTER the final timestamp.  `l_walkA`
still calls `toPRes` and `prepare`; the work happens, outside the interval.

The lesson is about measurement, not about the compiler: in-process timestamps
around PURE work are not trustworthy in Lean.  Section 5 therefore times
SEPARATE PROCESSES.

### Cost of doing it for real

`prepare` is named in `prepare_ok`, `prepare_sound`, `prepare_peel`,
`prepare_cons_split`, `prepare_cons_join_left`, `prepare_cons_join_right`,
`prepare_total_binds` and `prepare_hot_path`; `PRes` gains a constructor, so
`PResOK`, `PResSound`, `PRes.total`, `PRes.toCode`, `primStruct` and the peel
lemmas all gain a case.  The recommended shape is the one the audit asked for:
keep the current specializer as the SEMANTIC REFERENCE and prove a
representation/operation bridge to the optimized one, rather than weakening any
existing theorem.  Guarded discards, binding order, scope and object-specializer
agreement all have to be preserved across that bridge.

## 5. MEASURED: the representation change, end to end -- hypothesis REFUTED

`Projection/Proto/PartialEvaluatorFast.lean` is a diagnostic fork of the
specializer: a copy in namespace `Projection.ProtoFast` with `PRes` gaining
`| val : PVal → PRes`, the `var` rule returning `.val (.cons a b)` instead of
copying the spine, `prepare (.val v) = ⟨[], v⟩`, `PRes.total (.val _) = true`,
and the three peels answering from the spine.  Nothing else differs.  It is NOT
proved and is not in the core build or the axiom audit; the verified
specializer remains the semantic reference.

### Residuals are identical

Real `Hw.projectDesign` versus the fork, same `chainD`:

    n = 64    ref size 993    fast size 993    identical true   same-result true
    n = 128   ref size 1953   fast size 1953   identical true   same-result true
    n = 256   ref size 3873   fast size 3873   identical true   same-result true

`identical` is `==` on the shared `Program`; `same-result` runs both residuals
through `evalFuel` on the same stimulus and compares `encResult`.  Entry arity
is 3 on both sides.  So the change is shape- and semantics-preserving on this
workload -- which is evidence, not a proof.

### Timing: separate processes, clean build

    n        reference      fast     speedup
      64        0.23 s     0.17 s      1.35x
     128        1.21 s     0.62 s      1.95x
     256        7.99 s     3.50 s      2.28x
     512       56.41 s    22.65 s      2.49x
    1024      430.24 s   162.02 s      2.66x

Exponent per doubling:

    reference   2.40  2.72  2.82  2.93
    fast        1.87  2.50  2.69  2.84

**THE HYPOTHESIS n^3 -> n^2 IS REFUTED.**  The fork is a growing CONSTANT
factor -- 2.66x at n = 1024 -- and its exponent is still converging on 3.  The
"seconds on the ALU design" hypothesis goes with it: extrapolating the fast
side to 5118 nodes gives hours, not seconds.

### Why, exactly

callgrind on the fork, inclusive:

    prepare          1.22%   (was 39.90%)
    PVal.toPRes      absent  (was 19.72%)
    PVal.shift      61.28%   (was 27.11%)   exponent 2.74
    PEnv.shiftBy    37.75%   (was 12.06%)

The change did exactly what it claimed: its two targets are gone.  But
`PVal.shift`'s call count is UNCHANGED between the two -- 527,484 -> 3,512,188
(reference) against 527,140 -> 3,511,844 (fork) -- and it was ALREADY growing
at 2.74.  There were two independent cubic terms; removing one leaves the
other, now exposed as 61% of the runtime.

So the next target is `PVal.shift` / `PEnv.shiftBy`, i.e. de Bruijn LEVELS
instead of indices, which was correctly identified earlier as separate and
larger.  Whether THAT removes the exponent is an open question, to be measured
the same way -- a fork first, a proof only if the measurement justifies it.

### Bearing on the verified path

Nothing is promoted.  On these numbers the change does not justify a broad
proof rewrite on performance grounds: 2.66x does not reach any design that the
current specializer cannot already handle, and it does not move the asymptote.
If it is ever bridged in, the shape the audit prescribed still applies -- keep
the proved specializer as the semantic reference and prove a
representation/operation bridge -- and the obligations are unchanged: guarded
discards, binding order, scope, object-specializer agreement, axiom audit.

## 6. MEASURED: the zero-shift fast path -- the small fix that DID move the exponent

`PVal.shift 0 v` and `PEnv.shiftBy 0 env` were fully recursive: they walked the
whole value / whole environment to rebuild something identical.  `mixPArgs`
calls both with k = 0 whenever `prepare` emitted no bindings, which on this
workload is most of the time.  Added to the FORK as O(1) arms, k > 0 untouched:

    PVal.shift   : | 0, v => v   | _, .stat v => ...   | k, .dyn i => ...
    PEnv.shiftBy : | 0, env => env   | _, [] => []   | k, v :: rest => ...

Extensionally the identity, and the PROVED reference already states exactly
these two equations -- `PVal.shift_zero` (PartialEvaluatorCorrect:76) and
`PEnv.shiftBy_zero` (:88) -- so the bridge, if this is ever promoted, is those
two lemmas and nothing else.

### Equivalence, three shapes

Real `Hw.projectDesign` against the fork, `==` on the shared `Program` plus a
decoded one-cycle run of each residual:

    chain  n = 64/128/256   sizes 993 / 1953 / 3873        identical, same-result
    fan    n = 64/128/256   sizes 1056 / 2080 / 4128       identical, same-result
    flop   n = 64/128/256   sizes 13985 / 52513 / 203297   identical, same-result

`flop` is the sequential shape -- a flop per node, so the edge vector and the
commit path are exercised, not only the combinational environment.  (Its
residual is itself quadratic in n; that is the pre-existing flop-state
quadratic recorded in Phase 1, not something this change causes.)

LIMIT OF `same-result`: `runOne` drives TWO 4-bit inputs and a state built from
`D.flops`, for ONE cycle.  That is adequate for these synthetic fixtures, whose
designs read at most two inputs.  It is NOT adequate for a real certificate --
inputs, state and edges would have to be sized and initialised from the design
and compared against `interpretDesign` over a TRACE.  No such comparison has
been run, so nothing recorded here is a real-file execution result.

### Timing, separate processes, clean build

    shape    n     reference      fast    speedup
    chain    64       0.23 s    0.08 s      2.88x
    chain   128       1.21 s    0.26 s      4.65x
    chain   256       7.82 s    0.92 s      8.50x
    chain   512      55.72 s    3.61 s     15.43x
    chain  1024     417.80 s   13.43 s     31.11x
    fan      64       0.36 s    0.12 s      3.00x
    fan     128       2.11 s    0.42 s      5.02x
    fan     256      14.61 s    1.63 s      8.96x
    fan     512     109.06 s    6.18 s     17.65x
    flop     64       0.33 s    0.19 s      1.74x
    flop    128       1.58 s    0.68 s      2.32x
    flop    256       9.22 s    2.55 s      3.62x

Growth exponent per doubling, ON THE SIZES TESTED:

    chain reference   2.40  2.69  2.83  2.91
    chain fast        1.70  1.82  1.97  1.90
    fan   reference   2.55  2.79  2.90
    fan   fast        1.81  1.96  1.92

The speedup GROWS with n, which is what distinguishes this from the earlier
`PRes.val` experiment: that one was a flat constant factor.  These are finite
ratios over 64..1024 and are NOT a proof of an asymptotic bound -- but on the
sizes tested the reference is near-cubic and the fork is near-quadratic.

### What this does to the remaining shift cost

callgrind on the fork, inclusive, n = 128, before and after this change:

    PVal.shift     61.28%  ->  10.47%
    PEnv.shiftBy'2     --  ->   7.48%
    PVal.shift'2 CALLS   3,511,844  ->  195,080      (18x fewer)
    PVal.shift'2 exponent     2.74  ->  2.01

**So de Bruijn LEVELS are no longer the priority.**  The question this section
was told to answer before pursuing them is answered: the remaining NONZERO
shift cost is about 10%, not 61%.  What is left at the top is `mixPArgs`
(94.67% inclusive, it is the caller), `mixTerm`, and `PRes.toCode'2` at 47.55%.

### rt_intpipe_alu

    fast, projectDesign's own fuel (20000/200)
        -> MixError.outOfFuel after 309 s, peak RSS 993 MB

    fast, DIAGNOSTIC fuel 200000/2000 (projectDesign itself UNCHANGED)
        -> SUCCEEDS: residual size 24,614,113 in 644 s, peak RSS 997 MB

The smallest fully-supported real design now PRODUCES A RESIDUAL.  Before this
change it overflowed the interpreter stack, and natively extrapolated to hours.

Three things this is NOT.  It used RAISED fuel, so `projectDesign` AS DEFINED
still fails on this design -- the hardcoded 20000/200 is now the binding
constraint, directly observed rather than inferred.  Observed ON THE FORK: the
reference specializer was never run to `outOfFuel` on this design, and there is
no ref/fork generic equivalence theorem that would let the fork's failure be
read as the reference's.  The equivalence evidence is the finite table above
(chain/fan/flop at 64/128/256) and nothing wider.  The residual was not run
through `checkResidual`, not executed, and not compared against the reference
(which would need the same raised fuel and far longer).  And 24.6M terms for
5118 nodes is ~4800 terms per node against `chainD`'s ~15, so the per-node
residual cost on a real design is nothing like the synthetic shapes.

### Status of this change

Still a DIAGNOSTIC fork.  Not proved, not in the core build, not in the axiom
audit.  It is the strongest promotion candidate so far -- two extensional
equations that the reference already proves -- but promotion is a separate
decision and a separate piece of work.

## 3. Status

No optimization has been applied. The three candidate fixes differ sharply in
proof cost against `PartialEvaluatorCorrect.lean`, and `prepare` is named in
`prepare_ok`, `prepare_sound`, `prepare_peel`, `prepare_cons_split`,
`prepare_cons_join_left/right`, `prepare_total_binds` and `prepare_hot_path`,
so none of them is local. Recorded here so the next increment starts from a
measurement rather than a guess.

## 7. What the 24.6M-term ALU residual actually is

Section 6 produced a residual and did not explain it.  The number was inferred
from nothing: 24.6M terms for 5118 nodes, "~4800 terms per node".  This section
replaces that with a closed-form model, fitted on synthetic fixtures where the
inputs are varied ONE AT A TIME, and then checked against the real design.

### 7.1 The hypothesis that was wrong

The obvious candidate was the runtime `nthD` chains -- `srcVal` reads an
`.input` source as `nthD inp idx` and a `.flopQ` source as `nthD fq idx`, over
the DYNAMIC input and flop vectors, so source `idx` costs `idx` steps.

`scripts/cert_shape.py rt_intpipe_alu.dcert` kills it outright:

    sources 4045  nodes 5118  outputs 2  flops 0
    source kinds: {'const': 4041, 'input': 4}
    (A) runtime-vector chain 6 tl steps   {'const': 0, 'input': 6}
        indices [0, 1, 2, 3]  max 3  distinct 4/4  dense 0..m-1

Four inputs.  The input-vector chain can account for SIX `tl` steps out of
millions.  Had this gone unchecked, the next "optimization" would have targeted
a chain that costs nothing on this design.

(An earlier version of this script computed that chain as `m*(m-1)/2` and
called it an upper bound.  That is FALSE: the cost is the sum of the ACTUAL
indices, which for a sparse or high-indexed design exceeds `m*(m-1)/2` -- a
single input at index 100 costs 100, where the formula says 0.  It happens to
be right here only because this design's indices are dense `0..3`.  The script
now sums the real indices, covers `flopQ` and `flopQAsync` (which read BOTH
runtime vectors), range-checks deps, and prints whether the indices are dense;
8.5 shows three real designs where the old formula would have been wrong.)

### 7.2 Varying the inputs one at a time

Three fixtures, NODE COUNT HELD AT 64 throughout so that nothing below can be
explained by node count (`--depth-grid`), plus a fourth that moves node count
alone (`--node-depth`):

| fixture | sources | what the nodes read | isolates |
|---|---|---|---|
| `srcD`   | `nsrc` inputs | source slot `i % nsrc` -- DEEP | both chains together |
| `shalD`  | `nsrc` inputs | the two newest slots -- depth 0/1 | the input-vector chain alone |
| `cstD`   | 2 inputs + `nsrc-2` consts | source slot `i % nsrc` -- DEEP | the slot chain alone |
| `midD`   | 1 input + 1 const | node 0's slot, which sinks with every node | NODE-portion depth alone |

`midD` is the one that settles the shape of the answer:

    midD nnode=1024   node-portion read depth sums to 523,776
                      residual: terms 11,301   tl 1

Half a million levels of node-portion walking cost ONE residual `tl`, and the
residual is linear (11.0 terms/node at every size).  Phase 1's structural peel
works perfectly on the node portion of the environment.

`shalD` is exactly the input-vector chain and nothing else:

    shalD tl  =  nsrc*(nsrc-1)/2 + 1       EXACT at nsrc = 2,16,64,128,256,512,1024

(`shalD`'s inputs sit at dense indices `0..nsrc-1`, so the sum of indices IS
`nsrc*(nsrc-1)/2` here.  The general form is the sum of the actual indices --
see 7.1.)

`cstD` is exactly the slot chain and nothing else, and -- the key observation --
a slot read costs its depth in the SOURCE PREFIX, `nsrc - 1 - d`, not its depth
in the whole environment.  The node results stacked above it are free.

And the two are ADDITIVE: `srcD tl = shalD tl + cstD tl - 2` at all seven sizes.

### 7.3 The model

    tl  =  nInputs*(nInputs-1)/2                    (A) runtime input-vector chain
         + SUM over node deps `d` pointing at a SOURCE of (nSources - 1 - d)
                                                    (B) source-prefix slot chain
         ( node deps contribute ZERO )

    hd  =  nInputs + (number of node deps reading a source slot) + c,  c in 1..3

    terms ~ 3.0 * tl        (each surviving step emits one letIn + one var + one prim)

Checked against `rt_intpipe_alu` -- a design the model was not fitted on:

| quantity | predicted | measured | error |
|---|---|---|---|
| `tl` | 8,179,111 | 8,180,316 | 0.015% |
| `hd` | 4,181 | 4,182 | 1 |

    measured: terms 24614113  lit 13584 var 8202953 letIn 8191420 ite 1155
              prim 8204998 ctorT 2 caseT 1 call 0
              tl 8180316  hd 4182  consP 9165  isNil 0
              bvResize 3248  bvAnd 1170  bvMk 63  eqI 1124
    (wall 702 s, peak RSS 997 MB, diagnostic fuel 200000/2000)

#### How much of the residual the chain is -- stated as bounds, not one number

An earlier revision of this section said "99.97%".  That was arrived at by
SUBTRACTING a hand-picked set of operator primitives from the total and calling
the rest chain, which assumes the answer.  The defensible statement is a pair of
bounds, because `letIn` and `var` nodes are not labelled by what produced them.

Directly measured counts, which sum to the total exactly:

    lit 13,584 + var 8,202,953 + letIn 8,191,420 + ite 1,155
      + prim 8,204,998 + ctorT 2 + caseT 1 + call 0  =  24,614,113

    of the prim nodes, `tl` applications are 8,180,316

A surviving `nthD` step emits one `tl` prim, one `letIn` and one `var`, so:

| | terms | share |
|---|---|---|
| lower bound, 3 x tl | 24,540,948 | **99.7028%** |
| upper bound, letIn + var + tl (all three measured) | 24,574,689 | **99.8398%** |
| `tl` prim nodes alone | 8,180,316 | 33.23% |

The bounds are tight because `letIn` exceeds `tl` by only 11,104 and `var` by
22,637 -- i.e. almost every `letIn` and `var` in this residual belongs to the
chain.  **Non-chain residual is between 39,424 and 73,165 terms, 7.7 to 14.3 AST
nodes per node.**  Section 6's "~4800 terms per node" was the chain, not the
design, by a factor of 340 to 620.

The remaining points:

* **99.998%** of term (B) is reads of **`const` sources** -- 4,041 of them sitting
  in a 4,045-entry source prefix, read at a mean depth of 2,024.
* term (A), the chain the previous section would have attacked, is **6 steps**.
* `countPrim` covers 8 primitives and therefore misses **5,730** prim
  applications in this residual outright (`bvOr`, `bvXor`, `bvNot`, `bvUint`,
  `addI`, `subI`, `notB`, ...).  Any per-operator figure derived from it is a
  LOWER bound on operator work, not a term count.  The earlier "6,760 operator
  terms / 1.32 terms per node" was exactly that mistake: it counted
  `bvResize + bvAnd + bvMk + eqI` applications plus `ite` nodes, which is
  neither all operators nor a count of AST terms.  The honest figure for
  non-chain work is the 7.7-14.3 nodes/node bound above.

### 7.4 Where the cost comes from, and what is not yet established

ESTABLISHED by measurement: walking the NODE portion of the environment is
free, walking the SOURCE portion costs one let-bound `tl` per level and one
`hd` at the end.  Two independent counters agree, on 26 synthetic points and on
the real design, to within 0.02%.

NOT YET ESTABLISHED: *why* the two portions differ.  `mkSources`
(`HardwareInterpreter.lean:144`) and `evalNodes` (`:170`) build their spines
with the same syntactic `cons` in the same accumulator style, so the asymmetry
is not visible in the interpreter text.  The leading candidate is that `env0`
is bound by its own `letIn` in `main`'s `lets` block (`:115`), and a dynamic
`letIn` reifies its body (`PartialEvaluator.lean:326`), whereas the node spine
is the RESULT of the unfolded `evalNodes` call and survives as a `PVal.cons`.
That was a HYPOTHESIS when this section was written.  **Section 8 runs the
experiment and confirms it**: inlining that one binding drops `tl` to 1 on every
`cstD` size.  The rest of 7.4 is left as written, because the reasoning it
records was done before the answer was known.

Consequences for what to do next, recorded so the next increment is not chosen
on vibes:

* The fix is not in the input/flop vectors.  A shared-prefix destructuring of
  the runtime input vector would have removed 6 of 8,180,316 steps here.
* A constant source needs no runtime environment entry at all -- its value is
  static.  Making `srcVal` of a `.const` resolve without an environment slot
  would remove ~99.998% of this design's residual.  That is a change to `I_hw`,
  which `IHwAdequate_proved` is stated about, so it is NOT free: it re-opens the
  adequacy proof.  It is not a specializer change and must not be sold as one.
  **Section 8 shows this is not the change to make**: a far smaller rewrite,
  which does not reindex or delete any slot and does not touch source
  semantics, removes the same cost.
* `projectDesign`'s own fuel (20000/200) is still the binding constraint at
  section 6's numbers; nothing here changes that.

### 7.5 Reproducing

    bash scripts/build-native.sh scripts/proto_probe.lean
    ./.native-dev/proto_probe --depth-grid     # srcD / shalD / cstD, node count fixed at 64
    ./.native-dev/proto_probe --node-depth     # midD, node-portion depth alone
    ./.native-dev/proto_probe --file-profile rt_intpipe_alu.dcert 200000 2000
    python3 scripts/cert_shape.py rt_intpipe_alu.dcert

Raw logs: `.perfwork/depth-grid.log`, `.perfwork/node-depth.log`,
`.perfwork/alu-profile.log` (kept outside the repo; the commands above
regenerate them).

## 8. The `env0` A/B: the hypothesis in 7.4 was right

Section 7.4 recorded, as a hypothesis and not a finding, that the source
portion of the environment is opaque to the specializer because `env0` is bound
by its own `letN` in `main`'s binding block, and a dynamic `letIn` reifies its
body.  This section runs the experiment.

`Projection/Proto/InterpreterVariant.lean` applies ONE checked, local,
source-to-source rewrite to a COPY of `hwS`.  `hwS` itself is untouched and
remains the reference; `IHwAdequate_proved` is stated about `hwS`, not about
the variant.

    letN "env0" e (letN "env" e2 body)   ==>   letN "env" e2[env0 := e] body

Single-use inlining of the immediately following binding, applied only when all
four side conditions hold, and reported either way so a failure is visible:

| condition | why | measured |
|---|---|---|
| `env0` not free in its own rhs | no self-reference | `selfRef 0` |
| `env0` occurs EXACTLY once in the next rhs | work done exactly once | `useInNext 1` |
| `env0` occurs nowhere later in the block | nothing else observes it | `useLater 0` |
| that rhs has no `ite`/`switch`/`letN` | the use is unconditional, in the same order, and `e`'s failure is preserved -- `call` is strict in its arguments | `nextStrict true` |

No binder is introduced between the two bindings, so the rewrite cannot
capture.  That is an argument, not a proof; the variant is not proved and is
not in the core build or the axiom audit.

    rewrite: found true selfRef 0 useInNext 1 useLater 0 nextStrict true applied true
    variant: resolves true  bta true  wfA true  erase-ok true

### 8.1 Semantics first, measurement second

Before any speed number, both interpreters are run against the SHARED
reference semantics -- `interpretDesign`, not each other -- on stimulus sized
from each design (`maxInputIdx`, so sparse and high input indices are driven
correctly; the old fixed two-element vector was not adequate for that).

    semantics: 8 designs x 3 stimuli, both interpreters vs interpretDesign -- all agree

covering mixed const/input spines (`mixD 4`, `mixD 8`), SPARSE input indices
(`sparseD`: inputs at 0 and 9), `chainD`, `fanD`, a SEQUENTIAL fixture
(`flopD 8`), and the source-heavy `srcD`/`cstD`.  Error behaviour is a separate
case, because the property there is that they fail TOGETHER:

    out-of-range dep: ref-produced false variant-produced false  same-outcome true

### 8.2 The measurement

Same fork specializer on both sides, so the ONLY difference is the interpreter.
Every residual was also executed and matched `interpretDesign`.

| fixture | terms | | tl | | what the remaining `tl` is |
|---|---:|---:|---:|---:|---|
| `cstD 64/64`   | 13,214 | **988** | 4,033 | **1** | nothing -- chain (B) gone |
| `cstD 256/64`  | 87,518 | **1,756** | 28,545 | **1** | nothing |
| `cstD 512/64`  | 186,846 | **2,780** | 61,313 | **1** | nothing |
| `cstD 1024/64` | 385,502 | **4,828** | 126,849 | **1** | nothing |
| `srcD 256/64`  | 186,197 | 100,435 | 61,184 | 32,640 | chain (A), = 256*255/2 |
| `mixD 128`     | 57,205 | 8,179 | 18,272 | 2,016 | chain (A), = 64*63/2 |
| `flopD 64`     | 13,985 | 13,726 | 4,096 | 4,032 | the FLOP-vector chain |

Term (B), the source-prefix slot chain, is **eliminated**: `tl` drops to 1 at
every size, and `cstD` -- which is nothing but term (B) -- shrinks by 13x to
80x, growing with source count exactly as the model predicts it should.

Every remaining `tl` is exactly chain (A) or the flop-vector chain, neither of
which this rewrite touches and neither of which it claims to.

**`consP` is unchanged, and I first wrote that down as a good sign.  It is
not.**  Fitting it rather than assuming it:

    consP  =  nSources + nNodes + nFlops + 1      EXACT on all 8 fixtures
              (129, 321, 577, 1089, 321, 257, 131, 1027)

That is the ENVIRONMENT SPINE, not the output and flop-result lists -- `cstD`
has one output and no flops, so those account for 1 or 2 conses, not 1,089.
This is Trap 2 in the plan, and it catches the claim rather than exonerating
it: the residual no longer WALKS the slot environment, but it still BUILDS it,
one `consP` per slot.  That cost is LINEAR, not quadratic, so it is not what
section 7 was about -- but the plan's acceptance condition is a residual with
"no runtime slot environment AT ALL", the legacy straight-line `let` chain, and
this residual does not meet it.  The rewrite closes the quadratic; it does not
deliver the target shape.

`flopD` barely moves, as expected -- its cost is the flop-state quadratic
recorded in Phase 1, a third chain, still open.

### 8.3 What this does NOT fix

**Fuel is unchanged.**  (And see 8.7: a raised budget is not itself outside the
theorem -- `specializeDesign_correct` is budget-parametric and conditional on
success.  What follows is about the budget `projectDesign` happens to fix.)
On `rt_intpipe_alu` at `projectDesign`'s own 20000/200, the variant still
fails:

    variant FAILED (MixError.outOfFuel)     wall 309.68 s   peak RSS 97,524 KB

against the reference fork's `outOfFuel` after 309 s at 993 MB (section 6).
Same wall time, same failure, **10.2x less memory**.  That is the expected
shape: fuel counts SPECIALIZATION STEPS -- unfoldings of `nthD` and friends --
and the rewrite does not remove a single unfolding.  It removes what those
unfoldings EMIT.  The specializer still walks the chain; it just now collapses
each step instead of residualising it.

So the three walls separated in section 5 remain three walls.  This closes the
RESIDUAL SIZE one for term (B) and leaves the hardcoded-fuel one exactly where
it was.  Anyone reading "80x smaller" as "the ALU now projects" would be wrong.

**Two other chains are untouched**, by construction, and both are visible in
the table above: chain (A), the runtime input vector, which costs the sum of
the actual input indices; and the flop-state chain, which is why `flopD` barely
moves.  Neither is this rewrite's business.

### 8.4 What it would take to promote this

The variant is NOT proved and must not be counted.  The promotion path is the
one the review named, and it is small:

* the rewrite is a SINGLE-USE LET INLINING on the surface syntax.  **The side
  conditions as first written were not sufficient -- see 9.1, which gives the
  counterexample and adds a fourth -- and the theorem is an `SEval` iff, not
  observational equivalence; 9.2 states it exactly.**  `SurfaceSemantics.lean`
  already provides the relation and `SEval_sound` the bridge down to `Eval`;
* it does NOT need a new hardware semantics, and it does NOT touch the
  specializer, so `mixDriver_iff` is untouched;
* `IHwAdequate_proved` is stated about `hwS`.  Promoting the variant means
  either re-deriving adequacy for `hwSVar` through that equivalence lemma, or
  applying the rewrite to `hwS` itself and re-running the adequacy proof.  The
  first is the cheaper and the more honest: `hwS` stays the reference and the
  variant is related to it by a theorem.

Until that lemma exists, every number in section 8 is a measurement of an
unproved program, and the four side conditions are checked by `#eval`-style
runtime assertions in `proto_probe --env0-ab`, not by the kernel.

### 8.5 What the model predicts for the real certificates

`scripts/cert_shape.py --summary` applies the section 7.3 model to all 19
DCERT1 files in `livehd-d4-incremental/temp`.  These are **predictions**, not
runs: only `rt_intpipe_alu` has been measured, and only (A)+(B) is modelled
exactly -- (C), the flop hold-vector chain, is a LOWER bound (`flopD`
residualises about twice it).

| design | src | node | (A) inp | (B) slot | (C) flop | before = A+B | after = A+C |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rt_btb_gate` | 1,784 | 1,782 | 2,286 | 1,758,607 | 2,016 | 1,760,893 | **4,302** |
| `rt_intpipe_alu` | 4,045 | 5,118 | 6 | 8,179,105 | 0 | 8,179,111 | **6** |
| `rt_alu_gate` | 6,137 | 6,597 | 3 | 18,828,961 | 0 | 18,828,964 | **3** |
| `rt_decoder_gate` | 8,373 | 8,971 | 405 | 35,061,620 | 0 | 35,062,025 | **405** |
| `rt_aes_gate` | 8,658 | 11,681 | 3 | 37,476,641 | 0 | 37,476,644 | **3** |
| `rt_csr_regfile_gate` | 32,822 | 34,874 | 12,248 | 753,272,071 | 9,180 | 753,284,319 | **21,428** |
| `rt_cva6_hpdcache_subsystem_gate` | 97,774 | 108,666 | 155,551 | 4,956,403,289 | 146,611 | 4,956,558,840 | **302,162** |

Three to five orders of magnitude on every real design, because every one of
them is dominated by `const` sources read out of a long source prefix -- the
same shape as the ALU, not a peculiarity of it.

Three cautions that keep this from being a reach claim:

* **The "after" column is an UNDERCOUNT**, and 8.7 measures by how much on the
  one design that has been run: it predicts 6 for `rt_intpipe_alu` and the
  measured value is **1,211**.  The gap is the third additive term identified
  in 8.7, a per-design constant of 1,205 here, which this model does not cover
  and which only becomes visible once (B) is gone.  Read the column as "(B) is
  no longer the dominant term", not as a residual size.

* **Fuel, not residual size, is the binding constraint at `projectDesign`'s
  fixed budget** (8.3).  The ALU still fails at 20000/200 WITH the rewrite.
  That is a statement about that budget, not about the theorem, which is
  budget-parametric (8.7).  None of these designs becomes projectable AT THAT
  BUDGET on this evidence.
* The table also shows why the corrected `cert_shape.py` matters.  These
  designs have SPARSE input indices -- `rt_btb_gate` has 5 inputs at indices
  0,2,3,4,5 (sum 14, not 5*4/2 = 10); `rt_decoder_gate` has 27 inputs with max
  index 29 (sum 405, not 351); `rt_csr_regfile_gate`'s lowest input index is 2.
  The `m*(m-1)/2` formula the first version of the script used would have been
  wrong on every one of them, and `rt_btb_gate` and `rt_csr_regfile_gate` also
  carry `flopQAsync` sources, which read BOTH runtime vectors.

### 8.6 Reproducing section 8

    bash scripts/build-native.sh scripts/proto_probe.lean
    ./.native-dev/proto_probe --env0-ab              # conditions, semantics, A/B; exit 0 only if all agree
    ./.native-dev/proto_probe --file-ab CERT.dcert 20000 200     # projectDesign's own fuel
    ./.native-dev/proto_probe --file-ab CERT.dcert 200000 2000   # diagnostic fuel
    python3 scripts/cert_shape.py --summary /path/to/*.dcert

`--env0-ab` exits nonzero if any side condition fails, if the variant stops
being a different program from the reference, or if any interpreter or residual
disagrees with `interpretDesign`.  Logs: `.perfwork/env0-ab.log`,
`.perfwork/alu-var.log`, `.perfwork/alu-var2.log`.

### 8.7 `rt_intpipe_alu` with the variant: measured, checked, and EXECUTED

    fuel 200000/2000  (projectDesign's own is 20000/200)
      terms 72558   lit 13584 var 23816 letIn 12283 ite 1155
                    prim 21717 ctorT 2 caseT 1 call 0
      prims: tl 1211  hd 6  consP 9165  isNil 0
             bvResize 3248  bvAnd 1170  bvMk 63  eqI 1124
      checkResidual: ACCEPTED, exact fuel 20207
      seed 0..3: residual-ran true   matches interpretDesign true   (all four)
      control: variant interpreter vs interpretDesign true
    wall 785.74 s   RSS 101,888 KB

    reference interpreter, same fuel:  24,614,113 terms, tl 8,180,316,
                                       702 s, RSS 997,220 KB

**339x fewer terms, 6,755x fewer `tl`, 9.8x less memory.**  The wall time is NOT
comparable: this run shared the machine with a core build and an `--env0-ab`
run.  Memory and term counts are unaffected by that.

Three independent checks of the section 7.3 model fall out, and all three land:

* **`tl`.**  Before: 8,180,316 = (A) 6 + (B) 8,179,105 + 1,205.  After:
  1,211 = (A) 6 + 1,205.  **The same constant 1,205 in both.**  So (A)+(B) is
  exactly right and the 0.015% "error" in 7.3 was never error -- it is a THIRD,
  additive term the model does not cover, now the dominant one.  What produces
  it is not identified; the arity histogram (two arity-64 nodes, one each of 65,
  33, 32, 16, and 72 `MuxN`) is the obvious place to look next.
* **`hd`.**  Model `nInputs + sourceSlotReads + c`.  Before 4+4,176+2 = 4,182
  measured.  After 4+0+2 = **6, measured 6, exact.**
* **non-chain terms.**  8.2 predicted the non-chain residual lay in
  [39,424 , 73,165], i.e. 7.70-14.30 AST nodes per node.  Removing the chain
  leaves **72,558 terms = 14.18 per node** -- inside the band, at the top.
  The bounds were right and the upper one was tight.

`consP` is 9,165 against `nSrc + nNodes + nFlops + 1` = 9,164, consistent with
8.2: the environment spine is still built, just no longer walked.

#### What this is, and what it is not

This is the **first real supported design whose projected residual has been
accepted by `checkResidual` and then EXECUTED and compared against
`interpretDesign`** -- four stimuli, sized from the design, all matching, with
the interpreter itself checked on the same stimulus as a control on the
stimulus builder.

It does **not** count toward the 30-block destination.  But the reason is NOT
the one an earlier revision of this section gave, and getting that wrong
mattered:

**Raised fuel is not in itself a disqualifier.**  `specializeDesign_correct`
(`ProjectionCorrect.lean:108`) is already stated for `{sf wf : Nat}` with the
hypothesis `mixDriver sf wf hwAP [encDesign D] = .ok R` -- correctness
CONDITIONAL ON SUCCESS, at ANY budget.  `projectDesign` merely fixes
`20000/200`, and `projectDesign_correct` is a one-line instance of the general
theorem.  So "it used 200000/2000, therefore it is outside the theorem" was
wrong: a run at any budget that SUCCEEDS is covered.  `outOfFuel` is a
diagnostic failure of a particular budget, not evidence about the design.

The two things that actually disqualify this run are both about WHAT WAS RUN,
not how much fuel it got:

1. **Unproved specializer.**  It used `ProtoFast.mixDriver`, the diagnostic
   fork, whose `PRes.val` change has no bridge.  `specializeDesign_correct` is
   about `mixDriver`, not the fork.
2. **Unproved interpreter.**  It used `hwAPVar`, not `hwAP`.
   `IHwAdequate_proved` says nothing about it, and the equivalence lemma in 8.4
   / 9.2 does not exist yet.

Fix either and the other still blocks; fix both and the budget is already
covered by the existing theorem.

It is also one cycle, not a trace: `trace_agree` has not been exercised here.

## 9. Hardening the gate, and a side condition that was not sound

### 9.1 `noBranch` does not give order preservation

Section 8 claimed the three occurrence conditions plus `noBranch` gave "the
same order, and failure or divergence of `e` preserved, because `call` is
strict in its arguments".  **That is false.**

    letN "z" E (letN "w" (call f [A, z]) body)        -- E runs FIRST
    ==>  letN "w" (call f [A, E]) body                 -- A runs first, THEN E

Strictness says every argument is eventually evaluated.  It says nothing about
WHICH ONE FAILS FIRST.  With `E` a type error and `A` a loop, the original
fails with `typeError` and the rewritten one diverges.

**What that counterexample does and does not refute.**  It is NOT a
counterexample to the successful-`SEval` iff of 9.2: one side raises
`typeError`, the other diverges, so NEITHER has a successful `SEval` and the
iff holds vacuously there.  It refutes FAILURE-ORDER EQUIVALENCE, a strictly
stronger property.  Section 9 originally ran the two together.

A fourth side condition covers the stronger property -- `beforeHoleTotal`:
everything evaluated strictly before the occurrence must be a `ref` or a `lit`,
i.e. incapable of failing or diverging.  It is NOT needed for the iff.  It is
kept because a rewrite that silently turns an error into a hang is not one to
ship, proved iff or not.  Arguments AFTER the hole need no condition,
because they run after `e` in both programs.  `noBranch` is still required and
still load-bearing for a different reason: `substRef` is NOT capture-avoiding,
and `noBranch` is what guarantees no binder lies on the path to the hole.

`proto_probe --inline-negative` is the regression:

    negative case: found true useInNext 1 nextStrict true beforeTotal false applied false
      before rewrite: typeError      after FORCED rewrite: outOfFuel
      checker refused it: true   outcomes differ: true

Note `nextStrict true`: the OLD condition set would have ACCEPTED this rewrite.
The real `env0` rewrite satisfies the new condition -- its occurrence sits at
`call "evalNodes" [ref "nodes", ref "env0", ref "nsrc"]`, whose only preceding
argument is a `ref`.

### 9.2 The theorem to prove is an `SEval` iff, not observational equivalence

Stated in `InterpreterVariant.lean`'s header so a promotion cannot quietly aim
higher than the evidence:

    beforeHoleTotal + the three occurrence conditions  -->
      forall rho v,  SEval P rho (letN x e e2) v  <->  SEval P rho (e2[x := e]) v

`SEval` relates only terminating successful evaluations.  This is NOT an
equality of `evalFuel` results -- fuel accounting differs by one `letIn`
descent -- and without `beforeHoleTotal` the FAILURE MODE differs, as 9.1
shows.  Promotion must prove that statement and not a stronger one that is
false.

### 9.3 The execution gate is now strict

Previously `--file-ab` printed a checker rejection and a control mismatch and
still returned 0, and `--env0-ab` printed specialization failures and still
returned 0.  Both are now failures.  Exit codes:

| code | meaning |
|---|---|
| 0 | every check passed |
| 1 | disagreement with `interpretDesign`, checker rejection, or a vacuous trace |
| 2 | setup failure, or the design is NOT `SupportedByProjection` |
| 3 | `outOfFuel` at the requested budget -- a DIAGNOSTIC FAILURE, not a pass |

Four further changes, each closing a way the old gate could pass without
testing anything:

* **Failure kinds are preserved.**  The old comparator mapped every failure to
  `none` and treated `none == none` as agreement, so a `typeError` on one side
  and an `outOfFuel` on the other read as "both agree".  `Outcome` now carries
  `ok`/`undecodable`/`fuelOut`/`typeErr` and `Outcome.agree` matches on the
  kind.  The out-of-range case now reports `ref typeError  variant typeError
  same-kind true`, which is a real statement; `none == none` was not.
* **Checked execution runs at the CHECKED bound.**  `checkResidual` returns an
  EXACT fuel; running at a hardcoded 4,000,000 never exercised it.  `--file-ab`
  now executes at the returned bound, and falls back to 4M only when the
  checker REJECTED -- in which case it also fails.
* **Stimulus is width-aware.**  An 8-bit constant for every input cannot set a
  bit above 7, so the 64-bit operands of a real design were never driven.
  Seeds 0-4 are now the corners -- zero, all-ones, sign bit only, everything
  but the sign bit, alternating -- at each input's widest declared use, with
  pseudorandom values above that.
* **Traces, and vacuity detection.**  Multi-cycle runs feed `nextState`
  forward against `interpretDesign` iterated the same way, under an edge
  schedule where every third cycle nothing fires.  The runner also reports
  whether the state EVER changed and fails if a design with flops never moves,
  so a vacuous trace cannot pass as a sequential test.

### 9.4 What the strict gate immediately found

Not a bug in the residual -- a gap in what was being tested.  Three small
certificates disagreed with `interpretDesign` on every nonzero stimulus.  They
are designs the theorem EXCLUDES:

    rt_fx_opconst      wf T memFree T sources T ops F arities T flopClocks T
    rt_fx_opconst_wide wf T memFree T sources T ops F arities T flopClocks T
    rt_fx_memwritebe   wf T memFree F sources F ops F arities T flopClocks T

`I_hw` defines memory sources as `bvMk 0 0` -- deliberately-wrong, documented
in its header -- so disagreement there is the DESIGNED behaviour, not a defect.
`--file-ab` now evaluates all six `SupportedByProjection` fields first and
refuses to compare when any is false, naming the field.

`--support` over all 19 certificates: **14 supported, 5 not**.  The five are
the three above plus `rt_aes_gate` (sources, ops) and
`rt_cva6_hpdcache_subsystem_gate` (memFree, sources, ops).

### 9.5 `rt_intpipe_alu` re-run under the strict gate

The section 8.7 run predates the hardening: it used the old 8-bit stimulus and
executed at a hardcoded 4,000,000 fuel.  Re-run once, under every check in 9.3:

    support: wf true memFree true sources true ops true arities true flopClocks true
    fuel 200000/2000
      terms 72558   tl 1211  hd 6  consP 9165
      checkResidual: ACCEPTED, exact fuel 20207
      seed 0: residual ok  matches interpretDesign true      <- zero
      seed 1: residual ok  matches interpretDesign true      <- all ones
      seed 2: residual ok  matches interpretDesign true      <- sign bit only
      seed 3: residual ok  matches interpretDesign true      <- all but sign bit
      seed 4: residual ok  matches interpretDesign true      <- alternating
      seed 7: residual ok  matches interpretDesign true      <- pseudorandom
      trace: 4 cycles, state fed forward -- agrees true  state-changed false
             clock-domains 1  flops 0
      control: variant interpreter ok vs interpretDesign true
    wall 814.44 s   RSS 102,016 KB      (contended; two other jobs on the box)

Now a stronger statement than 8.7's: the execution happens at the bound
`checkResidual` RETURNED, so the fragment check is actually exercised rather
than bypassed by a larger constant, and the stimulus drives the 64-bit operands
this design has.  `state-changed false` is correct and is NOT flagged vacuous:
`rt_intpipe_alu` has zero flops, so there is no state to move.  The vacuity
check fires only when `D.flops.size > 0`.

Residual numbers are unchanged from 8.7, as they must be -- the gate changed,
not the specializer.

What this still is not: the fork specializer and the variant interpreter are
both unproved (8.7), so this is experimental execution evidence, not verified
coverage, and it is one design.

## 10. Four corrections to section 9, and the provenance question

### 10.1 "exact fuel" overstated what `checkResidual` proves

`checkResidual_sound` (`ResidualFragment.lean:318`) is

    checkResidual R = some b  ->  evalFuel b R [] (.call R.entry [a0,a1,a2]) != .outOfFuel

A **proved sufficient** bound, and it is the `height`, so it covers the DEEPEST
path through the residual.  It is NOT the minimum fuel for a given input: an
input that takes shorter branches needs less.  `--file-ab` now prints

    checkResidual: ACCEPTED, proved-sufficient bound 20207
      (height; no outOfFuel at this bound -- not a minimum for any given input)

Running AT that bound is still the right thing -- a larger constant never
exercises the checker -- but the claim attached to it was wrong.

### 10.2 `traceCompare` re-synchronised the residual every cycle

It fed `want.nextState`, the REFERENCE's next state, to both sides.  Because
`Outcome.agree` compares the whole `RuntimeResult`, `nextState` included, a
passing run was still a sound lockstep regression by induction -- not a false
pass.  But it meant a residual that drifted could only ever fail on the cycle
it drifted, never compound.

Fixed rather than merely documented: the two sides now carry INDEPENDENT
states, `stRef` from `interpretDesign` and `stRes` from the residual's own
`nextState`, so drift propagates into later cycles' inputs.

### 10.3 `obviouslyTotal` on a `ref` needs a scoping premise

`.ref y` is total only relative to an environment that BINDS `y`; an unbound
name fails.  So scopedness is needed to CLAIM `.ref` totality, and hence for
the failure-order property.

**But it does not make the successful iff false**, and an earlier revision of
this section said it did.  Scopedness sits with `beforeHoleTotal` in the
failure-order group, not with the iff.

The binder-aware statement, with the right scope: `x` is bound by the let being
REMOVED, so the environment at issue is the one the OUTER `letN` is evaluated
in, not the caller's.  `e2` runs under `sigma[x := w]` before and under
`sigma` after; the free names other than `x` agree because `z != x` and
`countRef x body = 0`, and `e` sees the same environment at the hole as at the
binding because no binder lies between them (`noBranch`).

### 10.4 Provenance: the ALU is CORE-ET, not CVA6

`rt_intpipe_alu` is a **CORE-ET** design -- the `intpipe_*` family
(`livehd-d4-incremental` `pass/lean/README.md:864`,
`CVA6_COVERAGE_PLAN.md:261`).  Nothing in sections 7-9 is CVA6 coverage, and
the destination in `SIMULATOR_PLAN.md` is 30 CVA6 blocks.  `rt_btb_gate` IS
CVA6 -- `frontend/btb.sv`, `CVA6_COVERAGE_PLAN.md:88`.

### 10.5 `btb_gate` is degenerate as a SEQUENTIAL target -- upstream, not here

Found while checking provenance, before the run finished.
`DIRECTION4_INCREMENTAL.md:516`:

> `btb_gate` turned out to be degenerate for this purpose at BOTH reset levels.
> Its `flopQAsync` sources say reset when input 4 is 0, while all 64 of its
> `FlopDesc` records say reset when input 4 is 1 -- opposite polarities on the
> same port, so no value lets the design run and the digest is all zeros
> throughout.  That looks like the `negreset` conflation the `FlopDesc`
> docstring warns about; it is upstream of this work.

So NO stimulus moves its state, and the vacuity check added in 9.3 should fail
it: `flops 64` with `state-changed false` is exit 1.  That is the gate working.
It also means `btb_gate` cannot be the "one supported real CVA6 block executed
and reference-compared over traces" -- not because projection fails, but
because the design has no reachable sequential behaviour to compare.

Remaining supported CVA6 candidates, from `--support`: `rt_alu_gate` (6,137
sources, 6,597 nodes, 0 flops) and `rt_decoder_gate` (8,373 / 8,971, 0 flops)
are COMBINATIONAL, so they can be executed and reference-compared but cannot
exercise a trace; `rt_csr_regfile_gate` (32,822 / 34,874, 136 flops) is the
only supported CVA6 design with flops that is not the degenerate `btb` family,
and it is the largest of them.

## 11. `rt_btb_gate`: the actual result, and two things I got wrong

### 11.1 The run

Finished before the degeneracy prediction in 10.5 could be tested; it was NOT
restarted for the harness edits in section 10, so it ran the pre-10 binary.

    rt_btb_gate: sources 1784 nodes 1782 flops 64
    support: wf true memFree true sources true ops true arities true flopClocks true
    fuel 20000/200  (projectDesign's OWN budget -- not raised)
      terms 415935   tl 129172  hd 390  consP 3631
      checkResidual: ACCEPTED, bound 8075
      seed 0,1,2,3,4,7: residual ok, matches interpretDesign  (all six)
      trace: 4 cycles -- agrees true   state-changed true   flops 64
      control: variant interpreter ok vs interpretDesign true
    wall 1901.10 s   RSS 70,520 KB   exit 0

A supported CVA6 block, projected **at the stock budget**, fragment-checked,
executed, and matching `interpretDesign` on six width-aware stimuli and a
4-cycle trace.  Still experimental: the fork specializer and the interpreter
variant are both unproved (8.7).

### 11.2 I predicted this would fail the vacuity check.  It did not, and the
reason is a defect in the check

10.5 predicted `state-changed false`.  It reported `state-changed true`.  The
cause is exactly the one review named: `mkStateFor` seeds flops with NONZERO
values (`(seed*5 + i*3) % 16`), so a design that merely collapses to its reset
value on cycle 1 and sits there has CHANGED ONCE and satisfies an
ever-changed bit.  A `stateEverChanged` flag is weak coverage, not evidence of
non-reset progression.

`traceCompare` now counts DISTINCT STATES instead, and fails at `<= 2`.

**And the first fix was itself wrong.**  Running `--trace-ref` at 1 cycle
printed `DEGENERATE: only 2 distinct states over 1 cycles`.  With `n` cycles
you can see at most `n+1` states, so `<= 2` is unavoidable below 3 cycles: the
verdict was unsupportable by its own evidence.  `--trace-ref` now reports
INCONCLUSIVE below 3 cycles.

So the degeneracy of `btb_gate` is **not established here**.  10.5 quotes the
upstream note and review confirmed the file evidence independently -- source
rows are `flopQAsync ... resetInput=4 activeLow=1` while all 64 flop rows are
`resetPin=1779 activeLow=0`, and slot 1779 is input 4, so the two paths
disagree on polarity for the same pin.  That is a certificate/export-boundary
discrepancy to QUARANTINE, not a demonstrated projection bug, and its literals
must not be edited to make a test pass.

### 11.3 The "cheap" reference-only diagnostic is not cheap on this design

`--reset-probe` drives reset asserted then de-asserted and measures
progression only after release.  It uses `interpretDesign` alone, no
specialization, and should be instant.  On `rt_btb_gate` it timed out at 600 s.

Measured directly: **one `interpretDesign` cycle on `rt_btb_gate` takes
190.91 s.**  For comparison, the ALU's `--file-ab` run (5,118 nodes) was 814 s
against 785 s for projection alone, so ~11 `interpretDesign` calls cost ~29 s
there -- about 2.6 s each.  `btb_gate` is 3x SMALLER and ~70x slower per cycle.

Two consequences, both load-bearing:

* **The 1901 s in 11.1 is mostly the REFERENCE, not projection.**  That run
  makes 11 `interpretDesign` calls; at 190 s each that is ~2,100 s of the
  budget, i.e. essentially all of it.  Projection at 20000/200 was comparatively
  fast.  Reading 1901 s as "projection is slow on btb" would be wrong.
* A reference-only diagnostic is only cheap where `interpretDesign` is cheap.
  Establishing `btb_gate`'s degeneracy needs >= 3 post-release cycles, i.e.
  >= 5 cycles total, i.e. ~16 min of REFERENCE time alone.  Not run.

What makes `btb_gate`'s reference evaluation 70x more expensive per node than
the ALU's is not identified.  It has 64 `flopQAsync` sources and 64-bit
widths where the ALU has none and mostly narrower ones; that is a hypothesis,
not a finding.

### 11.4 Coverage status, combinational and sequential kept apart

Per review, a supported COMBINATIONAL CVA6 block counts toward simulator
coverage without inventing sequential activity, provided the two are not
merged into one number.

| | design | status |
|---|---|---|
| CVA6, sequential | `rt_btb_gate` | executed and reference-compared at stock budget, BUT quarantined: reset-polarity discrepancy, degeneracy neither confirmed nor refuted |
| CVA6, combinational | `rt_alu_gate`, `rt_decoder_gate` | supported; NOT yet executed |
| CORE-ET, combinational | `rt_intpipe_alu` | executed and reference-compared (9.5), at a raised budget |

**Counted toward the 30 CVA6 blocks: zero.**  `rt_intpipe_alu` is CORE-ET.
`rt_btb_gate` is quarantined.  The combinational CVA6 blocks have not been run.

## 12. Coverage is not correctness, and the state-count heuristic was wrong both ways

### 12.1 The heuristic

Section 11.2 replaced "did the state ever change" with "> 2 distinct states"
and used it as a correctness gate.  That is wrong in BOTH directions, and three
one-line fixtures show it:

| fixture | states reached | heuristic says | truth |
|---|---:|---|---|
| `toggleD` -- `q' = not q` | **2** | VACUOUS | a one-bit toggler has two states FOREVER and is healthy |
| `holdD` -- enable tied low | **1** | VACUOUS | a hold test has one state BY DESIGN; that is the property under test |
| `resetOnlyD` -- reset tied asserted | **1** | VACUOUS | correct collapse to the reset value |

And in the other direction, three reset-driven states establish no useful
activity at all.  Measured, with the gate rebuilt:

    trace toggle (2 states forever): 6 cycles -- agrees true  states-reached 2
    trace hold (enable low, 1 state): 6 cycles -- agrees true  states-reached 1
    trace reset-only (collapses):     6 cycles -- agrees true  states-reached 1

All three PASS.  Under the section 11 gate all three would have FAILED.

### 12.2 The split

`traceCompare` now returns `(agree, distinctStates)`: a correctness verdict and
a coverage observation, which are different kinds of answer.

* **Agreement** with `interpretDesign` is the verdict.  It is a hard failure.
* **States reached** is reported beside it and is never a failure by itself.

When a design with flops reaches <= 2 states the runner prints

    coverage: INCONCLUSIVE -- N state(s) over 4 cycles.  That is correct for a
    toggler or a hold test and uninformative otherwise; it is NOT evidence of
    unreachable behaviour.

"Coverage-inconclusive", not "degenerate".  **Finite stimuli cannot establish
that a behaviour is unreachable** -- only that this stimulus did not reach it.
Sections 10.5 and 11.2 used the stronger word and should not have.

`btb_gate` stays quarantined, and on the DESCRIPTOR INCONSISTENCY alone --
`flopQAsync ... resetInput=4 activeLow=1` against all 64 flop rows
`resetPin=1779 activeLow=0` with slot 1779 being input 4 -- which is a
certificate/export-boundary discrepancy, independent of any state-count
heuristic and not something this branch should "fix" by editing literals.

### 12.3 The timing partition was also unsound

Section 11.3 multiplied 190.91 s by 11 calls to claim the 1901 s run was
"essentially all reference".  **The product is 2,100 s, which exceeds the
total** -- so at best the two numbers came from different runs with different
state and contention, and at worst the reasoning is circular.  190.91 s is also
per-CYCLE; calling it "70x slower per node" mixed units.

What survives: one `interpretDesign` cycle on `rt_btb_gate` did take 190.91 s,
measured directly, and that is large.  What does NOT survive: any claimed share
of the 1901 s.

`--file-ab` now MEASURES each stage in the same process -- specialize,
`checkResidual`, each reference run, each residual run, control -- and prints a
`STAGE TIMES` line.  Each stage forces its result INSIDE its own interval, via
a printed digest, because Lean reorders pure work across `IO.monoMsNow` (the
trap verified once in the generated C).  Progress is flushed, so a long run
reports as it goes.

## 13. `rt_alu_gate` -- the first targeted CVA6 run

### 13.1 Provenance, recorded before the run

    design     rt_alu_gate.dcert
    sha256     d731ee22a85e77bab50ba0c20225df138e2dd59227d82f2911dfec586432bdb1
    size       220,566 bytes
    source     CVA6 `alu.sv`, exported as `cva6_alu_export`
               (livehd-d4-incremental pass/lean/CVA6_COVERAGE_PLAN.md:125)
    shape      6,137 sources (6,135 const, 2 input), 6,597 nodes, 2 outputs,
               0 flops -- COMBINATIONAL
    inputs     indices 1 and 2 -- SPARSE, index 0 unused, so a vector sized
               from the max index is required and `m*(m-1)/2` would be wrong
    ops        all inside the validated set; max arity 65
    predicted  tl = 18,828,964 with term (B); 3 without it

Unlike `rt_intpipe_alu` (CORE-ET), this is a CVA6 block.

### 13.2 Stock fuel is NOT enough

    fuel 20000/200  (projectDesign's own)
      support: wf true memFree true sources true ops true arities true flopClocks true
      [specialize d=0 345788 ms]
    variant FAILED (MixError.outOfFuel) after 345788 ms
    wall 345.88 s  RSS 104,200 KB  exit 3

Exit 3 is the diagnostic-failure code from 9.3 doing its job: `outOfFuel` is
reported as a failure, not quietly passed.

### 13.3 The stage timer settles the partition question

| | |
|---|---:|
| wall, whole process | 345.88 s |
| `specialize`, forced inside its own interval | 345.788 s |
| everything else (load + support + startup) | **0.09 s** |

**99.97% of the run is specialization**, measured IN-PROCESS rather than
inferred from a different run -- which is what 11.3 did wrongly.

This also disposes of a hypothesis raised while chasing 11.3: that `loadCert`
might have dominated the 190.91 s `--trace-ref` measurement on `btb_gate`.
Parsing a 220 KB certificate here costs ~0.1 s, so it cannot. That leaves the
single `interpretDesign` cycle as the remaining candidate for btb's 190.91 s --
an inference ACROSS designs, not a measurement, and settleable by running
`--file-ab` stage times on btb when a rerun is warranted.  Not rerun now.

### 13.4 Higher budget

Per review, a recorded higher budget is legitimate; the theorem is
budget-parametric and conditional on success (8.7).  Re-running at 200000/2000,
recorded, with every check in 9.3.

### 13.5 At 200000/2000 it passes

    support: wf true memFree true sources true ops true arities true flopClocks true
      [specialize ... d=89996 1226168 ms]
      terms 89996   lit 18062 var 30610 letIn 16317 ite 430 prim 24574
      prims: tl 65  hd 4  consP 12736  isNil 0  bvResize 1633 bvAnd 1135 bvMk 206 eqI 397
      [checkResidual ... d=28899 17 ms]
      checkResidual: ACCEPTED, proved-sufficient bound 28899
      seed 0,1,2,3,4,7: residual ok, matches interpretDesign  (all six)
      trace: 4 cycles -- agrees true  states-reached 1  flops 0
      control: variant interpreter ok vs interpretDesign true
      STAGE TIMES ms: specialize 1226168  checkResidual 17
                      reference-runs 19275 (6)  residual-runs 8959 (6)  control 209133
    wall 1485.48 s  RSS 124,004 KB  exit 0

`states-reached 1` carries no coverage warning and should not: the design has
ZERO flops, so there is no state to move.  The coverage note fires only when
`D.flops.size > 0`.

### 13.6 Measured stage split

| stage | time | share |
|---|---:|---:|
| specialize | 1226.2 s | 83.8% |
| control (interpreter on the design) | 209.1 s | 14.3% |
| reference runs, 6 | 19.3 s | 3.21 s each |
| residual runs, 6 | 9.0 s | **1.49 s each** |
| `checkResidual` | 0.017 s | |
| staged total | 1463.6 s | |
| wall | 1485.48 s | |
| unaccounted | 21.9 s | trace (4 cycles, not individually staged), load, `profileResidual` |

The accounting nearly closes, which is the point: this is a MEASUREMENT, not
the kind of inference 11.3 made.

On these six runs, with this backend, the residual took 1.49 s per cycle
against the interpreter's 3.21 s -- a ratio of 2.15.  Stated that narrowly on
purpose: it is six measurements of one design under one native build, not a
general speedup figure, and nothing here establishes how it moves with design
size, stimulus, or backend.

On btb's 190.91 s: a reference run here costs 3.21 s on 6,597 nodes, so
`btb_gate` at 1,782 nodes really does look far slower per cycle.  But that
compares a STAGED measurement here against an UNSTAGED whole-invocation
measurement there, so it remains an inference across designs; `--file-ab` stage
times on btb would settle it, and btb was not rerun.

Two model checks, both holding:

* `consP` 12,736 against `nSources + nNodes + nFlops + 1` = 12,735 -- the
  environment spine, off by one (8.2).
* `tl` 65 against the model's (A) = 3, a residue of 62.  The CORE-ET ALU's
  residue was 1,205 (8.7), so the uncovered third term is per-design, as 8.7
  said.  Still unidentified.

### 13.7 Coverage, by category, with the status distinction kept

| category | design | executed + reference-compared | budget | path |
|---|---|---|---|---|
| **CVA6, combinational** | `rt_alu_gate` | **YES** -- 6 width-aware stimuli | 200000/2000 (stock 20000/200 FAILS) | experimental |
| CVA6, sequential | `rt_btb_gate` | yes, stock budget | 20000/200 | experimental, QUARANTINED (reset-descriptor inconsistency) |
| CORE-ET, combinational | `rt_intpipe_alu` | yes | 200000/2000 | experimental |

**CVA6 blocks counted toward the 30: one, combinational, EXPERIMENTAL** --
and "CVA6" here means ONE MODULE exported through a configured wrapper
(`cva6_alu_export`), not arbitrary or whole-core CVA6.

"Experimental" is not a hedge, it is the status: every run above uses
`ProtoFast.mixDriver` (the fork, whose `PRes.val` change has no bridge) and
`ProtoVar.hwAPVar` (the interpreter variant, which has no equivalence lemma).
The theorem-covered path is `mixDriver` + `hwAP`, and **zero blocks have been
run on it**.  Sequential CVA6 coverage is zero: the only non-degenerate
candidate, `rt_csr_regfile_gate`, has not been run.

## 14. Durable record, and the gap to a theorem

### 14.1 `experiments.jsonl`

Prose cannot be re-identified later, and "the ALU passed" is meaningless
without the certificate, the code, the budget and the category it passed
under.  `scripts/record_experiment.py` parses a `--file-ab` log and appends one
JSON object per run to `certio/experiments.jsonl`, pinning:

* design name, certificate **sha256** and byte size;
* source provenance, with an explicit note that it is a CONFIGURED EXPORT
  WRAPPER of one module, not arbitrary or whole-core CVA6;
* **code identity** -- sha256 of `proto_probe.lean`,
  `Proto/InterpreterVariant.lean` and `Proto/PartialEvaluatorFast.lean` -- plus
  the git commit, and the binary sha256 when supplied;
* the specializer and interpreter ACTUALLY USED, named as the fork and the
  variant, each with the bridge it is missing;
* the category string, which says in full that the run is certificate-relative
  execution agreement, NOT theorem-covered and NOT RTL equivalence;
* fuel (and the stock budget, for contrast), design shape, support result,
  residual counts, the `checkResidual` bound WITH its meaning, the stimulus
  kinds, the trace line, measured stage times, wall, peak RSS and exit code.

Failed runs are recorded too: the stock-fuel `rt_alu_gate` attempt is in the
file with `exit 3` and null residual fields, which is the honest shape for a
run that produced nothing.

### 14.2 The gap

`certio/PROMOTION_OBLIGATIONS.md` states the two things standing between these
runs and a theorem, in full:

* **A** -- `ProtoFast` must satisfy `mixDriver_iff`.  Change 2 (the zero-shift
  arms) is already discharged by `PVal.shift_zero` / `PEnv.shiftBy_zero`;
  Change 1 (`PRes.val`) is not.  Smallest route: an erasure
  `E : ProtoFast.PRes -> PRes` and six commutation lemmas, transporting the
  host's theorem rather than re-proving it.
* **B** -- `hwPVar` must be adequate.  Smallest route: construct the CANONICAL
  WITNESS `main_agree_var` for the variant and reuse the existing
  `Eval_det` + `ResultRel_canonical` pattern, which needs no surface
  completeness theorem at all.

**B first**, and it is three pieces, not one: **B1** a function-table
congruence (`hwSVar` differs from `hwS` only in `main`'s body, so any
derivation that does not re-enter `main` transports); **B2** the restricted
inlining lemma proved for `main`'s ACTUAL body under its ACTUAL environment;
**B3** generalizing `specializeDesign_correct` over the interpreter.

Three corrections to the first version of this paragraph, all recorded in
`PROMOTION_OBLIGATIONS.md` rather than quietly fixed, because two of them would
have sent the proof work down a route that does not close:

* `specializeDesign_correct` is **not** interpreter-parametric -- its
  hypothesis literally names `hwAP` (`ProjectionCorrect.lean:109`) and its
  proof consumes `hwAP_entry`, `hwAP_erases` and `IHwAdequacyGoal_proved`.  It
  cannot be re-instantiated at `hwAPVar`; hence B3.
* `Eval_det` does **not** supply the missing direction.  Determinism pins a
  value one already has; it cannot manufacture a source evaluation.  The tree
  already has the right pattern -- construct the witness, then read both
  directions off it.
* A's erasure does **not** commute, so "six lemmas and transport" was wrong.
  `primStruct .consP` on `[.val (.stat a), .val (.stat b)]` gives
  `.cons (.stat a) (.stat b)` after erasure, where the host gives
  `.stat (.cons a b)` -- equal in denotation, different as `PRes`.  A needs a
  semantic relation over the whole pass, which is the host development's shape
  rather than a transport of it.

Whether B is practically sufficient is an OPEN EMPIRICAL QUESTION, not a near
certainty: the fork's measured advantage is not only the 2.66x constant factor
-- the zero-shift change measured 31.11x at n = 1024 with the exponent moving
2.91 to 1.90 -- and the `env0` rewrite removed MEASURED growth on the designs
tried, not a proven asymptotic bound.  One `--file-ab`-shaped run with
`mixDriver` substituted for the fork would answer it; a negative result would
make A load-bearing and change the order.  Not run, and not to be launched
alongside another long run.

### 14.3 The manifest records evidence STRENGTH, after getting it wrong once

The first version of `record_experiment.py` had three defects that each made a
record claim more than it knew.  All three are fixed and regression-tested
(`record_experiment.py selftest`, 8 checks).

**It invented success.**  `if exit_code is None: exit_code = 0 if 'STAGE TIMES'
in t` read the presence of the stage-times line as a pass.  But `--file-ab`
prints `STAGE TIMES` and *then* returns 1 when a comparison disagreed, so that
line says nothing about the exit code.  Exit status is now read only from an
explicit trailer; otherwise it is `null` with
`exit_evidence: "MISSING -- the log carries no exit evidence"`.  Regressions
cover a truncated log, a log with stage times and no trailer, and a mismatch
run that printed stage times and exited 1.

**It claimed to pin code it had not hashed.**  Both `rt_alu_gate` rows recorded
`git_commit: 4f64d6aad` alongside `scripts/proto_probe.lean` sha
`056ac894...`.  But `git show 4f64d6aad:...proto_probe.lean | sha256sum` is
`1bf49b56...`; `056ac894...` is the CURRENT tree.  The hashes were taken at
RECORD time, after the run, and described the tree then -- not what executed.
`binary_sha256` was `null` throughout.

The manifest now carries an explicit evidence level:

| level | meaning |
|---|---|
| `captured-pre-launch` | hashed BEFORE the process started -- the only form that pins executed code without assumption |
| `captured-during-run` | hashed while it ran, including `/proc/<pid>/exe` |
| `reconstructed-at-record-time` | hashed AFTER; describes the tree now, NOT what ran |

The two `rt_alu_gate` rows are `reconstructed`, and say so in full: their
binary is **unknown**.  The current binary sha is NOT retrofitted onto them.

For `rt_decoder_gate`, whose run was still in flight, identity was captured
mid-run: `/proc/2795132/exe` hashes **identical** to
`.native-dev/proto_probe` (`efbed80c2b4cda45...`), which is direct evidence the
running image is that file; the tree was clean at `322029ec1`.  That is
`captured-during-run`, not pre-launch, and the record says so.  Future runs
capture before launch.

**Its support check tested for the wrong thing.**  `'false' not in line` passes
a TRUNCATED support line, which contains no `false` either.  It now requires
all six named fields -- `wf`, `memFree`, `sources`, `ops`, `arities`,
`flopClocks` -- to be present AND true, records each field separately, and
flags any that is absent.  The record also keeps the raw log's sha256 and the
control and trace lines verbatim.

## 15. `rt_decoder_gate`, and B1 proved

### 15.1 The run

    rt_decoder_gate.dcert   sha256 f87080df26fcfa7b08b62f157066a14893d95a01db0048fc66e0e883370b72f4
    8,373 sources  8,971 nodes  0 flops -- COMBINATIONAL, CVA6
    support: wf true memFree true sources true ops true arities true flopClocks true
    fuel 200000/2000
      [specialize ... d=138135 2413597 ms]
      terms 138135  tl 478  hd 30  consP 17347
      checkResidual: ACCEPTED, proved-sufficient bound 38902
      seed 0,1,2,3,4,7: residual ok, matches interpretDesign  (all six)
      trace: 4 cycles -- agrees true  states-reached 1  flops 0
      control: variant interpreter ok vs interpretDesign true
      STAGE TIMES ms: specialize 2413597  checkResidual 44
                      reference-runs 44806 (6)  residual-runs 12341 (6)  control 382996
    wall 2900.21 s  RSS 172,360 KB  exit 0

`states-reached 1` again carries no coverage note and should not: zero flops,
no state to move.

Identity was captured MID-RUN, not before launch -- `/proc/2795132/exe` hashed
identical to `.native-dev/proto_probe` (`efbed80c2b4cda45...`), tree clean at
`322029ec1`.  The manifest row records `captured-during-run`, which is weaker
than `captured-pre-launch` and says so.

| stage | time | share |
|---|---:|---:|
| specialize | 2413.6 s | 84.6% |
| control | 383.0 s | 13.4% |
| reference runs, 6 | 44.8 s | 7.47 s each |
| residual runs, 6 | 12.3 s | 2.06 s each |
| staged total vs wall | 2853.8 s / 2900.21 s | 46.4 s unaccounted |

On these six runs, the residual took 2.06 s per cycle against the
interpreter's 7.47 s, a ratio of 3.63.  `rt_alu_gate`'s was 2.15 on its own six
runs.  Two designs is not a trend, and neither figure is a general speedup.

Model checks hold again: `consP` 17,347 against
`nSources + nNodes + nFlops + 1` = 17,345 (off by 2); `tl` 478 against the
model's (A) = 405, a residue of 73.  Residues so far: 1,205 (CORE-ET alu),
62 (cva6 alu_gate), 73 (cva6 decoder_gate) -- per-design, as 8.7 said, and
still unidentified.  **Not being chased.**

### 15.2 Coverage

| category | design | executed + reference-compared | budget |
|---|---|---|---|
| CVA6, combinational | `rt_alu_gate` | yes, 6 stimuli | 200000/2000 (stock FAILS) |
| CVA6, combinational | `rt_decoder_gate` | yes, 6 stimuli | 200000/2000 |
| CVA6, sequential | `rt_btb_gate` | quarantined -- reset-descriptor conflict |  |
| CORE-ET, combinational | `rt_intpipe_alu` | yes | 200000/2000 |

**CVA6 toward the 30: TWO, both combinational, both EXPERIMENTAL.**  Sequential
CVA6: zero.  Theorem-covered path (`mixDriver` + `hwAP`): **zero designs run**.

### 15.3 B1 is proved

`Proto/VariantTransport.lean`, audited by `Proto/ProtoAudit.lean`: 14
directives, `propext`/`Quot.sound` only, no `sorryAx`, no `ofReduceBool`, and
`mainFreeB_hwS` **axiom-free**.

What it is: the function-table congruence `SEval_congr`, with
`MainFreeFuns hwS` DISCHARGED (`mainFreeB_sound` + a kernel `rfl`, not a
`#guard`), instantiated at the REAL `hwSVar` via `sFn_hwSVar`, plus the
corollary `SEval_hwSVar_of_hwS` that B2 will consume.

One thing that fell out of doing it: `goInline` is a `partial def`, so
`ProtoVar.changed` is NOT kernel-reducible and could not have been a
hypothesis.  `sFn_hwSVar` is unconditional instead, because the replacement
keeps the name `main` whatever `goInline` returns -- only the BODY depends on
it.

What it is NOT: variant adequacy.  B2 (the rewrite for `main`'s ACTUAL body),
the canonical witness `main_agree_var`, and B3 (making
`specializeDesign_correct` interpreter-parametric) are untouched.  The
corollary deliberately excludes a term that calls `main`, since that node is
exactly what the rewrite changes.


## 16. ETA discipline: no real-design time estimates

I projected a host specialization of `rt_alu_gate` at "~54 min" from
1226 s x 2.66, and called a run "inside that envelope".  **Withdrawn.**  The
2.66x figure is superseded by this document's own warning at section 14.2: the
fork's advantage is not only that constant factor -- the zero-shift change
measured **31.11x at n = 1024**, with the fitted exponent moving 2.91 -> 1.90
(table in the zero-shift section).  The estimate used a number the file
explicitly flags as incomplete.

Substituting 31.11x would be no better: it is a DIFFERENT SYNTHETIC SHAPE
(`chain`), at one size, and the exponent shift means the ratio is not even
constant across sizes, let alone transferable to a 6,597-node real design.

**There is no reliable real-design ETA, and none should be quoted.**  While a
run is in flight, report only MEASURED elapsed wall, CPU, RSS and the stage it
is in.  CPU saturation is liveness, not progress, and "no transition expected
yet" is a claim about an envelope that does not exist.

### What each runner can actually report

Marking this because a summary promised "all six / trace / control" for both
in-flight runs, and one of them cannot produce those:

| PID | runner | stimuli | trace | control | notes |
|---|---|---|---|---|---|
| 2877239 | `proto_probe --host-var` | seeds **[0,1,4] only** | **NOT RUN** | **NOT RUN** | its own output says "3 seeds, no trace, no control -- FEASIBILITY evidence".  It is also the OLD binary, which predates the checker-rejection gate fix: if its checker rejects, its exit status is the OLD, WRONG one, and that caveat stands regardless of what the present source says. |
| 3238710 | `total_probe` | 6 width-aware | yes | yes | full acceptance through the checked simulator path belongs to THIS run only |

## 17. The native builds are `-O0`, and three profile claims withdrawn

### 17.1 What the sample does and does NOT show

`perf record -F 49 -p 2877239 -- sleep 12`, 569 samples, 0 lost, **no
`-g`/`--call-graph`**.  So these are SELF samples only:

    19.47% lean_is_ctor   10.63% lean_ptr_tag   7.40% lean_to_ctor  ...
     2.82% l_Projection_prepare   2.45% l_Projection_PVal_toPRes
     1.75% l_Projection_PVal_shift

**Withdrawn: "the dominant stacks map to `prepare`/`toPRes`/`shift`".**  Without
call chains the helper samples cannot be attributed to any caller.  What the
sample supports is only that those three functions are ACTIVE, which is
consistent with the earlier profiles -- not that they account for the helper
time.

**Withdrawn: the proposed discriminator** "a moving worklist should change the
distribution, a tight loop should not".  It is not a discriminator in either
direction: homogeneous productive work keeps the same distribution, and a
pathological loop can vary.  A second histogram alone cannot establish progress
or termination.

**Qualified: the binary hash.**  `/proc/2877239/exe` matching
`hostvar-alu-capture.json` identifies the sampled EXECUTABLE.  It is not an
exact source/build reconstruction: that capture was taken with 4 dirty files
and a code list that did not cover every backend-relevant source.

### 17.2 The optimization setting, established rather than assumed

`build-native.sh` calls `leanc -c` with no optimization flag, which says
nothing on its own.  Checked directly:

* `leanc --print-cflags` emits `-fPIC -fvisibility=hidden -ffunction-sections
  -fdata-sections -fstack-clash-protection` and **no `-O` at all**;
* `leanc -v` shows it drives
  `.elan/toolchains/leanprover--lean4---v4.31.0/bin/clang`, **clang 22.1.4**,
  with no `-O` in the command line.  clang's default is `-O0`;
* the SHIPPED objects carry Lean's `static inline` runtime helpers as REAL
  local text symbols -- `7 of 7` of `lean_is_ctor`, `lean_ptr_tag`,
  `lean_to_ctor`, `lean_ctor_get`, `lean_ctor_set`, `lean_align`,
  `lean_is_scalar` in both `Projection_PartialEvaluator.o` and
  `proto_probe.o`.  At `-O2` those are inlined away;
* A/B on the SAME generated `.c`, into a scratch directory, nothing shipped
  touched:

      leanc (default) -> 5 helper symbols, 404,512 bytes
      leanc -O2       -> 2 helper symbols, 624,448 bytes

So **the current `build-native.sh` path produces `-O0` native binaries.**

NARROWER than the first version of this sentence, which said "every wall-clock
number recorded in this file".  It does not: this file also contains
INTERPRETED timings (which never go through `leanc` at all) and instrumented
callgrind/perf runs, and the build provenance of the older measurements is
incomplete.  The claim covers the native binaries built by this script.  That is also the straightforward
reading of a sample in which ~60% of the time sits in functions that would not
exist at `-O2`.

### 17.3 What this does and does not invalidate

* **Absolute times are inflated by an unknown factor.**  1,226 s, 2,413 s,
  190.91 s and the rest are `-O0` numbers.  No hardware extrapolation should be
  made from them, and none has been.
* **"Ratios between two `-O0` builds are less affected" is WITHDRAWN.**  It
  does not follow without measurement: `-O0` can move the bottleneck, and
  helper inlining is compiler-dependent.  2.66x and 31.11x are `-O0` ratios and
  nothing is claimed about what they become at `-O2`.
* **Nothing about CORRECTNESS changes.**  Every theorem, every `#guard`, and
  every reference-agreement result is independent of optimization level.

### 17.4 The bounded A/B -- RUN

(The scratch `.c` A/B COMPILE in 17.2 had already happened when 17.2 was
written; "proposed, not run" referred only to the executable TIMING
comparison.  That is what follows.)

`scripts/ab_probe.lean`, built twice from the SAME 37 generated `.c` files,
with every object AND the link compiled at an explicit `-O0` / `-O2` -- not
just the probe or the link flag.  Separate project-local trees under
`.perfwork/abopt/{O0,O2}`; `.native-dev` was not touched and both in-flight
experiments ran throughout.  Provenance, with hashes, in
`.perfwork/abopt/provenance.txt`.

Flag effectiveness, checked at the object level rather than assumed:
`Projection_PartialEvaluator.o` carries **7/7** Lean helper symbols at `-O0`
and **2/7** at `-O2`.

**Results and shapes are IDENTICAL across builds; only times differ.**  Both
exit 0.

    seq reset / enabled / held   next == 0 / 12 / 3, ref-equal   both builds
    seq refusal                  runtimeShape                    both builds
    seq trace (3 cycles)         next-states [0,12,12], ref-equal both builds

    n     -O0 spec   -O2 spec   ratio      terms / bound (identical)
    8        7 ms       1 ms     7.0x      118 / 33
    16      14 ms       4 ms     3.5x      206 / 57
    32      50 ms      13 ms     3.8x      382 / 105
    64     227 ms      43 ms     5.3x      734 / 201
    wall   330 ms      70 ms     4.7x

MEASUREMENT LIMITS, stated rather than buried: 1 ms resolution makes n = 8 and
16 unreliable; n = 64 (227 -> 43) is the firmest point; one run per build, no
repetition.  **The ratio is NOT constant across n** (7.0, 3.5, 3.8, 5.3), so it
is not a factor that may be carried to a real design, and none is carried.

### 17.5 Proposal, not a switch

On these fixtures `-O2` is several times faster with byte-identical results.
That is a reason to PROPOSE the optimized path for future acceptance runs, and
the proposal is: add an explicit optimization flag to `build-native.sh`, rebuild
under a NEW artifact name, and re-establish the fixture regressions on it
before any real-design run is attributed to it.  Switching silently would
invalidate the comparability of every number already recorded, and optimized
native executions still need their own regression evidence -- the identical
results above are for FIXTURES, not for a real design.

## 18. The covered backend, executed on a real design

`total_probe` built at explicit `-O2`, `mixDriver` + `hwAPVarT`, on
`rt_alu_gate` at 200000/2000.  **Exit 0**, wall **61,330.78 s (17 h 02 m)**,
peak RSS **24,904 KB**.

    support: wf true memFree true sources true ops true arities true
             flopClocks true | ALL true
    residual 89,996 terms, checker bound 28,899
    seed 0,1,2,3,4,7: ok, matches interpretDesign   (all six)
    trace: 4 cycles, INDEPENDENT states -- agrees true, states-reached 1, flops 0
    control: variant interpreter matches interpretDesign true
    STAGE TIMES ms: specialize+check 61,267,341   reference-runs 8,902 (6)
                    step-runs 9,897 (6)   control 27,513

`states-reached 1` carries no coverage note and should not: zero flops.

Pre-launch captured, binary sha256
`9168c8362030a190d9b44549d77cd7652486737a6c9053cf4e13966f928ba771`, cert
`d731ee22...432bdb1`, 38 object hashes, 14 source hashes, clean tree at
`ecf59cf7d`.  Recorded in `experiments.jsonl` under runner `total-probe`.

### 18.1 EXACT boundaries

* ONE NATIVE EXECUTION of an existing certificate.  The concrete
  `mixDriver sf wf hwAPVarT [encDesign D] = .ok R` is **NOT kernel-certified**;
  `mixDriver` does not kernel-reduce at nontrivial fuel.  The theorems
  (`IHwAdequate_varT`, `specializeDesign_varT_correct`, `simSound_varT`) are
  about that hypothesis, and this run does not discharge it.
* **NOT a new CVA6-block count.**  `rt_alu_gate` was already one of the two
  experimental combinational blocks; this is the SAME design on a different
  backend.  The count stays **two**.
* NOT RTL equivalence, and not a sequential result -- the design has no flops.
* The `-O2` flags are recorded ALONGSIDE the `-O0` records, not replacing them.

### 18.2 Two measured facts worth stating plainly

**The residual is identical to the fork backend's.**  Same design, same budget:

    fork  -O0  ProtoFast + hwAPVar   89,996 terms, bound 28,899
    host  -O2  mixDriver  + hwAPVarT 89,996 terms, bound 28,899

Identical size and identical checker bound.  That is a strong agreement signal
between the two backends; it is not a proof that they produce the same program.

**The covered backend took 61,267,338 ms where the fork took 1,226,168 ms**,
on the same certificate and budget.

**That 50x is NOT a controlled comparison, and calling it a "lower bound on the
specializer difference" was wrong.**  Three things differ at once, per
`PERFORMANCE_INVESTIGATION_PLAN.md` H4: a different specialization
implementation (`ProtoFast.mixDriver` vs `mixDriver`), different compiler flags
(`-O0` vs `-O2`), and different source snapshots.  My reasoning -- "`-O2` helps
the host, so the gap can only be larger" -- fixes the sign of ONE confounder
and says nothing about the other two.  The controlled number requires the
same-source `-O0`/`-O2` pair, and the `-O0` half is still running.

What survives: the covered backend took 17 hours on one combinational CVA6
block, measured.  That alone makes B's practical sufficiency DOUBTFUL and keeps
obligation A on the table, but it does not yet establish how much of the gap
is the representation change.

**And on this design the residual is NOT faster than the interpreter**:
reference 1.48 s/cycle against step 1.65 s/cycle.  Two caveats, both load-
bearing: `step` goes through `stepOf`, which adds a `runtimeOK` check and
`runProjected` at the checked bound, where the earlier fork figure used
`runResidAt` directly -- so this is NOT like-for-like with the 2.15x recorded
in section 13.6; and a single run of each.  What it does show is that no
speed-up should be assumed for the checked simulator path on this design.

## 19. Coverage inventory -- the 30-block milestone is certificate-limited

Coverage is first priority (`PERFORMANCE_INVESTIGATION_PLAN.md`).  Before
queuing more runs, the obvious question is what there is to run.  The answer
changes the shape of the milestone.

### 19.1 Nineteen files, SIX distinct CVA6 blocks

Grouping `livehd-d4-incremental/temp/*.dcert` by `(sources, nodes)`:

| shape | files | what they are |
|---|---:|---|
| 97,774 / 108,666 | 1 | `cva6_hpdcache_subsystem` |
| 32,822 / 34,874 | 1 | `csr_regfile` |
| 8,658 / 11,681 | 1 | `aes` |
| 8,373 / 8,971 | **4** | `decoder_gate` + `dec_d1/d3/d6` -- MUTANTS of one block |
| 6,137 / 6,597 | 1 | `alu_gate` |
| 4,045 / 5,118 | 1 | `intpipe_alu` (CORE-ET, not CVA6) |
| 1,784 / 1,782 | **7** | `btb_gate` + `btb_m1/m4/m5/m6/m7/m8` -- MUTANTS of one block |
| tiny | 3 | `fx_*` operator fixtures |

The `dec_d*` and `btb_m*` files are deliberately perturbed copies -- d4's
negative controls for its digest test -- with identical shapes and different
hashes.  **They are not separate blocks and must not be counted as such.**

So the distinct CVA6 blocks present are **six**:

| block | sources / nodes | supported | status |
|---|---|---|---|
| `alu` | 6,137 / 6,597 | yes | **executed + reference-compared** (fork backend; and again on the covered backend) |
| `decoder` | 8,373 / 8,971 | yes | **executed + reference-compared** (fork backend) |
| `btb` | 1,784 / 1,782 | yes | QUARANTINED -- reset-descriptor inconsistency |
| `csr_regfile` | 32,822 / 34,874 | yes | not run |
| `aes` | 8,658 / 11,681 | **no** -- `sources`, `ops` | blocked by support |
| `cva6_hpdcache_subsystem` | 97,774 / 108,666 | **no** -- `memFree`, `sources`, `ops` | blocked by support |

### 19.2 The blocker

> **WITHDRAWN -- see section 20.**  What follows was written after looking at
> `livehd-d4-incremental/temp/*.dcert` only.  The authoritative corpus is
> `temp/cva6_30_auth/blocks/<name>/lean/<name>_gate.dcert`, which I did not
> look in.  **All 30 frozen CVA6 blocks have certificates on disk**, 22 of the
> 29 present are SUPPORTED, and certificate supply is NOT the binding
> constraint.  The paragraph below is wrong and is kept so the error is
> visible.

`CVA6_COVERAGE_PLAN.md` targets **82** CVA6 modules.  This branch has
certificates for **6**, of which **4** are supported and **1** is quarantined.

**The 30-block milestone cannot be reached from the certificates present**, at
any specialization speed.  The binding constraint is CERTIFICATE SUPPLY, which
is produced by `livehd-new`'s dense exporter -- a DATA dependency of this
branch, not a code one, and deliberately so.

That reorders the practical work.  Specialization cost determines how long each
block takes; it does not determine how many blocks exist.  Of the two
unsupported blocks, `aes` fails on `sources` and `ops` only -- which is
operator-coverage work (Phase 3) rather than an exporter problem -- while
`hpdcache` additionally fails `memFree`, which is Phase 7 memory work.

### 19.3 Compact baseline table

Per `PERFORMANCE_INVESTIGATION_PLAN.md` step 2.  `experiments.jsonl` holds the
full records with hashes; this is the summary.  Native execution, theorem
coverage, and kernel-certified concrete specialization are SEPARATE columns.

| ID | design | runner / backend | flags | fuel | specialize | wall | peak RSS | residual / bound | checks | exit | native | theorem-covered | kernel-certified |
|---|---|---|---|---|---:|---:|---:|---|---|---:|---|---|---|
| E1 | `alu_gate` | file-ab, fork+hwAPVar | -O0 | 20000/200 | — | 345.88 s | 104,200 KB | — | — | 3 | yes | no | no |
| E2 | `alu_gate` | file-ab, fork+hwAPVar | -O0 | 200000/2000 | 1,226,168 ms | 1,485.48 s | 124,004 KB | 89,996 / 28,899 | 6 seeds, trace, control | 0 | yes | no | no |
| E3 | `decoder_gate` | file-ab, fork+hwAPVar | -O0 | 200000/2000 | 2,413,597 ms | 2,900.21 s | 172,360 KB | 138,135 / 38,902 | 6 seeds, trace, control | 0 | yes | no | no |
| E4 | `alu_gate` | total-probe, **mixDriver+hwAPVarT** | **-O2** | 200000/2000 | 61,267,338 ms | 61,330.78 s | **24,904 KB** | 89,996 / 28,899 | 6 seeds, trace, control | 0 | yes | **conditionally** | **no** |

E1-E3 use the diagnostic fork, which has no bridge, so "theorem-covered: no".
E4 uses `mixDriver` + `hwAPVarT`, for which `IHwAdequate_varT`,
`specializeDesign_varT_correct` and `simSound_varT` ARE proved -- but
conditionally on `mixDriver … = .ok R`, which no run discharges, so
"kernel-certified: no" for every row.

Two in-flight `-O0` runs (PIDs 2877239, 3238710) are not in the table; they
will be added with their own provenance when they exit.

**CVA6 blocks counted toward 30: TWO** (`alu`, `decoder`), both combinational,
both experimental.  E4 is the same `alu` on a different backend and adds none.

## 20. Certificate supply: the corpus exists, and the real blocker is a reset defect

### 20.1 Correcting section 19.2

Section 19.2 concluded the 30-block milestone was certificate-limited.  **That
was wrong, and the cause was looking in one directory.**  I inventoried
`livehd-d4-incremental/temp/*.dcert` (19 files, 6 distinct blocks) and did not
look in `temp/cva6_30_auth/blocks/`.

The canonical 30 are tracked: `pass/lean/manifests/cva6_30_frozen.tsv` on
`direction-3-translation-validation` at `7f5b1bd27`, a size-ranked subset of
the authoritative 78 in `CVA6_COVERAGE_PLAN.md` Phase 2, with names in
`pass/lean/tests/d4/cva6_30.list`.  Matching those names against every
`.dcert` under `projects/`: **30 of 30 have a certificate**, 29 of them as
`temp/cva6_30_auth/blocks/<name>/lean/<name>_gate.dcert`
(`cva6_hpdcache_wrapper` is in `cva6_30_auth2`).

So **certificate supply is NOT the binding constraint.**

### 20.2 Support across the authoritative 29

Measured with the current-source checker, all six fields:

**22 SUPPORTED.**  7 not: `aes` (sources, ops), `cva6_hpdcache_subsystem`
(memFree, sources, ops), `fpu_wrap` (sources, ops), `load_unit`
(sources, ops), `store_unit` (sources, ops), `mult` (ops), `multiplier` (ops).

Several supported blocks are MUCH smaller than the ALU (6,597 nodes):

| block | sources / nodes | flops |
|---|---|---:|
| `ariane_regfile` | 1,034 / 1,121 | 32 |
| `serdiv` | 1,082 / 1,180 | 12 |
| `instr_queue` | 1,105 / 1,190 | 71 |
| `commit_stage` | 1,193 / 1,283 | 0 |
| `instr_scan` | 1,235 / 1,264 | 0 |
| `store_buffer` | 1,331 / 1,387 | 62 |
| `btb` | 1,784 / 1,782 | 64 |
| `compressed_decoder` | 1,913 / 2,022 | 0 |
| `pmp` | 2,881 / 3,213 | 0 |
| `cva6_ptw` | 3,423 / 3,774 | 11 |
| `pmp_data_if` | 4,062 / 4,505 | 0 |

### 20.3 THE ACTUAL BLOCKER: a systemic reset-polarity defect

The conflict found on `btb`, and then on `csr_regfile`, is not per-design.
Scanning all 29 by pure parse:

    sequential blocks: 20     with reset-polarity CONFLICT: 20

**Every sequential block in the authoritative corpus has it**, and always the
same shape: the `flopQAsync` SOURCES declare reset on input `k` with
`activeLow = 1`, while every `FlopDesc` row for the same input `k` declares
`activeLow = 0`.

    ariane_regfile  32 flops  src (2,1)   flop (2,0)
    bht            256        (4,1)            (4,0)
    btb             64        (4,1)            (4,0)
    csr_regfile    136       (22,1)           (22,0)
    frontend       409       (14,1)           (14,0)
    issue_stage    332       (17,1)           (17,0)
    scoreboard     243       (10,1)           (10,0)
    ... 20 of 20, no exceptions

| | |
|---|---|
| **owner** | the DCERT1 exporter -- `pass/lean/pass_lean.cpp` and `design_cert_export.hpp` in `livehd-d4-incremental` (the writer is there, not in `livehd-new`).  NOT this branch. |
| **evidence** | the static scan above, reproducible from the certificates alone; plus `DIRECTION4_INCREMENTAL.md:516`, which already records the `btb` case and attributes it to "the `negreset` conflation the `FlopDesc` docstring warns about". |
| **consequence** | NO trustworthy sequential CVA6 coverage is obtainable from ANY of these certificates.  Differential agreement (residual vs `interpretDesign`) stays valid, because both sides read the same contradictory metadata. |
| **what would fix it** | one convention decision in the exporter, applied to both the source and the `FlopDesc` paths, then re-emission.  It is a single systematic bug, not 20. |

### 20.4 Two counts, kept apart from here on

| count | meaning | now |
|---|---|---|
| **differential** | specialized, checker-accepted, residual agrees with `interpretDesign` on the recorded stimuli | **2** (`alu`, `decoder`) |
| **semantically trusted** | the above AND the certificate's metadata is self-consistent, so the agreement is about the intended circuit | **2** -- both combinational, so the reset defect cannot bite them |

A sequential block that passes the gate adds to the FIRST count only, until the
exporter defect is fixed.

### 20.5 Concrete next step

**Nine blocks have ZERO flops**, so the reset defect cannot affect them, and
eight of those are supported: `alu` and `decoder` (done), plus `alu_wrapper`,
`commit_stage`, `compressed_decoder`, `instr_scan`, `pmp`, `pmp_data_if`.
`aes` is the ninth and is unsupported.

Those six un-run combinational blocks are the only ones that can raise the
SEMANTICALLY TRUSTED count today, and four of them are small
(`commit_stage` 1,283 nodes, `instr_scan` 1,264, `compressed_decoder` 2,022,
`pmp` 3,213) -- all well under the ALU's 6,597, so each should cost far less
than the ALU's run.

NOTE on certificate identity: `cva6_30_auth`'s `csr_regfile_gate` is
32,853 / 34,897, while the in-flight run uses `temp/rt_csr_regfile_gate.dcert`
at 32,822 / 34,874.  **Different revisions of the same block** -- the hashes
and shapes must travel with any result.

## 21. Coverage: six more zero-flop CVA6 blocks, and the CSR failure

### 21.1 Reproducible corpus audit

`scripts/cva6_corpus_audit.py` replaces the earlier ad-hoc scan.  One command,
from the certificates alone, printing the full per-block table (no ellipsis),
every sha256, and a `--json` dump:

    python3 scripts/cva6_corpus_audit.py \
      --frozen <d4>/pass/lean/tests/d4/cva6_30.list \
      --corpus <d4>/temp/cva6_30_auth/blocks \
      --corpus <d4>/temp/cva6_30_auth2/blocks \
      --json   .perfwork/cva6_corpus_audit.json

Frozen list: `pass/lean/manifests/cva6_30_frozen.tsv` on
`direction-3-translation-validation` @ `7f5b1bd27`; names at
`pass/lean/tests/d4/cva6_30.list`.  Support is NOT computed by that script --
it is Lean's, via `proto_probe --support`, and the script prints that command
rather than duplicating the logic.

Result: **30 of 30 named blocks have a certificate**, 0 missing.

**CORRECTION to section 20.3: the figure is 21 of 21, not 20 of 20.**  The
earlier scan missed `cva6_hpdcache_wrapper`, which lives in `cva6_30_auth2`.
With it included: **21 sequential blocks, 21 with the reset-polarity conflict,
9 combinational blocks on which the defect cannot apply.**

### 21.2 E5 -- `csr_regfile`, FAILED

    cert  temp/rt_csr_regfile_gate.dcert, sha 54c11010...fa54, 32,822 / 34,874
    MixError.outOfFuel after 3,237,243 ms (53 m 57 s), exit 3
    wall 3,237.32 s, RSS 93,320 KB

No checker-accepted residual and no comparison, so it adds **zero** to either
count.  Attached to THAT certificate only: the canonical
`cva6_30_auth` `csr_regfile_gate` is 32,853 / 34,897, sha `54c110...` vs a
different file -- a DIFFERENT REVISION, and this failure says nothing about it.

### 21.3 E6-E11 -- six zero-flop blocks, all exit 0

Canonical corpus, `--file-ab` full gate, one at a time, smallest first.  Every
one: all six support fields true, `checkResidual` accepted, six width-aware
stimuli matching `interpretDesign`, threaded trace agreeing, control agreeing.

| block | sources / nodes | specialize | bound | ref s/cyc | residual s/cyc | ratio | wall |
|---|---|---:|---:|---:|---:|---:|---:|
| `instr_scan` | 1,235 / 1,264 | 6,592 ms | 5,711 | 0.656 | 0.059 | **11.1x** | 15.6 s |
| `commit_stage` | 1,193 / 1,283 | 6,308 ms | 5,448 | 3.014 | 0.145 | **20.8x** | 42.8 s |
| `compressed_decoder` | 1,913 / 2,022 | 16,447 ms | 8,962 | 0.300 | 0.080 | **3.7x** | 25.8 s |
| `pmp` | 2,881 / 3,213 | 36,402 ms | 14,277 | 0.128 | 0.190 | **0.7x** | 45.5 s |
| `pmp_data_if` | 4,062 / 4,505 | 76,425 ms | 19,743 | 1.031 | 0.466 | **2.2x** | 115.3 s |
| `alu_wrapper` | 6,134 / 6,595 | 172,645 ms | 28,889 | 1.542 | 1.470 | **1.0x** | 256.5 s |

**There is no uniform speedup.**  Across all eight measured blocks the residual
ranges from **0.7x (slower than the interpreter) to 20.8x faster**, with no
visible relation to node count -- `pmp` at 3,213 nodes is slower while
`commit_stage` at 1,283 is 20.8x faster.  Any claim that projection "is faster"
needs a per-design qualifier.

Specialization, by contrast, does rise with node count on this backend:
6.6 s at 1,264 nodes to 172.6 s at 6,595.

### 21.4 Counts

| count | meaning | value |
|---|---|---|
| **differential** | specialized, checker-accepted, residual agrees with `interpretDesign` on the recorded stimuli | **8** |
| **no known reset conflict** | the above AND the per-flop reset scan finds no conflict -- which for a zero-flop block is vacuous | **8** |

All eight are zero-flop, so the systemic reset defect cannot apply to any of
them -- which is why the two counts coincide today.

**The second count was called "semantically trusted".  That name overclaimed
and is withdrawn.**  Zero flops excludes THIS reset defect and nothing else: it
does not establish that a certificate's metadata is self-consistent in general,
and it says nothing about whether the certificate expresses the intended RTL.
Native differential coverage, reset-conflict status, and RTL/kernel evidence
are three separate things and are kept separate.  They are
`alu`, `decoder`, `commit_stage`, `instr_scan`, `compressed_decoder`, `pmp`,
`pmp_data_if`, `alu_wrapper`.

**That is every supported zero-flop block in the frozen 30.**  The ninth
combinational block, `aes`, is unsupported (`sources`, `ops`).  Going further
requires either operator coverage for `aes`, or the exporter reset fix to make
the 21 sequential blocks trustworthy.

PROVENANCE NOTE: `alu` was run on the canonical certificate (sha `d731ee22...`,
identical to `temp/rt_alu_gate.dcert`).  **`decoder` was NOT** -- E3 used
`temp/rt_decoder_gate.dcert` (8,373 / 8,971, sha `f87080df...`) where the
canonical `decoder_gate` is 8,340 / 8,938, sha `23b4f031...`.  Different
revisions of the same block.  So **7 of the 8 are canonical-corpus runs**, and
`decoder` should be re-run on the canonical certificate before the count is
quoted as "8 of the frozen 30".

Status for every row: NATIVE / EXPERIMENTAL.  Not RTL equivalence, not kernel
proof -- no run discharges the concrete `hproj`, and the fork backend has no
bridge.

## 22. E12 `decoder` canonical, and a CONTROLLED host-vs-fork measurement

### 22.1 E12 -- canonical `decoder`, exit 0

E3 used `temp/rt_decoder_gate` (8,373 / 8,971, `f87080df...`); the canonical
`cva6_30_auth` certificate is 8,340 / 8,938, `23b4f0316c46af8d...`.  Re-run
through the same gated `-O2` `--file-ab` runner, binary
`1002f171cd5b9ff6...`, clean tree at `69dcfe359`:

    support: all six true
    checkResidual ACCEPTED, bound 38,754
    seeds 0,1,2,3,4,7 all match interpretDesign
    trace 4 cycles agrees; control agrees
    STAGE TIMES ms: specialize 330,909  reference-runs 16,086 (6)
                    residual-runs 11,290 (6)  control 52,040
    wall 455.00 s   RSS 26,008 KB   exit 0

So **all 8 counted blocks are now canonical-corpus runs.**  Residual
2.68 s/cycle against the reference's 1.43 s/cycle -- 0.5x, i.e. SLOWER, on this
certificate.

### 22.2 Reset audit corrected to per-flop matching

The first `reset_audit` compared two SETS of `(input, activeLow)` pairs
collected across the whole design, so it could pair a source pin with a flop
that never uses it, and it silently labelled an unresolved reset slot `ok`.

Now matched by FLOP IDENTITY -- `flopQAsync` source *i* describes flop *i* and
is compared with `FlopDesc` row *i* only -- with explicit `unknown:` verdicts
for an out-of-range slot, a non-`input` reset driver, or a differing pin.  It
is labelled in the output as a CANDIDATE CONFLICT SCAN, not a claim about
intended RTL semantics.

Sharper result: **21 sequential, 21 with at least one conflict, 16 conflicted
on EVERY flop, 0 unknown**, 9 combinational.  The five partial ones carry a few
flops with no `flopQAsync` source (`issue_read_operands` 88+1,
`issue_stage` 331+1, `fpu_wrap` 269+6, `cva6_hpdcache_wrapper` 491+24,
`cva6_hpdcache_subsystem` 518+24).

### 22.3 EXPERIMENT P1 -- is the host's cost quadratic in SOURCE count?

**Hypothesis** (plan H3): on the proved `mixDriver`, repeated preparation and
environment shifting dominate, and the fork's `PRes.val` + zero-shift changes
remove exactly that.  **Prediction:** the fork is faster at every size and the
gap WIDENS, because per-node spine work grows with the spine.

**Control:** both backends in ONE process, same source, build flags, input and
budgets, `-O2` throughout.  The one documented difference is the interpreter --
host `hwAPVarT` (from the TOTAL `rewrittenT`), fork `hwAPVar` (from the PARTIAL
`goInline`) -- and `--rewrite-agree` shows those two transforms produce the SAME
`main` body by exact `BEq`.  That is evidence, not a theorem.

**Node ladder** (`chainD`, sources fixed at 2).  Residual sizes identical, both
reference-checked:

    n      host    fork   ratio
    8      1 ms    1 ms    1.0x
    16     1 ms    2 ms    0.5x
    32     6 ms    3 ms    2.0x
    64    24 ms   10 ms    2.4x
    128  126 ms   36 ms    3.5x

Prediction held in shape, but 3.5x at n = 128 is far short of what the real
designs show -- because `chainD` holds SOURCES at 2, and section 7 measured
that source count, not node count, drives the spine.

**Source ladder** (`srcD`, nodes fixed at 64, sources varied).  Residual sizes
identical at every point:

    nsrc   host ms   fork ms   ratio   host exp   fork exp
      16        58        21    2.8x          -          -
      64       145        34    4.3x       0.66       0.35
     256     1,128       123    9.2x       1.48       0.93
    1024    14,190       515   27.6x       1.83       1.03
    4096   231,974     2,616   88.7x       2.02       1.17

**RESULT: the host is ~QUADRATIC in source count (exponent rising to 2.02);
the fork is ~LINEAR (settling near 1.1).**  That is the measured mechanism
behind the real-design gap: `rt_alu_gate` has 6,137 sources, where a
quadratic-vs-linear difference is large.

**Confounders, stated.**  One run per point; ms resolution makes `n = 8/16`
unreliable; the interpreters differ as described; the source ladder checks
residual-size identity but not reference agreement at every point (the node
ladder checks both).  And this does NOT retroactively decontaminate the real
ALU pair -- that one still differs in flags and source snapshots, so this
ladder is the controlled evidence, not that.

**Decision (plan H3 rule).**  The measured hotspot and the predicted complexity
reduction agree, so a representation change is justified -- and the change is
already written and measured: it is obligation A, the fork's `PRes.val` bridge.
This strengthens the 18.2 conclusion from "B's practical sufficiency is
doubtful" to a measured scaling difference in source count.

**Next experiment (P2), not run:** vary source count on the PROVED path with a
`perf` sample at the largest size that stays bounded, to attribute the
quadratic term to `prepare` / `PVal.toPRes` / `PVal.shift` by call chain rather
than by self-sample.  `nsrc = 4096` takes 232 s on the host, which is long
enough to sample properly.
