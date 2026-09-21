/*
:lec_top: dut
The identical-pair variant (_1) is the POSITIVE control for latch encoding, so it
must be discharged by the SOLVER. Without this it is answered by the structural
semdiff shortcut ("MATCHED (semdiff structural, no solver)") and proves nothing
about whether the engine can decide a latch at all. pack_local.prp disables it for
the same reason.
:lec_set: formal.lec.semdiff=none
*/
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
