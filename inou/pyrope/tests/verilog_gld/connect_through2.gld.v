module \connect_through2.sub (
   output reg signed [7:0] xx1
  ,output reg signed [7:0] xx2
);
always_comb begin
  xx2 = (8'sh58);
  xx1 = (8'sh4d);
end
endmodule
module connect_through2(
   output reg signed [8:0] out1
);
reg signed [7:0] t_pin24_1;
reg signed [7:0] t_pin25_2;
\connect_through2.sub  \i___c_0:yy1_0:connect_through2.sub (
.xx1(t_pin25_2)
,.xx2(t_pin24_1)
);
always_comb begin
  out1 = (t_pin25_2 + t_pin24_1);
end
endmodule
