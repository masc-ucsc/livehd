/-
`sym_refmap` -- refinement maps: an implementation certificate refines a spec certificate with
stuttering, and the spec's own facts.

  sym_refmap refmap <file.refmap> [jobs:<n>] [runs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>]
                    [cache] [solver:<cadical>] [work:<dir>]
  sym_refmap specfacts <file.refmap> [jobs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>]
                    [cache] [solver:<cadical>] [work:<dir>]

The `.refmap` file names both certificates and everything else by the names in their
`_io.json` sidecars; its grammar is in `pass/lean/tools/formats/refmap.md`.

`refmap` proves, in this order:
- the implementation's invariant (the `inv` conj spec, as `sym_verify conj:`);
- the base: from the all-zero state through `reset` cycles, the active output and the
  initial map hold;
- per case of the invariant's split, one obligation per target: comm per spec register group,
  allow and obs per slot, active, and rankLe/rankLt/rankSel per ranked channel.
The verdict is the verified check `runRefMapOf` (`runRefMapOf_sound`), evaluated over the
runs built here: `runConjTOf && sideRM && checkRM`, which is `runRefMapOf` by definition.

`specfacts` proves the spec's Moore and confluence facts (`runSpecFactsOf`,
`runSpecFactsOf_sound`).

`emit:` writes a theorem file for the verdict, stated as `Check.RefMapChecked` /
`Check.SpecFactsChecked` on the fused route (`runRefMapF`, `runSpecFactsF`), with the
certificates included as text. `f2` (the default) embeds the proofs and proves the statement
by `native_decide`; `f1` states it as one named axiom whose docstring records the certificates'
and the proofs' SHA-256 (for proofs too large to embed). `Check.refmap_compose_ref`,
`Check.start_ref` and `Check.refmap_progress_ref` take these statements.

`cache` reuses an LRAT proof already in `work/` when the obligation's CNF there is
byte-identical, without running the solver. The verified check re-checks every proof
either way, so the cache saves solver time only; a wrong or stale proof is rejected.

A satisfiable target is decoded through its case's template. The report
`<work>/cti_<target>.txt` gives, by spec register name, the map's state at cycle `n`, the
spec's state after the cycle's events, and the map's state at `n+1`, plus the slots that fired
and the nonzero inputs. Untrusted diagnostics: no verdict uses it.
-/
import HDLProof.Check.RefMapCompose
import HDLProof.Kit.Driver

open Compiler Projection HDLProof HDLProof.Sym HDLProof.Kit HDLProof.Check HDLProof.Driver
open Projection.MemProjection Projection.HwMem Std.Tactic.BVDecide

/-! ## The file -/

structure RMLine where
  key  : String
  args : List String

def parseRM (text : String) : List RMLine := Id.run do
  let mut out := []
  for raw in text.splitOn "\n" do
    let line := ((raw.splitOn "#").headD "").replace "\t" " " |>.trimAscii |>.toString
    if line.isEmpty then continue
    match (line.splitOn " ").filter (fun w => !w.isEmpty) with
    | [] => continue
    | k :: as => out := out ++ [{ key := k, args := as }]
  return out

/-- A name → ordinal index of a sidecar kind. -/
def nameIdx (ports : Array (String × Nat)) : Std.HashMap String Nat := Id.run do
  let mut m := {}
  for ((n, _), k) in ports.toList.zipIdx do
    unless n.isEmpty do m := m.insert n k
  return m

structure Names where
  ins   : Std.HashMap String Nat
  outs  : Std.HashMap String Nat
  outW  : Array (String × Nat)
  inW   : Array (String × Nat)
  flops : Std.HashMap String Nat
  flopN : Array (String × Nat)

def loadNames (cert : String) : IO Names := do
  let i ← ioPorts cert "inputs"
  let o ← ioPorts cert "outputs"
  let f ← ioPorts cert "flops"
  return { ins := nameIdx i, outs := nameIdx o, outW := o, inW := i, flops := nameIdx f, flopN := f }

/-! ## Port words, Booleans and observed words -/

def parsePort (N : Names) (tok : String) : Except String PortW := do
  if let some r := tok.dropPrefix? "out:" then
    let r := r.toString
    let (nm, nxt) := match r.dropSuffix? "@next" with
      | some b => (b.toString, true)
      | none => (r, false)
    match N.outs[nm]? with
    | some o => pure (.out o nxt)
    | none => throw s!"no implementation output {repr nm}"
  else if let some r := tok.dropPrefix? "in:" then
    match N.ins[r.toString]? with
    | some k => pure (.inp k)
    | none => throw s!"no implementation input {repr r.toString}"
  else throw s!"a port is out:<name>[@next] or in:<name>, got {repr tok}"

def portWidth (N : Names) : PortW → Nat
  | .out o _ => (N.outW.getD o ("", 0)).2
  | .inp k => (N.inW.getD k ("", 0)).2

mutual
/-- A Boolean over ports: `!`, `&`, `|`, parentheses; atoms `<port>`, `<port>==<n>`, `1`, `0`. -/
partial def bitExpr (N : Names) (cs : Array Char) (i : Nat) : Except String (BitE × Nat) := do
  let (a, i) ← bitTerm N cs i
  let mut acc := a
  let mut j := i
  while j < cs.size && cs[j]! == '|' do
    let (b, k) ← bitTerm N cs (j + 1)
    acc := .or acc b
    j := k
  pure (acc, j)

partial def bitTerm (N : Names) (cs : Array Char) (i : Nat) : Except String (BitE × Nat) := do
  let (a, i) ← bitFactor N cs i
  let mut acc := a
  let mut j := i
  while j < cs.size && cs[j]! == '&' do
    let (b, k) ← bitFactor N cs (j + 1)
    acc := .and acc b
    j := k
  pure (acc, j)

