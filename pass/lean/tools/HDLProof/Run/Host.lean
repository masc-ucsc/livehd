/-
# The host around a design: `bind v1`

A `bind v1` file says how a run drives a design and serves its memory requests. Ports are named
as in the `_io.json` beside the certificate (or given as ordinals). `pass/lean/tools/formats/bind.md`
documents the format; in short:

```
bind v1
reset <input> <active-value> <cycles>   hold the input at the value for the first cycles
const <input> <value>                   drive the input with a constant
bus <name> valid=<out> addr=<out> ready=<in> rdata=<in> [wdata=<out>] [wstrb=<out> | we=<out>]
           [instr=<out>] [latency=zero|fixed:<n>|random:<seed>:<max>]
ram <base> <size>                       host memory (bytes), read and written by every bus
rom <base> <size>                       host memory that a write stops the run on (rom-write)
console <addr>                          a write appends its low byte to the console
exit <addr>                             a write stops the run (status exit, code = the data)
word <addr> <name>                      a register: reads give the last value written (0 at first)
stop-output <out> <value>               stop (status stop-output) when the output has the value
break <addr>                            stop (breakpoint) when an instruction fetch asks for the address
watch <addr>                            stop (watchpoint) after a data access to the word completes
```

Numbers are decimal or `0x` hex. A bus is the simple valid/ready handshake: the design raises
`valid` with the request fields and holds them until the host raises `ready` for one cycle; a
read's data is on `rdata` in that cycle. Addresses are byte addresses; the data word is the
`rdata` port's width (a multiple of 8). A write is `wstrb != 0` (bit `i` writes byte `i`) or
`we = 1` (every byte).

The host state (memory, pending requests, devices, console, random generator, statistics) is
plain data; `HostState.encode` / `decode` give its checkpoint `ext` section, `cert_run host v1`.
-/
import HDLProof.Run.IoMeta
import HDLProof.Run.Image

namespace HDLProof
namespace Run

/-! ## The configuration -/

inductive Latency where
  | zero
  | fixed (n : Nat)
  | random (seed max : Nat)
deriving BEq, Repr, Inhabited

def Latency.render : Latency → String
  | .zero => "zero" | .fixed n => s!"fixed:{n}" | .random s m => s!"random:{s}:{m}"

def parseNum? (s : String) : Option Nat :=
  if s.startsWith "0x" || s.startsWith "0X" then hexVal? (s.drop 2).toString else s.toNat?

def Latency.parse? (s : String) : Option Latency :=
  match s.splitOn ":" with
  | ["zero"] => some .zero
  | ["fixed", n] => (parseNum? n).map .fixed
  | ["random", a, b] => match parseNum? a, parseNum? b with
    | some a, some b => some (.random a b)
    | _, _ => none
  | _ => none

structure Bus where
  name    : String
  valid   : Nat
  addr    : Nat
  ready   : Nat
  rdata   : Nat
  /-- the data word in bytes: the `rdata` input's width / 8 -/
  bytes   : Nat
  wdata   : Option Nat := none
  wstrb   : Option Nat := none
  we      : Option Nat := none
  instr   : Option Nat := none
  latency : Latency := .zero
deriving Inhabited, Repr

inductive DevKind where
  | console | exit | word (name : String)
deriving BEq, Inhabited, Repr

structure Region where
  base : Nat
  size : Nat
  rom  : Bool
deriving Inhabited, Repr

structure Bind where
  reset   : Option (Nat × Nat × Nat) := none   -- input ordinal, active value, cycles
  consts  : Array (Nat × Nat) := #[]           -- input ordinal, value
  buses   : Array Bus := #[]
  regions : Array Region := #[]
  devices : Array (Nat × DevKind) := #[]       -- word address, kind
  stops   : Array (Nat × Nat) := #[]           -- output ordinal, value
  breaks  : Array Nat := #[]
  watches : Array Nat := #[]
deriving Inhabited, Repr

private def kv (toks : List String) : List (String × String) :=
  toks.filterMap fun t => match t.splitOn "=" with
    | [k, v] => some (k, v)
    | _ => none

