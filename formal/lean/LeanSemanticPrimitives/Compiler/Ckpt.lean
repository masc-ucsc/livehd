/-
# `lhdcert-ckpt v1` — a simulator state, saved and restored

A checkpoint is one design's state at one step: every flop, and every memory word a step
can read (the words below the memory's `depth`; `Projection/MemAddrSound` proves no step
addresses another). It carries no route: the IR, VC and FP simulators write the same bytes
for the same state, and each can continue from a checkpoint any of them wrote.

```
lhdcert-ckpt v1
design <16 hex>          fnv1a64 of the certificate's DCERT4 rendering (`renderCert`)
cycle <decimal>          steps applied since power-on
flops <n>
<width> <hex>            n lines, by flop ordinal
mems <m>
mem <aw> <dw> <depth> <default hex> <k>
<addr hex> <value hex>   k lines: addresses strictly increasing and below depth,
                         no value equal to the default
exts <e>
ext <name bytes> <name> <payload bytes>
<payload>                taken by count; any tool copies an ext it does not know unchanged
digest <16 hex>          fnv1a64 of every byte before this line
```

Values are unsigned, at the declared width. A memory's default is its value at every
address it does not list, at or past `depth` and below 0 included; no step ever writes those.
The design hash is taken over `renderCert D`, not over a file, so it is a property of the record:
a DCERT3 file converted by `dcert_upgrade` (the same record) keeps every checkpoint taken against it.

`validate` refuses a checkpoint that does not fit the certificate, with one message per
rule. Two rules are about the certificate alone: every next image of a memory is a version of
that memory (`imgRootError`), and every word a step stores into a memory has the memory's width
(`wordError`). Without them the saved words would not determine the next state.
`restore` builds a `RuntimeState`; `save` reads one back (at the words below `depth`).
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Compiler.MemAddr
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Compiler
namespace Ckpt

open CertIO

/-! ## The record -/

/-- One memory: its shape, the value of every word it does not list (`dflt`), and the listed
words `(address, value)`. -/
structure MemCk where
  aw    : Nat
  dw    : Nat
  depth : Nat
  dflt  : Nat
  words : Array (Nat × Nat)
deriving Repr, Inhabited, DecidableEq

/-- A named section whose payload this module does not interpret. -/
structure Ext where
  name  : String
  bytes : List UInt8
deriving Repr, Inhabited, DecidableEq

structure Ckpt where
  /-- fnv1a64 of `renderCert D` -/
  design : Nat
  cycle  : Nat
  /-- `(width, value)` by flop ordinal -/
  flops  : Array (Nat × Nat)
  mems   : Array MemCk
  exts   : Array Ext := #[]
deriving Repr, Inhabited, DecidableEq

/-! ## Hashes -/

def fnvOffset : UInt64 := 0xcbf29ce484222325
def fnvPrime  : UInt64 := 0x100000001b3

/-- FNV-1a, 64 bits. -/
def fnv1a64 (b : ByteArray) : UInt64 :=
  b.foldl (fun h x => (h ^^^ x.toUInt64) * fnvPrime) fnvOffset

/-- The design hash a checkpoint carries: fnv1a64 of the certificate's DCERT4 rendering. -/
def designHash (D : DesignCert) : Nat := (fnv1a64 (renderCert D)).toNat

/-! ## Rendering -/

/-- Lowercase hex, no prefix, `0` for zero. -/
def hexStr (n : Nat) : String := String.ofList (Nat.toDigits 16 n)

/-- Exactly 16 hex digits (a 64-bit hash). -/
def hex16 (n : Nat) : String :=
  let s := hexStr (n % 2 ^ 64)
  String.ofList (List.replicate (16 - s.length) '0') ++ s

private def line (acc : ByteArray) (s : String) : ByteArray := acc ++ (s ++ "\n").toUTF8

/-- Every byte before the digest line. -/
def renderBody (c : Ckpt) : ByteArray := Id.run do
  let mut b := line ByteArray.empty "lhdcert-ckpt v1"
  b := line b s!"design {hex16 c.design}"
  b := line b s!"cycle {c.cycle}"
  b := line b s!"flops {c.flops.size}"
  for (w, v) in c.flops do
    b := line b s!"{w} {hexStr v}"
  b := line b s!"mems {c.mems.size}"
  for m in c.mems do
    b := line b s!"mem {m.aw} {m.dw} {m.depth} {hexStr m.dflt} {m.words.size}"
    for (a, v) in m.words do
      b := line b s!"{hexStr a} {hexStr v}"
  b := line b s!"exts {c.exts.size}"
  for e in c.exts do
    b := line b s!"ext {e.name.toUTF8.size} {e.name} {e.bytes.length}"
    b := b ++ ⟨e.bytes.toArray⟩
    b := line b ""
  return b

