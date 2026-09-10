# Direction 3 — proof-producing compiled simulation

**Scope.** Emit a real Lean `def` per design (a straight-line `let`-chain, no
constructor dispatch at runtime) plus a generated, kernel-checked proof that it
equals `denoteResidual R`. Keep `compileDesign_correct` (∀ D) for
LGraph → `ResidualProgram`; add per-design translation validation **only** for
`ResidualProgram` → Lean `def`.

Everything below is measured on this branch (`b1-b2-verified-compiler`,
`485510493`) unless marked *unmeasured*. Probes are in `formal/lean/probes/d3_*.lean`.

> **Part I (below) was written from probes; Part II reports building it.**
> The implementation pass **refutes** Part I's central engineering hope — the
> composed path via `runBindings_at` does not restore linearity, and reaches an
> exponent of ~1.9 against a pre-committed bar of 1.2.  It also **strengthens**
> the two negative findings: the naive proof does not merely get slow, it stops
> working at n≈200; and the speed payoff on a real CVA6 module is **1.08x**, not
> the 1.15x-1.21x the synthetic steelman suggested.  Skip to Part II for what
> was actually built and measured.

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

---

# Part II — Built, and measured (implementation pass)

The plan above was written from probes.  This part reports what happens when the
thing is actually built: a reifier metaprogram, a generated per-design proof, and
the linear composition path the plan identified as the only way to escape the
quadratic `simp` blast.

**Pre-committed success criterion**, fixed before any measurement: proof cost at
or below roughly `O(N^1.2)` out to at least 4,096 bindings, with DINO scale
(4,772) under ten minutes.

## What was built

### 1. `LeanSemanticPrimitives/Compiler/Reify.lean` — composition lemmas

Six lemmas, all `simp only` one-liners, all discharged.  The two that carry the
weight:

```lean
theorem runBindings_step (b : ResidualBinding) (bs : List ResidualBinding)
    (env : SlotEnv) (v : CertVal) (hv : denoteExpr env b.rhs = v) :
    runBindings (b :: bs) env = runBindings bs (env.push v)

theorem srcAgree_push {env base : SlotEnv} (v : CertVal)
    (hsz : base.size ≤ env.size)
    (h : ∀ j, j < base.size → refBV env j = refBV base j) :
    ∀ j, j < base.size → refBV (env.push v) j = refBV base j
```

`runBindings_step` advances the fold by one binding with the environment left
opaque, so the generated proof can `set` after every step and never let an
intermediate environment appear as a term of size `O(k)`.

`srcAgree_push` is the one that changes the exponent rather than the constant.
Without it, a binding that reads a SOURCE operand `k` levels down costs `k`
rewrites through the push chain.  Sources fan out to many consumers — in the
`chain` benchmark every one of the `N` bindings reads source slot 1 — so that
term alone is `O(N^2)` and it is what the original quadratic measurement was
mostly measuring.  With the invariant carried forward, a source read costs one
rewrite whatever `k` is.

### 2. `LeanSemanticPrimitives/Compiler/ReifyGen.lean` — the reifier

```
reify_design <designCert> as <name>
```

A `CommandElab` that runs `compileDesign` on the certificate **at elaboration
time** via `evalExpr`, folds the resulting `ResidualProgram` into `Syntax`, and
hands it to `elabCommand`.  All 21 `ResidualExpr` constructors are covered, plus
outputs, flop updates with enable and reset priority, and memory updates.

On a real CVA6 certificate:

```
reify_design csr_buffer_gate_designCert as csr_step
-- reify_design: csr_step emitted, 45 sources, 44 bindings

def tiny_step : RuntimeInput → RuntimeState → RuntimeResult :=
fun i st =>
  have s0 := (sourceValue i st tinyCert.sources[0]!).asBV;
  have s1 := (sourceValue i st tinyCert.sources[1]!).asBV;
  have v0 := randV 8 [s0, s1];
  have v1 := rorBitsV 8 [v0, s1];
  { nextState := { flops := #[], mems := #[] }, outputs := #[bv_resize 8 v1] }
```

