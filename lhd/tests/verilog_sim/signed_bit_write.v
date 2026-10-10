// A constant bit write into a signed local reaches its sign bit: `t[1] = 1` on
// `t = 2'b01` is 2'b11 == -1, not +3 (an unbounded set_mask kept the old sign
// extension above the declared width).
module signed_bit_write(input clock, input reset, input [1:0] a, input b, output signed [7:0] o);
  reg signed [1:0] t;
  always @(*) begin
    t = a;
    t[1] = b;
  end
  assign o = t;
endmodule
