# Rocq for Hardware Semantics: a Literature Review

*Written to justify the design of `pass.rocq`, the third theorem-prover export
target in LiveHD (after `pass.isabelle` and `pass.lean`).*

---

## 1. Scope and method

LiveHD already exports a post-`cprop` LGraph to two proof assistants:

| Pass | Support library | Proof stack |
|---|---|---|
| `pass.isabelle` | `formal/semantic_primitives`, `formal/translation_correctness` | Isabelle/HOL + HOL-Word |
| `pass.lean` | `formal/lean` (`LeanSemanticPrimitives`) | Lean 4 + Mathlib |

Both implement the same "Strategy B" chain:

```
generated fast model  =  evaluated graph certificate  =  mathematical certificate semantics
```

with an upstream LEC gate proving `RTL ≡ LGraph`, so the prover side only has to
justify *generated model = LGraph certificate*.

This review asks: **what does Rocq bring as a third target, and where will it
hurt?** It is Rocq-centric — Rocq is the subject, and Lean 4, Isabelle/HOL, and
ACL2 are the comparison set. The axes are the ones that actually determine
whether this pass works:

1. logical foundations (§3) — can bit widths live in *types*?
2. proof languages and tactics (§4)
3. automation and decision procedures (§5)
4. computation and reflection (§6) — **the axis this pass lives or dies on**
5. hardware specification and verification track record (§7)
6. bit-vector libraries (§8)
7. engineering at scale (§9)

Out of scope: HOL4, PVS, Agda, F\*, and the SMT/model-checking tools LiveHD
already uses (`cvc5`, Yosys SAT/BMC) except where a prover integrates with them.
Comparisons are about *interactive theorem provers used as a semantic backstop*,
not about push-button equivalence checking, which LiveHD's LEC gate already does
better than any of these four.

A note on honesty: two of these systems (ACL2, Isabelle) have decades of
industrial hardware deployment; Rocq has a deep *research* record but a thinner
industrial-hardware one; Lean is the newest and has the best bit-vector
automation but the shallowest track record. This review tries not to flatten
those differences.

---

## 2. What "Rocq" is now

The Coq Proof Assistant was renamed **The Rocq Prover** in 2025. Rocq 9.0
(released 12 March 2025) is the first release under the new name and completes
the rename. Practically relevant changes:

- **One binary.** `rocq` dispatches subcommands (`rocq c`, `rocq makefile`,
  `rocq top`, `rocq dep`, …) that used to be separate executables.
- **Stdlib split and rename.** The standard library moved to the `Stdlib`
  prefix; `From Stdlib Require Import ZArith.` is the modern spelling and
  `From Coq Require Import ZArith.` now goes through a deprecation shim.
- **Backward compatibility.** Rocq 9.0's CLI is backward compatible with Coq
  8.20, and compatibility shims keep the legacy binaries working: `coqc`,
  `coqtop`, `coq_makefile`, `coqdep`, `coqchk`, `coqdoc`, `coqpp`, `coqwc`,
  `coq-tex`, `coqnative`, `coqworkmgr`.
- Template-polymorphism handling moved closer to being subsumed by sort
  polymorphism.

For this pass the consequences are small but real: use `rocq c` / `rocq makefile`
and `From Stdlib Require`, and expect published literature and library names to
still say "Coq" everywhere. Below, "Rocq" refers to the system and "Coq" is kept
when naming a specific historical artifact (CompCert, CoqQFBV, coq-bbv).

---

## 3. Logical foundations

