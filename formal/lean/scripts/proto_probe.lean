/-
Bounded A/B of the REAL `projectDesign` pipeline against the diagnostic fork.

  reference   Projection.Hw.projectDesign      (the proved specializer)
  fast        Projection.ProtoFast.mixDriver   (same driver, two changes)

Both produce the SHARED `Program`, so residuals are compared with `==`, and
then run through `evalFuel` on the same stimulus so the comparison is semantic
and not only syntactic.

EXIT STATUS IS MEANINGFUL: 0 only when every case compared equal.  1 = a
residual or result mismatch; 2 = one side failed to specialize.  An earlier
version printed FAILED and still returned 0.

TIMING: use `--ref` / `--fast` single-side modes and time the PROCESS from
outside.  In-process timestamps are not trustworthy here -- Lean reorders pure
work across `IO.monoMsNow`, verified in the generated C.
-/
import LeanSemanticPrimitives.Projection.ProjectedStep
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast
import LeanSemanticPrimitives.Compiler.CertIO

open Compiler Projection

/-- Node `i` reads its predecessor (depth 0) and source 0 (deep). -/
def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- EVERY dep at maximum depth -- the worst case for slot reads. -/
def fanD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun _ =>
                { op := .Op_And, width := 4, deps := #[0, 1] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- SEQUENTIAL: a flop on every node, so the edge vector and the flop-commit
path are exercised too, not only the combinational environment. -/
def flopD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := (List.range n).toArray.map (fun i =>
                { width := 4, din := 2 + i, enable := none, resetPin := none
                , resetValue := 0, resetActiveLow := false })
  memories := #[]

def shapeOf : String → Nat → Option DesignCert
  | "chain", n => some (chainD n)
  | "fan",   n => some (fanD n)
  | "flop",  n => some (flopD n)
  | _,       _ => none

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b      => 1 + tsize a + tsize b
  | .ite a b c      => 1 + tsize a + tsize b + tsize c
  | .caseT s as     => 1 + tsize s + (as.map (fun a => tsize a.2.2)).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts => 1 + (ts.map tsize).foldl (·+·) 0

def szOf (R : Program) : Nat := (R.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0

/-- Count occurrences of one primitive in a term. -/
partial def countPrim (q : Prim) : Term → Nat
  | .prim p ts  => (if p == q then 1 else 0) + (ts.map (countPrim q)).foldl (·+·) 0
  | .lit _ | .var _ => 0
  | .letIn a b  => countPrim q a + countPrim q b
  | .ite a b c  => countPrim q a + countPrim q b + countPrim q c
  | .caseT s as => countPrim q s + (as.map (fun a => countPrim q a.2.2)).foldl (·+·) 0
  | .ctorT _ ts | .call _ ts => (ts.map (countPrim q)).foldl (·+·) 0

/-- Term-constructor census: lit, var, letIn, ite, prim, ctorT, caseT, call. -/
def add8 (a b : Array Nat) : Array Nat :=
  (List.range 8).toArray.map (fun i => (a[i]!) + (b[i]!))

partial def shape8 : Term → Array Nat
  | .lit _      => #[1,0,0,0,0,0,0,0]
  | .var _      => #[0,1,0,0,0,0,0,0]
  | .letIn a b  => add8 #[0,0,1,0,0,0,0,0] (add8 (shape8 a) (shape8 b))
  | .ite a b c  => add8 #[0,0,0,1,0,0,0,0] (add8 (shape8 a) (add8 (shape8 b) (shape8 c)))
  | .prim _ ts  => ts.foldl (fun acc t => add8 acc (shape8 t)) #[0,0,0,0,1,0,0,0]
  | .ctorT _ ts => ts.foldl (fun acc t => add8 acc (shape8 t)) #[0,0,0,0,0,1,0,0]
  | .caseT s as => as.foldl (fun acc a => add8 acc (shape8 a.2.2))
                     (add8 #[0,0,0,0,0,0,1,0] (shape8 s))
  | .call _ ts  => ts.foldl (fun acc t => add8 acc (shape8 t)) #[0,0,0,0,0,0,0,1]

def profileResidual (R : Program) : IO Unit := do
  let b := (R.funs.map (fun fd => fd.body))
  let tot := szOf R
  let sh  := b.foldl (fun acc t => add8 acc (shape8 t)) #[0,0,0,0,0,0,0,0]
  let cp := fun (q : Prim) => (b.map (countPrim q)).foldl (·+·) 0
  IO.println s!"  terms {tot}   lit {sh[0]!} var {sh[1]!} letIn {sh[2]!} ite {sh[3]!} \
prim {sh[4]!} ctorT {sh[5]!} caseT {sh[6]!} call {sh[7]!}"
  IO.println s!"  prims: tl {cp .tl}  hd {cp .hd}  consP {cp .consP}  isNil {cp .isNil}  \
bvResize {cp .bvResize}  bvAnd {cp .bvAnd}  bvMk {cp .bvMk}  eqI {cp .eqI}"

/-- SOURCE-HEAVY fixture: `nsrc` primary inputs and `nnode` nodes, varied
INDEPENDENTLY.  `I_hw` reads a primary input with `nthD inp idx`, which unrolls
to `idx` residual `tl` steps -- a chain over the RUNTIME input vector, which is
a different thing from the slot-environment lookup Phase 1 eliminated. -/
def srcD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i => .input i 4)
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := #[i % nsrc, (i + 1) % nsrc] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- SAME source count as `srcD`, but every node reads the TWO MOST RECENTLY
PUSHED slots, so each slot read sits at depth 0/1 no matter how many sources
there are.  This is the discriminator: `srcD` varies source count AND read
depth together (its nodes read slot `i % nsrc`, at the BOTTOM of the
environment); `shalD` varies source count with read depth PINNED near zero.
If the residual is flat in `nsrc` here and grows in `srcD`, the driver is the
DEPTH of the slot read, not the number of sources.  Requires `nsrc >= 2`. -/
def shalD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i => .input i 4)
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := #[nsrc + i - 1, nsrc + i - 2] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- `nsrc` sources of which all but two are CONSTANTS -- the ALU's shape
(4 inputs, 4041 consts).  Reads are deep, as in `srcD`, so this isolates
whether a STATIC source entry costs the same as a dynamic one. -/
def cstD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := #[i % nsrc, (i + 1) % nsrc] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- THE OTHER HALF of the discriminator.  Only 2 sources, so the source prefix
is negligible, but every node reads NODE 0's result -- which sinks deeper into
the environment with every node that follows.  Node-portion read depth therefore
sums to ~nnode^2/2 while source-portion depth stays ~nnode.

If the residual stays LINEAR here, walking the NODE portion of the chain is
free (the structural peel fires) and only the SOURCE portion is paid for. -/
def midD (nnode : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2, 2 + i - 1] })
  outputs  := #[{ slot := 2 + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

