# Direction 2 — the simulator as executable semantics of the compiler IR

**Objective:** emit a `DesignCert` at more than one point in the LiveHD pass
pipeline and check that consecutive graphs agree *observationally*:

```
                    RTL
                     │  slang → yosys script → yosys2lg
                     ▼
        G0  ──cprop──►  G1  ──bitwidth──►  G2  ──single_edge──►  G3
         │              │                  │                     │
      cert(G0)       cert(G1)           cert(G2)              cert(G3)
         └──────────────┴───── Sim(Gᵢ) ≟ Sim(Gᵢ₊₁) ─────────────┘
```

This makes the Lean model something other than a user-facing simulator: it
becomes **the executable semantics of the IR**, and the IR's own passes become
things that can be tested against it. Cuttlesim cannot do this — it bypasses
Kôika's RTL compiler entirely, so it has no intermediate stages to compare.

Status: **nothing built.** What follows is measured investigation plus a plan.
Two probe runs were executed (§2, §4); everything else is read-only.

---

## 0. Executive summary

| finding | § |
|---|---|
| The emitter is **already re-entrant** — `lhd compile lg:DIR --emit-dir lean:` works today, and CORE-ET already uses it | 2 |
| `--recipe O0` suppresses all graph passes, so a **true pre-cprop G0 is reachable** — measured | 2, 4 |
| The saved `lgdb_raw`/`lgdb_norm` artifacts are **useless as a differential pair** — cprop is already at fixpoint in them | 4 |
| A real pair exists: on `intpipe_inst_bits_stage`, cprop folds **170 → 139 constants** while leaving inputs (5) and flops (32) untouched | 4 |
| **The certificate contains zero names** — measured, literally `grep -c` = 0. Input/output ordinals happen to be name-sorted and stable; **flop ordinals are nid-sorted and are not** | 5 |
| `RuntimeState` has **no `DecidableEq`** (memories are functions), so state can only be compared on flops plus sampled memory addresses | 6 |
| `cert_lgraph_diff.py` is **not** rung 1 of this ladder — it is a different axis (one graph, two extractors, static) | 7 |
| The honest claim is narrower than "attacks RTL → LGraph": it attacks the **pass pipeline**, and it is **blind to any error common to both stages** | 8 |

---

## 1. Where `pass.lean` can be invoked, and what runs before it

The graph-pass recipe is short, explicit, and lives in one function
(`lhd/lhd_kernel_common.cpp:775-819`):

| recipe | graph passes | line |
|---|---|---|
| `O0` | **none** — returns `{}` | `lhd_kernel_common.cpp:794-796` |
| `O1` (default) | `pass.cprop` [+ `pass.bitfuzz` if enabled] | `:797-806` |
| `O2` | `pass.cprop` [+ `pass.bitfuzz`] + `pass.bitwidth` | `:807-817` |

Everything else in `graph_pipeline_and_emits` (`lhd_kernel_compile.cpp:1325`)
runs unconditionally after the recipe:

1. recipe loop — `lhd_kernel_compile.cpp:1327-1335`
2. latch-contract check — `:1343`
3. `pass.formal` (default `fast`, `none` under O0) — `:1369`
4. `lg:` save, then `emit_lean_outputs` — `:1405`

**`pass.single_edge` is not a recipe pass.** It is a separate top-level
subcommand (`lhd/lhd_kernel_passes.cpp:745-769`) taking `lg:` in and `lg:` out,
which is why the CORE-ET runner invokes it as its own process step.

So the CORE-ET pipeline as it actually runs today
(`scripts/run_coreet_module_lean.sh`):

| step | command | line | graph passes it runs |
|---|---|---|---|
| 1 | `lhd compile verilog … --emit-dir lg:LG_RAW` | `:91` | **cprop** (default O1) |
| 2 | `lhd pass single_edge lg:LG_RAW --emit-dir lg:LG_NORM` | `:111` | single_edge only |
| 3 | `lhd lec --impl lg:LG_NORM --ref verilog:…` | `:140` | — (the RTL-vs-LGraph gate) |
| 4 | `lhd compile lg:LG_NORM --emit-dir lean:…` | `:161` | **cprop again** (default O1) |

**Note the double cprop.** Today's certificate is really

```
cert( cprop( single_edge( cprop( yosys(RTL) ) ) ) )
```

Measured harmless right now (§4: the second cprop is a fixpoint on both probed
modules), but it is not documented anywhere and it is exactly the kind of thing
that makes a "which graph did we prove about?" question unanswerable. Step 4
should pass `--recipe O0` regardless of whether Direction 2 proceeds.

