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

## Audited status on 2026-09-12

Completed in this branch:

- `ObjectLanguage.lean` defines the first-order, deeply embedded language.
- `ObjectLanguageSemantics.lean` provides executable fuelled evaluation and the
  fuel-free `Eval` relation, with soundness and completeness bridges.
- `Encoding.lean` provides program-as-data encodings and round-trip theorems.
- `BTA.lean` and `PartialEvaluator.lean` implement offline specialization.
- `PartialEvaluatorCorrect.lean` proves `mixDriver_iff` (`mix_sound`) in both
  directions, without fuel in the semantic statement.
- `Demo.lean` executes the first projection for a toy expression interpreter;
  the residual program removes interpreter dispatch.
- `MixProgram.lean` contains the 53-function object-language specializer.
- `SecondProjection.lean` proves `secondProjection_correct`: the derived
  compiler, applied to any design value, agrees with running the object
  `mixProgram` on the encoded interpreter and that design value.
- `Gate0.lean` checks concrete self-application, including the corrected
  function-major residual-function order and absence of interpreter term-tag
  dispatch in the toy derived compiler.

Not completed:

- this branch does not yet contain the shared `DesignCert`/runtime/one-cycle
  semantic contract;
- there is no object-language encoding of a hardware `DesignCert`;
- there is no `I_hw` implementing the post-lowering hardware semantics;
- the current bit-vector primitives cover only part of `LGraphOp`;
- no residual program currently consumes `RuntimeInput`/`RuntimeState` or
  returns `RuntimeResult`;
- execution still requires caller-supplied fuel;
- no theorem yet relates a projected hardware simulator to `interpretDesign`;
- no multi-cycle projected trace or real hardware evaluation exists;
- `mixProgram_implements_mixHost` is not proved.  Therefore the current
  second-projection theorem is about the object `mixProgram` on its own terms;
  it does not yet show that object self-application reproduces the Lean host
  specializer.

Thus the branch currently establishes a correct general specializer and toy
first/second-projection evidence, but not yet a hardware simulator.

## Ordered implementation plan

### Milestone 0: establish the semantic boundary

1. Bring the shared `Compiler.DesignCert`, runtime types, and
   `interpretDesign` into this branch without bringing in `compileDesign`.
   The Futamura implementation must remain independent of the verified compiler
   it will later be compared against.
2. Pin the imported definitions to a common revision or move them to a shared
   base branch.  A copied-and-modified `interpretDesign` would make a later
   equivalence theorem much less meaningful.
3. Add a compatibility module containing only the common simulator contract and
   equivalence definitions.

Acceptance: a single `DesignCert`, input, and state value can be passed unchanged
to the reference semantics and to adapters for every executable track.

### Milestone 1: encode the hardware domain in the object language

Add `DesignEncoding.lean` and `RuntimeEncoding.lean`:

1. Encode/decode `LGraphOp`, sources, dense nodes, outputs, flops, memory
   descriptors, and `DesignCert` as tagged `Val` trees.
2. Encode bit-vector inputs, flop state, outputs, and next flop state.
3. Prove `decode (encode x) = some x` for every static certificate component.
4. Define `StateRel` and `ResultRel` between object values and compiler runtime
   values.

Memory needs an explicit decision.  `RuntimeState.mems` is function-valued and
cannot simply be serialized as the present finite `Val`.  First complete the
theorem for memory-free designs.  Then represent object memories by a default
value plus finite updates (or another executable finite map) and prove a
`MemRel` preserved by reads and writes.  Do not claim literal state equality for
memory-bearing designs unless function extensionality and the representation
bridge have actually been discharged.

Acceptance: certificate encodings round-trip, and runtime encoding relations
cover the declared supported fragment without an opaque unchecked primitive.

### Milestone 2: implement and prove the hardware interpreter `I_hw`

1. Extend `Prim`/`evalPrim` with the remaining concrete hardware operations:
   modular arithmetic, signed/unsigned comparisons and division, shifts, muxes,
   sign extension, masks, and later memory operations.
2. Give each residual primitive a bridge to the corresponding shared
   `eval_op_cert`/`interpretDesign` operation.
3. Write the hardware interpreter as an object `Program`.  It must walk sources
   and dense nodes, construct outputs, and compute all flop next-state values
   from the old state with reset-before-enable priority.
4. Annotate it with `DesignCert` static and runtime input/state dynamic.
5. Prove interpreter adequacy:

```text
Eval I_hw [encode D, runtime] result
  <-> ResultRel result (interpretDesign D input state)
```

The proof should assume the same checked/well-formed certificate conditions as
the shared semantics.  It must not appeal to the verified residual compiler.

Acceptance: direct execution of `I_hw` performs one hardware cycle on small
certificate fixtures and the adequacy theorem is kernel checked.

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
`interpretDesign`.

### Milestone 4: make the residual program a usable simulator

1. Define a checker for the residual fragment produced from `I_hw`: closed
   terms, correct entry arity, acyclic or otherwise terminating call graph, and
   decodable result shape.
2. Compute a conservative per-step evaluation bound, or define a total evaluator
   for that checked residual fragment.
3. Bundle the checked code/bound in `ProjectedSimulator`; hide raw `evalFuel`
   from the public simulator API.
4. Prove that `runProjected = .ok r` implies `r = interpretDesign ...`, and for
   accepted well-formed designs prove that `runProjected` succeeds.
5. Define `runProjectedTrace` by threading `nextState` through an input list and
   prove by induction that it equals iteration of `interpretDesign`.

Acceptance: users can run one cycle or a trace without selecting fuel, and both
APIs have reference-semantics theorems.

### Milestone 5: relate every simulator through the common semantics

Define a small reusable predicate, parameterized by an implementation's error
type:

```lean
StepCorrect step D :=
  forall input state result,
    step D input state = .ok result ->
    result = interpretDesign D input state
```

Instantiate it with existing results:

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

Acceptance: one theorem shows that any two successful implementations for the
same certificate produce equal one-cycle results (or related memory results),
and another shows equal/related traces.

### Milestone 6: finish the literal second projection

1. Prove `mixProgram_implements_mixHost` relationally and without a fixed fuel
   in the statement.
2. Combine it with the already proved `secondProjection_correct`.
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
  specializer.

Claims enabled by Milestones 0--5:

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
