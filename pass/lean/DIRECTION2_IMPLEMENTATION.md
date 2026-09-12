# Direction 2 — implementation plan for a standalone direct IR simulator

Companion to `DIRECTION2_IR_SEMANTICS.md`.  That document defines the semantic
scope and claim; this one orders the engineering and proof work.

## Objective and definition of done

Build a native Lean simulator that directly traverses an accepted `DesignCert`
on every cycle and prove that it implements `interpretDesign`:

```lean
directStep : DesignCert -> RuntimeInput -> RuntimeState
    -> Except SimError RuntimeResult

theorem directStep_correct
    (h : directStep D i s = .ok r) :
    r = interpretDesign D i s
```

The implementation is direct only if it consumes `DenseNodeCert` values and
dispatches on `LGraphOp`.  It must not call `compileDesign`, construct a
`ResidualProgram`, or execute `ResidualExpr`.

Completion also requires a trace runner, a trace-level correctness theorem, a
standalone executable, and measurements on real sequential and memory-bearing
designs.

---

## Existing baseline

Already inherited from B1+B2:

- `DesignCert`, including sources, dense nodes, outputs, flops, and memories;
- `RuntimeInput`, `RuntimeState`, and `RuntimeResult`;
- `sourceValue` and source-environment construction;
- `eval_op_cert` and generic `evalGraphG`;
- `interpretDesign`, the one-cycle reference semantics;
- the B1+B2 residual compiler and `compileDesign_correct`, used as an independent
  executable comparison target after Direction 2 is implemented.

Missing for Direction 2:

- fail-closed validation of the whole certificate as a semantic program;
- a linear dense-array evaluator of `DenseNodeCert`;
- a proved direct one-cycle step;
- multi-cycle execution and its proof;
- an executable interface and direct-simulation results.

---

## Architecture

```text
                       DesignCert D
                            |
                      checkDesign
                            |
                   sourceEnvArr D i s
                            |
                  evalDense D.nodes env
                    /                 \
              D.outputs          D.flops/D.memories
                    \                 /
                       RuntimeResult
```

`evalDense` starts with one array entry per source and pushes one result per node.
Dense topological numbering makes every dependency an earlier array lookup.  It
uses `eval_op_cert` directly, so operator dispatch remains visibly the semantics
of LGraph rather than the semantics of the residual language.

The proof relates the final array to the functional environment constructed by
`evalGraphG`; output and next-state agreement then follow slot by slot.

Certificate transport is outside this architecture.  Tests can pass an
elaborated `DesignCert`; Direction 4 can later provide a runtime-loaded one
without changing `directStep` or its theorem.

---

## Phase 0 — freeze and audit the semantic contract

This phase prevents the direct executable from turning existing defensive zero
fallbacks into silently accepted semantics.

1. Write an accepted-operator table from the post-lowering LGraph contract.  For
   every `LGraphOp`, record operand order, arity, result kind, width rule,
   signedness, and corner cases.
2. Reconcile that table with:
   - `eval_op_cert` and its memory cases;
   - the current operator census;
   - the C++ exporter's pin ordering;
   - grounded RTL/LGraph examples.
3. State the cycle boundary: current inputs/state produce current outputs and
   next state for one normalized single-edge step.
4. Record unsupported features explicitly, especially native multi-clock graphs.
5. Audit reset and memory rules: asynchronous visibility, reset priority and
   polarity, write collision order, byte enables, ROM bounds, and synchronous
   read-data state.

**Gate:** a reviewed table in the semantic document, with no accepted operator
reaching the wildcard or wrong-type fallback.

**Done.**  The table is `DIRECTION2_IR_SEMANTICS.md` §5.1, derived from
`cert_node_expr` and checked against a census of all 469 generated certificates
(`pass/lean/scripts/direct_sweep.py` re-runs the check).  24 operators accepted,
5 refused with a stated reason each.  "No accepted operator reaches a fallback"
is the `OpEquations` theorem list in `Compiler/DirectCheck.lean`: one `rfl`
equation per accepted (operator, arity), each naming the primitive the call
reduces to and each false if a fallback were taken.  The census also settled two
rules that guesswork would have got wrong — real arity-0 `Op_Or` nodes exist in
CVA6, and an async flop's descriptor polarity legitimately differs from its
source's.

---

## Phase 1 — fail-closed certificate checking