---

## 2. Is the emitter re-entrant? Yes, already — no plumbing needed

`lhd compile lg:DIR` loads every graph from a saved library
(`lhd_kernel_compile.cpp:1490-1498`) and then calls the same
`graph_pipeline_and_emits` (`:1502`) that the source path uses. The lean emit is
just another `--emit-dir` slot.

**Measured.** Four emits from saved libraries, all exit 0:

```
lhd compile lg:generated/core-et/<M>/lgdb_{raw,norm} --top <M> \
    --recipe {O0,O1} --emit-dir lean:<out> \
    --set formal.lean.mode=verified_compiler
```

`--recipe O0` is accepted on the `lg:` path and does suppress the recipe. That
is the lever Direction 2 needs: **emit a certificate from a saved graph without
perturbing it.**

What changes between stages, in certificate terms:

| certificate field | changes across a pass? |
|---|---|
| `sources` (`const`) | **yes** — cprop folds them (§4: 170 → 139) |
| `sources` (`input`) | no, unless a port is removed |
| `sources` (`flopQ`) | usually not for cprop/bitwidth; **yes** for single_edge (adds a phase divider) |
| `nodes` | yes — count and slot indices both shift |
| `outputs` | count is stable *unless* an output becomes undriven (§5 hazard) |
| `flops` | see above |
| widths | **yes** under O2 (`pass.bitwidth` is exactly a width rewriter) |

Slot indices shift on **every** stage, because the slot space is dense
(`DesignCert.lean`: sources `0..S-1`, node `i` at `S+i`). Slot numbers are
therefore meaningless across stages; only ordinals and array positions carry
over.

---

## 3. What `Sim(Gᵢ) = Sim(Gᵢ₊₁)` actually means

This is the crux. The equality is **not** literal — different slot spaces,
different node counts, possibly different flop sets. Writing it as `=` would be
a type error at best and a false claim at worst.

### 3.1 What is observable

`interpretDesign` is single-cycle (`DesignSemantics.lean:61-67`). Direction 2
needs a trace, which does not exist yet:

```lean
def runTrace (D : DesignCert) : RuntimeState → List RuntimeInput
    → List (Array BV) × RuntimeState
  | st, []      => ([], st)
  | st, i :: is =>
      let r          := interpretDesign D i st
      let (os, st')  := runTrace D r.nextState is
      (r.outputs :: os, st')
```

The observable interface of a `DesignCert` is exactly:

- **inputs** `Array BV`, positionally (`Runtime.lean:18`)
- **outputs** `Array BV`, positionally (`Runtime.lean:26-28`)

State is **not** observable. `RuntimeState.flops`/`.mems` are implementation
detail of a particular graph, and a pass is entitled to change them.

### 3.2 The relation

> **Definition (observational equivalence).** Certificates `D`, `D'` are
> observationally equivalent with respect to an input correspondence
> `ι : Fin n → Fin n'`, an output correspondence `ω : Fin m → Fin m'`, and an
> initial-state relation `≈₀`, iff
>
> ```
> ∀ st st', st ≈₀ st' →
>   ∀ is : List RuntimeInput,
>     (runTrace D  is             st ).1
>       = map (permute ω) (runTrace D' (map (permute ι) is) st').1
> ```

Read: **from corresponding initial states, corresponding input sequences produce
corresponding output sequences.** State is existentially quantified away behind
`≈₀` — it is never compared directly, because it is not observable.

This is a coinductive/infinitary statement. The *checkable* version fixes a
state pair and a finite input sequence, which is what the gate will run.

### 3.3 Three cases, in increasing difficulty

| case | when | `≈₀` | tractable? |
|---|---|---|---|
| **A — flop set preserved** | `n=n'`, `m=m'`, `D.flops` and `D'.flops` same length/widths in order, memories identical | **identity** (`st' = st`) | yes, decidable per `(st, is)` |
| **B — flop set permuted** | same flops, different order | a bijection `σ` with `st'.flops[σ i] = st.flops[i]` | needs names in the cert (§5) |
| **C — flop set changed** | flops added or removed | a stuttering refinement (below) | **out of scope for rung 1** |

**Case A is the target.** Measured available for cprop on
`intpipe_inst_bits_stage` (§4), and it is the expected shape for `pass.bitwidth`
too — bitwidth rewrites widths, not the flop set, though a width change makes
even Case A non-identity if a flop narrows.

