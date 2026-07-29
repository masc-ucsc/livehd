// Regression: cprop folding an And's constants to ZERO.
//
// `x & 1 & 0` is 0 for every x, but a zero fold shares the "identity" path used
// by Sum's 0 and Or's 0 -- cprop dropped the constants and forwarded `x`, so
// the `& 0` vanished and the emitted netlist returned the comparison instead of
// a constant 0. Zero is And's ANNIHILATOR, not its identity.
module and_zero_annihilator(input signed [1:0] sb, output [15:0] y);
  assign y = (~&sb) & (~|3'b101);
endmodule
