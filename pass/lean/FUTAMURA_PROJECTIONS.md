# Deriving the LiveHD-to-Lean Compiler with Futamura Projections

## 1. Purpose and Scope

This report describes how to apply the first and second Futamura projections to
the LiveHD-to-Lean translation flow.  The goal is to replace the semantic
code-generation core currently handwritten in `pass/lean/pass_lean.cpp` with a
compiler derived from a formal interpreter and a partial evaluator.

The intended result is not that every part of the LiveHD plugin is generated.
LiveHD graph access, pass registration, diagnostics, artifact naming, and file
handling remain ordinary integration code.  The component to derive is the
semantic compiler:

```text
DesignCert -> typed residual Lean hardware model
```

The derived compiler must preserve the current public model interface:

```text
<Top>_in
<Top>_out
<Top>_state
<Top>_comb
<Top>_next
<Top>_step
```

and must support a theorem connecting that fast model to the mathematical
meaning of the exported LiveHD design.

The report covers:

- the meaning of each term in the Futamura projections;
- how those terms map to LiveHD, `DesignCert`, and `pass.lean`;
- the static/dynamic split for combinational and sequential hardware;
- the small object language required by a self-applicable specializer;
- the first projection that derives one design-specific model;
- the second projection that derives a reusable design compiler;
- proof obligations for the interpreter, specializer, compiler, and exporter;
- integration and migration from the current handwritten emitter;
- scalability and hardware-semantic corner cases.

## 2. Current LiveHD-to-Lean Architecture

The current flow is conceptually:

```text
RTL
  |
  | elaboration, lowering, optimization
  v
LiveHD HHDS/LGraph
  |
  | pass.lean: graph traversal + handwritten semantic emission
  v
generated Lean fast model + graph certificate + bridge proofs
```

The reusable Lean support already contains several pieces needed by a
projection-based implementation:

- `formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean`
  defines `LGraphOp`, runtime-width `BV`, `NodeCert`, `GraphCert`, operator
  evaluation, and graph evaluation.
- `formal/lean/LeanSemanticPrimitives/Translation/GraphRefine.lean`
  proves that a dependency-ordered graph evaluator agrees with any environment
  satisfying the same local node recurrence.
- `formal/lean/LeanSemanticPrimitives/Translation/OpBridge.lean`
  relates runtime-width certificate operations to fixed-width Lean `BitVec`
  operations.
- `pass/lean/pass_lean.cpp` currently performs graph extraction, residual code
  generation, certificate generation, and per-design bridge generation.

The current proof strategy is:

```text
generated fixed-width fast model
  = evaluated graph certificate
  = mathematical graph-certificate semantics
```

This strategy remains useful.  The projection-based implementation changes how
the first equality is obtained: instead of manually coding every residualization
decision in C++, a verified specializer derives the fast model from the
interpreter.

## 3. Futamura Projection Terminology

### 3.1 Interpreter

An interpreter is a program that takes a source program and its runtime data:

```text
I(P, x) = result
```

For this project:

```text
I = formal hardware-design interpreter
P = one LiveHD design represented by DesignCert
x = current primary inputs and architectural/microarchitectural state
```

The result is:

```text
(next state, current-cycle outputs)
```

The complete type is approximately:

```lean
interpretDesign :
  DesignCert -> DesignInputs -> DesignState -> Prod DesignState DesignOutputs
```

Because input and state types vary by design, the foundational interpreter may
use schema-indexed or generic environments.  Named `<Top>_in`, `<Top>_state`, and
`<Top>_out` structures are produced by a later reification layer.

### 3.2 Source Program

In ordinary language interpretation, the source program might be a parser AST.
Here the source program is a hardware graph.  A graph is executable structure:

```text
node 42 = Add(node 7, node 9), width 64
node 43 = Mux(node 3, node 11, node 42), width 64
output result = node 43
flop pc.din = node 91
```

`DesignCert` is the inspectable representation of this source program inside
Lean and inside the small partial-evaluation language.

### 3.3 Dynamic Data

Dynamic data changes from one execution or clock cycle to another:

- primary input values;
- current flop and register contents;
- current memory contents;
- external memory responses;
- interrupts and other environmental inputs;
- reset and enable values when they are input signals;
- any nondeterministic environment value explicitly represented by the model.

Internal combinational wires are not independent dynamic arguments.  They are
computed values derived from primary inputs, current state, constants, and other
internal nodes.

### 3.4 Static Data

Static data describes one compiled design:

- graph topology and topological order;
- node operators;
- node and port widths;
- dependencies and operand order;
- constants;
- input, output, and state schemas;
- output-driver node IDs;
- flop `din`, reset, and enable drivers;
- reset polarity, priority, and reset values;
- memory dimensions, ports, enables, masks, and collision policies;
- source IDs and the mapping from sources to input/state selectors.

