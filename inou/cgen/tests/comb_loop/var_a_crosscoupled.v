// Variant A: TWO pure-comb sub instances cross-coupled.
//   u1.x -> u2.c   and   u2.x -> u1.c
// A correct atomic schedule EXISTS (x1,x2 are pure functions of primary inputs),
// but atomic Sub eval cannot separate each sub's x-output from its y-output, so
// forward_class sees a real 2-node SCC and BOTH fed-back inputs become
// create_integer(0) /*UNRESOLVED*/. Silently wrong, no diagnostic.
//
// Golden (a1=10,b1=20,d1=5 ; a2=2,b2=4,d2=7):
//   x1 = a1+b1 = 30 ; x2 = a2+b2 = 6
//   o1 = y1 = c1+d1 = x2[7:0]+d1 = 6+5  = 11
//   o2 = y2 = c2+d2 = x1[7:0]+d2 = 30+7 = 37
// Buggy cgen_sim: c1=c2=0 -> o1=5, o2=7.
module subadd(input  [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
              output [8:0] x, output [8:0] y);
  assign x = a + b;
  assign y = c + d;
endmodule

module top(input  [7:0] a1, input [7:0] b1, input [7:0] d1,
           input  [7:0] a2, input [7:0] b2, input [7:0] d2,
           output [8:0] o1, output [8:0] o2);
  wire [8:0] x1, x2;
  subadd u1(.a(a1), .b(b1), .c(x2[7:0]), .d(d1), .x(x1), .y(o1));
  subadd u2(.a(a2), .b(b2), .c(x1[7:0]), .d(d2), .x(x2), .y(o2));
endmodule
