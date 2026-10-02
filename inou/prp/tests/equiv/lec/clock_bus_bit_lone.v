/*
:lec_top: cbo
:lec_set: formal.phase_sched=false formal.timeout=60
Every clock is bit 1 of a multi-bit input, in a design that needs edge
normalization (a negedge flop). One bit used alone is one clock, so
pass.single_edge normalizes it -- rebinding every clock_pin to a SELECT of that
bit, since a bare bus would read its edge on bit 0 (clks[0]). _1 respells the
select through a wire and must PROVE; _2 changes the negedge flop's data and
must REFUTE (the proof is not vacuous).
*/
module cbo(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[1]) n <= q;
endmodule
