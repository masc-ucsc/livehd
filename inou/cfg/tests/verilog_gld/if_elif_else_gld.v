module if_elif_else(a, b, out);
  input  [7:0] a;
  input  [7:0] b;
  output [7:0] out;
  
  wire [7:0] mux1 = a == 2'd2 ? a & b : mux2;
  wire [7:0] mux2 = a == 2'd3 ? a | b : a ^ b;
  assign out = mux1 ;

endmodule