This file is worth reading as the concrete answer to *why the theorem is per
design*.  `compileDesign` is applied to a **value** here, obtained with
`evalExpr`; the result is folded into `Syntax` and handed to `elabCommand`,
which **mutates the environment**.  None of those steps is a function in the
logic, so there is no term the logic could quantify over.  The "per design" is
not a limitation of effort — it is where the metalanguage boundary falls.

### 3. `pass/lean/scripts/d3_gen.py` — the benchmark generator

Emits both proof variants for a synthetic design of `n` bindings, under two
dependency shapes, because the shape is what decides which term dominates:

* `chain` — binding `k` reads binding `k-1` and source 1.  Binding distance 1,
  source distance `k`.  This is the shape the original measurement used.
* `far` — binding `k` reads binding `k-1` and binding `k/2`.  Binding distances
  grow as `k/2` and no invariant can collapse them: the adversarial case.

## The naive proof has a hard ceiling, not just a slope

The plan reported the naive `simp` blast as quadratic.  Built and run, it is
worse than that: it **stops working entirely** at around 200 bindings.

```
probes/d3b_naive_chain_256.lean:557:2:
  error: `simp` failed: maximum number of steps exceeded
```

That is simp's `maxSteps` (default 100,000), not `maxHeartbeats`, which the
file already sets to 0.  The ceiling can be raised, but the growth that reaches
it in 256 bindings is the same growth the 4.35x doubling ratio measured, so
raising it buys a constant factor and not a usable design point.  Both `n = 256`
and `n = 512` fail after about 37 CPU-seconds — the same time, because both die
at the same step budget rather than at a size-dependent cost.

So the honest statement about the naive path is not "quadratic".  It is: **the
naive path does not reach the smallest real design in the benchmark suite.**
`cvxif_fu_gate`, the smallest CVA6 module that proves today, has 23 nodes;
`csr_buffer_gate` has 44; `controller_gate` has 104.  DINO has 4,772.

## Where the quadratic actually lives — three sources, not one

The plan attributed the blowup to `Array.push = ⟨toList ++ [a]⟩`.  Building it
shows that is one of three independent contributions, and removing it is not
enough.

**(a) Environment term size.**  A proof that names the intermediate environments
gives step `k` a term of size `O(k)`.  `runBindings_step` plus `set` fixes this:
each environment stays a single free variable.  **Removed.**

**(b) Dependency distance.**  Reading an operand `d` levels down costs `d`
rewrites through the push chain.  For SOURCE operands `srcAgree_push` collapses
this to one rewrite whatever the distance, which matters because sources fan out
to many consumers.  For BINDING operands nothing collapses it: the cost is the
sum of dependency distances, a property of the design, not of the proof.
**Removed for sources, intrinsic for bindings.**

**(c) Local-context size.**  This is the one the plan missed.  The generated
proof introduces roughly `6N` hypotheses — `v_k`, `E_k`, `hE_k`, `hs_k`,
`hag_k`, and two operand reads per binding.  Every tactic invocation re-scans
that context, so even with (a) and (b) fixed the total is `O(N^2)`.

The `residual list` is a fourth candidate — applying `runBindings_step` needs the
cons structure exposed, leaving a tail of size `O(N-k)` — and it is not
separable from (c) by this experiment.

## Measured: the criterion is missed

Marginal cost over a 12.08 CPU-second import baseline, `chain` shape, serial:

| n | naive | composed | pruned |
|---:|---:|---:|---:|
| 32 | 3.99 | 13.98 | 10.56 |
| 64 | 7.94 | 54.86 | 38.42 |
| 128 | 27.87 | 225.67 | 149.09 |
| 256 | **FAIL** (maxSteps) | | |
| 512 | **FAIL** (maxSteps) | | |

Doubling ratios and the exponent they imply:

