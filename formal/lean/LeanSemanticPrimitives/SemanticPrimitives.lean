/-
pass.lean shared helpers — Lean 4 port of formal/semantic_primitives/SemanticPrimitives.thy.
Lean 4.31.0, BitVec native.
-/

--------------------------------------------------------------------------------
-- Helpers
--------------------------------------------------------------------------------

def intAbs (a : Int) : Int := if a < 0 then -a else a

def bv_zext {a b : Nat} (x : BitVec a) : BitVec b :=
  BitVec.ofNat b x.toNat

def bv_sext {a b : Nat} (x : BitVec a) : BitVec b :=
  BitVec.ofInt b x.toInt

def bitvec_nonzero {w : Nat} (x : BitVec w) : Bool :=
  x ≠ 0#w

def bool_to_bv1 (b : Bool) : BitVec 1 :=
  if b then 1#1 else 0#1

--------------------------------------------------------------------------------
-- sem_udiv
--------------------------------------------------------------------------------

def sem_udiv {w : Nat} (a b : BitVec w) : BitVec w :=
  if b = 0#w then 0#w else a.udiv b

--------------------------------------------------------------------------------
-- trunc_div_int
--------------------------------------------------------------------------------

def trunc_div_int (a b : Int) : Int :=
  let an := intAbs a |>.toNat
  let bn := intAbs b |>.toNat
  let q  := if bn = 0 then 0 else (an / bn : Nat)
  if (a < 0) ≠ (b < 0) then -((q : Int)) else (q : Int)

--------------------------------------------------------------------------------
-- sem_sdiv
--------------------------------------------------------------------------------

def sem_sdiv {w : Nat} (a b : BitVec w) : BitVec w :=
  if b = 0#w then 0#w
  else BitVec.ofInt w (trunc_div_int a.toInt b.toInt)

--------------------------------------------------------------------------------
-- sem_sra
--------------------------------------------------------------------------------

def sem_sra {w n : Nat} (x : BitVec w) (shamt : BitVec n) : BitVec w :=
  x.sshiftRight shamt.toNat

--------------------------------------------------------------------------------
-- sem_ror_bool
--------------------------------------------------------------------------------

def sem_ror_bool (bs : List Bool) : BitVec 1 :=
  if bs.any id then 1#1 else 0#1

--------------------------------------------------------------------------------
-- bit_mask_at
--------------------------------------------------------------------------------

