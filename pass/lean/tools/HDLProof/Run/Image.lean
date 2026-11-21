/-
# Program images

Every input format becomes one `Image`: segments (a byte address, the file's bytes, and the
size in memory; the bytes past the file's are zero) and an entry address.

* ELF: 32- or 64-bit, little- or big-endian, any machine. The `PT_LOAD` segments are kept,
  placed at their physical address (`p_paddr`) or, with `vaddr`, at their virtual one. Each
  rejection names its rule. A dynamic or interpreted executable is refused.
* `.bin`: the raw bytes at a given address.
* `.hex`: `$readmemh` words: hex numbers separated by white space, `@<hex>` moves to a word
  address, `//` and `/* */` comments are skipped. Each word is `wordBytes` bytes, little-endian,
  at byte address `wordBytes * word address`.

Placement (`toWords`) turns an image into the words of a memory `dw` bits wide: byte `a` lands
in word `(a - base) / (dw / 8)`, little-endian within the word.
-/
import Std.Data.HashMap

namespace HDLProof
namespace Run

structure Segment where
  addr  : Nat
  bytes : ByteArray
  /-- bytes the segment occupies in memory; the last `memsz - bytes.size` are zero (BSS) -/
  memsz : Nat
deriving Inhabited

structure Image where
  segments : Array Segment
  entry    : Nat
deriving Inhabited

/-! ## Fields -/

def fieldLE (b : ByteArray) (o n : Nat) : Nat :=
  (List.range n).foldl (fun acc i => acc ||| (b.get! (o + i)).toNat <<< (8 * i)) 0

def fieldBE (b : ByteArray) (o n : Nat) : Nat :=
  (List.range n).foldl (fun acc i => (acc <<< 8) ||| (b.get! (o + i)).toNat) 0

/-! ## ELF -/

/-- Parse an ELF executable. `vaddr` places segments at `p_vaddr` instead of `p_paddr`. -/
def parseElf (b : ByteArray) (vaddr : Bool := false) : Except String Image := do
  let rej (rule : String) : Except String Image := .error s!"elf: {rule}"
  if b.size < 16 then return ← rej "truncated identification"
  unless b.get! 0 == 0x7f && b.get! 1 == 0x45 && b.get! 2 == 0x4c && b.get! 3 == 0x46 do
    return ← rej "bad magic (not an ELF file)"
  let cls := b.get! 4
  unless cls == 1 || cls == 2 do return ← rej s!"class {cls} is neither ELFCLASS32 nor ELFCLASS64"
  let be ← match b.get! 5 with
    | 1 => pure false
    | 2 => pure true
    | d => throw s!"elf: data encoding {d} is neither little- nor big-endian"
  unless b.get! 6 == 1 do return ← rej "bad identification version"
  let f (o n : Nat) : Nat := if be then fieldBE b o n else fieldLE b o n
  let is64 := cls == 2
  let hdr := if is64 then 64 else 52
  if b.size < hdr then return ← rej "truncated header"
  let etype := f 16 2
  if etype == 3 then return ← rej "type ET_DYN: a position-independent executable needs a loader"
  unless etype == 2 do return ← rej s!"type {etype} is not ET_EXEC"
  let entry := if is64 then f 24 8 else f 24 4
  let phoff := if is64 then f 32 8 else f 28 4
  let phentsize := if is64 then f 54 2 else f 42 2
  let phnum := if is64 then f 56 2 else f 44 2
  let want := if is64 then 56 else 32
  unless phentsize == want do return ← rej s!"program header size {phentsize}, expected {want}"
  if phoff + phnum * phentsize > b.size then return ← rej "truncated program headers"
  let mut segs : Array Segment := #[]
  for k in [0:phnum] do
    let o := phoff + k * phentsize
    let ptype := f o 4
    if ptype == 2 then return ← rej "PT_DYNAMIC present"
    if ptype == 3 then return ← rej "PT_INTERP present"
    if ptype != 1 then continue
    let (off, va, pa, filesz, memsz) :=
      if is64 then (f (o + 8) 8, f (o + 16) 8, f (o + 24) 8, f (o + 32) 8, f (o + 40) 8)
      else (f (o + 4) 4, f (o + 8) 4, f (o + 12) 4, f (o + 16) 4, f (o + 20) 4)
    if filesz > memsz then return ← rej s!"segment {k}: p_filesz > p_memsz"
    if off + filesz > b.size then return ← rej s!"segment {k}: truncated file"
    if memsz == 0 then continue
    segs := segs.push { addr := if vaddr then va else pa, bytes := b.extract off (off + filesz), memsz }
  for i in [0:segs.size] do
    for j in [i+1:segs.size] do
      let a := segs[i]!; let c := segs[j]!
      if a.addr < c.addr + c.memsz && c.addr < a.addr + a.memsz then
        return ← rej s!"segments {i} and {j} overlap"
  return { segments := segs, entry }

/-! ## Raw binary and `$readmemh` words -/

def imageOfBin (b : ByteArray) (addr : Nat) : Image :=
  { segments := #[{ addr, bytes := b, memsz := b.size }], entry := addr }

def hexVal? (s : String) : Option Nat :=
  if s.isEmpty then none else
  s.foldl (fun acc c => acc.bind fun a =>
    if '0' ≤ c ∧ c ≤ '9' then some (a * 16 + (c.toNat - 48))
    else if 'a' ≤ c ∧ c ≤ 'f' then some (a * 16 + (c.toNat - 87))
    else if 'A' ≤ c ∧ c ≤ 'F' then some (a * 16 + (c.toNat - 55))
    else if c == '_' then some a
    else none) (some 0)

/-- Drop `//` line comments and `/* */` block comments. -/
def stripComments (txt : String) : String := Id.run do
  let mut out := ""
  let mut i := 0
  let cs := txt.toList.toArray
  let mut block := false
  while i < cs.size do
    let c := cs[i]!
    let n := cs[i+1]?.getD ' '
    if block then
      if c == '*' && n == '/' then block := false; i := i + 2 else i := i + 1
    else if c == '/' && n == '*' then block := true; i := i + 2
    else if c == '/' && n == '/' then
      while i < cs.size && cs[i]! != '\n' do i := i + 1
    else out := out.push c; i := i + 1
  return out

/-- `$readmemh` words: `(word address, value)` in file order. -/
def parseHexWords (txt : String) : Except String (Array (Nat × Nat)) := do
  let mut words : Array (Nat × Nat) := #[]
  let mut cursor := 0
  let norm := (stripComments txt).map fun c => if c == '\n' || c == '\t' || c == '\r' then ' ' else c
  let toks := (norm.splitOn " ").filter (· ≠ "")
  for t in toks do
    if t.startsWith "@" then
      match hexVal? (t.drop 1).toString with
      | some a => cursor := a
      | none => throw s!"hex: bad address `{t}`"
    else
      match hexVal? t with
      | some v => words := words.push (cursor, v); cursor := cursor + 1
      | none => throw s!"hex: bad word `{t}`"
  return words

/-- `$readmemh` words of `wordBytes` bytes as an image, little-endian within each word. -/
def imageOfHex (words : Array (Nat × Nat)) (wordBytes : Nat) : Image :=
  let segs := words.map fun (wa, v) =>
    ({ addr := wordBytes * wa
       bytes := ByteArray.mk ((Array.range wordBytes).map fun i => ((v >>> (8 * i)) % 256).toUInt8)
       memsz := wordBytes } : Segment)
  { segments := segs, entry := 0 }

/-- Read an image by extension: `.elf` (or no extension), `.bin` (needs `addr`), `.hex`. -/
def loadImage (path : String) (addr : Option Nat) (wordBytes : Nat) (vaddr : Bool) : IO Image := do
  let p := System.FilePath.mk path
  let b ← IO.FS.readBinFile p
  let r : Except String Image :=
    match p.extension with
    | some "bin" =>
        match addr with
        | some a => .ok (imageOfBin b a)
        | none => .error "bin: give the load address (`@<addr>`)"
    | some "hex" =>
        match String.fromUTF8? b with
        | some t => (parseHexWords t).map fun ws => imageOfHex ws wordBytes
        | none => .error "hex: not text"
    | _ => parseElf b vaddr
  match r with
  | .ok img => return img
  | .error e => throw (IO.userError s!"{path}: {e}")

/-! ## Placement into a memory of words -/

/-- The words of a memory `dw` bits wide that the image sets, by word index: byte `a` lands in
word `(a - base) / (dw / 8)`. A byte below `base` or past `depth` words is an error. `dw` must be
a positive multiple of 8. -/
def toWords (img : Image) (base dw depth : Nat) : Except String (Array (Nat × Nat)) := do
  unless dw > 0 && dw % 8 == 0 do throw s!"a memory {dw} bits wide does not hold whole bytes"
  let wb := dw / 8
  let mut tbl : Std.HashMap Nat Nat := {}
  for s in img.segments do
    for k in [0:s.memsz] do
      let a := s.addr + k
      if a < base then throw s!"image byte at 0x{String.ofList (Nat.toDigits 16 a)} is below the base"
      let w := (a - base) / wb
      if w ≥ depth then
        throw s!"image byte at 0x{String.ofList (Nat.toDigits 16 a)} lands in word {w}, past the memory's {depth} words"
      let byte := if k < s.bytes.size then (s.bytes.get! k).toNat else 0
      let lane := (a - base) % wb
      let old := tbl.getD w 0
      let cleared := old &&& ((2 ^ dw - 1) ^^^ (255 <<< (8 * lane)))
      tbl := tbl.insert w (cleared ||| (byte <<< (8 * lane)))
  return (tbl.toArray.qsort (·.1 < ·.1))

end Run
end HDLProof