| | 32 → 64 | 64 → 128 | exponent |
|---|---:|---:|---:|
| naive | 1.99 | 3.51 | 0.99 / 1.81 |
| composed | 3.92 | 4.11 | **1.97 / 2.04** |
| pruned | 3.64 | 3.88 | **1.86 / 1.96** |

Both composed variants are flatly quadratic, and both are *slower in absolute
terms* than `naive` at every size where `naive` still works.  Pruning the local
context recovers **34%** at n=128 (149.09 against 225.67) — a real constant, and
no change to the slope.

**The pre-committed criterion was `O(N^1.2)` out to 4,096 bindings with DINO
scale under ten minutes.  The best variant reaches an exponent of about 1.9.
The criterion is missed, and not marginally.**  Extrapolating the best variant's
149 s at n=128 across the 37x to DINO's 4,772 bindings gives **35 h at exponent
1.86 and 50 h at 1.96** — against a ten-minute bar, and against the **0.8 s**
the existing `native_decide` takes on that same design.  (The extrapolation is
from three points over one decade; treat the order of magnitude as the result,
not the hour count.)

## Script size separates the two causes cleanly

The generated proof script's size on disk, by dependency shape:

| n | `chain` | `far` |
|---:|---:|---:|
| 128 | 116 KB | 349 KB |
| 256 | 234 KB | 1.19 MB |
| 512 | 469 KB | 4.32 MB |
| 1024 | 942 KB | **16.4 MB** |

`chain` doubles exactly with `n` — the script is **linear**.  `far` roughly
quadruples — the script is **quadratic**, because binding operands sit `k/2`
levels down and each level costs one emitted rewrite.  That is cause (b),
dependency distance, made directly visible: 16 MB of Lean source for 1,024
bindings.

The important half is `chain`.  Its script is linear in size, and its
elaboration is still quadratic in time (exponent 1.9).  **A linear script that
elaborates quadratically cannot be explained by term size or by dependency
distance** — it isolates cause (c), the growing local context and goal that
every tactic re-scans, as an independent obstruction.  This is the measurement
that rules out "write a smarter tactic" as a way out: the script was already as
short as it can be.

## D3 against the legacy flow

The claim the plan made for D3 was that it differs from the legacy
9,210-theorem flow in *scope*: the legacy path re-derived operator semantics per
design, while D3 reuses `compileDesign_correct` and only composes per-binding
facts.  That claim survives — it is a real structural difference, and the
generated proof never mentions `eval_op`, `interpOp`, or any operator lemma.

What does not survive is the consequence anyone would want from it.

| | legacy | D3 as built |
|---|---|---|
| per-design theorems | 9,210 | 1 |
| operator semantics | re-derived per design | shared, proved once |
| what the per-design proof does | everything | compose bindings |
| **measured cost** | hours at ~6k nodes | **35-50 h extrapolated at 4.8k** |
| ceiling | reached in practice | naive dies at n≈200 |

Reducing what the per-design proof has to *say* did not reduce what it *costs*,
because the cost is dominated by the mechanics of walking an `O(N)` object in a
term-rewriting kernel, not by the depth of the reasoning at each step.  Legacy's
CVA6 `alu` took 4 h 15 m at 6,305 nodes; D3 extrapolates to the same order at
smaller sizes.  **On the metric that motivated the branch, D3 is not an
improvement on the flow it was supposed to improve on.**

The contrast with what exists today is the sharp one.  `compilesOk` discharges
the entire per-design obligation with a single `native_decide`, measured at
**0.8 s on DINO's 4,772 nodes**.  That is not a better proof of the same
statement — it is a different mechanism: one decision procedure whose per-node
cost is a machine-word comparison, against `N` distinct kernel rewrites.  Any
per-design scheme built from rewrites is competing with a compiled decision
procedure and will lose by orders of magnitude.

## D3 against Kôika and Cuttlesim — the record

Kôika does **not** do translation validation, and the comparison is only
interesting if that is stated correctly.

