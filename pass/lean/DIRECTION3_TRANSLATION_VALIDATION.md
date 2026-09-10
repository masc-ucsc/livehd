# Direction 3 — proof-producing compiled simulation

**Scope.** Emit a real Lean `def` per design (a straight-line `let`-chain, no
constructor dispatch at runtime) plus a generated, kernel-checked proof that it
equals `denoteResidual R`. Keep `compileDesign_correct` (∀ D) for
LGraph → `ResidualProgram`; add per-design translation validation **only** for
`ResidualProgram` → Lean `def`.

Everything below is measured on this branch (`b1-b2-verified-compiler`,
`485510493`) unless marked *unmeasured*. Probes are in `formal/lean/probes/d3_*.lean`.

---

## Verdict, up front

Two questions decide this plan. The answers point in opposite directions.

**1. Is the kernel-defeq trap avoidable?  YES — measured.**

The original blowup (27 min / 27 GB, `vc_gates.py:17-24`) was *not* caused by
"a theorem statement names a `ResidualProgram`". It was caused by naming one
**whose definition is a `match` on `compileDesign D`**. An explicit literal
avoids it entirely:

| n | literals only | + `compileDesign D = .ok R_lit` | proof marginal |
|---:|---:|---:|---:|
| 256 | 8.94 s | 9.16 s | +0.2 s |
| 1,024 | 18.97 s | 16.91 s | −2.1 s |
| **4,096** | **49.73 s** | **46.11 s** | **−3.6 s** |

The marginal is inside the ±3 s noise band at every size and negative as often
as positive — **the proof costs nothing measurable**, at a node count past
DINO's 4,772, the very design that produced the original 27 GB / 27 min OOM.
Peak memory for the whole series is **1.5 GB**. `native_decide` builds *one*
program natively and compares it against a literal; the kernel never reduces
`compileDesign`.

**2. Is the speed payoff real?  NO — measured at 1.15–1.21×, not 10–50×, not 1000×.**

Same chain, same inputs, same answer, 20,000 cycles each. `shallowB` is the
steelman: `randV`'s body inlined so there is no per-node list allocation — what
a competent generator would actually emit.

| n (nodes) | `denoteResidual` (deep) | `let`-chain, inlined | speedup |
|---:|---:|---:|---:|
| 64 | 28,741 ms — 22 µs/node | 23,676 ms — 18 µs/node | **1.21×** |
| 256 | 111,473 ms — 21 µs/node | 96,796 ms — 18 µs/node | **1.15×** |

**Removing the interpretive layer is not where the time goes.** The cost is the
value representation: `BV = {width : Nat, value : Int}` allocates a GMP `Int`
per intermediate, and `bv_bitwise` walks the operand bit by bit through
`bits_to_int`. Both shapes call those *same* primitives — a shallow embedding
changes which code dispatches, not what it computes with. Constructor dispatch
is ~15–20% of runtime; the other 80–85% is untouched.

**Consequence: direction 3 as originally motivated does not pay.** It
reintroduces a per-design proof obligation to buy 1.2×. A2 — which replaces
`BV` with `BitVec w` and keeps ∀ R — is aimed at the 80%. See §6.

---

## Architecture (if it is built)

Five declarations per design, replacing four. Only the two marked **NEW** are
per-design proof obligations.

```lean
def Foo_designCert  : DesignCert     := { … }              -- unchanged, from C++
def Foo_residualLit : ResidualProgram := { … }             -- NEW: explicit literal
def Foo_fast (i : RuntimeInput) (s : RuntimeState) : RuntimeResult := …   -- NEW: let-chain

theorem Foo_compiles_to : compileDesign Foo_designCert = .ok Foo_residualLit := by
  native_decide                                            -- NEW obligation (1)

theorem Foo_fast_eq : ∀ i s, Foo_fast i s = denoteResidual Foo_residualLit i s := by
  simp [Foo_fast, denoteResidual, Foo_residualLit, runBindings, sourceEnvArr,
        denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
                                                           -- NEW obligation (2)

theorem Foo_fast_correct : ∀ i s, Foo_fast i s = interpretDesign Foo_designCert i s :=
  fun i s => (Foo_fast_eq i s).trans
             (compileDesign_correct _ _ Foo_compiles_to i s)
```

