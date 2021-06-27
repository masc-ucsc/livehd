module SimpleClockGroupSource(
   input signed [1:0] reset
  ,input signed clock
  ,output reg signed \auto_out_member_1.clock 
  ,output reg signed [1:0] \auto_out_member_1.reset 
  ,output reg signed \auto_out_member_2.clock 
  ,output reg signed [1:0] \auto_out_member_2.reset 
  ,output reg signed \auto_out_member_3.clock 
  ,output reg signed [1:0] \auto_out_member_3.reset 
  ,output reg signed \auto_out_member_4.clock 
  ,output reg signed [1:0] \auto_out_member_4.reset 
  ,output reg signed \auto_out_member_5.clock 
  ,output reg signed [1:0] \auto_out_member_5.reset 
  ,output reg signed \auto_out_member_clock 
  ,output reg signed [1:0] \auto_out_member_reset 
);
reg ___unsign_reset;
reg ___unsign_t_pin92_0;
reg ___unsign_t_pin94_0;
reg ___unsign_t_pin96_0;
reg ___unsign_t_pin99_0;
reg ___unsign_t_pin101_0;
reg ___unsign_t_pin103_0;
reg ___unsign_t_pin105_0;
reg ___unsign_t_pin107_0;
reg ___unsign_t_pin110_0;
reg ___unsign_t_pin112_0;
always_comb begin
  ___unsign_reset = reset[0:0];
  ___unsign_t_pin92_0 = ___unsign_reset[0:0];
  ___unsign_t_pin94_0 = ___unsign_t_pin92_0[0:0];
  ___unsign_t_pin96_0 = ___unsign_t_pin94_0[0:0];
  ___unsign_t_pin99_0 = ___unsign_t_pin96_0[0:0];
  ___unsign_t_pin101_0 = ___unsign_t_pin99_0[0:0];
  ___unsign_t_pin103_0 = ___unsign_t_pin101_0[0:0];
  ___unsign_t_pin105_0 = ___unsign_t_pin103_0[0:0];
  ___unsign_t_pin107_0 = ___unsign_t_pin105_0[0:0];
  ___unsign_t_pin110_0 = ___unsign_t_pin107_0[0:0];
  ___unsign_t_pin112_0 = ___unsign_t_pin110_0[0:0];
end
always_comb begin
  \auto_out_member_5.clock  = clock;
  \auto_out_member_5.reset  = reset;
  \auto_out_member_4.clock  = clock;
  \auto_out_member_4.reset  = reset;
  \auto_out_member_3.clock  = clock;
  \auto_out_member_3.reset  = reset;
  \auto_out_member_2.clock  = clock;
  \auto_out_member_2.reset  = reset;
  \auto_out_member_1.clock  = clock;
  \auto_out_member_1.reset  = reset;
  \auto_out_member_clock  = clock;
  \auto_out_member_reset  = reset;
end
endmodule
