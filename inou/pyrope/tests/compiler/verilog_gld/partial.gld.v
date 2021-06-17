module partial(
   input signed [1:0] a_2
  ,input signed [3:0] a_4
  ,output reg signed [6:0] out_c1
  ,output reg signed [6:0] out_c2
  ,output reg signed [6:0] out_c3
  ,output reg signed [3:0] out_c4
  ,output reg signed [6:0] out_c5
);

always_comb begin
  out_c1      = 6'h2f;
  out_c1[0]   = a_2[0];
  out_c1      = out_c1 ^ 4'sh7;

  out_c2      = 6'h2f;
  out_c2[1:0] = a_2;
  out_c2      = out_c2 ^ 4'sh7;

  out_c3      = 6'h2f;
  out_c3[0]   = a_4[0];
  out_c3      = out_c3 ^ 4'sh7;

  out_c4      = 6'h2f;
  out_c4      = a_4;
  out_c4      = out_c4 ^ 4'sh4;

  out_c5      = 6'h2f;
  out_c5[1:3] = a_4[2:0];
  out_c5      = out_c5 ^ 4'sh7;
end

endmodule