The chain is `Foo_fast = denoteResidual R_lit = interpretDesign D`, the second
step by `compileDesign_correct` (`CompileDesign.lean:270`).

**This is the distinction from the legacy flow.** The legacy path emitted 9,210
per-design theorems that re-derived operator semantics for every design — a
`_refines_fast` lemma per node, with all the operand-order, arity and width
reasoning repeated. Here `compileDesign_correct` does **all** the semantic work,
once, ∀ D. The per-design residue is only:

1. *this literal is what the compiler produced* — a decidable equality, and
2. *this `let`-chain is what the literal means* — pure unfolding, no operator
   reasoning at all.

Neither obligation mentions `eval_op`, `interpOp`, or any operator lemma.

---

## 1. Shape of the generated def

Worked from a real design: `generated/cva6_vc/lean/csr_buffer_gate_Lgraph.lean`
— 45 sources (slots 0–44), 44 nodes (slots 45–88), 2 flops, 3 outputs, 0 memories.

Its first nodes, verbatim from the certificate:

```lean
{ op := LGraphOp.Op_GetMask, width := 2, deps := #[44, 2], origin := 236 }
{ op := LGraphOp.Op_Not,     width := 1, deps := #[45],    origin := 140 }
{ op := LGraphOp.Op_GetMask, width := 2, deps := #[46, 3], origin := 244 }
{ op := LGraphOp.Op_GetMask, width := 2, deps := #[43, 4], origin := 232 }
{ op := LGraphOp.Op_Or,      width := 1, deps := #[1, 48], origin := 136 }
{ op := LGraphOp.Op_GetMask, width := 2, deps := #[49, 5], origin := 240 }
{ op := LGraphOp.Op_And,     width := 1, deps := #[50, 47], origin := 144 }
{ op := LGraphOp.Op_GetMask, width := 2, deps := #[51, 6], origin := 264 }
```

The generated `def`, with the source prologue and the flop epilogue that the
combinational sketch usually omits:

```lean
def csr_buffer_gate_fast (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  -- ── sources, slots 0..44.  `sourceValue` specialised per constructor. ──
  --   SourceDesc.flopQAsync 0 12 5 (0) true
  let v0  : BV := let r := i[5]?.getD (mk_bv 1 0)
                  if !(bv_nonzero r) then mk_bv 12 0
                  else bv_resize 12 (s.flops[0]?.getD (mk_bv 12 0))
  let v1  : BV := let r := i[5]?.getD (mk_bv 1 0)
                  if !(bv_nonzero r) then mk_bv 1 0
                  else bv_resize 1 (s.flops[1]?.getD (mk_bv 1 0))
  let v2  : BV := mk_bv 2 (-1)                      -- SourceDesc.const 2 (-1)
  …
  let v44 : BV := …
  -- ── nodes, slots 45..88.  One let per binding, operand order fixed here. ──
  let v45 : BV := rgetMaskV 2 v44 v2
  let v46 : BV := rnotV 1 v45
  let v47 : BV := rgetMaskV 2 v46 v3
  let v48 : BV := rgetMaskV 2 v43 v4
  let v49 : BV := rorBitsV 1 [v1, v48]
  let v50 : BV := rgetMaskV 2 v49 v5
  let v51 : BV := randV 1 [v50, v47]
  let v52 : BV := rgetMaskV 2 v51 v6
  …
  -- ── outputs and next state ──
  { outputs   := #[bv_resize 12 v0, bv_resize 1 v55, bv_resize 64 v58]
    nextState :=
      { flops := #[ -- flop 0: din 74, no enable, resetPin 40, resetValue 0, active-high
                    (if bv_nonzero v40 then mk_bv 12 0 else bv_resize 12 v74)
                  , (if bv_nonzero v40 then mk_bv 1 0  else bv_resize 1  v88) ]
        mems  := #[] } }
```

