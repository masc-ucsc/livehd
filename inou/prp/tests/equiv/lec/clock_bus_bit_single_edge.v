/*
:lec_top: cbs
:lec_set: formal.phase_sched=false formal.timeout=60
ONE bit of a multi-bit clock input drives every clock in a design that NEEDS
edge normalization (a negedge flop). pass.single_edge keys a clock by its input
net and once declined ANY clock on a bus bit, so with the phase schedule off
(the only path `lhd formal verify` has) this came back rc 7 although no two
clocks can be conflated. Only two DIFFERENT bits of one bus are ambiguous
(variant _2, a sound UNKNOWN). _1 respells the select through a wire and must
PROVE; _3 moves EVERY clock to clks[1] and must never be PROVEN (normalizing it
to the bare input would silently read clks[0]).
*/
module cbs(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge clks[0]) q <= d;
  always @(negedge clks[0]) n <= q;
endmodule
