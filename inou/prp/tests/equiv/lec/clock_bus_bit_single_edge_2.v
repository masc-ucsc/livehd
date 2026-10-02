/*
:lec_expect: refused
:lec_grep: clock-bus-bit.*`clks` clocks state on bit 0, bit 1
MUTATED: flop `q` moved from clks[0] to clks[1]. Two bits of one bus clock the
design, which edge normalization cannot key apart: a sound refusal, never PROVEN.
*/
module cbs(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[0]) n <= q;
endmodule
