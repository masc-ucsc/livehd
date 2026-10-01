// Golden for loop_ring_own_carry.prp: w = b ^ 0x5555 (each nibble XOR 0x5),
// and output lane i picks bit (sel[2i+1:2i]*4 + i) of w.
module loop_ring_own_carry (
  input  [15:0] b,
  input  [7:0]  sel,
  output [3:0]  o
);
  wire [15:0] w = b ^ 16'h5555;
  assign o[0] = w[{sel[1:0], 2'd0} + 0];
  assign o[1] = w[{sel[3:2], 2'd0} + 1];
  assign o[2] = w[{sel[5:4], 2'd0} + 2];
  assign o[3] = w[{sel[7:6], 2'd0} + 3];
endmodule
