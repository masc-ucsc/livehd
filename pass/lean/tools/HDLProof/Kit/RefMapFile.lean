/-
`Kit.RefMapFile` -- reading a `.refmap` file (`pass/lean/tools/formats/refmap.md`): its lines,
the certificates' port names, ports, Booleans and observed words, and ONE parser for its `chan`
and `slot` lines, which `sym_refmap refmap`/`specfacts` (`buildRM`) and `sym_refmap judge`
(`buildJudge`, here) both use, so one file is one contract to the checker and to the judge.
`scripts/mk_product.py` reads the same lines in Python; `tests/refmap_contract_dump.lean` prints
the judge's reading of a file in the canonical form `mk_product.py --dump` prints, and
`tests/judge_selftest.sh` compares the two. Untrusted: a verdict is a checker's closed Bool over
the spec built here. No theorems.
-/
import HDLProof.Kit.Driver
import HDLProof.Kit.Judge

open Compiler Projection HDLProof HDLProof.Sym HDLProof.Check HDLProof.Driver

namespace HDLProof.Kit

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

/-! ## Options, outputs, channels and slots -/

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

/-- A `chan <name> allow:.. [sel:..] [resp:..] [obs:..] [rank:..]` line's arguments: the channel's
name and its spec. `NS` names the spec's ports; `rank:` names the implementation's outputs and is
read only when their names are given (`rankNames`: the refinement map's progress measure; the
judge reads no rank). -/
def parseChan (NS : Names) (rankNames : Option Names) : List String → Except String (String × ChanS)
  | nm :: rest => do
      let some al := argOf rest "allow" | throw s!"chan {nm}: no allow:"
      let allow ← specProp NS al
      let mut sel : List (Nat × Int) := []
      for s in listArg rest "sel" do
        match s.splitOn "=" with
        | [i, v] =>
            let some k := NS.ins[i]? | throw s!"chan {nm}: no spec input {repr i}"
            let some z := v.toInt? | throw s!"chan {nm}: bad value {repr v}"
            sel := sel ++ [(k, z)]
        | _ => throw s!"chan {nm}: sel:<input>=<value>,..."
      let mut respIn : List Nat := []
      for i in listArg rest "resp" do
        let some k := NS.ins[i]? | throw s!"chan {nm}: no spec input {repr i}"
        respIn := respIn ++ [k]
      let mut obsOut : List Nat := []
      for o in listArg rest "obs" do
        let some k := NS.outs[o]? | throw s!"chan {nm}: no spec output {repr o}"
        obsOut := obsOut ++ [k]
      let rank ← match rankNames, argOf rest "rank" with
        | some NI, some r => match r.splitOn "/" with
          | [ro, so] =>
              let some a := NI.outs[ro]? | throw s!"chan {nm}: no implementation output {repr ro}"
              let some b := NI.outs[so]? | throw s!"chan {nm}: no implementation output {repr so}"
              pure (some (a, b))
          | _ => throw s!"chan {nm}: rank:<rank-output>/<selector-output>"
        | _, _ => pure none
      pure (nm, { sel, respIn, allow, obsOut, rank })
  | _ => throw "chan <name> allow:<out> ..."

/-- A `slot <chan> <bool> [resp:..] [obs:..]` line's arguments: the slot, its channel looked up
by name in `chanIdx`. `NI` names the implementation's ports. `kind` replaces the firing formula
(the judge's controls run D0 as the implementation, its slot firing always). -/
def parseSlot (NI : Names) (chanIdx : Std.HashMap String Nat) (kind : Option BitE := none) :
    List String → Except String SlotS
  | cn :: kindTxt :: rest => do
      let some c := chanIdx[cn]? | throw s!"slot: no channel {repr cn}"
      let kind ← match kind with
        | some k => pure k
        | none => (parseBitE NI kindTxt).mapError (s!"slot {cn}: " ++ ·)
      let mut resp : List PortW := []
      for p in listArg rest "resp" do
        resp := resp ++ [← (parsePort NI p).mapError (s!"slot {cn}: " ++ ·)]
      let mut obs : List ObsW := []
      for w in listArg rest "obs" do
        obs := obs ++ [← (parseObsW NI w).mapError (s!"slot {cn}: " ++ ·)]
      pure { chan := c, kind, resp, obs }
  | _ => throw "slot <chan> <kind> [resp:..] [obs:..]"

/-! ## The judge's contract -/

/-- The judge's contract from a `.refmap`: the one channel, the one slot, `xfer`, `maxstall`.
`NI` names the implementation's ports. A control (`ctrl`) runs D0 as the implementation: its
slot fires always (a stall choice reads D1's own observers, and the verdict does not depend on
it), and it reads no `asm` (its inputs are drawn on D1, which holds the environment monitor). -/
def buildJudge (lines : List RMLine) (NI NS : Names) (DS : DesignCert) (ctrl : Bool := false) :
    Except String (Judge.JSpec × Option PropSpec) := do
  let get (k : String) : List (List String) := (lines.filter (·.key == k)).map (·.args)
  let chanLines := get "chan"
  let slotLines := get "slot"
  unless chanLines.length == 1 && slotLines.length == 1 do
    throw s!"judge: exactly one chan and one slot (got {chanLines.length} and {slotLines.length})"
  let (cn, chan) ← parseChan NS none chanLines.head!
  let slot ← parseSlot NI (({} : Std.HashMap String Nat).insert cn 0)
    (if ctrl then some BitE.tt else none) slotLines.head!
  let obsTxt := listArg (slotLines.head!.drop 2) "obs"
  let xferTxt := (get "xfer").map (" ".intercalate ·)
  let mut xfer : List BitE := []
  for x in xferTxt do xfer := xfer ++ [← parseBitE NI x]
  let natLine (k : String) (d : Nat) : Except String Nat :=
    match (get k).head? with
    | none => pure d
    | some [v] => match v.toNat? with | some n => pure n | none => throw s!"{k}: bad value {repr v}"
    | some _ => throw s!"{k} <n>"
  let maxStall ← natLine "maxstall" 0
  let asm ← if ctrl then pure none else match (get "asm").head? with
    | some [tok] => pure (some (← implProp NI tok))
    | none => pure none
    | some _ => throw "asm <implementation-output>"
  pure ({ chan, slot, xfer, maxStall, specInW := inputWidths DS (inputCount DS)
        , obsName := obsTxt.toArray, xferName := xferTxt.toArray }, asm)

/-- `period <P> [<k>]`: random inputs change only at steps `n` with `n % P = k` (default `k` = 1
when `P` > 1; no line: `P` = 1, `k` = 0). -/
def judgePeriod (lines : List RMLine) : Nat × Nat :=
  match (lines.find? (·.key == "period")).map (·.args) with
  | some [p] => let P := p.toNat?.getD 1; (P, if P > 1 then 1 else 0)
  | some [p, k] => (p.toNat?.getD 1, k.toNat?.getD 0)
  | _ => (1, 0)

end HDLProof.Kit
