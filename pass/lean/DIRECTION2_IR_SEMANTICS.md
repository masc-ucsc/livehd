# Direction 2 — LGraph as a directly executable semantic model

## Corrected scope

Direction 2 asks one question:

> Can the semantics of LiveHD's post-lowering LGraph IR itself be executed as a
> cycle-accurate simulator, without first translating the design to a residual
> language or generating a design-specific semantic model?

The answer is not a pre-/post-pass comparison.  Such a comparison is a useful
application of executable semantics, but it neither defines the IR nor produces
its simulator.  The earlier revision of this document made that application the
whole direction and was therefore off target.

The intended result is a generic Lean interpreter whose program input is a
`DesignCert`:

```lean
directStep : DesignCert -> RuntimeInput -> RuntimeState -> Except SimError RuntimeResult
```

For a design `D`, repeatedly executing `directStep D` is its simulator:

```text
                   static design
                        D
                        |
          input_t ---- directStep ---- state_t
                         |                  |
                      output_t          state_t+1
```

One successful call is one transition of the post-lowering IR.  Iteration gives
the output and state trace.  In that precise sense, the simulator is the
executable semantic model of the IR.

---

## 1. What already exists

The B1+B2 branch already defines the source-side meaning of a certificate:

```lean
interpretDesign : DesignCert -> RuntimeInput -> RuntimeState -> RuntimeResult
```

`interpretDesign D i s` evaluates the graph, observes the current-cycle outputs,
and computes the next flop and memory state.  It is the right-hand side of
`compileDesign_correct`, hence the specification against which the verified
compiler is proved.

This definition is executable in Lean's logical sense, but it is not yet a
standalone simulator implementation.  Its environment is a nested function
`Nat -> CertVal`; every update adds another function layer, so evaluating all
slots is quadratic.  Its own source says that it is never executed and is used
only for reasoning.  There is no multi-cycle runner, simulator executable,
input/state format, or performance evaluation for direct interpretation.

Direction 2 starts from this existing definition.  It does not claim that a new
semantics was introduced on this branch.

---

## 2. Difference from the verified compiler

Both directions consume the same `DesignCert` and must have the same observable
behavior, but they realize it differently.

| | Direction 2: direct semantics | B1+B2: verified compiler |
|---|---|---|
| Static input | `DesignCert` | `DesignCert` |
| Per-design transformation | none | `compileDesign D : ResidualProgram` |
| Per-cycle dispatch | on `DenseNodeCert.op` | on `ResidualExpr` |
| Executed representation | LGraph certificate directly | typed residual program |
| Main theorem | direct execution = `interpretDesign` | residual execution = `interpretDesign` |
| Intended role | reference simulator and semantic model | compiled simulator |

The required Direction 2 theorem is therefore:

```lean
theorem directStep_correct
    (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) (r : RuntimeResult)
    (h : directStep D i s = .ok r) :
    r = interpretDesign D i s
```

The direct evaluator must not call `compileDesign`, construct a
`ResidualProgram`, or dispatch on `ResidualExpr`; otherwise it is only another
entry point to B1+B2.

The two implementations can still cross-check one another, and their generic
correctness theorems should imply:

```text
directStep D i s = ok r  and  compilesOk D = true
    implies r = compileAndRun D i s
```

for every certificate accepted by both.  This equality is a consequence and an
evaluation oracle, not Direction 2's definition.

---

## 3. Semantic boundary

The formal boundary begins at `DesignCert`, not at SystemVerilog and not at the
in-memory C++ graph object.  Calling this “LGraph semantics” is justified only
for the LGraph subset whose extraction into `DesignCert` is documented and
checked.

The chain is:

```text
RTL -> LiveHD post-lowering LGraph -> DesignCert -> directStep -> trace
      outside Lean                 trusted export   proved core
```

The C++ LGraph-to-`DesignCert` exporter is currently trusted.  RTL-to-LGraph LEC,
operator grounding, shape checks, and independent differential checks reduce
that risk but do not constitute a proof of the exporter.  The formal claim must
therefore be stated as:

> For every accepted `DesignCert`, direct execution implements the Lean-defined
> one-cycle semantics of that certificate.

It must not be stated as an end-to-end proof that arbitrary RTL and LGraph have
the intended meaning.

Certificate transport is separate from semantics.  A certificate may be an
elaborated Lean value or may arrive through Direction 4's runtime loader.  The
same `directStep D` consumes the resulting value.  An unverified parser enlarges
the trusted boundary; it does not change the semantic theorem.

---

## 4. Cycle contract

The plan must freeze the following contract before optimizing the evaluator.

### 4.1 One step

For primary inputs `i` and current state `s`, one step returns:

- combinational outputs for the current state and inputs;
- next flop values, with reset priority over enable and hold;
- next mutable-memory images after the cycle's writes.

