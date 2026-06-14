module top(input [3:0] a, input [3:0] b, output [4:0] s);
  adder u_adder(.a(a), .b(b), .s(s));
endmodule