These values are fixed when compiling one hardware design, so specialization can
remove interpretation overhead associated with them.

### 3.5 Partial Evaluator / Specializer (`mix`)

A partial evaluator takes program code and known static arguments and returns a
residual program:

```text
mix(program, static arguments) = residual program
```

Its defining correctness property is:

```text
run(mix(P, s), d) = run(P, (s, d))
```

where `s` is static and `d` is dynamic.

For one LiveHD design:

```text
mix(hardware interpreter, DesignCert)
  = design-specific residual hardware model
```

### 3.6 Residual Program

The residual program is the result of fixing the design while leaving runtime
inputs and current state unknown.  It is the generated fast model:

```text
<Top>_comb : <Top>_in -> <Top>_state -> <Top>_out
<Top>_next : <Top>_in -> <Top>_state -> <Top>_state
<Top>_step : <Top>_in -> <Top>_state -> Prod <Top>_state <Top>_out
```

It should contain fixed-width, let-bound operations instead of graph traversal:

```lean
let n7  : BitVec 64 := ...
let n9  : BitVec 64 := ...
let n42 : BitVec 64 := n7 + n9
let n43 : BitVec 64 := if bitvec_nonzero n3 then n42 else n11
```

### 3.7 Compiler

The compiler produced by the second projection accepts any supported
`DesignCert` and returns the corresponding residual program:

```text
derivedCompiler : DesignCert -> ResidualProgram
```

The current `pass_lean.cpp` is analogous to this compiler, but it is handwritten.
It is not currently the result of self-applying a general `mix`.

## 4. The Three Projection Equations

Let:

```text
I   = object-language implementation of interpretDesign
D   = one concrete DesignCert
x   = runtime inputs and state
M   = object-language implementation of mix
```

### 4.1 First Projection: Compile One Design

```text
R_D = mix(I, D)
```

Correctness gives:

```text
run(R_D, x) = run(I, (D, x))
```

In project terms:

```text
generated <Top>_step i s
  = interpretDesign <Top>_DesignCert i s
```

### 4.2 Second Projection: Derive the Compiler

The specializer must itself be represented as object-language program data:

```text
C = mix(M, I)
```

`M` is the source code of the specializer, not merely the host Lean function
used to execute specialization.  Applying the resulting compiler gives:

```text
run(C, D) = mix(I, D) = R_D
```

Combining both stages:

```text
run(run(C, D), x) = run(I, (D, x))
```

The outer `run(C, D)` returns code, so the small language must represent programs
as values.

### 4.3 Third Projection: Derive a Compiler Generator

The third projection is:

```text
compilerGenerator = mix(M, M)
```

Applied to an interpreter, it produces a compiler.  This is not required to
replace `pass.lean`.  The first and second projections are sufficient.

## 5. Correct Separation of Extractor, Interpreter, and Specializer

These three components must not be conflated.

### 5.1 LiveHD Semantic Exporter

The exporter reads the current LiveHD HHDS/graph API and creates canonical
`DesignCert` data:

```text
extractDesign : HHDS Graph -> DesignCert
```

It is responsible for understanding:

- graph input and output representation;
- node and pin identity;
- operator and operand-port ordering;
- width and signedness metadata;
- flop, reset, enable, and clock ports;
- memory node policies and port layout;
- which nodes are reachable from observable outputs and next-state drivers.

### 5.2 Design Interpreter

The interpreter does not rediscover LiveHD conventions.  It executes the meaning
already recorded in `DesignCert`:

```text
interpretDesign(D, inputs, state)
```

### 5.3 Specializer

The specializer takes code for the interpreter and a static `DesignCert`.  It
evaluates graph traversal, operator dispatch, width decisions, and metadata
lookups during specialization, then residualizes computations that depend on
runtime input or state.

```text
specialize(interpreter code, static DesignCert)
  -> residual input/state function
```

The specializer does not call LiveHD APIs.  It operates only on object-language
programs and ordinary `DesignCert` values.

## 6. `DesignCert` as LiveHD's Canonical Semantic Output

### 6.1 Why Not Pass an In-Memory C++ Graph Directly?

A Lean function cannot directly inspect a C++ `hhds::Graph` containing C++
objects, pointers, internal handles, and implementation-specific metadata.  A
stable serialized boundary is required.

Using `DesignCert` provides:

- deterministic, immutable input;
- explicit semantics-bearing fields;
- versioning and schema validation;
- portability across C++ and Lean;
- a value that can be inspected by `mix`;
- a value that can be hashed and tied to LEC artifacts;
- a compact proof object rather than a complete compiler database.

### 6.2 Proposed Schema

The exact schema should evolve with supported hardware features, but it should
contain structures equivalent to:

```lean
inductive ValueType
  | bv  (width : Nat)
  | mem (addrWidth dataWidth : Nat)

structure SourceDesc where
  sid      : Nat
  valueTy  : ValueType
  kind     : SourceKind
  selector : Nat

structure NodeDesc where
  nid        : Nat
  op         : LGraphOp
  resultTy   : ValueType
  deps       : List Nat
  operandTys : List ValueType

structure OutputDesc where
  name   : String
  width  : Nat
  driver : Nat

structure FlopDesc where
  name          : String
  width         : Nat
  currentSource : Nat
  dinDriver     : Nat
  resetDriver   : Option Nat
  resetValue    : Int
  resetPolarity : Polarity
  enableDriver  : Option Nat
  resetPriority : ResetPriority

structure MemoryDesc where
  name           : String
  addrWidth      : Nat
  dataWidth      : Nat
  depth          : Nat
  readPorts      : List ReadPortDesc
  writePorts     : List WritePortDesc
  readLatency    : ReadLatency
  collision      : CollisionPolicy
  writeOrdering  : WriteOrdering
  byteWidth      : Option Nat

structure DesignCert where
  schemaVersion : Nat
  producerId    : String
  graphDigest   : String
  sources       : List SourceDesc
  nodes         : List NodeDesc
  topo          : List Nat
  inputs        : List PortDesc
  outputs       : List OutputDesc
  flops         : List FlopDesc
  memories      : List MemoryDesc
```

### 6.3 Source of Truth for Export Rules

The exporter should follow current LiveHD behavior, but prose documentation is
not enough by itself.  Each mapping should be checked against:

- official LiveHD graph/HHDS API documentation;
- current `Ntype` definitions and sink/output port naming;
- `graph_util` width, constant, and node-classification functions;
- LiveHD's simulation and Verilog-generation implementations;
- small operator and sequential regression tests;
- RTL-to-LGraph LEC using the exact graph associated with the certificate.

The same canonical artifact should be consumed by Lean compilation and associated
with the LEC result.  `schemaVersion`, producer revision, and `graphDigest` should
prevent silently combining artifacts from different graph builds.

## 7. Formal Hardware Interpreter

### 7.1 Graph Evaluation

The interpreter starts with a source environment containing primary inputs,
flop outputs, constants, and memory images.  It evaluates nodes in topological
order:

```lean
def interpretNodes
    (D : DesignCert)
    (sourceEnv : Nat -> CertVal) : Nat -> CertVal :=
  D.topo.foldl
    (fun rho nid =>
      match D.nodeById nid with
      | none   => rho
      | some n => envSet rho nid
          (interpretOp n.op n.resultTy (n.deps.map rho)))
    sourceEnv
```

The production definition should use a representation with scalable lookup and
an explicit well-formedness condition.  The pseudocode shows the semantics only.

### 7.2 Current-Cycle Outputs

Outputs are computed from the evaluated old-state graph:

```lean
outputsOfCert D rho
```

For synchronous RTL timing:

```text
output_t = comb(input_t, state_t)
```

not `comb(input_t, state_(t+1))`.

### 7.3 Simultaneous Next-State Update

Every flop's next value is computed from the same old state and current input:

```lean
def interpretFlop (f : FlopDesc) (rho : Nat -> CertVal) (old : BV) : BV :=
  if resetActive f rho then
    mk_bv f.width f.resetValue
  else if enableActive f rho then
    (rho f.dinDriver).asBV
  else
    old
```

All results are collected before constructing the new state.  This preserves
clocked simultaneous-update semantics and prevents one generated field update
from observing another field's new value.

### 7.4 Complete Step

```lean
def interpretDesign D input oldState :=
  let source := makeSourceEnv D input oldState
  let rho    := interpretNodes D source
  let out    := outputsOfCert D rho
  let next   := nextStateOfCert D rho oldState
  (next, out)
```

This is the interpreter to specialize.

## 8. What Specialization Eliminates

Given a fixed `D`, `mix` can compute all design-structural work:

| Interpreter operation | Residual result |
|---|---|
| Iterate `D.topo` | Straight-line let bindings |
| Look up `D.nodes[nid]` | Selected concrete node |
| Match `node.op` | Selected fixed operator expression |
| Inspect `node.width` | Literal `BitVec w` type |
| Map dependency IDs through `rho` | References to prior let bindings or sources |
| Look up output drivers | Concrete output-record fields |
| Look up flop drivers | Concrete next-state record fields |
| Inspect reset/enable policies | Concrete nested conditionals in correct priority |
| Inspect memory policy | Specialized read/write/update expression |

The following remain dynamic:

- actual input bits;
- current flop values;
- current memory contents;
- run-time reset and enable signal values;
- arithmetic, logical, compare, mask, and memory operations over those values.

## 9. Why a Small Object Language Is Required

### 9.1 Programs Must Be Data

