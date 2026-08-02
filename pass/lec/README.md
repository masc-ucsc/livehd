# `pass/lec` — hierarchical equivalence: the algorithm

`lec.md` is the engine reference (encoder facts, CLI, options, tests). This file
is the **hierarchical decomposition**: what a "proven submodule" means, what is
proven where, and why each rule is the way it is.

Everything here is about `lhd lec --top T`. **Equivalence is a claim about `T`
only.** Proving other modules is a separate request (`--top X`), never something
the driver asserts on the user's behalf.

## 1. The shape of the proof — top-down, then discharge

```
for each def D in the top's subtree, IN ANY ORDER, ALL AT ONCE:
    prove D_ref == D_impl with EVERY child of D replaced by a box
                                                     # a PREMISE, not yet a fact

then, leaves-first (pure bookkeeping, no solving):
    D is UNCONDITIONALLY proven  iff  D proved AND every child it boxed is
```

That is the default (`formal.lec.hier_order=top_down`). Every def is proved
against the *assumption* that its children are equivalent, and those assumptions
are discharged by the other entries of the same pass.

**This is an induction, not circular reasoning.** The module DAG is well-founded:
D's premise set is strictly-lower nodes, and a leaf assumes nothing. So the
closure terminates and the composition is sound — it is ordinary assume-guarantee
over a partial order. Three things follow, and the third is why it is the default:

- **No def waits on another def's verdict**, so the hierarchy is dispatched with
  *no* dependency edges — fully parallel, instead of serialized by depth.
- **Every miter is at its minimum size.** Bottom-up must *flatten* a child that
  did not prove, which is exactly the case where the parent is hardest.
- **A refuting block is absorbed by its parent, and the chain stops there** (§3).

The premise direction matters and is easy to get backwards. Assume-guarantee
gives

> (∀ children C: C_ref ≡ C_impl) ⟹ D_ref ≡ D_impl

and **nothing in the other direction**. Refuting a premise does not refute the
conclusion, so a child that fails does not make its parent fail — it leaves the
parent *conditional*. A top proven only conditionally is reported **inconclusive
(exit 7), naming the undischarged premises**, never a pass.

### `formal.lec.hier_order=bottom_up` — the legacy order

```
topo-order the module-def DAG, leaves first
for each def D (children already settled):
    prove D_ref == D_impl, with every PROVEN child of D replaced by a box
```

Only a proven child becomes a box; an unproven one stays flattened into its
parent. Kept for A/B measurement, and because `hier_refute=fail` (a debug mode)
is *defined* in terms of leaves-first order — "skip a def whose child already
refuted" has no meaning when no def waits on a child.

Both orders discharge **the same set of per-def obligations**, so an all-proven
run costs the same either way. They must also produce the same verdict for every
design; an ordering strategy may change cost, never an answer
(`lec_hier_topdown_test.sh` case 4).

## 2. What a proven submodule is

**A sequence transducer, and nothing else.**

`D_ref ≡ D_impl` says: *from reset, identical input sequences produce identical
output sequences.* That is the whole content of the proof, and it is all a caller
needs. So in the parent P:

| | obligation |
|---|---|
| **A** | prove the inputs P drives into D are equal, at every step — the `bbin:<box>:<port>` compare points |
| **B** | prove everything outside D is equal — the ordinary outputs and next-states |
| **C** | assume D's outputs are equal — **one shared free symbol per (instance, port, cycle)** |

C is *justified by* A plus D's own proof plus equal initial state. Nothing else is
needed, and in particular:

- **Comb vs. stateful does not matter.** From the caller's side both are "given
  X-sequence, always Y-sequence". Whether D holds registers or memories is
  invisible, so there is one box kind, not two.
- **No state cut, no uninterpreted function, no congruence rule.** The box output
  is a plain bit-vector symbol shared between the two designs.

The symbol must be **fresh per cycle**. Reusing one symbol across the unroll pins
the child's output to a constant, which false-proves a design whose only
difference is *when* that output changes. A's obligations are re-checked every
step, so the sharing stays justified step by step.

### Initial state

If the run drives reset (`formal.phase=after_reset`, the default), the reset
prologue puts both copies of D in the same state and C follows outright.

If the run does **not** drive reset, we *assume* the two copies start in the same
state — exactly what the engine already does for a reset-less flop (a shared
`s0_<key>` symbol). Same rule, applied to a box.

