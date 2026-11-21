/-
# `lhd_ckpt` — check, compare and make `lhdcert-ckpt v1` checkpoints

```
lhd_ckpt check     <cert> <ckpt>                 parse (digest) and validate against the certificate
lhd_ckpt diff      <a.ckpt> <b.ckpt>             first difference, or SAME
lhd_ckpt seal      <in.ckpt> <out.ckpt>          recompute the digest of a hand-edited checkpoint
lhd_ckpt normalize <in.ckpt> <out.ckpt>          sort words, drop words equal to the default, reseal
lhd_ckpt init      <cert> <out.ckpt> [reset|zero]  the power-on state (default reset) at cycle 0
lhd_ckpt to-state  <cert> <ckpt> <out>           the same state as an lgraph-sim `--state` file
lhd_ckpt import-lhd <cert> <lhd-sim ckpt dir> <out.ckpt> [inst:<prefix>] [map:<file>] [fill] [emit-map]
```

`import-lhd` converts a checkpoint `lhd sim` wrote (`<workdir>/ckpt/<test>/ckp<N>/`: `regs.json`,
one `<inst>.<memory>.hex` per memory, `meta.json`) into `lhdcert-ckpt v1`, so the C++
simulator can fast-forward and a Lean route continue. Names come from the `_io.json` beside
the certificate:
- flop `n` is the `regs.json` key `<inst>.<n>`; memory `m` is the file `<inst>.<i>.hex`
  (`$readmemh` words), `i` being the sidecar's `inst` for it (the cell's instance name, which
  cgen uses), else its `name`. `inst` is the testbench's instance name, found from the `<inst>.__in.*`
  keys unless given.
- `map:<file>` overrides names, one line each: `flop <ordinal> <regs.json key>` or
  `mem <ordinal> <file name>`. `emit-map` prints the mapping used, in that format.
- A flop or memory with no match is refused, naming it; `fill` takes its power-on value
  instead and says so. Keys no flop claims (`__in.*` and `__clkprev*` aside) are listed.
- Values are hex (`0x...`) or decimal, a negative one modulo `2 ^ width`; a memory word past
  the memory's depth must be 0. The cycle is `meta.json`'s `cycle`.

`<cert>` is a DCERT4, DCERT3 or DCERT1 file. Exit codes: 0 ok / SAME, 1 refused / DIFFER,
2 usage or I/O, 3 the checkpoint does not parse.

The format and every refusal are in `Compiler/Ckpt.lean`; `pass/lean/tools/formats/ckpt.md`
documents them.
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.DirectTrace
import HDLProof.Run.IoMeta
import HDLProof.Run.Image

open Compiler Compiler.Ckpt

def loadCert (path : String) : IO DesignCert := do
  let b ← IO.FS.readBinFile path
  match CertIO.admitAny b with
  | .ok (_, D) => pure D
  | .error e   => throw (IO.userError s!"{path}: {e}")

def readCkpt (path : String) (checkDigest : Bool := true) : IO (Except String Ckpt) := do
  return parse (← IO.FS.readBinFile path) checkDigest

def usage : String :=
  "usage: lhd_ckpt check <cert> <ckpt> | diff <a> <b> | seal <in> <out> | normalize <in> <out>\n" ++
  "       | init <cert> <out> [reset|zero] | to-state <cert> <ckpt> <out>\n" ++
  "       | import-lhd <cert> <lhd-sim ckpt dir> <out> [inst:<prefix>] [map:<file>] [fill] [emit-map]"

/-! ## Importing an `lhd sim` checkpoint -/

/-- A flat `{"k": "v", ...}` object as pairs. -/
def readFlatJson (path : System.FilePath) : IO (Array (String × String)) := do
  match Lean.Json.parse (← IO.FS.readFile path) with
  | .error e => throw (IO.userError s!"{path}: {e}")
  | .ok j =>
    match j.getObj? with
    | .error _ => throw (IO.userError s!"{path}: not a JSON object")
    | .ok o => return o.toArray.filterMap fun ⟨k, v⟩ => match v.getStr? with
      | .ok s => some (k, s)
      | .error _ => none

/-- `0x...` hex, or a decimal that may be negative, modulo `2 ^ w`. -/
def parseVal (s : String) (w : Nat) : Option Nat :=
  let m := 2 ^ w
  if s.startsWith "0x" || s.startsWith "0X" then (HDLProof.Run.hexVal? (s.drop 2).toString).map (· % m)
  else match s.toInt? with
    | some v => some ((v % (m : Int)).toNat)
    | none => none

