module logic_bitwise_op(
  input  [7:0] a,
  input  [7:0] b,
  output       c,  
  output       d,  
  output [7:0] c2,  
  output [7:0] d2   
);


//assign c = a && b; //strange expression in Verilog
//assign d = a || b;
wire a_bool = a == 8'b0 ? 0 : 1;
wire b_bool = b == 8'b0 ? 0 : 1;

assign c = a_bool && b_bool;
assign d = a_bool || b_bool;
assign c2 = a & b;
assign d2 = a | b;

endmodule
