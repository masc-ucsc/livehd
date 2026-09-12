# Direction 2 — implementation plan

Companion to `DIRECTION2_IR_SEMANTICS.md`, which established *what* to build and
*what the relation means*.  This is the executable plan, and it revises that
document on two points that changed after it was written.

---

## What changed since the planning pass

### 1. `DesignCert` is the shared artifact; the transport is a detail

`DIRECTION2_IR_SEMANTICS.md:345-368` (§6, item 4) calls for generating a driver
`.lean` per module and elaborating it.  That is one way to get a certificate
into Lean.  Direction 4 added a second — `Compiler/CertIO.lean` parses one at
run time, since `compileAndRun_correct` is stated `∀ D` and the design never
has to become a Lean constant.

**These are transports, not artifacts.**  `pass_lean.cpp` emits one
`DesignCert` and every direction consumes that same object: the root branch
evaluates it, D2 compares two of them, D3 reifies one, D4 parses one.  D2 needs
the artifact.  It does not need D4.

So **D2 is built against a certificate source, not against a transport**:

```
loadCert : FilePath → IO DesignCert     -- elaboration today, CertIO if merged
```

Default is elaboration, which keeps D2 landable on its own and keeps D4's
unverified `parseCert` out of D2's trusted base.  If D4 merges, D2 picks up the
fast path by swapping one function, and gets a side benefit: a differential that
runs both transports over the same certificate puts `parseCert` itself under
test.

An earlier draft of this plan made D4 a prerequisite and justified it by saying
the two ~108k-node modules "have never been elaborated at all."  True as stated,
but it implied impossibility.  Extrapolating the measured O(N^1.77):

| module | nodes | elaboration, extrapolated |
|---|---:|---:|
| cva6_hpdcache_wrapper | 107,213 | ~2.8 h |
| cva6_hpdcache_subsystem | 108,666 | ~2.9 h |

Expensive, not impossible — a reason to prefer the fast transport when it
exists, never a reason to couple the directions.

### 2. The cost model in §9 is wrong by roughly 8×

§9 uses `≈1.3 ms/cycle`.  That figure was withdrawn: the probe materialised
`(List.range 20000)` inside the timed region, and it described a **one-node**
design besides.  The real unit is **≈28 µs per node-cycle** (measured on
`csr_buffer_gate`: 44 nodes, 1.251 ms/cycle), so evaluation cost scales with
design size, which §9 does not account for.

Recomputed over the 46 emitted CVA6 modules — 416,829 nodes, mean 9,061,
max 108,666:

| cycles per module | serial |
|---|---:|
| 100 | 19.5 min |
| 1,000 | **194.5 min** |

**Decision: budget node-cycles, not cycles.**  A fixed cycle count spends 92×
more compute on `cva6_hpdcache_subsystem` than on a 1k-node module for no extra
confidence.  At 10M node-cycles per module (~280 s):

| module | nodes | cycles |
|---|---:|---:|
| cva6_hpdcache_subsystem | 108,666 | 92 |
| csr_regfile | 34,874 | 286 |
| a typical 1k-node block | 1,000 | 10,000 |

Depth where the state space is small, breadth where it is not.

---

## Phase 0 — names in the certificate  *(prerequisite, ~half a day)*

**This belongs on the root branch, not on D2.**  It changes the shared
`DesignCert` that every direction consumes, so it is emitter work that D2
happens to be the first caller to need — the same way ROM `init` support was.
Landing it on a direction branch would fork the artifact.

Nothing past a single-module demo works without this.  Measured: an emitted
certificate contains **zero identifiers**.

Flops are keyed by LGraph nid (`pass_lean.cpp:266`), so any renumbering
permutes the state array silently; inputs and outputs are keyed by port name and
are alphabetically stable, so only the flop axis is actually broken.

1. Add `name` to `SourceIn`, `OutputIn`, `FlopIn`
   (`pass/lean/design_cert_export.hpp:43-90`).
2. For flops prefer the wire name already derived at `pass_lean.cpp:2079-2100`;
   make the fallback deterministic — a hash of the driver cone, **not**
   `flop_<nid>`, which bakes in the thing we are trying to be robust against.
3. Emit as a parallel array so the existing `DesignCert` shape is untouched and
   every proven module keeps proving.
4. Any transport that reads certificates round-trips the new field — today
   that means the elaborated form; `CertIO`'s `parseCert`/`writeCert` too if
   D4 has merged by then.

**Gate:** re-run the 129-module sweep and confirm 0 regressions.  This touches
the emitter every proof depends on, so it is the one phase that can break
existing work.

---

## Phase 1 — the comparison core  *(~1 day)*

Executable definitions only.  No proofs — this is model-use, not model-building.

1. `Compiler/Trace.lean`
   - `runTrace : DesignCert → List RuntimeInput → RuntimeState → List RuntimeResult`
   - `sampleMem : (Int → BV) → List Int → List BV` — memories are function-valued,
     so `RuntimeState` has no `DecidableEq` and never can; they compare only
     pointwise at sampled addresses.