**Case C is genuinely harder and should not be attempted first.**
`pass.single_edge` synthesises a phase divider, so `G'` has state `G` does not,
and `G'` takes P sub-steps per `G` step. The relation degrades to a *stuttering
refinement*: an abstraction `α : RuntimeState' → RuntimeState`, plus sampling
every P-th cycle,

```
(runTrace D is st).1  =  everyPth P (runTrace D' (stretch P is) st').1
```

with `P` and the sampling phase recovered from the pass. This is a different
theorem shape and belongs after rung 2.

### 3.4 What cannot be compared at all

`RuntimeState` derives **only `Inhabited`** (`Runtime.lean:21-24`):

```lean
structure RuntimeState where
  flops : Array BV
  mems  : Array (Int → BV)
deriving Inhabited
```

`mems` is function-valued, so there is no `DecidableEq RuntimeState` and there
cannot be one. Consequences:

- **outputs** (`Array BV`) — decidable, compare directly. This is the real check.
- **flops** (`Array BV`) — decidable, compare directly. Useful as a *stronger*
  optional check in Case A, and as a much better failure localiser than outputs
  alone.
- **memories** — **only pointwise at sampled addresses**. A full memory
  comparison is not expressible. Say so in the gate's output; a "PASS" that
  silently skipped memory state is the kind of thing this repo has been burned
  by before.

---

## 4. Measured: which pass pairs are worth checking

Two probes were run (small CORE-ET modules, detached, `MemoryMax`, `nice -n19
ionice -c3`).

### Probe 1 — the saved artifacts are not a usable pair

`txfma_e4` (3 nodes, combinational) and `intpipe_inst_bits_stage` (176 nodes,
32 flops), certificates emitted from `lgdb_raw` and `lgdb_norm` at both `O0`
and `O1`:

```
raw_O0  norm_O0  raw_O1  norm_O1   →  all four BYTE-IDENTICAL, both modules
```

Two facts fall out:

- **cprop is idempotent** on an already-cprop'd graph — the second cprop in
  step 4 changes nothing on these modules;
- **single_edge is a no-op** on a plain posedge design, exactly as
  `run_coreet_module_lean.sh:10-13` claims.

So the existing `lgdb_raw`/`lgdb_norm` pair **cannot** serve as the
differential. The interesting delta happens *inside* the first `lhd compile
verilog` invocation, before anything is written to disk.

### Probe 2 — a real pair, via `--recipe O0` on the source compile

`intpipe_inst_bits_stage`, `lhd compile verilog … --recipe {O0,O1} --emit-dir
lg:`, then certificate emitted from each at `--recipe O0`:

| certificate | **G0** (pre-cprop) | **G1** (post-cprop) |
|---|---:|---:|
| `SourceDesc.const` | **170** | **139** |
| `SourceDesc.input` | 5 | 5 |
| `SourceDesc.flopQ` | 32 | 32 |
| outputs | 1 | 1 |
| file bytes | 31,755 | 27,849 |

**31 constants folded; the input set, the output set and the flop set are all
untouched.** This is a textbook Case A pair, and it is the rung-1 experiment.

The `flopQ` ordinals were `0..31` on both sides (`SourceDesc.flopQ 0 1`,
`flopQ 1 1`, …) — but see §5: that stability is not guaranteed, it is luck.

### Recommended order

| rung | pair | case | why first |
|---|---|---|---|
| **1** | `G0` → `G1` (cprop) | A | measured available; flop set provably unchanged; largest node delta |
| **2** | `G1` → `G2` (bitwidth, O2) | A, with width changes | second real optimization; exercises the width axis, which is where the last shipped bug lived |
| **3** | `G1` → `G1'` with `pass.bitfuzz` on | A | **already a verification canary** (`lhd_kernel_common.cpp:778-790`): it strips width/sign annotations and forces reconstruction. Turning it on and demanding an *unchanged* certificate is the cheapest strong test in this whole plan |
| 4 | `G2` → `G3` (single_edge) | **C** | needs the stuttering relation; do last |

Rung 3 deserves emphasis. `pass.bitfuzz` already exists precisely to make width
assumptions observable, and Direction 2 gives it a much sharper oracle than it
has today: not "does the graph still typecheck" but "does it still compute the
same function".

---

## 5. The blocker: the certificate has no names

**Measured:** `grep -ciE 'name|"[a-z_]+"'` over an emitted certificate returns
**0**. There is not a single identifier in it.

