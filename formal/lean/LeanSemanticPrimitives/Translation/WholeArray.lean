/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.SemanticPrimitives

/- Whole-array memory operators for the legacy fast model (decision #2).

The fast model keeps a memory as a function `BitVec a → BitVec d`. A WHOLE-ARRAY
cell loads every entry from one bus (`Op_MemLoad n`: the bulk `update`, or the
reset value) and reads every entry as one bus (`Op_MemToBus n`: cgen's
`read_all`). These are the fast forms; `WholeArrayBridge` proves each equal to
the certificate's `cert_mem_load` / `cert_mem_to_bus` on encoded values.

Mathlib-free, so a legacy model WITHOUT its proof bridge can import it. Not part
of the package root: the emitter imports it only into a design that uses it. -/

/-- Load entries `0 .. n-1` from `bus`, entry `i` from bits `[i*d, (i+1)*d)`;
every other entry keeps `m`'s word. -/
def mem_load {a d bw : Nat} (m : BitVec a → BitVec d) (bus : BitVec bw) (n : Nat) :
    BitVec a → BitVec d :=
  fun x => if x.toNat < n then BitVec.ofNat d (bus.toNat >>> (x.toNat * d)) else m x

/-- Entries `0 .. k-1` of `m`, each cut to `e` bits, packed with entry 0 lowest.
An index at or past `2^a` is not an address of `m` and packs as 0. -/
def mem_pack {a d : Nat} (e : Nat) (m : BitVec a → BitVec d) : Nat → Nat
  | 0 => 0
  | k + 1 => mem_pack e m k + (if k < 2 ^ a then (m (BitVec.ofNat a k)).toNat % 2 ^ e else 0) * 2 ^ (k * e)

/-- Entries `0 .. n-1` as one `w`-bit bus, each `w / n` bits, entry 0 lowest. -/
def mem_to_bus {a d : Nat} (m : BitVec a → BitVec d) (n w : Nat) : BitVec w :=
  BitVec.ofNat w (mem_pack (w / n) m n)
