/-
  Runtime values as object-language data.

  Milestone 1 of `SIMULATOR_PLAN.md`, dynamic half.  `DesignEncoding.lean`
  encodes what `mix` specializes WITH RESPECT TO; this file encodes what the
  residual program is still given at run time -- the primary inputs, the
  sequential state, and the result of one cycle.

  A BIT VECTOR IS ALREADY AN OBJECT VALUE.  `Projection.mkBV` (tag 1000) is
  `ctor bvTag [int w, int v]`, mirroring `LGraphModel.BV = <width, value>` field
  for field, and `bvAnd`/`bvResize`/... already operate on exactly that shape.
  So `encBV` is not a translation, it is a retyping -- `encBV_eq_mkBV` below
  says so by `rfl`.  That is what lets `I_hw` apply the object's own bit-vector
  primitives to encoded runtime values with no marshalling step, which is in
  turn what keeps the residual program free of conversion code.

  MEMORY IS THE ONE THING THAT DOES NOT FIT, and this is where it shows up.
  `RuntimeState.mems : Array (Int -> BV)` is FUNCTION-valued.  `Val` is finite
  first-order data, so a memory-bearing state has no representative here at all
  -- not a lossy one, none.  The plan says to complete the memory-free theorem
  first and not to claim literal state equality for memory-bearing designs until
  function extensionality and a representation bridge have actually been
  discharged, so:

    * the ENCODERS are total and simple.  `encState` drops `mems`.
    * the RELATIONS are the honest objects, and they are stated through the
      DECODER.  `decState` can only produce `mems := #[]`, so `StateRel v s`
      entails `s.mems = #[]` (`StateRel_memFree`) -- a memory-bearing state
      satisfies no `StateRel` whatsoever, and `encState` dropping its memories
      cannot smuggle one through.

  Stating the relations through the decoder also makes them FUNCTIONAL for free
  (`StateRel_functional`): one object value denotes at most one runtime state.
  Milestone 5 needs exactly that -- without it, two tracks could satisfy the
  contract and still disagree, because the same `Val` would mean two things.
-/

import LeanSemanticPrimitives.Projection.DesignEncoding
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics
import LeanSemanticPrimitives.Projection.SimulatorContract

namespace Projection
open Compiler

/-! ## Tags

Continuing the hardware block allocated in `DesignEncoding.lean` (100-115). -/

def tagState  : Nat := 120
def tagResult : Nat := 121

/-! ## Bit vectors -/

/-- Literally the object language's own `BV` injection.  Not an alias for
tidiness: `evalPrim`'s bit-vector cases call `LGraphModel`'s `mk_bv`,
`bv_bitwise`, `bv_not` and `bv_resize` directly, so an encoded runtime `BV` is
accepted by the object primitives with no marshalling at all. -/
@[inline] def encBV (b : BV) : Val := ofBV b

theorem encBV_eq_mkBV (b : BV) : encBV b = mkBV (Int.ofNat b.width) b.value := rfl

@[simp] theorem asBV_encBV (b : BV) : asBV (encBV b) = some b := asBV_ofBV b

/-- The object language's own `asBV`, for the same reason `encBV` is `ofBV`: a
second decoder for the same representation is a second thing that can drift. -/
@[inline] def decBV : Val → Option BV := asBV

@[simp] theorem decBV_encBV (b : BV) : decBV (encBV b) = some b := asBV_ofBV b

/-! ### Why the object's bit operations are the pinned ones

This started as a Milestone 2 obligation and became a change to
`ObjectLanguageSemantics.lean` instead, which is the better place for it.

The object language used to reimplement the bit operations over `(Int, Int)`
pairs, and the reimplementation was not equivalent to `LGraphModel`'s: its
`bvBitAt` masked an operand by the RESULT width where `bv_bit` masks by the
operand's OWN width.  The two therefore disagreed on any bit vector not already
reduced modulo its own width --

```
  evalPrim .bvAnd [.int 4, encBV (mk_bv 4 5), encBV <width := 2, value := 7>]
    ≠  encBV (eval_op .Op_And 4 [mk_bv 4 5, <width := 2, value := 7>])
```

-- because the object saw `7 % 2^4 = 7` where `bv_bit` saw `7 % 2^2 = 3`.
Normalisation is an invariant of the shared semantics, so the divergence was
unreachable through `interpretDesign`; it was perfectly reachable through
`evalPrim`, and every operator bridge below would have had to carry a
normalisation hypothesis to exclude it.

Rather than prove around a fork, the fork was removed: `evalPrim` now calls the
pinned functions.  Every bridge in `OperatorBridge` is `rfl` as a result, and
the whole class of "the object models the operator slightly differently" bugs is
gone rather than excluded.  This is Milestone 0's rule applied one level down --
the object language was the mutable side, so it is the side that moved. -/

@[inline] def encBVs (xs : Array BV) : Val := encArr encBV xs
@[inline] def decBVs (v : Val) : Option (Array BV) := decArr decBV v

