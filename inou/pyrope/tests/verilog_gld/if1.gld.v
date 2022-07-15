module if1(
   input signed inp
  ,output reg signed [3:0] out
);
reg signed [3:0] x_4;
reg signed [3:0] x_5;
always_comb begin
   if ((|(2'sh1 == inp))) begin
     x_4 = 4'sh5;
   end else begin
     x_4 = 4'sh6;
   end
   if ((|('b0 == inp))) begin
     x_5 = 4'sh4;
   end else begin
     x_5 = x_4;
   end
end
always_comb begin
  out = x_5;
end
endmodule
