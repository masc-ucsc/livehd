module bw_overflow(
   input [62:0] a
  ,input [62:0] b
  ,output reg signed [64:0] out
);
reg [62:0] __p8_0_u;
reg [62:0] __p10_0_u;
always_comb begin
  __p8_0_u = a;
  __p10_0_u = b;
end
always_comb begin
  out = (((__p10_0_u ^ __p8_0_u) + 2'sh1) ^ __p10_0_u ^ __p8_0_u);
end
endmodule
