// Golden for sim_loop_pack_shiftand: same false word-level loop shape --
// pk is a pack of 1-bit flags and two flags read pk's own bits back.
// Bit-level acyclic, so the continuous-assign network settles:
//   pk[1] = a[0];  pk[0] = pk[1] & a[1];  pk[2] = ~(pk[1] | pk[0]).
module sim_loop_pack_shiftand(input [1:0] a, output [2:0] z);
  wire       ii;
  wire       vi;
  wire       wen;
  wire [2:0] pk;
  assign ii  = a[0];
  assign vi  = ((pk >> 1) & 3'b001) & a[1];
  assign wen = ~(((pk >> 1) & 3'b001) | (pk & 3'b001));
  assign pk  = {wen, ii, vi};
  assign z   = pk;
endmodule
