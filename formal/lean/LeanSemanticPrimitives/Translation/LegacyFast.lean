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

universe u

/-- A mux as a CALL, not an inline `if` (finding AC). Lean's compiler turns each
`if` inside a long chain of live lets into a join point nested in the previous
one: a 10,000-let `next` with 3,863 of them (VX_operands) did not compile in
36 min; the same body with every mux written as `lmux` compiled in under 2 s.
`noinline` keeps it a call. The condition is a `Bool`, never a `Prop` with a
`Decidable` instance: the compiler inlines that instance at every call (for a
Bool condition `c`, `Decidable (c = true)` is a `match` on `c`), which puts one
branch per mux back into the body (measured: the VX_operands build was again in
the same LCNF pass after 77 min). Both arms are values the model computes anyway
(a let, a memory read of a total function, a division that is total in Lean), so
evaluating them eagerly changes no result -- in a let chain. Only the fast
model's let chain uses `lmux`: where each node is its own function of the inputs
and state (the bridge model), evaluating both arms calls the operands' functions
again, which is exponential, so those keep the `if`. `lmux_eq` unfolds it to the
`if` it names; `lmux_beq` turns the emitter's `==` tests into `if x = y`. -/
@[noinline] def lmux {α : Sort u} (c : Bool) (a b : α) : α := if c then a else b

@[simp] theorem lmux_eq {α : Sort u} (c : Bool) (a b : α) :
    lmux c a b = if c then a else b := rfl

@[simp] theorem lmux_beq {α : Sort u} {β : Type} [BEq β] [LawfulBEq β] [DecidableEq β] (x y : β) (a b : α) :
    lmux (x == y) a b = if x = y then a else b := by
  unfold lmux; by_cases h : x = y <;> simp [h]