Three things the sketch must not gloss over:

- **Sources are not free.** `sourceEnvArr` is `srcs.map (sourceValue i s)`
  (`Runtime.lean:62`), and `sourceValue` has six constructor arms
  (`Runtime.lean:45-59`). `flopQAsync` alone expands to a reset-polarity test.
  The generator specialises each arm; for 45 sources that is 45 more `let`s.
- **Flops carry reset priority.** `flopNext` (`ResidualSemantics.lean:208-221`)
  tests reset *before* enable and falls back to `s.flops[idx]?`. The emitted
  form must reproduce all three branches, not just `din`.
- **Memory bindings are closures, not bit vectors.** `rmemWriteV` returns
  `Int → BV` (`ResidualSemantics.lean:138`), so the chain is **heterogeneous**:
  some `let`s bind `BV`, some bind `Int → BV`. `ResidualBinding.ty`
  (`ResidualIR.lean:88-91`) already records which — the generator reads it.
  *No probe covers a memory-valued binding; this is unmeasured.*

---

## 2. The proof

**No induction is needed, and `runBindings_at` / `runBindings_stable` are not
used.** Those lemmas (`CompileGraph.lean:49-82`) exist for the ∀ D route, where
the binding list is a variable and slots must be reasoned about abstractly. Here
`Foo_residualLit` is a **literal**, so:

- `runBindings` (`ResidualSemantics.lean:200-203`) unfolds by its own two
  equations, once per binding;
- each `Array.push` produces a concrete array;
- every slot index is a numeral, so `denoteRef` is a concrete `getElem?`.

`simp` discharges the whole thing by rewriting. The working tactic, found by
probe:

```lean
simp [Foo_fast, denoteResidual, Foo_residualLit, runBindings, sourceEnvArr,
      denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
```

Two findings that cost time to isolate and that a generator must encode:

**`rfl` does not work — not even at n=2.**

```
error: Tactic `rfl` failed: The left-hand side
  fast2 i s
is not definitionally equal to the right-hand side
  denoteResidual R2 i s
```

Cause: `Array.map` inside `sourceEnvArr` is not kernel-reducible — the same
property already recorded for `decide` in the generated files' comment
("`decide` is not usable — `Array.map` is not kernel-reducible"). The proof must
go through `simp`, which rewrites with `Array.map`'s simp lemmas rather than
reducing it.

**`CertVal.asBV` must be in the simp set.** Without it the goal is left as a
pile of `(CertVal.bv x).asBV = x` residue:

```
⊢ bv_resize 8 (randV 8 [randV 8 [bv_resize 8 (i[0]?.getD (mk_bv 8 0)), …], …]) =
  bv_resize 8 (CertVal.bv (randV 8 [(CertVal.bv (randV 8 [(CertVal.bv …).asBV, …])).asBV, …])).asBV
```

`CertVal.asBV` is `Translation/LGraphModel.lean:284`.

### The proof does reject a wrong chain — checked

A validation that accepts everything validates nothing, so two mutants of the
n=16 probe were run. Both are **rejected** with `unsolved goals`:

| mutant | change | result |
|---|---|---|
| `d3_mut_slot.lean` | node 5 reads `n3` instead of `n4` — a wrong dependency | **rejected** |
| `d3_mut_width.lean` | node 7 emitted at width 4 instead of 8 | **rejected** |

Wrong-slot is the bug class the dense slot space was designed against, and
wrong-width is the class that produced this branch's shipped constant-width bug
(`vc_gates.py:86-95`). Note the second one matters especially: widths are
*values* in `ResidualExpr`, not types, so nothing but this proof would catch it.
A commutative-operator operand swap is **not** detectable this way and was not
tested — `rand` is symmetric, so the mutant would be a true equation. Operand
order for non-commutative operators is already covered ∀ D by `compileOp_correct`.

---

