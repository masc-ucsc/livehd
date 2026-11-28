/-
# The certificate format version, at the codec (case C1 of the format test plan)

  lean --run dcert_version_check.lean <base.dcert> [write:<dir>]

`<base.dcert>` is a DCERT4 certificate that admission accepts (the tests use
`fixtures/ram12.dcert`, a 12-word RAM of depth 12 and address width 4, exported by lhd).

Checks, each a `FAIL <name>: ...` line when it does not hold:
- a record with every source tag, every operator code, a flop on clock 1 with an asynchronous
  reset, a memory with power-on words, a commit table, an async image and depth 12, and clock
  names with a space, a newline and multi-byte characters renders to bytes that start with
  `DCERT4\n` and parses back to the same record;
- the base re-renders to its own bytes (lhd writes what `renderCert` writes), and admits;
- the versions and inputs below, through `parseCert` and `readCertChecked`, each admitted or
  refused with its own message, as the table `cases` states;
- one trailing newline is admitted; a truncated file and a trailing byte are refused.
A run with no admitted and no refused case fails (`control`).

`write:<dir>` also writes the certificates the driver test (`cert_admit_selftest.sh`) runs
every driver on:

| file | content |
|---|---|
| `ok4.dcert` | the base |
| `ok3.dcert` | the base as DCERT3: byte 5 is `3` and the memory records have no depth |
| `v2.dcert` | the base with byte 5 set to `2` |
| `v1min.dcert` | `DCERT1` with every section empty |
| `v1flop.dcert` | `DCERT1` with one input, one synchronous flop and one output |
| `depth0.dcert` | the base with every memory's depth 0 |
| `noclk.dcert` | the base with no clock domains |
| `badout.dcert` | the base with output 0 reading a slot past the design |
| `junk.dcert` | `not a certificate` |

Prints `PASS dcert_version_check: <n> cases` and exits 0, or exits 1.
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Compiler.CertIORoute

open Compiler Compiler.CertIO

/-- Field by field: `DesignCert` has no `DecidableEq`, its fields do. -/
def certEq (a b : DesignCert) : Bool :=
  a.sources == b.sources && a.nodes == b.nodes && a.outputs == b.outputs &&
  a.flops == b.flops && a.memories == b.memories && a.clocks == b.clocks

/-- The DCERT3 memory record (the DCERT4 one without the depth), written here and not taken from
the library, so it checks the library's DCERT3 code from outside. -/
def memBody3 (m : MemoryDesc) : List UInt8 :=
  let (ha, a) := match m.asyncImg with | some x => (1, x) | none => (0, 0)
  natBytes m.aw ++ spc (natBytes m.dw) ++ spc (natBytes m.nextImg) ++ spc (natBytes m.clock)
    ++ spc (natBytes m.init.size) ++ (m.init.toList.map (fun v => spc (natBytes v))).flatten
    ++ spc (natBytes m.commits.size) ++ (m.commits.toList.map commitBody).flatten
    ++ spc (natBytes ha) ++ spc (natBytes a)

/-- `D` written as DCERT3. -/
def render3 (D : DesignCert) : ByteArray :=
  ⟨("DCERT3\n".toUTF8.toList ++ section' srcBytes D.sources ++ section' nodeBytes D.nodes
    ++ section' outBytes D.outputs ++ section' flopBytes D.flops
    ++ section' (fun m => memBody3 m ++ [chNL]) D.memories ++ section' clkBytes D.clocks).toArray⟩