The direct simulator must use the same source, output, flop, and memory rules as
`interpretDesign`, including asynchronous reset visibility, reset polarity and
value, byte enables, write ordering, read enable behavior, and synchronous-ROM
read-data registers.

### 4.2 Traces

The multi-cycle semantics is iteration, not a second semantic definition:

```lean
structure TraceResult where
  steps : List RuntimeResult
  finalState : RuntimeState

runDirect : DesignCert -> RuntimeState -> List RuntimeInput
    -> Except SimError TraceResult
```

At cycle `t+1`, the state is the `nextState` returned at cycle `t`.  The trace
theorem follows by induction from `directStep_correct`.

### 4.3 Initial state

Not every hardware design has a unique initial state.  The semantic API therefore
takes an explicit `RuntimeState`.  A reset-driving convenience function may be
provided by the executable, but it is stimulus generation rather than part of
the IR semantics.

### 4.4 Clock scope

One call represents one step of the normalized single-edge design accepted by
the certificate exporter.  Native multi-clock/event scheduling is outside the
current `DesignCert` model and must be rejected or normalized before export.
Calling the result cycle-accurate is relative to this documented cycle boundary.

### 4.5 Observations

The public observation is the ordered output vector at each cycle.  The state is
also returned so proofs can state invariants and refinements.  Mutable memories
are function-valued; they are reasoned about extensionally or sampled at chosen
addresses rather than compared with `DecidableEq`.

---

## 5. Accepted semantic fragment

**Delivered.**  `Compiler/DirectCheck.lean` defines the acceptance predicate,
independently of `compileDesign`: nothing in it mentions `ResidualExpr`,
`ResidualProgram`, or the compiler, and it does not even import them.  Using the
compiler as the admission test would make Direction 2 depend on the very
implementation it is supposed to differ from.

### 5.1 The accepted-operator table

The table is derived from the post-lowering LGraph contract as the exporter
implements it (`pass_lean.cpp`, `cert_node_expr`), then checked against a census
of every generated `DesignCert` (469 files, 147 distinct by content hash:
DINO ×3, CORE-ET, CVA6).  `w` is the node's result width, `n` its arity.

| operator | arity rule | width rule | operand kinds | result | grounded by |
|---|---|---|---|---|---|
| `Op_Const c` | `n = 0` | `w > 0` | — | bv `w` | `Ntype_op::Nconst` |
| `Op_Sum k` | `n ≥ 1`, `k ≤ n` | `w > 0` | all bv | bv `w` | `Ntype_op::Sum` (deps = adds ++ subs) |
| `Op_Mult` | `n ≥ 1` | `w > 0` | all bv | bv `w` | `Ntype_op::Mult` |
| `Op_UDiv` | `n = 2` | `w > 0` | bv | bv `w` | `Ntype_op::Div` |
| `Op_And` / `Op_Or` / `Op_Xor` | any, **including 0** | `w > 0` | bv | bv `w` | `Ntype_op::And/Or/Xor` |
| `Op_Ror` | any | `w = 1` | bv | bv 1 | `Ntype_op::Ror` |
| `Op_Not` | `n = 1` | `w > 0` | bv | bv `w` | `Ntype_op::Not` |
| `Op_EQ` | `n ≥ 2` | `w = 1` | bv | bv 1 | `Ntype_op::EQ` |
| `Op_ULT` / `Op_UGT` | `n = 2` | `w = 1` | bv | bv 1 | `Ntype_op::LT/GT`, unsigned |
| `Op_SLT` / `Op_SGT` | `n = 2` | `w = 1` | bv | bv 1 | `Ntype_op::LT/GT`, signed |
| `Op_SHL` | `n = 2` | `w > 0` | bv | bv `w` | `Ntype_op::SHL` |
| `Op_SRA` | `n = 2` | `w > 0` | bv | bv `w` | `Ntype_op::SRA` |
| `Op_Sext` | `n = 2` | `w > 0` | bv | bv `w` | `Ntype_op::Sext` (operand 1 = sign position) |
| `Op_GetMask` | `n = 2` | `w > 0` | bv | bv `w` | `Ntype_op::Get_mask` |
| `Op_SetMask` | `n = 3` | `w > 0` | bv | bv `w` | `Ntype_op::Set_mask` |
| `Op_MuxBool` | `n = 3` | `w > 0` | bv | bv `w` | `Ntype_op::Mux`, 2 data + 1-bit sel; deps `[sel, false, true]` |
| `Op_MuxN` | `n ≥ 2` | `w > 0` | bv | bv `w` | `Ntype_op::Mux` otherwise; deps `[sel, data…]` |
| `Op_MemRead` | `n = 3` | `w > 0` | `[mem, bv, bv]` | bv `w` | `cert_memory_expand` |
| `Op_MemWrite` | `n = 4` | `w > 0` | `[mem, bv, bv, bv]` | **mem** | `cert_memory_expand` |
| `Op_MemWriteBE b` | `n = 4`, `b > 0` | `w > 0` | `[mem, bv, bv, bv]` | **mem** | `cert_memory_expand` |

