# formal/rocq — the Rocq support library for `pass.rocq`

The Rocq counterpart of `formal/semantic_primitives` + `formal/translation_correctness`
(Isabelle) and `formal/lean/LeanSemanticPrimitives` (Lean 4).

Self-contained: the only dependency is `rocq-prover` itself. No `coq-bbv`, no
mathcomp, no opam packages beyond the prover.

## Layout

| File | Lines | Role |
|---|---|---|
| `theories/SemanticPrimitives.v` | ~290 | `BitVec w` and the total, width-explicit primitives the fast model calls |
| `theories/Translation/LGraphModel.v` | ~700 | `LGraphOp`, `BV`, `NodeCert`, `GraphCert`, `denote_op`/`eval_op`, `evalGraph`, and the keystone `evalGraphCorrectForCert` |
| `theories/SemanticPrimitives_Test.v` | ~300 | ~60 computational regressions, all closed by `vm_compute; reflexivity` |
| `_CoqProject`, `Makefile` | | `rocq makefile` wrapper |

## Build

```bash
eval "$(opam env)"          # rocq must be on PATH
make -C formal/rocq         # -> theories/**/*.vo
make -C formal/rocq clean
```

There are no `admit`s and no axioms. `make` is the gate:

```bash
! grep -rn "Admitted\|admit\." formal/rocq/theories
```

## Two bit-vector types, on purpose

```coq
Record BitVec (w : nat) : Set := mkBitVec { bv_raw : Z }.   (* fast model  *)
Record BV : Set := mkBV { bv_width : nat; bv_value : Z }.   (* certificate *)
```

- **`BitVec w`** carries the width in the TYPE. It is a *parameterised* record,
  so `BitVec 4` and `BitVec 8` are distinct types and the type checker catches
  emitter width bugs — but the parameter is phantom, so there are no dependent
  proof obligations and no transport along width equalities. That transport is
  the classic tax of `coq-bbv`-style `word n`, and this design does not pay it.
- **`BV`** carries the width in the VALUE. That is deliberately the ACL2
  representation (an integer plus a width predicate), chosen for the same
  reason ACL2 scales on bit-level goals: no width transport, uniform reasoning.
  It mirrors Lean's `structure BV` and Isabelle's `datatype bv = BV nat int`, so
  the three certificates stay comparable.

Accessors on `BV` are prefixed `bvc_` (`bvc_uint`, `bvc_sint`, `bvc_bit`, …)
to keep them distinct from the `bv_` operations on `BitVec w`. Correspondence:

| Rocq | Lean | Isabelle |
|---|---|---|
| `BV`, `bvc_uint` | `BV`, `bv_uint` | `bv`, `bv_uint` |
| `BitVec w`, `bv_uint` | `BitVec w`, `.toNat` | `'n word`, `unat` |

## Invariants the emitter relies on

1. **Everything is total.** No axioms, no `False_rect`, no partiality anywhere on
   the evaluation path. Division by zero yields 0; an unexpected argument arity
   yields `mk_bv w 0`. This matters more in Rocq than in Isabelle or Lean: a
   *stuck* reduction here does not fail, it builds an enormous partially
   evaluated term and exhausts memory.

2. **Normalised by construction.** Every operator returns through `bv_norm`, and
   every read goes through `bv_uint`, which normalises defensively. So
   `bv_uint_range : 0 <= bv_uint x < two_pow w` is unconditional.

3. **Shift and index amounts are clamped before exponentiating.** `clamp_shift`
   bounds a shift at the word width before computing `2 ^ n`. The clamp is
   semantically the identity (anything shifted out is 0 mod 2^w) but it is what
   stops a 2^31 shift amount from trying to build a two-billion-bit integer.
   `Op_MuxN` and `Op_Sext` guard in `Z` for the same reason and never call
   `Z.to_nat` on an unbounded value.

## Node ids are `N`, not `nat`

The sharpest place Rocq differs from its two sibling stacks.

Lean's `Nat` is a GMP-backed binary bignum and Isabelle's `nat` is a
code-generator abstraction, so both carry a LiveHD node id of 2 000 000 000 for
free. **Rocq's `nat` is genuinely unary.** The literal `2000000000 : nat`
elaborates through `Nat.of_num_uint`, and any reduction that forces it builds a
term with two billion `S` constructors. Certificate ids are compared on every
evaluator step, so `NodeCert.nc_nid`, `nc_deps`, `gc_topo`, `gc_sources` and the
environment domain are all `N` (binary), where a literal is O(log v) and `N.eqb`
is O(log v).

Consequence for generated code: every id list must carry an explicit `%N`
delimiter. Generated files open `Z_scope`, and Rocq does **not** push a record
field's element scope into a list literal, so a bare `[1; 2]` in an `nc_deps`
position elaborates as `list Z` and the file does not compile.
`pass/rocq/scripts/op_census.py` checks this.

Widths stay `nat`: they index `BitVec` (a phantom parameter that is never
reduced) and are only forced by `Z.of_nat` inside `two_pow`, bounded by the
pass's `max_width` (1024 by default).

## The keystone

```coq
Theorem evalGraphCorrectForCert : forall (G : GraphCert) (sourceEnv : N -> BV),
  envCorrectOn (gc_topo G)
    (evalGraph (gc_topo G) G sourceEnv)
    (graphDenotation (gc_topo G) G sourceEnv).
```

*Evaluated certificate = mathematical certificate semantics*, proved once. A
generated `<Top>_Lgraph_Cert.v` only instantiates it — it never re-proves any of
this. `eval_op` and `denote_op` have identical bodies and are kept as two
definitions on purpose so generated lemmas can name each side; `eval_op_correct`
closes that gap by conversion (`reflexivity`).

## Adding a new primitive

Same rule as the Isabelle and Lean stacks (see `pass/isabelle/README.md`):

1. Add the primitive here, total and width-explicit.
2. Add a `vm_compute` regression to `SemanticPrimitives_Test.v` — including the
   corner cases that have historically bitten: mux polarity, non-contiguous
   `Get_mask`/`Set_mask`, the all-ones mask zero-extend idiom, arithmetic-shift
   sign preservation, signed vs unsigned compare and divide, reset priority,
   enable behaviour, and constants wider than 64 bits.
3. Extend `LGraphOp` + `denote_op` + `eval_op` **and** the certificate emitter in
   the same change; the executable model and the certificate must expose the
   same semantics.
4. Add a fast-model-vs-certificate agreement example (the `agree_*` group in the
   test file). That pairing is the empirical form of the bridge theorem and it
   catches the dominant bug class in this family — the two emitters disagreeing
   on an operand width.