/-- The checkpoint as bytes: the body, then its digest. -/
def render (c : Ckpt) : ByteArray :=
  let body := renderBody c
  line body s!"digest {hex16 (fnv1a64 body).toNat}"

/-! ## Parsing -/

private def err {α : Type} (i : Nat) (msg : String) : Except String α :=
  .error s!"ckpt: byte {i}: {msg}"

/-- The keyword `w`, after optional whitespace, followed by whitespace or the end. -/
def kw (b : ByteArray) (i0 : Nat) (w : String) : Except String Nat :=
  let i := skipWs b i0
  let wb := w.toUTF8
  if i + wb.size ≤ b.size && (List.range wb.size).all (fun k => b[i + k]! == wb[k]!)
     && (i + wb.size == b.size || isWs b[i + wb.size]!) then
    .ok (i + wb.size)
  else if i ≥ b.size then .error "ckpt: truncated"
  else err i s!"expected '{w}'"

/-- A decimal number. -/
def dec (b : ByteArray) (i : Nat) : Except String (Nat × Nat) :=
  match readNat b i with
  | some r => .ok r
  | none   => if skipWs b i ≥ b.size then .error "ckpt: truncated" else err (skipWs b i) "bad number"

@[inline] def hexDigit (c : UInt8) : Option Nat :=
  if 48 ≤ c && c ≤ 57 then some (c.toNat - 48)
  else if 97 ≤ c && c ≤ 102 then some (c.toNat - 87)
  else if 65 ≤ c && c ≤ 70 then some (c.toNat - 55)
  else none

def readHexAux (b : ByteArray) (i acc : Nat) : Nat × Nat :=
  if h : i < b.size then
    match hexDigit b[i] with
    | some d => readHexAux b (i + 1) (acc * 16 + d)
    | none   => (acc, i)
  else (acc, i)
termination_by b.size - i
decreasing_by omega

/-- A hex number (no prefix). -/
def hex (b : ByteArray) (i0 : Nat) : Except String (Nat × Nat) :=
  let i := skipWs b i0
  let (v, j) := readHexAux b i 0
  if j == i then (if i ≥ b.size then .error "ckpt: truncated" else err i "bad hex number")
  else if j < b.size && !isWs b[j]! then err j "bad hex number"
  else .ok (v, j)

/-- The first line, for the "not a checkpoint" message. -/
def firstLine (b : ByteArray) : String :=
  let n := (List.range (min b.size 80)).find? (fun k => b[k]! == 10) |>.getD (min b.size 80)
  (String.fromUTF8? (b.extract 0 n)).getD "<not UTF-8>"

def magic : String := "lhdcert-ckpt v1\n"

/-- Parse a checkpoint, checking its digest. With `checkDigest := false` (`lhd_ckpt seal` and
`normalize`, for hand-edited files) the digest is not compared, and the digest line may be
missing. -/
def parse (b : ByteArray) (checkDigest : Bool := true) : Except String Ckpt := do
  let mb := magic.toUTF8
  unless mb.size ≤ b.size && (List.range mb.size).all (fun k => b[k]! == mb[k]!) do
    throw s!"ckpt: not an lhdcert-ckpt v1 file (first line is '{firstLine b}')"
  let mut i := mb.size
  i ← kw b i "design"
  let (design, i1) ← hex b i
  i ← kw b i1 "cycle"
  let (cycle, i2) ← dec b i
  i ← kw b i2 "flops"
  let (nf, i3) ← dec b i
  i := i3
  let mut flops : Array (Nat × Nat) := #[]
  for _ in [0:nf] do
    let (w, j) ← dec b i
    let (v, j) ← hex b j
    flops := flops.push (w, v); i := j
  i ← kw b i "mems"
  let (nm, i4) ← dec b i
  i := i4
  let mut mems : Array MemCk := #[]
  for _ in [0:nm] do
    i ← kw b i "mem"
    let (aw, j) ← dec b i
    let (dw, j) ← dec b j
    let (depth, j) ← dec b j
    let (dflt, j) ← hex b j
    let (k, j) ← dec b j
    i := j
    let mut words : Array (Nat × Nat) := #[]
    for _ in [0:k] do
      let (a, j) ← hex b i
      let (v, j) ← hex b j
      words := words.push (a, v); i := j
    mems := mems.push { aw, dw, depth, dflt, words }
  i ← kw b i "exts"
  let (ne, i5) ← dec b i
  i := i5
  let mut exts : Array Ext := #[]
  for _ in [0:ne] do
    i ← kw b i "ext"
    let (nl, j) ← dec b i
    unless j < b.size && b[j]! == 32 do throw (s!"ckpt: byte {j}: expected one space before the ext name")
    let s := j + 1
    unless s + nl ≤ b.size do throw "ckpt: truncated"
    let name ← match String.fromUTF8? (b.extract s (s + nl)) with
      | some n => pure n
      | none   => err s "ext name is not UTF-8"
    let (nb, j) ← dec b (s + nl)
    unless j < b.size && b[j]! == 10 do throw (s!"ckpt: byte {j}: expected a newline before the ext payload")
    let p := j + 1
    unless p + nb ≤ b.size do throw "ckpt: truncated"
    exts := exts.push { name, bytes := (b.extract p (p + nb)).toList }
    i := p + nb
  let dpos := skipWs b i
  if !checkDigest && dpos == b.size then
    return { design, cycle, flops, mems, exts }
  i ← kw b i "digest"
  let (dg, i6) ← hex b i
  unless skipWs b i6 == b.size do throw "ckpt: trailing bytes after the digest line"
  let h := (fnv1a64 (b.extract 0 dpos)).toNat
  if checkDigest && dg != h then
    throw s!"ckpt: digest mismatch: file says {hex16 dg}, contents hash to {hex16 h}"
  return { design, cycle, flops, mems, exts }