Five operators are REFUSED, each for a stated reason rather than because they
happen to be unreachable:

| refused | why |
|---|---|
| `Op_Sub` | no exporter site; `Ntype_op::Sum` carries subtrahends instead |
| `Op_LT`, `Op_GT` | no exporter site (`Ntype_op::LT/GT` always selects the signed or unsigned variant from `node_output_is_signed`), and `eval_op` defines both as UNSIGNED comparisons — an ungrounded equation that would silently compare a signed operand unsigned |
| `Op_Div`, `Op_SDiv` | no exporter site; `eval_op Op_Div`'s signedness is likewise ungrounded |

This is deliberately **not** a copy of `compileOp`'s eight refusals.  Direction 2
accepts three operators B1+B2 refuses — `Op_Const`, `Op_UDiv`, `Op_SetMask` —
because each has an exporter site and a total, grounded `eval_op` equation, and
it refuses `Op_Div`/`Op_LT`/`Op_GT` for a reason (ungrounded signedness) rather
than for a count.

### 5.2 Hard constraints versus contract constraints

Two different things live in the arity and width rules, and the difference is
recorded in the source:

* **fallback-avoidance (hard).**  `Op_Not` at arity ≠ 1, `Op_SRA` at arity ≠ 2,
  the comparisons, the mux and the three memory operators all reach `eval_op`'s
  `| _, w, _ => mk_bv w 0` wildcard at any other arity.  Unchecked, a malformed
  certificate would simulate as zeros.  `Op_MemWriteBE 0` is in this class too:
  `cert_masked_update` divides the bit index by the byte width.
* **contract (soft).**  `Op_EQ`, `Op_Ror` and the four comparisons produce a
  one-bit result in `graph/cell.cpp`; `Op_MuxN` needs a selector plus at least
  one data operand.  `eval_op` is defined at other shapes, but the LGraph
  contract is not, so those certificates are malformed and are refused.

`Op_And` / `Op_Or` / `Op_Xor` accept **any** arity, zero included, because the
census finds real arity-0 `Op_Or` nodes in CVA6 (`cva6_icache`, `csr_regfile`,
`id_stage`, `cva6_ptw`, `cva6_hpdcache_if_adapter`) — a driverless reduce node,
whose value is a defined `mk_bv w 0` and not a fallback.

### 5.3 What else `checkDesign` establishes

`DesignSemWF` (proved from `checkDesign D = .ok ()` by `checkDesign_sound`) also
carries: dependency ordering; every output, flop pin and memory image naming a
real slot; bit-vector-versus-memory typing for every dependency and every shell
reference, from a static `slotKind`; nonzero widths; flop and memory ordinals in
range and agreeing with their descriptors; asynchronous flops having a reset pin
and the matching reset value; ROM tables no larger than `2 ^ aw`; and consistent
widths for repeated primary-input ordinals.  `checkRuntime` adds the input/state
shape check: `inputArity D ≤ i.size`, and one state entry per declared flop and
memory.

### 5.4 What the checker cannot establish

Two things are recorded as trusted rather than checked:

* **Single-edge normalisation.**  The certificate carries no clock-model
  provenance, so no Lean predicate can discover whether the C++ graph was
  correctly normalised.  This stays an exporter precondition.
* **Asynchronous-reset POLARITY.**  An async flop's `resetPin` slot and its
  `resetInput` ordinal legitimately disagree on polarity — the census finds
  9,262 async sources whose `activeLow` is the opposite of their
  `FlopDesc.resetActiveLow`, because the pin slot reads an already-inverted node
  while the ordinal reads the raw port.  Only the reset VALUE, the width and the
  existence of a reset pin are cross-checkable, and only those are checked.  The
  `mutFlopResetPolarity` negative control shows the evaluator is sensitive to
  the field that is not cross-checked.

### 5.5 No accepted node reaches a fallback

