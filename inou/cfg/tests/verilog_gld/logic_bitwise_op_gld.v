module logic_bitwise_op(
  input  [7:0] a,
  input  [7:0] b,
  output       c,  
  output       d,  
  output [7:0] c2,  
  output [7:0] d2   
);


assign c = a && b;
assign d = a || b;
assign c2 = a & b;
assign d2 = a | b;

endmodule