partial def bitFactor (N : Names) (cs : Array Char) (i : Nat) : Except String (BitE × Nat) := do
  if i < cs.size && cs[i]! == '!' then
    let (a, j) ← bitFactor N cs (i + 1)
    pure (.not a, j)
  else if i < cs.size && cs[i]! == '(' then
    let (a, j) ← bitExpr N cs (i + 1)
    unless j < cs.size && cs[j]! == ')' do throw "missing ')'"
    pure (a, j + 1)
  else
    let mut j := i
    while j < cs.size && !"&|()!".contains cs[j]! do j := j + 1
    let atom := String.ofList (cs.extract i j).toList
    if atom == "1" then pure (.tt, j)
    else if atom == "0" then pure (.ff, j)
    else match atom.splitOn "==" with
      | [p] => pure (.nz (← parsePort N p), j)
      | [p, v] =>
          let some n := v.toNat? | throw s!"bad constant in {repr atom}"
          pure (.eqc (← parsePort N p) n, j)
      | _ => throw s!"cannot read {repr atom}"
end

def parseBitE (N : Names) (s : String) : Except String BitE := do
  let cs := s.toList.toArray
  let (e, j) ← (bitExpr N cs 0).mapError (s!"{repr s}: " ++ ·)
  unless j == cs.size do throw s!"trailing text in {repr s}"
  pure e

/-- An observed word: `<port>`, `<port>[lo:len]`, optionally `?<port>.<bit>`. -/
def parseObsW (N : Names) (tok : String) : Except String ObsW := do
  let (body, guardTxt) := match tok.splitOn "?" with
    | [b, g] => (b, some g)
    | _ => (tok, none)
  let (pTxt, slice) := match body.splitOn "[" with
    | [p, r] => (p, some (r.dropEnd 1).toString)
    | _ => (body, none)
  let port ← parsePort N pTxt
  let (lo, len) ← match slice with
    | none => pure (0, portWidth N port)
    | some sl => match sl.splitOn ":" with
      | [a, b] => match a.toNat?, b.toNat? with
        | some x, some y => pure (x, y)
        | _, _ => throw s!"bad slice in {repr tok}"
      | _ => throw s!"a slice is [lo:len], got {repr tok}"
  let guard ← match guardTxt with
    | none => pure none
    | some g => match g.splitOn "." with
      | [p, b] => match b.toNat? with
        | some k => pure (some (← parsePort N p, k))
        | none => throw s!"bad guard bit in {repr tok}"
      | _ => throw s!"a guard is <port>.<bit>, got {repr tok}"
  pure { port, lo, len, guard }

/-! ## Building the spec -/

def argOf (args : List String) (k : String) : Option String :=
  args.findSome? fun t => (t.dropPrefix? (k ++ ":")).map (·.toString)

def listArg (args : List String) (k : String) : List String :=
  match argOf args k with
  | some s => (s.splitOn ",").filter (!·.isEmpty)
  | none => []

def specProp (NS : Names) (tok : String) : Except String PropSpec := do
  let (nz, nm) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
  match NS.outs[nm]? with
  | some o => pure { out := o, nz }
  | none => throw s!"no spec output {repr nm}"

def implProp (NI : Names) (tok : String) : Except String PropSpec := do
  let (nz, nm) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
  match NI.outs[nm]? with
  | some o => pure { out := o, nz }
  | none => throw s!"no implementation output {repr nm}"

