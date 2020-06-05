module firrtl_tail (
  output [2:0] out
);

wire [3:0] tmp  = 5 + 9;
wire [2:0] tmp2 = tmp >> 1;
assign out = tmp2;

endmodule 
