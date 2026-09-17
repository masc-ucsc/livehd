# Direction 2 — results, measurements, and the trust boundary

Companion to `DIRECTION2_IR_SEMANTICS.md` (scope and claim) and
`DIRECTION2_IMPLEMENTATION.md` (engineering order).  This document records what
was actually run, what it produced, and — phase 7 — exactly which links in the
chain are proved, checked, tested, or trusted.

Everything below is reproducible from the repository; the command for each
result is given with it.

---

## 1. What was built

| artifact | where |
|---|---|
| acceptance predicate + accepted-operator table | `formal/lean/LeanSemanticPrimitives/Compiler/DirectCheck.lean` |
| dense evaluator, one-cycle step, `directStep_correct` | `.../Compiler/DirectSemantics.lean` |
| trace runner, `runDirect_correct` | `.../Compiler/DirectTrace.lean` |
| simulator library (`simMain`, file formats) | `.../Compiler/DirectSim.lean` |
| toy shapes, malformed certificates, mutants, 200k chain | `.../Compiler/DirectExamples.lean` |
| execution gate (`#guard`s run by `lake build`) | `.../Compiler/DirectTests.lean` |
| agreement with B1+B2 | `.../Compiler/DirectVsCompiled.lean` |
| measurement + differential harness | `.../Compiler/DirectBench.lean` |
| bundled binary | `formal/lean/Main.lean` → `lake build LgraphSim` |
| per-design launcher generator | `pass/lean/scripts/make_sim_launcher.py` |
| certificate sweep | `pass/lean/scripts/direct_sweep.py` |

The direct chain (`DirectCheck` → `DirectSemantics` → `DirectTrace` →
`DirectSim`) imports neither the residual compiler nor Mathlib.  That is not an
optimisation note: it is the structural claim of Direction 2, and the import
graph is what enforces it.  `DirectVsCompiled` and `DirectBench` are the only
modules that mention B1+B2, and they only compare.

---

## 2. Theorems

```lean
theorem directStep_correct (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (r : RuntimeResult) (h : directStep D i s = .ok r) :
    r = interpretDesign D i s

theorem runDirect_correct (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput)
    (t : TraceResult) (h : runDirect D s is = .ok t) :
    t = refTrace D s is

theorem checkDesign_sound  (h : checkDesign D = .ok u)      : DesignSemWF D
theorem checkRuntime_sound (h : checkRuntime D i s = .ok u) : RuntimeSemWF D i s

theorem directStep_eq_compileAndRun (hd : directStep D i s = .ok r)
    (hc : compilesOk D = true) : r = compileAndRun D i s
```

plus `directStep_deterministic`, `directStepRaw_sizes` (output/flop/memory array
shapes preserved), `directFlopNext_agree` (reset priority, polarity, value and
the hold fallback, checked against an independently written rule), and
`runDirectFrom_length`.

`#print axioms`, for all of them:

```text
'Compiler.Direct.directStep_correct'        [propext, Classical.choice, Quot.sound]
'Compiler.Direct.runDirect_correct'         [propext, Classical.choice, Quot.sound]
'Compiler.Direct.directStep_eq_compileAndRun' [propext, Classical.choice, Quot.sound]
'Compiler.Direct.checkDesign_sound'         [propext, Quot.sound]
'Compiler.Direct.directStep_deterministic'  [propext]
```

No `sorry` appears in any `Direct*` module.

---

## 3. Execution gate (phase 2)

`Compiler/DirectTests.lean` is ~70 `#guard`s that run during `lake build`, so a
regression is a build failure rather than a test someone forgot to run.  They
were verified to actually fail: a deliberately wrong expected value produces

```text
error: Expression decide (… = #[mk_bv 4 9]) did not evaluate to `true`
```

Covered:

* **combinational** — `(3+5)&0xF = 8`, `(9+12)&0xF = 5` (the 4-bit sum wraps
  first), and agreement with `interpretDesign`;
* **sequential** — four enabled cycles count `0,1,2,3` and leave `Q = 4`; with
  the enable deasserted the count holds;
* **asynchronous reset** — with `Q = 5` and reset asserted the output reads
  **0 in the same cycle**; with reset released the same state reads 5.  A plain
  `flopQ` source would report 5 in both, which is the entire reason
  `flopQAsync` exists;
* **inlined ROM** — a 4-entry table reads 10/20/30/40, and contributes no entry
  to `RuntimeState.mems`;
* **mutable memory** — the read observes the PRE-write image (0, not 77) while
  the next image holds 77 at address 1 and 0 elsewhere; cycle 2 reads it back;
* **scale** — a 1,000-deep adder chain, and agreement with `interpretDesign` on
  a 200-deep one;
* **refusals** — all ten malformed certificates, asserted by error TAG, so the
  test says *which* refusal fired;
* **runtime shape** — short input vectors and wrong-sized states are refused.

### Negative controls

