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

### 1b. Where the interpreter stack ceiling actually is

A stack overflow fails in well under a second; a run that is merely slow does
not. So a short timeout separates them cleanly.  `chainD`, interpreted,
120 s budget:

    n = 1024   timed out at 120 s, no stack error
    n = 2048   timed out at 120 s, no stack error
    n = 2560   STACK OVERFLOW  (deep recursion at 'interpreter')
    n = 3072   STACK OVERFLOW
    n = 4096   STACK OVERFLOW
    n = 8192   STACK OVERFLOW

so the interpreted ceiling on `chainD` is between 2048 and 2560 nodes, and
`rt_intpipe_alu` at 5118 is above it -- which is why the runner hit a stack
error there and not a timeout.

Natively, at the same depths:

    n = 4096   survived 120 s with no stack error (still computing)

so the native build does move that wall, as expected. It is the TIME wall it
leaves untouched.

### 1c. A FOURTH wall: the hardcoded specialization fuel

`projectDesign` is `mixDriver 20000 200`. Natively, with per-stage reporting:

    n = 4096   no fuel error within 25 s  (would succeed, given time)
    n = 5118   no fuel error within 25 s
    n = 6144   no fuel error within 25 s
    n = 7168   projectDesign FAILED (MixError.outOfFuel) after 100 ms
    n = 8192   projectDesign FAILED (MixError.outOfFuel) after  93 ms

A hard ceiling between 6144 and 7168 nodes that no amount of time or stack
fixes -- and it fails FAST and HONESTLY, with an `.error`, not a wrong answer.

`rt_intpipe_alu`'s 5118 nodes are BELOW it, so fuel is not what stops that
design. Recorded because it is the next wall after the time one.

Residual size is linear (`15n + 33`, as `Scaling` already pins) and the checked
bound is linear (`3n + 5`). The blow-up is entirely in the specializer's WORK,
not in what it produces.

Extrapolating cubic from the n = 1024 point, `rt_intpipe_alu`'s 5118 nodes cost
roughly `(5118/1024)^3 x 417 s ~ 14 hours`. A native build alone does not reach
it; that is why native is recorded here as a DIAGNOSTIC and not as the fix.

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

## 3. Status

No optimization has been applied. The three candidate fixes differ sharply in
proof cost against `PartialEvaluatorCorrect.lean`, and `prepare` is named in
`prepare_ok`, `prepare_sound`, `prepare_peel`, `prepare_cons_split`,
`prepare_cons_join_left/right`, `prepare_total_binds` and `prepare_hot_path`,
so none of them is local. Recorded here so the next increment starts from a
measurement rather than a guess.
