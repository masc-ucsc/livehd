/*
:top: shift_mask
*/
module shift_mask(input [7:0] data, input [15:0] amount, output [7:0] masked, shifted);
  assign masked = data & ~(8'hff << amount);
  assign shifted = data << amount;
endmodule
