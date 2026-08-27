/-
# Runtime input, state and result

Shared by both sides of `compileDesign_correct`: the source interpreter
(`interpretDesign`) and the target executor (`denoteResidual`) consume and
produce exactly these.

The value domain is `CertVal = bv | mem`, reused deliberately.  What must be
independent between the two sides is the *semantics functions*, not the value
type — the two sides have to produce comparable values or the equation cannot
even be stated.
-/
import LeanSemanticPrimitives.Compiler.DesignCert

namespace Compiler

/-- Primary inputs, positionally. -/
abbrev RuntimeInput := Array BV

/-- Sequential state: flop values and memory images, positionally. -/
structure RuntimeState where
  flops : Array BV
  mems  : Array (Int → BV)
deriving Inhabited

structure RuntimeResult where
  nextState : RuntimeState
  outputs   : Array BV
deriving Inhabited

/-- Slot-space environment.  An `Array`, not a `Nat → CertVal`: lookup must be
O(1) because `denoteResidual` actually runs.  (The *interpreter* may use the
nested `Nat → V` environment — it is only ever reasoned about, via
`evalGraphG_char`, which materialises no lookups.) -/
abbrev SlotEnv := Array CertVal

@[reducible] def badVal : CertVal := .bv (mk_bv 0 0)

/-- O(1) slot read.  Out-of-range is a defined, obviously-wrong value rather
than a partial function; `SlotsInRange` rules it out for well-formed designs. -/
def denoteRef (env : SlotEnv) (r : Nat) : CertVal :=
  env[r]?.getD badVal

/-- What a source slot reads at the start of a cycle. -/
def sourceValue (i : RuntimeInput) (s : RuntimeState) : SourceDesc → CertVal
  | .input  idx w    => .bv (bv_resize w (i[idx]?.getD (mk_bv w 0)))
  | .const  w v      => .bv (mk_bv w v)
  | .flopQ  idx w    => .bv (bv_resize w (s.flops[idx]?.getD (mk_bv w 0)))
  | .flopQAsync idx w ri rv al =>
      let r : BV := i[ri]?.getD (mk_bv 1 0)
      let asserted : Bool := if al then !(bv_nonzero r) else bv_nonzero r
      .bv (if asserted then mk_bv w rv else bv_resize w (s.flops[idx]?.getD (mk_bv w 0)))
  | .memImg idx _ _  => .mem (s.mems[idx]?.getD (fun _ => mk_bv 0 0))

/-- The initial slot environment: every source slot, in order. -/
def sourceEnvArr (srcs : Array SourceDesc) (i : RuntimeInput) (s : RuntimeState) : SlotEnv :=
  srcs.map (sourceValue i s)

theorem sourceEnvArr_size (srcs : Array SourceDesc) (i : RuntimeInput) (s : RuntimeState) :
    (sourceEnvArr srcs i s).size = srcs.size := by
  simp [sourceEnvArr]

end Compiler
