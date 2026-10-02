/*
:lec_expect: refuted
MUTATED: the latch is gated by ens[1] instead of ens[0]. Must REFUTE.
*/
module cbd(input [1:0] ens, input [3:0] d, output logic [3:0] l);
  always_latch if (ens[1]) l <= d;
endmodule