## 3. The kernel-defeq trap — measured

The gate exists because of a real incident (`vc_gates.py:17-24`): OOM at
27 min / 27 GB under a 40 GB cap, 120 GB uncapped, against 38 s / 7.4 GB for the
correct shape, on DINO's 4,772-node `SingleCycleCPU`.

**The gate's stated cause is slightly too broad.** `CompileDesign.lean:337-345`
gives the precise mechanism: the trap shape was

```lean
def   Foo_residual := match compileDesign Foo_designCert with | .ok R => R | _ => default
theorem Foo_compiles : compileDesign Foo_designCert = .ok Foo_residual := …
```

Deciding that equation asks the evaluator to build **two** `ResidualProgram`s and
compare them field-by-field with `DecidableEq`, and asks the kernel to check
`.ok Foo_residual` defeq `.ok (match compileDesign D …)` — which it does by
reducing `compileDesign`, where `Array.push = ⟨as.toList ++ [a]⟩` costs O(N²)
list cells as kernel terms.

With an **explicit literal** neither happens: one program is built natively, the
comparison is O(N), and no kernel reduction of `compileDesign` is required.
Measured above: 6.66 s marginal at n=256 with flat RSS.

### The gate must be relaxed, carefully

`vc_gates.py:76-84` rejects any theorem statement containing `_residual`. That
would reject `Foo_compiles_to`. The relaxation must keep the real invariant:

| | allowed? |
|---|---|
| theorem names a def whose body is `match compileDesign …` | **forbidden** — the actual trap |
| theorem names an explicit `ResidualProgram` literal | allowed |

Simplest robust implementation: keep emitting the existing `Foo_residual`
(match-based, for `#eval`) under that exact name, name the new literal
`Foo_residualLit`, and tighten the grep to `_residual\b` so it still catches the
trap shape and lets the literal through. Do **not** simply delete the gate.

---

## 4. Cost model

All numbers below are a **single serial run with nothing else on the box**
(`d3-clean`, 5 min 39 s CPU, **1.5 GB cgroup peak**). An earlier interleaved
series gave figures up to 3× worse; those are discarded. Noise band is ±3 s,
dominated by Mathlib olean page-cache state — the empty-file import floor alone
measured 8.37 s.

### A note on memory, because the first pass got it wrong

`/usr/bin/time -f %M` reported 6.6–7.3 GB for every probe and appeared to grow
with `n`. That is **shared, file-backed mmap of Mathlib's oleans**, not
allocation. The cgroup peak for the entire series — 17 Lean invocations up to
n = 4,096 — is **1.5 GB**. Memory is not a constraint for direction 3, and any
extrapolation built on the RSS column would be wrong.

### (a) `compileDesign D = .ok R_lit` by `native_decide` — **free**

| n | literals only | + the theorem | proof marginal |
|---:|---:|---:|---:|
| 8 | 9.00 s | 8.13 s | −0.9 s |
| 64 | 6.37 s | 7.55 s | +1.2 s |
| 256 | 8.94 s | 9.16 s | +0.2 s |
| 1,024 | 18.97 s | 16.91 s | −2.1 s |
| 4,096 | **49.73 s** | **46.11 s** | −3.6 s |

The marginal is inside the noise band at every size, and negative as often as
positive. **Obligation (1) costs nothing measurable, up to and past DINO scale
(4,772 nodes).** This is the clean refutation of the kernel-defeq worry: the
design that produced the original 27 GB / 27 min OOM is comfortably handled.

What *does* cost is emitting the literal at all — elaborating two 4,096-entry
array literals is 49.73 s against an 8.37 s floor, ≈ **10 ms per binding, and
linear** (1,024 → 4,096 is a 3.9× rise for a 4× size increase).

### (b) the `let`-chain `simp` proof — **quadratic, and this is the blocker**

