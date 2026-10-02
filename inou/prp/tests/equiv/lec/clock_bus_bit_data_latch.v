/*
:lec_top: cbd
:lec_set: formal.phase_sched=false formal.timeout=60
A DATA latch whose enable is one bit of a multi-bit input. Its enable is not a
clock (it keeps its own enable through normalization and takes its slot from
the state its enable reads), so the bus bit is irrelevant -- yet the clock
bus-bit decline in pass.single_edge refused it (rc 7) with the phase schedule
off. _1 respells the select through a wire and must PROVE; _2 gates the latch
from ens[1] instead and must REFUTE.
*/
module cbd(input [1:0] ens, input [3:0] d, output logic [3:0] l);
  always_latch if (ens[0]) l <= d;
endmodule
