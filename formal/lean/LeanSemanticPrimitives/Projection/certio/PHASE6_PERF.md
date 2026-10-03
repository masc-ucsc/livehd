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

**Fuel is unchanged.**  On `rt_intpipe_alu` at `projectDesign`'s own
20000/200, the variant still fails:

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

* the rewrite is a SINGLE-USE LET INLINING on the surface syntax, with the four
  side conditions above.  The lemma to prove is the generic one -- inlining a
  let whose bound variable occurs exactly once, in a strict position, in the
  immediately following binding -- stated over `SEval`, where
  `SurfaceSemantics.lean` already provides the relation and `SEval_sound` the
  bridge down to `Eval`;
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

* **Fuel, not residual size, is the binding constraint** (8.3).  The ALU still
  fails at `projectDesign`'s own 20000/200 WITH the rewrite.  None of these
  designs becomes projectable on this evidence.
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

It does **not** count toward the 30-block destination, for two independent
reasons, either of which alone is disqualifying:

1. **Raised fuel.**  `projectDesign` as defined (20000/200) still returns
   `outOfFuel` on this design WITH the variant (8.3).  No fuel-respecting run
   produced this residual.
2. **Unproved interpreter.**  The variant is `hwSVar`, not `hwS`.
   `IHwAdequate_proved` says nothing about it, and the equivalence lemma in 8.4
   does not exist yet.

It is also one cycle, not a trace: `trace_agree` has not been exercised here.
