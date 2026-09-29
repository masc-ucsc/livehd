// Behavioral golden for loop_rca_flat.prp.  Deliberately flat: the Pyrope side
// has eight rca_bit instances, while this side has no matching hierarchy.
module \loop_rca_flat.top (
  input  [7:0] a,
  input  [7:0] b,
  output [8:0] sum
);
  assign sum = {1'b0, a} + {1'b0, b};
endmodule