@[inline] def refOf  (D : DesignCert) : Except MixError Program := Hw.projectDesign D
@[inline] def fastOf (D : DesignCert) : Except ProtoFast.MixError Program :=
  ProtoFast.mixDriver 20000 200 Hw.hwAP [encDesign D]

/-- Same, with the fuel overridden -- a DIAGNOSTIC knob, to tell "ran out of
the hardcoded fuel" apart from any other failure.  `projectDesign` itself is
unchanged. -/
@[inline] def fastOfFuel (sf wf : Nat) (D : DesignCert) : Except ProtoFast.MixError Program :=
  ProtoFast.mixDriver sf wf Hw.hwAP [encDesign D]

/-- One cycle, decoded, so the A/B comparison is semantic.  A sequential design
needs a correctly sized state, so the state is built from the design. -/
def runOne (R : Program) (D : DesignCert) : Option RuntimeResult :=
  let e := allEdges D
  let i : RuntimeInput := #[mk_bv 4 5, mk_bv 4 9]
  let s : RuntimeState := { flops := D.flops.map (fun f => mk_bv f.width 3), mems := #[] }
  match evalFuel 4000000 R []
      (.call R.entry [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | .value v => decResult v
  | _        => none

def sameResult : Option RuntimeResult → Option RuntimeResult → Bool
  | some a, some b => encResult a == encResult b
  | none,   none   => false          -- neither produced a value: NOT agreement
  | _,      _      => false

/-- returns 0 ok, 1 mismatch, 2 a side failed -/
def compare (shape : String) (D : DesignCert) (n : Nat) : IO UInt32 := do
  match refOf D, fastOf D with
  | .ok R, .ok F =>
      let syn := R == F
      let ra := runOne R D
      let fa := runOne F D
      let sem := sameResult ra fa
      IO.println s!"{shape} n={n}  ref size {szOf R}  fast size {szOf F}  \
identical {syn}  same-result {sem}  ref-ran {ra.isSome}  fast-ran {fa.isSome}"
      if syn && sem then return 0 else do
        IO.eprintln s!"MISMATCH on {shape} n={n}: identical={syn} same-result={sem}"
        return 1
  | .error _, _ => do IO.eprintln s!"FAILED: reference on {shape} n={n}"; return 2
  | _, .error _ => do IO.eprintln s!"FAILED: fast on {shape} n={n}"; return 2

def one (shape : String) (n : Nat) (side : String) : IO UInt32 := do
  match shapeOf shape n with
  | none => do IO.eprintln s!"unknown shape {shape}"; return 64
  | some D =>
      if side == "ref" then
        match refOf D with
        | .ok R    => do IO.println s!"ref  {shape} n={n} size {szOf R}"; return 0
        | .error _ => do IO.eprintln s!"ref  {shape} n={n} FAILED"; return 2
      else if side == "fast" then
        match fastOf D with
        | .ok R    => do IO.println s!"fast {shape} n={n} size {szOf R}"; return 0
        | .error _ => do IO.eprintln s!"fast {shape} n={n} FAILED"; return 2
      else compare shape D n

/-- Load a real DCERT1 certificate and specialize it, one side or both. -/
def onFile (path : String) (side : String) : IO UInt32 := do
  let D ← CertIO.loadCert path
  IO.println s!"{path}: sources {D.sources.size} nodes {D.nodes.size} \
outputs {D.outputs.size} flops {D.flops.size} memories {D.memories.size}"
  if side == "ref" then
    match refOf D with
    | .ok R    => do IO.println s!"ref  size {szOf R}"; return 0
    | .error _ => do IO.eprintln "ref  FAILED"; return 2
  else if side == "fast" then
    match fastOf D with
    | .ok R    => do IO.println s!"fast size {szOf R}"; return 0
    | .error e => do IO.eprintln s!"fast FAILED ({repr e})"; return 2
  else compare path D D.nodes.size

def onFileFuel (path : String) (sf wf : Nat) : IO UInt32 := do
  let D ← CertIO.loadCert path
  IO.println s!"{path}: nodes {D.nodes.size}  (fast, stepFuel {sf} wlFuel {wf})"
  match fastOfFuel sf wf D with
  | .ok R    => do IO.println s!"fast size {szOf R}"; return 0
  | .error e => do IO.eprintln s!"fast FAILED ({repr e})"; return 2

def main (args : List String) : IO UInt32 := do
  match args with
  | "--grid" :: _ => do
      -- vary sources and nodes INDEPENDENTLY
      let pairs : List (Nat × Nat) :=
        [(2,64),(2,128),(2,256),(2,512),
         (16,64),(64,64),(128,64),(256,64),(512,64),
         (64,128),(64,256)]
      let mut bad := false
      for (ns, nn) in pairs do
        match fastOf (srcD ns nn) with
        | .error e => do
            IO.eprintln s!"srcD nsrc={ns} nnode={nn}: FAILED ({repr e})"; bad := true
        | .ok R => do
            IO.println s!"srcD nsrc={ns} nnode={nn}"
            profileResidual R
      return (if bad then 2 else 0)
  | "--node-depth" :: _ => do
      -- node-portion read depth grows as ~nnode^2/2; source prefix is 2.
      let mut bad := false
      for n in [64, 128, 256, 512, 1024] do
        match fastOf (midD n) with
        | .error e => do IO.eprintln s!"midD n={n}: FAILED ({repr e})"; bad := true
        | .ok R => do
            IO.println s!"midD nnode={n}  (node-portion depth sum ~ {n * (n-1) / 2})"
            profileResidual R
      return (if bad then 2 else 0)
  | "--depth-grid" :: _ => do
      -- THE DISCRIMINATOR: source count varied at DEEP reads (`srcD`), at
      -- SHALLOW reads (`shalD`), and with the sources made STATIC (`cstD`).
      -- Node count is held at 64 throughout, so nothing here can be explained
      -- by node count.
      let pairs : List (Nat × Nat) :=
        [(2,64),(16,64),(64,64),(128,64),(256,64),(512,64),(1024,64)]
      let fams : List (String × (Nat → Nat → DesignCert)) :=
        [("srcD-deep", srcD), ("shalD-shallow", shalD), ("cstD-const-deep", cstD)]
      let mut bad := false
      for (nm, f) in fams do
        for (ns, nn) in pairs do
          match fastOf (f ns nn) with
          | .error e => do
              IO.eprintln s!"{nm} nsrc={ns} nnode={nn}: FAILED ({repr e})"; bad := true
          | .ok R => do
              IO.println s!"{nm} nsrc={ns} nnode={nn}"
              profileResidual R
      return (if bad then 2 else 0)
  | "--file-profile" :: p :: a :: b :: _ => do
      let D ← CertIO.loadCert p
      IO.println s!"{p}: sources {D.sources.size} nodes {D.nodes.size} flops {D.flops.size}"
      match fastOfFuel ((a.toNat?).getD 20000) ((b.toNat?).getD 200) D with
      | .error e => do IO.eprintln s!"FAILED ({repr e})"; return 2
      | .ok R    => do profileResidual R; return 0
  | "--file-fuel" :: p :: a :: b :: _ =>
      onFileFuel p ((a.toNat?).getD 20000) ((b.toNat?).getD 200)
  | "--file"      :: p :: _  => onFile p "cmp"
  | "--file-ref"  :: p :: _  => onFile p "ref"
  | "--file-fast" :: p :: _  => onFile p "fast"
  | "--ref"  :: sh :: a :: _ => one sh ((a.toNat?).getD 0) "ref"
  | "--fast" :: sh :: a :: _ => one sh ((a.toNat?).getD 0) "fast"
  | sh :: rest => do
      let mut rc : UInt32 := 0
      for a in rest do
        match a.toNat? with
        | some n => do let r ← one sh n "cmp"; if r != 0 then rc := r
        | none   => pure ()
      if rc != 0 then IO.eprintln s!"EXIT {rc}: at least one case did not match"
      return rc
  | [] => do IO.eprintln "usage: proto_probe [--ref|--fast] <chain|fan|flop> <n>..."; return 64
