// Regression: left-shifting a SIGNED operand, signal and constant.
//
// Two distinct bugs meet here:
//  * cgen widened a shift's left operand with `{N{1'b0}} | val`, an UNSIGNED
//    OR, so a signed operand zero-extended and `sa << 0` turned -4 into +4.
//  * mw_of_val() returned 1 for EVERY negative constant, so `(-3) << ua` was
//    sized from magnitude 1 and its 9-bit intermediate wrapped `-3 << 7` from
//    -384 to +128.
module signed_shift_widen(input signed [2:0] sa, input [2:0] ua,
                          output [15:0] y0, output [15:0] y1, output [15:0] y2);
  assign y0 = sa << ua;
  assign y1 = sa <<< 3'sb101;
  assign y2 = 3'sb101 << ua;
endmodule