def importLhd (cert dir out : String) (opts : List String) : IO UInt32 := do
  let D ← loadCert cert
  let io ← HDLProof.Run.IoMeta.load cert
  let regs ← readFlatJson (System.FilePath.mk dir / "regs.json")
  let metaJ ← readFlatJson (System.FilePath.mk dir / "meta.json")
  let some cycle := ((metaJ.find? (·.1 == "cycle")).bind fun (_, v) => v.toNat?)
    | do IO.eprintln s!"{dir}/meta.json: no cycle"; return 2
  let fill := opts.contains "fill"
  let emitMap := opts.contains "emit-map"
  -- the instance prefix
  let given := opts.findSome? fun o => if o.startsWith "inst:" then some (o.drop 5).toString else none
  let inst ← match given with
    | some i => pure i
    | none =>
      let cands := (regs.filterMap fun (k, _) => match k.splitOn ".__in." with
        | [p, _] => some p
        | _ => none).toList.eraseDups
      match cands with
      | [p] => pure p
      | [] => do IO.eprintln "import-lhd: no `<inst>.__in.*` key in regs.json; give inst:<prefix>"; return 2
      | ps => do IO.eprintln s!"import-lhd: several instances ({ps}); give inst:<prefix>"; return 2
  -- name overrides
  let mut flopKey : Array (Nat × String) := #[]
  let mut memFile : Array (Nat × String) := #[]
  if let some mp := opts.findSome? (fun o => if o.startsWith "map:" then some (o.drop 4).toString else none) then
    for l in (← IO.FS.readFile mp).splitOn "\n" do
      match (l.splitOn " ").filter (· ≠ "") with
      | ["flop", k, key] => if let some k := k.toNat? then flopKey := flopKey.push (k, key)
      | ["mem", k, f] => if let some k := k.toNat? then memFile := memFile.push (k, f)
      | [] => pure ()
      | _ => if !l.startsWith "#" then IO.eprintln s!"{mp}: ignored `{l}`"
  if io.flops.size != D.flops.size then
    IO.eprintln s!"import-lhd: the sidecar names {io.flops.size} flops, the certificate has {D.flops.size}"
    return 3
  -- flops
  let mut flops : Array (Nat × Nat) := #[]
  let mut used : Array String := #[]
  let mut notes : Array String := #[]
  let mut mapOut : Array String := #[]
  for k in [0:D.flops.size] do
    let f := D.flops[k]!
    let nm := (io.flops.find? (·.ordinal == k)).map (·.name) |>.getD s!"flop_{k}"
    let key := (flopKey.find? (·.1 == k)).map (·.2) |>.getD s!"{inst}.{nm}"
    mapOut := mapOut.push s!"flop {k} {key}"
    match regs.find? (·.1 == key) with
    | some (_, v) =>
      match parseVal v f.width with
      | some x => flops := flops.push (f.width, x); used := used.push key
      | none => IO.eprintln s!"import-lhd: flop {nm}: `{v}` is not a number"; return 3
    | none =>
      if fill then
        flops := flops.push (f.width, (f.resetValue % (2 ^ f.width : Int)).toNat)
        notes := notes.push s!"flop {k} ({nm}): no key `{key}`, took its power-on value"
      else
        IO.eprintln s!"REFUSED: flop {k} ({nm}): regs.json has no key `{key}` (map:<file> names it; fill takes the power-on value)"
        return 1
  -- keys no flop claims, inputs (`__in.`) and clock history (`__clkprev`) aside
  let extra := regs.filter fun (k, _) =>
    !used.contains k && (k.splitOn ".__in.").length == 1 && (k.splitOn "__clkprev").length == 1
  -- memories
  let mut mems : Array Ckpt.MemCk := #[]
  for k in [0:D.memories.size] do
    let md := D.memories[k]!
    let mp := io.memories.find? (·.ordinal == k)
    let nm := (mp.map (·.name)).getD s!"mem_{k}"
    -- cgen names the dump after the cell's instance name; the sidecar's `name` is a wire's
    let cands := ((mp.map (·.inst)).toList.filter (· ≠ "")) ++ [nm]
    let mut file := s!"{inst}.{cands.head!}.hex"
    if let some (_, f) := memFile.find? (·.1 == k) then file := f
    else
      for c in cands do
        if ← (System.FilePath.mk dir / s!"{inst}.{c}.hex").pathExists then
          file := s!"{inst}.{c}.hex"; break
    mapOut := mapOut.push s!"mem {k} {file}"
    let path := System.FilePath.mk dir / file
    if ← path.pathExists then
      let ws ← match HDLProof.Run.parseHexWords (← IO.FS.readFile path) with
        | .ok ws => pure ws
        | .error e => do IO.eprintln s!"{path}: {e}"; return 3
      let mut words : Array (Nat × Nat) := #[]
      for (a, v) in ws do
        let v := v % 2 ^ md.dw
        if a ≥ md.depth then
          if v != 0 then
            IO.eprintln s!"REFUSED: memory {k} ({nm}): {file} has a nonzero word at {a}, past the depth {md.depth}"
            return 1
        else words := words.push (a, v)
      mems := mems.push (Ckpt.normalizeMem { aw := md.aw, dw := md.dw, depth := md.depth, dflt := 0, words })
    else if fill then
      mems := mems.push (Ckpt.saveMem md md.powerOnImg)
      notes := notes.push s!"memory {k} ({nm}): no file `{file}`, took its power-on image"
    else
      IO.eprintln s!"REFUSED: memory {k} ({nm}): no file `{file}` in {dir} (map:<file> names it; fill takes the power-on image)"
      return 1
  let c : Ckpt := { design := designHash D, cycle, flops, mems }
  if let some e := validate D (designHash D) c then
    IO.eprintln s!"REFUSED: the imported state does not fit the certificate: {e}"
    return 1
  IO.FS.writeBinFile out (render c)
  if emitMap then for l in mapOut do IO.println l
  for n in notes do IO.println s!"note: {n}"
  unless extra.isEmpty do
    IO.println s!"note: {extra.size} regs.json keys no flop claims: {", ".intercalate (extra.toList.take 8 |>.map (·.1))}"
  IO.println s!"OK: imported cycle {cycle}: {flops.size} flops, {mems.size} memories (instance `{inst}`)"
  return 0

