/-
# The direct simulator, staged: every node specialized once per design

`directStepRaw` evaluates each node through `eval_op_cert`: it dispatches on the operator and
on the shape of an operand list it builds, keeps every value as a `CertVal` holding a `BV`
whose value is an `Int` (a GMP object from `2 ^ 31` up), and decides again on every step what
it could decide once: that a GetMask's mask is a constant run of ones, where the run starts,
what a constant operand is.

`prepare D` decides those things once. Each node becomes an `SNode`: a specialized kind with
its operands as slot numbers or constants (a constant source's reduced value), or `generic`
(every other operator, and every operator whose operand widths are not fixed). A step then
keeps every slot's reduced unsigned value (`uintNat`) in one `Array Nat`, written in place;
below `2 ^ 63` a `Nat` is a machine word, so a node allocates nothing. A slot whose exact value
is not `⟨width, value⟩` at a width fixed at load time (a memory, a generic node's result) also
keeps that value in `memv`.

`step` returns exactly `directStepRaw`'s result (outputs, flops and memories).
-/
import LeanSemanticPrimitives.Compiler.DirectSemantics

namespace Compiler
namespace Staged

open FastBV Direct

/-- An operand: a slot, read from the value array at run time, or a number fixed at load
time (the reduced value of a constant source). -/
inductive Opd where
  | slot (r : Nat)
  | lit (n : Nat)
deriving Inhabited, Repr

@[inline] def Opd.get (vals : Array Nat) : Opd → Nat
  | .slot r => vals.getD r 0
  | .lit n => n

/-- A node, specialized at load time. `w` is the node's width; every specialized kind reads
only its operands' reduced values, plus widths fixed at load time. -/
inductive SNode where
  | and (w : Nat) (a : Opd) (bs : Array Opd)
  | or (w : Nat) (as : Array Opd)
  | xor (w : Nat) (as : Array Opd)
  | ror (w : Nat) (as : Array Opd)
  | not (w : Nat) (a : Opd)
  | mux (w : Nat) (s f t : Opd)
  | eq (w : Nat) (a : Opd) (bs : Array Opd)
  | ult (w : Nat) (a b : Opd)
  | ugt (w : Nat) (a b : Opd)
  /-- GetMask with a constant mask that is one run of `L` ones from bit `s` (checked at load
  against the mask's width): `mask = 2 ^ L - 1`. -/
  | getMask (w : Nat) (x : Opd) (s mask : Nat)
  /-- Sext whose operand `a` has the width `aw`, fixed at load time. -/
  | sext (w : Nat) (a : Opd) (aw : Nat) (n : Opd)
  /-- SRA whose operand `x` has the width `xw`, fixed at load time. -/
  | sra (w : Nat) (x : Opd) (xw : Nat) (k : Opd)
  | shl (w : Nat) (a : Opd) (ks : Array Opd)
  | generic (c : DenseNodeCert)
deriving Inhabited

/-- One `Op_SHL` term on reduced values: `bvShlStepFast`'s value. -/
@[inline] def shlStepNat (w u k : Nat) : Nat :=
  if hk : k ≥ w then 0
  else if h : w < maskTab.size then
    (u &&& maskTab[w - k]'(by omega)) *
      pow2Tab[k]'(by rw [pow2Tab_size]; rw [maskTab_size] at h; omega)
  else modPow2 (w - k) u <<< k

/-- `bvSraFast`'s value, for an operand of width `xw` and reduced value `u`. -/
@[inline] def sraNat (w xw u k : Nat) : Nat :=
  if k ≥ xw then modNat w (if xw = 0 then 0 else if ltPow2 u (xw - 1) then 0 else -1)
  else if ltPow2 u (xw - 1) then modPow2 w (u >>> k)
  else modNat w (Int.negSucc ((maskOf xw - u) >>> k))

/-- `sextOpFast`'s value, for an operand of width `aw` and reduced value `u`. -/
@[inline] def sextNat (w aw u n : Nat) : Nat :=
  if n = 0 then 0
  else if n > aw then truncNat w u
  else if h : n < maskTab.size ∧ w < maskTab.size then
    let v := u &&& maskTab[n]
    if v ≤ maskTab[n - 1]'(by omega) then v &&& maskTab[w]
    else (v + (maskTab[w] - maskTab[n])) &&& maskTab[w]
  else
    let v := modPow2 n u
    if ltPow2 v (n - 1) then modPow2 w v
    else modPow2 w (v + (maskOf w - maskOf n))

/-- A specialized node's reduced value. -/
def SNode.val (vals : Array Nat) : SNode → Nat
  | .and w a bs => bs.foldl (fun acc o => modPow2 w (acc &&& o.get vals)) (truncNat w (a.get vals))
  | .or w as => as.foldl (fun acc o => modPow2 w (acc ||| o.get vals)) 0
  | .xor w as => as.foldl (fun acc o => modPow2 w (acc ^^^ o.get vals)) 0
  | .ror w as => truncNat w (if as.any (fun o => o.get vals != 0) then 1 else 0)
  | .not w a => bitwiseNatTab w (fun x _ => !x) (a.get vals) 0
  | .mux w s f t => if s.get vals != 0 then truncNat w (t.get vals) else truncNat w (f.get vals)
  | .eq w a bs =>
    let ua := a.get vals
    truncNat w (if bs.all (fun o => o.get vals == ua) then 1 else 0)
  | .ult w a b => truncNat w (if a.get vals < b.get vals then 1 else 0)
  | .ugt w a b => truncNat w (if a.get vals > b.get vals then 1 else 0)
  | .getMask w x s mask => truncNat w ((x.get vals >>> s) &&& mask)
  | .sext w a aw n => sextNat w aw (a.get vals) (n.get vals)
  | .sra w x xw k => sraNat w xw (x.get vals) (k.get vals)
  | .shl w a ks =>
    let u := a.get vals
    ks.foldl (fun acc o => modPow2 w (acc ^^^ shlStepNat w u (o.get vals))) 0
  | .generic _ => 0

/-! ## Preparation -/

/-- A design prepared for staged steps. -/
structure Prep where
  D     : DesignCert
  /-- the width of every bit-vector slot (sources, then nodes) -/
  W     : Array Nat
  /-- the slot keeps its exact value in `memv` (a memory, a generic node) -/
  side  : Array Bool
  nodes : Array SNode
  /-- the value array a step starts from: constant sources filled in, every other slot 0 -/
  vals0 : Array Nat
  memv0 : Array CertVal
  /-- the sources a step reads: every source but the constants (already in `vals0`) -/
  srcIdx : Array Nat

def srcWidth : SourceDesc → Nat
  | .input _ w | .const w _ | .flopQ _ w | .flopQAsync _ w _ _ _ => w
  | _ => 0

def srcSide : SourceDesc → Bool
  | .memImg .. | .memConst .. => true
  | _ => false

def srcConst : SourceDesc → Option Nat
  | .const w v => some (modNat w v)
  | _ => none

/-- Slot `r` as an operand: a constant source's value, or the slot. -/
def opdOf (D : DesignCert) (r : Nat) : Opd :=
  match D.sources[r]? with
  | some (.const w v) => .lit (modNat w v)
  | _ => .slot r

/-- The width of slot `r` when it is fixed at load time (a bit-vector source, or a node already
specialized), `none` for a side slot. -/
def plainWidth (W : Array Nat) (side : Array Bool) (r : Nat) : Option Nat :=
  if r < W.size ∧ side.getD r true = false then some W[r]! else none

/-- `s` and `mask = 2 ^ L - 1` when the constant `M` is one run of `L` ones from bit `s`
that fits the width `mw`. -/
def runOf (M mw : Nat) : Option (Nat × Nat) :=
  let s := lowBitPos M
  let L := Nat.log2 ((M >>> s) + 1)
  if s + L ≤ mw ∧ M = (2 ^ L - 1) * 2 ^ s then some (s, 2 ^ L - 1) else none

/-- Node `c`, specialized against the slots before it (widths `W`, side flags `side`). -/
def specialize (D : DesignCert) (W : Array Nat) (side : Array Bool) (c : DenseNodeCert) : SNode :=
  let w := c.width
  let ds := c.deps
  let o := fun (k : Nat) => opdOf D (ds.getD k 0)
  let rest := (ds.extract 1 ds.size).map (opdOf D)
  match c.op with
  | .Op_And => if ds.size ≥ 1 then .and w (o 0) rest else .generic c
  | .Op_Or => .or w (ds.map (opdOf D))
  | .Op_Xor => .xor w (ds.map (opdOf D))
  | .Op_Ror => .ror w (ds.map (opdOf D))
  | .Op_Not => if ds.size = 1 then .not w (o 0) else .generic c
  | .Op_MuxBool => if ds.size = 3 then .mux w (o 0) (o 1) (o 2) else .generic c
  | .Op_EQ => if ds.size ≥ 1 then .eq w (o 0) rest else .generic c
  | .Op_ULT | .Op_LT => if ds.size = 2 then .ult w (o 0) (o 1) else .generic c
  | .Op_UGT | .Op_GT => if ds.size = 2 then .ugt w (o 0) (o 1) else .generic c
  | .Op_GetMask =>
    if ds.size = 2 then
      match D.sources[ds.getD 1 0]? with
      | some (.const mw v) =>
        match runOf (modNat mw v) mw with
        | some (s, mask) => .getMask w (o 0) s mask
        | none => .generic c
      | _ => .generic c
    else .generic c
  | .Op_Sext =>
    if ds.size = 2 then
      match plainWidth W side (ds.getD 0 0) with
      | some aw => .sext w (o 0) aw (o 1)
      | none => .generic c
    else .generic c
  | .Op_SRA =>
    if ds.size = 2 then
      match plainWidth W side (ds.getD 0 0) with
      | some xw => .sra w (o 0) xw (o 1)
      | none => .generic c
    else .generic c
  | .Op_SHL => if ds.size ≥ 1 then .shl w (o 0) rest else .generic c
  | _ => .generic c

def SNode.isGen : SNode → Bool
  | .generic _ => true
  | _ => false

/-- The side flags of every slot: a source's (`srcSide`), then each node's, the node
specialized against the flags before it. -/
def sideOf (D : DesignCert) (W : Array Nat) : Array Bool :=
  D.nodes.foldl (fun side c => side.push (specialize D W side c).isGen) (D.sources.map srcSide)

def prepare (D : DesignCert) : Prep :=
  let W := D.sources.map srcWidth ++ D.nodes.map (·.width)
  let side := sideOf D W
  { D, W, side
    nodes := D.nodes.map (specialize D W side)
    vals0 := D.sources.map (fun sd => (srcConst sd).getD 0) ++ Array.replicate D.nodes.size 0
    memv0 := Array.replicate (D.sources.size + D.nodes.size) (.bv (mk_bv 0 0))
    srcIdx := ((List.range D.sources.size).filter fun r =>
      (srcConst (D.sources[r]?.getD (.const 0 0))).isNone).toArray }

/-! ## A step -/

/-- Slot `r`'s exact value. -/
@[inline] def exact (P : Prep) (vals : Array Nat) (memv : Array CertVal) (r : Nat) : CertVal :=
  if P.side.getD r false then memv.getD r (.bv (mk_bv 0 0))
  else .bv { width := P.W.getD r 0, value := .ofNat (vals.getD r 0) }

/-- A bit-vector source's reduced value: `uintNat` of `sourceValue`. -/
@[inline] def srcNat (i : RuntimeInput) (s : RuntimeState) : SourceDesc → Nat
  | .input idx w => truncNat w (uintNat (i[idx]?.getD (mk_bv w 0)))
  | .const w v => modNat w v
  | .flopQ idx w => truncNat w (uintNat (s.flops[idx]?.getD (mk_bv w 0)))
  | .flopQAsync idx w ri rv al =>
      let r : BV := i[ri]?.getD (mk_bv 1 0)
      let asserted : Bool := if al then !(bv_nonzero r) else bv_nonzero r
      if asserted then modNat w rv else truncNat w (uintNat (s.flops[idx]?.getD (mk_bv w 0)))
  | _ => 0

/-- The sources `srcIdx[k]`, `srcIdx[k + 1]`, ... (the constants are already in `vals0`). -/
def srcLoop (P : Prep) (i : RuntimeInput) (s : RuntimeState) (k : Nat) (vals : Array Nat)
    (memv : Array CertVal) : Array Nat × Array CertVal :=
  if h : k < P.srcIdx.size then
    let r := P.srcIdx[k]
    let sd := P.D.sources[r]?.getD (.const 0 0)
    if srcSide sd then srcLoop P i s (k + 1) vals (memv.set! r (sourceValue i s sd))
    else srcLoop P i s (k + 1) (vals.set! r (srcNat i s sd)) memv
  else (vals, memv)
termination_by P.srcIdx.size - k

/-- The nodes from `j` on. -/
def nodeLoop (P : Prep) (j : Nat) (vals : Array Nat) (memv : Array CertVal) :
    Array Nat × Array CertVal :=
  if h : j < P.nodes.size then
    let slot := P.D.sources.size + j
    match P.nodes[j] with
    | .generic c =>
      let v := eval_op_cert c.op c.width (c.deps.toList.map (exact P vals memv))
      nodeLoop P (j + 1) (vals.set! slot (uintNat v.asBV)) (memv.set! slot v)
    | n => nodeLoop P (j + 1) (vals.set! slot (n.val vals)) memv
  else (vals, memv)
termination_by P.nodes.size - j

/-- Every slot of a step. -/
def run (P : Prep) (i : RuntimeInput) (s : RuntimeState) : Array Nat × Array CertVal :=
  let (v1, m1) := srcLoop P i s 0 P.vals0 P.memv0
  nodeLoop P 0 v1 m1

/-- `directFlopNext`, reading slots through `rho`. -/
def flopNextRho (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) : BV :=
  let inReset : Bool :=
    match f.resetPin with
    | none   => false
    | some r => let rv := bv_nonzero (rho r).asBV
                if f.resetActiveLow then !rv else rv
  let edge : Bool := fires e f.clock
  if inReset && (edge || f.asyncReset) then mk_bv f.width f.resetValue
  else
    let en : Bool :=
      match f.enable with
      | none    => true
      | some en => bv_nonzero (rho en).asBV
    if edge && en then bv_resize f.width (rho f.din).asBV
    else s.flops[idx]?.getD (mk_bv f.width 0)

/-- One staged step. -/
def step (P : Prep) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let (vals, memv) := run P i s
  let rho := exact P vals memv
  { outputs   := P.D.outputs.map fun o => ofNatLt o.width (truncNat o.width (vals.getD o.slot 0))
    nextState :=
      { flops := P.D.flops.mapIdx fun idx f => flopNextRho rho e s idx f
        mems  := memsOfVals (P.D.memories.mapIdx fun idx m => srcMemNextVal rho e s idx m) } }

/-- `directStepChecked`, staged: the same runtime shape check, then `step`. -/
def stepChecked (P : Prep) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Except SimError RuntimeResult :=
  match checkRuntime P.D e i s with
  | .error err => .error err
  | .ok _ => .ok (step P e i s)

/-- How many nodes `prepare` specialized (the rest are `generic`). -/
def specializedCount (P : Prep) : Nat :=
  P.nodes.foldl (fun k n => match n with | .generic _ => k | _ => k + 1) 0

end Staged
end Compiler
