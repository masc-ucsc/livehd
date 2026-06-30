// Variant C: a GENUINE combinational loop through a sub (the soundness guard).
//
// passthru is x = a+1; the parent drives the sub input a from x itself:
//   b = x[7:0] + din ;  u.a = b   ==>  x = a+1 = x + din + 1
// This has NO fixpoint -- it is a real comb loop. cgen_sim today ALSO silently
// emits create_integer(0) for u.a (it does not distinguish false vs genuine
// loops). The fix MUST keep this an ERROR (never silently resolve to 0); it must
// NOT be "fixed" into producing a value.
//
// Expected after the fix: a loud combinational-loop diagnostic, non-zero exit.
module passthru(input [7:0] a, output [8:0] x);
  assign x = a + 1;
endmodule

module top(input [7:0] din, output [8:0] o);
  wire [8:0] x;
  wire [7:0] b;
  passthru u(.a(b), .x(x));     // x = a + 1
  assign b = x[7:0] + din;      // GENUINE loop: b depends on x, a=b -> x = x+din+1
  assign o = x;
endmodule
