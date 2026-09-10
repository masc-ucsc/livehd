# Direction 4 — incremental ΔG → ΔSim → ΔProof

## Headline

**ΔProof is already free. It was never the problem.** The per-design cost is
**~100% Lean elaboration of the certificate data literal**, measured at two
scales, and that literal is *not required by the proof at all*.

`compileAndRun_correct` (`Compiler/CompileDesign.lean:358`) is quantified over
every `DesignCert`:

```lean
theorem compileAndRun_correct (D : DesignCert) (h : compilesOk D = true) :
    ∀ inp st, compileAndRun D inp st = interpretDesign D inp st
```

Nothing in it needs `D` to be a *named constant elaborated at compile time*. The
per-design `.lean` file is a **packaging choice**, and it currently costs
**23 s for a 3 k-node design and 1,399 s for a 35 k-node one** — 91–98 % of
end-to-end wall time on the large modules.

So Direction 4 splits into two very different halves:

| half | status |
|---|---|
| **ΔProof** | **already zero** — no per-design proof exists to redo |
| **ΔSim** | real, and dominated by one cost with three possible fixes |

This document measures the cost, enumerates the fixes, and states what Direction 4
becomes under each of the two futures for Direction 3.

---

## 1. Every per-design obligation, enumerated

Read from an actual emitted file (`generated/cva6_vc/lean/alu_gate_Lgraph.lean`,
12,786 lines / 845 KB), not from the emitter source. A generated certificate
contains exactly six items:

| # | item | what it costs |
|---|---|---|
| 1 | `import LeanSemanticPrimitives.Compiler.CompileDesign` | fixed baseline, **5.3 CPU-s warm** |
| 2 | `def <Top>_designCert : DesignCert := { … }` | **the whole cost — see §2** |
| 3 | `def <Top>_step := compileAndRun <Top>_designCert` | free (below noise) |
| 4 | `theorem <Top>_compiles : compilesOk … = true := by native_decide` | ≤ 2 CPU-s @ 3.2 k nodes |
| 5 | `theorem <Top>_step_correct := compileAndRun_correct …` | free — direct instantiation, no script |
| 6 | `def <Top>_residual : ResidualProgram := …` | free (never forced) |

Items 3–6 together are **within measurement noise** of zero. The parent's recollection
of "~0.8 s `native_decide` on DINO's 4,772 nodes" is consistent with what I measured
and is *not* the bottleneck — it is roughly 3 % of that design's Lean cost.

There is no seventh obligation. No per-node lemma, no proof script, no `sorry`.

---

## 2. Where the time actually goes (measured)

### Method

`pmp_gate` (2,881 sources + 3,213 nodes) cut into cumulative prefixes, each run
as `lake env lean` from `formal/lean` exactly as `scripts/run_lean_queue.sh:92-96`
does, `taskset -c 0-7`, `LEAN_NUM_THREADS=8`, detached under `systemd-run --user`.
CPU-seconds (user+sys), not wall, because the box is shared.

| variant | contains | wall s | peak RSS | **CPU s** | marginal |
|---|---|---:|---:|---:|---:|
| v0 | import + `set_option`s only | 24.78 | 6.6 GB | 13.3 | — |
| v1 | + the `designCert` literal | 32.63 | 7.09 GB | 33.3 | **+20.0** |
| v1b | + `_step` def | 30.25 | 7.16 GB | 31.3 | −2.0 *(noise)* |
| v2 | + `native_decide` theorem | 28.63 | 7.15 GB | 29.4 | −1.9 *(noise)* |
| v3 | full file | 28.15 | 7.17 GB | 29.8 | +0.4 |

v0's 24.78 s wall / 13.3 CPU-s was a **cold** olean read (CPU ≪ wall). A later
warm v0 measured **5.27 s wall / 5.3 CPU-s**; use that as the baseline. Everything
after v1 is flat.

### Confirmation at 5× the size

`issue_stage_gate`, 14,630 sources + 15,601 nodes:

| | wall s | peak RSS | CPU s |
|---|---:|---:|---:|
| full file (sweep, `prove.tsv`) | **311.48** | 8.7 GB | — |
| **literal only** (this run) | **318.83** | 9.0 GB | 324.7 |

The literal alone costs as much as the entire file. Measured at 3.2 k nodes and
at 15.6 k nodes; the same shape is *extrapolated*, not measured, at 35 k.

### The cost is superlinear in elements, and memory is not

