module bw_overflow(
   input signed [62:0] a
  ,input signed [62:0] b
  ,output reg signed [63:0] out
);
reg [61:0] __p8_0_u;
reg [61:0] __p10_0_u;
always_comb begin
  __p8_0_u = a;
  __p10_0_u = b;
end
always_comb begin
  out = ((__p10_0_u ^ __p8_0_u) + (__p10_0_u ^ __p8_0_u) + 2'sh1);
end
endmodule
