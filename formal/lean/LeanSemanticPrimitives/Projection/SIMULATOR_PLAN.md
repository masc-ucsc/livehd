# Futamura Projection Plan: a Cycle-Accurate LGraph Simulator

## Goal

The product of this track is a design-specialized, executable one-cycle
simulator in Lean.  Compiler derivation is the method, not the final user-facing
claim.

For a post-lowering hardware design `D`, represented by the same `DesignCert`
used by the other LiveHD tracks, the first projection should compute a residual
object program `P_D`:

```text
P_D = PE(I_hw, D)
```

`P_D` takes only current inputs and state and returns current-cycle outputs and
next state.  Iterating that step function gives the cycle-accurate simulator.
The primary theorem must say that the residual simulator has the same behavior
as the reference semantics:

```text
runProjected(P_D, input, state) = interpretDesign(D, input, state)
```

The theorem is semantic equality (or an explicit state/result relation for
memory-bearing designs), not textual equality of generated programs.

The second projection remains an important follow-on result:

```text
Compiler_I = PE(PE, I_hw)
Compiler_I(D) = P_D
```

It derives the generator of simulators.  It is not required before the first
projected simulator can be used.

## Common simulator contract

Do not introduce a second notion of a hardware cycle in this branch.  Port or
share these definitions from the compiler-semantic branches:

```lean
Compiler.DesignCert
Compiler.RuntimeInput
Compiler.RuntimeState
Compiler.RuntimeResult
Compiler.interpretDesign
```

The external interface of this track should be approximately:

```lean
projectDesign : DesignCert -> Except MixError ProjectedSimulator

runProjected :
  ProjectedSimulator -> RuntimeInput -> RuntimeState ->
  Except SimError RuntimeResult
```

`ProjectedSimulator` should bundle the residual `Projection.Program`, its entry
shape, and enough evidence or a checked bound to execute one cycle without an
arbitrary user-selected fuel value.

The dynamic/static division is:

| Static during specialization | Dynamic on every cycle |
| --- | --- |
| design topology and node order | primary inputs |
| operators, widths, and constants | current flop state |
| dependency and output slots | current memory state |
| flop/reset/enable descriptors | run-time reset/enable values |
| memory port policies | external responses |

Consequently, `P_D` should contain concrete bit-vector operations and
straight-line state logic.  It should not traverse `D.nodes`, look up node IDs,
or dispatch on `LGraphOp` at run time.

## Audited progress on 2026-09-12

| Milestone | Status | What exists | What is still missing |
| --- | --- | --- | --- |
| 0. Shared semantics | **Complete** | pinned shared semantic files; `SimulatorContract.lean`; one-cycle and trace agreement theorems | re-pin only when the shared source changes |
| 1. Hardware encoding | **Complete** | `DesignEncoding.lean` (total over `DesignCert`, memory descriptors included), `RuntimeEncoding.lean`, `StateRel`/`ResultRel` | finite-map memory representation, deferred by design |
| 2. Hardware interpreter | **Executable, not yet proved** | `OperatorBridge.lean` (every bridge `rfl`); `I_hw` as a 17-function object program; one cycle checked against `interpretDesign` on both fixtures | the adequacy THEOREM; `SupportedByProjection`; operators beyond `Op_And` |
| 3. First projected simulator | **Executable, not yet proved, does not scale** | `projectDesign`; both fixtures specialize; residual agrees with `interpretDesign` on every vector; no design tag survives; `Scaling.lean` measures the wall | `projectDesign_correct`; the O(N^2) environment plumbing, which is the blocker for every real design |
| 4. Simulator packaging | **Partial infrastructure** | generic `refTrace`, `stepTrace`, and trace theorem | projected step, total/bounded execution, public runner |
| 5. Cross-simulator relation | **Generic theorem complete** | `StepCorrect`, `step_agree`, and `trace_agree` | correctness instances/adapters for the concrete simulators |
| 6. Literal second projection | **Relative theorem complete** | `secondProjection_correct` and concrete Gate 0 checks | `mixProgram_implements_mixHost` and hardware instantiation |
| 7. Real-design evaluation | **Not started** | tiny shared-semantics fixture only | projected sequential, DINO, CORE-ET, CVA6 runs |

