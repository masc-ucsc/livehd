module get_mask1(
   input [9:0] in
  ,output reg signed [7:0] out1
  ,output reg signed [10:0] out2
  ,output reg signed [7:0] out3
  ,output reg signed [3:0] out4
  ,output reg signed [3:0] out5
);
reg [7:0] t_pin97_unsign;
reg [10:0] t_pin105_unsign;
reg [7:0] t_pin110_unsign;
reg signed t_pin88;
reg [10:0] in_unsign;
reg [10:0] in_unsign;
reg [10:0] in_unsign;
always_comb begin
  t_pin88 = 1'sb1;
  in_unsign = in;
  in_unsign = in;
  in_unsign = in;
  t_pin97_unsign = in_unsign[7:4];
  t_pin105_unsign = in_unsign;
  t_pin110_unsign = in_unsign[7:0];
end
always_comb begin
  out5 = (5'sha);
  out1 = t_pin97_unsign;
  out4 = (5'shd);
  out3 = t_pin110_unsign;
  out2 = t_pin105_unsign;
end
endmodule
