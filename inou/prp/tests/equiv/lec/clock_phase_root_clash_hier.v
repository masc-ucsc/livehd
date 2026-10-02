/*
:lec_top: cph
Hierarchical variant of clock_phase_root_clash. The child `cph_sub` names its
clock ports `a`/`b`, the top drives them from `c0`/`c1`. Under the default
hierarchical LEC the per-def phase plan names each root through the CLOCK
FOREST (a top input, `c0`), not the child's own port, so the root-agreement
check -- which looked the root up among the CHILD's inputs -- never fired, and
moving every flop from `a` (c0) to `b` (c1) (_1) was PROVEN. _2 is the
unmutated control.
*/
module cph_sub(input a, input b, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge a) q <= d;
  always @(negedge a) n <= q;
endmodule
module cph(input c0, input c1, input [3:0] d, output [3:0] q, output [3:0] n);
  cph_sub u(.a(c0), .b(c1), .d(d), .q(q), .n(n));
endmodule
