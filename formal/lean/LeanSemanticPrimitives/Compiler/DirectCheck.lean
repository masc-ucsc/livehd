/-
# `DirectCheck` — fail-closed acceptance for DIRECT execution of a `DesignCert`

Direction 2, phase 1.

`interpretDesign` is TOTAL: an operator at the wrong arity falls through
`eval_op`'s `| _, w, _ => mk_bv w 0`, and projecting a memory as a bit vector
(`CertVal.asBV` on a `.mem`) yields a zero-width zero.  Those fallbacks make the
logic total, which is right for a specification, and they are NOT an acceptable
user-visible semantics for a simulator: a malformed certificate would simulate,
quietly, as zeros.

So the direct simulator is fail-closed.  `checkDesign` is an INDEPENDENT,
source-level admission test — it never calls `compileDesign`, never builds a
`ResidualProgram`, and never mentions `ResidualExpr`.  Using the compiler as the
admission test would make Direction 2 depend on the very implementation it is
supposed to differ from.

## What the accepted-operator table is derived from

Not from `compileOp`'s refusal list.  From the post-lowering LGraph contract as
the exporter actually implements it (`pass/lean/pass_lean.cpp`,
`cert_node_expr`), cross-checked against a census of every generated
`DesignCert` (469 files, 147 distinct by content hash: DINO ×3, CORE-ET, CVA6):

  op              arity seen   width seen        exporter site
  Op_And          0,2,3,4      any               `Ntype_op::And`
  Op_Or           0..441       any               `Ntype_op::Or`
  Op_Xor          2,3          any               `Ntype_op::Xor`
  Op_Ror          1            1 only            `Ntype_op::Ror`
  Op_EQ           2            1 only            `Ntype_op::EQ`
  Op_Not          1            any               `Ntype_op::Not`
  Op_Sum k        2 (k=1,2)    any               `Ntype_op::Sum`
  Op_Mult         2            any               `Ntype_op::Mult`
  Op_ULT/UGT      2            1 only            `Ntype_op::LT/GT`, unsigned
  Op_SLT/SGT      2            1 only            `Ntype_op::LT/GT`, signed
  Op_SHL          2            any               `Ntype_op::SHL`
  Op_SRA          2            any               `Ntype_op::SRA`
  Op_Sext         2            any               `Ntype_op::Sext`
  Op_GetMask      2            any               `Ntype_op::Get_mask`
  Op_MuxBool      3            any               `Ntype_op::Mux`, 2 data + 1-bit sel
  Op_MuxN         3            any               `Ntype_op::Mux`, otherwise
  Op_MemRead      3            any               `cert_memory_expand`
  Op_MemWriteBE b 4 (b=1)      any               `cert_memory_expand`

Three operators the exporter CAN emit were not observed in the census but are
grounded by their exporter site, so they are accepted rather than refused:

  * `Op_Const` — `cert_node_expr` emits it for `Ntype_op::Nconst`; the dense
    `DesignCert` path turns constants into SOURCES, so it has zero node
    occurrences, but `eval_op Op_Const w _ = mk_bv w c` is total and grounded.
  * `Op_UDiv` — `cert_node_expr` maps `Ntype_op::Div` to it.
  * `Op_SetMask` — `cert_node_expr` emits it for `Ntype_op::Set_mask`.
  * `Op_MemWrite` — the non-byte-enabled write `cert_memory_expand` can emit.

Five are REFUSED, and this is where Direction 2's table differs from a copy of
anyone's list — each is refused for a stated reason, not because it happens to
be unreachable:

  * `Op_Sub`  — no exporter site; `Ntype_op::Sum` carries subtrahends instead.
  * `Op_LT`, `Op_GT` — no exporter site (`Ntype_op::LT/GT` always select the
    signed or unsigned variant from `node_output_is_signed`), and `eval_op`
    defines both as UNSIGNED comparisons, which is an ungrounded equation: if a
    signed `Op_LT` ever appeared it would silently compare unsigned.
  * `Op_Div`, `Op_SDiv` — no exporter site.  `eval_op Op_Div` is likewise an
    unsigned division whose signedness is ungrounded.

Refusing an operator whose Lean equation is not grounded in the exporter is the
whole point of a fail-closed checker; accepting it because the equation happens
to be total would be the mistake.

## Constraints that are contract, not fallback-avoidance

Two kinds of condition live in `opArity` / `opWidth`, and the difference matters:

  * FALLBACK-AVOIDANCE (hard): `Op_Not` at arity ≠ 1, `Op_SRA` at arity ≠ 2 …
    reach `eval_op`'s wildcard and evaluate to zero.  These arities MUST be
    checked or the fallback is user-visible.
  * CONTRACT (soft): `Op_EQ`/`Op_Ror`/the four comparisons produce a 1-bit
    result in `graph/cell.cpp`, and `Op_MuxN` needs a selector plus at least one
    data operand.  `eval_op` is defined at other shapes, but the LGraph contract
    is not, so a certificate violating them is malformed and is refused.

`Op_And` / `Op_Or` / `Op_Xor` accept ANY arity including zero, because the
census finds real arity-0 `Op_Or` nodes in CVA6 (`cva6_icache`, `csr_regfile`,
`id_stage`, `cva6_ptw`, `cva6_hpdcache_if_adapter`) — a driverless reduce node,
whose `eval_op` value is a defined `mk_bv w 0`, not a fallback.

## What this checker CANNOT prove

The certificate carries no clock-model provenance, so no Lean predicate here can
discover whether the C++ graph was correctly normalised to a single edge.
Single-edge normalisation stays an EXPORTER PRECONDITION, outside `DesignSemWF`,
and is reported as trusted rather than checked.  Likewise, an async flop's
`resetPin` slot and its `resetInput` ordinal legitimately differ in POLARITY —
the census shows 9,262 async sources whose `activeLow` is the opposite of their
`FlopDesc.resetActiveLow`, because the pin slot reads an already-inverted node
while the ordinal reads the raw port.  Only the reset VALUE, the width, and the
existence of a reset pin are cross-checkable, and only those are checked.
-/
import LeanSemanticPrimitives.Compiler.DesignSemantics

