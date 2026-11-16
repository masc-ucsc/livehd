/-
# `MemAddr` — every memory address stays inside the memory's real depth

A memory's table is `2 ^ aw` words (`aw = ceil_log2 depth`), and the step
semantics keep it as a total `Int → BV`. When the depth is not a power of two,
the words from `depth` to `2 ^ aw - 1` exist in the table but not in the
hardware: a 12-entry array has `aw = 4`, and words 12..15 are not storage. A
certificate whose step could write word 13 and read it back would simulate a
memory the RTL does not have.

`pass.bitwidth` refuses such a design (`mem-address-overflow`): every Memory
port address must be proven inside `[0, size)`, and a front end bounds an index
that is not by the clamp `addr < size ? addr : 0`. This module checks the same
fact again on the CERTIFICATE, once, when it is loaded (`CertIO.certWfError`),
so a certificate that was written without that proof (`--set sim.debug=true`)
or edited later is refused instead of simulated.

## The check

`ub D fuel r` is an upper bound on `bv_uint` of the value at slot `r`, read off
the certificate alone:

* a constant source: its value, exactly;
* any other source: `2 ^ w - 1` at its declared width (`sourceValue` resizes an
  input or a held flop to that width); a memory image reads as `0` when taken
  as a bit vector;
* a node: `2 ^ width - 1` (every operator returns its node width), refined for
  * `Op_Or [x]`, the exporter's resize of an address: no more than `x`;
  * `Op_MuxBool [s, f, t]`: no more than the larger of its arms, where the arm
    `t` is capped at `K - 1` when the select is `Op_ULT [t, k]` with `k` a
    constant `K` -- the clamp `t < K ? t : f`.

For every memory READ or WRITE whose image comes from a declared memory, the
bound of its address must be below that memory's `depth`. A whole-array load or
bus read of `n` words needs `n ≤ depth`. A ROM (`memConst`) is not checked: its
reads already stop at its real `contents.size`.

`Projection.MemAddrSound.memAccess_lt_depth` proves that the check is sound:
on every step of `interpretDesign`, from any state and any input, the address
of every such access is below the memory's depth.
-/
import LeanSemanticPrimitives.Compiler.DesignCert

namespace Compiler
namespace MemAddr

/-- `bv_uint` of a constant source's value `mk_bv w v`, as a `Nat`. -/
def constVal (w : Nat) (v : Int) : Nat := (v % (2 ^ w : Int)).toNat

/-- A bound on `bv_uint` of a source's value. -/
def srcCap : SourceDesc → Nat
  | .input _ w             => 2 ^ w - 1
  | .const w v             => constVal w v
  | .flopQ _ w             => 2 ^ w - 1
  | .flopQAsync _ w _ _ _  => 2 ^ w - 1
  | .memImg _ _ _          => 0
  | .memConst _ _ _        => 0

/-- The value of slot `r` when it is a constant source. -/
def constAt (D : DesignCert) (r : Nat) : Option Nat :=
  match D.sources[r]? with
  | some (.const w v) => some (constVal w v)
  | _                 => none