@[simp] theorem decBVs_encBVs (xs : Array BV) : decBVs (encBVs xs) = some xs :=
  decArr_encArr decBV_encBV xs

/-! ## Inputs

A bare cons chain, NOT a tagged record -- `RuntimeInput` is `Array BV`, a
positional list and nothing more, and `I_hw` walks it with `isNil`/`hd`/`tl`.
`RuntimeState` and `RuntimeResult` are tagged below because they are records
whose field structure the decoder has to check, and because a state's ABSENT
memory component is something the tag makes visible. -/

@[inline] def encInput (i : RuntimeInput) : Val := encBVs i
@[inline] def decInput (v : Val) : Option RuntimeInput := decBVs v

def InputRel (v : Val) (i : RuntimeInput) : Prop := decInput v = some i

theorem InputRel_encInput (i : RuntimeInput) : InputRel (encInput i) i := by
  simp [InputRel, decInput, encInput]

theorem InputRel_functional {v : Val} {i j : RuntimeInput}
    (h₁ : InputRel v i) (h₂ : InputRel v j) : i = j :=
  Option.some.inj (h₁ ▸ h₂)

/-! ## Record wrappers

`field1`/`field2` strip a tagged record's wrapper and nothing else, so every
decoder below is a chain of `bind`/`map` with no `match` in it.  That is not
cosmetic: a `match` inside a decoder forces `split at h` in every lemma about
it, and `split` names its own hypotheses in an order that shifts when the
definition changes.  With `bind` the same lemmas are `cases hx : <subterm>`,
which names nothing implicitly and does not drift. -/

def field1 (tg : Nat) : Val → Option Val
  | .ctor t [a] => if t = tg then some a else none
  | _           => none

def field2 (tg : Nat) : Val → Option (Val × Val)
  | .ctor t [a, b] => if t = tg then some (a, b) else none
  | _              => none

@[simp] theorem field1_ctor (tg : Nat) (a : Val) : field1 tg (.ctor tg [a]) = some a := by
  simp [field1]

@[simp] theorem field2_ctor (tg : Nat) (a b : Val) :
    field2 tg (.ctor tg [a, b]) = some (a, b) := by
  simp [field2]

/-! ## State

`mems` is dropped by the encoder and forced empty by the decoder; see the file
header for why that asymmetry is the honest arrangement rather than a shortcut. -/

/-- The states this encoding can represent at all. -/
def MemFree (s : RuntimeState) : Prop := s.mems = #[]

instance (s : RuntimeState) : Decidable (MemFree s) := by
  unfold MemFree; infer_instance

@[inline] def encState (s : RuntimeState) : Val := .ctor tagState [encBVs s.flops]

