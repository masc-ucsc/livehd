// Exercises signed/unsigned driver-pin handling end-to-end: signed and
// unsigned arithmetic, signed comparison, arithmetic vs logical shift, sign
// extension on width growth, and a signed register. The yosys equivalence
// check (lgcheck) validates that tolg populates pin_signed correctly and that
// cgen respects it on the round trip.
module signs(
    input              clk
  , input  signed [3:0] sa
  , input  signed [3:0] sb
  , input        [3:0] ua
  , input        [3:0] ub
  , input        [2:0] sh
  , output signed [7:0] sadd     // signed add, sign-extended
  , output       [7:0] uadd     // unsigned add, zero-extended
  , output signed [7:0] smul     // signed multiply
  , output             slt      // signed less-than
  , output             ult      // unsigned less-than
  , output signed [3:0] sshr     // arithmetic right shift (signed)
  , output       [3:0] ushr     // logical right shift (unsigned)
  , output signed [7:0] sext     // pure sign extension
  , output reg signed [7:0] sreg // signed register
);

  assign sadd = sa + sb;
  assign uadd = ua + ub;
  assign smul = sa * sb;
  assign slt  = sa < sb;
  assign ult  = ua < ub;
  assign sshr = sa >>> sh;
  assign ushr = ua >>  sh;
  assign sext = sa;            // sign-extend 4 -> 8 bits

  always @(posedge clk)
    sreg <= sa + sb;

endmodule
