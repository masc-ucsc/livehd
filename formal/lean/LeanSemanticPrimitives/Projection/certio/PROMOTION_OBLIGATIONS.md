# The two obligations between the experiments and a theorem

> **On `experiments.jsonl` itself.**  The manifest records three strengths of
> evidence about what actually ran and never upgrades one to another:
> `captured-pre-launch` (hashes taken before the process started -- the only
> form that pins executed code without assumption), `captured-during-run`
> (including `/proc/<pid>/exe`, which is evidence the running image is that
> file), and `reconstructed-at-record-time` (hashes of the tree AFTER the run,
> which describe the tree now and NOT what executed).  The two `rt_alu_gate`
> rows are `reconstructed`: their binary is unrecorded and UNKNOWN.  Exit
> status is read only from an explicit trailer and is otherwise `null` --
> `--file-ab` prints `STAGE TIMES` even when a comparison disagreed and it
> returns 1, so that line is not evidence of success.

Every run recorded in `experiments.jsonl` is EXPERIMENTAL in a precise sense:
it used `ProtoFast.mixDriver` and `ProtoVar.hwAPVar`, and neither is connected
to the proved development.  The proved path is `mixDriver` + `hwAP`, and **no
design has been run on it**.

Thirty experimental runs would still be zero verified coverage.  This file
states exactly what is missing.

> **Revision note.**  The first version of this file got three things wrong and
> they are corrected below, each marked.  They are kept visible rather than
> silently fixed because two of them would have sent the proof work down a
> route that does not close.

## Obligation A -- `ProtoFast` satisfies the specializer specification

`Proto/PartialEvaluatorFast.lean` is a copy of `PartialEvaluator.lean` with two
changes.

**Change 2 -- the zero-shift arms.**  `PVal.shift 0 v = v` and
`PEnv.shiftBy 0 env = env`.  `PartialEvaluatorCorrect.lean:76`
(`PVal.shift_zero`) and `:88` (`PEnv.shiftBy_zero`) are exactly those
equations.  **But having the identities is not having them USED**: they still
have to be threaded through whatever simulation relation discharges Change 1,
at every site where the fork takes the fast arm and the host does not.

**Change 1 -- `PRes.val`.**  `PRes` gains `| val : PVal → PRes`, with
`PVal.toCode`, `prepare (.val v) = ⟨[], v⟩`, `PRes.total (.val _) = true`,
`peelHd`/`peelTl`/`peelIsNil` answering from the spine, and the `var` rule
returning `.val (.cons a b)`.

The obligation is that the fork satisfies the SAME specification:

```
theorem protoFast_mixDriver_iff
    {sf wf : Nat} {A : AProgram} {statics dyns : List Val} {Pr : Program} {v : Val}
    (hwfA : wfAProgram A)
    (h : ProtoFast.mixDriver sf wf A statics = .ok Pr) :
    Eval Pr [] (.call Pr.entry (dyns.map .lit)) v
      ↔ Eval (eraseProgram A) [] (.call A.entry ((statics ++ dyns).map .lit)) v
```

### CORRECTION: a structural erasure does NOT commute, and six lemmas are not the burden

The first version of this file proposed an erasure `E : ProtoFast.PRes → PRes`
with `E (.val v) = v.toPRes`, six commutation lemmas, and transport.  **The
commutation is false**, and `primStruct` is the counterexample.  Both
definitions (`PartialEvaluator.lean:365`, `PartialEvaluatorFast.lean:370`) read:

```
| .consP, [.stat a, .stat b] => some (.stat (.cons a b))
| .consP, [a, b]             => some (.cons a b)
```

Take `rs = [.val (.stat a), .val (.stat b)]`.  In the FORK the first pattern
does not match -- these are `.val`, not `.stat` -- so the second fires and the
result is `.cons (.val (.stat a)) (.val (.stat b))`, whose erasure is
`.cons (.stat a) (.stat b)`.  In the HOST, `rs` erased is `[.stat a, .stat b]`,
the FIRST pattern matches, and the result is `.stat (.cons a b)`.

    E (fork rs)  =  .cons (.stat a) (.stat b)
    host (map E rs)  =  .stat (.cons a b)

Equal in denotation, different as `PRes`.  So the relation cannot be literal
equality after erasure.  It must be SEMANTIC -- relating results that denote
the same value -- or carry a proved reachability invariant showing
`.val (.stat _)` never arises, which is a claim about the whole pass and not a
local fact.