def main (args : List String) : IO UInt32 := do
  try
    match args with
    | ["check", cert, ck] =>
      let D ← loadCert cert
      match ← readCkpt ck with
      | .error e => IO.eprintln e; return 3
      | .ok c =>
        match validate D (designHash D) c with
        | some e => IO.println s!"REFUSED: {e}"; return 1
        | none =>
          IO.println (s!"OK: cycle {c.cycle}, {c.flops.size} flops, {c.mems.size} memories, " ++
                      s!"{c.exts.size} ext sections")
          return 0
    | ["diff", a, b] =>
      let ra ← readCkpt a
      let rb ← readCkpt b
      match ra, rb with
      | .error e, _ => IO.eprintln e; return 3
      | _, .error e => IO.eprintln e; return 3
      | .ok ca, .ok cb =>
        match Ckpt.diff ca cb with
        | none   => IO.println "SAME"; return 0
        | some d => IO.println s!"DIFFER: {d}"; return 1
    | ["seal", i, o] =>
      match ← readCkpt i (checkDigest := false) with
      | .error e => IO.eprintln e; return 3
      | .ok c => IO.FS.writeBinFile o (render c); return 0
    | ["normalize", i, o] =>
      match ← readCkpt i (checkDigest := false) with
      | .error e => IO.eprintln e; return 3
      | .ok c => IO.FS.writeBinFile o (render (normalize c)); return 0
    | "init" :: cert :: o :: rest =>
      let D ← loadCert cert
      let s ← match rest with
        | [] | ["reset"] => pure (Direct.resetState D)
        | ["zero"]       => pure (Direct.zeroState D)
        | _ => throw (IO.userError usage)
      IO.FS.writeBinFile o (render (save D (designHash D) 0 #[] s))
      return 0
    | "import-lhd" :: cert :: dir :: o :: rest => importLhd cert dir o rest
    | ["to-state", cert, ck, o] =>
      let D ← loadCert cert
      match ← readCkpt ck with
      | .error e => IO.eprintln e; return 3
      | .ok c =>
        match validate D (designHash D) c with
        | some e => IO.println s!"REFUSED: {e}"; return 1
        | none => IO.FS.writeFile o (toStateText c); return 0
    | _ => IO.eprintln usage; return 2
  catch e =>
    IO.eprintln s!"lhd_ckpt: {e}"
    return 2
