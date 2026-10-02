/*
:lec_top: cbn
Bits of one clock BUS in a design with a NEGEDGE flop, which hands the clocks to
edge normalization (pass.single_edge) and the formal phase schedule. Both key a
clock by its input net (latch_contract::control_root follows a bit select to
the whole input), so clks[0] and clks[1] were one clock: moving flop `s` to
clks[1] (variant _1) was PROVEN as "structurally identical". Neither can tell the
bits apart, so both now DECLINE and the run is a sound UNKNOWN.
*/
module cbn(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] s, output reg [3:0] n);
  always @(posedge clks[0]) s <= d;
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[0]) n <= s;
endmodule
