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
constraint, directly observed rather than inferred.  The residual was not run
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
