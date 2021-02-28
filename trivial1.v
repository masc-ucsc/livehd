module trivial1(
   input signed a
  ,input signed b
  ,input signed c
  ,output reg signed [1:0] y
);
reg a_unsign;
reg b_unsign;
reg c_unsign;
always @(*) begin
  a_unsign = a;
  b_unsign = b;
  c_unsign = c;
end
always @(*) begin
  y = (((a_unsign & b_unsign & c_unsign) | (a_unsign & b_unsign & c_unsign)) & (2'sh1));
end
endmodule