namespace Compiler
namespace Direct

open Compiler.DesignCert

--------------------------------------------------------------------------------
-- Errors
--------------------------------------------------------------------------------

/-- Why a certificate, an input, or a state is refused.  Direction 2's own type:
sharing `CompileError` would tie the direct simulator to the residual compiler's
vocabulary, and the two refuse different things. -/
inductive SimError where
  /-- a bit-vector width of zero, which no slot may carry -/
  | zeroWidth                (slot : Nat)
  /-- the operator has no grounded semantics in the accepted fragment -/
  | unsupportedOp            (node : Nat) (op : LGraphOp)
  /-- the operator is applied at an arity its semantics does not define -/
  | badArity                 (node : Nat) (op : LGraphOp) (got : Nat)
  /-- the operator's result width violates the LGraph cell contract -/
  | badResultWidth           (node : Nat) (op : LGraphOp) (got : Nat)
  /-- a dependency names a slot that is not strictly earlier -/
  | depNotEarlier            (node : Nat) (dep : Nat)
  /-- a dependency is a memory where a bit vector is required, or vice versa -/
  | depKindMismatch          (node : Nat) (pos : Nat) (dep : Nat)
  /-- an output / flop pin / memory image names a slot outside the design -/
  | slotOutOfRange           (slot : Nat)
  /-- a slot that must carry a bit vector carries a memory image -/
  | slotNotBV                (slot : Nat)
  /-- a slot that must carry a memory image carries a bit vector -/
  | slotNotMem               (slot : Nat)
  /-- a flop source names a flop ordinal outside `D.flops` -/
  | flopOrdOutOfRange        (slot : Nat) (idx : Nat)
  /-- a memory source names a memory ordinal outside `D.memories` -/
  | memOrdOutOfRange         (slot : Nat) (idx : Nat)
  /-- a flop source's width disagrees with its `FlopDesc` -/
  | flopWidthMismatch        (slot : Nat) (idx : Nat)
  /-- an asynchronous flop source whose `FlopDesc` has no reset pin -/
  | asyncResetMissing        (slot : Nat) (idx : Nat)
  /-- an asynchronous flop source whose reset VALUE differs from its `FlopDesc` -/
  | asyncResetValueMismatch  (slot : Nat) (idx : Nat)
  /-- a memory source's `aw`/`dw` disagree with its `MemoryDesc` -/
  | memDescMismatch          (slot : Nat) (idx : Nat)
  /-- an inlined ROM holds more entries than its address width can name -/
  | romTooLarge              (slot : Nat)
  /-- two sources read the same primary input at different widths -/
  | inputWidthConflict       (ordinal : Nat) (w1 : Nat) (w2 : Nat)
  /-- the supplied input vector is shorter than the ordinals the design reads -/
  | inputTooShort            (required : Nat) (got : Nat)
  /-- the supplied flop state does not have one entry per `FlopDesc` -/
  | flopStateMismatch        (required : Nat) (got : Nat)
  /-- the supplied memory state does not have one entry per `MemoryDesc` -/
  | memStateMismatch         (required : Nat) (got : Nat)
deriving Repr, Inhabited, DecidableEq

/-- The error's constructor name.  Used by the simulator's diagnostics and by
the negative tests, which assert WHICH refusal fired rather than only that one
did. -/
def SimError.tag : SimError → String
  | .zeroWidth _               => "zeroWidth"
  | .unsupportedOp _ _         => "unsupportedOp"
  | .badArity _ _ _            => "badArity"
  | .badResultWidth _ _ _      => "badResultWidth"
  | .depNotEarlier _ _         => "depNotEarlier"
  | .depKindMismatch _ _ _     => "depKindMismatch"
  | .slotOutOfRange _          => "slotOutOfRange"
  | .slotNotBV _               => "slotNotBV"
  | .slotNotMem _              => "slotNotMem"
  | .flopOrdOutOfRange _ _     => "flopOrdOutOfRange"
  | .memOrdOutOfRange _ _      => "memOrdOutOfRange"
  | .flopWidthMismatch _ _     => "flopWidthMismatch"
  | .asyncResetMissing _ _     => "asyncResetMissing"
  | .asyncResetValueMismatch _ _ => "asyncResetValueMismatch"
  | .memDescMismatch _ _       => "memDescMismatch"
  | .romTooLarge _             => "romTooLarge"
  | .inputWidthConflict _ _ _  => "inputWidthConflict"
  | .inputTooShort _ _         => "inputTooShort"
  | .flopStateMismatch _ _     => "flopStateMismatch"
  | .memStateMismatch _ _      => "memStateMismatch"

/-- A one-line human-readable rendering, for the simulator's stderr. -/
def SimError.render : SimError → String
  | .zeroWidth s               => s!"slot {s}: width 0 is not a legal bit-vector width"
  | .unsupportedOp n op        => s!"node {n}: operator {repr op} is outside the accepted fragment"
  | .badArity n op g           => s!"node {n}: operator {repr op} at arity {g}"
  | .badResultWidth n op g     => s!"node {n}: operator {repr op} may not produce width {g}"
  | .depNotEarlier n d         => s!"node {n}: dependency on slot {d}, which is not strictly earlier"
  | .depKindMismatch n p d     => s!"node {n}: operand {p} (slot {d}) has the wrong bv/mem kind"
  | .slotOutOfRange s          => s!"slot {s} is outside the design"
  | .slotNotBV s               => s!"slot {s} holds a memory image where a bit vector is required"
  | .slotNotMem s              => s!"slot {s} holds a bit vector where a memory image is required"
  | .flopOrdOutOfRange s i     => s!"source slot {s}: flop ordinal {i} is not declared"
  | .memOrdOutOfRange s i      => s!"source slot {s}: memory ordinal {i} is not declared"
  | .flopWidthMismatch s i     => s!"source slot {s}: width disagrees with flop {i}"
  | .asyncResetMissing s i     => s!"source slot {s}: async flop {i} has no reset pin"
  | .asyncResetValueMismatch s i => s!"source slot {s}: reset value disagrees with flop {i}"
  | .memDescMismatch s i       => s!"source slot {s}: aw/dw disagree with memory {i}"
  | .romTooLarge s             => s!"source slot {s}: ROM has more entries than 2^aw"
  | .inputWidthConflict o a b  => s!"primary input {o} is read at both width {a} and width {b}"
  | .inputTooShort req got     => s!"input vector has {got} entries; the design reads {req}"
  | .flopStateMismatch req got => s!"flop state has {got} entries; the design declares {req}"
  | .memStateMismatch req got  => s!"memory state has {got} entries; the design declares {req}"

