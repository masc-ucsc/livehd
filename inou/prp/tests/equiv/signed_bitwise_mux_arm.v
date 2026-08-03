// FIXME tracker -- a BITWISE op drops its operands' signedness, and a mux arm
// is where that becomes visible.
//
// `sa | 2'sb11` is -1 (both operands signed), but tolg's lower_op ends in
// bind_result, which stamps every result UNSIGNED -- and an unsigned pin is
// DEFINED to carry an always-0 spare sign bit. Widening the merge to the 16-bit
// output therefore ZERO-fills: y reads 0x0007 where the golden says 0xffff
// (c=1, sa=0, sb=0).
//
// Only the mux exposes it. A BARE `sa | 2'sb11` assigned straight to y is
// correct, because cgen's Get_mask path re-declares an undeclared operand as
// signed; putting the Or behind a mux changes cgen's visit order so the
// re-declare is skipped. That is the same order-dependence that was fixed for a
// subtracting Sum (see lower_op's `set_sign` and tests/equiv/signed_neg_mux).
//
// ATTEMPTED AND REVERTED (2026-08-02): `set_sign(out)` on And/Or/Xor whenever
// ANY operand can be negative. It fixes this pair but MISCOMPILES 128 of 2048
// vectors on tmp/chigen_fuzz/opfuzz.py --only-nested --seed 7 --depth 6,
// because Verilog resolves a bitwise op UNSIGNED as soon as ONE operand is
// unsigned -- and a CONCAT is always unsigned, so `signed_expr ^ {a,b}` is an
// unsigned expression. The ANY rule is the exact opposite of Verilog's, and the
// reader does not always pre-insert the widening node that would make the op's
// own stamp irrelevant.
//
// FIX: carry the reader's already-resolved signedness down to the op (the slang
// front end knows whether each expression is signed) instead of re-deriving it
// from operand pins in tolg.
//
// Found by opfuzz reducing the tmp/chimera vloghammer wideexpr_* refutations.
module signed_bitwise_mux_arm(input c, input signed [2:0] sa, input signed [1:0] sb, output [15:0] y);
  assign y = c ? (sa | 2'sb11) : (c ? sa : sb);
endmodule
