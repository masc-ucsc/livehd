// Golden for const_bit_select.prp. The Pyrope selects bits [12..=15] (entirely
// in the sign-extension region, above the 9-bit width) of two signed values:
//   - `cur = 0sb1????????` (sign bit KNOWN = 1) -> those 4 bits are all 1 = 4'hf
//   - `b` (signed [8:0])                        -> those 4 bits are all b[8]
// so `o = sel ? 4'hf : {4{b[8]}}`. The unknown low bits of `cur` never reach the
// selected range, so the result is fully determined.
//
// The bug this guards (see the .prp header): lowering the .prp through cgen used
// to emit `(9'sb1????????)[15:12]` — a part-select on a parenthesized constant —
// which slang rejects, so `lhd lec` ERRORED (the verification.html
// InvalidSelectExpression category). FIXED (2026-07-01): cgen now computes the
// sliced constant directly, so `prp-equiv-const_bit_select` proves against this
// golden.
module \const_bit_select.top (
  input                sel,
  input  signed [8:0]  b,
  output       [3:0]   o
);
  assign o = sel ? 4'hf : {4{b[8]}};
endmodule
