/-
# Writable-memory next-image semantics, before any proof support

Step 3 of the memory milestone: pin what the committed semantics ACTUALLY say,
at the value level, so the proof integration is written against measured
behaviour rather than against what the names suggest. Several of these are
policy choices that a reader would guess wrong.

Nothing here generates or proves a theorem, and no real memory design is run.
`reify_design_named` and `prove_reified_incr` still REFUSE memory updates.

Run: lake env lean probes/d3_memory_semantics_test.lean
-/
import LeanSemanticPrimitives.Compiler.Reify

open Compiler Compiler.Residual

-- an 8-bit-wide image that is `addr * 3` everywhere, so every cell is distinct
private def img : Int → BV := fun x => mk_bv 8 (x * 3)
private def V (n : Int) : BV := mk_bv 8 n
private def A (n : Int) : BV := mk_bv 4 n     -- 4-bit address space, 16 cells
private def E (n : Int) : BV := mk_bv 1 n

--------------------------------------------------------------------------------
-- 1. WRITE-DISABLED PRESERVATION. en = 0 leaves the image pointwise unchanged,
--    including at the address that would have been written.
--------------------------------------------------------------------------------
#guard (List.range 16).all fun x =>
  rmemWriteV img (A 5) (V 200) (E 0) (Int.ofNat x) == img (Int.ofNat x)
#guard rmemWriteV img (A 5) (V 200) (E 0) 5 == img 5

--------------------------------------------------------------------------------
-- 2. ENABLED WRITE. Exactly the addressed cell changes; every other cell is the
--    old image, not a default.
--------------------------------------------------------------------------------
#guard rmemWriteV img (A 5) (V 200) (E 1) 5 == V 200
#guard (List.range 16).all fun x =>
  x == 5 || rmemWriteV img (A 5) (V 200) (E 1) (Int.ofNat x) == img (Int.ofNat x)

--------------------------------------------------------------------------------
-- 3. ADDRESS SELECTION AND BOUNDS POLICY -- the part most likely to be guessed
--    wrong. `rmemWriteV` compares `x = bv_uint a` on INTEGERS and applies NO
--    bounds check: the image is a total function, so an address beyond the
--    declared space is written and readable there. The declared `aw` constrains
--    nothing at this layer.
--------------------------------------------------------------------------------
-- the address is the UNSIGNED value of the address operand, after its own width
-- has already truncated it: A 17 at width 4 is 1, so cell 1 is written, not 17
#guard (A 17).value == 1
#guard rmemWriteV img (A 17) (V 200) (E 1) 1 == V 200
#guard rmemWriteV img (A 17) (V 200) (E 1) 17 == img 17
-- but a WIDER address operand reaches past the declared space unchecked
#guard rmemWriteV img (mk_bv 8 100) (V 7) (E 1) 100 == V 7
-- and negative query addresses are simply other cells of the total function
#guard rmemWriteV img (A 5) (V 200) (E 1) (-3) == img (-3)

--------------------------------------------------------------------------------
-- 4. BYTE/BIT MASK. `rmemWriteBEV` masks per BYTE: result bit `i` takes the new
--    datum when mask bit `i / byteW` is set, else the old.
--------------------------------------------------------------------------------
-- byteW = 4, width 8: mask bit 0 covers bits 0-3, mask bit 1 covers bits 4-7
#guard rMaskedUpdateV 8 (V 0x00) (V 0xFF) (mk_bv 2 1) 4 == V 0x0F
#guard rMaskedUpdateV 8 (V 0x00) (V 0xFF) (mk_bv 2 2) 4 == V 0xF0
#guard rMaskedUpdateV 8 (V 0x00) (V 0xFF) (mk_bv 2 3) 4 == V 0xFF
#guard rMaskedUpdateV 8 (V 0xAA) (V 0x55) (mk_bv 2 0) 4 == V 0xAA   -- nothing masked in
-- byteW = 1: the mask is PER BIT
#guard rMaskedUpdateV 8 (V 0x00) (V 0xFF) (mk_bv 8 0b10100101) 1 == V 0xA5
-- the whole-word write is disabled by an ALL-ZERO mask, not by a separate enable
#guard (List.range 16).all fun x =>
  rmemWriteBEV 8 img (A 5) (V 0xFF) (mk_bv 2 0) 4 (Int.ofNat x) == img (Int.ofNat x)
-- and a partial mask merges with the OLD CELL at that address
#guard rmemWriteBEV 8 img (A 5) (V 0xFF) (mk_bv 2 1) 4 5
     == rMaskedUpdateV 8 (img 5) (V 0xFF) (mk_bv 2 1) 4
#guard rmemWriteBEV 8 img (A 5) (V 0xFF) (mk_bv 2 1) 4 6 == img 6

--------------------------------------------------------------------------------
-- 5. READ, AND READ/WRITE FORWARDING. `rmemReadV` returns ZERO when disabled --
--    not the cell, and not the previous read.
--------------------------------------------------------------------------------
#guard rmemReadV 8 img (A 5) (E 0) == mk_bv 8 0
#guard rmemReadV 8 img (A 5) (E 1) == img 5
-- Forwarding IS representable, and it is same-cycle: a read whose image operand
-- is a write's RESULT sees that write. Nothing special implements this -- the
-- write produces a new function and the read applies it.
#guard rmemReadV 8 (rmemWriteV img (A 5) (V 200) (E 1)) (A 5) (E 1) == V 200
-- a read of a DISABLED write sees the old cell, so the forwarding is not
-- unconditional
#guard rmemReadV 8 (rmemWriteV img (A 5) (V 200) (E 0)) (A 5) (E 1) == img 5
-- reading a different address is unaffected by the write
#guard rmemReadV 8 (rmemWriteV img (A 5) (V 200) (E 1)) (A 6) (E 1) == img 6

--------------------------------------------------------------------------------
-- 6. SIMULTANEOUS / OLD-STATE RHS. Two writes to the same image compose in the
--    order they are chained; each sees the image it was GIVEN, so chaining from
--    the same old image is not the same as chaining one onto the other.
--------------------------------------------------------------------------------
-- chained: the second write sees the first
#guard rmemWriteV (rmemWriteV img (A 5) (V 200) (E 1)) (A 6) (V 201) (E 1) 5 == V 200
#guard rmemWriteV (rmemWriteV img (A 5) (V 200) (E 1)) (A 6) (V 201) (E 1) 6 == V 201
-- two writes to the SAME cell: the outer one wins
#guard rmemWriteV (rmemWriteV img (A 5) (V 200) (E 1)) (A 5) (V 201) (E 1) 5 == V 201
-- both built from the OLD image independently: neither sees the other, which is
-- what a design with two memory updates reading the same source image gets
#guard rmemWriteV img (A 5) (V 200) (E 1) 6 == img 6
#guard rmemWriteV img (A 6) (V 201) (E 1) 5 == img 5

#eval IO.println "D3MEMSEM OK"