| design | elements (src+nodes) | literal CPU s | ms / element | peak RSS − 6.6 GB | KB / element |
|---|---:|---:|---:|---:|---:|
| `pmp_gate` | 6,094 | 19.5 | **3.2** | 0.5 GB | 82 |
| `issue_stage_gate` | 30,231 | 319.4 | **10.6** | 2.4 GB | 79 |
| `csr_regfile_gate` | 67,696 | ~1,393 | **20.6** | 5.1 GB | 75 |

11.1× the elements costs 71.4× the time → **O(N^1.77)**. Memory per element is
flat at ~78 KB, so the elaborator is doing something superlinear in *time* while
storing linearly — repeated traversal, not repeated allocation.

The `csr_regfile` row's CPU is inferred from its `prove.tsv` wall time (1,398.67 s)
minus baseline, not separately decomposed. Flagged as the weakest number here.

### Node elements cost 3× what source elements cost

Splitting `pmp_gate`'s two arrays into standalone modules and timing each warm:

| module | elements | wall s | CPU s | marginal CPU | ms / element |
|---|---:|---:|---:|---:|---:|
| baseline (import only) | — | 5.27 | 5.3 | — | — |
| `pmp_sources : Array SourceDesc` | 2,881 | 8.87 | 9.5 | +4.2 | **1.5** |
| `pmp_nodes : Array DenseNodeCert` | 3,213 | 20.24 | 20.6 | +15.3 | **4.8** |

`DenseNodeCert` is a 4-field structure with a nested `deps : Array Nat`
(`Compiler/DesignCert.lean:83`); `SourceDesc` is a flat constructor application
(`:32`). The nested array is what costs.

Note 4.2 + 15.3 = 19.5 ≈ the monolithic 20.0 — **at 6 k elements the cost is still
additive**, so splitting buys nothing yet. Superlinearity appears above ~6 k
elements, which is exactly where the expensive designs live.

### Emit side, for comparison

From `preflight.log` → certificate mtime in `generated/cva6_vc/mod/<m>/`:

| design | nodes | emit s | Lean s | Lean share |
|---|---:|---:|---:|---:|
| `pmp` | 3,213 | 3 | 23.85 | 89 % |
| `mult` | 4,202 | 3 | 32.63 | 92 % |
| `decoder` | 8,971 | 9 | 106.70 | 92 % |
| `scoreboard` | 9,584 | 117 | 127.69 | 52 % |
| `fpu_wrap` | 28,410 | 23 | 932.40 | **98 %** |
| `csr_regfile` | 34,874 | 140 | 1,398.67 | **91 %** |

Emit cost tracks the **slang/yosys front end**, not node count — `scoreboard`
spends 117 s in the front end while the larger `fpu_wrap` spends 23 s. These are
mtime deltas from a `-P 4` parallel sweep, so treat them as ±30 %, not precise.

**Conclusion: optimise Lean elaboration, or nothing else matters.**

---

## 3. Three ways to make ΔSim cheap

### Option A — don't elaborate the certificate at all *(recommended, and it makes ΔSim zero)*

`compileAndRun_correct` is `∀ D`. The design does not have to be a Lean constant.
Add, **once**, to the library:

```lean
def runChecked (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState)
    : Option RuntimeResult :=
  if compilesOk D then some (compileAndRun D inp st) else none

theorem runChecked_correct (D : DesignCert) (inp st) (r : RuntimeResult) :
    runChecked D inp st = some r → r = interpretDesign D inp st
```

Then a design is a **file read at runtime**, not a term. Per-design Lean cost
drops from 1,399 s to **zero**, for every design, at every size. ΔSim becomes
"re-serialise the certificate", which is already 3–9 s of emit for most modules.

What is given up, honestly:

- No per-design `#print axioms` gate. The plan's verification step 4
  (`lovely-popping-canyon.md`) would need to move to the library, where it is
  checked once instead of 44 times — arguably better, but it *is* a change.
- The deserialiser enters the TCB. It is small and could be verified separately,
  or deliberately trusted with the `cert_lgraph_diff.py` gate as backstop.
- No named, statically-checked artifact per design. For a *simulator* this is
  irrelevant; for a paper artifact it may not be.

**This should be measured before anything else in this direction is built.** It
is a ~1-day experiment and it may retire the entire cost problem.

### Option B — split the literal across Lean modules

