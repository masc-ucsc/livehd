module firrtl_tail (
  output [2:0] out
);

wire [3:0] tmp  = 5 + 9;
assign out = tmp[2:0]; //out = 6

endmodule 