```
              Kôika source
             /            \
   verified compiler     Cuttlesim
            |                |
        circuits          C++ simulator
     (machine-checked)   (unverified, separate compiler)
```

Cuttlesim is a **separate, unverified** Kôika-to-C++ compiler.  It does not
consume the verified compiler's circuits.  Its correctness rests on
**differential testing** between the Coq reference interpreter, Verilator, and
Cuttlesim itself, plus formal verification of a few static analyses.  The
authors say so, and note that a divergence could come from a Cuttlesim bug or
from external-function implementations.

Translation validation in the Pnueli (1998) / Necula (PLDI 2000) sense is
different: the transformer stays untrusted, but each *instance* emits a proof
that a checker verifies.  Nothing in Kôika does that for the simulator.

So the honest positioning of D3 is not "Kôika tests, we prove."  It is:

* **against Cuttlesim** — D3 would replace differential testing with a
  kernel-checked proof per design.  Strictly stronger *if it ran*.  Measured, it
  does not run at the sizes that matter.
* **against our own current branch** — D3 is strictly *weaker*, because
  `compileDesign_correct` already gives `∀ D` with no per-design proof, and
  Cuttlesim's whole reason for existing (a fast simulator) is worth only 1.15x
  to 1.21x here.

The structural point the plan made still stands and is worth keeping in a
write-up: our chain is serial (`RTL → LGraph → residual`) where Kôika's forks,
so a verified segment composes with the rest instead of running beside it.  That
argument does not depend on D3 being built.

## End to end on a real design

`csr_buffer_gate` — a real CVA6 module, certificate taken unmodified from
`generated/cva6_vc/lean/`, **45 sources and 44 bindings**.

```
reify_design csr_buffer_gate_designCert as csr_step
-- reify_design: csr_step emitted, 45 sources, 44 bindings
```

**Agreement, 50 sampled inputs:** the reified definition and `denoteResidual`
produce identical outputs and identical next-state flops.  `true`.

**Speed, 20,000 cycles**, forcing every observable (outputs and next-state
flops) and timing between `IO.monoMsNow` calls with `IO.println` in between:

| | total | per cycle | per node-cycle |
|---|---:|---:|---:|
| shallow — reified `def` | 23,120 ms | 1.156 ms | 26.3 µs |
| deep — `denoteResidual` | 25,020 ms | 1.251 ms | 28.4 µs |
| **speedup** | | **1.08x** | |

**1.08x on a real design** — below even the 1.15x-1.21x the synthetic steelman
gave.  Removing constructor dispatch buys almost nothing because dispatch is
not where the time goes: `BV`'s GMP `Int` payload and `bits_to_int`'s per-bit
loop dominate, and the emitted `let`-chain calls them identically.

### This independently confirms the baseline correction

The plan reported `denoteResidual` at 34.75 µs/cycle for a ONE-node design,
against the main plan's 1.3 ms/cycle for the same design — a 37x error caused by
materialising `List.range 20000` inside the timed region.

This measurement is a third, independent data point: 1.251 ms/cycle across
**44 nodes** is **28.4 µs per node-cycle**, which matches the 34.75 µs figure
and not the 1.3 ms one.  The correction stands, and Phase 6's ~50 µs/cycle
success bar should be re-examined against per-node cost rather than per-cycle.

### Two measurement traps, both hit here

Worth recording, because both produce plausible-looking numbers rather than
errors:

* `#eval timeit "..." (return e)` does **not** force `e` in the interpreter.  It
  reported a 20,000-iteration loop as **0.012 ms**.
* reading only `result.outputs.size` lets the compiler delete the entire
  computation, since the array's length is known from its literal structure.

Both were caught only because the numbers were physically impossible
(sub-nanosecond per cycle).  A subtler version would have passed.

## Verdict after building it

The plan said D3 was buildable but not worth building.  Building it changes two
of the three supporting claims, and both changes make the case *stronger*, not
weaker.

