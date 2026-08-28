/-
  The object language `L` for the Futamura projections.

  `mix` must INSPECT the program it specializes, and for the second projection it
  must inspect *itself*.  A Lean `def` can be applied but not pattern-matched on,
  so programs have to be an inductive datatype: a deep embedding.

  Two design decisions dominate this file.

  SELF-REPRESENTATION BY GÖDEL ENCODING (not a mutual inductive).  The tempting
  alternative puts `Term` inside `Val` (`| term : Term → Val`).  That is not
  blocked by Lean -- mutual+nested compiles and structural recursion over it
  works -- but `mixProgram` runs *inside* this language, so it cannot pattern
  match on a Lean inductive.  Every inspection would need a primitive: one tag
  accessor, fifteen field accessors, eight builders.  ~24 primitives, each
  costing an `evalPrim` case and a correctness lemma.  And the moment you make
  that economical -- "one primitive that turns a Term into a tagged tree, one
  that turns it back" -- you have written the encoding as a primitive pair
  instead of a function.  So: `Val` carries ONE generic tagged constructor
  (`ctor`), and `Encoding.lean` maps `Term`/`Program` into it.  `mixProgram` then
  destructures encoded programs with the language's ordinary `caseT`, exactly as
  a Lisp `mix` destructures S-expressions.

  FIRST ORDER.  No `lam`, no `app`, no closures.  Recursion is by `call` into a
  function table.  Higher-order binding-time analysis needs closure analysis, and
  every higher-order feature makes self-application harder.
-/

namespace Projection

/-! ## Values -/

/-- Object-language values.

`ctor` is the self-representation vehicle: `Term`, `Program`, annotated terms,
errors and residual programs all encode into tagged trees built from it.

Deliberately NOT carrying `Term`: see the file header. -/
inductive Val where
  | int  : Int → Val
  | bool : Bool → Val
  | nil  : Val
  | cons : Val → Val → Val
  | ctor : Nat → List Val → Val
  deriving Inhabited, Repr

/-! ### Decidable equality, by hand

`deriving DecidableEq` does not apply here -- the `List Val` field makes `Val` a
*nested* inductive, and no deriving handler covers it.  (This is a cost of the
nesting, not of any choice about self-representation: the mutual-inductive design
fails the same way.)  `mix`'s memo table is keyed on static `Val` arguments, so
decidable equality is load-bearing rather than a convenience. -/

mutual

def Val.beq : Val → Val → Bool
  | .int a,      .int b      => a == b
  | .bool a,     .bool b     => a == b
  | .nil,        .nil        => true
  | .cons a b,   .cons c d   => Val.beq a c && Val.beq b d
  | .ctor t as,  .ctor u bs  => t == u && Val.beqList as bs
  | _,           _           => false

def Val.beqList : List Val → List Val → Bool
  | [],      []      => true
  | a :: as, b :: bs => Val.beq a b && Val.beqList as bs
  | _,       _       => false

end

instance : BEq Val := ⟨Val.beq⟩

/-! ## Primitives

Closed and first-order.  Every primitive costs an `evalPrim` case and a semantic
correctness lemma -- but NOT a `mix` case: specialization of `prim` is one
generic rule (`PartialEvaluator.lean`), so extending this set never touches the
specializer or its structural proof.

Note `bvAnd`/`bvOr`/`bvXor` rather than a single `bvBitwise`: `LGraphModel`'s
`bv_bitwise (w) (f : Bool → Bool → Bool) (a b : BV)` takes a *function*, and a
first-order language cannot pass one. -/
inductive Prim where
  -- integers
  | addI | subI | mulI | divI | modI
  | ltI  | leI  | eqI
  -- booleans
  | andB | orB | notB
  -- lists.  `consP` is the only way to BUILD one: `ctorT` has a static field
  -- count, so variable-length data has to be a cons chain, and a chain that
  -- cannot be extended is useless.
  | isNil | hd | tl | consP
  -- structural equality on values, which `mix`'s memo table is keyed on and
  -- which `eqI` (integers only) cannot provide
  | eqV
  -- bit vectors, as LGraphModel's BV = ⟨width, value⟩
  | bvMk | bvWidth | bvUint | bvBit
  | bvAnd | bvOr | bvXor | bvNot
  | bvResize
  deriving DecidableEq, Inhabited, Repr