The language and specialization foundation completed before this simulator plan
consists of:

- the first-order deeply embedded language and its executable and relational
  semantics;
- program-as-data encodings with round-trip theorems;
- offline binding-time analysis and the host specializer;
- `mixDriver_iff` (`mix_sound`) in both directions, without fuel in the theorem;
- a toy first projection whose residual removes expression-interpreter
  dispatch;
- the 53-function object `mixProgram` and a relative second-projection theorem;
- concrete self-application checks, including the corrected function-major
  residual-function order and absence of interpreter term-tag dispatch.

Milestone 0 then added the missing hardware semantic boundary:

- `DesignCert`, `RuntimeInput`, `RuntimeState`, `RuntimeResult`, and
  `interpretDesign` are imported verbatim from `livehd-new` revision
  `f82056dbb84174bb4c4fd9fd3c6099efd58e8a23`;
- all eight pinned files match that revision exactly;
- compiler/residual implementation modules were deliberately not imported, so
  the future comparison with the verified compiler is non-circular;
- `StepCorrect` states the common one-cycle obligation;
- `step_agree` derives pairwise one-cycle equality through `interpretDesign`;
- `stepTrace_correct` lifts any `StepCorrect` implementation to traces;
- `trace_agree` derives pairwise trace equality;
- a real shared `DesignCert` fixture executes through `interpretDesign`.

Verification at this audit point:

- all 21 modules in `.build-proj.sh` compile successfully;
- `Audit.lean` checks 51 load-bearing theorems and reports only Lean's standard
  axioms (`propext`, `Quot.sound`, and where required `Classical.choice`);
- no `sorryAx` or `Lean.ofReduceBool` appears in the audit.

The branch still does **not** contain a projected hardware simulator.  The
immediate missing link is an object-language `I_hw` connected to the now-shared
`interpretDesign`, followed by its first projection.

## The scaling wall, measured (2026-09-19)

`Scaling.lean` replaces the analytic argument with data.  The residual is
O(N^2) in GENERATION, not merely at run time: `nthD` is `inline`, so a slot read
at depth `k` unrolls into `k` `let`-bound `tl` steps, and depths sum
quadratically.  `fanD` puts every read at maximum depth and the `tl` count comes
out as exactly `N^2`; `chainD` is the realistic shape at `N^2/2`.

| N (chain) | ms | residual size | `tl` |
| ---: | ---: | ---: | ---: |
| 16 | 15 | 1,004 | 136 |
| 64 | 125 | 8,420 | 2,080 |
| 256 | 1,665 | 107,204 | 32,896 |
| 1024 | 24,332 | 1,608,260 | 524,800 |

Extrapolated: CORE-ET's largest (14,860 nodes) is ~1.4 h and ~3.4e8 term nodes;
CVA6's largest (27,523) is ~4.9 h and ~1.2e9.  At 40 bytes per node that is
13 GB and 46 GB of residual, so neither is a matter of waiting longer.  The
practical ceiling is roughly N = 1000-2000, which puts DINO's 4,772-node
`SingleCycleCPU` already out of reach.  Fuel is not the constraint --
`projectDesign`'s hardcoded `mixDriver 20000 200` still succeeds at N = 256.

A specializer-side rewrite (`hd (consP a b) => a`) cannot fix this: the
`.ucall .dyn` rule `let`-binds every dynamic argument, so the environment
reaches the body as a residual VARIABLE and the `consP` is never syntactically
adjacent.  Post-processing cannot fix it either, because the O(N^2) term would
have to be built first.  The fix is partially-static values in the specializer,
which makes a slot read a single variable reference.

## Current critical path