/-- Parse a `bind v1` file against the design's ports. -/
def Bind.parse (io : IoMeta) (inW : Nat → Nat) (txt : String) : Except String Bind := do
  let lines := ((txt.splitOn "\n").map fun l => ((l.splitOn "#").headD "").trimAscii.toString).zipIdx
  let body := lines.filter (·.1 != "")
  match body with
  | [] => throw "empty bind file"
  | (hdr, _) :: rest =>
    unless hdr == "bind v1" do throw s!"line 1: expected `bind v1`, got `{hdr}`"
    let mut b : Bind := {}
    for (l, n) in rest do
      let at_ (m : String) : String := s!"line {n + 1}: {m}"
      let num (s : String) : Except String Nat :=
        match parseNum? s with | some v => .ok v | none => .error (at_ s!"`{s}` is not a number")
      let inp (s : String) : Except String Nat := (io.inputOrd s).mapError at_
      let out (s : String) : Except String Nat := (io.outputOrd s).mapError at_
      match (l.splitOn " ").filter (· ≠ "") with
      | ["reset", i, v, c] => b := { b with reset := some (← inp i, ← num v, ← num c) }
      | ["const", i, v] => b := { b with consts := b.consts.push (← inp i, ← num v) }
      | "bus" :: name :: fields =>
        let f := kv fields
        let get (k : String) : Except String String :=
          match f.lookup k with | some v => .ok v | none => .error (at_ s!"bus {name}: no `{k}=`")
        let opt (k : String) (g : String → Except String Nat) : Except String (Option Nat) :=
          match f.lookup k with | some v => (g v).map some | none => .ok none
        let rd ← inp (← get "rdata")
        let w := inW rd
        unless w > 0 && w % 8 == 0 do throw (at_ s!"bus {name}: rdata is {w} bits, not whole bytes")
        let lat ← match f.lookup "latency" with
          | none => pure Latency.zero
          | some s => match Latency.parse? s with
            | some l => pure l
            | none => throw (at_ s!"bad latency `{s}` (zero | fixed:N | random:SEED:MAX)")
        let bus : Bus :=
          { name, valid := ← out (← get "valid"), addr := ← out (← get "addr"),
            ready := ← inp (← get "ready"), rdata := rd, bytes := w / 8,
            wdata := ← opt "wdata" out, wstrb := ← opt "wstrb" out, we := ← opt "we" out,
            instr := ← opt "instr" out, latency := lat }
        if bus.wstrb.isSome && bus.we.isSome then throw (at_ s!"bus {name}: give wstrb= or we=, not both")
        if (bus.wstrb.isSome || bus.we.isSome) && bus.wdata.isNone then
          throw (at_ s!"bus {name}: a write needs wdata=")
        b := { b with buses := b.buses.push bus }
      | ["ram", a, s] => b := { b with regions := b.regions.push { base := ← num a, size := ← num s, rom := false } }
      | ["rom", a, s] => b := { b with regions := b.regions.push { base := ← num a, size := ← num s, rom := true } }
      | ["console", a] => b := { b with devices := b.devices.push (← num a, .console) }
      | ["exit", a] => b := { b with devices := b.devices.push (← num a, .exit) }
      | ["word", a, nm] => b := { b with devices := b.devices.push (← num a, .word nm) }
      | ["stop-output", o, v] => b := { b with stops := b.stops.push (← out o, ← num v) }
      | ["break", a] => b := { b with breaks := b.breaks.push (← num a) }
      | ["watch", a] => b := { b with watches := b.watches.push (← num a) }
      | _ => throw (at_ s!"unrecognised `{l}`")
    for i in [0:b.regions.size] do
      for j in [i+1:b.regions.size] do
        let r := b.regions[i]!; let q := b.regions[j]!
        if r.base < q.base + q.size && q.base < r.base + r.size then
          throw s!"regions at 0x{String.ofList (Nat.toDigits 16 r.base)} and 0x{String.ofList (Nat.toDigits 16 q.base)} overlap"
    return b

/-! ## The host state -/

/-- xorshift64: the latency draws. -/
def xorshift (x : UInt64) : UInt64 :=
  let x := x ^^^ (x <<< 13)
  let x := x ^^^ (x >>> 7)
  x ^^^ (x <<< 17)

structure Pending where
  arrival : Nat
  wait    : Nat
  addr    : Nat
  wdata   : Nat
  /-- bit `i` writes byte `i`; 0 for a read -/
  strb    : Nat
  instr   : Bool
deriving BEq, Repr, Inhabited

structure Stats where
  reads   : Nat := 0
  writes  : Nat := 0
  fetches : Nat := 0
  waits   : Nat := 0
deriving Inhabited, Repr, BEq

structure HostState where
  mem      : Array ByteArray
  pending  : Array (Option Pending)
  words    : Array (Nat × Nat) := #[]     -- device word registers written so far: address, value
  console  : ByteArray := .empty
  exitCode : Option Nat := none
  rng      : UInt64 := 0x2545F4914F6CDD1D
  stats    : Stats := {}
deriving Inhabited