Seven single-field mutations of otherwise WELL-FORMED certificates (all seven
are accepted by the checker — what is being tested is the evaluator's
sensitivity, not the checker's).  Each must change the output trace:

| mutation | detected |
|---|---|
| operator `Op_Sum 2` → `Op_Xor` | yes |
| dependency `Q + 1` → `Q + Q` | yes |
| constant `1` → `2` | yes |
| output slot `Q` → `Q+1` | yes |
| flop enable dropped | yes |
| flop `resetActiveLow` flipped | yes |
| memory `nextImg` → the pre-write image | yes |

The reset-polarity mutant is the interesting one: §5.4 of the semantic document
explains why the checker deliberately does *not* cross-check that field, and
this control is the evidence that leaving it unchecked is a judgement about
what is checkable rather than an oversight.

---

## 4. Real-design sweep (phases 1 and 6)

```bash
python3 pass/lean/scripts/direct_sweep.py --out pass/lean/SWEEP_direction2.tsv \
    --jobs 6 --cycles 4 /soe/czeng14/projects/livehd-new/generated
```

Every generated `*_Lgraph.lean` containing a `DesignCert`, deduplicated by
content hash, is run through `checkDesign` and then simulated for four cycles
through the CHECKED `runDirect`.  The probe imports `Compiler.DirectTrace`, not
`Compiler.CompileDesign` — the direct path needs neither the verified compiler
nor Mathlib, which also drops peak RSS from the ~6.6 GB the B1+B2 sweep needs to
under 0.5 GB for small designs.

| verdict | designs |
|---|---|
| **ACCEPTED** | 145 |
| **LEAN_ERROR** | 2 |

147 distinct certificates, 449,969 nodes in total.  72 carry sequential state, and 3 carry a mutable memory (2 distinct modules: `intpipe_csr_msgs` with one, `minion_dcache_reduce` with four; the
twelve-memory `cva6_hpdcache_wrapper_gate` is accepted too — see below).  Content-hash deduplication keeps near-duplicates that differ only in a header comment, so a few modules appear twice.  The largest is `csr_regfile_gate` at 34,874 nodes (32,822 sources); its semantic check takes 622 ms.

A cross-section (all figures from the sweep TSV; times are the Lean
INTERPRETER, which the sweep uses -- the native numbers are in §6):

| design | sources | nodes | flops | mems | check | 1st step | 4 cycles |
|---|---|---|---|---|---|---|---|
| `aes_gate` | 8,658 | 11,681 | 0 | 0 | 131 ms | 564 ms | 2317 ms |
| `csr_buffer_gate` | 45 | 44 | 2 | 0 | 2 ms | 3 ms | 7 ms |
| `alu_gate` | 6,137 | 6,597 | 0 | 0 | 81 ms | 754 ms | 3060 ms |
| `SingleCycleCPU` | 4,438 | 4,772 | 33 | 0 | 60 ms | 342 ms | 1340 ms |
| `PipelinedCPU` | 4,711 | 5,061 | 64 | 0 | 66 ms | 350 ms | 1380 ms |
| `PipelinedDualIssueCPU` | 9,862 | 10,740 | 97 | 0 | 133 ms | 705 ms | 2799 ms |
| `intpipe_csr_msgs` | 6,510 | 7,447 | 53 | 1 | 124 ms | 397 ms | 1461 ms |
| `bht_gate` | 7,372 | 7,275 | 256 | 0 | 102 ms | 2396 ms | 9284 ms |
| `vpu_mask` | 14,590 | 14,860 | 60 | 0 | 323 ms | 2093 ms | 6733 ms |
| `issue_stage_gate` | 14,630 | 15,601 | 324 | 0 | 306 ms | 32585 ms | 127044 ms |
| `fpu_wrap_gate` | 24,825 | 28,410 | 275 | 0 | 394 ms | 1652 ms | 6461 ms |
| `csr_regfile_gate` | 32,822 | 34,874 | 136 | 0 | 622 ms | 35870 ms | 139609 ms |

### The two that did not run at the emitted recursion limit

`cva6_hpdcache_wrapper_gate` and `cva6_hpdcache_subsystem_gate` (14 MB
certificate files) fail before `checkDesign` is ever called:

```text
hpd_probe.lean:13:4: error: maximum recursion depth has been reached
hpd_probe.lean:204274:0: error: failed to compile definition, consider marking it as
  'noncomputable' because it depends on 'cva6_hpdcache_wrapper_gate_designCert',
  which is 'noncomputable'
```

Line 13 is the `sources := #[…]` array.  Lean's front end exhausts
`maxRecDepth` ELABORATING the certificate literal — the exact failure
`design_cert_export.hpp`'s own header predicts — the definition becomes
noncomputable, and that cascades into everything below it.  This is a
CERTIFICATE TRANSPORT limit, not a semantic one, and it is a ceiling B1+B2
shares: neither design appears in `SWEEP_b1-b2.tsv` or `SWEEP_cva6.tsv`.

**Raising the limit fixes it, and the design then simulates.**  Re-run with
`set_option maxRecDepth 20000000` in place of the emitted `1000000`:

```text
SHAPE     sources=96486 nodes=107213 outputs=19 flops=515 mems=12 inputs=22
VERDICT   ACCEPTED
CHECK_MS  1780
STEP1_MS  9970    outsum 2147483650
RUN_MS    35321   cycles 4
RUNDIRECT OK      4
wall 14289 s   peak 16.6 GB
```

`cva6_hpdcache_wrapper_gate` is by a wide margin the largest certificate
anywhere in the corpus — **107,213 nodes, 96,486 sources, 515 flops and twelve
mutable memories**, three times `csr_regfile_gate` — and the direct simulator
accepts it and runs it.  Almost the entire 4-hour wall and all 16.6 GB are
Lean's front end ELABORATING the literal: the semantic check itself is 1.8 s and
four cycles are 35 s.

So the sweep's two `LEAN_ERROR` rows are a property of the emitted
`set_option`, not of the design or of the checker.  Two consequences worth
acting on separately:

* `design_cert_export.hpp` emits `maxRecDepth 1000000`, which is not enough at
  this size.  Raising it is a one-line change to the exporter — deliberately
  NOT made here, because that emitter is shared with B1+B2 and changing it
  rewrites every generated file for both directions.
* Direction 4's runtime loader would remove the cost entirely rather than
  raise a constant: `simMain` and `directStep` take a `DesignCert` VALUE, so a
  loaded certificate needs no elaboration at all and changes neither
  `directStep` nor its theorem.

The TSV records the sweep as it ran, at the emitted limit; pass
`--max-rec-depth 20000000` to `direct_sweep.py` to reproduce the run above.

**Nothing was REFUSED**, at any size.  That is the result the phase-0 table was aiming at: a
table derived from `cert_node_expr` and cross-checked against the census
classifies every real certificate that elaborates, and the two rules the census
(not intuition) settled — arity-0 `Op_Or` is real, and async reset polarity
legitimately differs between the two routes — are exactly the two that would
otherwise have produced false refusals on CVA6 and CORE-ET.

---

## 5. Differential against the verified compiler (phase 6)

`lgraph-bench-<Top>` compiles the certificate ONCE with `compileDesign`, then
compares the two implementations cycle by cycle on ordered outputs, next flop
state, and memory sampled at addresses 0,1,2,3,255.  `compileAndRun` is
deliberately not used in the loop: it calls `compileDesign` on every call, so
timing it per cycle would measure the compiler.

```bash
python3 pass/lean/scripts/make_sim_launcher.py --bench <cert>.lean
cd formal/lean && lake build Bench<Top>
.lake/build/bin/lgraph-bench-<Top> 20
```

| design | source | sources | nodes | outputs | flops | mems | differential |
|---|---|---|---|---|---|---|---|
| `alu_gate` | CVA6 | 6,137 | 6,597 | 2 | 0 | 0 | **OK**, 20 cycles |
| `SingleCycleCPU` | DINO | 4,438 | 4,772 | 9 | 33 | 0 | **OK**, 20 cycles |
| `intpipe_csr_msgs` | CORE-ET | 6,510 | 7,447 | 11 | 53 | 1 | **OK**, 20 cycles |
| `minion_dcache_reduce` | CORE-ET | 2,567 | 2,945 | 42 | 64 | **4** | **OK**, 20 cycles |

One combinational design, one sequential CPU, and two memory-bearing designs —
the phase-6 gate, plus the four-memory case because one memory does not exercise
the ordinal indexing.  All four run from the same initial state (`zeroState`)
and the same inputs (`zeroInput`) on both sides.

The DINO CPU is worth showing, because "it executes" is a stronger claim when
the trace means something:

```text
# lgraph-sim SingleCycleCPU sources=4438 nodes=4772 outputs=9 flops=33 mems=0 inputs=7
cycle 0 out 0 0 1 0 1 0 0 0 1
cycle 1 out 0 0 1 0 1 0 0 4 1
cycle 2 out 0 0 1 0 1 0 0 8 1
cycle 3 out 0 0 1 0 1 0 0 12 1
cycle 4 out 0 0 1 0 1 0 0 16 1
```

That is the program counter advancing by four per cycle, out of a certificate
executed directly.

---

## 6. Performance (phase 6)

Reported separately, as the plan requires.  All numbers are native binaries on
the dev machine, `nice -n 19`, from `lgraph-bench-<Top> 20`.

| design | nodes | check | first step | steady state, direct | steady state, compiled | compile |
|---|---|---|---|---|---|---|
| `alu_gate` | 6,597 | 4 ms | 231 ms | 226 ms/cycle | 226 ms/cycle | 1 ms |
| `SingleCycleCPU` | 4,772 | 3 ms | 89 ms | 83 ms/cycle | 83 ms/cycle | <1 ms |
| `intpipe_csr_msgs` | 7,447 | 3 ms | 99 ms | 97 ms/cycle | 95 ms/cycle | <1 ms |
| `minion_dcache_reduce` | 2,945 | 1 ms | 45 ms | 61 ms/cycle | 64 ms/cycle | <1 ms |

Three findings, and the third is the useful one:

1. **Semantic checking is free.**  3–5 ms for a 7,447-node certificate, against
   ~100 ms for one cycle.  Fail-closed costs nothing.
2. **`compileDesign` is also cheap when COMPILED** — about 1 ms for a
   7,447-node design.  Do not read this against the tens of seconds a B1+B2
   per-design gate takes: that figure is dominated by ELABORATING the
   certificate literal and by `native_decide`, not by running the compiler.
   Two different machines, two different measurements.
3. **The direct interpreter and the compiled simulator run at the SAME speed**,
   within 1 % on three of the four designs and 5 % on the fourth — in both
   directions, so it is noise rather than an advantage either way.  Dispatching
   on `LGraphOp` is not the bottleneck; the shared `BV` primitives are.
   `bv_bitwise` evaluates `bits_to_int w` — an O(w) list traversal whose
   `bv_bit` does an O(w) big-int shift — so a bitwise operator is O(w²), and
   CVA6 nodes reach w = 5,772.  That is why `alu_gate` costs 34 µs per node
   against the DINO CPU's 17 µs, at the same order of size.

So the honest performance claim is not "direct interpretation is competitive
with a verified compiler".  It is: *at this value representation the two are
indistinguishable, because neither is paying for dispatch.*  Making either fast
means replacing `BV`, which phase 2 deliberately excluded and which would be the
natural next piece of work.

### Scaling and memory

From `lgraph-sim <top> --cycles N`:

| design | 1 cycle | 10 | 100 | 500 | peak RSS at 500 |
|---|---|---|---|---|---|
| `alu_gate` (no state) | 0.37 s | 2.33 s | 22.4 s | 111.2 s | 11 MB |
| `SingleCycleCPU` (33 flops) | 0.14 s | 1.01 s | 8.3 s | 41.2 s | 9.5 MB |
| `intpipe_csr_msgs` (1 memory) | 0.11 s | 1.34 s | 10.6 s | 53.0 s | **445 MB** |

Time is linear in cycles for all three.  Memory is flat for the two designs
without a mutable memory and grows linearly for the one with: a memory image is
a CLOSURE over the previous cycle's image (`cert_mem_write` returns
`fun x => if x = a then d else m x`), so `N` cycles of writes build an `N`-deep
chain, at ~0.9 MB per cycle here.  That is a property of the function-valued
memory model the certificate semantics uses, not of the trace runner; collapsing
it — a finite-map representation plus an extensionality proof — is representation
work of the same kind as replacing `BV`, and is out of phase 2's scope.

