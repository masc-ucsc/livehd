/*
:lec_expect: refused
:lec_grep: normalizes against clock 'clks' but the impl side against 'clks\[1\]'
MUTATED: every clock moved from clks[0] to clks[1]. One bit is used on each
side, but a DIFFERENT one, so the normalized reference clocks differ and the
run must refuse -- never PROVEN.
*/
module cbs(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[1]) n <= q;
endmodule
