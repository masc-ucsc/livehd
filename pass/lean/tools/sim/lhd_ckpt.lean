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
                    [sim:<lhd sim folder>]
```

`import-lhd` converts a checkpoint `lhd sim` wrote into `lhdcert-ckpt v1`, so the C++ simulator can
fast-forward and a Lean route continue. It reads both layouts `lhd sim` has written:
- the current one (since 2026-11-21): `<simroot>/ckpt/<test>/c<N>/` with `state.bin` (the DUT state
  words), `frame.bin` (the testbench's, not read) and `meta.json` (`cycle`). The layout of
  `state.bin` comes from `<simroot>/sim/drv.tb.json` (each test's instances: `var`, `stem`,
  `base`, the instance's first word) and `<simroot>/sim/<stem>.dut.json` (each field's `name`,
  `kind` 2 flop / 3 memory, `bits`, `word`, `entries`). A value is `bits` bits from bit
  `(base + word) * 64`, low bits first; memory entry `e` is `e * stride` bits further, the stride
  being the entry's storage width (8, 16, 32 or whole words), or `bits` for a memory of at most
  128 bits packed. `sim:<dir>` gives the `sim` folder when the checkpoint was moved.
- the earlier one: `<workdir>/ckpt/<test>/ckp<N>/` with `regs.json`, one `<inst>.<memory>.hex` per
  memory, and `meta.json`. `lhd sim` has not written it since 2026-11-21, but
  `pass/lean/xpatch/transport.py` writes it (a state built outside `lhd sim`, by flop and memory
  name, for another certificate), so this reader stays.
Names come from the `_io.json` beside the certificate:
- flop `n` is the key `<inst>.<n>` (a `regs.json` key, or the field `n` of instance `inst`);
  memory `m` is `<inst>.<i>` (the field, or the file `<inst>.<i>.hex`: `$readmemh` words), `i` being
  the sidecar's `inst` for it (the cell's instance name), else its `name`. `inst` is the
  testbench's instance name (`var`), found when the checkpoint holds one instance (earlier layout:
  from the `<inst>.__in.*` keys) unless given.
- `map:<file>` overrides names, one line each: `flop <ordinal> <key>` or `mem <ordinal> <key>` (a
  key as `lhd sim --list-signals` names it, `<inst>.<field>`; earlier layout: a `regs.json` key
  and a file name). `emit-map` prints the mapping used, in that format.
- A flop or memory with no match is refused, naming it; `fill` takes its power-on value
  instead and says so. Flop keys no flop claims (earlier layout: `__in.*` and `__clkprev*` aside)
  are listed. A field whose width is not the certificate's is refused.
- Values are hex (`0x...`) or decimal, a negative one modulo `2 ^ width`; a memory word past
  the memory's depth must be 0. The cycle is `meta.json`'s `cycle`.

`<cert>` is a DCERT4 file (DCERT3 is still read), admitted with `CertIO.readCertChecked`. Exit codes: 0 ok / SAME, 1 refused / DIFFER,
2 usage or I/O, 3 the checkpoint does not parse.

The format and every refusal are in `Compiler/Ckpt.lean`; `pass/lean/tools/formats/ckpt.md`
documents them.
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Compiler.DirectTrace
import HDLProof.Run.IoMeta
import HDLProof.Run.Image

open Compiler Compiler.Ckpt

/-- `CertIO.loadCert`: read the file, admit it with `readCertChecked`. -/
def loadCert (path : String) : IO DesignCert := CertIO.loadCert path

def readCkpt (path : String) (checkDigest : Bool := true) : IO (Except String Ckpt) := do
  return parse (← IO.FS.readBinFile path) checkDigest

def usage : String :=
  "usage: lhd_ckpt check <cert> <ckpt> | diff <a> <b> | seal <in> <out> | normalize <in> <out>\n" ++
  "       | init <cert> <out> [reset|zero] | to-state <cert> <ckpt> <out>\n" ++
  "       | import-lhd <cert> <lhd-sim ckpt dir> <out> [inst:<prefix>] [map:<file>] [fill] [emit-map]\n" ++
  "         [sim:<lhd sim folder>]"

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

/-- An `lhd sim` checkpoint as `import-lhd` reads it, in either layout. -/
structure Snap where
  cycle  : Nat
  /-- what the flop keys come from, for messages -/
  src    : String
  /-- every flop key (`<inst>.<name>`) with its value (`0x...` or decimal) -/
  regs   : Array (String × String)
  /-- each flop key's width, when the layout records it -/
  bits   : Array (String × Nat) := #[]
  /-- the instances the checkpoint holds -/
  insts  : List String
  /-- keys the unclaimed-key note leaves out -/
  skip   : String → Bool := fun _ => false
  /-- the key of a memory of instance `inst` with one of these names, when there is one -/
  memKey : String → List String → IO (Option String)
  /-- the key a memory with name `n` of instance `inst` would have (for the refusal) -/
  memDefault : String → String → String
  /-- a memory's nonzero words (address, value) and its word width when the layout records it;
  `none` when there is no memory with that key -/
  memWords : String → IO (Option (Except String (Array (Nat × Nat) × Option Nat)))
  /-- how a missing memory is named in a refusal -/
  memWhat : String := "file"

/-- The earlier layout: `regs.json`, one `<inst>.<memory>.hex` per memory, `meta.json`. -/
def readSnapRegs (dir : System.FilePath) : IO (Except String Snap) := do
  let regs ← readFlatJson (dir / "regs.json")
  let metaJ ← readFlatJson (dir / "meta.json")
  let some cycle := ((metaJ.find? (·.1 == "cycle")).bind fun (_, v) => v.toNat?)
    | return .error s!"{dir}/meta.json: no cycle"
  let insts := (regs.filterMap fun (k, _) => match k.splitOn ".__in." with
    | [p, _] => some p
    | _ => none).toList.eraseDups
  return .ok {
    cycle, src := "regs.json", regs, insts
    skip := fun k => (k.splitOn ".__in.").length != 1 || (k.splitOn "__clkprev").length != 1
    memKey := fun inst cands => do
      for c in cands do
        if ← (dir / s!"{inst}.{c}.hex").pathExists then return some s!"{inst}.{c}.hex"
      return none
    memDefault := fun inst c => s!"{inst}.{c}.hex"
    memWords := fun key => do
      let path := dir / key
      unless ← path.pathExists do return none
      return some <| match HDLProof.Run.parseHexWords (← IO.FS.readFile path) with
        | .ok ws => .ok (ws, none)
        | .error e => .error s!"{path}: {e}" }

/-- `w` bits of `b` from bit `off`, low bits first (little-endian words). -/
def bitsAt (b : ByteArray) (off w : Nat) : Nat := Id.run do
  if w == 0 then return 0
  let lo := off / 8
  let hi := (off + w + 7) / 8
  let mut v := 0
  for i in [lo:hi] do
    v := v + (b.get! i).toNat <<< (8 * (i - lo))
  return (v >>> (off % 8)) % 2 ^ w

/-- A memory entry's storage width in `state.bin` (lnast_sim_rt.hpp's `put`). -/
def storeBits (bits : Nat) : Nat :=
  if bits ≤ 8 then 8 else if bits ≤ 16 then 16 else if bits ≤ 32 then 32 else ((bits + 63) / 64) * 64

/-- A JSON file, parsed. -/
def readJson (path : System.FilePath) : IO (Except String Lean.Json) := do
  unless ← path.pathExists do return .error s!"{path}: no such file"
  return (Lean.Json.parse (← IO.FS.readFile path)).mapError fun e => s!"{path}: {e}"

/-- One field of a `.dut.json`. -/
structure DutField where
  name    : String
  kind    : Nat
  bits    : Nat
  word    : Nat
  entries : Nat

/-- The current layout: `state.bin` and `meta.json`, laid out by the `sim` folder's `drv.tb.json`
and `<stem>.dut.json` files (module header). -/
def readSnapState (dir : System.FilePath) (simDir : Option String) : IO (Except String Snap) := do
  let fail (m : String) : IO (Except String Snap) := pure (.error m)
  let metaJ ← match ← readJson (dir / "meta.json") with
    | .ok j => pure j
    | .error e => return .error e
  let some cycle := (metaJ.getObjValAs? Nat "cycle").toOption
    | fail s!"{dir}/meta.json: no cycle"
  -- `<simroot>/ckpt/<test>/c<N>`: the test's folder and the simulation root
  let some testDir := dir.parent | fail s!"{dir}: not inside a test's checkpoint folder"
  let some testName := testDir.fileName | fail s!"{dir}: not inside a test's checkpoint folder"
  let sim : System.FilePath := match simDir with
    | some d => d
    | none => ((testDir.parent.bind (·.parent)).getD ".") / "sim"
  let man ← match ← readJson (sim / "drv.tb.json") with
    | .ok j => pure j
    | .error e => return .error s!"{e} (sim:<dir> names the lhd sim folder)"
  -- the test whose folder this is: its name with every character but letters and digits as `_`
  let safe (n : String) : String := String.map (fun c => if c.isAlphanum then c else '_') n
  let tests := ((man.getObjValAs? (Array Lean.Json) "tests").toOption).getD #[]
  let some t := tests.find? (fun t => ((t.getObjValAs? String "name").toOption.map safe) == some testName)
    | fail s!"{sim}/drv.tb.json: no test whose checkpoint folder is `{testName}`"
  let insts := ((t.getObjValAs? (Array Lean.Json) "instances").toOption).getD #[]
  let st ← IO.FS.readBinFile (dir / "state.bin")
  if let some w := (metaJ.getObjValAs? Nat "words").toOption then
    if w * 8 != st.size then
      return .error s!"{dir}/state.bin: {st.size} bytes, meta.json says {w} words"
  -- each instance's fields, and every flop's value
  let mut regs : Array (String × String) := #[]
  let mut bits : Array (String × Nat) := #[]
  let mut vars : List String := []
  let mut mems : Array (String × DutField × Nat) := #[]
  for i in insts do
    let some var := (i.getObjValAs? String "var").toOption | return .error s!"{sim}/drv.tb.json: an instance without `var`"
    let some stem := (i.getObjValAs? String "stem").toOption | return .error s!"{sim}/drv.tb.json: an instance without `stem`"
    let some base := (i.getObjValAs? Nat "base").toOption | return .error s!"{sim}/drv.tb.json: an instance without `base`"
    vars := vars ++ [var]
    let dut ← match ← readJson (sim / s!"{stem}.dut.json") with
      | .ok j => pure j
      | .error e => return .error e
    for f in ((dut.getObjValAs? (Array Lean.Json) "fields").toOption).getD #[] do
      let get (k : String) : Nat := ((f.getObjValAs? Nat k).toOption).getD 0
      let fd : DutField := { name := ((f.getObjValAs? String "name").toOption).getD "",
                             kind := get "kind", bits := get "bits", word := get "word", entries := get "entries" }
      let key := s!"{var}.{fd.name}"
      if fd.kind == 2 && fd.bits > 0 then
        let off := (base + fd.word) * 64
        if off + fd.bits > st.size * 8 then
          return .error s!"{dir}/state.bin: {key} lies past the end of the file"
        regs := regs.push (key, toString (bitsAt st off fd.bits))
        bits := bits.push (key, fd.bits)
      else if fd.kind == 3 && fd.bits > 0 then
        mems := mems.push (key, fd, base)
  return .ok {
    cycle, src := "state.bin", regs, bits, insts := vars, memWhat := "memory"
    memKey := fun inst cands => pure <| cands.findSome? fun c =>
      if mems.any (·.1 == s!"{inst}.{c}") then some s!"{inst}.{c}" else none
    memDefault := fun inst c => s!"{inst}.{c}"
    memWords := fun key => pure <| (mems.find? (·.1 == key)).map fun (_, fd, base) => Id.run do
      let stride := if storeBits fd.bits != fd.bits && fd.bits * fd.entries ≤ 128 then fd.bits
        else storeBits fd.bits
      let start := (base + fd.word) * 64
      if start + fd.entries * stride > st.size * 8 then
        return .error s!"{dir}/state.bin: memory {key} lies past the end of the file"
      let mut ws : Array (Nat × Nat) := #[]
      for e in [0:fd.entries] do
        let v := bitsAt st (start + e * stride) fd.bits
        if v != 0 then ws := ws.push (e, v)
      return .ok (ws, some fd.bits) }

def importLhd (cert dir out : String) (opts : List String) : IO UInt32 := do
  let D ← loadCert cert
  let io ← HDLProof.Run.IoMeta.load cert
  let d := System.FilePath.mk dir
  let simDir := opts.findSome? fun o => if o.startsWith "sim:" then some (o.drop 4).toString else none
  -- the layout: the current one has state.bin, the earlier one regs.json
  let snap ← if ← (d / "state.bin").pathExists then readSnapState d simDir else readSnapRegs d
  let S ← match snap with
    | .ok s => pure s
    | .error e => do IO.eprintln s!"import-lhd: {e}"; return 2
  let cycle := S.cycle
  let regs := S.regs
  let fill := opts.contains "fill"
  let emitMap := opts.contains "emit-map"
  -- the instance prefix
  let given := opts.findSome? fun o => if o.startsWith "inst:" then some (o.drop 5).toString else none
  let inst ← match given with
    | some i => pure i
    | none =>
      match S.insts with
      | [p] => pure p
      | [] => do
        if S.src == "regs.json" then
          IO.eprintln "import-lhd: no `<inst>.__in.*` key in regs.json; give inst:<prefix>"
        else IO.eprintln s!"import-lhd: {S.src}'s test has no instance; give inst:<prefix>"
        return 2
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
      if let some (_, b) := S.bits.find? (·.1 == key) then
        if b != f.width then
          IO.eprintln s!"REFUSED: flop {k} ({nm}): `{key}` has {b} bits in {S.src}, the certificate's flop {f.width}"
          return 1
      match parseVal v f.width with
      | some x => flops := flops.push (f.width, x); used := used.push key
      | none => IO.eprintln s!"import-lhd: flop {nm}: `{v}` is not a number"; return 3
    | none =>
      if fill then
        flops := flops.push (f.width, (f.resetValue % (2 ^ f.width : Int)).toNat)
        notes := notes.push s!"flop {k} ({nm}): no key `{key}`, took its power-on value"
      else
        IO.eprintln s!"REFUSED: flop {k} ({nm}): {S.src} has no key `{key}` (map:<file> names it; fill takes the power-on value)"
        return 1
  -- keys no flop claims
  let extra := regs.filter fun (k, _) => !used.contains k && !S.skip k
  -- memories
  let mut mems : Array Ckpt.MemCk := #[]
  for k in [0:D.memories.size] do
    let md := D.memories[k]!
    let mp := io.memories.find? (·.ordinal == k)
    let nm := (mp.map (·.name)).getD s!"mem_{k}"
    -- the dump or field is named after the cell's instance name; the sidecar's `name` is a wire's
    let cands := ((mp.map (·.inst)).toList.filter (· ≠ "")) ++ [nm]
    let key ← match memFile.find? (·.1 == k) with
      | some (_, f) => pure f
      | none => do pure ((← S.memKey inst cands).getD (S.memDefault inst cands.head!))
    mapOut := mapOut.push s!"mem {k} {key}"
    match ← S.memWords key with
    | some (.error e) => IO.eprintln s!"import-lhd: {e}"; return 3
    | some (.ok (ws, wbits)) =>
      if let some b := wbits then
        if b != md.dw then
          IO.eprintln s!"REFUSED: memory {k} ({nm}): `{key}` has {b}-bit words in {S.src}, the certificate's memory {md.dw}"
          return 1
      let mut words : Array (Nat × Nat) := #[]
      for (a, v) in ws do
        let v := v % 2 ^ md.dw
        if a ≥ md.depth then
          if v != 0 then
            IO.eprintln s!"REFUSED: memory {k} ({nm}): {key} has a nonzero word at {a}, past the depth {md.depth}"
            return 1
        else words := words.push (a, v)
      mems := mems.push (Ckpt.normalizeMem { aw := md.aw, dw := md.dw, depth := md.depth, dflt := 0, words })
    | none =>
      if fill then
        mems := mems.push (Ckpt.saveMem md md.powerOnImg)
        notes := notes.push s!"memory {k} ({nm}): no {S.memWhat} `{key}`, took its power-on image"
      else
        IO.eprintln s!"REFUSED: memory {k} ({nm}): no {S.memWhat} `{key}` in {dir} (map:<file> names it; fill takes the power-on image)"
        return 1
  let c : Ckpt := { design := designHash D, cycle, flops, mems }
  if let some e := validate D (designHash D) c then
    IO.eprintln s!"REFUSED: the imported state does not fit the certificate: {e}"
    return 1
  IO.FS.writeBinFile out (render c)
  if emitMap then for l in mapOut do IO.println l
  for n in notes do IO.println s!"note: {n}"
  unless extra.isEmpty do
    IO.println s!"note: {extra.size} {S.src} keys no flop claims: {", ".intercalate (extra.toList.take 8 |>.map (·.1))}"
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