def bit_mask_at {w : Nat} (i : Nat) : BitVec w :=
  (1#w) <<< i

--------------------------------------------------------------------------------
-- mask_indices
--------------------------------------------------------------------------------

def mask_indices {w : Nat} (m : BitVec w) : List Nat :=
  (List.range w).filter fun i => m.getLsbD i

--------------------------------------------------------------------------------
-- pack_low
--------------------------------------------------------------------------------

def pack_low {a b : Nat} (x : BitVec a) (is : List Nat) : BitVec b :=
  match is with
  | [] => 0#b
  | i :: is' =>
    let packed := pack_low x is'
    let len    := is'.length
    if x.getLsbD i then
      ((1#b) <<< len) ||| packed
    else
      packed

--------------------------------------------------------------------------------
-- sem_get_mask
--------------------------------------------------------------------------------

def sem_get_mask {a m b : Nat} (x : BitVec a) (mask : BitVec m) : BitVec b :=
  pack_low x (mask_indices mask).reverse

--------------------------------------------------------------------------------
-- sem_set_mask
--   Uses `|||` with bit_mask_at and `&&& ~~~mbit` to clear.
--   No `setLsb` in Lean 4.31 BitVec — we operate with bitwise masks.
--------------------------------------------------------------------------------

def sem_set_mask {a m v : Nat} (acc : BitVec a) (mask : BitVec m) (val : BitVec v) : BitVec a :=
  let idxs  := mask_indices mask
  let pairs := List.zip (List.range idxs.length) idxs
  pairs.foldl (fun acc' (p : Nat × Nat) =>
    let j    := p.1
    let i    := p.2
    let mbit := (bit_mask_at i : BitVec a)
    if val.getLsbD j then acc' ||| mbit
    else acc' &&& ~~~mbit)
    acc

--------------------------------------------------------------------------------
-- sem_shl_many (reference-only)
--------------------------------------------------------------------------------

def sem_shl_many {a b : Nat} (x : BitVec a) (bs : List (BitVec b)) : BitVec a :=
  bs.foldl (fun acc b => ((x <<< b.toNat) ||| acc)) 0#a

--------------------------------------------------------------------------------
-- flop_next
--------------------------------------------------------------------------------

def flop_next {w : Nat} (reset_active : Bool) (initial_value : BitVec w)
    (enable_active : Bool) (din current : BitVec w) : BitVec w :=
  if reset_active then initial_value
  else if enable_active then din
  else current

--------------------------------------------------------------------------------
-- Function-valued memories
--------------------------------------------------------------------------------

def mem_read {addr data : Nat} (m : (BitVec addr) → (BitVec data))
    (a : BitVec addr) : BitVec data :=
  m a

def mem_write {addr data : Nat} (m : (BitVec addr) → (BitVec data))
    (a : BitVec addr) (v : BitVec data) : (BitVec addr) → (BitVec data) :=
  fun x => if x = a then v else m x

--------------------------------------------------------------------------------
-- Byte-enable support
--------------------------------------------------------------------------------

def byte_mask_word (byte_w byte_idx : Nat) {d : Nat} : BitVec d :=
  let w := (1#d <<< byte_w) - 1#d
  w <<< (byte_idx * byte_w)

def byte_enable_mask {be d : Nat} (bev : BitVec be) (byte_w : Nat) : BitVec d :=
  (List.range be).foldl (fun (acc : BitVec d) (byte_idx : Nat) =>
    if bev.getLsbD byte_idx then
      acc ||| (byte_mask_word byte_w byte_idx (d := d))
    else acc)
    0#d

def masked_word_update {d be : Nat} (old new : BitVec d)
    (bev : BitVec be) (byte_w : Nat) : BitVec d :=
  let mask := byte_enable_mask bev byte_w (d := d)
  (old &&& ~~~mask) ||| (new &&& mask)

def mem_write_be {addr data be : Nat} (m : (BitVec addr) → (BitVec data))
    (a : BitVec addr) (v : BitVec data) (bev : BitVec be) (byte_w : Nat) :
    (BitVec addr) → (BitVec data) :=
  mem_write m a (masked_word_update (mem_read m a) v bev byte_w)

--------------------------------------------------------------------------------
-- SRAM 1RW read-first
--------------------------------------------------------------------------------

def sram_1rw_read_first {addr data : Nat} (we : Bool) (addr_w : BitVec addr)
    (wdata : BitVec data) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let rdata := mem_read m addr_w
  let m' := if we then mem_write m addr_w wdata else m
  (m', rdata)

def sram_1rw_write_first {addr data : Nat} (we : Bool) (addr_w : BitVec addr)
    (wdata : BitVec data) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let m' := if we then mem_write m addr_w wdata else m
  (m', mem_read m' addr_w)

def sram_1rw_be_read_first {addr data be : Nat} (we : Bool) (addr_w : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : Nat)
    (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let rdata := mem_read m addr_w
  let m' := if we then mem_write_be m addr_w wdata bev byte_w else m
  (m', rdata)

def sram_1rw_be_write_first {addr data be : Nat} (we : Bool) (addr_w : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : Nat)
    (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let m' := if we then mem_write_be m addr_w wdata bev byte_w else m
  (m', mem_read m' addr_w)

--------------------------------------------------------------------------------
-- SRAM 1R1W read-first
--------------------------------------------------------------------------------

def sram_1r1w_read_first {addr data : Nat} (we : Bool) (waddr : BitVec addr)
    (wdata : BitVec data) (raddr : BitVec addr) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let rdata := mem_read m raddr
  let m' := if we then mem_write m waddr wdata else m
  (m', rdata)

def sram_1r1w_write_first {addr data : Nat} (we : Bool) (waddr : BitVec addr)
    (wdata : BitVec data) (raddr : BitVec addr) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let m' := if we then mem_write m waddr wdata else m
  (m', mem_read m' raddr)

def sram_1r1w_be_read_first {addr data be : Nat} (we : Bool) (waddr : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : Nat)
    (raddr : BitVec addr) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let rdata := mem_read m raddr
  let m' := if we then mem_write_be m waddr wdata bev byte_w else m
  (m', rdata)

def sram_1r1w_be_write_first {addr data be : Nat} (we : Bool) (waddr : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : Nat)
    (raddr : BitVec addr) (m : (BitVec addr) → (BitVec data)) :
    ((BitVec addr) → (BitVec data)) × (BitVec data) :=
  let m' := if we then mem_write_be m waddr wdata bev byte_w else m
  (m', mem_read m' raddr)

--------------------------------------------------------------------------------
-- SRAM synchronous read register update
--------------------------------------------------------------------------------

def sram_sync_read_reg_next {d : Nat} (ren : Bool) (read_value current : BitVec d) : BitVec d :=
  if ren then read_value else current
