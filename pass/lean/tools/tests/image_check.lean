/-
Program images (`HDLProof/Run/Image`): the ELF reader on both classes and both byte orders, its
refusals, `$readmemh` words with `@<addr>` and comments, and the placement into a memory.

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/image_check.lean)

The ELF files are built here, byte by byte, from the ELF specification's header layouts (no
toolchain): one `PT_LOAD` segment of 8 file bytes (and 4 more in memory, zero) at `p_paddr`
0x1000, `p_vaddr` 0x80001000, entry 0x1004, in each of ELFCLASS32/ELFCLASS64 and
little/big-endian. Each must give that one segment at 0x1000 (0x80001000 with `vaddr`). The
refusals: a `PT_INTERP` header, class byte 3, data byte 3, `ET_DYN`, a truncated file, bad magic.
`.hex`: `@10 /* c */ 11 // x` then `22` is words 16 and 17. Placement: 4 bytes at base + 4 into
a 32-bit memory is word 1, little-endian; bytes below the base or past the depth are refused.

A self-check: one case with a wrong expectation must be reported as a failure by this file's own
comparison (`expect`), or the comparison proves nothing.

Prints `PASS image_check: <n> cases` or one `FAIL` line per case that went wrong.
-/
import HDLProof.Run.Image

open HDLProof.Run

/-- `n` bytes of `v`, little- or big-endian. -/
def field (be : Bool) (n v : Nat) : List UInt8 :=
  let le := (List.range n).map fun i => ((v >>> (8 * i)) % 256).toUInt8
  if be then le.reverse else le

/-- An executable with one program header of type `ptype` and, after the headers, the 8 bytes
`payload`: the header (52 or 64 bytes), the program header (32 or 56), the payload. -/
def mkElf (is64 be : Bool) (ptype : Nat := 1) (cls : Option Nat := none) (data : Option Nat := none)
    (etype : Nat := 2) : ByteArray :=
  let f := field be
  let ehsize := if is64 then 64 else 52
  let phsize := if is64 then 56 else 32
  let off := ehsize + phsize
  let ident : List UInt8 := [0x7f, 0x45, 0x4c, 0x46, (cls.getD (if is64 then 2 else 1)).toUInt8,
    (data.getD (if be then 2 else 1)).toUInt8, 1, 0] ++ List.replicate 8 0
  let ehdr :=
    if is64 then
      ident ++ f 2 etype ++ f 2 243 ++ f 4 1 ++ f 8 0x1004 ++ f 8 ehsize ++ f 8 0 ++ f 4 0 ++
        f 2 ehsize ++ f 2 phsize ++ f 2 1 ++ f 2 0 ++ f 2 0 ++ f 2 0
    else
      ident ++ f 2 etype ++ f 2 243 ++ f 4 1 ++ f 4 0x1004 ++ f 4 ehsize ++ f 4 0 ++ f 4 0 ++
        f 2 ehsize ++ f 2 phsize ++ f 2 1 ++ f 2 0 ++ f 2 0 ++ f 2 0
  let phdr :=
    if is64 then
      f 4 ptype ++ f 4 5 ++ f 8 off ++ f 8 0x80001000 ++ f 8 0x1000 ++ f 8 8 ++ f 8 12 ++ f 8 4
    else
      f 4 ptype ++ f 4 off ++ f 4 0x80001000 ++ f 4 0x1000 ++ f 4 8 ++ f 4 12 ++ f 4 5 ++ f 4 4
  ⟨(ehdr ++ phdr ++ [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88]).toArray⟩

/-- The one segment, as (address, file bytes, size in memory), and the entry; or the refusal. -/
def summary (r : Except String Image) : String :=
  match r with
  | .error e => s!"error {e}"
  | .ok img =>
    let segs := img.segments.toList.map fun s => s!"{s.addr} {s.bytes.toList} {s.memsz}"
    s!"segments {segs} entry {img.entry}"

def segAt (a : Nat) : String := s!"segments [{a} [17, 34, 51, 68, 85, 102, 119, 136] 12] entry 4100"

def startsWith (s p : String) : Bool := s.startsWith p

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  let expect (name : String) (ok : Bool) (fails : Array String) : Array String :=
    if ok then fails else fails.push name
  -- 1. both classes, both byte orders, at p_paddr and (vaddr) at p_vaddr
  for is64 in [false, true] do
    for be in [false, true] do
      let nm := s!"ELF{if is64 then 64 else 32} {if be then "big" else "little"}-endian"
      let b := mkElf is64 be
      n := n + 2
      fails := expect s!"{nm}: one segment at p_paddr 0x1000 ({summary (parseElf b)})"
        (summary (parseElf b) == segAt 0x1000) fails
      fails := expect s!"{nm} vaddr: one segment at p_vaddr 0x80001000 ({summary (parseElf b true)})"
        (summary (parseElf b true) == segAt 0x80001000) fails
  -- 2. the refusals, each naming its rule
  let refusals : List (String × ByteArray × String) :=
    [ ("PT_INTERP", mkElf false false (ptype := 3), "elf: PT_INTERP present")
    , ("PT_DYNAMIC", mkElf true false (ptype := 2), "elf: PT_DYNAMIC present")
    , ("class 3", mkElf false false (cls := some 3), "elf: class 3 is neither ELFCLASS32 nor ELFCLASS64")
    , ("data 3", mkElf false false (data := some 3), "elf: data encoding 3 is neither little- nor big-endian")
    , ("ET_DYN", mkElf true true (etype := 3), "elf: type ET_DYN")
    , ("truncated", (mkElf false false).extract 0 40, "elf: truncated header")
    , ("bad magic", ⟨#[0x7f, 0x45, 0x4c, 0x47] ++ (mkElf false false).data.extract 4 100⟩, "elf: bad magic") ]
  for (nm, b, msg) in refusals do
    n := n + 1
    let got := summary (parseElf b)
    fails := expect s!"{nm} refused with '{msg}' ({got})" (startsWith got s!"error {msg}") fails
  -- 3. `$readmemh` words: `@` moves the word address, comments are skipped
  n := n + 2
  let hx := parseHexWords "@10 /* c */ 11 // x\n22"
  fails := expect s!"hex @10 with comments ({repr (hx.toOption)})" (hx.toOption == some #[(16, 0x11), (17, 0x22)]) fails
  fails := expect "hex: a bad word is refused" ((parseHexWords "12 zz").toOption.isNone) fails
  -- 4. placement: 4 bytes at base + 4, 32-bit words, little-endian within the word
  n := n + 3
  let img : Image := { segments := #[{ addr := 0x104, bytes := ⟨#[0x78, 0x56, 0x34, 0x12]⟩, memsz := 4 }], entry := 0 }
  fails := expect s!"toWords: word 1 = 0x12345678 ({repr (toWords img 0x100 32 4).toOption})"
    ((toWords img 0x100 32 4).toOption == some #[(1, 0x12345678)]) fails
  fails := expect "toWords: a byte below the base is refused" ((toWords img 0x108 32 4).toOption.isNone) fails
  fails := expect "toWords: a byte past the depth is refused" ((toWords img 0x100 32 1).toOption.isNone) fails
  -- 5. the self-check: a wrong expectation must fail this file's comparison
  n := n + 1
  let selfCheck := expect "self-check" (summary (parseElf (mkElf false false)) == segAt 0x2000) #[]
  fails := expect "self-check: a wrong expectation is reported as a failure" (selfCheck.size == 1) fails
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS image_check: {n} cases"
    return 0
  return 1
