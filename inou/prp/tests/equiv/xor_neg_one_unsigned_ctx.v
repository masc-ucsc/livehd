module xor_neg_one_unsigned_ctx(input [7:0] a, input [2:0] b, input [7:0] c, input [2:0] s,
                                output [7:0] o, output [7:0] p);
  wire [15:0] bs = {13'b0, b} << s;
  wire [15:0] cs = {8'b0, c} << s;
  assign o = (a & ~bs[7:0]) | cs[7:0];
  assign p = (a ^ ~bs[7:0]) | cs[7:0];
endmodule