Prioritize one narrow, real-LGraph vertical slice:

```text
shared memory-free DesignCert/runtime encoding
  -> object I_hw for a small supported LGraph subset
  -> I_hw adequacy for that subset
  -> PE(I_hw, D) for one sequential certificate
  -> projected StepCorrect instance
  -> equality with another simulator via step_agree
```

This path establishes the paper's central simulator claim sooner than either
encoding the entire hardware language up front or completing literal
self-application first.  Extend operator coverage and memories after the first
real sequential residual simulator and equivalence theorem work end to end.

## Ordered implementation plan

### Milestone 0: establish the semantic boundary — COMPLETE

Implemented by `SHARED_SEMANTICS.md` and `SimulatorContract.lean`:

1. The shared certificate, runtime, well-formedness, operator semantics, and
   `interpretDesign` are pinned verbatim.
2. `compileDesign` and its residual implementation remain outside this branch.
3. `StepCorrect`, `step_agree`, `refTrace`, `stepTrace`,
   `stepTrace_correct`, and `trace_agree` are proved.
4. The acceptance fixture passes a shared certificate, input, and state
   unchanged to `interpretDesign`.

Maintenance rule: make semantic changes in the shared source branch and re-pin;
do not locally fork these definitions.

### Milestone 1: encode the hardware domain in the object language — COMPLETE

Implemented by `DesignEncoding.lean` and `RuntimeEncoding.lean`:

1. Reuse the existing generic tagged `Val` and encoded-BV representation.
2. First encode/decode the memory-free subset of `LGraphOp`, sources, dense
   nodes, outputs, flops, and `DesignCert` needed by the initial vertical slice.
3. Encode bit-vector inputs, flop state, outputs, and next flop state.
4. Prove `decode (encode x) = some x` for every supported static certificate
   component.
5. Define `StateRel` and `ResultRel` between object values and compiler runtime
   values.
6. Reuse `SimulatorContract.Acceptance.tinyD`, then add one sequential
   flop/reset/enable certificate; avoid creating a parallel fixture format.

Memory needs an explicit decision.  `RuntimeState.mems` is function-valued and
cannot simply be serialized as the present finite `Val`.  First complete the
theorem for memory-free designs.  Then represent object memories by a default
value plus finite updates (or another executable finite map) and prove a
`MemRel` preserved by reads and writes.  Do not claim literal state equality for
memory-bearing designs unless function extensionality and the representation
bridge have actually been discharged.

Acceptance: both the combinational and sequential shared certificates
round-trip, and their inputs, states, and results satisfy the runtime relations
without an opaque unchecked primitive.  **Met**, with three deviations from what
this milestone expected, all in the direction of more coverage:

- the certificate encoding is **total**, memory included.  Every *static*
  memory component -- `memImg`, `memConst` with its literal table,
  `MemoryDesc`, and all three memory operators -- is ordinary finite data.  The
  hardness is confined to `RuntimeState.mems`, which is function-valued, so the
  memory-free restriction lives in `RuntimeEncoding.lean` and nowhere else.
- the encoders are total and the RELATIONS carry the honesty.  `encState` drops
  `mems`; `decState` can only produce `mems := #[]`, so `StateRel_memFree` shows
  a memory-bearing state satisfies no `StateRel` at all and the dropped
  component cannot be smuggled through.  Stating the relations through the
  decoder also makes them functional (`StateRel_functional`,
  `ResultRel_functional`) -- one object value denotes at most one runtime value,
  which Milestone 5 needs and which an encoder-side definition would not give.
- an obligation for Milestone 2 was found and is recorded in
  `RuntimeEncoding.lean` rather than left to surface as a failing bridge lemma:
  the object's `bvBitAt` masks its operand by the RESULT width while
  `bv_bit` masks by the operand's OWN width.  These agree for bit vectors
  already reduced modulo their own width and diverge otherwise (counterexample
  in the file).  Normalisation is an invariant of the shared semantics, not of
  the `BV` type, so `I_hw`'s operator bridge must carry it explicitly.