/-! ## Checking a checkpoint against a certificate -/

/-- Each memory's next images (`nextImg`, its commit-table images, its async-reset image)
are versions of that same memory: following writes and loads back from each reaches the
memory's own `memImg` source. Without this the stored state does not determine the step. -/
def imgRootError (D : DesignCert) : Option String :=
  let fuel := D.nodes.size + 1
  (List.range D.memories.size).findSome? fun k =>
    let md := D.memories[k]!
    let slots := [md.nextImg] ++ md.commits.toList.map (·.nextImg) ++ md.asyncImg.toList
    slots.findSome? fun s =>
      if MemAddr.rootOf D fuel s == some (some k) then none
      else some s!"ckpt: memory {k}'s next image (slot {s}) is not a version of memory {k}; \
                   a checkpoint cannot determine this design's future"

/-- The width of the words node `c` stores into the image at its operand 0: a plain write's
data width, a byte-enable write's or a load's node width; `none` for any other node. -/
def storedWidth (D : DesignCert) (c : DenseNodeCert) : Option Nat :=
  match c.op with
  | .Op_MemWrite =>
    match c.deps[2]? with
    | some d => match Direct.slotKind D d with
      | some (.bv w) => some w
      | _            => none   -- not a scalar: the direct checker refuses it
    | none => none
  | .Op_MemWriteBE _ | .Op_MemLoad _ => some c.width
  | _ => none

/-- A node that may store a word wider or narrower than its memory's `dw`. A checkpoint keeps
each word at `dw` bits, which is exact only when every word a step stores has that width. -/
def wordError (D : DesignCert) : Option String :=
  let fuel := D.nodes.size + 1
  (List.range D.nodes.size).findSome? fun j =>
    let c := D.nodes[j]!
    match storedWidth D c, c.deps[0]? with
    | some w, some m =>
      match MemAddr.rootOf D fuel m with
      | some (some k) =>
        match D.memories[k]? with
        | some md =>
          if w == md.dw then none
          else some s!"ckpt: node {D.sources.size + j} stores {w}-bit words into memory {k}, \
                       whose words are {md.dw} bits; a checkpoint keeps {md.dw} bits"
        | none => none
      | _ => none
    | _, _ => none

/-- Why the listed words `ws` of memory `k` (`dw` bits, `depth` words, default `dflt`) are
not valid, `prev` being the address listed before them, or `none`. -/
def wordsError (k dw depth dflt : Nat) : Option Nat → List (Nat × Nat) → Option String
  | _, [] => none
  | prev, (a, v) :: ws =>
    if a ≥ depth then
      some s!"ckpt: memory {k}: word at 0x{hexStr a} is past the memory's depth {depth}"
    else if v ≥ 2 ^ dw then
      some s!"ckpt: memory {k}: word at 0x{hexStr a} value 0x{hexStr v} does not fit in {dw} bits"
    else if let some p := prev.filter (a ≤ ·) then
      some s!"ckpt: memory {k}: addresses not strictly increasing at 0x{hexStr a} \
              (after 0x{hexStr p})"
    else if v == dflt then
      some s!"ckpt: memory {k}: word at 0x{hexStr a} repeats the default \
              (run `lhd_ckpt normalize`)"
    else wordsError k dw depth dflt (some a) ws