Viable, and superlinearly profitable *at scale*. Splitting N elements into k
modules costs `k·(N/k)^1.77 = N^1.77 / k^0.77`; a one-module edit costs
`N^1.77/k^1.77`.

Extrapolated for `csr_regfile` (67,696 elements, 1,393 CPU-s) at k=8:

| | monolithic | 8 modules |
|---|---:|---:|
| full rebuild | 1,393 s | ~290 s |
| **one-module edit** | 1,393 s | **~36 s** |

**Extrapolation from three points, not measured.** The 8-way split has not been
built. The k=2 split at `pmp` scale showed *no* saving (§2), consistent with the
model but not evidence for it.

Two real obstacles:

1. **`DesignCert` is flat.** Sources occupy `0..S-1`, node `i` occupies `S+i`
   (`DesignCert.lean:121-127`, and the header comment in every emitted file).
   There is no hierarchy to split along — the boundary must be invented, and a
   ΔG that shifts slot indices invalidates every downstream module. Compare
   `cgen_sim`, which splits along LGraph module hierarchy for free (§4).
2. `native_decide` still runs over the whole array in the top module. Harmless
   today (≤ 2 CPU-s at 3.2 k), **unverified at 35 k**.

### Option C — change the literal's encoding

Unexplored. `DenseNodeCert`'s nested `deps : Array Nat` costs 3× a flat
constructor (§2), so encoding the whole certificate as a `String` or `ByteArray`
decoded by a verified parser would collapse elaboration to one literal. This is
Option A with the data still inside Lean — it keeps the per-design artifact and
the `#print axioms` gate. Cost of the decoder proof is unknown.

---

## 4. LiveHD's actual incremental machinery

**LiveSim is not in this repository.** The only hits are citations:
`README.md:94` references *LiveSim: A Fast Hot Reload Simulator for HDLs*
(ISPASS 2020) as a paper. No `livesim`, `hot_reload`, or checkpointing source
exists under `core/`, `pass/`, `inou/`, or `lhd/`. `README.md:22,39-42` states
incrementality as a project *goal* ("small changes … results in a few seconds",
"we do not need to perform too fine grain incremental work") — aspiration, not
implementation.

What does exist and is directly relevant:

- **`inou/cgen/cgen_sim.cpp` (3,795 lines)** — LiveHD's *compiled-simulation*
  backend, the C++ analogue of what Direction 4 wants in Lean. Its header
  (`inou/cgen/cgen_sim.hpp:14-23`) documents an incremental design already
  shipped:

  > "Each module is split into `<name>.hpp` (the interface …) and `<name>.cpp`
  > (the bodies, 'the slop') **so a body edit recompiles one .o** and a module
  > appears once however many times it is instantiated."

  This is Option B, in C++, along the **module hierarchy** — the split boundary
  our flat `DesignCert` does not have. It is the precedent to copy, and the
  argument for teaching `pass.lean` to preserve hierarchy.

- **LGraph persistence** — `Hhds_graph_library::save(path)`
  (`graph/graph_library_singleton.cpp:60`), writing to the `lgdb/` directory
  (present, empty at rest). So ΔG *detection* has a place to live, but nothing
  currently computes a graph delta.

**Finding: there is no incremental infrastructure to connect to.** Direction 4
would be building it, not wiring into it — with `cgen_sim`'s module split as the
one existing design to follow.

---

## 5. Direction 4 under the two futures

The parent's framing is right and the tension is real.

### Future 1 — stay deep (status quo, or A2)

- **ΔProof = 0.** Structurally, not by optimisation. A design edit of any size
  re-runs one `native_decide` and nothing else.
- **ΔSim = elaboration only**, and Option A deletes it entirely.
- Direction 4 is therefore **mostly already solved**, and what remains is an
  engineering question about serialisation, not a research question.
- A2 does not change this: `FastProgram` is still data produced by a `∀ R`
  theorem, so it adds no per-design obligation.

### Future 2 — go shallow (Direction 3: per-design proofs for the last mile)

Everything above inverts.

- A generated `def <Top>_step := let n3 := …; let n7 := …` is a **term**, so it
  must be elaborated, kernel-checked *and* compiled to native code — three costs
  where today there is one, on an object the same size as the certificate.
- `reify_correct : generated = denoteResidual R` is a **real per-design proof**,
  linear in node count at best. On `csr_regfile`'s 34,874 nodes that is 34,874
  rewrite steps.