### Milestone 2: implement and prove the hardware interpreter `I_hw` — EXECUTABLE, ADEQUACY NOT PROVED

1. Implement the first vertical slice using the already available BV
   constructor/access/bitwise/resize primitives: sources, `Op_And`, outputs,
   and the sequential shell.
2. Give each residual primitive a bridge to the corresponding shared
   `eval_op_cert`/`interpretDesign` operation.
3. Write the hardware interpreter as an object `Program`.  It must walk sources
   and dense nodes, construct outputs, and compute all flop next-state values
   from the old state with reset-before-enable priority.
4. Annotate it with `DesignCert` static and runtime input/state dynamic.
5. Prove interpreter adequacy for the accepted subset:

```text
Eval I_hw [encode D, runtime] result
  <-> ResultRel result (interpretDesign D input state)
```

6. Once the vertical slice is proved and projected, extend `Prim`/`evalPrim` to
   modular arithmetic, signed/unsigned comparisons and division, shifts, muxes,
   sign extension, masks, and later memory operations.  Extend adequacy rather
   than replacing it with a second theorem.

The proof should assume the same checked/well-formed certificate conditions as
the shared semantics plus an explicit `SupportedByProjection D` predicate while
coverage is incomplete.  It must not appeal to the verified residual compiler.

Acceptance: direct execution of `I_hw` performs one hardware cycle on small
certificate fixtures and the adequacy theorem is kernel checked.  **First half
met, second half not.**  `I_hw` computes `encResult (interpretDesign D i s)`
exactly, on the combinational fixture and on all three sequential vectors
(enabled, disabled, reset-beats-enable), compared against the shared semantics
rather than against a hand-written expected value.  The adequacy theorem is not
written, so these are `#guard`s -- the same distinction Gate 0 draws.

Two findings from writing it, both recorded in the file:

- **the slot environment is the whole design problem.**  `interpretDesign`
  carries `rho : Nat -> CertVal`, a function; `L` is first order, so the
  environment must be data.  It is a cons chain, newest-first, read at depth
  `n - 1 - s`; both `n` and `s` come from the certificate, so the walk is static
  and `ucall` unrolls it.  Newest-first is what avoids `append`.
- **what that costs.**  A dep at depth `k` residualizes to `k` `tl`s and one
  `hd`, so an N-slot design gives O(N^2) plumbing and the residual still conses
  its environment at run time.  The dispatch is gone, which is what the first
  projection is for, but this is not yet the straight-line `let` chain the
  legacy fast model emits.  The fix is a specializer-side simplification
  (`hd (consP a b) => a`, `tl (consP a b) => b`), which collapses the chain to a
  single variable reference once the elements are `let`-bound.  That touches
  `PartialEvaluator.lean` and its 1783-line proof, so it is deliberately not
  bundled with getting this correct first.

### Milestone 3: obtain the first projected simulator

Define:

```text
projectDesign(D) = mixDriver(A_I_hw, [encode D])
```

Use the existing `mixDriver_iff` and `I_hw` adequacy theorem to prove:

```lean
theorem projectDesign_correct
    (hp : projectDesign D = .ok P_D) ... :
  Eval P_D dynamicArgs result <->
  ResultRel result (interpretDesign D input state)
```

This is the hardware instance of:

```text
PE(I_hw, D) = Sim_D
```

where equality means equality of observable one-cycle behavior.  Add structural
checks showing that `P_D` contains no certificate traversal, node lookup, or
operator-tag dispatch.  These checks demonstrate successful specialization;
they are not substitutes for the semantic theorem.

Acceptance: at least one sequential certificate produces and executes a
residual one-cycle simulator, with a generic proof connecting it to
`interpretDesign`.  **Execution met, proof not.**  `projectDesign` specializes
both fixtures; each residual is a SINGLE function of two arguments (input and
state -- the design is gone, literally, from the arity), and it reproduces
`interpretDesign` on every vector.

