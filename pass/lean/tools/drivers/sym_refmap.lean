/-
`sym_refmap` -- refinement maps: an implementation certificate refines a spec certificate with
stuttering, and the spec's own facts.

  sym_refmap refmap <file.refmap> [jobs:<n>] [runs:<n>] [sf:<n>] [wf:<n>] [sim|simS|simI:fp|ir|vc (default vc)] [emit:[f1:|f2:]<file>]
                    [cache] [solver:<cadical>] [work:<dir>]
  sym_refmap specfacts <file.refmap> [jobs:<n>] [sf:<n>] [wf:<n>] [sim:fp|ir|vc (default vc)] [emit:[f1:|f2:]<file>]
                    [cache] [solver:<cadical>] [work:<dir>]
  sym_refmap judge <file.refmap> trace:<file.tsv> | random:<steps>,<runs>[,<seed>]
                    [sim:fp|ir|vc] [check:<steps>] [classify:<K>] [horizon:<H>] [tries:<n>]
                    [control:pos|neg [delay:<out>=<k>,...] [flip:<out>@<valid>,...]]
                    [sf:<n>] [wf:<n>] [work:<dir>] [label:<name>]

`judge` decides, on concrete runs from the all-zero state, whether ANY stuttering alignment of
the spec matches the implementation UNDER THE CONTRACT (`Kit.Judge`): no map, no invariant, no
solver. It reads only `impl`, `spec`, the one `chan` and the one `slot` (their response and
observed words), `asm` (the environment's protocol) and three lines the other modes ignore:
`xfer <bool>` (an implementation transfer: no hold in a step where one is true; repeatable),
`maxstall <R>` (at most R holds in a row, in steps, as the refinement's stutters are; default 0)
and `period <P> [<k>]` (random runs only: a P-slot certificate's inputs change only at steps n
with n % P = k, default k = 1 when P > 1, the real clock's input timing; default P = 1).
Inputs: a `trace:` TSV (`cycle` then one column per implementation input, as `sym_verify` writes
for a counterexample; `#` lines skipped; a missing input reads 0), or `random:` runs (1-bit
inputs fair coins; wider inputs drawn from 4 values per input, 0 always one of them, so addresses
repeat and configuration words often take their default; a line
`drive <input> <v0> <n> <v1>` fixes an input of random runs to v0 before step n and v1 from step
n on, e.g. a reset schedule). Random runs keep `asm`: at a step where the inputs may change, a
vector that breaks it is replaced, up to `tries` times (default 64), alternately by the previous
vector with each input drawn again with probability 1/2 and by a fresh vector, then the previous
vector is held, then the all-zero vector is tried. A run where no vector keeps `asm` is cut there
and the search goes on with the next run; `tries:0` stops at the first vector that breaks it
(ENVIRONMENT VIOLATED, exit 3). A trace is replayed as given, and whether it keeps `asm` is
reported. After the runs, the transactions searched are counted per channel (a step whose guard
is set, as the contract reads D1's ports); a channel with none is warned about: the search says
nothing about it.

Results: `NO MATCH at step n` (exit 1), `MATCH FOUND` (exit 0; the report adds where the slot's
own stall choice fails, if it does: STALL CHOICE WRONG), `ENVIRONMENT VIOLATED at step n` (exit
3, `tries:0` only). A NO MATCH is a verdict on the contract as much as on the
implementation: a contract that reads an output channel the change delays in the same cycle, or
allows fewer holds than the change adds, rejects a right implementation too. So every NO MATCH
comes with:
- the run's inputs as a trace (`<work>/<label>_run<k>.tsv`, steps 0..n+1), to re-run it;
- whether the environment held on it (a trace that breaks `asm` is not a legal witness);
- the failing channels as transactions: D0's words at step n, and D1's transactions on that
  channel around it (a step whose guard is set, as the contract reads D1's ports);
- `CLASS:` a hint from re-reading the same run under relaxed contracts, steps 0 .. n+H
  (`horizon:`, default 20): each failing channel (the word, its guard and the words sharing the
  guard) read 1..K steps later (`classify:<K>`, default 2; `classify:0` turns this off), `maxstall`
  raised by 1..K, each `xfer` left out alone. `DELAY?` or `STUTTER?` names the relaxation that
  removes the NO MATCH; `VALUE DIFFERENCE`: none does. A hint, never a verdict: a delay or a wait
  counts only if the change adds it (read the RTL). A trace (a product counterexample is short)
  is re-read past its end too: its last vector once, then random vectors that keep `asm` (seed
  1); the search and its verdict read the trace only.

Controls (`control:pos|neg`): the same contract with D0 run as the implementation, its answer
known. `pos`: D0 with each output in `delay:` passed through k registers (the delays the change
adds, counted in the RTL) must MATCH on every run, or the contract is stricter than the change
allows (CONTRACT TOO STRICT). `neg` (after `pos` on the same run): D0 with one output bit flipped
at a time must NO MATCH: `flip:<out>@<valid>` flips bit 0 of `<out>` while `<valid>` is 1; by
default bit `lo` of every observed word is flipped in every cycle; a flip made but never seen is
a word the contract does not compare (NOT SEEN). D0's certificate must carry the contract's port
names; its slot fires always; random runs are drawn on the implementation (keeping `asm`) and
replayed on D0 by input name. A channel that carries no transaction on the positive control's
random runs fails the controls (not exercised; on a trace the counts are only reported).
`RESULT: CONTROLS PASS` (exit 0) or `CONTROLS FAIL` (exit 1).
Warnings when the file is read: an observed word whose payload and guard are read at different
cycles, and an `xfer` that reads an output at a cycle the slot never observes it at.

The simulator route (`sim:`), each with its theorem relating it to `interpretDesign`:
`fp` the projection's specialized residual (`runMemSim`, `runMemSim_correct`), `ir`
the post-lowering IR stepped directly (`directStepRaw` after one `checkDesign`,
`directStepRaw_correct`), `vc` the verified compiler's straight-line program (`sim:tv` is the
old name and is still accepted)
(`compileDesign` once, `denoteResidual` every step, `compileDesign_correct`; the default). `ir`
and `vc` need a memory-free certificate; without `sim:` a certificate with memories uses `fp`. With `ir` or `vc` every run is also cross-checked against `fp` on its
first `check:` steps (default 4; outputs and next state), and a NO MATCH (of the search, or of a
positive control) is re-run on `fp` alone before it is reported: two proved routes must agree,
which also covers the judge's own state conversion between them (a different answer on fp is
ROUTE DISAGREES, exit 5). The implementation is simulated one step ahead of the search, so a run
stops at its first NO MATCH: its cost is the steps up to it (plus the classification's re-reads),
whatever the run's length, and the remaining runs are skipped.

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
import HDLProof.Kit.Judge
import HDLProof.Kit.SymRoute
import LeanSemanticPrimitives.Compiler.DirectSemantics
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

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

/-- The symbolic step of `D` on route `sim` (`Kit.SymRoute`), with the fuel the checks use. -/
def symRouteOf (sim : String) (D : DesignCert) (sf wf : Nat) : IO (Except String (Stepper × Nat × String)) := do
  match ← IO.lazyPure (fun _ => mkSymRoute sim "fused" sf wf D) with
  | .ok rt => return .ok (rt.S, rt.fuel + 16, rt.name)
  | .error e => return .error e

def specialize (D : DesignCert) (sf wf : Nat) : IO (Except String (Program × Nat)) := do
  let res ← IO.lazyPure (fun _ => MemFused.projectMemFDesignAt sf wf D)
  match res with
  | .error e => return .error s!"specialization failed: {repr e}"
  | .ok R => match Hw.checkResidual R with
    | some b => return .ok (R, b + 16)
    | none => return .error "checkResidual rejected the residual"

/-- The invariant's proofs, as `sym_verify conj:` builds them. -/
def proveInv (D : DesignCert) (R : Stepper) (sp : ConjTSpec) (s0 : StateM) (cname : Array String)
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
  -- the symbolic route: `sim:` for both designs, `simS:` / `simI:` for one
  let fpOnly := (emitOpt rest).isSome
  let gS := (opt "simS").orElse fun _ => opt "sim"
  let gI := (opt "simI").orElse fun _ => opt "sim"
  let simS := defaultSim gS fpOnly
  let simI := defaultSim gI fpOnly
  if (simS != "fp" || simI != "fp") && (emitOpt rest).isSome then
    IO.eprintln s!"emit: only sim:fp has a closed checker so far (spec sim:{simS}, impl sim:{simI})"; return 2
  let (RS, fuelS, nameS) ← match ← symRouteOf simS DS sf wf with
    | .ok x => pure x
    | .error e => do say s!"  RESULT: REFUSED (spec: {e})"; return 4
  say s!"  spec route sim:{simS}{simWhy gS fpOnly}: {nameS}"
  let (DI, RI, fuelI) ← if facts then pure (DS, RS, fuelS) else do
    let (_, DI) ← Projection.CertLoadAny.loadAny implCert
    say s!"impl {implCert}: flops {DI.flops.size} outputs {DI.outputs.size} memories {DI.memories.size}"
    unless HwMem.admitsWB DI do say "  RESULT: REFUSED (the memory route does not admit the implementation)"; return 4
    let (RI, fuelI, nameI) ← match ← symRouteOf simI DI sf wf with
      | .ok x => pure x
      | .error e => do say s!"  RESULT: REFUSED (implementation: {e})"; return 4
    say s!"  impl route sim:{simI}{simWhy gI fpOnly}: {nameI}"
    pure (DI, RI, fuelI)
  let t1 ← IO.monoMsNow
  say s!"  specialized and checked in {t1 - t0} ms"
  say s!"  [time] setup {t1 - t0}"
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


/-! ## The judge: one concrete run, every stuttering alignment -/

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
  let chan ← match chanLines.head! with
    | nm :: rest => do
        let some al := argOf rest "allow" | throw s!"chan {nm}: no allow:"
        let allow ← specProp NS al
        let mut sel : List (Nat × Int) := []
        for x in listArg rest "sel" do
          match x.splitOn "=" with
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
        pure ({ sel, respIn, allow, obsOut } : ChanS)
    | _ => throw "chan <name> allow:<out> ..."
  let (slot, obsTxt) ← match slotLines.head! with
    | _ :: kindTxt :: rest => do
        let kind ← if ctrl then pure BitE.tt else parseBitE NI kindTxt
        let mut resp : List PortW := []
        for x in listArg rest "resp" do resp := resp ++ [← parsePort NI x]
        let mut obs : List ObsW := []
        for w in listArg rest "obs" do obs := obs ++ [← parseObsW NI w]
        pure (({ chan := 0, kind, resp, obs } : SlotS), listArg rest "obs")
    | _ => throw "slot <chan> <kind> [resp:..] [obs:..]"
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

/-- The outputs a Boolean reads, with their cycle (`true`: n+1). -/
def bitOuts : BitE → List (Nat × Bool)
  | .nz (.out o t) => [(o, t)]
  | .eqc (.out o t) _ => [(o, t)]
  | .not a => bitOuts a
  | .and a b => bitOuts a ++ bitOuts b
  | .or a b => bitOuts a ++ bitOuts b
  | _ => []

/-- Warnings about a contract's timing: an observed word whose payload and guard are read at
different cycles, and an `xfer` that reads an output at a cycle the slot never observes it at
(on a channel read late, its transfers are read late too). -/
def judgeLints (J : Judge.JSpec) (NI : Names) : List String := Id.run do
  let nm (o : Nat) : String := (NI.outW.getD o (s!"#{o}", 0)).1
  let cyc (t : Bool) : String := if t then "n+1" else "n"
  let mut ws : List String := []
  let mut reads : List (Nat × Bool) := []
  for (w, i) in J.slot.obs.zipIdx do
    if let .out o t := w.port then reads := reads ++ [(o, t)]
    if let some (.out g tg, _) := w.guard then
      reads := reads ++ [(g, tg)]
      if let .out _ t := w.port then
        if t != tg then
          ws := ws ++ [s!"observed word {J.obsName.getD i s!"#{i}"} reads its payload at cycle {cyc t} and its guard at cycle {cyc tg}"]
  for (x, i) in J.xfer.zipIdx do
    for (o, t) in bitOuts x do
      let ts := (reads.filter (·.1 == o)).map (·.2)
      if !ts.isEmpty && !ts.contains t then
        ws := ws ++ [s!"xfer {J.xferName.getD i s!"#{i}"} reads {nm o} at cycle {cyc t}, but the slot observes {nm o} only at cycle {cyc (!t)}: on a channel read late, read its transfers late too (out:{nm o}{if t then "" else "@next"})"]
  return ws.eraseDups

/-- The input vectors of a `trace:` TSV, by implementation input name. -/
def readTrace (path : String) (NI : Names) (inW : List Nat) : IO (Except String (Array RuntimeInput)) := do
  let rows := ((← IO.FS.readFile path).splitOn "\n").filter fun l => !l.isEmpty && !l.startsWith "#"
  let some hdr := rows.head? | return .error s!"{path}: empty"
  let cols := (hdr.splitOn "\t").drop 1
  let idx := cols.map fun nm => NI.ins[nm]?
  let mut out : Array RuntimeInput := #[]
  for r in rows.drop 1 do
    let vs := (r.splitOn "\t").drop 1
    let mut v : Array BV := (inW.map fun w => mk_bv w 0).toArray
    for (ko, x) in idx.zip vs do
      if let some k := ko then
        let some n := x.toNat? | return .error s!"{path}: bad value {repr x}"
        v := v.set! k (mk_bv (inW.getD k 1) n)
    out := out.push v
  return .ok out

/-- A run's inputs as a `trace:` TSV (named inputs only). -/
def writeTrace (path : System.FilePath) (NI : Names) (ins : Array RuntimeInput) : IO Unit := do
  let named := NI.inW.toList.zipIdx.filter fun ((nm, _), _) => !nm.isEmpty
  let mut s := "cycle\t" ++ "\t".intercalate (named.map fun ((nm, _), _) => nm) ++ "\n"
  for (v, j) in ins.toList.zipIdx do
    s := s ++ s!"{j}\t" ++ "\t".intercalate (named.map fun (_, k) => toString (bv_uint (v.getD k (mk_bv 1 0))).toNat) ++ "\n"
  IO.FS.writeFile path s

/-- The random inputs' generator: four values per wide input, drawn once per run (0, the reset
and default value of most configuration words, always one of them, so addresses repeat); 1-bit
inputs fair coins. `cur` is the vector held between change steps (`period`), `last` the last
vector used. -/
structure RandG where
  g       : StdGen
  pools   : Array (Array Nat)
  cur     : Array BV := #[]
  last    : RuntimeInput := #[]
  redraws : Nat := 0
  held    : Nat := 0
  zeros   : Nat := 0

def randInit (inW : List Nat) (seed : Nat) : RandG := Id.run do
  let mut g := mkStdGen seed
  let mut pools : Array (Array Nat) := #[]
  for w in inW do
    let mut pool : Array Nat := #[0]
    for _ in [0:3] do
      let (x, g') := randNat g 0 (2 ^ w - 1); g := g'; pool := pool.push x
    pools := pools.push pool
  return { g, pools, cur := (inW.map fun w => mk_bv w 0).toArray }

/-- One input's random value: a 1-bit input a fair coin; a wide one a random value one time in
four, else one of its four values. -/
def randOne (pools : Array (Array Nat)) (w k : Nat) (g : StdGen) : BV × StdGen := Id.run do
  if w == 1 then
    let (x, g') := randNat g 0 1
    return (mk_bv 1 x, g')
  let (c, g1) := randNat g 0 3
  let (i, g2) := randNat g1 0 3
  if c == 0 then
    let (y, g3) := randNat g2 0 (2 ^ w - 1)
    return (mk_bv w y, g3)
  return (mk_bv w ((pools.getD k #[]).getD i 0), g2)

/-- One random vector. -/
def randDraw (inW : List Nat) (G : RandG) : Array BV × RandG := Id.run do
  let mut g := G.g
  let mut v : Array BV := #[]
  for (w, k) in inW.zipIdx do
    let (b, g') := randOne G.pools w k g
    g := g'; v := v.push b
  return (v, { G with g })

/-- The vector `prev` with each input drawn again with probability 1/2 (a protocol that holds a
waiting request keeps most of the previous vector). -/
def randMix (inW : List Nat) (G : RandG) (prev : Array BV) : Array BV × RandG := Id.run do
  let mut g := G.g
  let mut v : Array BV := #[]
  for (w, k) in inW.zipIdx do
    let (c, g') := randNat g 0 1
    g := g'
    if c == 0 then v := v.push (prev.getD k (mk_bv w 0))
    else
      let (b, g'') := randOne G.pools w k g
      g := g''; v := v.push b
  return (v, { G with g })

/-- A trace as a feed (past its end, its last vector again). `asm` is recorded on each step of
the trace, not enforced: a trace is replayed as given. -/
def traceFeed (ins : Array RuntimeInput) (sim : Judge.Sim) (asm : Option PropSpec) :
    Nat → StateM → Unit → Judge.FeedR Unit := fun n st _ =>
  let v := ins.getD n (ins.back?.getD #[])
  match sim v st with
  | some r => .ok v r (n ≥ ins.size || (asm.map (propBit r.outputs ·)).getD true) ()
  | none => .refused

/-- Random runs as a feed that keeps the environment. At a step where the inputs may change, a
vector that breaks `asm` is replaced, up to `tries` times, alternately by the previous vector with
each input drawn again with probability 1/2 and by a fresh vector; then the previous vector is
held, then the all-zero vector is tried (`drive` lines apply to every vector). At a step where the
inputs may not change (`period`), a vector that breaks `asm` ends the run. `tries` = 0 ends the
run at the first vector that breaks `asm`. A run that never breaks `asm` draws the same vectors
whatever `tries` is. -/
def randFeed (inW : List Nat) (sim : Judge.Sim) (asm : Option PropSpec)
    (steps period phase tries : Nat) (drive : Nat → RuntimeInput → RuntimeInput) :
    Nat → StateM → RandG → Judge.FeedR RandG := fun n st G0 => Id.run do
  let envOk (r : StateM.ResultM) : Bool := (asm.map (propBit r.outputs ·)).getD true
  if n ≥ steps then
    -- one step past the run (the last step's outputs at n+1): the last vector again
    match sim G0.last st with
    | some r => return .ok G0.last r true G0
    | none => return .refused
  let P := max 1 period
  unless n == 0 || n % P == phase % P do
    let v := drive n G0.cur
    match sim v st with
    | some r => return (if envOk r then .ok v r true { G0 with last := v } else .envViolated)
    | none => return .refused
  let mut G := G0
  for t in [0:tries + 1] do
    let (raw, G') := if t % 2 == 1 then randMix inW G G0.cur else randDraw inW G
    G := { G' with cur := raw }
    let v := drive n raw
    match sim v st with
    | none => return .refused
    | some r =>
        if envOk r then
          return .ok v r true { G with last := v, redraws := G.redraws + (if t > 0 then 1 else 0) }
  if tries > 0 then
    for (raw, hold) in [(G0.cur, true), ((inW.map fun w => mk_bv w 0).toArray, false)] do
      let v := drive n raw
      match sim v st with
      | some r =>
          if envOk r then
            return .ok v r true { G with cur := raw, last := v
                                         held := G.held + (if hold then 1 else 0)
                                         zeros := G.zeros + (if hold then 0 else 1) }
      | none => return .refused
  return .envViolated

/-- A trace extended for the classification only: its rows as given, then its last vector once
(as `traceFeed`; the search reads no further than that), then random vectors that keep `asm`
(`rand`, a `randFeed`, continuing from the trace's last vector). A NO MATCH near the end of a
short trace (a product counterexample) can then be re-read under relaxed contracts past the
trace's end, instead of over its last vector held. -/
def traceExtFeed (ins : Array RuntimeInput) (sim : Judge.Sim) (asm : Option PropSpec)
    (rand : Nat → StateM → RandG → Judge.FeedR RandG) : Nat → StateM → RandG → Judge.FeedR RandG :=
  fun n st G =>
    if n ≤ ins.size then
      match traceFeed ins sim asm n st () with
      | .ok v r ok _ => .ok v r ok { G with cur := v, last := v }
      | .refused => .refused
      | .envViolated => .envViolated
    else rand n st G

def showV (v : Int) : String :=
  if v < 10 then toString v else s!"{v} (0x{String.ofList (Nat.toDigits 16 v.toNat)})"

def appendLine (p : System.FilePath) (l : String) : IO Unit := do
  let h ← IO.FS.Handle.mk p .append
  h.putStrLn l

/-! ### Classifying a NO MATCH -/

/-- The output channel of observed word `i`: the outputs to re-time together and the words it
covers. A guarded word's channel is its guard's port and every word that shares the guard or is
that port; an unguarded word's channel is its port and every word it guards. -/
def chanOf (J : Judge.JSpec) (i : Nat) : List Nat × List Nat := Id.run do
  let obs := J.slot.obs
  let outOf (p : PortW) : Option Nat := match p with | .out o _ => some o | _ => none
  let w := obs.getD i default
  let key : Option Nat := match w.guard with
    | some (g, _) => outOf g
    | none => outOf w.port
  let mut words : List Nat := [i]
  if let some k := key then
    for (v, j) in obs.zipIdx do
      let gk := v.guard.bind fun (g, _) => outOf g
      if gk == some k || (v.guard.isNone && outOf v.port == some k) then words := words ++ [j]
  let ws := words.eraseDups.mergeSort
  let outs := (ws.filterMap fun j => outOf (obs.getD j default).port) ++
    (ws.filterMap fun j => (obs.getD j default).guard.bind fun (g, _) => outOf g)
  return (outs.eraseDups, ws)

/-- A channel's name for reports. -/
def chanLabel (J : Judge.JSpec) (words : List Nat) : String :=
  let nm (j : Nat) : String := J.obsName.getD j s!"#{j}"
  let obs := J.slot.obs
  let plural (k : Nat) : String := if k == 1 then "" else "s"
  match words.find? (fun j => (obs.getD j default).guard.isNone),
        words.filter (fun j => (obs.getD j default).guard.isSome) with
  | some v, [] => nm v
  | some v, gs => s!"{nm v} and the {gs.length} word{plural gs.length} it guards"
  | none, gs => match gs.head? with
      | some g => s!"the {gs.length} word{plural gs.length} guarded by {((nm g).splitOn "?").getD 1 "?"}"
      | none => "?"

/-- The channels a contract's observed words name by their guards: guard, bit, name. -/
def guardsOf (J : Judge.JSpec) : List (PortW × Nat × String) := Id.run do
  let mut gs : List (PortW × Nat × String) := []
  for (w, j) in J.slot.obs.zipIdx do
    if let some (g, b) := w.guard then
      unless gs.any (fun (h, c, _) => h == g && c == b) do
        gs := gs ++ [(g, b, ((J.obsName.getD j "").splitOn "?").getD 1 "?")]
  return gs

/-- How many steps of `ps` show a transaction on each guarded channel. -/
def countTxns (J : Judge.JSpec) (ps : Array Ports) : List (String × Nat) :=
  (guardsOf J).map fun (g, b, nm) =>
    (nm, (ps.toList.filter fun C => match g.val C with
      | some x => (BV.bits x).getLsbD b
      | none => false).length)

def addCounts (a b : List (String × Nat)) : List (String × Nat) :=
  if a.isEmpty then b else a.map fun (nm, k) => (nm, k + ((b.find? (·.1 == nm)).map (·.2)).getD 0)

/-- The channels of a NO MATCH: those of the words its last candidates differ on. -/
def failChans (J : Judge.JSpec) (last : List Judge.Last) : List (List Nat × List Nat) :=
  ((last.filterMap (·.step.word?)).eraseDups.map (chanOf J)).eraseDups

/-- The words of one output channel around a NO MATCH at step `n`: D0's words at the step (each
last candidate, at most 3), and D1's transactions on the channel as the contract reads D1's ports
(`ps`, from step 0). A transaction is a step whose channel guard is set (every step, for a word
with no guard). Returns the report's lines and a one-line summary. -/
def txnView (J : Judge.JSpec) (simS : Judge.Sim) (ps : Array Ports) (n : Nat)
    (last : List Judge.Last) (words : List Nat) (impl : String := "D1") : List String × String := Id.run do
  let obs := J.slot.obs
  let nm (j : Nat) : String := J.obsName.getD j s!"#{j}"
  let guard : Option (PortW × Nat) := (words.filterMap fun j => (obs.getD j default).guard).head?
  let valOf (C : Ports) (j : Nat) : Int := ((obs.getD j default).val C).getD 0
  let shown (C : Ports) : Bool := match guard with
    | some (g, b) => match g.val C with | some x => (BV.bits x).getLsbD b | none => false
    | none => true
  -- the word that IS the channel's valid (unguarded, on the guard's port), if it is observed
  let validWord : Option Nat := match guard with
    | some (g, _) => words.find? fun j => let w := obs.getD j default; w.guard.isNone && w.port == g
    | none => none
  let cname := chanLabel J words
  let fmt (vs : List Int) : String := " ".intercalate ((words.zip vs).map fun (j, v) => s!"{nm j}={showV v}")
  let lim := ps.size
  let d1 : List (Nat × List Int) := ((List.range lim).filter fun m => shown ps[m]!).map fun m =>
    (m, words.map fun j => valOf ps[m]! j)
  let k := (d1.filter (·.1 < n)).length
  let C := ps.getD n default
  let lo := n - 2
  -- a word with no guard: values per step, not transactions
  if guard.isNone then
    let mut ls : List String := [s!"word {cname} (no guard: its value every step, as the contract reads {impl}'s ports):"]
    let d1v (m : Nat) : List Int := words.map fun j => valOf (ps.getD m default) j
    let mut summary := s!"word {cname}: no D0 candidate could be stepped"
    let mut first := true
    for (l, ci) in (last.take 3).zipIdx do
      match simS (specIn J.specInW J.chan (respOf C J.slot)) l.cand.st with
      | none => ls := ls ++ [s!"  step {n}, D0 candidate {ci}: the D0 simulator refuses the step"]
      | some r =>
          let d0 := outVals r.outputs J.chan.obsOut
          let d0w := words.map fun j => d0.getD j 0
          ls := ls ++ [s!"  step {n}, D0 candidate {ci}: {fmt d0w}"]
          if first then
            first := false
            let later := (List.range lim).find? fun m => m > n && d1v m == d0w
            summary := s!"word {cname} at step {n}: D0 {fmt d0w}, {impl} {fmt (d1v n)}" ++
              (match later with
               | some m => s!"; {impl} shows D0's value at step {m}, {m - n} step{if m - n == 1 then "" else "s"} later"
               | none => s!"; {impl} does not show D0's value through step {lim - 1}")
    ls := ls ++ [s!"  {impl}'s values as read, steps {lo}..{lim - 1}:"]
    for m in (List.range lim).filter (· ≥ lo) do ls := ls ++ [s!"    step {m}: {fmt (d1v m)}"]
    return (ls, summary)
  let mut ls : List String := [s!"channel {cname}:",
    s!"  before step {n} {impl} showed {k} transaction{if k == 1 then "" else "s"} on it (as the contract reads {impl}'s ports){if n == 0 then "" else s!", and the run matched through step {n - 1}"}"]
  let mut summary := s!"channel {cname}: no D0 candidate could be stepped"
  let mut first := true
  for (l, ci) in (last.take 3).zipIdx do
    match simS (specIn J.specInW J.chan (respOf C J.slot)) l.cand.st with
    | none => ls := ls ++ [s!"  step {n}, D0 candidate {ci}: the D0 simulator refuses the step"]
    | some r =>
        let d0 := outVals r.outputs J.chan.obsOut
        let d0w := words.map fun j => d0.getD j 0
        let d0shown := match validWord with | some j => d0.getD j 0 != 0 | none => true
        ls := ls ++ [s!"  step {n}, D0 candidate {ci}: {if d0shown then s!"transaction #{k}" else "no transaction"}: {fmt d0w}"]
        let cmp : String :=
          if d0shown then
            match d1[k]? with
            | some (m, vs) =>
                if vs == d0w then
                  if m == n then s!"{impl}'s transaction #{k} at step {m} has D0's words"
                  else s!"D0 shows transaction #{k} at step {n}; {impl} shows the same words at step {m}, {m - n} step{if m - n == 1 then "" else "s"} later"
                else
                  let ds := ((words.zip (d0w.zip vs)).filter fun (_, a, b) => a != b).map fun (j, a, b) =>
                    s!"{nm j}: D0 {showV a}, {impl} {showV b}"
                  s!"D0 shows transaction #{k} at step {n}; {impl}'s transaction #{k} (step {m}) differs: {"; ".intercalate ds}"
            | none => s!"D0 shows transaction #{k} at step {n}; {impl} shows no transaction #{k} through step {lim - 1}"
          else if shown C then s!"{impl} shows a transaction at step {n} (its #{k}) that D0 does not show there"
          else s!"neither shows a transaction at step {n}"
        if first then summary := s!"channel {cname}: {cmp}"; first := false
  ls := ls ++ [s!"  step {n}, {impl} as read: {if shown C then "a transaction" else "no transaction"}: {fmt (words.map fun j => valOf C j)}"]
  let near := d1.zipIdx.filter fun ((m, _), _) => m ≥ lo
  ls := ls ++ [s!"  {impl}'s transactions as read, steps {lo}..{lim - 1}:{if near.isEmpty then " none" else ""}"]
  for ((m, vs), j) in near do ls := ls ++ [s!"    step {m}, #{j}: {fmt vs}"]
  return (ls, summary)

/-- What a relaxed contract does to the NO MATCH at step `n`. -/
inductive ROut where
  | full (lim : Nat)
  | moved (m : Nat)
  | same (m : Nat)
  | other

def ROut.show (n : Nat) : ROut → String
  | .full l => s!"removes it: the run then matches through step {l - 1}"
  | .moved m => s!"moves it from step {n} to step {m}"
  | .same m => s!"does not remove it (NO MATCH at step {m})"
  | .other => "cannot re-read the run"

/-- Re-read the same run (`outs`, `ins`: D1's outputs and inputs per cycle, from cycle 0) under
relaxed contracts, steps `0 .. lim-1`: each failing channel read `1..K` steps later (and one step
earlier when the contract reads it late already); `maxstall` raised by `1..K`; each `xfer` left
out alone. Each is a hint: a relaxation that removes the NO MATCH says what the contract would
have to allow, never that the change does. One that only moves it later is reported, not taken
as a class (a real difference can survive a few more steps by chance). Returns the report's lines
and the class: DELAY?, STUTTER? or VALUE DIFFERENCE. -/
def classifyRun (J : Judge.JSpec) (simS : Judge.Sim) (s0 : StateM) (outs : Array (Array BV))
    (ins : Array RuntimeInput) (lim n K : Nat) (last : List Judge.Last) (ctrl : Bool := false) :
    List String × String := Id.run do
  let tryR (J' : Judge.JSpec) (shift : Nat → Int) : ROut :=
    match (Judge.search J' simS s0 lim (Judge.arraySrc (Judge.shiftedPorts outs ins lim shift)) ()).1 with
    | .matched _ _ _ => .full lim
    | .noMatch m _ => if m > n then .moved m else .same m
    | _ => .other
  let mut ls : List String := []
  let mut delayHit : Option String := none
  let mut stutHit : Option String := none
  let mut moved : Option String := none
  for (cOuts, cWords) in failChans J last do
    let cname := chanLabel J cWords
    let late := cWords.all fun j => match (J.slot.obs.getD j default).port with
      | .out _ t => t
      | _ => false
    let ds : List Int := (if late then [-1] else []) ++ (List.range K).map fun d => ((d + 1 : Nat) : Int)
    for d in ds do
      let r := tryR J (fun o => if cOuts.contains o then d else 0)
      let desc := s!"{cname} read {if d < 0 then "1 step earlier" else s!"{d} step{if d == 1 then "" else "s"} later"}"
      ls := ls ++ [s!"  {desc}: {r.show n}"]
      match r with
      | .full l => if delayHit.isNone then delayHit := some s!"{desc}: the run then matches through step {l - 1}"
      | .moved m => if moved.isNone then moved := some s!"{desc} moves it to step {m}"
      | _ => pure ()
  for j in List.range K do
    let ms := J.maxStall + j + 1
    let r := tryR { J with maxStall := ms } (fun _ => 0)
    ls := ls ++ [s!"  maxstall {ms}: {r.show n}"]
    match r with
    | .full l => if stutHit.isNone then stutHit := some s!"maxstall {ms}: the run then matches through step {l - 1}"
    | .moved m => if moved.isNone then moved := some s!"maxstall {ms} moves it to step {m}"
    | _ => pure ()
  for i in List.range J.xfer.length do
    let x := J.xferName.getD i s!"#{i}"
    let r := tryR { J with xfer := J.xfer.eraseIdx i, xferName := (J.xferName.toList.eraseIdx i).toArray } (fun _ => 0)
    ls := ls ++ [s!"  without xfer {x}: {r.show n}"]
    match r with
    | .full l => if stutHit.isNone then stutHit := some s!"without xfer {x}: the run then matches through step {l - 1}"
    | .moved m => if moved.isNone then moved := some s!"without xfer {x} moves it to step {m}"
    | _ => pure ()
  let cls := if ctrl then
      match delayHit, stutHit with
      | some d, _ => s!"DELAY? ({d}): the contract's late readings and the control's delay: list disagree on this channel by that many steps; both must be the registers the change adds (read the RTL)"
      | none, some t => s!"STUTTER? ({t}): unexpected for D0, which never waits; read the contract's xfer and maxstall lines"
      | none, none => s!"VALUE DIFFERENCE: no relaxation tried makes the contract accept D0 here; read the contract's observed words and guards against the spec's"
    else match delayHit, stutHit, moved with
    | some d, s, _ =>
        s!"DELAY? ({d}{match s with | some t => s!"; also STUTTER? ({t})" | none => ""}). A hint, not a verdict: the delay must be the change's own (count the registers it adds on this channel in the RTL); if it is, read the channel late in the contract, run the controls (control:pos) and search again"
    | none, some s, _ =>
        s!"STUTTER? ({s}). A hint, not a verdict: the change must add these waits (read the RTL); if it does, allow them in the contract, run the controls (control:pos) and search again"
    | none, none, m =>
        s!"VALUE DIFFERENCE (no relaxation tried removes the NO MATCH at step {n}{match m with | some t => s!"; {t} only" | none => ""}). A bug candidate: a bug only on a legal run, under a contract whose controls pass, stated as a transaction that differs; a channel whose delay varies with the state is beyond the judge (open)"
  return (ls, cls)

/-! ### One run of the bug search -/

structure JudgeCtx where
  J      : Judge.JSpec
  NI     : Names
  asm    : Option PropSpec
  asmTxt : String
  stepS  : Judge.Sim
  stepI  : Judge.Sim
  fpS    : Judge.Sim
  fpI    : Judge.Sim
  s0S    : StateM
  s0I    : StateM
  route  : String
  check  : Nat
  K      : Nat
  H      : Nat
  tries  : Nat
  work   : System.FilePath
  label  : String
  report : System.FilePath

/-- The witness lines of a NO MATCH at step `n` (the report and stdout): the inputs as a trace,
whether the environment held, the failing channels as transactions, and the class. `outs`/`ins`
hold D1's run from cycle 0 (at least `n + 2` cycles); `lim` is the window the relaxed contracts
re-read (0: no classification). -/
def witnessReport (X : JudgeCtx) (what : String) (J : Judge.JSpec) (simS : Judge.Sim) (s0 : StateM)
    (outs : Array (Array BV)) (ins : Array RuntimeInput) (n lim : Nat) (last : List Judge.Last)
    (ctrl : Bool := false) : IO Unit := do
  let ps := Judge.shiftedPorts outs ins (min (outs.size - 1) (n + 2 + X.K)) (fun _ => 0)
  appendLine X.report "the failing channels as transactions:"
  for (_, ws) in failChans J last do
    let (ls, sm) := txnView J simS ps n last ws (if ctrl then "the control" else "D1")
    for l in ls do appendLine X.report s!"  {l}"
    say s!"  [{what}] {sm}"
  if X.K > 0 && lim > n then
    let t0 ← IO.monoMsNow
    let (ls, cls) ← IO.lazyPure fun _ => classifyRun J simS s0 outs ins lim n X.K last ctrl
    let t1 ← IO.monoMsNow
    appendLine X.report s!"classification: the same run re-read under relaxed contracts, steps 0..{lim - 1} (each a hint, never a verdict; {t1 - t0} ms):"
    for l in ls do appendLine X.report l
    appendLine X.report s!"CLASS: {cls}"
    say s!"  [{what}] CLASS: {cls}"
  else if X.K > 0 then
    say s!"  [{what}] CLASS: not classified (the run could not be re-read past step {n})"

/-- One run of the bug search: the search on the route, fp's confirmation of a NO MATCH, the
witness report. `none`: go on with the next run; `some c`: stop with exit code `c`. Also the
run's transactions per channel, over the steps searched, and whether the run was cut short (no
vector kept `asm` at a step). `traceEnd`: the run is a trace of that many rows, and `feed`
extends it past them for the classification only (`traceExtFeed`). -/
def judgeRun {γ : Type} (X : JudgeCtx) (what : String) (runNo steps : Nat)
    (feed : Nat → StateM → γ → Judge.FeedR γ) (g0 : γ) (note : γ → String)
    (traceEnd : Option Nat := none) :
    IO (Option UInt32 × List (String × Nat) × Bool) := do
  let ta ← IO.monoMsNow
  let (v, R) ← IO.lazyPure fun _ =>
    Judge.search X.J X.stepS X.s0S steps (Judge.feedSrc feed) ({ g := g0, st := X.s0I } : Judge.RunSt γ)
  let tb ← IO.monoMsNow
  -- the route against fp: D1's first `check` steps, outputs and next state
  if X.route != "fp" then
    let ok := Id.run do
      let mut a := X.s0I
      let mut b := X.s0I
      for i in R.ins.toList.take X.check do
        match X.stepI i a, X.fpI i b with
        | some r, some q =>
            if !(decide ((r.outputs.map normBV) = (q.outputs.map normBV)) &&
                 decide (Judge.normState r.nextState = Judge.normState q.nextState)) then return false
            a := r.nextState; b := q.nextState
        | _, _ => return false
      return true
    unless ok do
      say s!"  [{what}] RESULT: ROUTE DISAGREES: {X.route} and fp differ within the first {X.check} steps"
      return (some 5, [], false)
  -- a NO MATCH on a faster route is re-run on fp alone, on the same inputs
  let v ← match v with
    | .noMatch n _ =>
        if X.route == "fp" then pure v else do
          say s!"  [{what}] NO MATCH on route {X.route} ({tb - ta} ms): re-running this run on fp"
          let (w, _) ← IO.lazyPure fun _ => Judge.search X.J X.fpS X.s0S (n + 1)
            (Judge.feedSrc (traceFeed R.ins X.fpI none)) ({ g := (), st := X.s0I } : Judge.RunSt Unit)
          match w with
          | .noMatch m _ =>
              if m == n then pure w else do
                say s!"  [{what}] RESULT: ROUTE DISAGREES: {X.route} gives NO MATCH at step {n}, fp at step {m}"
                return (some 5, [], false)
          | _ => do
              say s!"  [{what}] RESULT: ROUTE DISAGREES: {X.route} gives NO MATCH at step {n}, fp does not"
              return (some 5, [], false)
    | _ => pure v
  let tc ← IO.monoMsNow
  let done := match v with
    | .noMatch n _ => n + 1 | .matched s _ _ => s | .implRefused n => n | .envViolated n => n
  let rerun := if tc > tb then s!", then {tc - tb} ms on fp" else ""
  let nt := note R.g
  say s!"  [{what}] {done} of {steps} steps searched in {tb - ta} ms on {X.route} ({if done == 0 then 0 else (tb - ta) / done} ms/step: D1, D0 and the comparison){rerun}{if nt.isEmpty then "" else s!"; {nt}"}"
  match v with
  | .implRefused n =>
      say s!"  [{what}] RESULT: REFUSED (the implementation simulator refused step {n})"
      return (some 4, [], false)
  | .envViolated n =>
      if X.tries == 0 then
        say s!"  [{what}] RESULT: ENVIRONMENT VIOLATED at step {n} (the random vector broke {X.asmTxt}; tries:0)"
        appendLine X.report s!"{what}: ENVIRONMENT VIOLATED at step {n}"
        return (some 3, [], false)
      let cnt := countTxns X.J (Judge.shiftedPorts R.outs R.ins (min (n - 1) (R.outs.size - 1)) (fun _ => 0))
      say s!"  [{what}] no input vector kept {X.asmTxt} at step {n}: the run ends there, MATCH over steps 0..{n - 2}"
      appendLine X.report s!"{what}: cut at step {n} (no vector kept {X.asmTxt}); MATCH over steps 0..{n - 2}"
      return (none, cnt, true)
  | .matched steps slotFail maxF =>
      let slotTxt := match slotFail with
        | some (m, w) => s!"; the slot's own stall choice fails at step {m}: {w} (STALL CHOICE WRONG on this run)"
        | none => "; the slot's own stall choice also matches"
      say s!"  [{what}] MATCH FOUND over {steps} steps (largest candidate set {maxF}){slotTxt}"
      appendLine X.report s!"{what}: MATCH FOUND over {steps} steps (largest candidate set {maxF}){slotTxt}"
      return (none, countTxns X.J (Judge.shiftedPorts R.outs R.ins (min steps (R.outs.size - 1)) (fun _ => 0)), false)
  | .noMatch n last =>
      say s!"  [{what}] RESULT: NO MATCH at step {n}: no stuttering alignment of the spec matches the implementation under this contract (its observed words, xfer and maxstall): classify it before calling it a bug (CLASS below)"
      appendLine X.report s!"{what}: NO MATCH at step {n} (under this contract)"
      appendLine X.report "inputs of steps 0..n (nonzero only):"
      let inName (k : Nat) : String := (X.NI.inW.getD k (s!"in{k}", 0)).1
      for (i, j) in (R.ins.toList.take (n + 1)).zipIdx do
        let nz := (i.toList.zipIdx.filter fun (b, _) => bv_uint b != 0).map fun (b, k) => s!"{inName k}={bv_uint b}"
        appendLine X.report s!"  step {j}: {" ".intercalate nz}"
      appendLine X.report "the last candidates of the spec, and why each fails at that step:"
      for (l, c) in last.zipIdx do
        appendLine X.report s!"  candidate {c}: step: {l.step.show X.J}; hold: {l.hold.show X.J}"
      let tsv := X.work / s!"{X.label}_run{runNo}.tsv"
      writeTrace tsv X.NI (R.ins.extract 0 (n + 2))
      say s!"  [{what}] the run's inputs, steps 0..{n + 1}, as a trace: {tsv}"
      if X.asm.isSome then
        match R.envBad with
        | some m =>
            say s!"  [{what}] environment: {X.asmTxt} FAILS at step {m}: this run is not a legal witness"
            appendLine X.report s!"environment: {X.asmTxt} fails at step {m}: not a legal witness"
        | none =>
            say s!"  [{what}] environment: {X.asmTxt} held on steps 0..{n + 1}"
            appendLine X.report s!"environment: {X.asmTxt} held on steps 0..{n + 1}"
      -- D1's run past the NO MATCH, for the transactions and the relaxed contracts (a trace:
      -- past its end, the feed's random vectors that keep asm)
      let lim := if traceEnd.isSome then n + 1 + X.H else min steps (n + 1 + X.H)
      let (R', _) := R.extend feed (lim + X.K + 1)
      let lim := min lim (R'.outs.size - (X.K + 1))
      if let some e := traceEnd then
        if X.K > 0 && R'.ins.size > e + 1 then
          let t := s!"the trace ends at step {e - 1}: the classification reads on to step {R'.ins.size - 1}, past it with random inputs that keep {X.asmTxt} (seed 1; the verdict above is the trace's own)"
          say s!"  [{what}] {t}"
          appendLine X.report t
      witnessReport X what X.J X.stepS X.s0S R'.outs R'.ins n lim last
      return (some 1, [], false)

/-! ### The controls: the contract on D0, whose answer is known -/

/-- The controls (`control:pos`, `control:neg`): the same contract with D0 run as the
implementation. Positive: D0 with each output in `delay:<out>=<k>,...` passed through `k`
registers (the delays the change adds, counted in the RTL; none when no channel is read late)
must give MATCH FOUND on every run: a NO MATCH means the contract is stricter than the change
allows. Negative (`neg`, after the positive one on the same run): the same D0 with one output bit
flipped at a time must give NO MATCH. `flip:<out>@<valid>,...` flips bit 0 of `<out>` while
`<valid>` is 1 (the channel's own valid, which also tests the contract's guards); by default bit
`lo` of every observed word is flipped in every cycle. A flip that is made but never seen is a
word the contract does not compare. A random run is drawn on D1 (keeping `asm` there) and
replayed on D0 by input name; a trace is replayed as given (and extended past its end with
random vectors drawn on D1 that keep `asm`, for the classification only). -/
def controlMain (X : JudgeCtx) (lines : List RMLine) (path : String) (NS : Names) (DS : DesignCert)
    (mode : String) (rest : List String) (runs : List (String × Option (Array RuntimeInput) × Nat × Nat))
    (inW : List Nat) (period phase tries : Nat) (drive : Nat → RuntimeInput → RuntimeInput)
    (randNote : RandG → String) : IO UInt32 := do
  unless mode == "pos" || mode == "neg" do
    IO.eprintln "control:pos|neg"; return 2
  let (J0, _) ← match buildJudge lines NS NS DS (ctrl := true) with
    | .ok x => pure x
    | .error e => do
        IO.eprintln s!"{path}: control (D0 as the implementation, read by the spec's port names): {e}"; return 2
  let outName (o : Nat) : String := (NS.outW.getD o (s!"#{o}", 0)).1
  let mut delays : List (Nat × Nat) := []
  for d in listArg rest "delay" do
    match d.splitOn "=" with
    | [o, k] =>
        match NS.outs[o]?, k.toNat? with
        | some oi, some kk => delays := delays ++ [(oi, kk)]
        | _, _ => IO.eprintln s!"delay: no spec output {repr o}, or a bad delay"; return 2
    | _ => IO.eprintln "delay:<out>=<k>,..."; return 2
  let mut flips : List (Nat × Nat × Option Nat) := []
  for f in listArg rest "flip" do
    match f.splitOn "@" with
    | [o, vv] =>
        match NS.outs[o]?, NS.outs[vv]? with
        | some oi, some vi => flips := flips ++ [(oi, 0, some vi)]
        | _, _ => IO.eprintln s!"flip: no spec output {repr o} or {repr vv}"; return 2
    | _ => IO.eprintln "flip:<out>@<valid>,..."; return 2
  if flips.isEmpty then
    for w in J0.slot.obs do
      if let .out o _ := w.port then flips := flips ++ [(o, w.lo, none)]
    flips := flips.eraseDups
  let doNeg := mode == "neg"
  let delayTxt := if delays.isEmpty then "none" else
    ", ".intercalate (delays.map fun (o, k) => s!"{outName o} by {k}")
  say s!"  control: D0 as the implementation, the slot firing always; output delays (the change's own, from the RTL): {delayTxt}"
  appendLine X.report s!"controls: D0 as the implementation; output delays: {delayTxt}"
  -- D1's inputs to D0's, by name (an input D1 does not have reads 0)
  let inWS := inputWidths DS (inputCount DS)
  let mapIn (v : RuntimeInput) : RuntimeInput := (inWS.zipIdx.map fun (w, k) =>
      let nm := (NS.inW.getD k ("", 0)).1
      match (if nm.isEmpty then none else X.NI.ins[nm]?) with
      | some j => mk_bv w (bv_uint (v.getD j (mk_bv 1 0)))
      | none => mk_bv w 0).toArray
  let d0Trace (sim : Judge.Sim) (ins : Array RuntimeInput) (len : Nat) : Option (Array (Array BV)) := Id.run do
    let mut st := X.s0S
    let mut outs : Array (Array BV) := #[]
    for m in [0:len] do
      match sim (ins.getD m (ins.back?.getD #[])) st with
      | some r => outs := outs.push r.outputs; st := r.nextState
      | none => return none
    return some outs
  let delayed (outs : Array (Array BV)) : Array (Array BV) := outs.mapIdx fun m os => os.mapIdx fun o b =>
    match delays.find? (·.1 == o) with
    | some (_, k) => if k == 0 then b else if m ≥ k then (outs.getD (m - k) #[]).getD o b else mk_bv b.width 0
    | none => b
  let flipped (outs : Array (Array BV)) (lim o bit : Nat) (vld : Option Nat) : Array (Array BV) × Nat := Id.run do
    let mut cnt := 0
    let mut res : Array (Array BV) := #[]
    for (os, m) in outs.toList.zipIdx do
      let on := match vld with | some vi => bv_nonzero (os.getD vi (mk_bv 1 0)) | none => true
      if on && o < os.size then
        let b := os[o]!
        res := res.push (os.set! o (mk_bv b.width (((bv_uint b).toNat ^^^ 2 ^ bit : Nat) : Int)))
        if m ≤ lim then cnt := cnt + 1
      else res := res.push os
    return (res, cnt)
  let XC : JudgeCtx := { X with J := J0, NI := NS, asm := none }
  let mut posFail : List String := []
  let mut seen : Array (Option (Nat × Nat)) := Array.replicate flips.length none
  let mut flipCnt : Array Nat := Array.replicate flips.length 0
  let mut negRuns := 0
  let mut posCounts : List (String × Nat) := []
  for ((what, ins?, steps, seed), k) in runs.zipIdx do
    -- the run's inputs: a trace as given; a random run drawn on D1, keeping asm there
    -- `E`: the rows the classification may re-read (a trace: its rows, its last vector once,
    -- then random vectors drawn on D1 that keep asm; the search reads only the trace)
    let (insD1, L, nt, E) := match ins? with
      | some ins =>
          let rand := randFeed inW X.stepI X.asm (steps + X.H + X.K + 3) period phase tries drive
          let (R, _) := Judge.RunSt.extend (traceExtFeed ins X.stepI X.asm rand)
            ({ g := randInit inW 1, st := X.s0I } : Judge.RunSt RandG) (steps + X.H + X.K + 2)
          let ext := if R.ins.size > steps + 1 then R.ins.extract (steps + 1) R.ins.size else #[]
          let insX := (if ins.isEmpty then ins else ins.push ins.back!) ++ ext
          (insX, steps, "", if ext.isEmpty then steps else insX.size - (X.K + 1))
      | none =>
          let (R, _) := Judge.RunSt.extend (randFeed inW X.stepI X.asm steps period phase tries drive)
            ({ g := randInit inW seed, st := X.s0I } : Judge.RunSt RandG) (steps + 1)
          (R.ins, min steps (R.ins.size - 1), randNote R.g, min steps (R.ins.size - 1))
    if L == 0 then
      say s!"  [{what}] control: no step to run"
      continue
    let insS := insD1.map mapIn
    let len := L + 2 + X.K + X.H
    let some outs0 := d0Trace X.stepS insS len
      | do say s!"  [{what}] RESULT: REFUSED (the D0 simulator refused a step)"; return 4
    let outsC := delayed outs0
    let psC := Judge.shiftedPorts outsC insS L (fun _ => 0)
    posCounts := addCounts posCounts (countTxns J0 psC)
    let (v, _) := Judge.search J0 X.stepS X.s0S L (Judge.arraySrc psC) ()
    match v with
    | .matched _ _ _ =>
        say s!"  [{what}] positive control: MATCH FOUND over {L} steps (ok){if nt.isEmpty then "" else s!"; {nt}"}"
        appendLine X.report s!"{what}: positive control: MATCH FOUND over {L} steps"
        if doNeg then
          negRuns := negRuns + 1
          for ((o, bit, vld), fi) in flips.zipIdx do
            let (outsF, cnt) := flipped outsC L o bit vld
            flipCnt := flipCnt.modify fi (· + cnt)
            if seen[fi]!.isNone && cnt > 0 then
              let (vf, _) := Judge.search J0 X.stepS X.s0S L
                (Judge.arraySrc (Judge.shiftedPorts outsF insS L (fun _ => 0))) ()
              if let .noMatch m _ := vf then seen := seen.set! fi (some (k + 1, m))
    | .noMatch n last =>
        if X.route != "fp" then
          let agree := match d0Trace X.fpS insS len with
            | none => false
            | some of0 =>
                match (Judge.search J0 X.fpS X.s0S L
                    (Judge.arraySrc (Judge.shiftedPorts (delayed of0) insS L (fun _ => 0))) ()).1 with
                | .noMatch m _ => m == n
                | _ => false
          unless agree do
            say s!"  [{what}] RESULT: ROUTE DISAGREES on the positive control: {X.route} gives NO MATCH at step {n}, fp does not agree"
            return 5
        let firstFail := posFail.isEmpty
        posFail := posFail ++ [s!"{what}: NO MATCH at step {n}"]
        say s!"  [{what}] positive control: NO MATCH at step {n}: CONTRACT TOO STRICT: this contract rejects D0 with the change's output delays ({delayTxt})"
        appendLine X.report s!"{what}: positive control: NO MATCH at step {n} (CONTRACT TOO STRICT)"
        unless firstFail do continue
        appendLine X.report "the last candidates of the spec, and why each fails at that step:"
        for (l, c) in last.zipIdx do
          appendLine X.report s!"  candidate {c}: step: {l.step.show J0}; hold: {l.hold.show J0}"
        let tsv := X.work / s!"{X.label}_control_run{k + 1}.tsv"
        writeTrace tsv X.NI (insD1.extract 0 (n + 2))
        say s!"  [{what}] the run's inputs (D1's names), steps 0..{n + 1}, as a trace: {tsv}"
        if ins?.isSome && E > L && X.K > 0 then
          let t := s!"the trace ends at step {L - 1}: the classification reads on past it with random inputs drawn on D1 that keep {X.asmTxt} (seed 1; the verdict above is the trace's own)"
          say s!"  [{what}] {t}"
          appendLine X.report t
        witnessReport XC what J0 X.stepS X.s0S outsC insS n (min E (n + 1 + X.H)) last (ctrl := true)
    | _ =>
        say s!"  [{what}] RESULT: REFUSED (the D0 simulator refused a step)"
        return 4
  -- outputs both designs have that no observed word reads
  let readI : List String := X.J.slot.obs.flatMap fun w =>
    (match w.port with | .out o _ => [(X.NI.outW.getD o ("", 0)).1] | _ => []) ++
    (match w.guard with | some (.out g _, _) => [(X.NI.outW.getD g ("", 0)).1] | _ => [])
  let readS : List String := X.J.chan.obsOut.map outName
  let unread := (X.NI.outW.toList.map (·.1)).filter fun nm =>
    !nm.isEmpty && (NS.outs[nm]?).isSome && !readI.contains nm && !readS.contains nm
  unless unread.isEmpty do
    say s!"  note: outputs both certificates have that no observed word reads (the contract does not compare them): {", ".intercalate unread}"
    appendLine X.report s!"not observed: {", ".intercalate unread}"
  if posFail.isEmpty then say s!"  positive control: MATCH FOUND on every run ({runs.length})"
  unless posCounts.isEmpty do
    say s!"  transactions on the positive control's runs, per channel: {", ".intercalate (posCounts.map fun (nm, k) => s!"{nm} {k}")}"
  -- a random control that sees no transaction on a channel says nothing about it; a trace (a
  -- witness) is the run it is, and need not touch every channel
  let isTrace := runs.any fun (_, ins?, _, _) => ins?.isSome
  let idle := if isTrace then [] else (posCounts.filter (·.2 == 0)).map (·.1)
  let mut blind : List String := []
  if doNeg then
    say s!"  negative control, on the {negRuns} run{if negRuns == 1 then "" else "s"} the positive control passed: one output bit flipped at a time"
    for ((o, bit, vld), fi) in flips.zipIdx do
      let wtxt := s!"{outName o} bit {bit}{match vld with | some vi => s!" while {outName vi} = 1" | none => ", every cycle"}"
      let line := match seen[fi]! with
        | some (r, m) => s!"{wtxt}: seen (NO MATCH at step {m} of run {r})"
        | none =>
            if flipCnt[fi]! == 0 then s!"{wtxt}: never flipped on these runs (not exercised)"
            else s!"{wtxt}: NOT SEEN: flipped on {flipCnt[fi]!} cycles and never compared: the contract does not see this word"
      if (seen[fi]!).isNone && flipCnt[fi]! > 0 then blind := blind ++ [wtxt]
      say s!"    {line}"
      appendLine X.report s!"negative control: {line}"
  if posFail.isEmpty && blind.isEmpty && idle.isEmpty then
    say "  RESULT: CONTROLS PASS"
    say s!"  report: {X.report}"
    return 0
  let why := (if posFail.isEmpty then [] else [s!"positive: CONTRACT TOO STRICT on {posFail.length} run{if posFail.length == 1 then "" else "s"}"]) ++
    (if blind.isEmpty then [] else [s!"negative: not seen: {", ".intercalate blind}"]) ++
    (if idle.isEmpty then [] else [s!"not exercised: no transaction on {", ".intercalate idle} (a control that sees no transaction says nothing; run longer or with other inputs)"])
  say s!"  RESULT: CONTROLS FAIL ({"; ".intercalate why})"
  say s!"  report: {X.report}"
  return 1

def judgeMain (path : String) (rest : List String) : IO UInt32 := do
  let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
  let sf := ((opt "sf").bind (·.toNat?)).getD 20000
  let wf := ((opt "wf").bind (·.toNat?)).getD 200
  let work := System.FilePath.mk ((opt "work").getD "judge_work")
  IO.FS.createDirAll work
  let label := (opt "label").getD "judge"
  let dir := (System.FilePath.mk path).parent.getD "."
  let lines := parseRM (← IO.FS.readFile path)
  let pathOf (k : String) : Option String :=
    ((lines.find? (·.key == k)).bind (·.args.head?)).map fun p =>
      if p.startsWith "/" then p else (dir / p).toString
  let some specCert := pathOf "spec" | IO.eprintln "no spec <certificate>"; return 2
  let some implCert := pathOf "impl" | IO.eprintln "no impl <certificate>"; return 2
  let (_, DS) ← Projection.CertLoadAny.loadAny specCert
  let (_, DI) ← Projection.CertLoadAny.loadAny implCert
  say s!"judge {path}"
  say s!"  spec {specCert}: flops {DS.flops.size} outputs {DS.outputs.size} memories {DS.memories.size}"
  say s!"  impl {implCert}: flops {DI.flops.size} outputs {DI.outputs.size} memories {DI.memories.size}"
  unless HwMem.admitsWB DS && HwMem.admitsWB DI do
    say "  RESULT: REFUSED (the memory route does not admit a certificate)"; return 4
  let t0 ← IO.monoMsNow
  let (RS, _) ← match ← specialize DS sf wf with
    | .ok x => pure x | .error e => do say s!"  RESULT: REFUSED (spec: {e})"; return 4
  let (RI, _) ← match ← specialize DI sf wf with
    | .ok x => pure x | .error e => do say s!"  RESULT: REFUSED (implementation: {e})"; return 4
  let some simS := mkMemSimOfW DS RS | say "  RESULT: REFUSED (no simulator for the spec)"; return 4
  let some simI := mkMemSimOfW DI RI | say "  RESULT: REFUSED (no simulator for the implementation)"; return 4
  let t1 ← IO.monoMsNow
  say s!"  specialized both and built the simulators in {t1 - t0} ms (route fp: runMemSim)"
  let NI ← loadNames implCert
  let NS ← loadNames specCert
  let (J, asm) ← match buildJudge lines NI NS DS with
    | .ok x => pure x | .error e => do IO.eprintln s!"{path}: {e}"; return 2
  let asmTxt := match (lines.find? (·.key == "asm")).bind (·.args.head?) with
    | some a => s!"asm {a}" | none => "asm"
  let (period, phase) : Nat × Nat := match (lines.find? (·.key == "period")).map (·.args) with
    | some [p] => let P := p.toNat?.getD 1; (P, if P > 1 then 1 else 0)
    | some [p, k] => (p.toNat?.getD 1, k.toNat?.getD 0)
    | _ => (1, 0)
  let K := ((opt "classify").bind (·.toNat?)).getD 2
  let H := ((opt "horizon").bind (·.toNat?)).getD 20
  let tries := ((opt "tries").bind (·.toNat?)).getD 64
  say s!"  contract: {J.slot.obs.length} observed words, {J.xfer.length} xfer, maxstall {J.maxStall}; random inputs change at steps n % {period} = {phase}"
  for w in judgeLints J NI do say s!"  warning: {w}"
  let fpS : Judge.Sim := fun i st => runMemSim simS (edgesAll DS) i st
  let fpI : Judge.Sim := fun i st => runMemSim simI (edgesAll DI) i st
  -- `vc` by default; `fp` when a certificate has memories (the concrete ir/vc steps here are
  -- memory-free); `tv`: the old name of `vc`
  let memFree := DS.memories.size == 0 && DI.memories.size == 0
  let route := match opt "sim" with
    | some "tv" => "vc"
    | some r => r
    | none => if memFree then "vc" else "fp"
  if (opt "sim").isNone then
    say s!"  route sim:{route} ({if memFree then "the default" else "a certificate has memories: the concrete ir and vc steps are memory-free"})"
  let check := ((opt "check").bind (·.toNat?)).getD 4
  let ofRT (r : RuntimeResult) : StateM.ResultM := { nextState := { flops := r.nextState.flops, mems := #[] }, outputs := r.outputs }
  let mkRoute (D : DesignCert) (fp : Judge.Sim) : IO (Option Judge.Sim) := do
    if route == "fp" then return some fp
    unless D.memories.size == 0 do say s!"  route {route} needs a memory-free certificate"; return none
    if route == "ir" then
      match Compiler.Direct.checkDesign D with
      | .error _ => say "  route ir: checkDesign refused the certificate"; return none
      | .ok _ => return some fun i st =>
          match Compiler.Direct.checkRuntime D (edgesAll D) i (StateM.denote st) with
          | .ok _ => some (ofRT (Compiler.Direct.directStepRaw D (edgesAll D) i (StateM.denote st)))
          | .error _ => none
    if route == "vc" then
      match compileDesign D with
      | .error e => say s!"  route vc: compileDesign refused the certificate: {repr e}"; return none
      | .ok R => return some fun i st => some (ofRT (denoteResidual R (edgesAll D) i (StateM.denote st)))
    say s!"  sim:{route} is not one of fp | ir | vc"; return none
  let tr0 ← IO.monoMsNow
  let some stepS ← mkRoute DS fpS | return 2
  let some stepI ← mkRoute DI fpI | return 2
  let tr1 ← IO.monoMsNow
  if route != "fp" then say s!"  route {route} built in {tr1 - tr0} ms; cross-checked against fp on the first {check} steps of every run"
  let inW := inputWidths DI (inputCount DI)
  -- `drive <input> <v0> <n> <v1>`: a fixed schedule of an input on random runs
  let mut drives : List (Nat × Int × Nat × Int) := []
  for a in (lines.filter (·.key == "drive")).map (·.args) do
    match a with
    | [nm, v0, n, v1] =>
        match NI.ins[nm]?, v0.toInt?, n.toNat?, v1.toInt? with
        | some k, some x, some m, some y => drives := drives ++ [(k, x, m, y)]
        | _, _, _, _ => IO.eprintln s!"drive: cannot read {a}"; return 2
    | _ => IO.eprintln "drive <input> <v0> <n> <v1>"; return 2
  let drive (n : Nat) (v : RuntimeInput) : RuntimeInput :=
    drives.foldl (fun v (k, x, m, y) => v.set! k (mk_bv (inW.getD k 1) (if n < m then x else y))) v
  -- the runs: (name, the trace's inputs or none for a random run, steps, seed)
  let runs : List (String × Option (Array RuntimeInput) × Nat × Nat) ← match opt "trace", opt "random" with
    | some t, _ => do
        match ← readTrace t NI inW with
        | .ok ins => pure [(s!"trace {t}", some ins, ins.size, 0)]
        | .error e => do IO.eprintln e; return 2
    | none, some r =>
        match (r.splitOn ",").map (·.toNat?) with
        | [some steps, some n] => pure ((List.range n).map fun k => (s!"random seed {k + 1}", none, steps, k + 1))
        | [some steps, some n, some s0] => pure ((List.range n).map fun k => (s!"random seed {s0 + k}", none, steps, s0 + k))
        | _ => do IO.eprintln "random:<steps>,<runs>[,<seed>]"; return 2
    | none, none => do IO.eprintln "judge: trace:<file.tsv> or random:<steps>,<runs>[,<seed>]"; return 2
  let report := work / s!"{label}.txt"
  IO.FS.writeFile report s!"judge {path}\n"
  let X : JudgeCtx := { J, NI, asm, asmTxt, stepS, stepI, fpS, fpI, s0S := zeroState DS, s0I := zeroState DI
                      , route, check, K, H, tries, work, label, report }
  let randNote (G : RandG) : String :=
    if G.redraws + G.held + G.zeros == 0 then "" else
      s!"{G.redraws} step{if G.redraws == 1 then "" else "s"} drawn again, {G.held} held and {G.zeros} set to all zero to keep {asmTxt}"
  -- the controls: D0 as the implementation, under this contract
  if let some mode := opt "control" then
    return ← controlMain X lines path NS DS mode rest runs inW period phase tries drive randNote
  let mut counts : List (String × Nat) := []
  let mut cut := 0
  for ((what, ins?, steps, seed), k) in runs.zipIdx do
    let (r, cnt, wasCut) ← match ins? with
      | some ins =>
          -- past the trace's end, random vectors that keep asm, for the classification only
          let rand := randFeed inW stepI asm (ins.size + H + K + 3) period phase tries drive
          judgeRun X what (k + 1) steps (traceExtFeed ins stepI asm rand) (randInit inW 1) (fun _ => "")
            (traceEnd := some ins.size)
      | none =>
          let feed := randFeed inW stepI asm steps period phase tries drive
          judgeRun X what (k + 1) steps feed (randInit inW seed) randNote
    if let some code := r then
      say s!"  report: {report}"
      return code
    counts := addCounts counts cnt
    if wasCut then cut := cut + 1
  unless counts.isEmpty do
    say s!"  transactions searched, per channel (as the contract reads D1's ports): {", ".intercalate (counts.map fun (nm, k) => s!"{nm} {k}")}"
    appendLine report s!"transactions searched, per channel: {", ".intercalate (counts.map fun (nm, k) => s!"{nm} {k}")}"
  for (nm, k) in counts do
    if k == 0 then say s!"  warning: channel {nm} carried no transaction on these runs: this search says nothing about it"
  say s!"  RESULT: MATCH FOUND on every run ({runs.length}{if cut == 0 then "" else s!", {cut} cut short where no vector kept {asmTxt}"})"
  say s!"  report: {report}"
  return 0

def main (args : List String) : IO UInt32 := do
  match args with
  | "refmap" :: path :: rest => refmapMain path rest false
  | "specfacts" :: path :: rest => refmapMain path rest true
  | "judge" :: path :: rest => judgeMain path rest
  | _ =>
    IO.eprintln "usage: sym_refmap refmap|specfacts <file.refmap> [jobs:<n>] [runs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>] [cache] [solver:<cadical>] [work:<dir>]\n       sym_refmap judge <file.refmap> trace:<file.tsv> | random:<steps>,<runs>[,<seed>] [sim:fp|ir|vc] [check:<n>] [classify:<K>] [horizon:<H>] [tries:<n>] [control:pos|neg [delay:<out>=<k>,...] [flip:<out>@<valid>,...]] [sf:<n>] [wf:<n>] [work:<dir>] [label:<name>]"
    return 2
