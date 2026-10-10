# Grounding `SRA` / `Get_mask` / `Sext` against the LiveHD spec

**Question this answers:** is
[`docs/05-lgraph`](https://masc-ucsc.github.io/docs/livehd/05-lgraph/) — the
official LiveHD documentation, and the normative spec for this project — precise
enough to write a Lean operator semantics *against*?

This matters because it gates the verified-compiler work. Today a wrong
`eval_op` case surfaces as a *failed* per-design proof when the emitter
disagrees. After the compiler is verified, a wrong `eval_op` case is silently
baked in and **proved correct**. The spec stops being documentation and becomes
load-bearing.

Three operators were chosen because each has already produced a divergence:
Bug 1 (`Sext` side condition), Bug 4 (`Get_mask` all-ones spelling), Bug 10
(`SRA` widening — a real mistranslation).

## Verdict

**No, not for these three.** The spec is silent on the two questions that caused
real bugs, and **contradicts both implementations** on the third.

| | what the spec says | is it decisive? |
|---|---|---|
| `SRA` widening | *"Arithmetic (sign preserving) shift right: `Y = a >>> b`"*, and *"LGraph only has arithmetic right shift"* | **No** — no forward-propagation rule, and nothing about the result width exceeding the operand width. That is exactly Bug 10's question. |
| `Get_mask` packing | *"extract the bits selected by mask"*, plus `get_mask(a,-1) == zext(a)` | **No** — "extract" does not say whether selected bits are compacted to the low end or left in place. |
| `Sext` operand `b` | *"sign-extend from bit position `b`"* | **Contradicts the implementations** — see below. |

The spec explicitly gives forward-propagation (max/min) rules for `Sum`, `Mult`,
`Div`, `Not`, `And` and the comparators. It gives **none** for `SRA`,
`Get_mask` or `Sext`.

The one general statement that bears on this:

> "most LGraph cell types generate the same result if the input is sign-extended.
> This has the advantage of simplifying the decisions of when to drop bits in a
> value."

That is about *inputs*, not about result width.

## What the implementations actually do

Executable checks against `eval_op` (`Translation/LGraphModel.lean`), read
alongside `inou/cgen/cgen_sim.cpp`.

### `SRA` — widening sign-extends. All three agree.

```lean
-- 65-bit all-ones operand (= -1 signed), shift 0, result width 192 — Bug 10's exact shape
#eval (bv_uint sra0 == 2^192 - 1, bv_uint sra0 == 2^65 - 1)   -- ⇒ (true, false)
#eval bv_sint (eval_op Op_SRA 8 [mk_bv 8 (-8), mk_bv 8 2])    -- ⇒ -2
```

- **`eval_op`**: `bv_sra w x s = mk_bv w (bv_sint x / 2^s)` — reads the operand
  **signed**, so a negative result sign-fills to `w`.
- **`cgen_sim`**: `operand(e[0].driver, wbits, /*signed=*/1)`, commented *"The
  shifted operand of an ARITHMETIC shift must be read as signed."*
- **`pass_lean.cpp`**: emits `bv_sext` when `w > vw`, `bv_zext` otherwise — the
  Bug 10 fix.

Consistent with the spec's "sign preserving", though the spec does not state it.

### `Get_mask` — selected bits are PACKED to the low end. Spec silent.

```lean
-- mask 0b1010 over value 0b1010
#eval bv_uint (eval_op Op_GetMask 8 [mk_bv 8 10, mk_bv 8 10])   -- ⇒ 3   (0b11)
#eval bv_uint (eval_op Op_GetMask 8 [mk_bv 4 13, mk_bv 4 (-1)]) -- ⇒ 13  (= zext ✓)
```

`3`, not `10` — the bits at positions 1 and 3 are **compacted** into positions 0
and 1. `eval_op` does this via `pack_low_bv … (mask_indices_bv m).reverse`;
`cgen_sim`'s `get_mask_op` is a per-bit loop with the same effect, and its
low-contiguous fast path is commented *"keeps the low n bits and zeroes the rest,
**packed LSB-first in place**"*.

The documented identity `get_mask(a,-1) == zext(a)` holds.

**Both implementations agree. A reader of the spec alone could not have chosen
between packed and in-place** — and "extract the bits selected by mask" arguably
suggests in-place.

### `Sext` — the spec is off by one.

```lean
-- a = 0b1000 at 8 bits
#eval bv_sint (eval_op Op_Sext 8 [mk_bv 8 8, mk_bv 8 4])   -- ⇒ -8
#eval bv_sint (eval_op Op_Sext 8 [mk_bv 8 8, mk_bv 8 5])   -- ⇒  8
```

`b = 4` gives **−8**. Read the spec literally — *"sign-extend from bit position
`b`"* — bit 4 of `0b1000` is `0`, so the result would be **+8**. The spec's
reading is wrong by one.

Both implementations treat `b` as a **width/count**: keep `b` bits `[b-1:0]`,
sign bit at `b-1`.

- **`eval_op`**: `u := bv_uint a % 2^n`, then `if u < 2^(n-1) then u else u - 2^n`
  — sign bit at `n-1`.
- **`cgen_sim`** says so in a comment written by someone who hit this:

  > *"LGraph `Sext(a, b)` keeps `b` bits `[b-1:0]` (cgen_verilog emits
  > `a[b-1:0]`), i.e. the sign bit is at `b-1`. `Slop::sext_op(fb)` takes `fb` as
  > the sign-bit position, so pass `b-1` (NOT `b`) or the value stays
  > unsigned/off-by-one."*

