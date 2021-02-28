module trivial_and(
   input signed [1:0] a
  ,input signed [1:0] b
  ,output reg signed [2:0] g
);
reg [1:0] b_unsign;
reg [1:0] a_unsign;
always @(*) begin
  b_unsign = b;
  a_unsign = a;
end
always @(*) begin
  g = ((3'sh3) & b_unsign & a_unsign);
end
endmodule