/-- First error wins.  A local definition rather than `Option.orElse` so the
`= none` decomposition below is a two-case `cases`, with no reliance on which
simp lemmas core happens to provide for `<|>`. -/
def firstOf : Option SimError → Option SimError → Option SimError
  | some e, _ => some e
  | none,   b => b

theorem firstOf_eq_none {a b : Option SimError} (h : firstOf a b = none) :
    a = none ∧ b = none := by
  cases a with
  | none   => exact ⟨rfl, h⟩
  | some e => exact absurd h (by simp [firstOf])

--------------------------------------------------------------------------------
-- Static slot kinds: `bv | mem`
--------------------------------------------------------------------------------

/-- The STATIC type of a slot.  `CertVal` carries the `bv | mem` split at
runtime; this is the same split decided from the certificate alone, which is
what lets the checker refuse a memory used as a bit vector BEFORE any value
exists. -/
inductive SlotKind where
  | bv  (w : Nat)
  | mem
deriving Repr, Inhabited, DecidableEq

def SlotKind.isMem : SlotKind → Bool
  | .bv _ => false
  | .mem  => true

def sourceKind : SourceDesc → SlotKind
  | .input _ w            => .bv w
  | .const w _            => .bv w
  | .flopQ _ w            => .bv w
  | .flopQAsync _ w _ _ _ => .bv w
  | .memImg _ _ _         => .mem
  | .memConst _ _ _       => .mem

/-- A node's result kind.  Only the two write operators produce a memory image;
`Op_MemRead` produces the read DATA, which is a bit vector. -/
def opKind : LGraphOp → Nat → SlotKind
  | .Op_MemWrite,     _ => .mem
  | .Op_MemWriteBE _, _ => .mem
  | _,                w => .bv w

/-- The static kind of any slot, or `none` when the slot does not exist. -/
def slotKind (D : DesignCert) (k : Nat) : Option SlotKind :=
  match D.sources[k]? with
  | some sd => some (sourceKind sd)
  | none    => (D.nodes[k - D.sources.size]?).map fun c => opKind c.op c.width

/-- `true` = memory image, `false` = bit vector, `none` = no such slot. -/
def slotIsMem (D : DesignCert) (k : Nat) : Option Bool :=
  (slotKind D k).map SlotKind.isMem

theorem lt_of_getElem?_some {α : Type} {a : Array α} {i : Nat} {x : α}
    (h : a[i]? = some x) : i < a.size := by
  by_cases hi : i < a.size
  · exact hi
  · rw [Array.getElem?_eq_none (Nat.le_of_not_lt hi)] at h; exact absurd h (by simp)

theorem exists_getElem?_of_mem_toList {α : Type} {a : Array α} {x : α}
    (h : x ∈ a.toList) : ∃ i : Nat, a[i]? = some x := by
  have hm : x ∈ a := by simpa using h
  obtain ⟨i, hi, hix⟩ := Array.mem_iff_getElem.mp hm
  exact ⟨i, by rw [Array.getElem?_eq_getElem hi, hix]⟩

/-- A slot that has a static kind is a slot of the design. -/
theorem slotKind_lt {D : DesignCert} {r : Nat} {k : SlotKind} (h : slotKind D r = some k) :
    r < D.numSlots := by
  by_cases hr : r < D.sources.size
  · simp only [DesignCert.numSlots]; omega
  · have hs : D.sources[r]? = none := Array.getElem?_eq_none (Nat.le_of_not_lt hr)
    rw [slotKind, hs] at h
    cases hn : D.nodes[r - D.sources.size]? with
    | none   => rw [hn] at h; exact absurd h (by simp)
    | some c =>
        have := lt_of_getElem?_some hn
        simp only [DesignCert.numSlots]; omega

theorem slotIsMem_lt {D : DesignCert} {r : Nat} {b : Bool} (h : slotIsMem D r = some b) :
    r < D.numSlots := by
  unfold slotIsMem at h
  cases hk : slotKind D r with
  | none   => rw [hk] at h; exact absurd h (by simp)
  | some k => exact slotKind_lt hk

--------------------------------------------------------------------------------
-- The accepted-operator table
--------------------------------------------------------------------------------

/-- Is this operator in the accepted fragment at all?  See the header for why
each of the five refusals is refused. -/
def opAccepted : LGraphOp → Bool
  | .Op_Sub  => false
  | .Op_Div  => false
  | .Op_SDiv => false
  | .Op_LT   => false
  | .Op_GT   => false
  | _        => true

