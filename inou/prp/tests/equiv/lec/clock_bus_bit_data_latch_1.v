/*
Same latch, the enable bit taken through a wire. Must PROVE.
*/
module cbd(input [1:0] ens, input [3:0] d, output logic [3:0] l);
  wire e0 = ens[0];
  always_latch if (e0) l <= d;
endmodule