| n | wall | marginal over the ~11.5 s floor | doubling ratio |
|---:|---:|---:|---:|
| 4 | 12.15 s | 0.7 s | — |
| 8 | 11.72 s | 0.2 s | — |
| 16 | 11.61 s | 0.1 s | — |
| 32 | 11.36 s | ~0 s | — |
| 64 | 17.69 s | 6.2 s | — |
| 128 | 38.50 s | **27.0 s** | **4.35×** |

Below n ≈ 32 the proof is free; above it, doubling `n` **quadruples** the cost.
That is O(n²), and the mechanism is the one already documented for the kernel
(`CompileDesign.lean:337-345`) now reappearing at the `simp` level:
`runBindings` performs N `Array.push`es, `Array.push` is `⟨as.toList ++ [a]⟩`,
and each slot lookup rewrites through the accumulated list.

Extrapolating quadratically from n=128 — *a two-point ratio over one operator,
so an order of magnitude, not a prediction*:

| design | nodes | extrapolated proof |
|---|---:|---:|
| mean CORE-ET proven design | 1,992 | ~1.8 h |
| DINO `SingleCycleCPU` | 4,772 | ~10 h |
| `vpu_mask` (largest proven) | 14,860 | ~4 days |

**Against today's actual cost:** 85 proven CORE-ET designs, 169,329 nodes total,
**2,204 s wall — a mean of 25.9 s per design** (`pass/lean/SWEEP_b1-b2.tsv`).
The naive whole-program `simp` is **three to four orders of magnitude** more
expensive on the large designs.

So the naive route does not scale, and memory is not what stops it — time is.
If direction 3 is built, obligation (2) must be replaced by a **per-binding
proof that composes linearly**: one lemma per slot, each depending only on its
own dependencies' lemmas, never materialising the whole environment. That is
exactly the shape `slot_agree` uses for the ∀ D proof
(`CompileDesign.lean:279`), and `runBindings_at` + `runBindings_stable`
(`CompileGraph.lean:49-82`) are the composition lemmas it is built from — they
exist and are proved. Phase 2 below. *Unmeasured whether this recovers
linearity in practice; that is the single question on which direction 3 lives
or dies.*

---

## 5. Speed payoff — measured, and it is small even when steelmanned

Two shallow variants were measured against the interpreter, 20,000 cycles each,
identical results (`probes/d3_bench2.lean`):

- **shallowA** — the naive `let`-chain: `let nk := randV 8 [n_{k-1}, s1]`.
  Still allocates a `List BV` per node.
- **shallowB** — the steelman: `randV`'s two-argument body inlined to
  `bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n_{k-1}) s1`. No list, no
  `foldl`, exactly what a competent generator would emit.

| n | deep | shallowA | shallowB | best speedup |
|---:|---:|---:|---:|---:|
| 64 | 28,741 ms (22 µs/node) | 25,482 ms (19) | **23,676 ms (18)** | **1.21×** |
| 256 | 111,473 ms (21 µs/node) | 103,261 ms (20) | **96,796 ms (18)** | **1.15×** |

**The steelman buys 1.15–1.21×.** Per node, 22 µs → 18 µs.

The remaining 18 µs/node is not interpretive overhead — it is `bv_bitwise`
walking 8 bits through `bits_to_int`, and `BV`'s `Int` payload allocating a GMP
value per intermediate. A shallow embedding cannot touch either: the emitted
`let`-chain calls the *same* primitives. Only changing the value representation
does, which is precisely A2.

### A correction to the plan's baseline figure

The main plan's "baseline to beat" table records `denoteResidual` at
25.6 s / 20k ≈ **1.3 ms/cycle** for a 1-node 8-bit add
(`probes/speed_probe.lean`). This probe measures **34.75 µs/cycle** for the same
shape — **37× faster**. The difference is the driver: the old probe folds over
`(List.range 20000)`, materialising a 20,000-element list inside the timed
region; this one uses `for k in [0:it]` with `IO.monoMsNow`.

**The 1.3 ms/cycle figure overstates the interpreter's cost by ~37× and should
be re-measured before it is used to justify anything** — including A2's
"10–50×" target and the ~50 µs/cycle success bar, which the interpreter may
already be close to. The deep-vs-shallow *ratios* above are unaffected: every
column runs under the identical driver.