Gate 0 applied to hardware: the only `caseT` tag surviving in either residual is
`tagState`, which destructures the runtime state record and must be there.  No
source tag, no `tagNode`, no `tagOp`, no `tagFlop` -- so no walk over `D.nodes`
and no test on an opcode, where the interpreter dispatches on all twelve.  The
surviving primitives are the hardware ones plus the environment plumbing; no
reflection primitive (`mkCtorP`/`ctorTagP`/`ctorFieldsP`) survives at all.  The
sequential residual keeps exactly two `ite`s -- the reset and enable tests,
which are genuinely runtime conditions -- and the combinational one keeps none.

### Milestone 4: make the residual program a usable simulator — PARTIAL INFRASTRUCTURE

1. Define a checker for the residual fragment produced from `I_hw`: closed
   terms, correct entry arity, acyclic or otherwise terminating call graph, and
   decodable result shape.
2. Compute a conservative per-step evaluation bound, or define a total evaluator
   for that checked residual fragment.
3. Bundle the checked code/bound in `ProjectedSimulator`; hide raw `evalFuel`
   from the public simulator API.
4. Prove that `runProjected = .ok r` implies `r = interpretDesign ...`, and for
   accepted well-formed designs prove that `runProjected` succeeds.
5. Define `stepOf (sim : ProjectedSimulator) : Step SimError` by running the
   already-specialized artifact and ignoring the redundant design argument;
   prove `StepCorrect (stepOf sim) D` from `projectDesign D = .ok sim`.  Do not
   re-run specialization on every simulated cycle.
6. Instantiate the already implemented generic `stepTrace` runner.  Its
   correctness follows immediately from `stepTrace_correct` once the projected
   one-cycle `StepCorrect` instance exists; do not implement or prove a second
   Futamura-specific trace semantics.

Acceptance: users can run one cycle or a trace without selecting fuel, and both
APIs have reference-semantics theorems.

### Milestone 5: relate every simulator through the common semantics — GENERIC THEOREM COMPLETE

The reusable predicate and its one-cycle/trace composition theorems are already
implemented in `SimulatorContract.lean`:

```lean
StepCorrect step D :=
  forall input state result,
    step D input state = .ok result ->
    result = interpretDesign D input state
```

What remains is to instantiate it with existing results:

| Track | Executable step | Existing/common proof boundary |
| --- | --- | --- |
| Futamura | `runProjected` on `P_D` | `projectDesign_correct` + simulator wrapper theorem |
| verified compiler | `compileAndRun` | `compileAndRun_correct` |
| direct IR semantics | `Direct.directStep` | `Direct.directStep_correct` |
| runtime-loaded path | `CertIO.runChecked` | `CertIO.runChecked_correct` |
| legacy generated Lean/Isabelle model | generated `<Top>_step` | per-design certificate bridge plus typed/generic adapters |

Then derive pairwise equivalence by transitivity, rather than re-proving each
pair:

```text
runProjected(P_D, i, s)
  = interpretDesign(D, i, s)
  = compileAndRun(D, i, s)
  = Direct.directStepRaw(D, i, s)
```

For checked APIs, state the pairwise theorem under successful `.ok` results.
For the legacy fixed-width models, use an input/state/output refinement relation
rather than pretending their types are definitionally equal.  Lift the one-step
relations to traces.  Trace comparison tests on identical stimuli remain useful
regression evidence, but the Lean theorem through `interpretDesign` is the
formal relationship.

`step_agree` and `trace_agree` already provide the final composition.  The
acceptance gate is therefore concrete instances: first Futamura plus one other
simulator, then the remaining adapters.  These adapters may live on an
integration branch if importing another implementation here would violate the
non-circular dependency boundary.

Acceptance: instantiate `StepCorrect` for the projected simulator and at least
one existing implementation, then demonstrate `step_agree` and `trace_agree`
on the same real sequential certificate.

