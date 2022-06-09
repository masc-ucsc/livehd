module mem_comb1(
   input signed [1:0] a
  ,output reg signed [4:0] out
);
reg [1:0] __p10_0_u;
reg [3:0] __p12_0;
always_comb begin
  __p10_0_u = a[1:0];
   case (__p10_0_u)
     3'd0 : __p12_0 = 5'sha;
     3'd1 : __p12_0 = 5'shb;
     3'd2 : __p12_0 = 5'shc;
     3'd3 : __p12_0 = 5'shd;
       default: __p12_0 = 'hx;
   endcase
end
always_comb begin
  out = __p12_0;
end
endmodule