The export structs (`pass/lean/design_cert_export.hpp:43-90`) carry `id`,
`kind`, `width`, `addr_w`, `ordinal` — and no name:

```cpp
struct SourceIn { uint32_t id; SourceKind kind; uint32_t width; …; uint32_t ordinal; … };  // :43
struct OutputIn { uint32_t id; uint32_t width; };                                          // :71
struct FlopIn   { uint32_t width; uint32_t din; std::optional<uint32_t> enable; … };        // :76
```

So cross-stage correspondence rests entirely on ordinals and array order. Those
come from three `std::map`s in `pass_lean.cpp`:

| what | container | keyed by | ordering | stable across passes? |
|---|---|---|---|---|
| inputs | `input_field` (`:259`) | **port name** (`std::string`) | alphabetical | **yes** |
| outputs | `output_field` (`:263`) | **port name** (`std::string`) | alphabetical | **yes** |
| flops | `flop_field` (`:266`) | **LGraph nid** (`uint32_t`) | nid order | **no** |

Ordinals are assigned by walking those maps in order
(`pass_lean.cpp:2398-2412`), and outputs are emitted by walking `output_field`
(`:2568-2574`).

**Two concrete hazards:**

1. **Flop ordinals are nid-ordered.** Any pass that renumbers nodes permutes the
   flop array silently. Probe 2 happened to preserve `0..31`, which proves
   nothing — cprop touched only constants there. The flop *name* is recoverable
   in principle (`pass_lean.cpp:2079-2100` derives `st_<wirename>` from the
   first non-`_` output wire), but it is **not exported**, and the fallback
   `flop_<nid>` bakes the nid into the name anyway.

2. **An undriven output is silently dropped.** `pass_lean.cpp:2569-2572`:
   ```cpp
   auto it = output_cert_ids.find(kv.first);
   if (it == output_cert_ids.end()) { continue; }   // undriven output: no slot to name
   ```
   So output arity can differ between stages, and the comparison would then
   align the wrong ports against each other. A differential that does not check
   arity first would report a spurious mismatch — or worse, a spurious match.

### Fix 1 (prerequisite for everything past rung 1)

Add a stable name to `SourceIn`, `OutputIn` and `FlopIn`, emit it into the
certificate as a comment or a parallel Lean array, and key the cross-stage
correspondence on **names, not ordinals**. For flops, prefer the wire name and
make the unnamed fallback deterministic (e.g. a hash of the driver cone) rather
than nid-derived.

This is a small emitter change and it is the difference between a gate that
works on one module and a gate that works on 129.

---

## 6. What to build

1. **`Compiler/Trace.lean`** — `runTrace` as in §3.1, plus
   `sampleMem : (Int → BV) → List Int → List BV` for the memory limitation.
   Small, no proofs needed; this is executable-model territory.
2. **`Compiler/Differential.lean`** — the checkable specialisation:
   ```lean
   def traceAgree (D D' : DesignCert) (ι ω : Array Nat)
       (st : RuntimeState) (st' : RuntimeState) (is : List RuntimeInput) : Bool
   ```
   Decidable, `native_decide`-able, returning the first differing cycle and port
   rather than a bare `false` — localisation is most of the value.
3. **Emitter change (Fix 1)** — names in the certificate.
4. **`scripts/stage_diff.sh`** — per module: compile at `--recipe O0` and
   `--recipe O1` to two `lg:` dirs, emit a certificate from each at
   `--recipe O0`, generate a driver `.lean`, run it.
5. **Arity/shape preflight** — refuse to compare when input arity, output arity,
   or flop count differ, with the reason named. This is what stops the §5
   hazards from producing a meaningless verdict.
6. **Input generation** — pseudorandom over the declared widths, seeded from
   `lhd.seed` so a failure reproduces. Constrained-random would be better but is
   not needed to find a pass bug.

---

## 7. Relation to `cert_lgraph_diff.py`

**It is not the first rung of this ladder.** It is a different axis, and
conflating them would overstate what exists:

| | `cert_lgraph_diff.py` | Direction 2 |
|---|---|---|
| graphs compared | **one** | **two** |
| extractors | **two** (`pass.lean` vs `inou.cgen.verilog`) | **one** |
| comparison | static, constants only | executable, full I/O trace |
| catches | a wrong constant width in the extractor | a pass that changes behaviour |

`cert_lgraph_diff.py` exists because the constant-width bug passed both gates by
being *shared* between the fast model and the certificate; the fix was a second,
independent extractor. Direction 2 keeps one extractor and varies the graph.