An ordinary Lean function can be applied, but an ordinary theorem-level function
cannot generally pattern-match on the syntax of an arbitrary Lean definition.
Lean metaprograms can inspect `Lean.Expr`, but that moves the transformation into
the metaprogramming layer and requires a separate correctness treatment.

A literal second projection needs a deep embedding: programs are represented by
an inductive datatype that `mix` can inspect and construct.

### 9.2 Recommended First-Order Language

Start with a first-order language rather than unrestricted lambda calculus:

```lean
inductive Prim
  | add | sub | mul | udiv | sdiv
  | bitAnd | bitOr | bitXor | bitNot
  | eq | ult | ugt | slt | sgt
  | shl | sra | mux | sext | getMask | setMask
  | listCons | listHead | listTail
  | programCtor | termCtor | inspectTerm

inductive Term
  | lit    : Val -> Term
  | var    : Nat -> Term
  | letIn  : Term -> Term -> Term
  | ite    : Term -> Term -> Term -> Term
  | prim   : Prim -> List Term -> Term
  | ctor   : Nat -> List Term -> Term
  | case   : Term -> List Term -> Term
  | call   : Nat -> List Term -> Term

structure FunDef where
  arity : Nat
  body  : Term

structure Program where
  functions : Array FunDef
  entry     : Nat
```

Use de Bruijn indices or another binding representation with a proved
substitution discipline.  Named recursive functions represented by `call` are
simpler than a general `fix`, `lam`, and `app` in the first implementation.

### 9.3 Code Values

The second projection requires the specializer to consume and produce programs,
so object-language values must include syntax values:

```lean
inductive Val
  | nat     : Nat -> Val
  | int     : Int -> Val
  | bool    : Bool -> Val
  | list    : List Val -> Val
  | term    : Term -> Val
  | program : Program -> Val
  | design  : DesignCert -> Val
  | residual : ResidualProgram -> Val
```

Alternatively, all syntax can be represented with ordinary tagged constructors
inside the object language.  The representation must support inspection and
construction by `mixProgram`.

## 10. Language Semantics and the Fuelled Evaluator

### 10.1 Executable Evaluator

Recursive object programs may diverge.  Lean requires executable recursive
definitions to terminate, so an implementation can use fuel:

```lean
evalFuel : Nat -> Program -> Env -> Term -> Option Val
```

Each recursive evaluator call consumes fuel.  `none` may mean fuel exhaustion or
an execution error, so a production implementation should distinguish errors:

```lean
inductive EvalResult
  | value       : Val -> EvalResult
  | outOfFuel
  | typeError   : String -> EvalResult
  | missingName : Nat -> EvalResult
```

Fuel guarantees that the evaluator terminates.  It does not prove that the
object program terminates.

### 10.2 Mathematical Semantics

Fuel should not be the primary denotation because arbitrary fuel bounds would
pollute every projection theorem.  Define an inductive evaluation relation:

```lean
inductive Eval (P : Program) : Env -> Term -> Val -> Prop
```

Then prove:

```lean
theorem evalFuel_sound:
  evalFuel fuel P env t = .value v -> Eval P env t v

theorem evalFuel_complete_for_terminating_run:
  Eval P env t v -> exists fuel, evalFuel fuel P env t = .value v
```

The graph interpreter is total on a finite, well-formed, dependency-ordered
`DesignCert`.  General object programs may diverge.

## 11. Implementing `mix`

### 11.1 Partial Values

The specializer tracks whether each value is known now or must be computed later:

```lean
inductive PartialVal
  | static   : Val -> PartialVal
  | residual : Term -> PartialVal
```

For typed residual output, use a typed equivalent carrying `ValueType` and a
typed target expression.

### 11.2 Core Specialization Rules

Representative rules are:

```text
literal:
  specialize(lit v) = static v

static variable:
  specialize(var x) = static environment[x]

dynamic variable:
  specialize(var x) = residual(var x)

primitive with all-static operands:
  specialize(prim op args) = static(evalPrim op staticArgs)

primitive with any dynamic operand:
  specialize(prim op args) = residual(prim op residualizedArgs)

conditional with static condition:
  specialize(ite c t e) = specialize(selected branch)

conditional with dynamic condition:
  specialize(ite c t e) = residual(ite c' t' e')

static let:
  extend static environment and specialize body

dynamic let:
  emit residual let to preserve sharing
```

### 11.3 Recursion and Termination

A self-applicable specializer needs:

- memoization of specialized function calls by function and static arguments;
- residual recursive calls when specialization cannot safely unfold;
- generalization/widening when static arguments produce an unbounded sequence of
  specialization variants;
- a termination policy for online specialization, or an offline binding-time
  analysis that guarantees specialization termination;
- let insertion so duplicated dynamic computations do not cause code explosion.

The LGraph interpreter is favorable for specialization because traversal follows
a finite topological list.  The difficult recursion is primarily in `mix`
itself and in generic list/map utilities used by `mix`.