### Scale limit

A synthetic 200,000-node dependency chain (`addChain`, in
`Compiler/DirectExamples.lean` and the bundled binary as `chain-200k`) is
checked and simulated for two cycles in **0.60 s and 37 MB**:

```bash
.lake/build/bin/lgraph-sim chain-200k --cycles 2
```

That is 13× the largest real certificate in the census (`fpu_wrap_gate`, 28,410
nodes) and clears phase 2's "stack-safe on at least 110k nodes" requirement.
Both the evaluator (`Array.foldl`) and the checker (`scanFrom`, a tail recursion
on the count) are loops; the obvious spelling of the checker — `findSome?` over
`slotsFrom 0 n` — conses a 200k-deep list and does not survive.

---

## 7. Trusted computing base (phase 7)

The chain, link by link, with the strongest word each one has earned:

| link | status |
|---|---|
| RTL → post-lowering LGraph | **checked** by LEC (`lhd lec`), per-design.  LEC relates RTL to a graph; it says nothing about how that graph was written out. |
| LGraph → `DesignCert` (C++ `pass_lean.cpp` + `design_cert_export.hpp`) | **TRUSTED**.  Not proved.  The exporter's own remap failures are loud, and the census cross-checks its operator/arity/width output against the Lean table, but neither is a proof. |
| clock-domain assignment and the single-edge lowering | **TRUSTED**, with the boundary moved (§10): the certificate now carries clock provenance, so the checker refuses an undeclared or out-of-range domain, an inconsistent async flag and an edge vector of the wrong width — but whether each element is in the RIGHT domain, and whether `pass.single_edge` lowered the reference domain's latches and negedge state faithfully, are C++ transcriptions validated by the iverilog differentials, not by Lean. |
| async reset polarity between `FlopDesc.resetPin` and `SourceDesc.flopQAsync`'s ordinal | **TRUSTED**, deliberately.  The two routes read the same signal through different logic and legitimately disagree; only the reset value, the width and the pin's existence are cross-checked. |
| `DesignCert` is in the accepted fragment | **CHECKED**, and the check is **PROVED SOUND** (`checkDesign_sound`). |
| accepted `DesignCert` → one cycle of `interpretDesign` | **PROVED** (`directStep_correct`). |
| iterated cycles → the iterated specification | **PROVED** (`runDirect_correct`). |
| agreement with B1+B2 where both accept | **PROVED** (`directStep_eq_compileAndRun`), and separately **TESTED** on three real designs. |
| the operator equations' fidelity to LiveHD | **GROUNDED, not proved.**  §5.1 traces each accepted operator to its exporter site; `eval_op` itself is a Lean definition nobody has proved equal to LiveHD's C++ evaluator.  `eval_op_correct` proves `eval_op = denote_op`, two near-identical bodies, and must not be cited as validation of the equations. |
| Lean's code generator and runtime | **TRUSTED** (as for any extracted-and-run artifact). |
| certificate transport | **N/A today** — certificates are elaborated Lean values.  If a runtime loader is added, the parser joins the TCB until a theorem such as `parseCert (writeCert D) = .ok D` exists.  A theorem about a *parsed* `DesignCert` is not a parser-correctness theorem. |