/-- Every record kind the format has, for the round trip. Not an admissible design: the codec
judges nothing, and this checks only that it reads back what it wrote. -/
def rich : DesignCert where
  sources  := #[.input 0 8, .const 8 (-3), .flopQ 0 8, .flopQAsync 1 8 0 5 true, .memImg 0 4 8,
                .memConst 2 8 #[1, -2, 3, 4]]
  nodes    := ((List.range 31).filterMap fun c =>
                let arg : Int := match c with | 0 => -5 | 1 => 2 | 28 => 1 | 29 => 2 | 30 => 2 | _ => 0
                (opOfCode c arg).map fun op =>
                  ({ op := op, width := 8 + c, deps := #[0, c % 6], origin := 1000000000 + c } :
                    DenseNodeCert)).toArray
  outputs  := #[{ slot := 0, width := 8 }, { slot := 36, width := 38 }]
  flops    := #[{ width := 8, din := 6, enable := some 1, resetPin := some 0, resetValue := -1,
                  resetActiveLow := true, clock := 1, asyncReset := true },
                { width := 8, din := 7, enable := none, resetPin := none, resetValue := 0,
                  resetActiveLow := false }]
  memories := #[{ aw := 4, dw := 8, nextImg := 30, clock := 1, init := #[1, 2, 3],
                  commits := #[{ mask := 1, nextImg := 30 }, { mask := 2, nextImg := 31 }],
                  asyncImg := some 32, depth := 12 },
                { aw := 2, dw := 1, nextImg := 33 }]
  clocks   := #[{ name := "clk" }, { name := "a b\nc" }, { name := "λ→時計" }]

/-- What an entry point must do with an input: admit it, or refuse it with a message that
contains the given text. -/
inductive Want where
  | ok
  | err (msg : String)

def Want.show : Want → String
  | .ok => "admitted"
  | .err m => s!"refused ({m})"

/-- One input and what `parseCert` and `readCertChecked` must do with it. -/
structure Case where
  name  : String
  bytes : ByteArray
  parse : Want
  check : Want

def judge (w : Want) {α : Type} (r : Except String α) : Option String :=
  match w, r with
  | .ok, .ok _ => none
  | .ok, .error e => some s!"refused: {e}"
  | .err _, .ok _ => some "admitted"
  | .err m, .error e => if (e.splitOn m).length > 1 then none else some s!"refused with '{e}'"

def withByte (b : ByteArray) (k : Nat) (v : UInt8) : ByteArray := b.set! k v

def str (s : String) : ByteArray := s.toUTF8