Add a source-semantic checker rather than using `compileDesign` as the admission
test.  Using the compiler would make Direction 2 depend on the implementation it
is supposed to differ from.

Suggested interfaces:

```lean
checkDesign : DesignCert -> Except SimError Unit
checkRuntime : DesignCert -> RuntimeInput -> RuntimeState -> Except SimError Unit
```

It must check at least:

1. nonzero node and result widths;
2. every dependency is a real, strictly earlier slot;
3. every output, flop pin, and memory image names a real slot;
4. every flop/memory ordinal is in range, repeated input ordinals have consistent
   widths, and the supplied input/state arrays have the required runtime shape;
5. accepted operator arity and output kind;
6. bit-vector versus memory typing for every dependency and shell reference;
7. reset-source and reset-polarity consistency;
8. memory/ROM descriptor consistency;
9. cycle-model provenance, if that metadata is added to the certificate.

Define a propositional `DesignSemWF D` and a Boolean/`Except` implementation,
then prove checker soundness:

```lean
checkDesign D = .ok () -> DesignSemWF D
```

Define a corresponding `RuntimeSemWF D i s` and prove `checkRuntime` sound.  The
current certificate carries no clock metadata, so single-edge normalization
cannot yet be proved by this checker; it remains an exporter precondition and
must be reported as such.

Malformed-certificate tests must cover every error constructor.  In particular,
wrong arity and BV/memory confusion must be refused rather than evaluated as
zero.

**Gate:** checker soundness proved, negative tests pass, and all currently
accepted B1/B2 benchmark certificates are classified with explicit reasons.

**Done.**  `Compiler/DirectCheck.lean`: `checkDesign`, `checkRuntime`,
`DesignSemWF`, `RuntimeSemWF`, and `checkDesign_sound` / `checkRuntime_sound`.
The scan is `scanFrom`, a tail recursion on the COUNT — materialising
`slotsFrom 0 n` as a list first is the obvious spelling and conses to a depth a
200k-node certificate does not survive.  Ten malformed certificates in
`Compiler/DirectExamples.lean` cover the refusal constructors and are asserted
by tag, not merely by failure, in `Compiler/DirectTests.lean`.  Every one of the
147 distinct generated certificates is ACCEPTED — see `SWEEP_direction2.tsv`.

Single-edge normalisation is NOT provable here (no clock provenance in the
certificate) and is reported as an exporter precondition, as the plan requires.
Async reset POLARITY is also not cross-checkable; §5.4 of the semantic document
says why, and the `mutFlopResetPolarity` negative control shows the evaluator is
sensitive to the field that is left unchecked.

---

## Phase 2 — linear direct evaluator

Implement the direct evaluator in a new source-semantic module, for example
`Compiler/DirectSemantics.lean`.

Suggested internal API:

```lean
evalDenseNode : SlotEnv -> DenseNodeCert -> CertVal
evalDense     : Array DenseNodeCert -> SlotEnv -> SlotEnv
directStepRaw : DesignCert -> RuntimeInput -> RuntimeState -> RuntimeResult
directStep    : DesignCert -> RuntimeInput -> RuntimeState
    -> Except SimError RuntimeResult
```

Requirements:

- each cycle is one left-to-right traversal of `D.nodes`;
- slot lookup is array-based;
- the evaluator calls `eval_op_cert` on the node's `LGraphOp` and dependencies;
- outputs and state updates read the resulting slot environment directly;
- iteration is tail-recursive or otherwise stack-safe on at least 110k nodes;
- no residual program or generated semantic expression is constructed;
- the checked entry point refuses before evaluation.

Do not optimize low-level `BV` operations in this phase.  First establish the
correct direct interpreter; representation and primitive speedups are separate.

**Gate:** toy combinational, sequential, asynchronous-reset, ROM, and mutable
memory designs execute for multiple cycles and match `interpretDesign` on small
instances.

**Done.**  `Compiler/DirectSemantics.lean`.  `evalDense` is `Array.foldl`, one
push per node; `evalDenseList` is its proof-facing twin and `evalDense_eq_list`
identifies them.  `directFlopNext` is written independently of `srcFlopNext`
(nested `if`s over explicit reads, not a delegation) so `directFlopNext_agree`
re-checks reset priority, polarity, value and the hold fallback rather than
unfolding to them.  No import of the residual language: `DirectSemantics`
imports `DirectCheck` only, and the whole direct chain is Mathlib-free.