/-- The arity rule.  Hard cases (marked *) are fallback-avoidance: `eval_op`
reaches its wildcard at any other arity.  The rest are the LGraph cell contract. -/
def opArity : LGraphOp → Nat → Bool
  | .Op_Const _,      n => n == 0
  | .Op_Sum k,        n => 0 < n && k ≤ n
  | .Op_Mult,         n => 0 < n
  | .Op_UDiv,         n => n == 2        -- *
  | .Op_And,          _ => true
  | .Op_Or,           _ => true
  | .Op_Xor,          _ => true
  | .Op_Ror,          _ => true
  | .Op_Not,          n => n == 1        -- *
  | .Op_EQ,           n => 2 ≤ n
  | .Op_ULT,          n => n == 2        -- *
  | .Op_UGT,          n => n == 2        -- *
  | .Op_SLT,          n => n == 2        -- *
  | .Op_SGT,          n => n == 2        -- *
  | .Op_SHL,          n => n == 2
  | .Op_SRA,          n => n == 2        -- *
  | .Op_Sext,         n => n == 2        -- *
  | .Op_GetMask,      n => n == 2        -- *
  | .Op_SetMask,      n => n == 3        -- *
  | .Op_MuxBool,      n => n == 3        -- *
  | .Op_MuxN,         n => 2 ≤ n
  | .Op_MemRead,      n => n == 3        -- *
  | .Op_MemWrite,     n => n == 4        -- *
  | .Op_MemWriteBE b, n => n == 4 && 0 < b   -- * (b = 0 would divide by zero)
  -- the five refusals never reach here; `opAccepted` rejects them first
  | .Op_Sub,  _ => false
  | .Op_Div,  _ => false
  | .Op_SDiv, _ => false
  | .Op_LT,   _ => false
  | .Op_GT,   _ => false

/-- The result-width rule beyond `0 < w`: the predicate cells are one bit. -/
def opWidth : LGraphOp → Nat → Bool
  | .Op_Ror, w => w == 1
  | .Op_EQ,  w => w == 1
  | .Op_ULT, w => w == 1
  | .Op_UGT, w => w == 1
  | .Op_SLT, w => w == 1
  | .Op_SGT, w => w == 1
  | _,       _ => true

/-- Which dependency POSITION must be a memory image.  Only the memory
operators have one, and it is always position 0 — the operand order
`eval_op_cert` matches on. -/
def depIsMem : LGraphOp → Nat → Bool
  | .Op_MemRead,      0 => true
  | .Op_MemWrite,     0 => true
  | .Op_MemWriteBE _, 0 => true
  | _,                _ => false

/-- One node's whole shape obligation. -/
def opShape (op : LGraphOp) (w n : Nat) : Bool :=
  0 < w && opAccepted op && opArity op n && opWidth op w

--------------------------------------------------------------------------------
-- Per-item checks
--------------------------------------------------------------------------------

/-- One step of a dense scan: check item `i` if it exists.  Factored out so the
scan's definitional form and `scan_none`'s statement are the SAME matcher — a
`match` written twice elaborates to two different auxiliary functions, and the
soundness lemma then fails to apply. -/
def scanItem {α : Type} (get : Nat → Option α) (chk : Nat → α → Option SimError)
    (i : Nat) : Option SimError :=
  match get i with
  | none   => none
  | some a => chk i a

/-- Scan `start .. start+k-1`, first error wins.  Structural recursion on the
COUNT with the recursive call in tail position, so a 200k-node certificate costs
200k iterations and no stack: materialising `slotsFrom 0 n` as a list first — the
obvious spelling — conses to a depth the checker cannot survive on a real
design. -/
def scanFrom {α : Type} (get : Nat → Option α) (chk : Nat → α → Option SimError) :
    Nat → Nat → Option SimError
  | _,     0     => none
  | start, k + 1 =>
    match scanItem get chk start with
    | some e => some e
    | none   => scanFrom get chk (start + 1) k

/-- Scan `0 .. n-1`, first error wins. -/
def scan {α : Type} (n : Nat) (get : Nat → Option α)
    (chk : Nat → α → Option SimError) : Option SimError :=
  scanFrom get chk 0 n

theorem scanFrom_none {α : Type} {get : Nat → Option α}
    {chk : Nat → α → Option SimError} :
    ∀ (k start i : Nat), scanFrom get chk start k = none →
      start ≤ i → i < start + k → ∀ a, get i = some a → chk i a = none := by
  intro k
  induction k with
  | zero => intro start i _ _ hlt; omega
  | succ k ih =>
      intro start i h hlo hhi a ha
      unfold scanFrom at h
      cases he : scanItem get chk start with
      | some e => rw [he] at h; exact absurd h (by simp)
      | none =>
          rw [he] at h
          by_cases hs : i = start
          · subst hs
            unfold scanItem at he
            rw [ha] at he
            exact he
          · exact ih (start + 1) i h (by omega) (by omega) a ha

/-- A clean scan means every in-range item checked clean. -/
theorem scan_none {α : Type} {n : Nat} {get : Nat → Option α}
    {chk : Nat → α → Option SimError} (h : scan n get chk = none)
    (i : Nat) (hi : i < n) (a : α) (ha : get i = some a) : chk i a = none :=
  scanFrom_none n 0 i h (Nat.zero_le _) (by omega) a ha

/-- Source slot `j`.  Widths, state ordinals, and descriptor agreement. -/
def checkSource (D : DesignCert) (j : Nat) : SourceDesc → Option SimError
  | .input _ w => if w == 0 then some (.zeroWidth j) else none
  | .const w _ => if w == 0 then some (.zeroWidth j) else none
  | .flopQ idx w =>
      if w == 0 then some (.zeroWidth j)
      else match D.flops[idx]? with
        | none   => some (.flopOrdOutOfRange j idx)
        | some f => if f.width == w then none else some (.flopWidthMismatch j idx)
  | .flopQAsync idx w _ri rv _al =>
      if w == 0 then some (.zeroWidth j)
      else match D.flops[idx]? with
        | none   => some (.flopOrdOutOfRange j idx)
        | some f =>
            if f.width != w then some (.flopWidthMismatch j idx)
            else if f.resetPin.isNone then some (.asyncResetMissing j idx)
            else if f.resetValue != rv then some (.asyncResetValueMismatch j idx)
            else none
  | .memImg idx aw dw =>
      if dw == 0 then some (.zeroWidth j)
      else match D.memories[idx]? with
        | none   => some (.memOrdOutOfRange j idx)
        | some m => if m.aw == aw && m.dw == dw then none else some (.memDescMismatch j idx)
  | .memConst aw dw contents =>
      if dw == 0 then some (.zeroWidth j)
      else if 2 ^ aw < contents.size then some (.romTooLarge j)
      else none