/-- Arity of a primitive.  `evalPrim` rejects any other operand count, so this is
the single source of truth for primitive shape. -/
def Prim.arity : Prim → Nat
  | .addI | .subI | .mulI | .divI | .modI => 2
  | .ltI  | .leI  | .eqI                  => 2
  | .andB | .orB                          => 2
  | .notB                                 => 1
  | .isNil | .hd | .tl                    => 1
  | .consP | .eqV                         => 2
  | .bvMk                                 => 2
  | .bvWidth | .bvUint                    => 1
  | .bvBit                                => 2
  | .bvAnd | .bvOr | .bvXor               => 3   -- width, a, b
  | .bvNot                                => 2   -- width, a
  | .bvResize                             => 2   -- width, a

/-! ## Terms -/

/-- Object-language terms.  de Bruijn indices; `var 0` is the innermost binder.

Binders: `letIn` binds one, a `caseT` alternative binds its `arity` fields, and a
function body binds its parameters (`FunDef.arity`, outermost first).

A `caseT` alternative is `(tag, arity, body)` as a plain tuple rather than a
named structure.  A `structure Alt` would have to sit in a `mutual` block with
`Term` (each mentions the other), and mutual+nested induction buys nothing here --
`Term` stays a simple nested inductive this way, which keeps structural recursion
and the `mix_sound` induction straightforward.  `Alt.tag`/`arity`/`body` below
restore the readable names. -/
inductive Term where
  | lit   : Val → Term
  | var   : Nat → Term
  | letIn : Term → Term → Term
  | ite   : Term → Term → Term → Term
  | prim  : Prim → List Term → Term
  | ctorT : Nat → List Term → Term
  | caseT : Term → List (Nat × Nat × Term) → Term
  | call  : Nat → List Term → Term
  deriving Inhabited, Repr, BEq

/-- One branch of a `caseT`: `(tag, arity, body)`.

`arity` is what makes `caseT` a *binder*: matching `ctorT tag [v₀ … vₙ₋₁]`
against an alternative with that tag extends the environment with those `n`
fields before evaluating `body`.  Without it a branch could not name what it
matched. -/
abbrev Alt := Nat × Nat × Term

@[inline] def Alt.tag   (a : Alt) : Nat  := a.1
@[inline] def Alt.arity (a : Alt) : Nat  := a.2.1
@[inline] def Alt.body  (a : Alt) : Term := a.2.2

/-- A named function.  `body` is evaluated in an environment holding exactly
`arity` arguments, argument 0 outermost. -/
structure FunDef where
  arity : Nat
  body  : Term
  deriving Inhabited, Repr, BEq

/-- A program: a function table plus an entry index.

`List`, not `Array`: an array encodes fine via `toList`, but every proof about
the function table would then drag in `Array` lemmas for no benefit.  If lookup
speed matters it is a host-side concern, not a semantic one. -/
structure Program where
  funs  : List FunDef
  entry : Nat
  deriving Inhabited, Repr, BEq

/-- Environments are finite lists, innermost binding first (de Bruijn order).

Deliberately NOT `Nat → Val`: a function is not encodable into `Val`, and the
object specializer must be able to carry environments as data. -/
abbrev Env := List Val

@[inline] def Env.lookup (ρ : Env) (i : Nat) : Option Val := ρ[i]?

/-- Function-table lookup. -/
@[inline] def Program.fn (P : Program) (i : Nat) : Option FunDef := P.funs[i]?

end Projection
