// Regression: an async reset written as BIT SLICES silently became SYNCHRONOUS.
//
// The reset arm here spells one constant value (10'b0000000001) across two
// slices. The slang reader's async-rung harvest only accepted a WHOLE-reg
// `NamedValue` write, so it rejected this arm and the whole always block fell
// back to the synchronous demotion -- the reset stopped taking effect off the
// clock edge, which is a behaviour change LEC refutes.
//
// Slices are now accumulated per reg and folded into one `initial=` value once
// every bit is covered; partial coverage (bits that would have to HOLD) still
// demotes, since a reset value cannot express a hold.
module async_reset_bit_slices(input clk, input rst_b, input en, input [9:0] d,
                              output reg [9:0] q);
  always @(posedge clk, negedge rst_b) begin
    if (rst_b == 1'b0) begin
      q[9:1] <= 9'b0;
      q[0]   <= 1'b1;
    end else if (en) q <= d;
  end
endmodule