| plan said | built and measured |
|---|---|
| kernel-defeq trap is avoidable | **confirmed** — explicit literals stay inside noise at 4,096 |
| naive `let`-chain proof is quadratic | **understated** — it *fails* at n≈200, `simp` maxSteps |
| composed path via `runBindings_at` is the fix | **refuted** — best variant still exponent ~1.9 |
| shallow buys 1.15x-1.21x | **confirmed and worse** — **1.08x** on a real design |

The one genuinely new finding is the third.  The plan identified `Array.push`
term growth as the obstruction and expected per-binding composition to remove
it.  Composition *does* remove it, and the cost stays quadratic anyway, because
the local context grows to `~6N` hypotheses that every tactic re-scans.  Pruning
dead hypotheses recovers a constant factor, not the slope.

**The deeper statement, which is the useful one to carry forward:** any
per-design scheme that walks an `O(N)` object with `O(N)` kernel rewrites is
quadratic in a term-rewriting kernel, whatever the rewrites say.  Escaping that
requires the per-step facts to be *uniform* — decided by one compiled procedure
rather than by `N` distinct rewrites.  That is exactly what `native_decide` on
`compilesOk` already does, in **0.8 s at 4,772 nodes**.

So D3's failure is not an engineering shortfall to be fixed with better tactics.
It is the reason `compileDesign_correct` plus a decision procedure is the right
architecture, stated as a measurement instead of a preference.

## What is kept

The reifier itself is worth keeping regardless of the verdict:

* `ReifyGen.lean` emits a genuine straight-line Lean `def` from any accepted
  certificate, all 21 constructors, flops and memories included.  It is the
  concrete artifact for "compiled simulation", and it is the file to point at
  when explaining why `reify_correct` is per design — `compileDesign` runs on a
  *value* via `evalExpr` and the result is installed by `elabCommand`, an
  environment mutation, so no term exists for the logic to quantify over.
* `Reify.lean`'s six lemmas are sound and reusable; `srcAgree_push` in
  particular is the right way to decouple proof cost from source fan-out and
  would be needed by any future per-design scheme.
* The agreement harness — reify, then check the emitted def against
  `denoteResidual` on sampled inputs — is a cheap differential gate that does
  not depend on the proof working, and would catch a reifier bug immediately.

## Recommendation, unchanged and now measured

Do not adopt D3 as the production path.  Keep it as the research comparison it
was built to be: it now answers "why not translation validation for the last
mile?" with three numbers rather than an argument — **1.08x** speed, exponent
**~2.0** proof cost, and a naive path that **does not reach 256 bindings**.

A2 stays first.  It keeps `∀ R`, and since the measured bottleneck is the `BV`
value representation rather than dispatch, A2's typed `BitVec` targets the term
that actually dominates.  The re-measurement of `denoteResidual` should happen
before A2 is built at all, because 28.4 µs per node-cycle may already clear the
bar the phase was created to reach.

## What Part II did not measure

Stated so the numbers above are not read as more than they are:

* **`pruned` at n=256 and the `far`-shape time series were not run.**  The
  benchmark was stopped once three points across two variants agreed on an
  exponent near 1.9; more points would confirm the verdict, not change it.  The
  `far` shape's behaviour is already established by its script size, which is
  quadratic (16.4 MB at n=1024).
* **The generated proof was exercised on synthetic designs only.**  The real
  CVA6 module was carried end to end for the *reified definition* — emitted,
  agreement-checked against `denoteResidual` on 50 inputs, and timed — but not
  for the per-design proof, which would need the operand-read walk extended
  from `rand` to all 21 constructors.  Since the criterion already failed
  decisively on synthetic designs, that extension was not written.
* **`#print axioms` on a generated per-design proof was therefore not run.**
  The synthetic proofs use only `simp only`, `rw` and `rfl` over the committed
  lemmas, so no new axiom is expected, but this is an expectation and not a
  measurement.
* The DINO extrapolation is from three points over one decade.  It supports an
  order of magnitude, not an hour count.