/-- Node `i`'s dependencies: strictly earlier, in range, and of the kind the
operand position requires. -/
def depError (D : DesignCert) (i : Nat) (c : DenseNodeCert) (p d : Nat) : Option SimError :=
  if D.sources.size + i ≤ d then some (.depNotEarlier (D.slotOfNode i) d)
  else match slotIsMem D d with
    | none   => some (.slotOutOfRange d)
    | some b => if b == depIsMem c.op p then none
                else some (.depKindMismatch (D.slotOfNode i) p d)

def checkDeps (D : DesignCert) (i : Nat) (c : DenseNodeCert) : Option SimError :=
  scan c.deps.size (fun p => c.deps[p]?) (depError D i c)

def checkNode (D : DesignCert) (i : Nat) (c : DenseNodeCert) : Option SimError :=
  if c.width == 0 then some (.zeroWidth (D.slotOfNode i))
  else if !opAccepted c.op then some (.unsupportedOp (D.slotOfNode i) c.op)
  else if !opArity c.op c.deps.size then some (.badArity (D.slotOfNode i) c.op c.deps.size)
  else if !opWidth c.op c.width then some (.badResultWidth (D.slotOfNode i) c.op c.width)
  else checkDeps D i c

/-- A shell reference (output slot, flop pin, memory image) must exist and have
the required kind. -/
def checkRef (D : DesignCert) (needMem : Bool) (r : Nat) : Option SimError :=
  match slotIsMem D r with
  | none   => some (.slotOutOfRange r)
  | some b => if b == needMem then none
              else if needMem then some (.slotNotMem r) else some (.slotNotBV r)

def checkFlop (D : DesignCert) (f : FlopDesc) : Option SimError :=
  firstOf (checkRef D false f.din)
    (firstOf (match f.enable with | none => none | some e => checkRef D false e)
             (match f.resetPin with | none => none | some r => checkRef D false r))

--------------------------------------------------------------------------------
-- Whole-certificate scans
--------------------------------------------------------------------------------

def sourceErrors (D : DesignCert) : Option SimError :=
  scan D.sources.size (fun j => D.sources[j]?) (checkSource D)

def nodeErrors (D : DesignCert) : Option SimError :=
  scan D.nodes.size (fun i => D.nodes[i]?) (checkNode D)

def checkOutput (D : DesignCert) (_k : Nat) (o : OutputDesc) : Option SimError :=
  if o.width == 0 then some (.zeroWidth o.slot) else checkRef D false o.slot

def outputErrors (D : DesignCert) : Option SimError :=
  scan D.outputs.size (fun k => D.outputs[k]?) (checkOutput D)

def checkFlopSlot (D : DesignCert) (_k : Nat) (f : FlopDesc) : Option SimError :=
  if f.width == 0 then some (.zeroWidth f.din) else checkFlop D f

def flopErrors (D : DesignCert) : Option SimError :=
  scan D.flops.size (fun k => D.flops[k]?) (checkFlopSlot D)

def checkMemory (D : DesignCert) (_k : Nat) (m : MemoryDesc) : Option SimError :=
  if m.dw == 0 then some (.zeroWidth m.nextImg) else checkRef D true m.nextImg

def memoryErrors (D : DesignCert) : Option SimError :=
  scan D.memories.size (fun k => D.memories[k]?) (checkMemory D)

/-- The declared width of primary input `idx`, taken from the first source that
reads it. -/
def inputWidthOf (D : DesignCert) (idx : Nat) : Option Nat :=
  D.sources.foldl (fun acc sd =>
    match acc with
    | some w => some w
    | none =>
      match sd with
      | .input i w => if i == idx then some w else none
      | _          => none) none