### 11.4 Offline Versus Online Specialization

An online specializer decides static versus dynamic while specializing.  An
offline specializer first performs binding-time analysis and annotates terms.

For literal self-application, an offline design is easier to control:

```text
source Program
  -> binding-time analysis
  -> annotated Program
  -> specialization
  -> residual Program
```

The binding-time division for the hardware interpreter is clear: `DesignCert`
and all data derived solely from it are static; input/state values and operations
depending on them are dynamic.

## 12. Typed Residual Hardware IR

The language in which `mix` is implemented can be dynamically represented for
bootstrapping, but the final hardware program should be typed by width and value
kind.

```lean
inductive VType
  | bv  : Nat -> VType
  | mem : Nat -> Nat -> VType

def DenoteVType : VType -> Type
  | .bv w      => BitVec w
  | .mem aw dw => BitVec aw -> BitVec dw

inductive HExpr : VType -> Type
  | input  : InputRef (.bv w) -> HExpr (.bv w)
  | state  : StateRef ty -> HExpr ty
  | const  : (w : Nat) -> Int -> HExpr (.bv w)
  | add    : HExpr (.bv w) -> HExpr (.bv w) -> HExpr (.bv w)
  | resize : HExpr (.bv a) -> HExpr (.bv b)
  | mux    : HExpr (.bv 1) -> HExpr ty -> HExpr ty -> HExpr ty
  | memRead  : HExpr (.mem aw dw) -> HExpr (.bv aw) -> HExpr (.bv dw)
  | memWrite : HExpr (.mem aw dw) -> HExpr (.bv aw) ->
      HExpr (.bv dw) -> HExpr (.mem aw dw)
```

Because a graph node's width is known during specialization but differs between
nodes, the compiler environment stores existentially packaged expressions:

```lean
structure SomeHExpr where
  ty   : VType
  expr : HExpr ty
```

The residual program should preserve DAG sharing explicitly:

```lean
structure Binding where
  id   : Nat
  rhs  : SomeHExpr

structure ResidualProgram where
  bindings : List Binding
  outputs  : List ResidualOutput
  next     : List ResidualStateUpdate
```

This avoids deeply nested expression trees and keeps generated Lean elaboration
linear in graph size.

## 13. First Projection in Detail

Given a concrete design `D`:

```lean
def residualFor (D : DesignCert) : Except MixError ResidualProgram :=
  specialize interpreterProgram (Val.design D)
```

During specialization:

1. `D.topo` is static, so list traversal is unrolled.
2. Every node lookup is resolved.
3. Every operator match selects one interpreter branch.
4. Width checks produce literal type indices.
5. Static constants become typed literals modulo their declared width.
6. Source nodes become input or old-state projections.
7. Computed nodes become residual let bindings.
8. Output lookups become output structure construction.
9. Flop policies become concrete reset/enable conditionals.
10. All next-state RHS expressions continue to reference old state.
11. Memory descriptors select concrete read/write/collision semantics.

The first-projection correctness theorem is:

```lean
theorem residualFor_correct
    (hD : DesignCert.WellFormed D)
    (h : residualFor D = .ok R) :
  forall input state,
    denoteResidual R input state = interpretDesign D input state
```

This theorem is generic.  A new DINO or CVA6 design requires only a new
`DesignCert` and a successful well-formedness check, not a new semantic proof.

## 14. Second Projection in Detail

### 14.1 Host Specializer and Object Specializer

Initially, `mixHost` is an ordinary Lean implementation:

```lean
mixHost : Program -> StaticInput -> Except MixError Program
```

For self-application, the same algorithm must be represented inside the small
language:

```lean
mixProgram : Program
```

The key bootstrapping theorem is:

```lean
theorem mixProgram_implements_mixHost:
  objectRun mixProgram [Val.program P, encodeStatic s]
    = .value (Val.program R)
  <->
  mixHost P s = .ok R
```

The precise theorem may be relational to avoid fixed fuel.

### 14.2 Interpreter as Object Program

The formal design interpreter must also be represented in the small language:

```lean
interpreterProgram : Program
```

and connected to the direct Lean semantics:

```lean
theorem interpreterProgram_correct:
  objectRun interpreterProgram [encode D, encode input, encode state]
    = encode (interpretDesign D input state)
```

### 14.3 Deriving the Compiler

Now specialize the object-language specializer with respect to the fixed
interpreter:

```lean
def compilerProgram : Program :=
  mixHost mixProgram (encodeStatic interpreterProgram)
```

This is the second projection:

```text
compilerProgram = mix(mixProgram, interpreterProgram)
```

The resulting compiler leaves the design argument dynamic at compiler-generation
time.  When run later on a concrete `D`, it produces the same residual program as
the first projection:

```lean
theorem secondProjection_correct:
  runCompiler compilerProgram D = residualFor D
```

