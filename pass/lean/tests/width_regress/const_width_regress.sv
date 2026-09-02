// Regression fixtures for constant-width resolution in pass.lean.
//
// Each output exercises one case from the resolver in `pin_width`
// (pass/lean/pass_lean.cpp).  The bug these exist for: an UNSIZED constant was
// modeled at width 1, so 0x6000000 became 0.  It passed step 5 (the fast model
// and the certificate share the extractor, so both truncated the same way) and
// passed the LEC gate (which compares RTL to LGraph, not the model to either).
//
// Expected values are asserted by pass/lean/tests/width_regress/check.py, which
// evaluates the EMITTED Lean model and compares against these comments.

module const_width_regress (
  input  logic [31:0] a,
  input  logic [4:0]  sh,
  output logic [28:0] o_sra_pos_const,   // 0x6000000 >>> 4          = 0x600000
  output logic [28:0] o_sra_neg_const,   // -32 >>> 2                = -8
  output logic [57:0] o_wide_const,      // 58'h00c000000000010
  output logic [31:0] o_shl_const,       // 0x123 << 8               = 0x12300
  output logic [31:0] o_and_wide,        // a & 0x6000000
  output logic [63:0] o_big_const,       // 64'hDEADBEEFCAFEBABE
  output logic        o_eq_const         // a == 0x6000000
);

  // UNSIZED positive constant whose minimal width (27) would set the sign bit
  // of a signed read.  Must be modeled at >= the 29-bit operation width.
  assign o_sra_pos_const = $signed(29'h6000000) >>> 4;

  // Negative constant: get_bits() already counts the sign bit.
  assign o_sra_neg_const = $signed(-29'sd32) >>> 2;

  // Wide constant with set bits far above 64: exercises the arbitrary-width path.
  assign o_wide_const = 58'h00c000000000010;

  // Shift AMOUNT is a constant (the pre-existing shift_dep_width path) while the
  // shifted DATA is also constant.
  assign o_shl_const = 32'h123 << 8;

  assign o_and_wide = a & 32'h6000000;

  assign o_big_const = 64'hDEADBEEFCAFEBABE;

  assign o_eq_const = (a == 32'h6000000);

endmodule