/-- Why `m` is not a valid image of memory `k` declared `md`, or `none`. -/
def memError (k : Nat) (md : MemoryDesc) (m : MemCk) : Option String :=
  if m.aw != md.aw || m.dw != md.dw || m.depth != md.depth then
    some s!"ckpt: memory {k}: aw={m.aw} dw={m.dw} depth={m.depth}, the certificate \
            declares aw={md.aw} dw={md.dw} depth={md.depth}"
  else if m.dflt ≥ 2 ^ m.dw then
    some s!"ckpt: memory {k}: default 0x{hexStr m.dflt} does not fit in {m.dw} bits"
  else wordsError k m.dw m.depth m.dflt none m.words.toList

/-- Why flop `k`'s entry `(w, v)` does not fit flop `f`, or `none`. -/
def flopError (k : Nat) (f : FlopDesc) (wv : Nat × Nat) : Option String :=
  if wv.1 != f.width then
    some s!"ckpt: flop {k}: width {wv.1}, the certificate declares {f.width}"
  else if wv.2 ≥ 2 ^ wv.1 then
    some s!"ckpt: flop {k}: value 0x{hexStr wv.2} does not fit in {wv.1} bits"
  else none

/-- Why `c` does not fit the certificate `D` whose design hash is `hash`, or `none`. The rules
in order: design hash, flop count, each flop, memory count, each memory, then the two rules
about the certificate alone (`imgRootError`, `wordError`). -/
def validate (D : DesignCert) (hash : Nat) (c : Ckpt) : Option String :=
  if c.design != hash then
    some s!"ckpt: design hash {hex16 c.design} does not match the certificate's \
            {hex16 hash} (fnv1a64 of its DCERT4 rendering)"
  else if c.flops.size != D.flops.size then
    some s!"ckpt: {c.flops.size} flops, the certificate declares {D.flops.size}"
  else if let some e := (List.range c.flops.size).findSome? (fun k =>
      flopError k D.flops[k]! c.flops[k]!) then some e
  else if c.mems.size != D.memories.size then
    some s!"ckpt: {c.mems.size} memories, the certificate declares {D.memories.size}"
  else if let some e := (List.range c.mems.size).findSome? (fun k =>
      memError k D.memories[k]! c.mems[k]!) then some e
  else if let some e := imgRootError D then some e
  else wordError D

/-! ## Restore and save -/

/-- The memory's words below `depth` as a dense table, built once (not inside the closure
`memImage` returns, which would rebuild it on every read). A later entry for the same address
wins; an entry at or past `depth` is ignored. -/
@[noinline] def memTable (m : MemCk) : Array Nat :=
  m.words.toList.foldl (fun t (a, v) => t.setIfInBounds a v) (Array.replicate m.depth m.dflt)

/-- The memory image a checkpoint describes: listed words below `depth`, `dflt` elsewhere. -/
def memImage (m : MemCk) : Int → BV :=
  let t := memTable m
  fun x => mk_bv m.dw (((if 0 ≤ x then t[x.toNat]? else none).getD m.dflt : Nat) : Int)

/-- The state a checkpoint describes. -/
def restore (c : Ckpt) : RuntimeState :=
  { flops := c.flops.map fun (w, v) => mk_bv w (v : Int)
    mems  := c.mems.map memImage }

/-! ### Building each table once

Lean compiles a definition whose result is a function, such as `memImage`, with the function's
argument added to its parameters, and the `let` before the `fun` moves into the body. A
partial application `memImage m` then rebuilds `memTable m` on every read. `restoreTabled`
first builds every table as data (`Tabled`), then makes each image from its built table, so
a read is one array lookup. The `@[csimp]` lemma makes every caller of `restore` run it; the
two are equal, so no result changes. -/

/-- `memImage` over a table that is already built. -/
def tableImage (dw dflt : Nat) (t : Array Nat) : Int → BV :=
  fun x => mk_bv dw (((if 0 ≤ x then t[x.toNat]? else none).getD dflt : Nat) : Int)

theorem memImage_eq_tableImage (m : MemCk) : memImage m = tableImage m.dw m.dflt (memTable m) := rfl

/-- A checkpoint memory with its dense table. -/
structure Tabled where
  m : MemCk
  t : Array Nat

/-- `restore`, with each memory's table built once. -/
def restoreTabled (c : Ckpt) : RuntimeState :=
  { flops := c.flops.map fun (w, v) => mk_bv w (v : Int)
    mems  := (c.mems.map fun m => ({ m, t := memTable m } : Tabled)).map
      fun p => tableImage p.m.dw p.m.dflt p.t }