What IS easy is the piece the first version called hardest: `total (toPRes v)`
is a structural induction on `PVal`, whose leaves are `stat`/`dyn`, both total.

**The actual burden is the pass/request/driver relation** -- `mixTerm`,
`mixTerms`, `mixUArgs`, the alternatives, the worklist and `generate` -- under
a semantic relation, with the zero-shift identities threaded in.  That is the
same shape as the host development, not a transport of it.

## Obligation B -- `hwPVar` is adequate

`IHwAdequate_proved` is stated about `hwS`/`hwAP`; `ProtoVar.hwSVar` is a
different program.  What is needed:

```
theorem hwVar_adequate
    {D : DesignCert} {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState} {r : Val}
    (hsup : SupportedByProjection D)
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) :
    Eval ProtoVar.hwPVar []
      (.call ProtoVar.hwPVar.entry
        [.lit (encDesign D), .lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s)
```

### CORRECTION: determinism does not manufacture the missing direction

The first version said `SEval_sound` goes one way and `Eval_det` supplies the
converse.  Determinism pins a value you ALREADY HAVE; it cannot produce a
source evaluation that was never constructed.

The right pattern is already in the tree.  `IHwAdequate_proved`
(`HardwareAdequacy.lean:1879-1904`) does not use generic surface completeness
at all.  It CONSTRUCTS a canonical witness and reads both directions off it:

```
hobj : Eval hwP [] (.call hwP.entry [...]) (encResult (interpretDesign D e i s))
     := hw_entry (main_agree hsup hrs)
-- forward:  Eval_det hr hobj          pins r = encResult …
-- converse: ResultRel_canonical hr    turns ResultRel back into that equality
```

So B needs no completeness theorem.  It needs the canonical witness for the
variant, and nothing else changes.

### The smallest lemma, and it is the witness

```
theorem main_agree_var {D e i s}
    (hsup : SupportedByProjection D) (hrs : RuntimeSized D e i s) :
    SEval ProtoVar.hwSVar []
      (.call "main" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                     .lit (encState s)])
      (encResult (interpretDesign D e i s))
```

`main_agree` (`HardwareAdequacy.lean:1787`) is the same statement for `hwS`.
`hw_entry` then crosses to `Eval` through `SEval_entry hwP_resolves`, and the
variant needs its own `hwPVar_resolves`, which is a `rfl`-shaped fact about
`resolveProgram hwSVar`.

It splits into two pieces, and **neither is a generic term lemma applied
blindly** -- `hwS` and `hwSVar` are different programs with different function
tables, and no same-program rewriting lemma relates them automatically:

* **B1, the table. -- PROVED.**  `hwSVar` differs from `hwS` only in `main`'s
  body; every helper is byte-identical.  `SEval` resolves calls by NAME against
  the program and uses it in exactly ONE rule, so a derivation cannot observe a
  function it does not call.  `Proto/VariantTransport.lean`:

  ```
  SEval_congr {P Q} (hag : ∀ f, f ≠ "main" → sFn Q f = sFn P f)
              (hmf : MainFreeFuns P) :
      ∀ σ e v, noMainCallB e = true → SEval P σ e v → SEval Q σ e v
  ```

  `MainFreeFuns hwS` is DISCHARGED, not assumed: `mainFreeB_sound` turns the
  Bool check into the predicate and `mainFreeB_hwS` is a kernel `rfl` (1.6 s),
  not a `#guard`.  The precondition was checked against the source first --
  `main` is called by nothing in `hwS`, not by any of the other 39 functions
  and not by itself.

  Instantiated at the REAL `hwSVar`, not an arbitrary replacement:
  `sFn_hwSVar` holds UNCONDITIONALLY, because the replacement keeps the name
  `main` whatever `goInline` returns -- only the BODY depends on it.  That
  matters: `goInline` is a `partial def`, so `ProtoVar.changed` is NOT
  kernel-reducible and could not have been assumed.

  The corollary B2 consumes is `SEval_hwSVar_of_hwS`, and it deliberately does
  NOT cover a term that calls `main` -- `noMainCallB` excludes it, because the
  top `call "main"` node is exactly what the rewrite changes.  Transporting
  that node would assume what B2 must prove.

  Audited by `Proto/ProtoAudit.lean`: 14 directives, `propext`/`Quot.sound`
  only, `mainFreeB_hwS` axiom-free, no `sorryAx`, no `ofReduceBool`.  This is
  GENERIC TRANSPORT plus its instantiation -- **not** variant adequacy.