Combining with first-projection correctness:

```lean
theorem derivedCompiler_end_to_end:
  runCompiler compilerProgram D = .ok R ->
  forall input state,
    denoteResidual R input state = interpretDesign D input state
```

### 14.4 What Is and Is Not Automatically Derived

The second projection derives:

```text
DesignCert -> ResidualProgram
```

It does not derive:

- LiveHD pass registration;
- access to `hhds::Graph`;
- `DesignCert` serialization;
- command-line parsing;
- output filenames and atomic rename handling;
- LEC orchestration;
- Lean process invocation.

Those remain a thin wrapper around the derived semantic compiler.

## 15. Verifying the Derived Compiler

Verification is a chain, not one theorem.

### 15.1 DesignCert Well-Formedness

Required properties include:

- unique node and source IDs;
- no node/source ID collision;
- every topological node exists;
- every internal dependency precedes its user;
- every dependency names a source or node;
- positive and bounded widths where required;
- operator arity and operand-type compatibility;
- output and flop drivers exist and have compatible types;
- reset/enable signals have valid Boolean interpretation;
- memory dimensions and policies are supported and internally consistent.

Use a computable checker and prove its soundness once:

```lean
DesignCert.wfBool D = true -> DesignCert.WellFormed D
```

Each generated design then discharges only the concrete Boolean check.

### 15.2 Operator Semantics

Each operator needs an independent semantic specification and a theorem relating
the typed residual operation to it.  Required cases include:

- modular arithmetic at exact output width;
- signed and unsigned comparisons at operand width, not result width;
- unsigned and signed division, including divide-by-zero policy;
- logical versus arithmetic right shift;
- exact shift-amount truncation/extension policy;
- mux polarity and n-way mux out-of-range behavior;
- sign and zero extension;
- arbitrary-width constants, including negative values and all-ones masks;
- `Get_mask` dense low-bit packing order;
- `Set_mask` selected-bit replacement and out-of-range behavior;
- zero-width artifact rejection;
- memory read/write enables and byte masks.

The existing `OpBridge.lean` library is reusable here.

### 15.3 Interpreter Correctness

The graph interpreter proof establishes that topological evaluation implements
the local node denotation and that the sequential shell uses old-state,
simultaneous-update semantics.

The current `eval_op` and `denote_op` definitions are intentionally similar.
Equality between duplicated definitions is structurally useful but is not an
independent validation of LiveHD semantics.  Operator semantics must ultimately
be tied to LiveHD through source review, regression oracles, and RTL/LGraph LEC.

### 15.4 Specializer Correctness

The central theorem is:

```lean
theorem mix_sound:
  mixHost P static = .ok residual ->
  forall dynamic result,
    Eval residual dynamic result <->
    Eval P (combine static dynamic) result
```

The proof proceeds by induction over annotated object-language terms, with
separate arguments for memoized recursive specialization.

### 15.5 Self-Representation Correctness

Literal second projection additionally requires:

```text
mixProgram implements mixHost
```

Without this theorem, `mixProgram` is just a second handwritten implementation,
and self-application does not establish a trustworthy compiler derivation.

### 15.6 Residual Reifier Correctness

`ResidualProgram` must be emitted as Lean declarations.  There are two choices:

1. Define a Lean metaprogram that reifies residual IR into `Lean.Syntax` or
   `Lean.Expr`, and have generated top-level equality theorems checked by the
   kernel.
2. Keep residual IR as the official model and expose named generated structures
   as an ergonomic view proved equal to its denotation.

The second is easier to verify compositionally.  In either case, generated
artifacts must be audited for missing declarations and `sorryAx`.

### 15.7 Export Boundary

The projection theorems begin at `DesignCert`; they do not prove the C++
HHDS-to-certificate export.  That boundary needs:

- schema validation;
- extraction unit tests for every supported operator and sequential primitive;
- direct comparison of exported IDs, widths, dependencies, and policies with the
  source graph;
- artifact hashing/versioning;
- LEC relating the exact source graph to RTL;
- ideally, one canonical LiveHD semantic export used by all downstream tools.

## 16. End-to-End Correctness Statement

The desired chain is:

```text
RTL
  = [LEC]
LiveHD graph
  = [validated canonical export]
DesignCert
  = [interpreter definition]
interpretDesign DesignCert
  = [mix soundness / first projection]
residual hardware program
  = [reifier bridge]
generated Lean <Top>_comb/<Top>_next/<Top>_step
```

A Lean theorem can cover the last three links:

```lean
theorem generated_step_correct
    (hD : DesignCert.WellFormed D)
    (hc : runCompiler compilerProgram D = .ok R)
    (hr : reifyResidual R = generatedTop) :
  forall input state,
    generatedTop.step input state = interpretDesign D input state
```

LEC and export validation establish the external connection to RTL.  Unless
LiveHD itself is formalized, that connection is an explicit trusted/validated
boundary rather than an internal Lean theorem.

