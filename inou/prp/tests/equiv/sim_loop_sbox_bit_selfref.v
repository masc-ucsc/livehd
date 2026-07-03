// Golden for sim_loop_sbox_bit_selfref: acyclic per-bit form of the Pyrope
// self-referential `wire` pack (w[0]=a[0], w[1]=w[0]^a[1], w[2]=w[1]^a[2]).
module sim_loop_sbox_bit_selfref(input [2:0] a, output [2:0] z);
  wire b0 = a[0];
  wire b1 = b0 ^ a[1];
  wire b2 = b1 ^ a[2];
  assign z = {b2, b1, b0};
endmodule
