module trivial(
   input signed a
  ,input signed b
  ,output reg signed [1:0] c
);
reg b_unsign;
reg a_unsign;
always @(*) begin
  b_unsign = b;
  a_unsign = a;
end
always @(*) begin
  c = ((b_unsign ^ a_unsign) & (2'sh1));
end
endmodule