## 17. Relation to the Existing Fast-Model Certificate Proof

The current bridge should be retained during migration:

```text
handwritten fast model = certificate evaluator
```

It provides:

- regression comparison for the first-projection implementation;
- mature operator lemmas;
- known scaling techniques for large graphs;
- detection of width, operand-order, reset, and mask bugs;
- a way to compare old and derived compilers design by design.

During migration, generate both:

```text
oldFast(D)      -- current C++ emitter
projectedFast(D) -- first projection
```

and prove or test:

```text
oldFast(D) = interpretDesign(D)
projectedFast(D) = interpretDesign(D)
```

Direct textual identity is not required.  Semantic identity is the criterion.

After the derived compiler is mature, the C++ per-operator emission switch and
per-node proof-dispatch switch can be removed.  The C++ pass becomes an exporter
and invocation wrapper.

## 18. Scalability Requirements

The current DINO experience identified several proof-term and elaboration traps.
The projection-based compiler must preserve the successful scaling techniques:

- residual let bindings must preserve graph sharing;
- no residual graph interpreter loop or node-ID lookup should remain;
- static maps should be resolved during specialization;
- generated definitions should be factored per node or manageable block;
- avoid expanding an `O(N)` environment inside every proof;
- use balanced data lookup during compile-time evaluation where needed;
- combine per-node facts with linear folds, not giant disjunction elimination;
- check well-formedness with a proved Boolean checker and scalable concrete
  evaluation;
- generate output and next-state bridges by field groups when whole-design
  elaboration is too large;
- preserve arbitrary-width constants without host integer truncation;
- reject unsupported or ambiguous semantics before residual emission.

The derived compiler should be benchmarked on:

1. semantic primitive oracle graphs;
2. SingleCycleCPU;
3. PipelinedCPU;
4. PipelinedDualIssueCPU;
5. representative CVA6 combinational blocks;
6. memory-bearing CVA6 blocks;
7. a full-design scale gate if graph size permits.

## 19. Hardware Corner Cases That Must Be Fixed in the Interpreter

Specialization preserves the interpreter's semantics, including its mistakes.
The interpreter and `DesignCert` schema must therefore resolve these cases before
compiler derivation is considered complete:

### 19.1 Bit Width and Signedness

- no reachable zero-width value;
- exact node and operand widths;
- explicit zero/sign extension;
- comparison at operand width;
- signed/unsigned division distinction;
- arithmetic right shift preserves the sign bit;
- shift amounts follow LiveHD truncation and conversion semantics;
- constants are arbitrary-precision and reduced modulo declared width.

### 19.2 Mux and Mask Operations

- Boolean mux false/true operand order;
- n-way selector indexing and default behavior;
- `Get_mask` contiguous and non-contiguous packing order;
- all-ones mask behavior above 64 bits;
- `Set_mask` value-bit-to-mask-position mapping;
- selected mask positions beyond destination width;
- canonicalization by `pass.cprop` does not remove the need for general semantics.

### 19.3 Sequential State

- reset polarity and reset value;
- reset versus enable priority;
- synchronous versus asynchronous reset;
- flop enable disabled means hold old value;
- every RHS reads old state;
- state updates are simultaneous;
- current-cycle outputs use old/current state, not post-step state;
- latches and unsupported sequential nodes are rejected or modeled explicitly.

### 19.4 Memories

- asynchronous versus synchronous read;
- registered read data;
- read enable disabled behavior;
- write enable and byte-enable behavior;
- same-address read/write collision policy;
- multiple writes and deterministic port priority;
- initialization/reset policy;
- forwarding policy;
- address truncation and out-of-range behavior;
- unsupported `undef` policies are rejected rather than guessed.

## 20. Recommended Implementation Plan

### Phase 0: Freeze and Validate `DesignCert`

1. Define a versioned canonical schema.
2. Make LiveHD emit it directly from the post-lowering graph.
3. Include graph digest and producer revision.
4. Add tiny operator, flop, and memory export tests.
5. Associate the artifact with the corresponding LEC result.

### Phase 1: Complete the Direct Lean Interpreter

1. Define `interpretDesign` over `DesignCert`.
2. Reuse or refactor `LGraphModel` operator semantics.
3. Add output and sequential-shell semantics.
4. Add memory semantics.
5. Prove well-formed graph evaluation and simultaneous state update.

### Phase 2: Define Typed Residual IR

1. Define `VType`, `HExpr`, bindings, outputs, and state updates.
2. Define `denoteResidual`.
3. Add a Lean reifier for named structures and definitions.
4. Prove reified model equals residual denotation.

### Phase 3: Implement First Projection