@[csimp] theorem restore_eq_tabled : @restore = @restoreTabled := by
  funext c
  simp only [restore, restoreTabled, Array.map_map]
  rfl

/-- Word `a` of image `f`, unsigned at `dw` bits. -/
def wordAt (dw : Nat) (f : Int → BV) (a : Int) : Nat := (bv_uint (f a)).toNat % 2 ^ dw

/-- Memory `md`'s image `f` as a checkpoint memory: the default is the word at -1 (no step
writes a negative address), and the listed words are those below `depth` that differ from it. -/
def saveMem (md : MemoryDesc) (f : Int → BV) : MemCk :=
  let dflt := wordAt md.dw f (-1)
  let words := (List.range md.depth).filterMap fun a =>
    let v := wordAt md.dw f (Int.ofNat a)
    if v == dflt then none else some (a, v)
  { aw := md.aw, dw := md.dw, depth := md.depth, dflt, words := words.toArray }

/-- The checkpoint of state `s` of `D` (design hash `hash`) after `cycle` steps. Each flop is
read at its declared width, as its source reads it (`sourceValue` resizes). -/
def save (D : DesignCert) (hash cycle : Nat) (exts : Array Ext) (s : RuntimeState) : Ckpt :=
  { design := hash, cycle, exts
    flops := D.flops.mapIdx fun k f =>
      (f.width, (bv_uint (s.flops[k]?.getD (mk_bv f.width 0))).toNat % 2 ^ f.width)
    mems := D.memories.mapIdx fun k md =>
      saveMem md (s.mems[k]?.getD fun _ => mk_bv md.dw 0) }

/-! ## Normal form -/

/-- Sort words by address (a later entry for the same address wins), drop words past
`depth` and words equal to the default. For hand-edited checkpoints. -/
def normalizeMem (m : MemCk) : MemCk :=
  let t := memTable m
  let words := (List.range t.size).filterMap fun a =>
    let v := t[a]?.getD m.dflt
    if v == m.dflt then none else some (a, v)
  { m with words := words.toArray }

def normalize (c : Ckpt) : Ckpt := { c with mems := c.mems.map normalizeMem }

/-- The first difference between two checkpoints of the same design, or `none`. Memories are
compared word by word below `depth` and by default. -/
def diff (a b : Ckpt) : Option String := Id.run do
  if a.design != b.design then
    return some s!"design {hex16 a.design} vs {hex16 b.design}"
  if a.flops.size != b.flops.size then
    return some s!"{a.flops.size} vs {b.flops.size} flops"
  for k in [0:a.flops.size] do
    if a.flops[k]! != b.flops[k]! then
      return some s!"flop {k}: {a.flops[k]!.1}'h{hexStr a.flops[k]!.2} vs \
                     {b.flops[k]!.1}'h{hexStr b.flops[k]!.2}"
  if a.mems.size != b.mems.size then
    return some s!"{a.mems.size} vs {b.mems.size} memories"
  for k in [0:a.mems.size] do
    let ma := normalizeMem a.mems[k]!
    let mb := normalizeMem b.mems[k]!
    if (ma.aw, ma.dw, ma.depth) != (mb.aw, mb.dw, mb.depth) then
      return some s!"memory {k}: shape differs"
    if ma.dflt != mb.dflt then
      return some s!"memory {k}: default 0x{hexStr ma.dflt} vs 0x{hexStr mb.dflt}"
    let ta := memTable ma
    let tb := memTable mb
    for x in [0:ta.size] do
      if ta[x]! != tb[x]! then
        return some s!"memory {k} word 0x{hexStr x}: 0x{hexStr ta[x]!} vs 0x{hexStr tb[x]!}"
  return none

/-! ## Other state formats -/

/-- The same state as an lgraph-sim `--state` file (`Compiler/DirectSim.lean`,
`parseStateFile`): decimal values, every flop, every memory's default and listed words. -/
def toStateText (c : Ckpt) : String := Id.run do
  let mut out := s!"# from an lhdcert-ckpt v1 checkpoint at cycle {c.cycle}\n"
  for k in [0:c.flops.size] do
    out := out ++ s!"flop {k} {c.flops[k]!.2}\n"
  for k in [0:c.mems.size] do
    let m := c.mems[k]!
    out := out ++ s!"mem {k} default {m.dflt}\n"
    for (a, v) in m.words do
      out := out ++ s!"mem {k} {a} {v}\n"
  return out

end Ckpt
end Compiler