/-- The cases, built from the base certificate `D` (its bytes `b`). The version rows say what
the codec does today; each version a later change retires flips its row to a refusal. -/
def cases (D : DesignCert) (b : ByteArray) : List Case :=
  let mem0 (f : MemoryDesc → MemoryDesc) := { D with memories := D.memories.map f }
  let badout := { D with outputs := D.outputs.modify 0 fun o => { o with slot := D.numSlots } }
  [ { name := "ok4", bytes := b, parse := .ok, check := .ok }
  , { name := "ok4 with a trailing newline", bytes := b.push 10, parse := .ok, check := .ok }
    -- today: DCERT3 is read, every memory at the whole 2^aw table
  , { name := "ok3", bytes := render3 D, parse := .ok, check := .ok }
  , { name := "v2", bytes := withByte b 5 50, parse := .err "bad magic", check := .err "bad magic" }
  , { name := "v1min", bytes := str "DCERT1\n0\n0\n0\n0\n0\n", parse := .err "bad magic",
      check := .err "bad magic" }
  , { name := "v1flop", bytes := str v1flopText, parse := .err "bad magic", check := .err "bad magic" }
  , { name := "junk", bytes := str "not a certificate\n", parse := .err "bad magic",
      check := .err "bad magic" }
  , { name := "truncated", bytes := b.extract 0 (b.size - 10), parse := .err "malformed",
      check := .err "malformed" }
  , { name := "trailing byte", bytes := b.push 120, parse := .err "malformed", check := .err "malformed" }
  , { name := "noclk", bytes := renderCert { D with clocks := #[] }, parse := .ok,
      check := .err "no clock domains" }
  , { name := "badout", bytes := renderCert badout, parse := .ok, check := .err "output 0 reads slot" }
  , { name := "depth0", bytes := renderCert (mem0 fun m => { m with depth := 0 }), parse := .ok,
      check := .err "declares depth 0" }
  , { name := "depth 2^aw+1", bytes := renderCert (mem0 fun m => { m with depth := 2 ^ m.aw + 1 }),
      parse := .ok, check := .err "declares depth" } ]
where
  /-- One input, one synchronous flop on it, one output reading the flop. -/
  v1flopText : String := "DCERT1\n2\n0 0 1\n2 0 1\n0\n1\n1 1\n1\n1 0 0 0 0 0 0 0\n0\n"

/-- What `CertIO.admitAny` (every driver's admission today) does with the version inputs:
DCERT4 and DCERT3 through `readCertChecked`, DCERT1 through `admitV1`. -/
def admitAnyCases (D : DesignCert) (b : ByteArray) : List (String × ByteArray × Want) :=
  [ ("ok4", b, .ok), ("ok3", render3 D, .ok), ("v2", withByte b 5 50, .err "unrecognised"),
    ("v1min", str "DCERT1\n0\n0\n0\n0\n0\n", .ok), ("v1flop", str (cases.v1flopText), .ok),
    ("junk", str "not a certificate\n", .err "unrecognised") ]

def main (args : List String) : IO UInt32 := do
  let some base := args.head? | IO.eprintln "usage: dcert_version_check <base.dcert> [write:<dir>]"; return 2
  let b ← IO.FS.readBinFile base
  let D ← match readCertChecked b with
    | .ok D => pure D
    | .error e => do IO.println s!"FAIL base: {base}: {e}"; return 1
  let mut fails := 0
  let mut n := 0
  let fail (s : String) : IO Unit := IO.println s!"FAIL {s}"
  -- the round trip on every record kind
  n := n + 1
  let rb := renderCert rich
  unless rb.extract 0 7 == str "DCERT4\n" do fail "rich: the rendering does not start with DCERT4"; fails := fails + 1
  match parseCert rb with
  | .ok R => unless certEq R rich do fail "rich: parseCert (renderCert rich) is another record"; fails := fails + 1
  | .error e => fail s!"rich: parseCert (renderCert rich) refused: {e}"; fails := fails + 1
  -- lhd writes what renderCert writes
  n := n + 1
  unless renderCert D == b do fail s!"base: {base} is not renderCert of what it parses to"; fails := fails + 1
  let mut nok := 0
  let mut nerr := 0
  for c in cases D b do
    n := n + 1
    for (w, nm, r) in [(c.parse, "parseCert", (parseCert c.bytes).map (fun _ => ())),
                       (c.check, "readCertChecked", (readCertChecked c.bytes).map (fun _ => ()))] do
      match w with | .ok => nok := nok + 1 | .err _ => nerr := nerr + 1
      if let some why := judge w r then
        fail s!"{c.name}: {nm}: want {w.show}, got {why}"; fails := fails + 1
  for (nm, bs, w) in admitAnyCases D b do
    n := n + 1
    match w with | .ok => nok := nok + 1 | .err _ => nerr := nerr + 1
    if let some why := judge w ((admitAny bs).map (fun _ => ())) then
      fail s!"{nm}: admitAny: want {w.show}, got {why}"; fails := fails + 1
  -- control: the table has both kinds
  unless nok > 0 && nerr > 0 do fail s!"control: {nok} admitted and {nerr} refused expectations"; fails := fails + 1
  -- the driver test's certificates
  if let some dir := (args.drop 1).head? >>= (fun a => if a.startsWith "write:" then some (a.drop 6).toString else none) then
    IO.FS.createDirAll dir
    let byName := (cases D b).map fun c => (c.name, c.bytes)
    for (nm, file) in [("ok4", "ok4"), ("ok3", "ok3"), ("v2", "v2"), ("v1min", "v1min"),
                       ("v1flop", "v1flop"), ("depth0", "depth0"), ("noclk", "noclk"),
                       ("badout", "badout"), ("junk", "junk")] do
      match byName.lookup nm with
      | some bs => IO.FS.writeBinFile (System.FilePath.mk dir / s!"{file}.dcert") bs
      | none => fail s!"write: no case {nm}"; fails := fails + 1
  if fails > 0 then
    IO.println s!"FAIL dcert_version_check: {fails} of {n} cases"
    return 1
  IO.println s!"PASS dcert_version_check: {n} cases ({nok} admitted and {nerr} refused expectations)"
  return 0
