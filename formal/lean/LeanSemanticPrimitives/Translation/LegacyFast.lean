/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.SemanticPrimitives

/- Faster compiled forms for the legacy fast model (finding Q).

`mem_write_be m a v bev byte_w` (SemanticPrimitives, pinned) is
`mem_write m a (masked_word_update (mem_read m a) v bev byte_w)`. Lean compiles
a definition whose result is a function with the result's argument as one more
parameter, so the compiled `mem_write_be` takes the read address `x` too and
evaluates `m a` AND `m x` on every read. A chain of k byte-enable writes then
reads the bottom image 2^k times: measured, 20 writes 2.6 s, and the legacy
model of a memory with masked writes got slower by about 1.4x per step.

`mem_write_be_fast` reads the old word only at the address it writes (`m x`
with `x = a`), so a read calls the image below it ONCE. The `@[csimp]` lemma
swaps it in for compiled code only; every definition and proof keeps
`mem_write_be`. Measured: 100,000 writes 354 ms, same values.

Mathlib-free; the legacy emitter imports it into a design with a byte-enable
write. -/

/-- `mem_write_be`, reading the old word only at the written address. -/
def mem_write_be_fast {addr data be : Nat} (m : (BitVec addr) → (BitVec data))
    (a : BitVec addr) (v : BitVec data) (bev : BitVec be) (byte_w : Nat) :
    (BitVec addr) → (BitVec data) :=
  fun x => if x = a then masked_word_update (m x) v bev byte_w else m x

@[csimp] theorem mem_write_be_eq_fast : @mem_write_be = @mem_write_be_fast := by
  funext addr data be m a v bev byte_w x
  unfold mem_write_be mem_write_be_fast mem_write mem_read
  by_cases h : x = a
  · subst h; simp
  · simp [h]
