// HANG repro #2 (multiply distributes over add). Pair with distrib_ref.v.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a*b + a*c;
endmodule