They are complementary and neither subsumes the other. What generalises is the
principle — *get a second opinion from something that does not share the failure
mode* — not the code.

---

## 8. What this establishes, and what it does not

**The claim to make:**

> Direction 2 is the only executable check on the **LiveHD graph-pass pipeline**.
> It can localise a behavioural discrepancy to a specific pass, which the LEC
> gate cannot do.

**Not** "the only empirical attack on RTL → LGraph" — that overstates it. The
boundary decomposes, and Direction 2 covers only the last arrow:

```
RTL ─slang─► AST ─yosys script─► RTLIL ─yosys2lg─► G_yosys ─cprop/bitwidth/single_edge─► G_final
└──────────────────── LEC gate covers this, end to end ─────────────────────────────────┘
                                                        └──── Direction 2 covers this ────┘
```

The LEC gate (`run_coreet_module_lean.sh:128-145`) already compares raw RTL
against the final graph, and it is *stronger* where it is conclusive — it is a
formal equivalence check, not testing. Direction 2's distinct contributions are:

- **localisation** — LEC says "the graph disagrees with the RTL"; Direction 2
  says "cprop is where it started";
- **coverage where LEC is inconclusive** — the runner already tolerates an
  inconclusive LEC (`:148`, non-fatal unless `LEC_STRICT=true`);
- **it tests the passes on their own terms**, without needing the RTL at all,
  so it works on graphs that have no RTL (post-`single_edge`, post-`abc`).

**What it does not establish — state all three in any report:**

1. **Nothing about the reader.** `G0` is already yosys's output. Everything
   upstream of `yosys2lg` — slang elaboration, the yosys script's `flatten`,
   `proc -ifx`, `memory -nomap` — is invisible to this check.
2. **It is testing, not proof.** The relation in §3.2 is universally quantified
   over input sequences; the gate checks finitely many. A pass bug on an
   unexercised path survives.
3. **It is blind to any error common to both stages.** Both certificates go
   through the same `pass.lean` extractor and are interpreted by the same
   `eval_op`. A wrong operator semantics cancels *exactly*, on both sides — this
   is structurally the same failure that let the constant-width bug through both
   gates. **Direction 2 cannot be the answer to "is `eval_op` right?"** It is
   the answer to "do the passes preserve whatever `eval_op` means".

That third point is the sharpest limitation and the one most likely to be
forgotten when the gate is green.

---

## 9. Cost

**Emit side** — measured on `intpipe_inst_bits_stage` (207 → 176 nodes): each
`lhd` invocation under 1 s. Two extra invocations per module (one `compile
verilog --recipe O0`, one `compile lg: --recipe O0 --emit-dir lean:`). Negligible
against the front-end cost already being paid.

**Evaluation side** — `denoteResidual` measured at **≈1.3 ms/cycle**. For 1,000
cycles that is ~1.3 s of evaluation per module, but per-module Lean startup
(~10 s import baseline, measured) dominates. Estimate for the **85 CORE-ET + 44
CVA6 = 129** modules already proving:

| | serial | 4-way |
|---|---:|---:|
| emit (2 × 129 invocations) | ~5 min | ~2 min |
| evaluate (1,000 cycles each) | ~25 min | ~7 min |

Comfortably a single detached sweep. **If A2 lands** (Phase 6, 10–50× on
`denoteResidual`), the evaluation half becomes free and the cycle count can go
up by the same factor.

**Caveat:** the 9 CORE-ET front-end hangs and the 2 CVA6 timeouts
(`cva6_mmu`, `cva6_tlb` at 1800 s) are unavailable to this sweep for the same
reason they are unavailable to the main one — the certificate never gets
produced.

---

## 10. Sequencing

Rungs 1–3 are independent of every other phase in the coverage plan and need no
theorem changes — `runTrace` and `traceAgree` are executable definitions, not
proofs. Rung 1 is roughly a day once Fix 1 lands; Fix 1 is a few hours.

The natural slot is **alongside Phase 3**, not after it: Phase 3 modifies
`pass.single_edge`, which is shared with the Isabelle/Rocq/ACL2 flows and the
LEC gate, and the plan already calls for a regression run across all of them.
A working `Sim(Gᵢ) ≟ Sim(Gᵢ₊₁)` gate is exactly the instrument that regression
wants — it would test the modified pass directly rather than inferring its
correctness from downstream proofs.

Rung 4 (single_edge, Case C) should follow Phase 3, since the phase-divider
structure it must model is the thing Phase 3 changes.