- It must not be `rfl`: naming a `ResidualProgram` in a theorem statement forces
  kernel reduction of `compileDesign`, and `Array.push` unfolds to
  `⟨toList ++ [a]⟩` → O(N²). That cost 27 min / 27 GB once already and is now
  gated by `pass/lean/scripts/vc_gates.py`.
- Option A becomes **unavailable** — a runtime-loaded certificate cannot have a
  compile-time generated `def` proved equal to it.
- Option B becomes **mandatory**, and its blocker (the flat slot space) becomes
  a blocker for Direction 3 as well: splitting a shallow `def` across modules
  needs a *composition* theorem for `compileDesign` over subgraphs, which does
  not exist.

**So Direction 3 and Direction 4 are not merely in tension — Direction 3 creates
the problem Direction 4 solves.** Anyone costing Direction 3 must add the
incremental work to its price, and the flat-`DesignCert` refactor is the shared
prerequisite.

---

## 6. What is not measured

Stated plainly, because the numbers above are load-bearing:

1. **Option A has not been built or timed.** The claim that per-design Lean cost
   goes to zero follows from `compileAndRun_correct`'s `∀ D`, but the runtime
   deserialisation cost is unknown.
2. **The 8-way split (Option B) is an extrapolation from three points.** The one
   split actually built (k=2, `pmp`) showed no saving.
3. **`native_decide` is measured cheap only at 3.2 k nodes.** Its behaviour at
   35 k — where it must compile the literal to native code — is unknown, and it
   is the one item that Option B cannot incrementalise.
4. **Emit-side times are mtime deltas from a parallel sweep**, ±30 %.
5. `csr_regfile`'s literal cost is inferred by subtraction, not decomposed.
6. No ΔG computation exists anywhere, so "how big is a typical Δ?" — the number
   that determines whether any of this pays — is **entirely unmeasured**. A
   one-line RTL edit may well perturb 60 % of a flat post-`cprop` LGraph.

Item 6 is the one that could invalidate the whole direction, and it is cheap to
settle: emit certificates for two adjacent RTL revisions and diff them.

---

## 7. Recommended sequence

1. **Measure Δ first** (½ day). Two adjacent revisions of one CVA6 module →
   two certificates → diff. If a small RTL edit perturbs most of the flat slot
   space, Option B is dead and Option A is the only answer. *Nothing else in this
   plan should start before this number exists.*
2. **Prototype Option A** (1 day). `runChecked` + a certificate reader. If it
   works, per-design Lean cost is zero and items 3–4 below are unnecessary.
3. **Only if Option A is rejected for artifact reasons:** build the 8-way split
   for `csr_regfile` and measure against the 1,393 s baseline.
4. **Only if Direction 3 is adopted:** the flat slot space must be replaced by a
   hierarchy-preserving `DesignCert` with a composition theorem, following
   `cgen_sim.hpp:19-23`. Budget this as part of Direction 3, not Direction 4.

**Recommendation: do steps 1 and 2, then stop and re-evaluate.** There is a real
possibility that this direction is two days of work rather than a research
programme — and reporting that honestly is worth more than manufacturing a
roadmap around a cost that a `∀ D` theorem already lets us delete.

---

## Appendix — sweep timings used

44 modules, `generated/cva6_vc/prove.tsv`, all PROVEN, 0 gate failures.
**`prove.tsv` is append-only and contained duplicate rows** from an earlier run
against pre-v8 wrappers; every figure here takes the *last* row per module. Using
the first row instead reports `alu_gate` at 13.76 s (a 436-node design) rather
than 65.99 s (6,597 nodes) — a 5× error, and the same class of harness bug that
has bitten this project twice before.

| design | nodes | wall s | peak RSS |
|---|---:|---:|---:|
| `cvxif_fu_gate` | 23 | 9.69 | 6.3 GB |
| `pmp_gate` | 3,213 | 23.85 | 6.8 GB |
| `mult_gate` | 4,202 | 32.63 | 7.0 GB |
| `decoder_gate` | 8,971 | 106.70 | 7.7 GB |
| `scoreboard_gate` | 9,584 | 127.69 | 7.8 GB |
| `issue_stage_gate` | 15,601 | 311.48 | 8.7 GB |
| `fpu_wrap_gate` | 28,410 | 932.40 | 10.5 GB |
| `csr_regfile_gate` | 34,874 | 1,398.67 | 11.7 GB |
