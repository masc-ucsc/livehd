// `dly` is a one-cycle delay; `top` adds one more stage flop after it (the
// `stage[2]` over a call that lands at cycle 1), so `x` is `a` two cycles ago.
module \timecheck_false_stage_mismatch.dly (
  input            clock,
  input      [3:0] d,
  output reg [3:0] q
);

  always @(posedge clock) q <= d;

endmodule

module \timecheck_false_stage_mismatch.top (
  input        clock,
  input  [3:0] a,
  input  [3:0] b,
  output [3:0] o
);

  wire [3:0] q;
  reg  [3:0] x;

  \timecheck_false_stage_mismatch.dly u_dly (.clock(clock), .d(a), .q(q));

  always @(posedge clock) x <= q;

  assign o = x ^ b;

endmodule