def decState (v : Val) : Option RuntimeState :=
  ((field1 tagState v).bind decBVs).map (fun fs => { flops := fs, mems := #[] })

/-- `StateRel v s` -- object value `v` denotes runtime state `s`. -/
def StateRel (v : Val) (s : RuntimeState) : Prop := decState v = some s

theorem StateRel_encState {s : RuntimeState} (h : MemFree s) : StateRel (encState s) s := by
  simp only [StateRel, encState, decState, field1_ctor, Option.bind_some,
             decBVs_encBVs, Option.map_some, Option.some.injEq]
  cases s with
  | mk flops mems => simp only [MemFree] at h; subst h; rfl

/-- The relation refuses memory even though the encoder drops it silently: no
object value is related to a state with a memory, so `encState` cannot smuggle
a memory-bearing state past `StateRel`. -/
theorem StateRel_memFree {v : Val} {s : RuntimeState} (h : StateRel v s) : MemFree s := by
  simp only [StateRel, decState] at h
  cases hf : (field1 tagState v).bind decBVs with
  | none   => rw [hf] at h; simp at h
  | some a => rw [hf] at h; simp only [Option.map_some, Option.some.injEq] at h
              exact h ▸ rfl

/-- One object value denotes at most one state.  Milestone 5 depends on this:
without it two tracks could both satisfy `StepCorrect` and still disagree,
because the same `Val` would mean two different states. -/
theorem StateRel_functional {v : Val} {s t : RuntimeState}
    (h₁ : StateRel v s) (h₂ : StateRel v t) : s = t :=
  Option.some.inj (h₁ ▸ h₂)

/-! ## Results -/

@[inline] def encResult (r : RuntimeResult) : Val :=
  .ctor tagResult [encState r.nextState, encBVs r.outputs]

def decResult (v : Val) : Option RuntimeResult :=
  (field2 tagResult v).bind (fun so =>
    (decState so.1).bind (fun s' =>
      (decBVs so.2).map (fun o' => { nextState := s', outputs := o' })))

def ResultRel (v : Val) (r : RuntimeResult) : Prop := decResult v = some r

theorem ResultRel_encResult {r : RuntimeResult} (h : MemFree r.nextState) :
    ResultRel (encResult r) r := by
  have hs : decState (encState r.nextState) = some r.nextState := StateRel_encState h
  simp only [ResultRel, encResult, decResult, field2_ctor, Option.bind_some, hs,
             decBVs_encBVs, Option.map_some]

theorem ResultRel_memFree {v : Val} {r : RuntimeResult} (h : ResultRel v r) :
    MemFree r.nextState := by
  simp only [ResultRel, decResult] at h
  cases hp : field2 tagResult v with
  | none    => rw [hp] at h; simp at h
  | some so =>
    rw [hp] at h
    simp only [Option.bind_some] at h
    cases hs : decState so.1 with
    | none    => rw [hs] at h; simp at h
    | some s' =>
      rw [hs] at h
      simp only [Option.bind_some] at h
      cases ho : decBVs so.2 with
      | none    => rw [ho] at h; simp at h
      | some o' =>
        rw [ho] at h
        simp only [Option.map_some, Option.some.injEq] at h
        exact h ▸ StateRel_memFree hs

theorem ResultRel_functional {v : Val} {r q : RuntimeResult}
    (h₁ : ResultRel v r) (h₂ : ResultRel v q) : r = q :=
  Option.some.inj (h₁ ▸ h₂)

/-- A design with no memories produces states this encoding can represent, for
every input and every starting state.

This is the side condition `ResultRel_encResult` asks for, discharged once from
a property of the CERTIFICATE rather than per result value -- `mems` is
`D.memories.map ...`, so it is empty exactly when `D.memories` is, whatever the
graph evaluates to.  Milestone 2's `SupportedByProjection` will contain this
conjunct. -/
theorem interpretDesign_memFree {D : DesignCert} (h : D.memories = #[])
    (i : RuntimeInput) (s : RuntimeState) : MemFree (interpretDesign D i s).nextState := by
  simp [MemFree, interpretDesign, h]

/-! ## Milestone 1 acceptance

The plan's gate: both shared certificates round-trip, and their inputs, states
and results satisfy the runtime relations.  The fixtures are
`SimulatorContract.Acceptance`'s -- deliberately the same ones Milestone 0
accepted, not a parallel format. -/

namespace Acceptance
open Projection.Acceptance

/-! ### Certificates round-trip

`decDesign_encDesign` is general, so these are instances rather than evidence --
but they are the instances the next three milestones actually run on, and a
`DecidableEq` on `DesignCert` makes them executable checks rather than proofs
that could quietly be about a different fixture. -/

deriving instance DecidableEq for Compiler.DesignCert

#guard decDesign (encDesign tinyD) == some tinyD
#guard decDesign (encDesign seqD)  == some seqD

/-! ### Runtime values satisfy the relations

Stated as theorems, since `RuntimeState` and `RuntimeResult` have a
function-valued field and so cannot carry `DecidableEq` at all. -/

theorem tiny_state  : StateRel (encState tinySt) tinySt := StateRel_encState rfl
theorem tiny_input  : InputRel (encInput tinyIn) tinyIn := InputRel_encInput _
theorem seq_state (q : Int) : StateRel (encState (seqSt q)) (seqSt q) := StateRel_encState rfl
theorem seq_input (d en rst : Int) : InputRel (encInput (seqIn d en rst)) (seqIn d en rst) :=
  InputRel_encInput _

theorem tiny_result :
    ResultRel (encResult (interpretDesign tinyD tinyIn tinySt))
              (interpretDesign tinyD tinyIn tinySt) :=
  ResultRel_encResult (interpretDesign_memFree rfl _ _)

theorem seq_result (d en rst q : Int) :
    ResultRel (encResult (interpretDesign seqD (seqIn d en rst) (seqSt q)))
              (interpretDesign seqD (seqIn d en rst) (seqSt q)) :=
  ResultRel_encResult (interpretDesign_memFree rfl _ _)

/-! ### …and the whole chain actually runs

The theorems above are about the relations; these execute them.  A round trip
that only typechecks would be satisfied by an encoding that computes nothing, so
the decoded result is compared against the reference semantics' own numbers --
the same ones `SimulatorContract` pins for `interpretDesign`. -/

private def seqRun (d en rst q : Int) : Option RuntimeResult :=
  decResult (encResult (interpretDesign seqD (seqIn d en rst) (seqSt q)))

#guard (seqRun 5 1 0 0).map (fun r => r.outputs)         == some #[mk_bv 4 0]
#guard (seqRun 5 1 0 0).map (fun r => r.nextState.flops) == some #[mk_bv 4 4]
#guard (seqRun 3 0 0 4).map (fun r => r.nextState.flops) == some #[mk_bv 4 4]
#guard (seqRun 3 1 1 4).map (fun r => r.nextState.flops) == some #[mk_bv 4 0]

-- the flop state survives the round trip as a genuine cycle: 0 -> 4 -> 4
#guard ((refTrace seqD (seqSt 0) [seqIn 5 1 0, seqIn 5 1 0]).map
          (fun r => (decResult (encResult r)).map (fun x => x.outputs)))
       == [some #[mk_bv 4 0], some #[mk_bv 4 4]]

end Acceptance
end Projection