/-- `some K` when slot `s` is a node `Op_ULT [t, k]` comparing slot `t` with a
constant `K`: a select that is nonzero only when `t < K`. -/
def ultGuard (D : DesignCert) (s t : Nat) : Option Nat :=
  if s < D.sources.size then none else
  match D.nodes[s - D.sources.size]? with
  | some g =>
    match g.op, g.deps.toList with
    | .Op_ULT, [t', k] => if t' = t then constAt D k else none
    | _, _             => none
  | none => none

/-- The width bound alone: a source's cap, a node's `2 ^ width - 1`, and `0` past
the last slot (`denoteRef` reads `badVal` there). -/
def capAt (D : DesignCert) (r : Nat) : Nat :=
  match D.sources[r]? with
  | some sd => srcCap sd
  | none =>
    match D.nodes[r - D.sources.size]? with
    | some c => 2 ^ c.width - 1
    | none   => 0

/-- An upper bound on `bv_uint` of the value at slot `r` (see the module doc).
`fuel` bounds the walk; out of fuel, the width bound. -/
def ub (D : DesignCert) : Nat → Nat → Nat
  | 0, r => capAt D r
  | f + 1, r =>
    match D.sources[r]? with
    | some sd => srcCap sd
    | none =>
      match D.nodes[r - D.sources.size]? with
      | none   => 0
      | some c =>
        match c.op, c.deps.toList with
        | .Op_Or, [x] => min (2 ^ c.width - 1) (ub D f x)
        | .Op_MuxBool, [s, fa, ta] =>
          let tb := match ultGuard D s ta with
            | some K => min (ub D f ta) (K - 1)
            | none   => ub D f ta
          min (2 ^ c.width - 1) (max (ub D f fa) tb)
        | _, _ => 2 ^ c.width - 1

/-- How far `ub` looks back. An address is the exporter's resize (`Op_Or [x]`)
of a clamp (`Op_MuxBool`) of an index: three levels. -/
def ubFuel : Nat := 6

/-- The declared memory an image slot comes from, following write and load
nodes back to a source: `some (some k)` for memory `k` (`memImg`), `some none`
for a ROM (`memConst`), `none` when no source is reached within `fuel` steps or
the slot is not an image. -/
def rootOf (D : DesignCert) : Nat → Nat → Option (Option Nat)
  | 0, _ => none
  | f + 1, r =>
    match D.sources[r]? with
    | some (.memImg k _ _)   => some (some k)
    | some (.memConst _ _ _) => some none
    | some _                 => none
    | none =>
      match D.nodes[r - D.sources.size]? with
      | some c =>
        match c.op, c.deps[0]? with
        | .Op_MemWrite, some m     => rootOf D f m
        | .Op_MemWriteBE _, some m => rootOf D f m
        | .Op_MemLoad _, some m    => rootOf D f m
        | _, _                     => none
      | none => none

/-- A memory operator that reads its address from operand 1. -/
def isAddrOp : LGraphOp → Bool
  | .Op_MemRead | .Op_MemWrite | .Op_MemWriteBE _ => true
  | _ => false

/-- A memory operator that touches words `0 .. n - 1` of the whole array. -/
def wholeWords : LGraphOp → Option Nat
  | .Op_MemLoad n | .Op_MemToBus n => some n
  | _ => none

/-- Why node `j` may touch a word past its memory's depth, or `none`. -/
def accessError (D : DesignCert) (j : Nat) (c : DenseNodeCert) : Option String :=
  if !isAddrOp c.op && (wholeWords c.op).isNone then none else
  match c.deps[0]? with
  | none   => none   -- wrong arity: refused by the direct checker; no word is touched
  | some m =>
    match rootOf D (D.nodes.size + 1) m with
    | none => some s!"node {j} (LGraph {c.origin}) accesses an image that no declared memory \
                      or ROM starts"
    | some none => none
    | some (some k) =>
      match D.memories[k]? with
      | none => some s!"node {j} (LGraph {c.origin}) accesses memory {k}, which is not declared"
      | some md =>
        match wholeWords c.op with
        | some n =>
          if n ≤ md.depth then none
          else some s!"node {j} (LGraph {c.origin}) moves {n} words of memory {k}, \
                       which has {md.depth}"
        | none =>
          match c.deps[1]? with
          | none   => none
          | some a =>
            let u := ub D ubFuel a
            if u < md.depth then none
            else some s!"node {j} (LGraph {c.origin}): memory {k} has {md.depth} words, but \
                         its address (slot {a}) is bounded only by {u}, so a step may touch a \
                         word the memory does not have (pass.bitwidth proves every address \
                         inside the memory before a certificate is written)"

/-- The first access that may touch a word past its memory's depth, or `none`. -/
def addrError (D : DesignCert) : Option String :=
  (List.range D.nodes.size).findSome? fun j =>
    match D.nodes[j]? with
    | some c => accessError D j c
    | none   => none

/-- A memory whose depth is zero or larger than its table. -/
def depthError (D : DesignCert) : Option String :=
  match D.memories.findIdx? (fun m => m.depth == 0 || m.depth > 2 ^ m.aw) with
  | some k => some s!"memory {k} declares depth {D.memories[k]!.depth}, outside \
                      1 .. 2^{D.memories[k]!.aw}"
  | none   => none

/-! ## Facts the soundness proof reads off the check -/

theorem addrError_none {D : DesignCert} (h : addrError D = none) {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) : accessError D j c = none := by
  have hj : j < D.nodes.size := by
    rcases Nat.lt_or_ge j D.nodes.size with h' | h'
    · exact h'
    · simp [Array.getElem?_eq_none h'] at hc
  unfold addrError at h
  have := List.findSome?_eq_none_iff.mp h j (List.mem_range.mpr hj)
  simpa [hc] using this

theorem accessError_addr {D : DesignCert} {j : Nat} {c : DenseNodeCert}
    (h : accessError D j c = none) (hop : isAddrOp c.op = true)
    {m a k : Nat} {md : MemoryDesc} (hm : c.deps[0]? = some m) (ha : c.deps[1]? = some a)
    (hr : rootOf D (D.nodes.size + 1) m = some (some k)) (hmd : D.memories[k]? = some md) :
    ub D ubFuel a < md.depth := by
  have hw : wholeWords c.op = none := by
    unfold isAddrOp at hop; unfold wholeWords; split <;> simp_all
  unfold accessError at h
  simp only [hop, hw, Option.isNone_none, Bool.not_true, Bool.false_and, Bool.false_eq_true,
    ite_false, hm, hr, hmd, ha] at h
  by_cases hlt : ub D ubFuel a < md.depth
  · exact hlt
  · simp [hlt] at h

theorem accessError_whole {D : DesignCert} {j : Nat} {c : DenseNodeCert}
    (h : accessError D j c = none) {n : Nat} (hop : wholeWords c.op = some n)
    {m k : Nat} {md : MemoryDesc} (hm : c.deps[0]? = some m)
    (hr : rootOf D (D.nodes.size + 1) m = some (some k)) (hmd : D.memories[k]? = some md) :
    n ≤ md.depth := by
  unfold accessError at h
  simp only [hop, Option.isNone_some, Bool.and_false, Bool.false_eq_true, ite_false, hm, hr,
    hmd] at h
  by_cases hlt : n ≤ md.depth
  · exact hlt
  · simp [hlt] at h

end MemAddr
end Compiler