### The one asymmetry worth knowing

From reset, C is a **consequence**. Mid-stream — the single-step inductive miter,
which starts from an arbitrary equal state rather than from reset — "the two Ds
are in corresponding states" is an **assumption**: D's own from-reset proof
compared outputs, it never built a bisimulation between D_ref's and D_impl's state
spaces. It is the standard assume-guarantee hypothesis and it is the right one to
make, but it is an assumption, which is why the from-reset frame is the one to
prefer for hierarchy.

### Legacy box model (`--set formal.lec.box_model=uf`)

The previous encoding modelled a stateless child as `UF_<def>_<port>(inputs)`
(`Comb_box`) and a stateful one as `UF_out(state)` / `UF_next(inputs, state)` with
a threaded per-instance state cut (`State_box`). It is kept as an escape hatch,
but the sequence model is strictly better:

- the UFs force the query into **QF_AUFBV**, which disables cvc5's eager
  bit-blaster — the driver's flat re-solve exists to work around exactly that;
- `Comb_box` emitted **no `bbin:` obligations at all**, so a stateless child's
  inputs were never checked and ABC saw the two sides' `APPLY_UF` as unrelated
  free inputs, leaving every cut downstream of it undecidable;
- `State_box`'s per-instance state cut can cross two interchangeable instances and
  refute two equivalent designs;
- `boxcong` existed only to recover "equal inputs ⇒ equal next state" for the UF
  form.

## 3. A REFUTED intermediate def is not a bug report

**A module boundary is not part of the specification.** Functionality moves across
it: the Pyrope and the Verilog may partition the same machine differently, so an
intermediate def's I/O behaviour need not match even when the design is
equivalent. Only the requested top's boundary is contractual.

So on a refute at def D (`formal.lec.hier_refute=escalate`, the **default**):

- **inline D into its caller P** and re-prove P;
- D's **siblings and descendants stay boxed** — only the offender is expanded, so
  the cost is bounded;
- if P proves, that P-with-D-inlined is what became a proven unit. It is *not* a
  proof of D: another caller of D may partition differently and has to inline it
  too;
- D's counterexample is kept as a **diagnostic** ("the two designs stop agreeing
  here"), never as the run's verdict. The driver reports how many refutations
  were **ABSORBED** this way.

### Why top-down makes the escalation stop at P

This is the case the order exists for. Under top-down, by the time D's refutation
is known, **P has already been proven with D boxed**, and so has every ancestor of
P with *its* child boxed. So:

1. re-prove P with D inlined, everything else still boxed;
2. if P proves, P's own boundary behaviour is intact — and every ancestor's proof
   already only ever depended on *P being equivalent*, which is now established
   without the D premise. **The chain closes. Nothing above P is re-solved.**
3. only if P *itself* refutes does the escalation move up a level, and there the
   same argument applies again.

Bottom-up has no such stopping rule: it learns D refuted *before* P is proved, so
P runs with D flattened; if P then refutes, P's parent runs with both flattened,
and the cone grows one level per step — in the worst case all the way to the top,
which is the slow path this replaces. (User ruling, 2026-08-02.)

**Measured on minion** (same compiled inputs, same trust list, order the only
variable): 146 s top-down vs 222 s bottom-up, same 166/170 defs proven, same exit.
The report is where it really shows. One def, `intpipe_csr_file`, is UNKNOWN under
both. Bottom-up flattens it into `intpipe_top`, which is then UNKNOWN and flattens
into `core_top`, which flattens into `minion_top` — where the accumulated cone hits
a word-level combinational cycle and the encode fails outright, so the run's
headline is a 29-node cycle dump. Top-down proves all three with their children
boxed (`minion_top`: *297/297 cones PROVEN, every cut discharged*) and names the
three undischarged premises. The cycle never enters a miter, because nothing is
flattened.

`formal.lec.hier_refute=fail` is the old fail-fast behaviour: it stops at the
first differing block, which localizes quickly but calls a boundary mismatch a
design bug. It is a **debug aid**, announces itself with a warning, and forces
`bottom_up` (it is a leaves-first notion).

Under this policy, **a refutation of the top is a real bug** — in the generated
Pyrope, in the Verilog read, or in LEC itself — and should be chased as one.

## 4. Correspondence: the part that can lie

A/B/C is sound. What can still go wrong is *which instance pairs with which*.

