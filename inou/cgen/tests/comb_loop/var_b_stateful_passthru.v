// Variant B: a STATEFUL sub (has a flop, so hhds stamps it loop_break / Type 3)
// that ALSO exposes a pure-comb passthrough output `x=a+b`, whose value is fed
// back through parent logic into the sub's own input `c`.
//
// This is the proof that the "mark comb subs as loop_break" hack is WRONG.
// Because the Sub is loop_break, the false loop u.x -> c -> u.c is "broken" at
// the module boundary -- but that just relocates the missing same-cycle update
// into the flop: the fed-back input `c` is a BOGUS value that LATCHES into the
// flop next-state. The comb passthrough x is correct, but the registered output
// is silently wrong on the NEXT cycle.
//
// Two manifestations of the SAME root cause have been observed:
//   * pure-comb path (see base/var_a):  c = create_integer(0) /*UNRESOLVED*/,
//     which the Stage 0 safety net catches (a valid non-const driver with no
//     binding is provably a comb-cycle back-edge).
//   * loop_break path (this fixture, current HEAD): cprop cannot resolve the
//     atomic loop_break Sub's output inside the false loop, so the fed-back net
//     is folded to an UNKNOWN-BITS const `0ub0?????????` in top.cpp's cycle();
//     `q` then latches that unknown -> wrong. Stage 0 does NOT flag this (an
//     unknown-bits const also occurs legitimately in correct designs, so a
//     blanket guard would false-positive). var_b is therefore covered by the
//     Stage 1 cycle detector (which identifies the loop_break Sub on the comb
//     cycle), not by Stage 0. Until then, repro.sh still flags it as present.
//
// Golden (a=10,b=20,d=3): x=30, c=x[7:0]+d=33, after one posedge q = c+1 = 34.
// Buggy cgen_sim today:    x=30 (passthru ok) but c=bogus -> q wrong (unknown/1).
module subps(input clk, input [7:0] a, input [7:0] b, input [7:0] c,
             output [8:0] x,            // PURE COMB passthrough: cone {a,b}
             output reg [8:0] q);        // REGISTERED: q <= c+1 (depends on fed-back c)
  assign x = a + b;
  always @(posedge clk) q <= c + 1;
endmodule

module top(input clk, input [7:0] a, input [7:0] b, input [7:0] d,
           output [8:0] xo, output [8:0] qo);
  wire [8:0] x;
  subps u(.clk(clk), .a(a), .b(b), .c(x[7:0] + d), .x(x), .q(qo));  // x -> c feedback
  assign xo = x;
endmodule