**Note a discrepancy with the earlier figure.** The plan's baseline records
`denoteResidual` at 25.6 s / 20k invocations ≈ **1.3 ms/cycle** for a 1-node
8-bit add (`probes/speed_probe.lean`). This probe measures **34.75 µs/cycle** for
the same shape — 37× faster. The difference is the driver: the old probe folds
over `(List.range 20000)`, materialising a 20,000-element list, while this one
uses `for k in [0:it]` with `IO.monoMsNow`. **The 1.3 ms/cycle figure in the
plan's "baseline to beat" table overstates the interpreter's cost by ~37× and
should be re-measured before it is used to justify anything.** The deep-vs-
shallow *comparison* here is unaffected: both sides run under the identical
driver.

---

## 6. Relation to A2 — builds on it, and A2 is now clearly first

A2 (Phase 6 of the main plan) defines a typed IR — `BitVec w` with the width in
the type — plus `reify : ResidualProgram → Option FastProgram` and
`denoteFast`, with `reify_correct` proved **∀ R**, no per-design obligation.

| | A2 | direction 3 |
|---|---|---|
| attacks | value representation (`BV`'s `Int`, `bits_to_int`'s bit loop) | constructor dispatch + list allocation |
| measured share of runtime | ~80–85% *(by subtraction, not directly)* | **15–20%, measured** |
| per-design proof | none | two obligations |
| keeps ∀ | yes | no |

**They do not compete, and the measurement reorders them decisively.** Dispatch
plus list allocation is 15–20% of runtime — that is the *whole* ceiling for
direction 3, measured with the steelman generator. A2 targets the remaining
80–85%. Direction 3 pays a per-design proof for the smaller share.

Recommended sequencing, unchanged in order but now with a much stronger reason:

1. **Re-measure the baseline first** (§5) — it is off by ~37×, and both A2's
   "10–50×" estimate and its ~50 µs/cycle success bar were set against the wrong
   number. The interpreter is at ~35 µs/cycle for 1 node and ~22 µs/node
   marginal, so the bar may already be met for small designs. This is an
   afternoon and it may retire a whole phase.
2. **A2 second, and measure per-node.** The honest question is what `BitVec w`
   does to the 18–22 µs/node, since that is where the time provably is.
3. **Direction 3 only if A2 still falls short** of what differential testing
   against `cgen_sim` needs — and knowing the ceiling is 1.2× on top of
   whatever A2 achieves.
4. If it is ever built, generate the `let`-chain from **`FastProgram`, not
   `ResidualProgram`** — widths live in the types, so the generator is simpler,
   the proof obligation is over a typed object, and the two speedups compose.

---

## 7. Kôika and Cuttlesim — the record, corrected

**Cuttlesim does not do translation validation.** It is worth stating precisely,
because the comparison is load-bearing for how this branch is positioned.

- **Kôika** (Bourgeat, Pit-Claudel, Chlipala, Arvind, *The Essence of Bluespec*,
  PLDI 2020) has a Coq-verified compiler from the Kôika rule-based language to
  circuits, with a machine-checked theorem that the circuits agree with the
  source semantics.
- **Cuttlesim** (Pit-Claudel, Bourgeat, Lau, Arvind, Chlipala, *Effective
  simulation and debugging for a high-level hardware language using software
  compilers*, ASPLOS 2021) is a **separate, unverified** compiler from Kôika
  directly to C++ — not a back end for the verified circuits. Its correctness
  rests on **differential testing** against the Coq reference interpreter and
  Verilator, plus a small number of formally verified static analyses. There is
  no per-instance proof and no kernel check.
- **Translation validation** proper means proving each *run* of a compiler
  correct rather than the compiler itself: Pnueli, Siegel & Singerman,
  *Translation Validation*, TACAS 1998 (which introduces the term); Necula,
  *Translation validation for an optimizing compiler*, PLDI 2000 (inferring
  simulation relations for a production optimizer).