| System | Logic | Types | Kernel |
|---|---|---|---|
| **Rocq** | Calculus of Inductive Constructions (pCuIC) | dependent, universe-polymorphic, `Prop`/`SProp`/`Set`/`Type` | small type checker; proof terms are objects |
| **Lean 4** | CIC variant | dependent, universe-polymorphic | small kernel; adds quotient types, propositional extensionality, and choice as axioms |
| **Isabelle/HOL** | simply-typed higher-order logic (Church's STT + Hindley–Milner-ish schematic polymorphism) | simple types + type classes; **no dependent types** | LCF-style: theorems are an abstract datatype, no proof terms by default |
| **ACL2** | first-order, quantifier-free, total recursive functions over a Lisp universe; induction up to ε₀ | **untyped** (guards are a separate, non-logical discipline) | the prover *is* the kernel — a large trusted Lisp program |

### Why this matters for a hardware export

The question that decides the shape of the generated model is: **can a bit width
be part of a type?**

- **Isabelle**: yes, via `'n word` with the width in a numeral type
  (`32 word`). Width errors are caught by the type checker. The cost is that
  every width change needs an explicit `ucast`/`scast`, and arithmetic *about*
  widths is awkward because HOL has no dependent types — `LENGTH('a)` is a
  side-channel.
- **Lean 4**: yes, `BitVec w` with `w : Nat`, genuinely dependent. Width algebra
  (`BitVec (m + n)`) works, and `bv_decide` reasons about it.
- **Rocq**: yes in principle (it is the most dependently typed of the four), and
  this is exactly where Rocq's reputation for pain comes from. A width-indexed
  `word n` (as in `coq-bbv`) means that any lemma that changes a width produces
  a *type* mismatch that needs transport along an equality proof
  (`eq_rect`/`JMeq`), not just a rewrite. Kami and Kôika live with this; it is
  the single most-cited friction point in dependently typed hardware
  formalization.
- **ACL2**: no. There are no types; a bit vector is a natural number, and
  "32-bit" is the *predicate* `(unsigned-byte-p 32 x)` carried in hypotheses.
  This sounds primitive and is in fact one of the reasons ACL2 scales so well on
  bit-level industrial proofs (§5, §7): there is no width-transport problem at
  all, because widths never appear in types.

The trade is real and it cuts both ways. Types catch width bugs at
elaboration time; untyped-with-predicates makes automation uniform. This review's
recommendation in §11 threads that needle for `pass.rocq` specifically.

A second consequence: **Rocq's `Prop` is proof-irrelevant and erasable, and
proof terms exist as objects.** That makes proof-by-reflection natural (§6) and
makes *extraction* to OCaml/Haskell a first-class feature — Rocq's extraction is
more mature than Lean's compiler-based story and far beyond Isabelle's code
generator in terms of how much the community relies on it. CompCert ships as
extracted OCaml. That matters if LiveHD ever wants a *verified checker binary*
rather than a proof script.

---

## 4. Proof languages and tactics

This is where the four systems differ most in day-to-day feel.

### Rocq: a stack of tactic languages

- **Ltac1** — the classic untyped tactic DSL. Ubiquitous, and its weakness is
  well documented: it offers the tactic programmer *no static guarantees*, so
  large tactic developments are hard to maintain. Failure modes are notorious —
  silent backtracking, `match goal` fragility under variable naming, and error
  messages that surface far from the cause.
- **Ltac2** — an ML-family language (call-by-value, effectful, Hindley–Milner
  static typing) designed to stay as close to Ltac1 as reasonable while fixing
  those defects. Important caveat: Ltac2 **deliberately does not** statically
  guarantee that a Rocq term produced by an Ltac2 computation is well-typed;
  well-typedness is checked dynamically, for Ltac1 backward compatibility.
- **SSReflect** — not a separate language so much as a disciplined tactic
  dialect plus a *small-scale reflection* methodology: keep decidable predicates
  as `bool`, move between `bool` and `Prop` with views, and drive proofs with a
  very powerful `rewrite`. It is the backbone of mathcomp and of the four-colour
  and odd-order theorem developments. Its influence is such that Lean has a port
  (LeanSSR), which notably does *not* need the explicit bool/Prop switching that
  Coq's SSReflect requires.
- **Mtac2** — typed metaprogramming with static guarantees Ltac and OCaml
  plugins lack, extended to backward-reasoning tactics.
- **Coq-Elpi** — λProlog-based metaprogramming, increasingly used for
  derivation/elaboration plugins.
- **OCaml plugins** — the escape hatch; maximum power, maximum maintenance cost.

The plurality is genuinely double-edged: it means there is always a way to write
the automation you need, and it means a codebase can accumulate four
incompatible styles.

### Isabelle: Isar + apply-scripts + Eisbach

Isar ("Intelligible semi-automated reasoning") is the distinctive artifact — a
hierarchical proof language with explicit statements of assumptions and
conclusions, reading like mathematical prose, in contrast to the scripting
approach elsewhere. `apply`-scripts remain for the unstructured cases, and
Eisbach provides a declarative way to build proof methods. LCF-style means
tactics cannot produce unsound theorems by construction.

### Lean 4: tactics are just Lean programs

Lean 3 introduced a metaprogramming framework in which proof procedures are
themselves Lean programs, rather than a separate DSL like Ltac; Lean 4 pushes
this all the way — the tactic framework, `simp`, `omega`, and `bv_decide` are
Lean code, elaborated and compiled by the same system. The practical effect
(explicitly noted by the LeanSSR authors) is that tactic implementers and proof
engineers share one language and one debugger.

### ACL2: no tactics at all

ACL2 is the outlier and the comparison is more interesting for it. The user does
not write a proof; the user states a theorem and the prover runs the
**Boyer–Moore waterfall**: simplification, destructor elimination,
cross-fertilization, generalization, elimination of irrelevance, and induction,
applied to emerging subgoals until all are discharged. Induction schemes are
chosen by heuristics that score candidate schemes (a "hitting ratio").

The user's leverage is *indirect*:

- **rewrite rules** — every proved theorem can be stored as a rewrite/linear/
  type-prescription rule that changes how the simplifier behaves globally;
- **`:hints`** — per-goal steering (`:use`, `:in-theory`, `:induct`, `:cases`);
- **books** — the Community Books repository holds roughly 7,500 books of
  reusable rule libraries; picking the right book is often the whole proof.

So an ACL2 "proof engineering" skill is *library curation and rule design*,
whereas Rocq/Lean/Isabelle skill is *tactic script authorship*. For highly
regular, bit-level, industrially repetitive goals — exactly hardware datapath
proofs — the ACL2 model has proven extraordinarily effective (§7). For irregular,
structurally creative goals it is much weaker.

**Relevance to `pass.rocq`:** a code generator does not write creative proofs. It
emits thousands of near-identical obligations. That profile favours (a) ACL2's
rule-library model, (b) reflection (§6), and (c) *not* hand-written Ltac1. The
design in §11 leans on reflection and on definitional equality, using tactics as
thinly as possible.

---

## 5. Automation and decision procedures

### Rocq

- Arithmetic: `lia`, `nia`, `lra`, `nra`, `psatz` (Presburger / nonlinear real
  arithmetic via Positivstellensatz certificates).
- Algebra: `ring`, `field` — themselves classic reflection-based tactics.
- Equality/logic: `congruence`, `btauto`, `tauto`, `firstorder`, `auto`/`eauto`
  with hint databases.
- **SMTCoq** — a plugin that dispatches goals to external proof-producing
  solvers and *checks their certificates inside Rocq*. It supports zChaff for
  propositional logic and veriT/CVC4 for the quantifier-free combination of
  fixed-size bit vectors, arrays with extensionality, linear integer arithmetic,
  and uninterpreted functions. This is the closest Rocq analogue to
  sledgehammer, and unlike sledgehammer it produces a kernel-checked certificate
  rather than a reconstructed proof attempt.
- **CoqQFBV** — a *certified* QF_BV solver built from a verified bit-blasting
  algorithm, reported to give certified answers on ~97% of a real-world
  cryptographic-verification benchmark set.

Note what is missing: Rocq has **no built-in bit-vector decision procedure** in
the stdlib. Bit-level automation is a plugin/library decision.

### Isabelle/HOL

- **Sledgehammer** — the flagship. Bridges to external ATPs (E, Vampire,
  Leo-III) and SMT solvers (Z3, CVC4/cvc5), then *reconstructs* the proof inside
  Isabelle (usually as `metis`/`smt`/`blast` calls). Its productivity effect is
  the single most-cited reason people choose Isabelle.
- **Counterexample finders** — Nitpick (via Kodkod → SAT, finds finite fragments
  of infinite countermodels) and Quickcheck (uses the ML compiler as a fast
  ground evaluator). Being able to *disprove* a conjectured invariant in seconds
  is worth a great deal on a hardware model.
- Bit-vector-specific: `Word_Lib` / HOL-Word plus `word_bitwise` (blast a word
  goal to bit equations) and the `smt` method. Effective but noticeably less
  polished than Lean's current story.

### Lean 4

- `simp` with a large lemma set, `omega`, `decide`, `norm_num`, `polyrith`.
- **`bv_decide`** — verified bit-blasting to an external SAT solver with an
  in-kernel-checked certificate. Recent work reports Lean's canonical `BitVec`
  library covers all SMT-LIB 2.7 QF_BV operations (including overflow
  predicates) and that `bv_decide` solves more theorems than the prior
  state of the art in verified bit-blasting, specifically CoqQFBV.

That last point is directly relevant here and should not be soft-pedalled: **for
pure bit-vector goals, Lean currently has the best verified automation of the
four.** LiveHD already has a Lean pass; Rocq's case has to rest on something
else (§10).

### ACL2

- The waterfall is *the* automation, always on.
- **GL** and its successor **FGL** — symbolic simulation frameworks that prove
  finitely bounded ACL2 theorems by bit-blasting them into Boolean formulas and
  discharging with a BDD package or a SAT solver. GL added an AIG representation
  and links to external SAT solvers; FGL adds **incremental SAT**, so related
  queries share learned clauses and heuristic state.
- The `def-gl-thm` idiom is, in effect, "prove this bounded hardware property by
  bit-blasting", integrated into the same logic as the unbounded inductive
  proofs.

This is the most *hardware-shaped* automation of the four systems, and it is why
ACL2 owns the industrial datapath-verification niche.

### Summary

| | ATP/SMT bridge | Bit-vector decision procedure | Counterexamples |
|---|---|---|---|
| **Rocq** | SMTCoq (certificate-checked) | CoqQFBV (certified), SMTCoq QF_BV | `QuickChick` (property testing) |
| **Isabelle** | Sledgehammer (reconstructed) | `word_bitwise`, `smt` | Nitpick, Quickcheck |
| **Lean 4** | limited | **`bv_decide`** (verified bit-blasting, current SOTA) | `plausible`/`slim_check` |
| **ACL2** | — (waterfall instead) | **GL/FGL** (BDD/AIG/SAT, incremental) | random testing via books |

---

## 6. Computation and reflection — the axis that matters most

`pass.rocq` emits a *certificate evaluator*: the generated model is an
executable function, and the correctness claim is that evaluating a reified graph
certificate yields the same result. That makes **how fast and how trustworthily
the prover can compute** the single most important property.

### Rocq

Three reduction strategies, in increasing speed and increasing trust cost:

| Tactic | Mechanism | Speed | Trusted base |
|---|---|---|---|
| `cbv` / `compute` | kernel reduction, tunable (`delta`/`beta`/`iota`/`zeta` flags) | slowest | kernel only |
| `vm_compute` | compiles to bytecode for a call-by-value VM; comparable to the OCaml bytecode compiler; dramatically faster than `cbv`, but **cannot be fine-tuned** | fast | kernel + the VM |
| `native_compute` | compiles the term to OCaml and runs native code; typically **2–5× faster than `vm_compute`**, with reports of a further ~3× in some scenarios | fastest | kernel + VM + the OCaml compiler and runtime |

For calibration: Rocq's VM runs CompCert at about 2/3 the speed of CompCert
extracted to OCaml bytecode, and ~15% of the speed of natively compiled extracted
CompCert.

Two practical warnings from the literature that apply directly to generated code:

- `vm_compute` and `native_compute` **defy `Opaque`** — they will unfold
  definitions you marked opaque. A generated model that relies on opacity for
  layering will not get it.
- Reduction that gets *stuck* on a missing symbol produces enormous partially
  evaluated terms and consumes huge memory. On a 19k-line generated model this is
  not a theoretical risk. Every primitive in the support library must be a
  closed, computable definition — no axioms, no opaque helpers on the evaluation
  path.

**Proof by reflection** is the idiom Rocq is built for: instead of building a
proof object, define a verified decision procedure in Rocq, prove it sound once,
and then discharge each instance by *running* it. This converts proof
construction into computation, which is orders of magnitude cheaper for
mechanically generated goals. `ring`, `lia`, `btauto`, SMTCoq's checker, and
CoqQFBV are all reflection-based. This is precisely the shape of a graph
certificate evaluator, and it is the strongest single argument for Rocq as a
LiveHD target.

### Isabelle

- `eval` — via the code generator: compiles to ML and runs it. Fast; puts the
  code generator *and* the ML compiler in the trusted base.
- `code_simp` / `normalization` — kernel-level simplification; slower, smaller
  TCB.

`pass.isabelle`'s `BRIDGE_BUGS.md` records that the `eval` vs `code_simp` choice
was settled *by measurement* on real designs, which is the right way to make it.

### Lean 4

- `decide` — kernel reduction of a `Decidable` instance. Trustworthy, and slow
  enough that the Lean pass explicitly uses it only for small per-node side
  goals.
- `native_decide` — compiles and runs; adds the Lean compiler and runtime to the
  TCB. `pass/lean/README.md` notes `native_decide` blowing up on very wide words,
  which is exactly why `max_width` defaults to 1024.

### ACL2

Everything in the logic is *executable by construction* — the logic is a subset
of Common Lisp, and evaluating a term is running Lisp. There is no `compute`
tactic because there is no gap between the logic and the evaluator. This is
enormously convenient and it means the Lisp runtime is unavoidably in the trusted
base.

### Trust-base comparison (smallest first)

```
Rocq  cbv/compute        ⊂  Rocq vm_compute        ⊂  Rocq native_compute
Lean  decide             ⊂  Lean native_decide
Isa   code_simp          ⊂  Isabelle eval
ACL2  (Lisp evaluation is the semantics; no smaller option)
```

**Design consequence:** `pass.rocq` exposes an `eval_engine` knob
(`vm｜native｜cbv`) so this trade is an explicit, per-run decision rather than a
hard-coded one, and defaults to `vm` — the fast option that does *not* add the
OCaml compiler to the TCB.

---

## 7. Hardware specification and verification: track record

### Rocq / Coq

The research record is deep and directly on-topic:

- **Kami** (ICFP 2017, MIT PLV) — a Coq library for expressive, *modular*
  reasoning about hardware in the style of Bluespec. Designs are specified,
  implemented, and verified entirely inside Coq, ending in automatic extraction
  into a pipeline that reaches FPGAs. It introduces a Bluespec-inspired language
  supporting sequential characterization of modules while preserving concurrent
  hardware semantics. This is the strongest existence proof that a whole
  processor-scale design can live inside Rocq.
- **Kôika** (PLDI 2020) — "the essence of Bluespec": a core rule-based hardware
  language where programs are rules that appear to update state atomically. Its
  novel *deterministic* operational semantics uses dynamic analysis to rule out
  concurrency anomalies, and its implementation includes Coq definitions of
  syntax, semantics, key metatheorems, **and a verified compiler to circuits**.
- **Fe-Si** — an earlier Coq-embedded HDL in the same lineage.
- **Vericert** (OOPSLA 2021) — a formally verified high-level synthesis tool
  extending CompCert with a hardware-oriented IR and a Verilog backend, proven
  correct in Coq. Notably, **its Verilog semantics were ported to Coq from a
  HOL4 semantics** — a direct precedent for "port an existing prover's HDL
  semantics into Rocq", which is what `pass.rocq` does relative to
  `formal/semantic_primitives`.
- **riscv-coq / Bedrock2** (MIT PLV) — a Coq RISC-V ISA formalization used as
  the target of a verified compiler, and the `coq-bbv` bit-vector library that
  Kami and Kôika build on.
- **Sail** — the language behind the official RISC-V reference specification
  adopted by RISC-V International. Sail generates theorem-prover definitions for
  **Isabelle, Rocq, and Lean** (and HOL4), plus a SystemVerilog reference model
  for formal hardware verification. Caveats worth stating: the generated Rocq
  definitions lack bisimulation relations for relating abstraction levels, and
  the Sail→Rocq translation is itself unverified.
- **Islaris** (PLDI 2022) — verification of machine code against authoritative
  ISA semantics, in Coq, with translation validation for Isla traces against the
  Sail-generated Coq model.

So Rocq's hardware story is: *excellent for designing and verifying hardware
written inside Rocq* (Kami, Kôika), *good for compiler-to-hardware correctness*
(Vericert), *serviceable as a Sail/ISA target*.

### ACL2 — the industrial record

ACL2 has the deepest industrial hardware deployment of any of the four:

- **AMD**: the 1995 proof of correctness of the K5 floating-point division
  microcode, then the AMD Athlon's elementary floating-point operations —
  addition, subtraction, multiplication, division, and square root — proved
  compliant with IEEE 754.
- **Centaur Technology**: an ACL2 specification of a subset of the x86
  architecture, validated by routinely running millions of tests against real
  Intel, AMD, and Centaur silicon; and verification of the media unit of a
  64-bit x86-compatible processor implementing 100+ instructions, including a
  floating-point adder doing four add/subtract pairs per cycle at two-cycle
  latency. The supporting tool chain — the **VL** Verilog toolkit and the **SV**
  symbolic-vector hardware library — ships in the ACL2 Community Books.
- **`x86isa`** — a large formal x86 model used for verifying instruction
  implementations.

The method is: model the RTL as ACL2 functions, state the spec as ACL2
functions, and discharge with GL/FGL bit-blasting plus the waterfall for the
inductive/structural parts. This is the closest thing in this review to what
LiveHD is doing, and it is the strongest argument that a fourth pass
(`pass.acl2`) would also be worth having.

### Isabelle/HOL

- **seL4** — the flagship, and though it is an OS kernel rather than hardware,
  it is the reason Isabelle is trusted for machine-checked systems work at
  scale.
- **Sail → Isabelle** ISA models for ARMv8-A, RISC-V, and CHERI-MIPS.
- `Word_Lib` is a mature, heavily exercised bit-vector library.

### Lean 4

The newest entrant. Strong `BitVec` + `bv_decide` (§5, §8), a Sail Lean backend,
and rapidly growing tooling — but no hardware verification result yet comparable
to Kami, seL4, or the AMD/Centaur work. LiveHD's own `pass.lean` is, in that
sense, early-adopter work.

### Summary

| | Hardware DSLs *in* the prover | Industrial silicon record | ISA models |
|---|---|---|---|
| **Rocq** | **Kami, Kôika, Fe-Si, Vericert** | research/FPGA | riscv-coq, Sail→Rocq, Islaris |
| **ACL2** | VL/SV (Verilog toolkit) | **AMD K5/Athlon, Centaur x86** | `x86isa` |
| **Isabelle** | — | seL4 (software) | Sail→Isabelle (ARMv8, RISC-V, CHERI) |
| **Lean 4** | — | — | Sail→Lean |

---

## 8. Bit-vector libraries

| System | Canonical type | Width location | Computable? | Automation |
|---|---|---|---|---|
| **Rocq** | *none in stdlib.* `Bvector` is a `Vector bool` (poor for computation); `coq-bbv` `word n`; mathcomp-word / `ssrbit` | type (dependent) | bbv: yes | CoqQFBV, SMTCoq |
| **Isabelle** | `'n word` (HOL-Word / `Word_Lib`) | type (numeral type) | yes, via code gen | `word_bitwise`, `smt` |
| **Lean 4** | **`BitVec w`** — built in, `Fin (2^w)` internally | type (dependent, `w : Nat`) | yes, fast | **`bv_decide`**; full SMT-LIB 2.7 QF_BV coverage incl. overflow predicates; width-independent reasoning API |
| **ACL2** | plain integers + `unsigned-byte-p`/`signed-byte-p` predicates, `bitops` books | **hypotheses, not types** | natively | GL/FGL |

Two observations that drove this pass's design:

1. **Rocq is the only one of the four with no obvious default.** Isabelle has
   `'n word`, Lean has `BitVec`, ACL2 has "integers plus predicates". Rocq
   forces a choice among `coq-bbv` (dependent `word n`; Kami/Kôika's choice;
   battle-tested for hardware, but full width-transport pain),
   mathcomp-word/`ssrbit` (strong algebra, drags in mathcomp and imposes
   SSReflect style on the whole stack), or rolling one's own.
2. **The certificate layer wants the width in the *value*, not the type.** Both
   `pass.isabelle` and `pass.lean` already learned this: the fast model uses a
   width-typed vector (`'n word` / `BitVec w`), while the reified certificate
   uses a runtime-width bignum record (`BV nat int` / `structure BV`). Note that
   the certificate representation is, in effect, *the ACL2 representation* —
   integers with the width carried alongside — and it is chosen for the same
   reason ACL2 scales: no width transport, uniform automation.

Interesting cross-system datapoint on the two Rocq bit-blasters: Coq's
bit-vector theory (in the CoqQFBV lineage) defines operations through bit
manipulation rather than integer operations, which suits proving a bit-blaster
correct but is slower to *execute* than an integer-backed representation. For a
certificate *evaluator* — which is what this pass needs — integer-backed is the
right call.

---

## 9. Engineering at scale

Concrete numbers from LiveHD's existing passes, which set the bar:

| Design | Emitted | Prover cost |
|---|---|---|
| DINO `SingleCycleCPU` | ~19k lines Lean | model+cert typechecks in ~3 min, ~6 GB |
| DINO `PipelinedCPU` | ~20k lines Lean | ~3.7 min, ~6.5 GB |
| DINO fast-view bridge | 4772 nodes | ~280 ms/node |

Lessons that transfer:

- **Separate compilation is a genuine Rocq advantage.** Rocq compiles each `.v`
  to a cached `.vo`. Lean elaborates a file as one unit, which is why
  `pass.lean` emits a single `<Top>_Lgraph.lean` and why "reduce the single-file
  typecheck cost via a file split" is still on its TODO. `pass.isabelle` already
  splits model from certificate into two theories precisely so a certificate
  failure does not force re-elaborating the expensive model — and its run script
  goes further, generating two Isabelle *sessions*. **`pass.rocq` should split
  from day one**, not retrofit.
- **Monolithic definitions are single-threaded.** `pass.isabelle` measured 107 s
  + 83 s on DINO for one monolithic `definition`, which is why the
  `emit_fast_bridge` mode factors each node into its own top-level definition.
  Rocq has the same property: one giant `Definition` is one elaboration task.
- **Reduction blowup is the scaling risk, not proof search.** Per §6, a stuck
  reduction produces enormous terms. On generated code the mitigation is
  discipline in the support library: total, closed, computable primitives with
  no axioms on the evaluation path.
- **`Qed` is not free.** Rocq type-checks the completed proof term at `Qed`.
  For thousands of tiny generated lemmas the aggregate cost is real; `Defined`
  (transparent) is faster to close but leaks the proof term into later
  reduction — which, given §6's warning about `Opaque`, needs care.

---

## 10. Rocq for LiveHD: honest pros and cons

### Pros

1. **Reflection is the native idiom, and this pass is a reflection problem.**
   A graph certificate evaluator proved sound once and then *run* on each design
   is exactly the pattern `ring`/`lia`/SMTCoq use. Rocq has the deepest tooling
   and the strongest cultural fit for it.
2. **`vm_compute` gives a fast evaluator without enlarging the TCB.** Unlike
   Isabelle `eval` (ML compiler) and Lean `native_decide` (Lean compiler),
   Rocq's *default* fast path is the bytecode VM, which is a much smaller
   addition than a full native compiler. `native_compute` remains available when
   speed matters more than trust.
3. **Separate `.vo` compilation** matches the two-file (model / certificate)
   emission strategy and caches across certificate iterations (§9).
4. **The richest hardware-formalization precedent** — Kami, Kôika, Vericert,
   riscv-coq — means design decisions have prior art, and that a future
   `pass.rocq` output could plausibly be related to a Kami/Kôika model rather
   than living in isolation.
5. **Vericert is a direct precedent for porting HDL semantics into Coq from
   another prover** (HOL4 → Coq), which is structurally what
   `formal/rocq` does relative to `formal/semantic_primitives`.
6. **Extraction** is mature: a verified certificate *checker binary* is a
   realistic future step, not a research project.
7. **Third-prover diversity has real value.** A semantic bug that is
   independently reproduced in three unrelated logics is far more convincing
   than one confirmed in a single stack — and, as `BRIDGE_BUGS.md` records, the
   `Get_mask(a,-1)` bug was in fact a *shared* defect found across the Isabelle
   and Lean passes.

### Cons

1. **No canonical bit-vector library.** Isabelle and Lean each hand you one;
   Rocq makes you choose or build. This pass builds one (§11), which is code
   LiveHD now owns and must maintain.
2. **Dependent-width friction.** If widths live in types, width-changing lemmas
   need transport along equality proofs. This is the classic Kami/Kôika tax.
3. **Weaker out-of-the-box bit-level automation than the alternatives.** Lean's
   `bv_decide` is reported to beat CoqQFBV; ACL2's GL/FGL is purpose-built for
   this and industrially proven. Rocq's advantage is *not* automation strength.
4. **`native_compute` trust cost** and the `Opaque`-defying behaviour of both
   fast reducers (§6) are sharp edges on generated code.
5. **Ltac1 brittleness.** Any hand-written tactic in the support library is a
   maintenance liability; generated tactic scripts even more so.
6. **Ecosystem fragmentation.** Four tactic languages and three plausible
   bit-vector libraries is more choice than Isabelle or Lean present, and the
   library ecosystem is less unified than mathlib.
7. **The rename is recent.** Documentation, package names, and search results
   still say "Coq"; tooling and CI examples lag. Minor, but real friction.

### Where Rocq lands relative to the others, for *this* job

- **vs Lean**: Lean wins on bit-vector automation and on tactic-language
  coherence. Rocq wins on separate compilation, reflection maturity,
  extraction, and hardware precedent.
- **vs Isabelle**: Isabelle wins on sledgehammer productivity and on
  counterexample finding (Nitpick/Quickcheck are genuinely missed elsewhere).
  Rocq wins on reflection, on proof terms as objects, and on a smaller fast-eval
  TCB.
- **vs ACL2**: ACL2 wins decisively on *industrial bit-level datapath*
  automation and on the absence of any width-typing problem. Rocq wins on
  expressiveness (parametric/modular hardware, higher-order specs) and on
  foundational trust. These are close to orthogonal, which is the case for
  having both.

---

## 11. Design implications for `pass.rocq`

Every choice below traces to a section above.

| Decision | Rationale |
|---|---|
| **Self-contained, Z-backed bit vectors** in `formal/rocq` rather than `coq-bbv` or mathcomp-word | §8: no canonical stdlib type; bbv's dependent `word n` brings width transport (§3) we do not need, and mathcomp imposes SSReflect on the whole stack. Integer-backed executes faster under `vm_compute` than bit-manipulation-backed (§8). Also keeps opam deps at exactly one package. |
| **Width in the type for the fast model** — `Record BitVec (w : nat) := mkBitVec { bv_raw : Z }` | A *parameterised record* makes `BitVec 4` and `BitVec 8` distinct types (catching emitter width bugs at `rocq c` time, matching what `'n word` and `BitVec w` give the other two passes) with **zero** dependent proof obligations — no subset type, no transport. This sidesteps con #2. |
| **Width in the value for the certificate** — `Record BV := mkBV { bv_width : nat; bv_value : Z }` | §8, observation 2: this is the ACL2 representation, chosen for the same reason — uniform, transport-free, width-agnostic reasoning. Mirrors `pass.lean`'s `BV` and `pass.isabelle`'s `datatype bv = BV nat int` exactly, so the three certificates stay comparable. |
| **Every primitive total, closed, and computable; no axioms on the evaluation path** | §6: stuck reduction produces enormous terms and exhausts memory. Also matches the existing rule in `pass/isabelle/README.md` that primitives must be total and width-explicit with no `undefined`. |
| **All operators normalize through `bv_norm`** | Keeps "always normalized" true by construction, so no proof-carrying invariant is needed and `vm_compute` stays on integers. |
| **Two emitted files** — `<Top>_Lgraph.v` (model) and `<Top>_Lgraph_Cert.v` (certificate) | §9: Rocq caches `.vo` per file, so certificate iteration does not re-elaborate the model. Follows `pass.isabelle`'s split rather than `pass.lean`'s single file, because Rocq — unlike Lean — actually has the compilation unit to exploit. |
| **`eval_engine` knob: `vm｜native｜cbv`, default `vm`** | §6: makes the speed/TCB trade explicit per run instead of hard-coded. `vm` is fast without adding the OCaml compiler to the TCB; `native` is available when needed; `cbv` is the minimal-TCB fallback. Neither `pass.isabelle` nor `pass.lean` exposes this axis, and Rocq is the system where all three points are cleanly available. |
| **`evalGraphCorrectForCert` proved once, generically; generated files only *instantiate* it** | §4: a code generator emits thousands of near-identical obligations, which is the reflection profile, not the creative-tactic profile. Keeps hand-written tactics confined to one small library. |
| **Reflection over tactic scripts wherever there is a choice; definitional equality (`reflexivity`) for the cert-model bridge** | §4 con #5 and §6: avoid Ltac1 in generated output entirely. |
| **`max_width` default 1024, `0` = unlimited** | Same reasoning as `pass.lean`: a proof-tractability guard, not a LiveHD limit; §6's reduction-blowup risk is the actual constraint. |
| **Defer `OpBridge` / fast-view bridge to a later cut** | §9's measured ~280 ms/node on the Lean side says the bridge is the expensive part; the model+certificate layer is independently useful and independently checkable. |

### What this review does *not* claim

It does not claim Rocq is the best of the four for LiveHD. On automation
strength for bit-level goals, Lean (`bv_decide`) and ACL2 (GL/FGL) are ahead. The
case for Rocq is: reflection fit, a smaller fast-evaluation TCB, separate
compilation, the deepest hardware-formalization precedent, and — most
practically — **independent confirmation in a third, unrelated logic**, which is
worth more than any single system's convenience.

---

## 12. References

**Rocq / Coq**
- Rocq Prover 9.0.0 Release Notes — https://rocq-prover.org/releases/9.0.0
- Rocq Prover reference manual, recent changes — https://rocq-prover.org/refman/changes.html
- Ltac2 documentation — https://rocq-prover.org/doc/V8.19.0/refman/proof-engine/ltac2.html
- Ltac documentation — https://rocq-prover.org/doc/V8.18.0/refman/proof-engine/ltac.html
- Rocq — Wikipedia — https://en.wikipedia.org/wiki/Rocq
- "Using Coq's evaluation mechanisms in anger" (Gagallium) — https://gallium.inria.fr/blog/coq-eval/
- `native_compute` documentation issue — https://github.com/coq/coq/issues/7846
- "`vm_compute` and `native_compute` defy `Opaque`" — https://github.com/coq/coq/issues/4476
- "Speeding Up Proofs with Computational Reflection", G. Malecha — https://gmalecha.github.io/reflections/2017/speeding-up-proofs-with-computational-reflection/
- "Towards a Scalable Proof Engine: A Performant Prototype Rewriting Primitive for Coq" — https://arxiv.org/pdf/2305.02521
- "Mtac2: typed tactics for backward reasoning in Coq", PACMPL — https://dl.acm.org/doi/10.1145/3236773
- "Small Scale Reflection for the Working Lean User" (LeanSSR) — https://arxiv.org/abs/2403.12733

**Hardware verification in Coq/Rocq**
- "Kami: a platform for high-level parametric hardware specification and its modular verification", ICFP 2017 — https://dl.acm.org/doi/10.1145/3110268 · http://plv.csail.mit.edu/kami/papers/icfp17.pdf
- "The essence of Bluespec: a core language for rule-based hardware design" (Kôika), PLDI 2020 — https://dl.acm.org/doi/10.1145/3385412.3385965
- Kôika repository — https://github.com/mit-plv/koika
- "Formal verification of high-level synthesis" (Vericert), OOPSLA 2021 — https://dl.acm.org/doi/10.1145/3485494 · https://johnwickerson.github.io/papers/vericert_oopsla21.pdf
- Vericert repository — https://github.com/ymherklotz/vericert
- riscv-coq (MIT PLV) — https://github.com/mit-plv/riscv-coq
- `coq-bbv` (Bedrock bit vectors) — https://github.com/mit-plv/bbv
- "Islaris: Verification of Machine Code Against Authoritative ISA Semantics", PLDI 2022 — https://www.cl.cam.ac.uk/~pes20/2022-pldi-islaris.pdf
- "Interaction Tree Semantics for RISC-V: Bridging Compiler and Hardware Verification" — https://arxiv.org/pdf/2605.04933
- Sail RISC-V model — https://github.com/riscv/sail-riscv
- "A Multipurpose Formal RISC-V Specification Without Creating New Tools" — https://people.csail.mit.edu/bthom/riscv-spec.pdf

**Bit vectors and SMT integration**
- "CoqQFBV: A Scalable Certified SMT Quantifier-Free Bit-Vector Solver", CAV 2021 — https://yfu.tw/publication/cav21-coq-qfbv/cav21-coq-qfbv.pdf
- "Extending SMTCoq, a Certified Checker for SMT" — https://arxiv.org/pdf/1606.05947
- "Verifying Bit-vector Invertibility Conditions in Coq", PxTP 2019 — https://homepage.cs.uiowa.edu/~tinelli/papers/EkiEtAl-PxTP-19.pdf
- "Formal Verification of Bit-Vector Invertibility Conditions in Coq", FroCoS 2023 — https://dl.acm.org/doi/10.1007/978-3-031-43369-6_3
- Lean 4 BitVec library — https://deepwiki.com/leanprover/lean4/5.2-bitvector-library
- "Interactive Bitvector Reasoning using Verified Bit-Blasting" (`bv_decide`) — https://www.researchgate.net/publication/396384351_Interactive_Bitvector_Reasoning_using_Verified_Bit-Blasting
- "Reconstruction of Z3's Bit-Vector Proofs in HOL4 and Isabelle/HOL" — https://www.researchgate.net/publication/220818741
- "Fine-grained SMT proofs for the theory of fixed-width bit-vectors", LPAR 2015 — https://homepage.cs.uiowa.edu/~tinelli/papers/HadEtAl-LPAR-15.pdf

**ACL2**
- "Industrial hardware and software verification with ACL2", Phil. Trans. R. Soc. A 375(2104), 2017 — https://royalsocietypublishing.org/doi/10.1098/rsta.2015.0399
- "Use of Formal Verification at Centaur Technology" — https://link.springer.com/chapter/10.1007/978-1-4419-1539-9_3
- "Centaur Technology Media Unit Verification", CAV 2009 — https://link.springer.com/chapter/10.1007/978-3-642-02658-4_28
- "Verifying x86 Instruction Implementations" — https://arxiv.org/pdf/1912.10285
- ACL2 hardware-verification manual page — https://www.cs.utexas.edu/~moore/acl2/manuals/current/manual/index-seo.php/ACL2____HARDWARE-VERIFICATION
- ACL2 proof-automation manual page — https://acl2.org/doc/index-seo.php?xkey=ACL2____PROOF-AUTOMATION
- "Verified AIG Algorithms in ACL2" — https://arxiv.org/abs/1304.7861
- "ACL2 Induction Heuristics", J S. Moore — https://www.cs.utexas.edu/~moore/publications/acl2-induction-heuristics.pdf
- "The Boyer-Moore Waterfall Model Revisited" — https://arxiv.org/pdf/1808.03810
- "Balancing Automation and Control for Formal Verification of Microprocessors", CAV 2021 — https://link.springer.com/chapter/10.1007/978-3-030-81685-8_2
- "Formal Verification of an Iterative Low-Power x86 Floating-Point Multiplier with Redundant Feedback" — https://arxiv.org/pdf/1110.4675

**Isabelle/HOL and cross-system comparison**
- "From LCF to Isabelle/HOL", L. Paulson — https://arxiv.org/pdf/1907.02836
- "Automatic Proof and Disproof in Isabelle/HOL" (Sledgehammer, Nitpick, Quickcheck) — https://www.tcs.ifi.lmu.de/staff/jasmin-blanchette/frocos2011-dis-proof.pdf
- "Comparison of Two Theorem Provers: Isabelle/HOL and Coq" — https://arxiv.org/pdf/1808.09701
- "Why not just use Lean?", L. Paulson — https://lawrencecpaulson.github.io/2026/04/23/Why_not_Lean.html
- "Interactive Theorem Proving and the Lean Theorem Prover", J. Avigad — https://www.andrew.cmu.edu/user/avigad/Talks/wuhan.pdf
- "QED at Large: A Survey of Engineering of Formally Verified Software" — https://arxiv.org/pdf/2003.06458
- "ISA semantics for ARMv8-A, RISC-V, and CHERI-MIPS", POPL 2019 — https://www.researchgate.net/publication/330153637

**In-repo prior art (read these before changing the emitter)**
- `pass/isabelle/BRIDGE_BUGS.md` — the fast-model ↔ certificate operand-width bug class, five worked postmortems, the `eval` vs `code_simp` measurement, and the φ/lookup representation bake-off.
- `pass/lean/STEP5_BRIDGE_BUGS.md` — nine further postmortems, most of them prover-independent in kind.
- `formal/translation_correctness/README.md` — the Strategy B design rationale.