/-- The implementation's invariant from a conj spec file (the `sym_verify conj:` format). -/
def buildInv (implCert : String) (NI : Names) (path : Option String) (asmOut : Option PropSpec)
    (asmIn : List (Nat × Int)) (fuel : Nat) (inW : List Nat) (L : Shape) :
    IO (Except String (ConjTSpec × Array String)) := do
  let base : ConjTSpec := { conjs := [], asmOut, asmIn, fuel, inW, L }
  let some p := path | return .ok (base, #[])
  let lines ← match parseSpec (← IO.FS.readFile p) with
    | .ok l => pure l
    | .error e => return .error s!"{p}: {e}"
  let mut conjs : Array ConjK := #[]
  let mut cname : Array String := #[]
  let mut groups : Std.HashMap String (List Nat) := {}
  for l in lines do
    let mut ks : List ConjK := []
    match l.kind with
    | ["eq", a, b] =>
        let some ba := resolveReg NI.flops a | return .error s!"{l.name}: no register {repr a}"
        let some bb := resolveReg NI.flops b | return .error s!"{l.name}: no register {repr b}"
        unless ba.length == bb.length do return .error s!"{l.name}: {a} and {b} differ in width"
        ks := (ba.zip bb).map fun (f, g) => .eq f g
    | ["eqif", c, a, b] =>
        let cas ← match parseCase NI.flops L.fws c with
          | .ok x => pure x
          | .error e => return .error s!"{l.name}: {e}"
        let some ba := resolveReg NI.flops a | return .error s!"{l.name}: no register {repr a}"
        let some bb := resolveReg NI.flops b | return .error s!"{l.name}: no register {repr b}"
        unless ba.length == bb.length do return .error s!"{l.name}: {a} and {b} differ in width"
        ks := (ba.zip bb).map fun (f, g) => .eqIf cas f g
    | ["oneof", cs] =>
        let mut cases : List Case := []
        for c in (cs.splitOn ";").filter (fun w => !w.isEmpty) do
          match parseCase NI.flops L.fws c with
          | .ok x => cases := cases ++ [x]
          | .error e => return .error s!"{l.name}: {e}"
        ks := [.oneOf cases]
    | [tok] =>
        match implProp NI tok with
        | .ok p => ks := [.out p]
        | .error e => return .error s!"{l.name}: {e}"
    | k => return .error s!"{l.name}: cannot read {k}"
    let start := conjs.size
    for (k, i) in ks.zipIdx do
      conjs := conjs.push k
      cname := cname.push (if ks.length == 1 then l.name else s!"{l.name}[{i}]")
    groups := groups.insert l.name ((List.range ks.length).map (start + ·))
  let n := conjs.size
  let mut hyps : Array (Option (List Nat)) := Array.replicate n none
  let mut split : Array (Option Nat) := Array.replicate n none
  for l in lines do
    let some mine := groups[l.name]? | continue
    if let some hs := l.hyps then
      let mut idx : List Nat := []
      for h in hs do
        let some g := groups[h]? | return .error s!"{l.name}: no conjunct named {repr h}"
        idx := idx ++ g
      for t in mine do hyps := hyps.set! t (some idx)
    if let some sname := l.split then
      match groups[sname]? with
      | some [s] =>
          match conjs[s]? with
          | some (.oneOf _) => for t in mine do split := split.set! t (some s)
          | _ => return .error s!"{l.name}: split {sname} is not a oneof"
      | _ => return .error s!"{l.name}: split {sname} is not a oneof"
  return .ok ({ base with conjs := conjs.toList, hyps := hyps.toList, split := split.toList }, cname)

structure Built where
  sp       : RefMapSpec
  conjName : Array String
  chanName : Array String
  slotName : Array String
  specNames : Names
  implNames : Names

/-- The refinement spec of a `.refmap` file. -/
def buildRM (lines : List RMLine) (implCert specCert : String) (invPath : Option String)
    (DI DS : DesignCert) (fuelI fuelS : Nat) (facts : Bool) : IO (Except String Built) := do
  let NI ← loadNames implCert
  let NS ← loadNames specCert
  let get (k : String) : List (List String) := (lines.filter (·.key == k)).map (·.args)
  let one (k : String) : Option (List String) := (get k).head?
  -- cycle assumptions
  let asmOut ← match one "asm" with
    | some [tok] => match implProp NI tok with
      | .ok p => pure (some p)
      | .error e => return .error e
    | _ => pure none
  let mut asmIn : List (Nat × Int) := []
  for a in get "in" do
    match a with
    | [tok] => match tok.splitOn "=" with
      | [nm, vs] =>
          let some k := NI.ins[nm]? | return .error s!"no implementation input {repr nm}"
          let some v := vs.toInt? | return .error s!"bad value {repr vs}"
          asmIn := asmIn ++ [(k, v)]
      | _ => return .error s!"in <input>=<value>, got {repr tok}"
    | _ => return .error "in <input>=<value>"
  let LI := shapeOf DI
  let inWI := inputWidths DI (inputCount DI)
  let inv ← match ← buildInv implCert NI invPath asmOut asmIn fuelI inWI LI with
    | .ok x => pure x
    | .error e => return .error e
  let (invSp, conjName) := inv
  let R0 := ((one "reset").bind (·.head?) |>.bind (·.toNat?)).getD 1
  let active ← match one "active" with
    | some [tok] => match implProp NI tok with
      | .ok p => pure (some p)
      | .error e => return .error e
    | _ => pure none
  let LS := shapeOf DS
  let nS := LS.fws.length
  -- the map and the initial state, one entry per spec flop
  let mut alpha : Array (Option Nat) := Array.replicate nS none
  for a in get "alpha" do
    match a with
    | [sf, io] =>
        let some k := NS.flops[sf]? | return .error s!"no spec register {repr sf}"
        let some o := NI.outs[io]? | return .error s!"no implementation output {repr io}"
        alpha := alpha.set! k (some o)
    | _ => return .error "alpha <spec-register> <implementation-output>"
  let mut alphaL : List Nat := []
  for (a, k) in alpha.toList.zipIdx do
    match a with
    | some o => alphaL := alphaL ++ [o]
    | none =>
        -- the spec facts do not read the map
        if facts then alphaL := alphaL ++ [0]
        else return .error s!"no alpha for spec register {repr (NS.flopN.getD k ("", 0)).1}"
  let mut init : Array InitE := Array.replicate nS .free
  for a in get "init" do
    match a with
    | [sf, v] =>
        let some k := NS.flops[sf]? | return .error s!"no spec register {repr sf}"
        if v == "free" then init := init.set! k .free
        else if let some o := v.dropPrefix? "out0:" then
          let some oi := NI.outs[o.toString]? | return .error s!"no implementation output {repr o.toString}"
          init := init.set! k (.out0 oi)
        else
          let some z := v.toInt? | return .error s!"init {sf}: bad value {repr v}"
          init := init.set! k (.cnst z)
    | _ => return .error "init <spec-register> <int>|out0:<implementation-output>|free"
  -- channels
  let mut chans : Array ChanS := #[]
  let mut chanName : Array String := #[]
  for a in get "chan" do
    match a with
    | nm :: rest =>
        let some al := argOf rest "allow" | return .error s!"chan {nm}: no allow:"
        let allow ← match specProp NS al with | .ok p => pure p | .error e => return .error e
        let mut sel : List (Nat × Int) := []
        for s in listArg rest "sel" do
          match s.splitOn "=" with
          | [i, v] =>
              let some k := NS.ins[i]? | return .error s!"chan {nm}: no spec input {repr i}"
              let some z := v.toInt? | return .error s!"chan {nm}: bad value {repr v}"
              sel := sel ++ [(k, z)]
          | _ => return .error s!"chan {nm}: sel:<input>=<value>,..."
        let mut respIn : List Nat := []
        for i in listArg rest "resp" do
          let some k := NS.ins[i]? | return .error s!"chan {nm}: no spec input {repr i}"
          respIn := respIn ++ [k]
        let mut obsOut : List Nat := []
        for o in listArg rest "obs" do
          let some k := NS.outs[o]? | return .error s!"chan {nm}: no spec output {repr o}"
          obsOut := obsOut ++ [k]
        let rank ← match argOf rest "rank" with
          | none => pure none
          | some r => match r.splitOn "/" with
            | [ro, so] =>
                let some a := NI.outs[ro]? | return .error s!"chan {nm}: no implementation output {repr ro}"
                let some b := NI.outs[so]? | return .error s!"chan {nm}: no implementation output {repr so}"
                pure (some (a, b))
            | _ => return .error s!"chan {nm}: rank:<rank-output>/<selector-output>"
        chans := chans.push { sel, respIn, allow, obsOut, rank }
        chanName := chanName.push nm
    | _ => return .error "chan <name> allow:<out> ..."
  let chanIdx := nameIdx (chanName.map (·, 0))
  -- slots and requirements
  let mut slots : Array SlotS := #[]
  let mut slotName : Array String := #[]
  for a in get "slot" do
    match a with
    | cn :: kindTxt :: rest =>
        let some c := chanIdx[cn]? | return .error s!"slot: no channel {repr cn}"
        let kind ← match parseBitE NI kindTxt with | .ok k => pure k | .error e => return .error s!"slot {cn}: {e}"
        let mut resp : List PortW := []
        for p in listArg rest "resp" do
          match parsePort NI p with
          | .ok w => resp := resp ++ [w]
          | .error e => return .error s!"slot {cn}: {e}"
        let mut obs : List ObsW := []
        for w in listArg rest "obs" do
          match parseObsW NI w with
          | .ok x => obs := obs ++ [x]
          | .error e => return .error s!"slot {cn}: {e}"
        slots := slots.push { chan := c, kind, resp, obs }
        slotName := slotName.push s!"{cn}#{slots.size - 1}"
    | _ => return .error "slot <chan> <kind> [resp:..] [obs:..]"
  let mut reqs : List ReqS := []
  for a in get "req" do
    match a with
    | _ :: rest =>
        let some e := argOf rest "en" | return .error "req: no en:"
        let some f := argOf rest "fires" | return .error "req: no fires:"
        match parseBitE NI e, parseBitE NI f with
        | .ok en, .ok fires => reqs := reqs ++ [{ en, fires }]
        | .error x, _ => return .error s!"req: {x}"
        | _, .error x => return .error s!"req: {x}"
    | _ => return .error "req <name> en:<bool> fires:<bool>"
  -- commutation groups: declared ones, then every other spec flop alone
  let mut groups : List (List Nat) := []
  let mut covered : Std.HashSet Nat := {}
  for a in get "group" do
    match a with
    | [g] =>
        let mut ks : List Nat := []
        for f in (g.splitOn ",").filter (!·.isEmpty) do
          let some k := NS.flops[f]? | return .error s!"group: no spec register {repr f}"
          ks := ks ++ [k]
          covered := covered.insert k
        groups := groups ++ [ks]
    | _ => return .error "group <reg>,<reg>,..."
  for k in List.range nS do
    unless covered.contains k do groups := groups ++ [[k]]
  let split ← match one "split" with
    | some [nm] =>
        match conjName.toList.idxOf? nm with
        | some s => pure (some s)
        | none => return .error s!"split: no invariant conjunct {repr nm}"
    | _ => pure none
  let sp : RefMapSpec := {
    inv := invSp, R0, active, alpha := alphaL, init := init.toList, chans := chans.toList,
    slots := slots.toList, reqs, groups, split, fuelS, specInW := inputWidths DS (inputCount DS), LS }
  return .ok { sp, conjName, chanName, slotName, specNames := NS, implNames := NI }

/-! ## Running -/

def showTarget (B : Built) : RTarget → String
  | .active => "active"
  | .comm g => s!"comm {",".intercalate (g.map fun k => (B.specNames.flopN.getD k ("", 0)).1)}"
  | .allow j => s!"allow {B.slotName.getD j s!"slot{j}"}"
  | .obs j => s!"obs {B.slotName.getD j s!"slot{j}"}"
  | .rankLe c => s!"rankLe {B.chanName.getD c s!"chan{c}"}"
  | .rankLt c => s!"rankLt {B.chanName.getD c s!"chan{c}"}"
  | .rankSel c => s!"rankSel {B.chanName.getD c s!"chan{c}"}"

def showSTarget (B : Built) : STarget → String
  | .moore c => s!"moore {B.chanName.getD c s!"chan{c}"}"
  | .persist c d => s!"persist {B.chanName.getD c ""}/{B.chanName.getD d ""}"
  | .outC c d => s!"outC {B.chanName.getD c ""}/{B.chanName.getD d ""}"
  | .comm c d => s!"comm {B.chanName.getD c ""}/{B.chanName.getD d ""}"

def safeName (s : String) : String := s.map fun ch => if ch.isAlphanum || ch == '_' then ch else '_'

/-- A state value's spec registers, evaluated on a decoded assignment. -/
def specRegs (NS : Names) (get : Nat → PBV) (s : SVal) (n : Nat) : List (String × String) :=
  (List.range n).map fun k =>
    let nm := (NS.flopN.getD k (s!"flop{k}", 0)).1
    match flopE s k with
    | some ⟨w, e⟩ => (nm, s!"{w}'h{(Nat.toDigits 16 (evalD get e).toNat).asString}")
    | none => (nm, "?")

def specialize (D : DesignCert) (sf wf : Nat) : IO (Except String (Program × Nat)) := do
  let res ← IO.lazyPure (fun _ => MemFused.projectMemFDesignAt sf wf D)
  match res with
  | .error e => return .error s!"specialization failed: {repr e}"
  | .ok R => match Hw.checkResidual R with
    | some b => return .ok (R, b + 16)
    | none => return .error "checkResidual rejected the residual"

/-- The invariant's proofs, as `sym_verify conj:` builds them. -/
def proveInv (D : DesignCert) (R : Program) (sp : ConjTSpec) (s0 : StateM) (cname : Array String)
    (solver : String) (work : System.FilePath) (jobs : Nat) (cache : Bool) :
    IO (Except String (ByteArray × List (List ByteArray) × List String × Nat)) := do
  unless sideOKC D sp.plain s0 do return .error "invariant side conditions"
  let rb := baseRunC D R sp.plain s0
  let (sB, stB) ← match rb with
    | .ok ([s], st) => pure (s, st)
    | .ok _ => return .error "invariant base run shape"
    | .error e => return .error s!"invariant base run: {e}"
  unless scopedB stB do return .error "invariant base definitions not scoped"
  let some φB := baseOblT sp s0 stB sB | return .error "an invariant conjunct could not be read"
  let (aB, _, _, ncB) ← solve solver work "inv_base" φB (cache := cache)
  let pfB ← match aB with
    | .unsat p => pure p
    | .sat _ => return .error "COUNTEREXAMPLE (invariant base): some conjunct fails from the initial state"
    | .failed w => return .error s!"SOLVER FAILED (invariant base): {w}"
  let allC := sp.allCases
  let mut runList : List (Case × Except String (List StepO × SymSt)) := []
  for c in allC do runList := runList ++ [(c, stepRunT D R sp c)]
  let n := sp.conjs.length
  let mut items : Array (Nat × Nat × Case) := #[]
  for t in List.range n do
    for (c, m) in (sp.casesOf t).zipIdx do items := items.push (t, m, c)
  let mut pfs : Array (Array ByteArray) :=
    (List.range n).toArray.map fun t => Array.replicate (sp.casesOf t).length ByteArray.empty
  let mut failing : List String := []
  let mut total := ncB
  for batch in chunksOf (List.range items.size) jobs do
    let mut tasks := #[]
    for j in batch do
      let (t, m, c) := items[j]!
      tasks := tasks.push (← IO.asTask (prio := .dedicated) (do
        let some (.ok ([a, b], st)) := findRun runList c | return (j, SatAnswer.failed "no run", 0)
        unless scopedB st do return (j, SatAnswer.failed "definitions not scoped", 0)
        let some φ := stepOblT sp c st a b t | return (j, SatAnswer.failed "unreadable", 0)
        let (ans, _, _, nc) ← solve solver work s!"inv_{t}_{m}" φ (cache := cache)
        return (j, ans, nc)))
    for task in tasks do
      match ← IO.wait task with
      | .error e => return .error s!"task: {e}"
      | .ok (j, ans, nc) =>
        let (t, m, _) := items[j]!
        total := total + nc
        match ans with
        | .unsat p => pfs := pfs.modify t (·.set! m p)
        | .sat _ => failing := failing ++ [cname.getD t s!"conj{t}"]
        | .failed w => return .error s!"SOLVER FAILED (invariant): {w}"
  return .ok (pfB, pfs.toList.map (·.toList), failing, total)

/-! ## Theorem files -/

def leanStr (s : String) : String :=
  "\"" ++ (s.toList.map fun c =>
    if c == '"' then "\\\"" else if c == '\\' then "\\\\" else if c == '\n' then "\\n" else c.toString).foldl (· ++ ·) "" ++ "\""

def hexDigit (n : Nat) : Char := "0123456789abcdef".toList.getD n '0'

/-- A proof as a hex string the file decodes (`hexBytes`): an array literal of its bytes
exceeds the elaborator's recursion depth for all but the smallest proofs. -/
def leanBytes (b : ByteArray) : String :=
  "hexBytes \"" ++ String.ofList (b.toList.flatMap fun x => [hexDigit (x.toNat / 16), hexDigit (x.toNat % 16)]) ++ "\""

def hexBytesDef : String := "/-- A byte string from its hex digits. -/
def hexBytes (s : String) : ByteArray := Id.run do
  let cs := s.toList.toArray
  let hv (c : Char) : UInt8 := if c.isDigit then (c.toNat - 48).toUInt8 else (c.toNat - 87).toUInt8
  let mut out := ByteArray.empty
  for i in [0:cs.size / 2] do
    out := out.push (hv cs[2 * i]! * 16 + hv cs[2 * i + 1]!)
  return out

"

def leanBytesL (bs : List ByteArray) : String := "[" ++ ", ".intercalate (bs.map leanBytes) ++ "]"

def leanBytesLL (bss : List (List ByteArray)) : String := "[" ++ ", ".intercalate (bss.map leanBytesL) ++ "]"

def pretty {α : Type} [Repr α] (x : α) : String := (repr x).pretty 1000000

def leanConjT (sp : ConjTSpec) : String :=
  s!"\{ conjs := {pretty sp.conjs}, hyps := {pretty sp.hyps}, split := {pretty sp.split}, \
asmOut := {pretty sp.asmOut}, asmIn := {pretty sp.asmIn}, fuel := {sp.fuel}, inW := {pretty sp.inW}, \
L := {pretty sp.L} }"

def leanRM (sp : RefMapSpec) : String :=
  s!"\{ inv := {leanConjT sp.inv},\n    R0 := {sp.R0}, active := {pretty sp.active}, alpha := {pretty sp.alpha},\n    \
init := {pretty sp.init},\n    chans := {pretty sp.chans},\n    slots := {pretty sp.slots},\n    \
reqs := {pretty sp.reqs}, groups := {pretty sp.groups}, hyps := {pretty sp.hyps}, split := {pretty sp.split},\n    \
fuelS := {sp.fuelS}, specInW := {pretty sp.specInW}, LS := {pretty sp.LS} }"

/-- `target`'s path relative to directory `dir`, both resolved. -/
def relPath (dir target : System.FilePath) : IO String := do
  let a := ((← IO.FS.realPath dir).toString.splitOn "/").filter (!·.isEmpty)
  let b := ((← IO.FS.realPath target).toString.splitOn "/").filter (!·.isEmpty)
  let n := ((a.zip b).takeWhile fun (x, y) => x == y).length
  return "/".intercalate (List.replicate (a.length - n) ".." ++ b.drop n)

def sha256 (path : String) : IO String := do
  let out ← IO.Process.output { cmd := "sha256sum", args := #[path] }
  return (out.stdout.splitOn " ").headD "?"

/-- The SHA-256 of some proofs, in order (provenance only). -/
def proofsHash (work : System.FilePath) (pfs : List ByteArray) : IO String := do
  let blob := work / "emit_proofs.bin"
  -- streamed: the proofs of a real design are gigabytes
  IO.FS.withFile blob .write fun h => do
    for p in pfs do h.write p
  let d ← sha256 blob.toString
  IO.FS.removeFile blob
  return d

def certDef (tag path : String) (dir : System.FilePath) : IO String := do
  return s!"def cert{tag} : String := include_str {leanStr (← relPath dir path)}

def D{tag} : DesignCert := certOf cert{tag}
"

/-- A theorem file for a verdict. `f2`: the proofs are in the file and `verdict` is their
check, by `native_decide` (trusted native: one `_native` axiom). `f1`: the verdict is a named
axiom; its docstring records the certificates' and the proofs' hashes. -/
def emitFile (out : System.FilePath) (kind : String) (facts : Bool) (implCert specCert : String)
    (sf wf : Nat) (sp : RefMapSpec) (pfs : List ByteArray) (pfDefs : Unit → String) (pfArgs : String)
    (work : System.FilePath) : IO Unit := do
  let dir := out.parent.getD "."
  IO.FS.createDirAll dir
  let name := safeName (out.fileStem.getD "verdict")
  let ns := s!"RefMap{kind.toUpper}.{name}"
  let certs := if facts then [("S", specCert)] else [("I", implCert), ("S", specCert)]
  let mut defs := ""
  for (t, c) in certs do defs := defs ++ (← certDef t c dir) ++ "\n"
  let what := if facts then s!"the spec `{specCert.splitOn "/" |>.getLast!}` has its Moore and confluence facts"
    else s!"`{implCert.splitOn "/" |>.getLast!}` refines `{specCert.splitOn "/" |>.getLast!}`"
  let stmt := if facts then s!"SpecFactsChecked {sf} {wf} DS spec"
    else s!"RefMapChecked {sf} {wf} {sf} {wf} DI DS spec s0"
  let check := if facts then s!"runSpecFactsF {sf} {wf} DS spec {pfArgs}"
    else s!"runRefMapF {sf} {wf} {sf} {wf} DI DS spec s0 {pfArgs}"
  let s0Def := if facts then "" else "def s0 : StateM :=
  { flops := DI.flops.map (fun f => mk_bv f.width 0)
  , mems  := DI.memories.map (fun md =>
      { words := List.replicate (2 ^ md.aw) (mk_bv md.dw 0), dflt := mk_bv md.dw 0 }) }

"
  let body ← if kind == "f2" then
      pure s!"{hexBytesDef}{pfDefs ()}
theorem verdict : {check} = true := by native_decide

theorem checked : {stmt} := ⟨{pfArgs.replace " " ", "}, verdict⟩
"
    else do
      let mut prov := ""
      for (t, c) in certs do prov := prov ++ s!"\n  certificate {t}: sha256 {← sha256 c}"
      prov := prov ++ s!"\n  proofs: {pfs.length} LRAT proofs, {pfs.foldl (· + ·.size) 0} bytes, sha256 {← proofsHash work pfs}"
      pure s!"/-- {what}: the proofs passed the check, natively evaluated by sym_refmap.{prov} -/
axiom checked : {stmt}
"
  let trust := if kind == "f2" then "The LRAT proofs came from an untrusted solver and are checked by the
verified checker, which `native_decide` evaluates (trusted native: one `_native` axiom)."
    else "The verdict is a named axiom: sym_refmap evaluated the verified checker on the
proofs natively (trust F1); the docstring records what it checked."
  let src := s!"/- Generated by sym_refmap: {what}.
The certificates are included as text. {trust} -/
import HDLProof.Check.RefMapRef
open Compiler Projection HDLProof HDLProof.Sym HDLProof.Check

namespace {ns}

{defs}{s0Def}def spec : RefMapSpec :=
  {leanRM sp}

{body}
#print axioms checked

end {ns}
"
  IO.FS.writeFile out src

/-- `emit:[f1:|f2:]<file>`; F2 when no kind is given. -/
def emitOpt (rest : List String) : Option (String × System.FilePath) :=
  (rest.findSome? fun t => (t.dropPrefix? "emit:").map (·.toString)).map fun e =>
    if let some f := e.dropPrefix? "f1:" then ("f1", System.FilePath.mk f.toString)
    else if let some f := e.dropPrefix? "f2:" then ("f2", System.FilePath.mk f.toString)
    else ("f2", System.FilePath.mk e)

def refmapMain (path : String) (rest : List String) (facts : Bool) : IO UInt32 := do
  let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
  let sf := ((opt "sf").bind (·.toNat?)).getD 20000
  let wf := ((opt "wf").bind (·.toNat?)).getD 200
  let jobs := max 1 (((opt "jobs").bind (·.toNat?)).getD 8)
  -- reuse an LRAT proof whose CNF in work/ is byte-identical (the check re-checks it)
  let cache := rest.contains "cache"
  let solver ← match opt "solver" with
    | some s => pure s
    | none => do pure ((← IO.appDir) / "cadical").toString
  let work := System.FilePath.mk ((opt "work").getD "refmap_work")
  IO.FS.createDirAll work
  let dir := (System.FilePath.mk path).parent.getD "."
  let lines := parseRM (← IO.FS.readFile path)
  let pathOf (k : String) : Option String :=
    ((lines.find? (·.key == k)).bind (·.args.head?)).map fun p =>
      if p.startsWith "/" then p else (dir / p).toString
  let some specCert := pathOf "spec" | IO.eprintln "no spec <certificate>"; return 2
  let implCert := (pathOf "impl").getD ""
  if !facts && implCert.isEmpty then IO.eprintln "no impl <certificate>"; return 2
  let (_, DS) ← Projection.CertLoadAny.loadAny specCert
  say s!"spec {specCert}: flops {DS.flops.size} outputs {DS.outputs.size} memories {DS.memories.size}"
  unless HwMem.admitsWB DS do say "  RESULT: REFUSED (the memory route does not admit the spec)"; return 4
  let t0 ← IO.monoMsNow
  let (RS, fuelS) ← match ← specialize DS sf wf with
    | .ok x => pure x
    | .error e => do say s!"  RESULT: REFUSED (spec: {e})"; return 4
  let (DI, RI, fuelI) ← if facts then pure (DS, RS, fuelS) else do
    let (_, DI) ← Projection.CertLoadAny.loadAny implCert
    say s!"impl {implCert}: flops {DI.flops.size} outputs {DI.outputs.size} memories {DI.memories.size}"
    unless HwMem.admitsWB DI do say "  RESULT: REFUSED (the memory route does not admit the implementation)"; return 4
    let (RI, fuelI) ← match ← specialize DI sf wf with
      | .ok x => pure x
      | .error e => do say s!"  RESULT: REFUSED (implementation: {e})"; return 4
    pure (DI, RI, fuelI)
  let t1 ← IO.monoMsNow
  say s!"  specialized and checked in {t1 - t0} ms"
  let B ← match ← buildRM lines (if facts then specCert else implCert) specCert (pathOf "inv") DI DS fuelI fuelS facts with
    | .ok b => pure b
    | .error e => do IO.eprintln s!"{path}: {e}"; return 2
  let sp := B.sp
  let table := work / (if facts then "specfacts_table.tsv" else "refmap_table.tsv")
  IO.FS.writeFile table "target\tcase\tclauses\tbuild_ms\tsolve_ms\tverdict\n"
  let row (t c : String) (nc bb sat : Nat) (v : String) : IO Unit := do
    let h ← IO.FS.Handle.mk table .append
    h.putStrLn s!"{t}\t{c}\t{nc}\t{bb}\t{sat}\t{v}"
  if facts then
    -- the spec's Moore and confluence facts
    unless specSideOK DS sp && sp.LS.mds.isEmpty do say "  RESULT: REFUSED (spec side conditions)"; return 4
    let ts := targetsSF sp
    say s!"  spec facts: {sp.chans.length} channel(s), {ts.length} obligation(s)"
    let mut pfs : Array ByteArray := Array.replicate ts.length ByteArray.empty
    let mut failing : List String := []
    for batch in chunksOf (List.range ts.length) jobs do
      let mut tasks := #[]
      for i in batch do
        let t := ts[i]!
        tasks := tasks.push (← IO.asTask (prio := .dedicated) (do
          let some φ := sfObl DS RS sp t | return (i, SatAnswer.failed "the obligation could not be built", 0, 0, 0)
          let (ans, bb, sat, nc) ← solve solver work s!"sf_{i}" φ (cache := cache)
          return (i, ans, bb, sat, nc)))
      for task in tasks do
        match ← IO.wait task with
        | .error e => do say s!"  RESULT: SOLVER FAILED (task: {e})"; return 5
        | .ok (i, ans, bb, sat, nc) =>
          let tag := showSTarget B ts[i]!
          match ans with
          | .unsat p =>
              pfs := pfs.set! i p
              say s!"  [{tag}] {nc} clauses, solver {sat} ms: UNSAT"; row tag "-" nc bb sat "UNSAT"
          | .sat _ =>
              failing := failing ++ [tag]
              say s!"  [{tag}] {nc} clauses, solver {sat} ms: SAT"; row tag "-" nc bb sat "SAT"
          | .failed w => do say s!"  [{tag}] RESULT: SOLVER FAILED: {w}"; return 5
    unless failing.isEmpty do
      say s!"  RESULT: SPEC FACTS FAIL: {failing}"
      return 1
    let ok ← IO.lazyPure (fun _ => runSpecFactsOf DS RS sp pfs.toList)
    if ok then
      if let some (kind, out) := emitOpt rest then
        emitFile out kind true "" specCert sf wf sp pfs.toList
          (fun _ => s!"def pfF : List ByteArray := {leanBytesL pfs.toList}\n") "pfF" work
        say s!"  {kind.toUpper} theorem file: {out}"
      say "  RESULT: SPEC FACTS PROVED -- runSpecFactsOf_sound: Moore, and the channel spec is confluent"
      return 0
    else
      say "  RESULT: CHECK REJECTED (the verified checker did not accept the proofs)"
      return 6
  -- the refinement map
  let s0 := zeroState DI
  say s!"  map: {sp.alpha.length} spec registers, {sp.chans.length} channel(s), {sp.slots.length} slot(s), \
{sp.reqs.length} requirement(s), {sp.groups.length} commutation group(s), reset {sp.R0}"
  unless sideRM DS sp do say "  RESULT: REFUSED (map side conditions: spec shape, alpha/init per register, groups)"; return 4
  let t2 ← IO.monoMsNow
  let (pfIB, pfIS) ← match ← proveInv DI RI sp.inv s0 B.conjName solver work jobs cache with
    | .ok (pB, pS, [], nc) => do
        say s!"  invariant: {sp.inv.conjs.length} conjunct(s) PROVED ({nc} clauses)"
        pure (pB, pS)
    | .ok (_, _, failing, _) => do
        say s!"  RESULT: NOT INDUCTIVE (invariant): {failing}"; return 1
    | .error e => do say s!"  RESULT: {e}"; return 1
  let t3 ← IO.monoMsNow
  -- the base
  let rb := baseRunRM DI RI sp s0
  let pfB ← match rb with
    | .ok (steps, st) =>
        unless scopedB st do say "  RESULT: REFUSED (base definitions not scoped)"; return 4
        let some φB := baseOblRM sp st steps | say "  RESULT: REFUSED (the base could not be built)"; return 4
        let (aB, bb, sat, nc) ← solve solver work "rm_base" φB (cache := cache)
        row "base" "-" nc bb sat (match aB with | .unsat _ => "UNSAT" | .sat _ => "SAT" | .failed _ => "FAILED")
        match aB with
        | .unsat p => do say s!"  base: {nc} clauses, solver {sat} ms: UNSAT"; pure p
        | .sat _ => do say "  RESULT: COUNTEREXAMPLE (base): the active output or the initial map fails at the first refined cycle"; return 1
        | .failed w => do say s!"  RESULT: SOLVER FAILED (base): {w}"; return 5
    | .error e => do say s!"  RESULT: REFUSED (base run: {e})"; return 4
  -- one run per case
  let cases := casesRM sp
  let runJobs := max 1 (((opt "runs").bind (·.toNat?)).getD jobs)
  let mut runs : List (Case × Except String RunRM) := []
  for batch in chunksOf (List.range cases.length) runJobs do
    let mut tcs := #[]
    for j in batch do
      let c := cases.getD j []
      tcs := tcs.push (c, ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => rmRun DI DS RI RS sp c))
    for (c, task) in tcs.toList do
      match ← IO.wait task with
      | .ok r => runs := runs ++ [(c, r)]
      | .error e => do say s!"  RESULT: REFUSED ({e})"; return 4
  for (c, r) in runs do
    match r with
    | .ok R => unless scopedB R.st do say s!"  RESULT: REFUSED (case {showCase (B.implNames.flopN.map (·.1)) c}: not scoped)"; return 4
    | .error e => do say s!"  RESULT: REFUSED (case {showCase (B.implNames.flopN.map (·.1)) c}: {e})"; return 4
  let t4 ← IO.monoMsNow
  let ts := targetsRM sp
  say s!"  runs: {cases.length} case(s) in {t4 - t3} ms; {ts.length} target(s) per case"
  let mut items : Array (Nat × Nat × Case) := #[]
  for ti in List.range ts.length do
    for (c, m) in cases.zipIdx do items := items.push (ti, m, c)
  let mut pfs : Array (Array ByteArray) :=
    (List.range ts.length).toArray.map fun _ => Array.replicate cases.length ByteArray.empty
  let mut failing : List String := []
  let mut totalC := 0
  let mut maxC := 0
  let flopNames := B.implNames.flopN.map (·.1)
  for batch in chunksOf (List.range items.size) jobs do
    let mut tasks := #[]
    for j in batch do
      let (ti, m, c) := items[j]!
      tasks := tasks.push (← IO.asTask (prio := .dedicated) (do
        let some (.ok R) := findRun runs c | return (j, SatAnswer.failed "no run for the case", 0, 0, 0, none)
        let some φ := oblRM sp c R ti ts[ti]! | return (j, SatAnswer.failed "the obligation could not be built", 0, 0, 0, none)
        let (ans, bb, sat, nc) ← solve solver work s!"rm_{ti}_{m}" φ (cache := cache)
        return (j, ans, bb, sat, nc, some φ)))
    for task in tasks do
      match ← IO.wait task with
      | .error e => do say s!"  RESULT: SOLVER FAILED (task: {e})"; return 5
      | .ok (j, ans, bb, sat, nc, φo) =>
        let (ti, m, c) := items[j]!
        let tname := showTarget B ts[ti]!
        let tag := if cases.length == 1 then tname else s!"{tname} | {showCase flopNames c}"
        totalC := totalC + nc
        maxC := max maxC nc
        match ans with
        | .unsat p =>
            pfs := pfs.modify ti (·.set! m p)
            say s!"  [{tag}] {nc} clauses, solver {sat} ms: UNSAT"
            row tname (showCase flopNames c) nc bb sat "UNSAT"
        | .sat model =>
            failing := failing ++ [tag]
            say s!"  [{tag}] {nc} clauses, solver {sat} ms: SAT"
            row tname (showCase flopNames c) nc bb sat "SAT"
            -- decode the cycle-n state and both cycles' inputs; evaluate the map's states
            if let (some φ, some (.ok R)) := (φo, findRun runs c) then
              let (st0, ins) := decodeStepT φ (parseModel model) sp.inv.L sp.inv.inW (sp.inv.tmplOf c)
              let xs := (stateVals st0 ++ inputVals ins[0]! ++ inputVals ins[1]!).toArray
              let σa := extendD xs R.st
              let get := fun i => σa.getD i ⟨0#0⟩
              let nS := sp.LS.fws.length
              let r0 := specRegs B.specNames get R.σ0 nS
              let re := specRegs B.specNames get R.σe nS
              let r1 := specRegs B.specNames get R.σ1 nS
              let diff := (re.zip r1).filterMap fun ((nm, a), (_, b)) =>
                if a == b then none else some s!"  {nm}: spec after the cycle {a}, map at n+1 {b}"
              let fired := (R.recs.zip B.slotName.toList).filterMap fun (r, nm) =>
                if (evalD get r.kind).toNat == 1 then some nm else none
              let inLine (i : RuntimeInput) : String := " ".intercalate ((List.range i.size).filterMap fun k =>
                let v := bv_uint i[k]!
                if v == 0 then none else some s!"{(B.implNames.inW.getD k (s!"in{k}", 0)).1}={v}")
              -- a group's name lists its registers: keep the file name within the usual
              -- 255-byte limit, the target's index keeping it unique
              let base := safeName tname
              let base := if base.length ≤ 120 then base else s!"{base.take 80}__target{ti}"
              let f := work / s!"cti_{base}.txt"
              IO.FS.writeFile f s!"counterexample for {tag}\nslots that fire at cycle n: {fired}\n\
nonzero inputs, cycle n: {inLine ins[0]!}\nnonzero inputs, cycle n+1: {inLine ins[1]!}\n\
spec registers where the spec after the cycle differs from the map at n+1:\n{"\n".intercalate diff}\n\
map at cycle n:\n{"\n".intercalate (r0.map fun (r, v) => s!"  {r} = {v}")}\n\
implementation state at cycle n:\n{"\n".intercalate ((groupedState flopNames st0).map fun (r, v) => s!"  {r} = {v}")}\n"
              say s!"  [{tag}] counterexample in {f}"
        | .failed w => do say s!"  [{tag}] RESULT: SOLVER FAILED: {w}"; return 5
  let t5 ← IO.monoMsNow
  say s!"  targets: {items.size} obligation(s), {totalC} clauses in total, largest {maxC}; {t5 - t4} ms"
  unless failing.isEmpty do
    say s!"  RESULT: NOT A REFINEMENT (step): {failing}"
    return 1
  let pfL := pfs.toList.map (·.toList)
  let ok ← IO.lazyPure (fun _ => runConjTOf DI RI sp.inv s0 pfIB pfIS && sideRM DS sp && checkRM sp rb runs pfB pfL)
  let t6 ← IO.monoMsNow
  say s!"  verified check (runRefMapOf over the runs above; LRAT-check all) in {t6 - t5} ms; total {t6 - t2} ms"
  if ok then
    if let some (kind, out) := emitOpt rest then
      emitFile out kind false implCert specCert sf wf sp ([pfIB] ++ pfIS.flatten ++ [pfB] ++ pfL.flatten)
        (fun _ => s!"def pfIB : ByteArray := {leanBytes pfIB}\ndef pfIS : List (List ByteArray) := {leanBytesLL pfIS}\n\
def pfB : ByteArray := {leanBytes pfB}\ndef pfS : List (List ByteArray) := {leanBytesLL pfL}\n")
        "pfIB pfIS pfB pfS" work
      say s!"  {kind.toUpper} theorem file: {out}"
    say "  RESULT: REFINEMENT PROVED (refinement map) -- runRefMapOf_sound: on every run from the all-zero state, \
the map refines the spec with stuttering from the first refined cycle on"
    return 0
  else
    say "  RESULT: CHECK REJECTED (the verified checker did not accept the proofs)"
    return 6

def main (args : List String) : IO UInt32 := do
  match args with
  | "refmap" :: path :: rest => refmapMain path rest false
  | "specfacts" :: path :: rest => refmapMain path rest true
  | _ =>
    IO.eprintln "usage: sym_refmap refmap|specfacts <file.refmap> [jobs:<n>] [runs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>] [cache] [solver:<cadical>] [work:<dir>]"
    return 2