def HostState.fresh (b : Bind) : HostState :=
  { mem := b.regions.map fun r => ByteArray.mk (Array.replicate r.size 0)
    pending := b.buses.map fun _ => none
    rng := match (b.buses.toList.findSome? fun bus => match bus.latency with
      | .random s _ => some s | _ => none) with
      | some s => (if s == 0 then 0x2545F4914F6CDD1D else s.toUInt64)
      | none => 0x2545F4914F6CDD1D }

/-- Copy an image into the host memory. -/
def HostState.load (b : Bind) (h : HostState) (img : Image) : Except String HostState := do
  let mut mem := h.mem
  for s in img.segments do
    for k in [0:s.memsz] do
      let a := s.addr + k
      let byte : UInt8 := if k < s.bytes.size then s.bytes.get! k else 0
      match (List.range b.regions.size).find? (fun i =>
          let r := b.regions[i]!; r.base ≤ a && a < r.base + r.size) with
      | some i => mem := mem.set! i ((mem[i]!).set! (a - b.regions[i]!.base) byte)
      | none => throw s!"image byte at 0x{String.ofList (Nat.toDigits 16 a)} is outside every ram/rom region"
  return { h with mem }

/-! ## Memory access -/

inductive Target where
  | region (i : Nat) (off : Nat)
  | device (k : DevKind)
  | none

/-- What an aligned word address names: a device, a region, or nothing. -/
def Bind.target (b : Bind) (wordAddr bytes : Nat) : Target :=
  match b.devices.find? (·.1 == wordAddr) with
  | some (_, k) => .device k
  | none =>
    match (List.range b.regions.size).find? (fun i =>
        let r := b.regions[i]!; r.base ≤ wordAddr && wordAddr + bytes ≤ r.base + r.size) with
    | some i => .region i (wordAddr - b.regions[i]!.base)
    | none => .none

def readLE (m : ByteArray) (o n : Nat) : Nat :=
  (List.range n).foldl (fun acc i => acc ||| (m.get! (o + i)).toNat <<< (8 * i)) 0

def writeStrobed (m : ByteArray) (o n data strb : Nat) : ByteArray := Id.run do
  let mut m := m
  for i in [0:n] do
    if (strb >>> i) % 2 == 1 then m := m.set! (o + i) ((data >>> (8 * i)) % 256).toUInt8
  return m

/-- The value a read of the word at `wordAddr` returns; `none` is a bus error. -/
def HostState.read (b : Bind) (h : HostState) (wordAddr bytes : Nat) : Option Nat :=
  match b.target wordAddr bytes with
  | .region i o => some (readLE h.mem[i]! o bytes)
  | .device (.word _) => some ((h.words.find? (·.1 == wordAddr)).map (·.2) |>.getD 0)
  | .device _ => some 0
  | .none => none

/-- Apply a completed write. Returns the host and a stop status. -/
def HostState.write (b : Bind) (h : HostState) (wordAddr bytes data strb : Nat) :
    HostState × Option String :=
  match b.target wordAddr bytes with
  | .region i o =>
    if b.regions[i]!.rom then (h, some "rom-write")
    else ({ h with mem := h.mem.set! i (writeStrobed h.mem[i]! o bytes data strb) }, none)
  | .device .console => ({ h with console := h.console.push (data % 256).toUInt8 }, none)
  | .device .exit => ({ h with exitCode := some data }, some "exit")
  | .device (.word _) =>
    ({ h with words := (h.words.filter (·.1 != wordAddr)).push (wordAddr, data) }, none)
  | .none => (h, some "bus-error")

/-- The wait of a new request on bus `bus`. -/
def HostState.drawWait (h : HostState) (bus : Bus) : Nat × HostState :=
  match bus.latency with
  | .zero => (0, h)
  | .fixed n => (n, h)
  | .random _ mx =>
    let r := xorshift h.rng
    ((r.toNat % (mx + 1)), { h with rng := r })

/-! ## The checkpoint section `cert_run host v1` -/

def extName : String := "cert_run host v1"

private def pushLine (b : ByteArray) (s : String) : ByteArray := b ++ (s ++ "\n").toUTF8