So the accurate framing is:

| | verified once | per-instance proof | tested only |
|---|---|---|---|
| Kôika → circuits | ✅ | | |
| Kôika → C++ (Cuttlesim) | | | ✅ |
| **this branch**, LGraph → `ResidualProgram` | ✅ | | |
| **legacy** flow, LGraph → Lean model | | ✅ (9,210 theorems) | |
| direction 3, `ResidualProgram` → Lean `def` | | ✅ (2 obligations) | |

The legacy flow was already translation validation, and a far more expensive
variety than direction 3 would be — it re-derived operator semantics per design.
Direction 3 validates only the syntactic last mile. That distinction is the one
worth making in a paper; "we generate Lean, they generate C++" is not.

---

## Phases, if this is built

Sequenced after A2 measures out, per §6.

**Phase 0 — relax the gate (half a day).** `vc_gates.py:76-84` per §3. Add a
regression that the *trap* shape is still rejected; that gate has already paid
for itself once.

**Phase 1 — emit `_residualLit` + `_compiles_to` (2 days).** Independently
useful even if the rest is never built: it makes the compiler's output
inspectable in the file rather than only via `#eval`, and `cert_lgraph_diff.py`
could then diff the literal against the LGraph directly. Measure across the full
sweep — this is the (a) column of §4 at real node counts, and it is the number
the current extrapolation is weakest on.

**Phase 2 — linear per-binding proof composition (the real work, 1–2 weeks).**
Replace whole-program `simp` with one lemma per slot via `runBindings_at` +
`runBindings_stable`. **Success criterion, stated in advance so it cannot drift:
the doubling ratio on `probes/d3_simp_*.lean` must fall from the measured 4.35×
to ≤2.2×, and n=4,096 must complete in under 10 minutes.** If it does not,
direction 3 is dead and should be recorded here as such — the honest outcome,
given §5 already shows the prize is 1.2×.

**Phase 3 — emit the `let`-chain and re-measure speed.** Including memory-valued
bindings, which no probe covers.

**Verification bar** — the same as every phase of the main plan: full CORE-ET +
CVA6 sweep, `blocked-here` must stay 0, no `sorry`, and `vc_gates.py` +
`cert_lgraph_diff.py` clean on every emitted file.

One thing changes in the axiom audit. Today a proven design reports exactly:

```
'store_unit_gate_step_correct' depends on axioms: [propext,
 Classical.choice,
 Quot.sound,
 store_unit_gate_compiles._native.native_decide.ax_1_1]
```

`native_decide` mints a **per-declaration** axiom (not a shared
`Lean.ofReduceBool`), so direction 3's second `native_decide` adds a *second*
such axiom, `Foo_compiles_to._native.native_decide.ax_1_1`. The queue's axiom
gate counts `#print axioms` lines rather than matching names
(`run_lean_queue.sh:113-122`), so it will not break — but any reviewer
comparing axiom lists across branches must expect two native axioms, not one,
and each is an additional appeal to the compiler's native evaluator.

---

## What is unmeasured

Listed so nothing here is mistaken for a result.

- **Everything above n=128 for the `simp` route.** The quadratic extrapolation
  rests on a single doubling (64 → 128). `native_decide` is measured to 4,096
  and needs no extrapolation.
- **One operator only.** Every probe uses `Op_And` → `rand`. Real designs use 20
  distinct operators; `rsext`, `rmemWriteBE` and the variadic `rsum` have
  materially different proof shapes (`ResidualSemantics.lean:20-35` grades them
  "real content" vs "mapping only").
- **No memory-valued binding is exercised**, so the heterogeneous `let`-chain of
  §1 is a design sketch, not a tested shape.
- **Whether per-binding composition actually recovers linearity** — Phase 2's
  entire premise.
- The `#eval`-driver measurements in §5 run under Lean's interpreter driving
  compiled leaf functions; a fully compiled harness may shift both columns,
  though not obviously their ratio.
