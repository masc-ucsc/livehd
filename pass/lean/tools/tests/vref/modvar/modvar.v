// % with a variable divisor.
module modvar(input [7:0] a, input [7:0] b, output [7:0] q);
  assign q = a % b;
endmodule