/-- Two sources may not read the same primary input at different widths:
`sourceValue` resizes to the source's own width, so a disagreement means the
certificate has two different opinions about one port. -/
def checkInputWidth (D : DesignCert) (_j : Nat) : SourceDesc → Option SimError
  | .input idx w =>
      match inputWidthOf D idx with
      | some w' => if w == w' then none else some (.inputWidthConflict idx w' w)
      | none    => none
  | _ => none

def inputWidthErrors (D : DesignCert) : Option SimError :=
  scan D.sources.size (fun j => D.sources[j]?) (checkInputWidth D)

--------------------------------------------------------------------------------
-- `checkDesign`
--------------------------------------------------------------------------------

def designErrors (D : DesignCert) : Option SimError :=
  firstOf (sourceErrors D)
    (firstOf (inputWidthErrors D)
      (firstOf (nodeErrors D)
        (firstOf (outputErrors D)
          (firstOf (flopErrors D) (memoryErrors D)))))

/-- The admission test.  Independent of `compileDesign` by construction: nothing
above mentions the residual language. -/
def checkDesign (D : DesignCert) : Except SimError Unit :=
  match designErrors D with
  | some e => .error e
  | none   => .ok ()

/-- Boolean mirror, for `#eval`-style sweeps and `native_decide` gates. -/
def acceptsDesign (D : DesignCert) : Bool :=
  (designErrors D).isNone

--------------------------------------------------------------------------------
-- The propositional content of a successful check
--------------------------------------------------------------------------------

/-- Everything `checkDesign` establishes about a certificate, as propositions.

`depsBounded` and `slotsInRange` are the two conditions the inherited
`DesignCertWF` already names; reusing them rather than restating them means
`wf_depOrdered` (hence B2's fixpoint theorem) applies directly. -/
structure DesignSemWF (D : DesignCert) : Prop where
  /-- every dependency names a strictly earlier slot -/
  depsBounded  : DesignCert.DepsBounded D
  /-- every output, flop pin and memory image names a real slot -/
  slotsInRange : DesignCert.SlotsInRange D
  /-- every node is an accepted operator at an accepted arity and width -/
  nodeShapes   : ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
                   opShape c.op c.width c.deps.size = true
  /-- every dependency has the static kind its operand position requires -/
  nodeKinds    : ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
                   ∀ (p d : Nat), c.deps[p]? = some d →
                     slotIsMem D d = some (depIsMem c.op p)
  /-- every output slot carries a bit vector -/
  outputKinds  : ∀ (k : Nat) (o : OutputDesc), D.outputs[k]? = some o →
                   slotIsMem D o.slot = some false
  /-- every flop pin carries a bit vector -/
  flopKinds    : ∀ (k : Nat) (f : FlopDesc), D.flops[k]? = some f →
                   slotIsMem D f.din = some false ∧
                   (∀ e, f.enable = some e → slotIsMem D e = some false) ∧
                   (∀ r, f.resetPin = some r → slotIsMem D r = some false)
  /-- every memory's next image carries a memory image -/
  memKinds     : ∀ (k : Nat) (m : MemoryDesc), D.memories[k]? = some m →
                   slotIsMem D m.nextImg = some true
  /-- every source slot is well formed against the state it names -/
  sourceOK     : ∀ (j : Nat) (sd : SourceDesc), D.sources[j]? = some sd →
                   checkSource D j sd = none
  /-- every source reading primary input `idx` agrees on its width, so the
  certificate has exactly one opinion about each port -/
  inputWidths  : ∀ (j idx w : Nat), D.sources[j]? = some (.input idx w) →
                   ∀ w', inputWidthOf D idx = some w' → w = w'

--------------------------------------------------------------------------------
-- Soundness
--------------------------------------------------------------------------------

section Sound
variable {D : DesignCert}

theorem checkDeps_sound {i : Nat} {c : DenseNodeCert} (h : checkDeps D i c = none)
    (p d : Nat) (hd : c.deps[p]? = some d) :
    d < D.sources.size + i ∧ slotIsMem D d = some (depIsMem c.op p) := by
  have hp : p < c.deps.size := lt_of_getElem?_some hd
  have hone := scan_none (by unfold checkDeps at h; exact h) p hp d hd
  unfold depError at hone
  by_cases hle : D.sources.size + i ≤ d
  · simp only [if_pos hle] at hone; exact absurd hone (by simp)
  · simp only [if_neg hle] at hone
    refine ⟨by omega, ?_⟩
    cases hk : slotIsMem D d with
    | none   => rw [hk] at hone; exact absurd hone (by simp)
    | some b =>
        rw [hk] at hone
        by_cases hb : b == depIsMem c.op p
        · exact congrArg some (by simpa using hb)
        · simp only [if_neg hb] at hone; exact absurd hone (by simp)

theorem checkNode_sound {i : Nat} {c : DenseNodeCert} (h : checkNode D i c = none) :
    opShape c.op c.width c.deps.size = true ∧ checkDeps D i c = none := by
  unfold checkNode at h
  by_cases hw : c.width == 0
  · simp only [if_pos hw] at h; exact absurd h (by simp)
  · simp only [if_neg hw] at h
    by_cases ha : !opAccepted c.op
    · simp only [if_pos ha] at h; exact absurd h (by simp)
    · simp only [if_neg ha] at h
      by_cases hn : !opArity c.op c.deps.size
      · simp only [if_pos hn] at h; exact absurd h (by simp)
      · simp only [if_neg hn] at h
        by_cases hv : !opWidth c.op c.width
        · simp only [if_pos hv] at h; exact absurd h (by simp)
        · simp only [if_neg hv] at h
          refine ⟨?_, h⟩
          simp only [opShape, Bool.and_eq_true, decide_eq_true_eq]
          refine ⟨⟨⟨?_, ?_⟩, ?_⟩, ?_⟩
          · have : ¬ (c.width = 0) := by simpa using hw
            omega
          · simpa using ha
          · simpa using hn
          · simpa using hv

theorem checkRef_sound {needMem : Bool} {r : Nat} (h : checkRef D needMem r = none) :
    slotIsMem D r = some needMem := by
  unfold checkRef at h
  cases hk : slotIsMem D r with
  | none   => rw [hk] at h; exact absurd h (by simp)
  | some b =>
      rw [hk] at h
      by_cases hb : b == needMem
      · exact congrArg some (by simpa using hb)
      · simp only [if_neg hb] at h
        cases needMem <;> simp at h

theorem designErrors_parts (h : designErrors D = none) :
    sourceErrors D = none ∧ inputWidthErrors D = none ∧ nodeErrors D = none ∧
    outputErrors D = none ∧ flopErrors D = none ∧ memoryErrors D = none := by
  unfold designErrors at h
  obtain ⟨h1, h⟩ := firstOf_eq_none h
  obtain ⟨h2, h⟩ := firstOf_eq_none h
  obtain ⟨h3, h⟩ := firstOf_eq_none h
  obtain ⟨h4, h⟩ := firstOf_eq_none h
  obtain ⟨h5, h6⟩ := firstOf_eq_none h
  exact ⟨h1, h2, h3, h4, h5, h6⟩

/-- **Checker soundness.**  A design the direct simulator accepts really does
satisfy every semantic condition `DesignSemWF` names. -/
theorem checkDesign_sound {u : Unit} (h : checkDesign D = .ok u) : DesignSemWF D := by
  have hnone : designErrors D = none := by
    unfold checkDesign at h
    cases he : designErrors D with
    | none   => rfl
    | some e => rw [he] at h; exact absurd h (by simp)
  obtain ⟨hsrc, hiw, hnode, hout, hflop, hmem⟩ := designErrors_parts hnone
  rw [sourceErrors] at hsrc
  rw [inputWidthErrors] at hiw
  rw [nodeErrors] at hnode
  rw [outputErrors] at hout
  rw [flopErrors] at hflop
  rw [memoryErrors] at hmem
  -- node shapes and dependency facts
  have hnodes : ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
      checkNode D i c = none := by
    intro i c hc
    exact scan_none hnode i (lt_of_getElem?_some hc) c hc
  have hshape : ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
      opShape c.op c.width c.deps.size = true :=
    fun i c hc => (checkNode_sound (hnodes i c hc)).1
  have hdeps : ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
      checkDeps D i c = none :=
    fun i c hc => (checkNode_sound (hnodes i c hc)).2
  -- outputs
  have houts : ∀ (k : Nat) (o : OutputDesc), D.outputs[k]? = some o →
      slotIsMem D o.slot = some false := by
    intro k o ho
    have := scan_none hout k (lt_of_getElem?_some ho) o ho
    unfold checkOutput at this
    by_cases hw : o.width == 0
    · simp only [if_pos hw] at this; exact absurd this (by simp)
    · simp only [if_neg hw] at this; exact checkRef_sound this
  -- flops
  have hflops : ∀ (k : Nat) (f : FlopDesc), D.flops[k]? = some f →
      slotIsMem D f.din = some false ∧
      (∀ e, f.enable = some e → slotIsMem D e = some false) ∧
      (∀ r, f.resetPin = some r → slotIsMem D r = some false) := by
    intro k f hf
    have hall := scan_none hflop k (lt_of_getElem?_some hf) f hf
    unfold checkFlopSlot at hall
    by_cases hw : f.width == 0
    · simp only [if_pos hw] at hall; exact absurd hall (by simp)
    · simp only [if_neg hw] at hall
      unfold checkFlop at hall
      obtain ⟨hd, hall⟩ := firstOf_eq_none hall
      obtain ⟨he, hr⟩ := firstOf_eq_none hall
      refine ⟨checkRef_sound hd, ?_, ?_⟩
      · intro e hee; rw [hee] at he; exact checkRef_sound he
      · intro r hrr; rw [hrr] at hr; exact checkRef_sound hr
  -- memories
  have hmems : ∀ (k : Nat) (m : MemoryDesc), D.memories[k]? = some m →
      slotIsMem D m.nextImg = some true := by
    intro k m hm
    have := scan_none hmem k (lt_of_getElem?_some hm) m hm
    unfold checkMemory at this
    by_cases hw : m.dw == 0
    · simp only [if_pos hw] at this; exact absurd this (by simp)
    · simp only [if_neg hw] at this; exact checkRef_sound this
  refine
    { depsBounded := ?_, slotsInRange := ?_, nodeShapes := hshape, nodeKinds := ?_,
      outputKinds := houts, flopKinds := hflops, memKinds := hmems, sourceOK := ?_,
      inputWidths := ?_ }
  · -- deps bounded: from `checkDeps`, via the index of each dep
    intro i c hc d hd
    obtain ⟨p, hp⟩ := exists_getElem?_of_mem_toList hd
    exact (checkDeps_sound (hdeps i c hc) p d hp).1
  · -- slots in range: an output/pin/image with a KIND has, in particular, a slot
    refine ⟨?_, ?_, ?_⟩
    · intro o ho
      obtain ⟨k, hk⟩ := exists_getElem?_of_mem_toList ho
      exact slotIsMem_lt (houts k o hk)
    · intro f hf
      obtain ⟨k, hk⟩ := exists_getElem?_of_mem_toList hf
      obtain ⟨hd, he, hr⟩ := hflops k f hk
      exact ⟨slotIsMem_lt hd, fun e hee => slotIsMem_lt (he e hee),
             fun r hrr => slotIsMem_lt (hr r hrr)⟩
    · intro m hm
      obtain ⟨k, hk⟩ := exists_getElem?_of_mem_toList hm
      exact slotIsMem_lt (hmems k m hk)
  · intro i c hc p d hd; exact (checkDeps_sound (hdeps i c hc) p d hd).2
  · intro j sd hj
    exact scan_none hsrc j (lt_of_getElem?_some hj) sd hj
  · intro j idx w hj w' hw'
    have h := scan_none hiw j (lt_of_getElem?_some hj) _ hj
    simp only [checkInputWidth] at h
    rw [hw'] at h
    by_cases hb : w == w'
    · simpa using hb
    · simp only [if_neg hb] at h; exact absurd h (by simp)

end Sound

--------------------------------------------------------------------------------
-- Runtime shape: the input vector and the state must fit the certificate
--------------------------------------------------------------------------------

/-- How many primary inputs the design actually reads: one past the largest
ordinal named by an `input` source or by an asynchronous flop's reset ordinal.

`sourceValue` reads `i[idx]?.getD (mk_bv w 0)`, so a short input vector would
simulate silently with zeros on the missing ports.  Checking this before
execution is what makes that branch unreachable. -/
def inputArity (D : DesignCert) : Nat :=
  D.sources.foldl (fun acc sd =>
    match sd with
    | .input idx _           => max acc (idx + 1)
    | .flopQAsync _ _ ri _ _ => max acc (ri + 1)
    | _                      => acc) 0

/-- The runtime counterpart of `DesignSemWF`.  Strictly stronger than the
inherited `RuntimeWF`, which constrains only the two state arrays. -/
structure RuntimeSemWF (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Prop where
  inputsSized : inputArity D ≤ i.size
  flopsSized  : s.flops.size = D.flops.size
  memsSized   : s.mems.size  = D.memories.size

def checkRuntime (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) :
    Except SimError Unit :=
  if i.size < inputArity D then
    .error (.inputTooShort (inputArity D) i.size)
  else if s.flops.size != D.flops.size then
    .error (.flopStateMismatch D.flops.size s.flops.size)
  else if s.mems.size != D.memories.size then
    .error (.memStateMismatch D.memories.size s.mems.size)
  else .ok ()

theorem checkRuntime_sound {D : DesignCert} {i : RuntimeInput} {s : RuntimeState} {u : Unit}
    (h : checkRuntime D i s = .ok u) : RuntimeSemWF D i s := by
  unfold checkRuntime at h
  by_cases h1 : i.size < inputArity D
  · simp only [if_pos h1] at h; exact absurd h (by simp)
  · simp only [if_neg h1] at h
    by_cases h2 : s.flops.size != D.flops.size
    · simp only [if_pos h2] at h; exact absurd h (by simp)
    · simp only [if_neg h2] at h
      by_cases h3 : s.mems.size != D.memories.size
      · simp only [if_pos h3] at h; exact absurd h (by simp)
      · exact ⟨by omega, by simpa using h2, by simpa using h3⟩

/-- The inherited minimal runtime condition is implied. -/
theorem RuntimeSemWF.toRuntimeWF {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}
    (h : RuntimeSemWF D i s) : RuntimeWF D i s :=
  ⟨h.flopsSized, h.memsSized⟩

--------------------------------------------------------------------------------
-- The operator equations of the accepted fragment
--
-- PHASE 0'S GATE, AS THEOREMS.  Each equation names the primitive one accepted
-- (operator, arity) reduces to.  None of them mentions `eval_op`'s wildcard
-- `| _, w, _ => mk_bv w 0` or `eval_op_cert`'s wrong-arity memory fallbacks, and
-- each is FALSE if the call did take one — so together with `opArity` they are
-- the evidence that no accepted node evaluates through a fallback.
--
-- They are `rfl`, deliberately: the claim is about which BRANCH is taken, not a
-- second denotation of the operator.  Restating each body would be the mistake
-- `eval_op_correct` already makes (near-identical sides proving nothing about
-- the equations' fidelity to LiveHD).
--------------------------------------------------------------------------------

section OpEquations
variable (w : Nat)

-- non-memory operators, over `eval_op`; `eval_op_cert_bv` lifts each to `CertVal`
theorem sem_const (c : Int) (l : List BV) : eval_op (.Op_Const c) w l = mk_bv w c := rfl

theorem sem_sum (k : Nat) (l : List BV) :
    eval_op (.Op_Sum k) w l =
      mk_bv w (((l.map bv_uint).take k).sum - ((l.map bv_uint).drop k).sum) := rfl

theorem sem_mult (l : List BV) : eval_op .Op_Mult w l = mk_bv w (l.map bv_uint).prod := rfl

theorem sem_udiv (a b : BV) :
    eval_op .Op_UDiv w [a, b] =
      mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b) := rfl

theorem sem_and_nil : eval_op .Op_And w [] = mk_bv w 0 := rfl

theorem sem_and (a : BV) (l : List BV) :
    eval_op .Op_And w (a :: l) =
      l.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a) := rfl

theorem sem_or (l : List BV) :
    eval_op .Op_Or w l =
      l.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0) := rfl

theorem sem_xor (l : List BV) :
    eval_op .Op_Xor w l =
      l.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0) := rfl

theorem sem_ror (l : List BV) :
    eval_op .Op_Ror w l = mk_bv w (if l.any bv_nonzero then 1 else 0) := rfl

theorem sem_not (a : BV) : eval_op .Op_Not w [a] = bv_not w a := rfl

theorem sem_eq (a : BV) (l : List BV) :
    eval_op .Op_EQ w (a :: l) =
      mk_bv w (if l.all fun b => bv_uint b = bv_uint a then 1 else 0) := rfl

theorem sem_ult (a b : BV) :
    eval_op .Op_ULT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0) := rfl