The cross-reference to `cgen_verilog` emitting `a[b-1:0]` is the tiebreaker:
three artifacts (`eval_op`, `cgen_sim`, `cgen_verilog`) agree that `b` is a
width. **The spec's wording is the outlier and should be corrected upstream.**

## A coverage gap found along the way

`pass_lean.cpp`'s fast model for `Sext` **ignores the amount operand entirely**:

```cpp
return "((bv_sext " + driver_expr(ctx, drivers[0]) + ") : BitVec " + w + ")";
```

`bv_sext` extends from the *operand's own* width. That is only equal to
`eval_op`'s "sign bit at `b-1`" in two cases, which is exactly what the two
bridges require:

| bridge | side condition | meaning |
|---|---|---|
| `sext_bridge` | `amt.toNat = wa` | amount equals the **operand** width |
| `sext_bridge_low` | `amt.toNat = w ∧ w ≤ wa` | amount equals the **result** width, truncating |

Any node with `amt ∉ {wa, w}` — e.g. `Sext(a[31:0], 8)` into a 32-bit result —
satisfies neither, both `rw`s fail, and the design **fails to typecheck**.

That is a *loud* failure, not a silent mis-model, so nothing unsound has shipped.
But the fast model cannot express "sign-extend from bit `b-1`" for a general `b`,
and a verified compiler will have to, because `compileNode_correct` must cover
the whole `Op_Sext` case rather than the two shapes DINO and CVA6 happen to use.

## Consequences for the three branches

1. **The spec cannot be transcribed mechanically.** Writing `DesignInterpreter`
   from `docs/05-lgraph` alone would get `Sext` wrong by one and would have to
   guess on `Get_mask`. Every operator needs the same triangulation used here:
   spec (normative) → `cgen_sim`/`cgen_verilog` (empirical) → executable check.
2. **`eval_op` is in better shape than expected.** All three operators agree with
   `cgen_sim`. No divergence was found — the risk was real but did not
   materialise here.
3. **The `Sext` spec wording should be reported upstream.** Until it is, the
   comment in `cgen_sim.cpp` is the more reliable statement, and any Lean
   semantics should cite it rather than the doc.
4. **`Op_Sext` needs a general bridge** before a verified compiler can close its
   case — the two width-specific bridges are not exhaustive.

## Reproducing

```bash
cd formal/lean && lake env lean <<'EOF'
import LeanSemanticPrimitives.Translation.LGraphModel
#eval bv_uint (eval_op LGraphOp.Op_GetMask 8 [mk_bv 8 10, mk_bv 8 10])     -- 3  ⇒ packed
#eval bv_sint (eval_op LGraphOp.Op_Sext   8 [mk_bv 8 8,  mk_bv 8 4])       -- -8 ⇒ b is a width
EOF
```