1. Define the small object language.
2. Define `Eval` and `evalFuel`.
3. Implement binding-time analysis.
4. Implement `mixHost`.
5. Encode `interpreterProgram`.
6. Prove `mix_sound` and `interpreterProgram_correct`.
7. Generate and validate DINO residual models.

This phase already yields a verified reusable function:

```text
DesignCert -> ResidualProgram
```

It is a manually staged or verified generating extension even before literal
self-application is complete.

### Phase 4: Implement Literal Second Projection

1. Encode `mixProgram` in the object language.
2. Prove `mixProgram_implements_mixHost`.
3. Confirm that `mixHost` can specialize `mixProgram` without divergence or
   uncontrolled code growth.
4. Compute `compilerProgram = mixHost mixProgram interpreterProgram`.
5. Prove `secondProjection_correct`.
6. Compile `compilerProgram` to an executable compiler.
7. Compare its residual output with direct first-projection output.

### Phase 5: Integrate and Retire Handwritten Semantic Emission

1. Keep `pass_lean.cpp` as thin LiveHD integration.
2. Export canonical `DesignCert` atomically.
3. Invoke the derived compiler.
4. Emit and typecheck the generated Lean artifact.
5. Audit top-level theorem axioms.
6. Run LEC, semantic regression, DINO, and CVA6 gates.
7. Remove duplicated per-operator C++ semantic emission only after parity is
   established.

## 21. Practical Alternative to Literal Self-Application

Literal second projection is a substantial research task.  A lower-risk
production architecture obtains nearly all practical benefits:

```text
one verified staged interpreter
  -> compileDesign : DesignCert -> ResidualProgram
  -> generic compileDesign_correct theorem
```

This is a verified generating extension derived structurally from the
interpreter, but not by `mix(mix, interpreter)` self-application.  It removes
semantic duplication from C++, scales to new designs, and yields a generic
correctness theorem.

Recommended ordering:

1. deliver the verified staged compiler first;
2. use it as the production replacement for handwritten emission;
3. pursue literal second projection in the small object language as a contained
   research milestone;
4. require both to produce semantically equivalent residual programs.

This avoids making self-application a blocker for replacing the current pass,
while preserving the full second projection as a well-defined objective.

## 22. Acceptance Criteria

The project can claim a verified first projection when:

- `DesignCert` is well formed;
- the interpreter is defined for every supported operator/sequential primitive;
- `mix_sound` is proved;
- specialization of the interpreter on any well-formed `DesignCert` succeeds or
  returns an explicit unsupported-feature error;
- residual denotation equals interpreter execution;
- generated named Lean definitions equal residual denotation;
- no generated top-level proof depends on `sorryAx`;
- DINO and selected CVA6 models pass semantic and LEC gates.

The project can claim a verified second projection when, additionally:

- `mixProgram` is represented in the object language;
- `mixProgram_implements_mixHost` is proved;
- self-specialization terminates for the interpreter;
- `compilerProgram = mix(mixProgram, interpreterProgram)` is produced;
- applying `compilerProgram` to any supported `DesignCert` agrees with direct
  first projection;
- the derived compiler's residual model satisfies the same end-to-end theorem.

## 23. Proposed Source Layout

```text
formal/lean/LeanSemanticPrimitives/Projection/
  DesignCert.lean
  DesignCertWF.lean
  DesignInterpreter.lean
  ObjectLanguage.lean
  ObjectLanguageSemantics.lean
  BindingTime.lean
  PartialEvaluator.lean
  PartialEvaluatorCorrect.lean
  ResidualIR.lean
  ResidualSemantics.lean
  ResidualReifier.lean
  FirstProjection.lean
  SelfApplicableMix.lean
  SecondProjection.lean

pass/lean/
  pass_lean.cpp                 # eventual thin exporter/integration wrapper
  design_cert_export.cpp
  design_cert_export.hpp
  FUTAMURA_PROJECTIONS.md
```

The exact package split can change, but interpreter semantics, partial evaluator,
residual IR, and LiveHD extraction should remain separate modules with explicit
theorems at each boundary.

## 24. References

- N. D. Jones, C. K. Gomard, and P. Sestoft, *Partial Evaluation and Automatic
  Program Generation*, 1993. Full text:
  <https://www.itu.dk/~sestoft/pebook/pebook.html>
- Y. Futamura, *Partial Evaluation of Computation Process -- An Approach to a
  Compiler-Compiler*. English retrospective and publication information:
  <https://doi.org/10.11309/jssst.21.343>
- C. Fallin and B. De Sutter, *Partial Evaluation, Whole-Program Compilation*,
  PLDI 2025: <https://cfallin.org/pubs/pldi2025_weval.pdf>
- Lean 4 reference, quotations and macros:
  <https://lean-lang.org/doc/reference/latest/Notations-and-Macros/Macros/>
- Lean 4 metaprogramming overview:
  <https://leanprover-community.github.io/lean4-metaprogramming-book/main/02_overview.html>