* **B2, the rewrite, for the ACTUAL term.**  The inlining lemma must be proved
  for `main`'s body as it is -- under the σ that `main`'s parameters, the two
  `switch` patterns and the preceding `lets` bindings create -- with the side
  conditions `InterpreterVariant.lean` already checks mechanically
  (`selfRef 0`, `useInNext 1`, `useLater 0`, `noBranch`).

What B2 is NOT: it is an iff on SUCCESSFUL evaluation only.  Not failure-order
equivalence (`beforeHoleTotal` is what that needs, and `--inline-negative`
shows why), and not an `evalFuel` equality, since the two differ by one `letIn`
descent.  `substRef` is not capture-avoiding, so the proof must USE `noBranch`
to keep binders off the path rather than assume freshness.

## Which first, and a correction about why

### CORRECTION: `specializeDesign_correct` is NOT interpreter-parametric

The first version said B alone lets `specializeDesign_correct` "re-instantiate
at `hwAPVar`".  It cannot.  Reading `ProjectionCorrect.lean:108`, the
hypothesis literally names `hwAP`:

```
(hproj : mixDriver sf wf hwAP [encDesign D] = .ok R)
```

and the proof consumes `hwAP_entry`, `hwAP_erases` and
`IHwAdequacyGoal_proved`.  It is fuel-parametric, not interpreter-parametric.

So B needs a third piece:

* **B3.**  Generalize the composition.  The proof body uses only generic
  facts -- `mixDriver_entry`, `mixDriver_iff`, `Eval_entry`, `eraseProgram_fn`
  -- plus three premises about the interpreter: an entry fact, an erasure fact,
  and adequacy.  Abstract those into

  ```
  theorem specialize_correct_of
      {A : AProgram} {P : Program} {sf wf : Nat} {D : DesignCert} {R : Program}
      (hentry : ∃ afd, A.fn A.entry = some afd ∧ afd.params = [.stat, .dyn, .dyn, .dyn])
      (herase : eraseProgram A = P)
      (hadq   : ∀ D e i s r, SupportedByProjection D → Compiler.RuntimeWF D i s →
                  RuntimeSized D e i s →
                  (Eval P [] (.call P.entry [...]) r ↔ ResultRel r (interpretDesign D e i s)))
      (hproj : mixDriver sf wf A [encDesign D] = .ok R) … : …
  ```

  and recover `specializeDesign_correct` as the `A := hwAP` instance.  This is
  a refactor of an existing proof, not new mathematics, and it is worth doing
  on its own: it is what makes "the interpreter" a parameter rather than a
  hard-coded constant.

**B (B1 + B2 + B3) before A.**  B yields a theorem-covered path -- `mixDriver`
with `hwAPVar` -- and the fork is then not needed at all.  A without B leaves
every run going through an interpreter nothing is proved about.  B is also
smaller: one term lemma, one table congruence and one refactor, against A's
semantic simulation across the project's largest development.

### CORRECTION: the evidence that B is practically sufficient is weaker than claimed

The first version said the fork's advantage was "a measured 2.66x constant
factor, not an asymptotic one".  That omits the later result: the zero-shift
change (Change 2) measured **31.11x at n = 1024**, with the fitted exponent
moving 2.91 → 1.90 (PHASE6_PERF.md section on the zero-shift fast path).  And
the `env0` rewrite removed the MEASURED source-prefix residual growth on the
designs tried; it is not a proven asymptotic bound on specialization in
general.

So whether the HOST `mixDriver` with `hwAPVar` can project a real design at a
workable budget is **an open empirical question**, not a near-certainty.  One
`--file-ab`-shaped run with `mixDriver` substituted for `ProtoFast.mixDriver`
would answer it, and a negative result would change the order -- it would mean
B alone does not buy a usable covered path and A becomes load-bearing.

That run is legitimate evidence, not a guarantee of viability, and it must not
be launched concurrently with another long run.

## What is deliberately NOT being chased

The residual still carries a per-design residue (`tl` 65 on `rt_alu_gate`,
1,205 on `rt_intpipe_alu`) and a linear `consP` environment spine
(`nSources + nNodes + nFlops + 1`), both recorded in `PHASE6_PERF.md` 8.2 and
8.7.  Neither is a reason to open another optimization increment: the gap
between the experiments and a theorem is A and B, not another factor on
residual size.