theorem sem_ugt (a b : BV) :
    eval_op .Op_UGT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0) := rfl

theorem sem_slt (a b : BV) :
    eval_op .Op_SLT w [a, b] = mk_bv w (if bv_sint a < bv_sint b then 1 else 0) := rfl

theorem sem_sgt (a b : BV) :
    eval_op .Op_SGT w [a, b] = mk_bv w (if bv_sint a > bv_sint b then 1 else 0) := rfl

theorem sem_shl (a b : BV) :
    eval_op .Op_SHL w [a, b] =
      bv_bitwise w (fun x y => xor x y) (mk_bv w 0)
        (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat)) := rfl

theorem sem_sra (a b : BV) : eval_op .Op_SRA w [a, b] = bv_sra w a b := rfl

theorem sem_sext (a amt : BV) :
    eval_op .Op_Sext w [a, amt] =
      (let n := (bv_uint amt).toNat
       if n = 0 then mk_bv w 0
       else
         let u := bv_uint a % (2 ^ n : Int)
         if u < (2 ^ (n - 1) : Int) then mk_bv w u else mk_bv w (u - (2 ^ n : Int))) := rfl

theorem sem_getMask (a m : BV) : eval_op .Op_GetMask w [a, m] = bv_get_mask w a m := rfl

theorem sem_setMask (a m v : BV) :
    eval_op .Op_SetMask w [a, m, v] = bv_set_mask w a m v := rfl