Phase 0's gate is a theorem list, not a promise.  `DirectCheck`'s `OpEquations`
section gives, for every accepted (operator, arity), the named primitive the
call reduces to — `eval_op .Op_Not w [a] = bv_not w a`, `eval_op_cert
.Op_MemRead w [.mem m, .bv a, .bv en] = .bv (cert_mem_read w m a en)`, and so on
for all 24.  Each is `rfl`, which is the point: the claim is about which BRANCH
is taken, and each equation is false if the call took a fallback.  Restating the
operator bodies instead would repeat the mistake `eval_op_correct` already makes
(near-identical sides, proving nothing about the equations' fidelity to LiveHD).

---

## 6. What “formally verified” means here

Direction 2 needs four layers of evidence, stated separately.

1. **Semantic equations.** Each accepted operator and each state-update rule has
   an explicit Lean definition, including corner cases such as division by zero,
   shift amounts, signedness, reset priority, and memory collisions.
2. **Executable-refinement proof.** The efficient dense evaluator equals
   `interpretDesign` for every accepted certificate, input, and state.
3. **Trace proof.** Iterating the efficient evaluator produces the same trace as
   iterating the reference one-step semantics.
4. **Artifact grounding.** Tests, LEC, and extraction checks connect real LGraphs
   to certificates.  This is validation of the trusted boundary, not a Lean proof
   of the C++ exporter.

The existing theorem `eval_op_correct` has nearly identical executable and
denotational bodies.  It establishes definitional agreement but is not an
independent validation of the operator equations.  The plan must not cite it as
proof that the equations match LiveHD or RTL.

Useful secondary theorems are determinism, preservation of runtime-state shape,
and agreement with B1+B2 on certificates accepted by both.

---

## 7. Deliverable

The Direction 2 artifact is a generic simulator library and a native Lean
executable, not generated semantic code:

```text
lgraph-sim DESIGN INPUT_TRACE INITIAL_STATE
```

The core executable API accepts a `DesignCert` value.  Initially, a small
per-design launcher may import an elaborated certificate literal.  If Direction
4 is available, the same simulator may receive certificates at run time; a
verified parser is separate follow-up work.

The executable reports ordered outputs and next state per cycle, rejects invalid
certificates before execution, and uses deterministic trace formats suitable for
Lean/Isabelle proof workflows and differential testing.

Because `RuntimeState.mems` contains functions, a CLI cannot serialize it
directly.  Its external state format must represent each memory by a default
value plus finite address/value overrides, convert that representation to a
function on input, and print only requested/sampled addresses on output.

---

## 8. Current status

| Component | Status |
|---|---|
| `DesignCert`, runtime input/state/result types | implemented on the inherited B1+B2 base |
| `interpretDesign` one-cycle reference semantics | implemented and used as B1+B2's specification |
| Generic graph traversal and operator semantics | implemented |
| Accepted-operator table, grounded and census-checked | **done** — §5.1, `Compiler/DirectCheck.lean` |
| Independent source-level certificate checker | **done** — `checkDesign` / `checkRuntime`, soundness proved |
| Efficient direct evaluator over dense certificate nodes | **done** — `evalDense`, one array pass per cycle |
| `directStep_correct` | **proved**, no `sorry`, axioms `propext / Classical.choice / Quot.sound` |
| Multi-cycle direct trace runner and trace theorem | **done** — `runDirect`, `runDirect_correct` |
| Agreement with B1+B2 where both accept | **proved** — `directStep_eq_compileAndRun` |
| Standalone simulator executable | **done** — `lgraph-sim`, generic over `DesignCert` |
| Direct-simulation measurements on CORE-ET, CVA6, DINO | **done** — see `DIRECTION2_RESULTS.md` |

The honest current claim is therefore stronger than the previous revision's:

> The certificate's one-cycle semantics is now executed directly, by a checked
> evaluator proved to implement `interpretDesign`, and the resulting simulator
> runs real CORE-ET, CVA6 and DINO designs — every distinct generated
> certificate is accepted, up to a 107,213-node CVA6 cache subsystem with twelve
> mutable memories.  What remains trusted is the C++
> LGraph → `DesignCert` exporter and the single-edge clock normalisation it
> performs — neither is a Lean theorem, and §3 states the boundary.

---

## 9. Non-goals and separate applications

Direction 2 does not by itself:

- prove LiveHD optimization passes correct;
- compare graphs before and after compiler passes;
- derive a residual compiler through Futamura projection;
- replace B1+B2's compiled simulator;
- verify the C++ exporter or RTL frontend;
- provide native multi-clock/event-driven semantics.

Pre-/post-pass trace comparison remains a plausible application once
`runDirect` exists.  It should have its own plan and claims.  It is not the
definition or completion criterion of Direction 2.

---

## 10. Paper claim

A statement the artifacts support:

> We define a one-cycle Lean semantics for post-lowering LGraph certificates and
> a standalone interpreter that executes it directly — dispatching on the
> LGraph operator, without residual compilation, and without importing the
> verified compiler.  The interpreter is fail-closed against an independent,
> source-level acceptance predicate whose accepted-operator table is derived
> from the exporter and validated against every generated certificate, and it is
> proved to implement the same one-cycle semantics that is already the verified
> compiler's specification; iterating it is proved to produce the same trace as
> iterating the specification.  The resulting binary runs real CORE-ET, CVA6 and
> DINO certificates.  The formal claim begins at `DesignCert`: the C++ exporter
> and its single-edge clock normalisation remain trusted.
