// FALSE combinational loop through a pure-comb sub-instance (the base bug).
//
// subadd is purely combinational: x=a+b (cone {a,b}), y=c+d (cone {c,d}).
// `top` instantiates it ONCE and closes a loop OUTSIDE the instance:
//   u.x -> c = x[7:0] + topx -> u.c
// There is NO real bit-level cycle (x does not depend on c), but at the
// instance/node level it is Sub -> adder -> Sub, a cycle.
//
// cgen_sim treats the Sub atomically (one child.cycle(), all-inputs->all-outputs),
// so the back-edge input `c` is read before it can be computed and is silently
// emitted as `create_integer(0) /*UNRESOLVED*/` -> y_out is WRONG, no diagnostic.
// cgen_verilog (declarative always_comb) handles the same design correctly.
//
// Golden (a=10,b=20,d=5,topx=3): x_out=30, c=33, y_out = c+d = 38.
// Buggy cgen_sim today:           x_out=30, c=0,  y_out = 0+d = 5.
module subadd(input  [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
              output [8:0] x, output [8:0] y);
  assign x = a + b;     // cone {a,b} only
  assign y = c + d;     // cone {c,d} only
endmodule

module top(input  [7:0] a, input [7:0] b, input [7:0] d, input [7:0] topx,
           output [8:0] x_out, output [8:0] y_out);
  wire [8:0] x;
  wire [7:0] c;
  subadd u(.a(a), .b(b), .c(c), .d(d), .x(x), .y(y_out));
  assign c     = x[7:0] + topx;   // FALSE loop: u.x -> c -> u.c
  assign x_out = x;
endmodule
