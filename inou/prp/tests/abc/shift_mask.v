/*
:top: shift_mask
:lec_set: pass.satopt=true
An explicit satopt must optimize only the elaborated LEC sides, never the
--lib model library, which has one top per cell.
*/
module shift_mask(input [7:0] data, input [15:0] amount, output [7:0] masked, shifted);
  assign masked = data & ~(8'hff << amount);
  assign shifted = data << amount;
endmodule