### Milestone 6: finish the literal second projection — RELATIVE THEOREM COMPLETE

Already complete: `secondProjection_correct` proves that the derived compiler,
when applied to any design value, computes exactly what the object
`mixProgram` computes on the interpreter and design.  Gate 0 checks its concrete
toy instantiation and verifies that interpreter term-tag dispatch is absent.

Remaining work:

1. Prove `mixProgram_implements_mixHost` relationally and without a fixed fuel
   in the statement.
2. Combine it with `secondProjection_correct` to connect object
   self-application to the already verified host specializer.
3. Decode the compiler's output and prove that applying the derived compiler to
   `D` produces a residual simulator semantically equivalent to direct
   `projectDesign D`.
4. Reuse `projectDesign_correct`; do not duplicate the hardware correctness
   proof for compiler-produced residuals.
5. Check that the derived compiler has no residual dispatch over the encoded
   interpreter syntax, extending the existing Gate 3 beyond the toy example.

Only after these steps does the strong claim
`PE(PE, I_hw) = Compiler_I` connect the object self-application result to the
host specializer and the hardware simulator theorem.

Acceptance: the derived compiler emits a `Sim_D` satisfying exactly the same
one-cycle and trace contract as the direct first projection.

### Milestone 7: real-design and scaling evaluation

Progress in this order:

1. tiny combinational and flop/reset/enable semantic fixtures;
2. a small pipelined/sequential design;
3. the three DINO designs;
4. representative CORE-ET and CVA6 blocks;
5. memory-bearing blocks after `MemRel` is complete.

For each design record specialization time, residual function/term count,
one-cycle and trace run time, and whether interpreter dispatch was eliminated.
Compare behavior against the verified compiler, direct IR simulator, and legacy
model on identical input traces.  Performance parity is desirable, but semantic
parity and a genuinely executable residual are the first acceptance gates.

## Trust and claim boundaries

The projection theorem starts from `DesignCert`.  It does not prove that the C++
exporter faithfully captured the intended post-lowering LGraph; that remains a
validated/LEC boundary unless separately formalized.  Likewise, testing equal
traces does not replace a Lean equivalence theorem.

Claims supported by the current branch:

- a general offline specializer has a fuel-free semantic correctness theorem;
- the toy first projection removes interpreter dispatch and executes correctly;
- a relative second-projection theorem relates a derived compiler to the object
  specializer;
- the hardware tracks now share one pinned `DesignCert` one-cycle semantics;
- any two implementations satisfying `StepCorrect` are generically proved to
  agree for one cycle and for traces.  No Futamura hardware implementation
  satisfies that contract yet;
- a `DesignCert` is object-language data: the encoding round-trips for every
  certificate, and is injective, so a theorem about the specializer's input is a
  theorem about the certificate rather than about a value resembling one.

Claims enabled after completing Milestones 1--5:

- specializing the formal post-lowering IR interpreter produces an executable,
  cycle-accurate simulator for each accepted design;
- the projected simulator is semantically equivalent to the other simulators
  through their shared `DesignCert` semantics.

Claims that must wait for Milestone 6:

- the self-applicable object specializer faithfully implements the host
  specializer;
- `PE(PE, I_hw)` derives the verified simulator compiler end to end.

## Definition of done

This direction is a simulator result, rather than only a partial-evaluation
demonstration, when all of the following hold:

1. `I_hw` has a proved connection to `interpretDesign`.
2. `projectDesign D` produces executable residual code for a real sequential
   `DesignCert`.
3. the public one-cycle runner needs no arbitrary fuel from the user.
4. the residual code's one-cycle result and multi-cycle trace are proved equal
   or related to the reference semantics.
5. the same common-semantics theorem relates it to at least the verified
   compiler and direct IR simulator; legacy and runtime-loaded adapters follow
   the same pattern.
6. the residual no longer interprets design topology or operator tags at run
   time.
7. real-design evaluation reports supported and unsupported constructs
   explicitly.
