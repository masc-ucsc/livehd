module logic_bitwise_op(
  input  [7:0] a,
  input  [7:0] b,
  output       c,  
  output       d,  
  output [7:0] c2,  
  output [7:0] d2   
);


assign c = a && b; //strange expression in Verilog
assign d = a || b;
// assign c = (a != 8'b0) && (b != 8'b0);
// assign d = (a != 8'b0) || (b != 8'b0);
assign c2 = a & b;
assign d2 = a | b;

endmodule