The five shapes are `Compiler/DirectExamples.lean` and the gate is 60-odd
`#guard`s in `Compiler/DirectTests.lean`, which run during `lake build` — a
regression is a build failure.  Stack safety is checked past the requirement: a
synthetic 200,000-node chain (`addChain`) is checked and simulated in 0.6 s and
37 MB.

---

## Phase 3 — one-cycle correctness proof

Prove an environment invariant for every processed node prefix:

```text
the dense array agrees at every existing slot with the functional environment
produced by evalGraphG on the same prefix
```

Use it to prove:

1. source slots agree;
2. each appended node result agrees;
3. final output slots agree;
4. flop updates agree, including reset and enable priority;
5. memory next-image slots agree extensionally.

The public theorem should use successful execution as its only semantic premise:

```lean
theorem directStep_correct
    (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (r : RuntimeResult) (h : directStep D i s = .ok r) :
    r = interpretDesign D i s
```

Also prove:

- determinism;
- output/flop/memory array-size preservation under `DesignSemWF`;
- agreement with `compileAndRun` for certificates accepted by both B1+B2 and
  Direction 2.

The last theorem should be a short composition of `directStep_correct` and
`compileAndRun_correct`, not a new node-by-node proof.

**Gate:** no `sorry`, and `#print axioms` reports only the project's accepted
Lean foundations.

**Done.**  `directStep_correct` is proved in `Compiler/DirectSemantics.lean` by
instantiating `GraphRefine.evalGraphG_of_localAgree` — uniqueness of the topo
fixpoint — exactly as `compileGraph_correct` does, with `compileOp_correct`
replaced by nothing at all, because the dense array holds `eval_op_cert` applied
to the node's own operator.  `directStep_deterministic` and
`directStepRaw_sizes` are the two secondary theorems.  Agreement with B1+B2 is
`directStep_eq_compileAndRun` in `Compiler/DirectVsCompiled.lean`, a two-line
composition as the plan predicted.

`#print axioms Compiler.Direct.directStep_correct` reports
`[propext, Classical.choice, Quot.sound]`; no `sorry` appears in any `Direct*`
module.

---

## Phase 4 — trace semantics

Add a multi-cycle driver that threads `nextState`:

```lean
structure TraceResult where
  steps : List RuntimeResult
  finalState : RuntimeState

runDirect : DesignCert -> RuntimeState -> List RuntimeInput
    -> Except SimError TraceResult
```

Define the reference trace by iterating `interpretDesign` and prove, by induction
on the input list, that every successful direct trace equals the reference trace.

Provide explicit initial-state and stimulus helpers, but keep them outside the
semantic definition.  Designs without complete reset logic must accept an
explicit initial state rather than silently inventing one.

For external I/O, represent a memory by a default value and a finite list of
address/value overrides, then decode it to `Int -> BV`.  Trace output may expose
selected addresses for diagnostics; the theorem compares the internal memory
functions extensionally.

**Gate:** trace theorem proved and a sequential design runs through reset and
multiple non-reset cycles.

**Done.**  `Compiler/DirectTrace.lean`: `refTrace`, `runDirectRaw`,
`runDirectFrom`, `runDirect`, and `runDirect_correct` by induction on the input
list.  `checkDesign` runs ONCE per trace and `checkRuntime` once per cycle; the
state shape never needs re-establishing because `directStepRaw_sizes` proves each
step returns the declared number of flops and memories.  `zeroState` and
`zeroInput` are stimulus helpers and are kept outside the semantic definitions.
The asynchronous-reset design is driven through assert/release/count/re-assert
and the memory design through write/read-back, both as `#guard`s.

---

## Phase 5 — standalone executable

Build a native Lean executable around the generic simulator library.

Required interface:

```text
lgraph-sim DESIGN --inputs TRACE --state INITIAL_STATE --cycles N
```

The first implementation may use a small generated launcher that imports an
elaborated certificate literal and passes it to a generic `simMain D`.  This is
still direct interpretation: the launcher packages data but emits no semantic
code.

If Direction 4 is merged, add runtime `DCERT1` loading as a transport option.
Report the parser as trusted until a theorem such as
`parseCert (writeCert D) = .ok D` is proved.  Direction 2 must remain usable with
an elaborated value so its semantic result does not depend on Direction 4.