Three sentences that must not be upgraded:

* a successful differential run on three designs is not a universal proof;
* `directStep_correct` begins at `DesignCert`, not at SystemVerilog;
* "cycle-accurate" is relative to the documented step boundary of §4.4 — one
  step, one edge vector over the declared domains; which domains fire in a step
  is stimulus, and which domain an element belongs to is an exporter
  transcription rather than a checked property.

---

## 8. Reproducing everything

```bash
cd formal/lean
lake build LeanSemanticPrimitives.Compiler.DirectTests   # the execution gate
lake build LgraphSim                                     # the bundled binary
.lake/build/bin/lgraph-sim --list
.lake/build/bin/lgraph-sim chain-200k --cycles 2         # scale limit

cd ..
python3 pass/lean/scripts/direct_sweep.py \
    --out pass/lean/SWEEP_direction2.tsv --jobs 6 --cycles 4 <generated-root>

python3 pass/lean/scripts/make_sim_launcher.py --bench <generated>/lean/<Top>_Lgraph.lean
cd formal/lean
lake build Sim<Top> Bench<Top>
.lake/build/bin/lgraph-sim-<Top>   <Top> --cycles 20 --show-state
.lake/build/bin/lgraph-bench-<Top> 20
```

`lake build Bench<Top>` links against Mathlib's native objects (the verified
compiler needs them) and takes about an hour the first time; `lake build
Sim<Top>` does not, and takes about ninety seconds.

---

## 9. Multi-clock plan, Phase A: memories under the phase divider