theorem sem_muxBool (sel fv tv : BV) :
    eval_op .Op_MuxBool w [sel, fv, tv] =
      (if bv_nonzero sel then bv_resize w tv else bv_resize w fv) := rfl

theorem sem_muxN (sel : BV) (l : List BV) :
    eval_op .Op_MuxN w (sel :: l) =
      (let idx := (bv_uint sel).toNat
       if idx < l.length then bv_resize w ((l[idx]?).getD (mk_bv w 0)) else mk_bv w 0) := rfl

-- memory operators, over `eval_op_cert` (they are the cases `eval_op` does NOT have)
theorem sem_memRead (m : Int → BV) (a en : BV) :
    eval_op_cert .Op_MemRead w [.mem m, .bv a, .bv en] = .bv (cert_mem_read w m a en) := rfl

theorem sem_memWrite (m : Int → BV) (a d en : BV) :
    eval_op_cert .Op_MemWrite w [.mem m, .bv a, .bv d, .bv en]
      = .mem (cert_mem_write m a d en) := rfl

theorem sem_memWriteBE (bw : Nat) (m : Int → BV) (a d be : BV) :
    eval_op_cert (.Op_MemWriteBE bw) w [.mem m, .bv a, .bv d, .bv be]
      = .mem (cert_mem_write_be w m a d be bw) := rfl

end OpEquations

end Direct
end Compiler
