module trivial_reduce ( 
input [7:0]a, 
output c, 
output d, 
output e,
output rnand,
output rnor,
output rnxor
);

assign c = &a;
assign d = |a;
assign e = ^a;
assign rnand =  ~&a;
assign rnor = ~|a;
assign rnxor = ~^a;
endmodule   