The trace format must be deterministic and include cycle number, ordered outputs,
and optional named state observations.  Signal names are metadata for usability;
they are not required by the semantics and must not be added as a prerequisite
to the core evaluator.

**Gate:** a standalone binary executes toy and real certificates and returns a
nonzero status for malformed or unsupported designs.

**Done.**  `Compiler/DirectSim.lean` is the generic library and `Main.lean` the
bundled launcher (`lake build LgraphSim` -> `.lake/build/bin/lgraph-sim`).
`pass/lean/scripts/make_sim_launcher.py` writes a per-design launcher plus its
`lean_exe` stanza; the launcher imports the certificate with `DirectTrace` in
place of `CompileDesign`, so a real design's simulator builds without Mathlib.

Real certificates run natively: `lgraph-sim-SingleCycleCPU` (DINO, 4,772 nodes,
33 flops) shows the PC advancing 0, 4, 8, 12, 16 …; `lgraph-sim-alu_gate` (CVA6,
6,597 nodes); `lgraph-sim-intpipe_csr_msgs` (CORE-ET, 7,447 nodes, 1 memory).
Refusals exit 1 with a rendered reason, usage errors 2, I/O errors 3 — the ten
malformed certificates are in the bundled binary so the behaviour is
demonstrable without a build.  A memory is transported as a default plus finite
overrides (`MemImageExt`) and sampled on output with `--watch-mem IDX:ADDR`,
because `RuntimeState.mems` holds functions.

---

## Phase 6 — real-design validation and evaluation

Use exactly the same inputs and initial states for every compared implementation.

### Correctness-oriented evaluation

1. Direct simulator versus `interpretDesign` on small designs, where the slow
   reference can actually run.
2. Direct simulator versus B1+B2 residual simulator on CORE-ET, CVA6, and DINO.
3. Negative controls: mutate a live operator, dependency, constant, output, flop
   update, and memory update; require a mismatch or checker rejection.
4. Reset and memory traces, not only combinational one-cycle examples.
5. Existing RTL-to-LGraph LEC and certificate audits remain separate gates on the
   trusted extraction boundary.

### Performance evaluation

Report separately:

- certificate elaboration or loading time;
- semantic checking time;
- first-step time and steady-state time per node-cycle;
- peak memory;
- direct interpreter versus residual simulator throughput;
- scaling from toy designs through the largest executable CVA6 certificates.

Do not require the direct interpreter to beat the verified compiler.  Its value
is being a simple executable reference semantics.  Performance is sufficient
when it can run representative real designs and generate useful proof/debugging
traces.

**Gate:** at least one combinational, one sequential CPU, and one memory-bearing
real design execute for meaningful traces; all comparisons are reproducible.

**Done.**  Numbers and commands are in `DIRECTION2_RESULTS.md`; the headline
facts:

* every distinct generated certificate is ACCEPTED by `checkDesign`
  (`SWEEP_direction2.tsv`), none refused — including, once the emitted
  `maxRecDepth` is raised, the 107,213-node twelve-memory
  `cva6_hpdcache_wrapper_gate`;
* the differential against B1+B2 passes on `alu_gate` (CVA6, combinational,
  6,597 nodes), `SingleCycleCPU` (DINO, sequential CPU, 4,772 nodes, 33 flops)
  and `intpipe_csr_msgs` (CORE-ET, 7,447 nodes, one memory) — the three the gate
  asks for — comparing outputs, flop state and sampled memory each cycle;
* the DINO trace is meaningful, not merely non-crashing: the program counter
  advances 0, 4, 8, 12, 16;
* the seven single-field mutants are all detected;
* the direct interpreter and the compiled simulator run within 1–2 % of each
  other, because neither pays for dispatch — the `BV` primitives dominate both.
  That is the measurement that says what to optimise next, and it is not what
  the plan expected.

### Known limitation found by the measurement

A mutable memory's image is a closure over the previous cycle's image, so an
N-cycle trace builds an N-deep chain: peak RSS on the one memory-bearing design
grows to 445 MB over 500 cycles, while the two designs without memories stay
under 11 MB.  This is the function-valued memory model, not the trace runner.
Collapsing it is representation work of the same kind as replacing `BV`.

## Phase 7 — extraction boundary and claim audit

Before publication, enumerate the trusted computing base:

- LGraph-to-`DesignCert` C++ extraction;
- wrapper generation where used;
- certificate parsing when runtime loading is used;
- Lean's code generator/runtime for execution.

State which links are proved, checked, tested, or trusted.  In particular:

- `directStep_correct` begins at `DesignCert`;
- LEC relates RTL to an LGraph but does not prove that the C++ exporter encoded
  that graph correctly;
- a successful differential test is not a universal proof;
- a theorem about a parsed `DesignCert` is not a parser-correctness theorem.

**Gate:** the paper's abstract and evaluation use no stronger wording than the
artifacts justify.

**Done.**  `DIRECTION2_RESULTS.md` §7 enumerates the TCB link by link with the
strongest word each has earned — proved / checked / tested / trusted — and
records the three sentences that must not be upgraded.  `DIRECTION2_IR_SEMANTICS.md`
§10 is the claim wording those artifacts support.

---

## Definition of done

- [x] Audited semantic contract for the accepted post-lowering LGraph subset
- [x] Fail-closed `checkDesign` and proved soundness
- [x] Linear, stack-safe direct evaluator over `DenseNodeCert`
- [x] No call to `compileDesign` or use of `ResidualProgram` in the evaluator
- [x] `directStep_correct` proved without `sorry`
- [x] Multi-cycle trace runner and trace-equivalence theorem
- [x] Standalone native Lean executable
- [x] Sequential, reset, ROM, and mutable-memory coverage
- [x] Reproducible CORE-ET/CVA6/DINO comparison against B1+B2
- [x] Performance and trust-boundary report

## Current status

All seven phases are implemented, and the plan's definition of done is met.
What exists:

| | |
|---|---|
| `Compiler/DirectCheck.lean` | accepted-operator table, `checkDesign` / `checkRuntime`, `DesignSemWF`, soundness, per-operator reduction equations |
| `Compiler/DirectSemantics.lean` | `evalDense`, `directStep`, `directStep_correct` |
| `Compiler/DirectTrace.lean` | `runDirect`, `runDirect_correct` |
| `Compiler/DirectSim.lean`, `Main.lean` | the `lgraph-sim` binary |
| `Compiler/DirectExamples.lean`, `DirectTests.lean` | the execution gate, refusals, negative controls, the 200k-node chain |
| `Compiler/DirectVsCompiled.lean` | agreement with B1+B2 |
| `Compiler/DirectBench.lean` | measurement and differential harness |
| `scripts/make_sim_launcher.py`, `scripts/direct_sweep.py` | per-design launchers, whole-corpus sweep |
| `SWEEP_direction2.tsv`, `DIRECTION2_RESULTS.md` | the results |

What is deliberately NOT done, and why:

* **`BV` is not optimised.**  Phase 2 excluded it, and phase 6 then showed why it
  is the thing to do next: bitwise operators are O(w²) and CVA6 reaches
  w = 5,772, which is the whole of the per-cycle cost for both implementations.
* **The memory closure chain is not collapsed.**  Same category: a
  representation change plus an extensionality proof.
* **Single-edge normalisation is not proved**, and cannot be until the exporter
  emits clock provenance into the certificate.
* **Runtime certificate loading is not implemented**, and the sweep found where
  that costs: the two largest CVA6 certificates (14 MB files) exhaust Lean's
  emitted `maxRecDepth` of 1,000,000 while ELABORATING the `sources := #[…]`
  literal, before `checkDesign` is ever reached.  Raising it to 20,000,000 makes
  `cva6_hpdcache_wrapper_gate` — **107,213 nodes, 515 flops, twelve memories**,
  three times the next largest — ACCEPTED and runnable, at four hours and
  16.6 GB of which the semantic check is 1.8 s and four cycles are 35 s.  So it
  is a transport ceiling, not a semantic one, and the same ceiling B1+B2 faces
  (neither design appears in either B1+B2 sweep).  Two follow-ups, both outside
  this plan: the exporter should emit a larger `maxRecDepth` (a one-line change
  to an emitter B1+B2 shares, so not made here), and Direction 4's loader would
  remove the elaboration cost entirely without touching `directStep` or its
  theorem.

## Separate future application

Once this simulator exists, pre-/post-pass traces can be compared to localize an
optimization bug.  That is simulator-based differential validation and should be
planned and evaluated separately.  It is not a milestone in constructing the
semantic model itself.
