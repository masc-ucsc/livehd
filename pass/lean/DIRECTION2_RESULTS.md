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
| single-edge clock normalisation | **TRUSTED**, and *not checkable here*: the certificate carries no clock-model provenance, so no Lean predicate can discover whether the C++ graph was normalised correctly.  Multi-clock graphs must be rejected or normalised before export. |
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
* "cycle-accurate" is relative to the documented single-edge cycle boundary of
  §4.4, which is an exporter precondition rather than a checked property.

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
| `minion_dcache_top` | normalises -- P=2, 12 latches retyped, **302 gated clocks folded, 21 memories slotted**, 985 elements -- but the `pass.lean` emission of the resulting ~100k-node graph exceeded the pipeline's 90-minute budget (11 GB resident) and was killed. The first time this design has reached the emitter at all: the census recorded it as a `single_edge` refusal. An emitter-scaling question, not a lowering one; relaunched with an 8-hour budget, outcome not yet known |
| refused: latch array (new named refusal) | `vpu_rf` |
| refused: mixed read-port clocking encoded as `type=24` | `minion_frontend_thread_buffer` -- normalises (37 gated clocks folded, 2 memories slotted) but the certificate cannot represent it |
| blocked in core-et RTL, not LiveHD | `minion_frontend`: slang rejects `vpu_defs_pkg.sv:875` (`TXFMA_EXP_FRAC_OFFSET` used before its declaration at `:892`); `livehd-new`'s Sep-1 binary fails identically, so it is the RTL, which has changed since the census recorded this module as a `single_edge` refusal |

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