def HostState.encode (h : HostState) : ByteArray := Id.run do
  let mut b := pushLine .empty s!"regions {h.mem.size}"
  for m in h.mem do
    b := pushLine b s!"region {m.size}"
    b := b ++ m
    b := pushLine b ""
  b := pushLine b s!"pending {h.pending.size}"
  for p in h.pending do
    b := pushLine b (match p with
      | none => "-"
      | some p => s!"{p.arrival} {p.wait} {p.addr} {p.wdata} {p.strb} {if p.instr then 1 else 0}")
  b := pushLine b s!"words {h.words.size}"
  for (a, v) in h.words do b := pushLine b s!"{a} {v}"
  b := pushLine b s!"console {h.console.size}"
  b := b ++ h.console
  b := pushLine b ""
  b := pushLine b s!"exit {match h.exitCode with | some c => toString c | none => "-"}"
  b := pushLine b s!"rng {h.rng.toNat}"
  b := pushLine b s!"stats {h.stats.reads} {h.stats.writes} {h.stats.fetches} {h.stats.waits}"
  return b

/-- A cursor over the section's bytes. -/
structure Rd where
  b : ByteArray
  i : Nat

def Rd.line (r : Rd) : Except String (List String × Rd) := do
  let mut j := r.i
  while j < r.b.size && r.b.get! j != 10 do j := j + 1
  if j ≥ r.b.size then throw "host section: truncated"
  let s ← match String.fromUTF8? (r.b.extract r.i j) with
    | some s => pure s
    | none => throw "host section: not text"
  return ((s.splitOn " ").filter (· ≠ ""), { r with i := j + 1 })

def Rd.raw (r : Rd) (n : Nat) : Except String (ByteArray × Rd) := do
  if r.i + n + 1 > r.b.size then throw "host section: truncated payload"
  unless r.b.get! (r.i + n) == 10 do throw "host section: payload not followed by a newline"
  return (r.b.extract r.i (r.i + n), { r with i := r.i + n + 1 })

def HostState.decode (b : ByteArray) : Except String HostState := do
  let num (s : String) : Except String Nat :=
    match s.toNat? with | some v => .ok v | none => .error s!"host section: bad number `{s}`"
  let mut r : Rd := { b, i := 0 }
  let (l, r1) ← r.line; r := r1
  let nreg ← match l with | ["regions", n] => num n | _ => throw "host section: expected `regions`"
  let mut mem : Array ByteArray := #[]
  for _ in [0:nreg] do
    let (l, r1) ← r.line; r := r1
    let sz ← match l with | ["region", n] => num n | _ => throw "host section: expected `region`"
    let (m, r2) ← r.raw sz; r := r2
    mem := mem.push m
  let (l, r1) ← r.line; r := r1
  let npend ← match l with | ["pending", n] => num n | _ => throw "host section: expected `pending`"
  let mut pending : Array (Option Pending) := #[]
  for _ in [0:npend] do
    let (l, r1) ← r.line; r := r1
    match l with
    | ["-"] => pending := pending.push none
    | [a, w, ad, d, s, ins] =>
      let p : Pending := { arrival := (← num a), wait := (← num w), addr := (← num ad),
                           wdata := (← num d), strb := (← num s), instr := ins == "1" }
      pending := pending.push (some p)
    | _ => throw "host section: bad pending line"
  let (l, r1) ← r.line; r := r1
  let nw ← match l with | ["words", n] => num n | _ => throw "host section: expected `words`"
  let mut words : Array (Nat × Nat) := #[]
  for _ in [0:nw] do
    let (l, r1) ← r.line; r := r1
    match l with
    | [a, v] => words := words.push (← num a, ← num v)
    | _ => throw "host section: bad word line"
  let (l, r1) ← r.line; r := r1
  let nc ← match l with | ["console", n] => num n | _ => throw "host section: expected `console`"
  let (console, r2) ← r.raw nc; r := r2
  let (l, r1) ← r.line; r := r1
  let exitCode ← match l with
    | ["exit", "-"] => pure none
    | ["exit", c] => some <$> num c
    | _ => throw "host section: expected `exit`"
  let (l, r1) ← r.line; r := r1
  let rng ← match l with | ["rng", n] => num n | _ => throw "host section: expected `rng`"
  let (l, r1) ← r.line; r := r1
  let stats ← match l with
    | ["stats", a, c, d, e] =>
      pure ({ reads := (← num a), writes := (← num c), fetches := (← num d), waits := (← num e) } : Stats)
    | _ => throw "host section: expected `stats`"
  unless r1.i == b.size do throw "host section: trailing bytes"
  return { mem, pending, words, console, exitCode, rng := rng.toUInt64, stats }

/-- The host state fits the bind file: one memory per region of its size, one slot per bus. -/
def HostState.fits (b : Bind) (h : HostState) : Bool :=
  h.mem.size == b.regions.size && (List.range h.mem.size).all (fun i => h.mem[i]!.size == b.regions[i]!.size)
    && h.pending.size == b.buses.size

end Run
end HDLProof