The 37 CORE-ET modules with no certificate were classified from the census
(`generated/core-et/coreet_census.tsv`), and 22 of them were refused by
`pass.single_edge`.  Seventeen of those had nothing to do with multiple clocks:

> `memory ... would commit on every sub-step under a phase divider`
> (`pass_single_edge.cpp`, formerly a hard refusal; the hint read *"slot enables
> are not wired into the Memory cell yet"*)

They are single-clock register-file and array blocks that contain a latch or a
negedge flop and so need the P=2 phase divider -- and the pass slotted every flop
(`enable &= (phase == slot)`) but never a `Memory` cell, so it failed closed.
Phase A wires the same gate into the Memory cell.  `DesignCert` is untouched: the
normalised design is single-edge, exactly as before.

### What the lowering does (`pass/single_edge/pass_single_edge.cpp`)

* **Classification (`build_plan`, step 4).**  A clocked memory's committing
  ports are read per port: every write port, plus a sync-read (`type == 1`)
  port whose enable captures its read-data register.  An async read is
  combinational and is neither checked nor gated.  Each committing port's
  `clock_pin` resolves through the same walk a flop's does -- `resolve_icg`
  first, so a port clocked through an ICG cone (`clk & en`) takes the CLOCK's
  commit class and carries its enables; else `control_root`.  All committing
  ports must agree on one (net, edge), and that key must be the reference
  clock.  A memory contributes its slot to P.
* **Rewrite.**  For each committing port, `enable := (phase == slot) & enable
  & <ICG enables>`; a negedge `posclk` is dropped (the slot now carries the
  edge); explicit clocks are rebound to the reference root, as for flops.
* **Rule 4 extended.**  The L1 hazard -- a latch and a same-edge committer
  whose input cone reads the latch's Q -- is checked over a memory's
  addr/data/enable cones too, not only over flop `din`.
* **The enable-latch bypass** is now one helper shared by flops and memories,
  so the parity is decided in one place.

### New named refusals (fail-closed, self-explaining)

| code | when | example |
|---|---|---|
| `memory-latch-array` | a committing port's clock is (or resolves to) a constant: yosys emits a level-sensitive latch-array write as a `$mem_v2` port with `WR_CLK_ENABLE = 0` | `vpu_rf` (`gen_latch.u_rf.rf_q`) |
| `memory-type-unsupported` | `type` is not 0/1/2: the yosys reader writes the per-read-port `RD_CLK_ENABLE` **bitmask** into the cell's one global `type`, so mixed clocked/unclocked reads arrive as e.g. `24 = 0b11000` | `minion_frontend_thread_buffer` (5 read ports, two registered) |
| `memory-mixed-edge` | ports disagree on (net, edge), or the reader's `Memory_posclk_mixed` sentinel; the message names every port's root | -- |
| `off-reference-clock` (memory) | the memory's committing edge is on a net other than the reference clock; the message names both | -- |

The `type` refusal matters beyond diagnostics: treating `24` as "not sync"
would have left two registered read-data registers unslotted -- committing on
every sub-step -- which is exactly the wrong lowering this pass exists never to
emit.  `pass.lean` refuses the same value downstream; refusing it upstream is
what makes the pass correct on its own.

### Validation

* `lhd/tests/single_edge_memory_slot_test.sh` -- a posedge flop, a negedge flop
  (forces P=2) and a clocked memory written from the flop, read
  asynchronously.  The pass fires (`P=2 slots ... 1 memory(ies) slotted`), the
  normalised emission has no `negedge`, and **iverilog** agrees with the real
  negedge/memory source over 39 periods with the memory read included; the P=1
  negative control fails as it must.  Edge normalisation is not
  cycle-preserving, so this independent oracle -- not `lhd lec` -- is the
  binding check, as for the four-classes test.
* The four pre-existing `single_edge_*` tests still pass.
* **The 17 modules, through the real emit pipeline** (`run_coreet_module_lean.sh`
  in `verified_compiler` mode) -- `SWEEP_direction2_phaseA.tsv`:

| outcome | modules |
|---|---|
| **certificate emitted, `checkDesign` ACCEPTED, 4 cycles simulated** | 13: `vpu_tensor{a,b,c,tmp}_rf`, `vpu_lane_tima` (7 gated clocks, 2 memories), `minion_tlb`, `minion_dcache_{128x64,128x72}_1r1w_lram`, `minion_dcache_{buffer,data,metadata,tlb}_array` (up to 8 memories), `minion_dcache_replay_queue` (129 flops) |
| `minion_dcache_top` | normalises -- P=2, 12 latches retyped, **302 gated clocks folded, 21 memories slotted**, 985 elements -- but the `pass.lean` emission of the resulting ~100k-node graph exceeded the pipeline's 90-minute budget (11 GB resident) and was killed. The first time this design has reached the emitter at all: the census recorded it as a `single_edge` refusal. An emitter-scaling question, not a lowering one. An 8-hour relaunch was stopped after ~2 h when the exporter changed under it (Phase B, §10); it has not been re-run, so the budget item stands |
| refused: latch array (new named refusal) | `vpu_rf` |
| refused: mixed read-port clocking encoded as `type=24` | `minion_frontend_thread_buffer` -- normalises (37 gated clocks folded, 2 memories slotted) but the certificate cannot represent it |
| blocked at the front end (at the time) | `minion_frontend`: slang rejects `vpu_defs_pkg.sv:875` (`TXFMA_EXP_FRAC_OFFSET` used before its declaration at `:892`). *Superseded in §10*: the RTL had not changed (core-et's last commit predates the census); slang's `--allow-use-before-declare` compiles it, and it is then refused by `memory-type-unsupported` — the same `type=24` thread buffer as `minion_frontend_thread_buffer` above |

* **Differential against B1+B2** on the newly unblocked, memory-bearing
  `minion_dcache_data_array` (517 nodes, 26 flops, **8 memories**): `diff OK`
  over 20 cycles, direct 13 ms/cycle vs compiled 13 ms/cycle.
  On `vpu_lane_tima` (818 nodes, 13 flops, 2 memories, **7 clock gates folded into
  enables** by the new memory path): `diff OK` over 20 cycles, direct
  6.3 ms/cycle vs compiled 6.1 ms/cycle -- the only D2/B1+B2
  agreement point that exercises an ICG-clocked memory.

### What this does and does not establish

The iverilog fixture validates the LOWERING on the shape it exercises.  On the
real modules the evidence is that the normalised graph is accepted and executed
by two independent implementations of the certificate semantics -- consistency,
not an RTL-level oracle: no cycle-accurate checker can see an edge
normalisation, and the LEC gate in the pipeline compares RTL to the
*normalised* graph only at P=1.  That limit is the same one the pass has always
had for flops.

### A census-label bug found on the way

`scripts/run_vc_sweep.sh` stamped every `single_edge` refusal as
`NO_EMIT(compile)`: its classifier tested the generic `"status":"fail"` JSON
marker before the `single_edge` marker, and every failing `lhd` invocation
prints that marker.  The 2026-08 sweep therefore reported 24 compile failures
where the census shows 8 (`yosys-failed`) plus 22 `single_edge` refusals.  The
specific markers are now tested first.

### Reproducing

```bash
bazel build -c dbg //lhd:lhd
bash lhd/tests/single_edge_memory_slot_test.sh            # or: bazel test //lhd/tests:single_edge_memory_slot_test
COREET_TOP=vpu_tensora_rf LEAN_MODE=verified_compiler RUN_LEAN=false RUN_LEC_GATE=false \
  STOP_AFTER=lean OUT=/tmp/pa/vpu_tensora_rf scripts/run_coreet_module_lean.sh
python3 pass/lean/scripts/direct_sweep.py --out SWEEP.tsv --max-rec-depth 20000000 /tmp/pa
```

---

## 10. Multi-clock plan, Phase B: clock provenance in the certificate

Phase A (§9) left the certificate untouched.  Phase B changes it: `DesignCert`
names its clock domains, every state element carries the ordinal of the one it
commits on, and the step semantics takes an edge vector.  The change is made
IN PLACE — every route theorem is re-derived over the new type — and its
conservativity is a theorem.  Only this worktree is changed; the same type
change is the user's to port to the other branches (see *Porting* below).

### The type and the semantics

```lean
structure ClockDesc where name : String                      -- provenance only
structure FlopDesc   where ... clock : Nat := 0; asyncReset : Bool := false
structure MemoryDesc where ... clock : Nat := 0                -- one per memory
structure DesignCert where ... clocks : Array ClockDesc := #[{ name := "clock" }]

abbrev ClockEdges := Array Bool                                 -- which domains fire this step
def interpretDesign (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState)
```

One step is one batch of `pass/lec`'s microstep schedule.  A flop whose domain
fires commits as before; a flop whose domain is quiet HOLDS — unless its reset is
asynchronous and asserted, in which case it resets regardless (`asyncReset` is
the commit-side twin of `SourceDesc.flopQAsync`; a synchronous reset is sampled
at the edge and must NOT act in a quiet step).  A memory whose domain is quiet
keeps its pre-step image.  The one-clock semantics is kept verbatim as
`interpretDesignLegacy`, and

```lean
theorem interpretDesign_allEdges (D) (hf : ∀ f ∈ D.flops, f.clock < D.clocks.size)
    (hm : ∀ m ∈ D.memories, m.clock < D.clocks.size) (i s) :
    interpretDesign D (allEdges D) i s = interpretDesignLegacy D i s
```

says that adding clocks changed the meaning of no existing certificate: a
certificate that spells no `clocks` is the one-domain design it always was (the
field defaults to one domain, every ordinal to 0).  `interpretDesign_allEdges_of_wf`
discharges the range hypotheses from `DesignSemWF`.

The checker gained what item 9 of the implementation plan reserved for it:
`noClocks`, `flopClockOutOfRange`, `memClockOutOfRange`, `asyncFlagMismatch`
(a `flopQAsync` source whose `FlopDesc` is not marked async) and, per step,
`edgesMismatch` — with `DesignSemWF.clocksDeclared/flopClocks/memClocks` and
`RuntimeSemWF.edgesSized` as their propositional content and `checkDesign_sound`
/ `checkRuntime_sound` extended.  `directStep D e i s`, `Tick := {edges, input}`,
`runDirect D s (ts : List Tick)`, `compiledTrace`, `diffStep`/`diffTrace` and the
in-worktree B1+B2 copies (`ResidualFlopUpdate.clock/asyncReset`,
`ResidualMemoryUpdate.clock`, `flopNext env e ...`, `denoteResidual R e i s`,
`compileAndRun D e inp st`, `compileDesign_correct : ∀ e inp st, ...`) are all
threaded through.  `#print axioms` on `interpretDesign_allEdges`,
`directStep_correct`, `runDirect_correct`, `compileDesign_correct`,
`directStep_eq_compileAndRun`, `runDirect_eq_compiledTrace`, `checkDesign_sound`
and `checkRuntime_sound`: `propext`, `Classical.choice`, `Quot.sound` only.

The simulator reads an optional leading `@<bits>` token per trace line — one
`0`/`1` per declared clock, ordinal 0 first — and fires every clock when the
token is absent; an edge vector of the wrong width is refused.

### The exporter, and what it refuses now

`pass.lean` now emits the clock table and a clock ordinal per element, deriving
identity from `latch_contract`'s resolved root net (the notion `pass.single_edge`
and the LEC clock forest already share): ordinal 0 is the root carrying the most
state, a Pyrope implicit clock joins the unique other root (the LEC's "unique
other root" rule) and is otherwise a domain of its own named `clock`, and the
`async` pin becomes `asyncReset`.  Two things the exporter used to model silently
are refused by name: a flop or memory port still clocked through a GATED-clock
cone (`clk & en`) that `pass.single_edge` has not folded into its enable — this
model has no per-step commit term for a gate — and a falling-edge commit spelled
as an inversion in the clock cone.  A latch-array (constant-clock) write port and
a memory whose committing ports sit on different nets are refused too.  The
per-design theorem is now `∀ edges inp st, X_step edges inp st = interpretDesign
X_designCert edges inp st`.

`pass.single_edge` gained `multi_clock=true`: the reference domain is normalized
exactly as before, a plain posedge flop or memory on ANOTHER root is left on its
own clock (its gated clock still folds into its enable — at P=1 too, which the
Phase-A memory fold had missed) and exported as its own domain, and a latch or
negedge element off the reference clock is refused by name (it would need a
divider of its own).  Off by default: `pass/lec` and `sim` model a second domain
themselves and keep seeing the skip they always saw.  Its multi-clock refusal
now NAMES the nets (`clk_i: 409 element(s) e.g. ... ; <internal>: 14 element(s)
e.g. latch_12796 via sext_45648`), which is what made the next finding possible.

### The five "multi-clock" designs, resolved by name

The census refused five CORE-ET tops as "N clock nets and no known integer
ratio between them".  Four of them were never multi-clock.

| module | census | what it is, and where it stands now |
|---|---|---|
| `intpipe_csr_file` | 2 clock nets | **ONE clock.**  The second "net" was 14 latches gated by `clock_wb & sel`, where `clock_wb = clk_i & en_q` is a `prim_clk_gate` output: a gate of a gate.  `resolve_icg` identified the inner `And` as "the clock" (flops root there) and stopped; it now flattens nested gating, conjoining the enables.  Normalizes: P=2, **1055 latches** retyped, 175 gated clocks folded, 1462 elements, 1 memory slotted, 1 clock domain.  Certificate emission of the resulting graph was still running when this was written (`SWEEP_multiclock6.tsv` carries the outcome) |
| `intpipe_mul_div_top` | 2 clock nets; then a slang error | one clock, the same nested-gate shape (12 latches).  The compile failure — `start_mul_2p` used before its declaration — is a slang strictness, not an RTL change (core-et's last commit predates the census); `--allow-use-before-declare` is now passed by the sweep.  Normalizes: P=2, 529 latches retyped, 29 gated clocks folded.  **Certificate emitted, `checkDesign` ACCEPTED, 4 cycles run** — 9,343 nodes, 536 flops, 95 s |
| `intpipe_top` | 3 clock nets; then a slang error | compiles with the flag; one clock after the fix; refused by the pre-existing L1 rule `coincident-commit-edge`: `latch_27604` and `rf.u_rf.wr_data_del_q` commit on the same edge and the flop reads the latch combinationally.  A named fail-closed refusal about a latch/flop pair, not about clocks |
| `core_top` | 3 clock nets; then a slang error | compiles with the flag; refused `memory-type-unsupported` — `u_frontend.gen_thread_buf[0].u_tb.buffer_pc` has `type=24`, the mixed clocked/unclocked read-port bitmask of §9 |
| `vpu_ctrl` | 2 clock nets; then a slang error | **the one genuinely two-clock design**: 600 flops on `clock_sec`, 185 on `clock_aon` (both top-level ports), 2 negedge flops and 3 latches, all on `clock_sec`.  With `multi_clock=true` the clocks are no longer the reason it stops: it is refused `memory-latch-array` (`tena_rf.u_rf.rf_q`, the same latch-array register file as `vpu_rf` in §9) |
| `minion_frontend` (§9's RTL-blocked module) | slang error | compiles with the flag; refused `memory-type-unsupported` (the same thread buffer) |

So "true multi-clock in `DesignCert`" unblocks no CORE-ET module today: the
only two-clock design is stopped by a latch-array memory, a Memory-cell
representation gap that Phase A named.  What the change buys is honesty of the
model — the certificate now says which edge each element commits on and the
Lean side checks what it can — plus three designs reached by the two LiveHD-side
fixes it forced (the nested-gate recognizer and the slang flag).

### Validation

* **Every Lean `#guard`** in `DirectTests` passes: the five original shapes, the
  refusal tags (four new), and a new section — two free-running counters in two
  domains (counter 1 advances only when `clk_b` fires), a synchronous reset in a
  quiet domain HOLDS while the same reset in a firing domain clears, an
  asynchronous reset in a quiet domain acts and is visible in the same step, a
  write port in a quiet domain does not commit, direct execution agrees with
  `interpretDesign` under partial edge vectors, and the edge vector's width is
  enforced.  `conservative` executes `interpretDesign_allEdges` on every one-clock
  example.  A `mutFlopClock` negative control shows the ordinal is live.
* **Old versus new certificates, trace by trace.**  Every Phase-A certificate
  emitted by the OLD exporter (no clock table) and its re-emission by the new one
  were elaborated side by side under the new semantics and run for 8 zero-stimulus
  cycles with `ticksAll`; outputs, flop state and the first 16 addresses of every
  memory image are compared cycle by cycle.  Result: **equal on all 13**
  (`vpu_tensor{a,b,c,tmp}_rf`, `vpu_lane_tima`, `minion_tlb`,
  `minion_dcache_{128x64,128x72}_1r1w_lram`,
  `minion_dcache_{buffer,data,metadata,tlb}_array`, `minion_dcache_replay_queue`);
  the probe elaborates `generated/core-et/phaseA/lean/X_Lgraph.lean` and
  `generated/core-et/phaseB/lean/X_Lgraph.lean` in two namespaces and compares
  `runDirectRaw` traces.  The old certificates also pass the new `checkDesign`
  unchanged (none of the 13 has an asynchronous reset, so `asyncFlagMismatch`
  does not fire on them; a pre-provenance certificate WITH async flops would be
  refused and must be re-emitted).
* **Re-emission sweep** (`SWEEP_direction2_phaseB.tsv`): the 13 Phase-A modules
  plus `intpipe_mul_div_top` — **14/14 emitted, `checkDesign` ACCEPTED, 4 cycles
  run**, every certificate with a one-entry clock table (`clk_i` or `clock`).
* **`lhd/tests/single_edge_multi_clock_test.sh`** — a posedge and a negedge flop
  on `clk_a`, a posedge flop on `clk_b`, async resets.  Without `multi_clock` the
  pass refuses by name and names both nets; with it, P=2 on the reference and
  `2 clock domain(s) exported`; the normalized emission has no `negedge` and still
  clocks `b` on `posedge clk_b`; the certificate declares `clocks := #[clk_a,
  clk_b]` with `a`, `n` and the divider on ordinal 0, `b` on ordinal 1 and
  `asyncReset := true`; **iverilog** agrees over 39 periods with `clk_b` identical
  on both sides, and the P=1 negative control fails as it must.  The fixture's
  resets are asynchronous on purpose: slang folds a synchronous reset into the
  next-value mux, leaving no `reset_pin` for the divider to copy, and a divider
  with only an `initial` value starts as X under iverilog.
* The nine pre-existing `single_edge_*`, latch-contract, clock-cell and
  LEC-clock-blindness tests still pass with the nested-gate recognizer change.

### What this does and does not establish

The conservativity theorem is about the semantics; the trace comparison is about
the exporter (that emitting a clock table changed nothing else it writes).
Neither says the exporter puts each element in the RIGHT domain: that, and the
faithfulness of the reference-domain lowering, are validated by the iverilog
differentials on fixtures and stay trusted on the real modules — the §7 table
says so.  No real multi-domain certificate exists yet, because the one two-clock
design is stopped upstream; the two-clock fixture's certificate is the only one.

### Porting to the other branches

`DesignCert.lean` was byte-identical across `livehd-new`, `d3`, `d4` and
`futamura`; this worktree's copy now differs.  What a port has to carry:

* `DesignCert.lean`: `ClockDesc`; `FlopDesc.clock : Nat := 0`,
  `FlopDesc.asyncReset : Bool := false`; `MemoryDesc.clock : Nat := 0`;
  `DesignCert.clocks : Array ClockDesc := #[{ name := "clock" }]`.
* `Runtime.lean`: `ClockEdges`, `fires`, `allEdges`, `allEdges_size`,
  `fires_allEdges`.
* `DesignSemantics.lean`: `srcFlopNext rho e s idx f` (edge + async rule),
  `srcMemNext`, `interpretDesign D e i s`, the `Legacy` copies,
  `interpretDesign_allEdges`.
* B1+B2: `ResidualFlopUpdate.clock/asyncReset`, `ResidualMemoryUpdate.clock`,
  `flopNext env e s idx f`, `denoteResidual R e i s` (memories via `mapIdx` with
  the hold), `compileFlop`/`compileMemory` carry the fields, `flopNext_agree`
  takes `e`, `compileDesign_correct`/`compileAndRun`/`compileAndRun_correct`
  quantify over `e`.
* Exporter output: `clocks := #[{ name := ".." }, ..]` after `memories`;
  `, clock := k, asyncReset := b` on every flop line; `, clock := k` on every
  memory line; `def X_step : ClockEdges → RuntimeInput → RuntimeState →
  RuntimeResult`; `theorem X_step_correct : ∀ edges inp st, ...`.
* Futamura's `Projection/DesignEncoding.lean` needs tags for the three new
  fields and the clock table, with round-trip proofs; D4's `DCERT1` parser needs
  the fields; D3's emitted models take the edge vector.

### Reproducing

```bash
bazel build -c dbg //lhd:lhd
bash lhd/tests/single_edge_multi_clock_test.sh            # or: bazel test //lhd/tests:single_edge_multi_clock_test
cd formal/lean && lake build LeanSemanticPrimitives.Compiler.DirectTests LgraphSim
printf '@11 0\n@10 0\n@11 0\n@10 0\n' > /tmp/edges.txt
.lake/build/bin/lgraph-sim two-domain-counters --inputs /tmp/edges.txt --show-state
COREET_TOP=intpipe_mul_div_top LEAN_MODE=verified_compiler RUN_LEAN=false RUN_LEC_GATE=false \
  STOP_AFTER=lean OUT=/tmp/pb/intpipe_mul_div_top scripts/run_coreet_module_lean.sh
python3 pass/lean/scripts/direct_sweep.py --out SWEEP.tsv --max-rec-depth 20000000 /tmp/pb
```