Boxes pair by `<defname>#<tag>`, name-first (an instance name survives both
front-ends) with an occurrence fallback for the unnamed remainder. With two
interchangeable instances of one def, a wrong pairing makes A fail on a named
port — a *false* refutation, but a well-localized one. The fix is a permutation
retry over the symmetric bucket, the same shape as the same-shape-memory
permutation retry in `todo/livehd/2f-lec.html`.

A box key present on **one side only** yields one-sided obligations, which the
miters gate to an incomplete correspondence — never a Proven or a Refuted.

## 5. Timing: the phase schedule

Orthogonal to the above, and documented in `todo/livehd/2f-lec.html`
(§phase-schedule) and `todo/livehd/2f-latch.html` (§m10). In brief: a design with
a latch, a negedge endpoint or a recognized clock gate is encoded over an ordered
sequence of **microsteps** per source period —

```
close_low   active-LOW clock-role latches close (immediately before rise)
rise        posedge flops, posedge memory ports, data-gated latches
close_high  active-HIGH clock-role latches close (immediately before fall)
fall        negedge flops, negedge memory ports
```

— from a **read-only** analysis (`phase_sched.{hpp,cpp}`): no coloring, no graph
rewrite, no synthesized phase counter, no timing state threaded through ports, so
it composes across hierarchy. A close microstep only runs when something happens
in it, so a plain posedge/negedge design costs two encodes per period, not four.

### The clock forest

Clock relationships are decided **design-wide, top-down, before any def is
proven** (`Clock_forest`, built in `lec_hierarchical`):

```
seed       the def-under-proof's clock ports, unioned across ALL its
           instantiation sites anywhere in the library
propagate  reverse-topo (top-down): each child's clock port takes the root of
           the net its parent drives into it
derive     a Clock_cell / inline gate resolves THROUGH to its reference clock —
           gating, inversion and division split the SCHEDULE, never the forest
close      with exactly one top root, every clock-spelled port in the subtree
           carries it
implicit   a def's `clock` port is its own clock: the unique other root
```

Resolving each endpoint bottom-up instead is not enough, and all three ways it
fails were live on minion:

1. A Pyrope `reg x = 0` has **no clock cone at all** — it commits on its module's
   own clock — so it became a synthetic `<implicit>` root.
2. A cone that stops at an opaque boundary names the **child's** port, so one net
   acquires as many roots as it has spellings (`clk_i` in 85 defs, `clock` in 6).
3. tolg gives any module holding a `reg x = 0` an input port named `clock`, and
   **no instantiation site ever drives it** — so no site union can tie it to the
   module's named clock port. That one unbound port refused `intpipe_csr_file`
   (declares only `clk_i`; its parent wires `clk_i = clock`) and, through it,
   `intpipe_top` and `core_top` — a module whose only clock port is `clk_i` was
   reported as having three roots, with the two sides disagreeing.

The implicit-clock rule is deliberately narrow: `clock` joins the unique *other*
root, so a module with two genuinely unrelated clocks still refuses by name.

## 6. Trust

`--trust DEF` / `formal.lec.trust` assumes a def equivalent **without proving
it** — an explicit, disclosed assumption ("PROVEN under N trusted def(s)"), for a
def the user chooses not to compare. It is not a workaround for a cell the
encoder cannot model; latches, negedge endpoints and clock gates are modelled
directly now.

## 7. Verdict discipline

PASS means definitively equivalent; FAIL means definitively different; timeout,
unsupported timing and ambiguous correspondence are INCONCLUSIVE. Collapse,
pairing, phase scheduling, proof order and every retry must preserve that. An
abstraction may cause more work or an UNKNOWN — never a wrong verdict. A check
that compares nothing is not a proof of anything.

Two ways the hierarchy can quietly violate this, both guarded:

- **An undischarged premise is not a proof.** A top proved with every child boxed
  is only a *conditional* result; if any premise never discharges it is degraded
  to Unknown (exit 7) naming the open blocks. `rc 7` ("could not decide") and
  `rc 10` ("here is a counterexample") must never be conflated
  (`tests/lec_verdict_policy_test.sh`).
- **An absorbed block CEX is not the design's answer.** Once a parent re-proves
  with a refuting block inlined, that block's counterexample is a diagnostic; it
  must be dropped before the run picks a fallback verdict, or a design that is
  equivalent reports `equiv_fail`.