2. `Compiler/Differential.lean`
   - `traceAgree (D D' : DesignCert) (namesIn namesOut namesFlop : Array String) …`
   - Returns **the first differing cycle and the port name**, not `Bool`.
     Localisation is most of the value; a bare `false` on a 108k-node module is
     nearly useless.
3. `Compiler/DiffMain.lean` — the entry point: two certificates, a seed, a
   node-cycle budget; exit 0 / 1 with a report on stderr.  It takes certificates
   through `loadCert`, so it is indifferent to how they arrived.

Correspondence keys on **names**, never ordinals.

---

## Phase 2 — preflight and harness  *(~1 day)*

**Preflight refuses to compare rather than producing a meaningless verdict.**
Refuse, naming the reason, when input arity, output arity or flop count differ.
This matters concretely: `pass_lean.cpp:2569-2572` silently `continue`s past an
undriven output, so output arity genuinely can differ between stages, and a
comparison that aligned the wrong ports could report a spurious **match**.

`scripts/stage_diff.sh <module>`:
1. `lhd compile verilog … --recipe O0 --emit-dir lg:G0`
2. `lhd compile lg:G0 --recipe O1 --emit-dir lg:G1`
3. certificate from each at `--recipe O0` (verified: this certifies a graph
   without perturbing it)
4. `DiffMain G0.cert G1.cert --seed $LHD_SEED --budget 10M`

Inputs are pseudorandom over declared widths, seeded so any failure reproduces.

---

## Phase 3 — rung 1, cprop  *(~1 day)*

One module first: `intpipe_inst_bits_stage`, the measured pair — 170 → 139
const sources, inputs and flops untouched.  Case A, so the state relation is the
identity and the whole thing is decidable.

Then all 129.  **Expected result: no differences.**  A green sweep here is not a
null result — it is the calibration that makes a later red meaningful, and it is
the first time the pass pipeline has ever been checked behaviourally.

---

## Phase 4 — rungs 2 and 3  *(~2 days)*

**Rung 2, bitwidth (O2).** Exercises the width axis, where the last shipped bug
lived.

**Rung 3, `pass.bitfuzz` — do this one first if time is short.**
`lhd_kernel_common.cpp:778-790`: it already exists as a verification canary,
stripping width/sign annotations and forcing reconstruction.  Today its oracle
is "does the graph still typecheck."  Direction 2 upgrades that to "does it
still compute the same function," for the cost of turning a flag on.  Cheapest
strong test in either document.

---

## Phase 5 — rung 4, `single_edge`  *(after coverage Phase 3)*

Case C: `single_edge` inserts a phase divider, so the flop set changes and the
relation degrades to **stuttering refinement** — an abstraction α plus
every-P-th-cycle sampling.

Sequenced after coverage Phase 3 deliberately: that phase *modifies*
`pass.single_edge`, and the plan already calls for a regression run across the
Isabelle/Rocq/ACL2 flows and the LEC gate.  This gate is what that regression
wants — it tests the modified pass directly instead of inferring its
correctness from downstream proofs.

---

## Definition of done

- [ ] Names in the certificate; 129-module sweep shows 0 regressions
- [ ] `DiffMain` runs on two certificates with no Lean elaboration
- [ ] Preflight refuses on arity mismatch, with the reason named
- [ ] Rung 1 green across all 129
- [ ] Rung 3 (`bitfuzz`) green, or a localised failure with cycle and port
- [ ] Findings folded back into `DIRECTION2_IR_SEMANTICS.md`

## Cost

| phase | effort | compute |
|---|---|---|
| 0 names | ~0.5 day | one 129-module re-sweep |
| 1 core | ~1 day | — |
| 2 harness | ~1 day | — |
| 3 rung 1 | ~1 day | ~20 min at 10M node-cycles, 4-way |
| 4 rungs 2–3 | ~2 days | ~40 min |
| 5 rung 4 | after coverage Phase 3 | — |

## What would invalidate this plan

- **Certificates prove identical across every pass pair.**  Then the gate has no
  discriminating power on this benchmark set and its value is regression-only.
  Probe 1 already saw this once — four certificates byte-identical — though that
  was an artifact of comparing saved libraries rather than mid-compile stages.
- **`--recipe O0` turns out to perturb the graph** on some module.  The
  no-graph-passes claim is verified on two modules, not 129.
- **The 28 µs/node-cycle rate does not hold** at 100k nodes.  It is measured at
  44 nodes and at one cycle on the large ones; the intermediate range is
  interpolated, not measured.

## Standing limitation

Both sides share one extractor and one `eval_op`, so a wrong operator semantics
**cancels exactly** — structurally the same blind spot that let the
constant-width bug through two gates.  This answers whether the passes preserve
what `eval_op` means, never whether `eval_op` is right.
