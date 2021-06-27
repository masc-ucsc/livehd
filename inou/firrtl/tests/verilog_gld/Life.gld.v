module Cell(
   input signed reset
  ,input signed clock
  ,input signed [1:0] \io_neighbors_0 
  ,input signed [1:0] \io_neighbors_1 
  ,input signed [1:0] \io_neighbors_2 
  ,input signed [1:0] \io_neighbors_3 
  ,input signed [1:0] \io_neighbors_4 
  ,input signed [1:0] \io_neighbors_5 
  ,input signed [1:0] \io_neighbors_6 
  ,input signed [1:0] \io_neighbors_7 
  ,input signed [1:0] \io_running 
  ,input signed [1:0] \io_writeEnable 
  ,input signed [1:0] \io_writeValue 
  ,output reg signed [1:0] \io_out 
);
reg signed [1:0] \#isAlive ;
reg signed [1:0] \___next_#isAlive  ;
reg \#isAlive_6 ;
reg signed [1:0] t_pin52_0;
reg ___unsign_t_pin52_0;
reg signed [2:0] t_pin58_0;
reg [1:0] ___unsign_t_pin58_0;
reg signed [3:0] t_pin63_0;
reg [2:0] ___unsign_t_pin63_0;
reg signed [3:0] t_pin68_0;
reg [2:0] ___unsign_t_pin68_0;
reg signed [3:0] t_pin73_0;
reg [2:0] ___unsign_t_pin73_0;
reg signed [3:0] t_pin79_0;
reg [2:0] ___unsign_t_pin79_0;
reg signed [3:0] t_pin84_0;
reg [2:0] ___unsign_t_pin84_0;
reg signed [3:0] t_pin88_0;
reg [2:0] ___unsign_t_pin88_0;
reg signed \#isAlive_11 ;
reg signed t_pin103_0;
reg ___unsign_t_pin103_0;
reg signed [1:0] \#isAlive_15 ;
reg signed [1:0] \#isAlive_16 ;
reg \___unsign_io_writeEnable ;
reg \___unsign_io_writeValue ;
reg \___unsign_io_neighbors_0 ;
reg \___unsign_io_neighbors_1 ;
reg \___unsign_io_neighbors_2 ;
reg \___unsign_io_neighbors_3 ;
reg \___unsign_io_neighbors_4 ;
reg \___unsign_io_neighbors_5 ;
reg \___unsign_io_neighbors_6 ;
reg \___unsign_io_neighbors_7 ;
reg \___unsign_io_running ;
reg \___unsign_#isAlive_16 ;
always_comb begin
  \___unsign_io_writeEnable  = \io_writeEnable [0:0];
  \___unsign_io_writeValue  = \io_writeValue [0:0];
  \___unsign_io_neighbors_0  = \io_neighbors_0 [0:0];
  \___unsign_io_neighbors_1  = \io_neighbors_1 [0:0];
  \___unsign_io_neighbors_2  = \io_neighbors_2 [0:0];
  \___unsign_io_neighbors_3  = \io_neighbors_3 [0:0];
  \___unsign_io_neighbors_4  = \io_neighbors_4 [0:0];
  \___unsign_io_neighbors_5  = \io_neighbors_5 [0:0];
  \___unsign_io_neighbors_6  = \io_neighbors_6 [0:0];
  \___unsign_io_neighbors_7  = \io_neighbors_7 [0:0];
  \___unsign_io_running  = \io_running [0:0];
   if (\___unsign_io_writeEnable ) begin
     \#isAlive_6  = \___unsign_io_writeValue ;
   end else begin
     \#isAlive_6  = \#isAlive ;
   end
  t_pin52_0 = (4'sh7) & ((1'sh0) + \___unsign_io_neighbors_7 );
  ___unsign_t_pin52_0 = t_pin52_0;
  t_pin58_0 = (4'sh7) & (___unsign_t_pin52_0 + \___unsign_io_neighbors_6 );
  ___unsign_t_pin58_0 = t_pin58_0;
  t_pin63_0 = (4'sh7) & (___unsign_t_pin58_0 + \___unsign_io_neighbors_5 );
  ___unsign_t_pin63_0 = t_pin63_0;
  t_pin68_0 = (4'sh7) & (___unsign_t_pin63_0 + \___unsign_io_neighbors_4 );
  ___unsign_t_pin68_0 = t_pin68_0;
  t_pin73_0 = (4'sh7) & (___unsign_t_pin68_0 + \___unsign_io_neighbors_3 );
  ___unsign_t_pin73_0 = t_pin73_0;
  t_pin79_0 = (4'sh7) & (___unsign_t_pin73_0 + \___unsign_io_neighbors_2 );
  ___unsign_t_pin79_0 = t_pin79_0;
  t_pin84_0 = (4'sh7) & (___unsign_t_pin79_0 + \___unsign_io_neighbors_1 );
  ___unsign_t_pin84_0 = t_pin84_0;
  t_pin88_0 = (4'sh7) & (___unsign_t_pin84_0 + \___unsign_io_neighbors_0 );
  ___unsign_t_pin88_0 = t_pin88_0;
   if ((___unsign_t_pin88_0 < (3'sh2))) begin
     \#isAlive_11  = (1'sh0);
   end else begin
     \#isAlive_11  = (___unsign_t_pin88_0 < (4'sh4));
   end
  t_pin103_0 = (\#isAlive  == (1'sh0)) & (___unsign_t_pin88_0 == (3'sh3));
  ___unsign_t_pin103_0 = t_pin103_0;
   if (\#isAlive ) begin
     \#isAlive_15  = \#isAlive_11 ;
   end else begin
     \#isAlive_15  = ___unsign_t_pin103_0;
   end
   if (((1'sh0) == \___unsign_io_running )) begin
     \#isAlive_16  = \#isAlive_6 ;
   end else begin
     \#isAlive_16  = \#isAlive_15 ;
   end
  \___unsign_#isAlive_16  = \#isAlive_16 [0:0];
end
always_comb begin
  \io_out  = \#isAlive ;
  \___next_#isAlive  = \___unsign_#isAlive_16 ;
end
always @(posedge clock ) begin
if (reset) begin
\#isAlive  <= 1'sh0;
end else begin
\#isAlive  <= \___next_#isAlive ;
end
end
endmodule
module Life(
   input signed reset
  ,input signed clock
  ,input signed [1:0] \io_running 
  ,input signed [1:0] \io_writeColAddress 
  ,input signed [2:0] \io_writeRowAddress 
  ,input signed [1:0] \io_writeValue 
  ,output reg signed [1:0] \io_state_0 
  ,output reg signed [1:0] \io_state_1 
);
reg t_pin49_14;
reg t_pin34_14;
reg t_pin95_0;
reg t_pin56_0;
reg t_pin97_0;
reg t_pin99_0;
reg t_pin101_0;
reg signed t_pin55_0;
reg [1:0] \___unsign_io_writeRowAddress ;
reg \___unsign_io_writeColAddress ;
Cell i_nid35(
.io_neighbors_0(t_pin34_14)
,.io_neighbors_1(t_pin34_14)
,.io_neighbors_2(t_pin34_14)
,.io_neighbors_3(t_pin49_14)
,.io_neighbors_4(t_pin49_14)
,.io_neighbors_5(t_pin34_14)
,.io_neighbors_6(t_pin34_14)
,.io_neighbors_7(t_pin34_14)
,.io_out(t_pin49_14)
,.io_running(t_pin99_0)
,.io_writeEnable(t_pin56_0)
,.io_writeValue(t_pin101_0)
);
Cell i_nid20(
.io_neighbors_0(t_pin49_14)
,.io_neighbors_1(t_pin49_14)
,.io_neighbors_2(t_pin49_14)
,.io_neighbors_3(t_pin34_14)
,.io_neighbors_4(t_pin34_14)
,.io_neighbors_5(t_pin49_14)
,.io_neighbors_6(t_pin49_14)
,.io_neighbors_7(t_pin49_14)
,.io_out(t_pin34_14)
,.io_running(t_pin95_0)
,.io_writeEnable(t_pin56_0)
,.io_writeValue(t_pin97_0)
);
always_comb begin
  \___unsign_io_writeColAddress  = \io_writeColAddress [0:0];
  t_pin95_0 = \io_running [0:0];
  t_pin97_0 = \io_writeValue [0:0];
  t_pin99_0 = \io_running [0:0];
  t_pin101_0 = \io_writeValue [0:0];
  \___unsign_io_writeRowAddress  = \io_writeRowAddress [1:0];
  t_pin55_0 = ((2'sh1) == \___unsign_io_writeRowAddress ) & ((1'sh0) == \___unsign_io_writeColAddress );
  t_pin56_0 = t_pin55_0;
end
always_comb begin
  \io_state_0  = t_pin34_14;
  \io_state_1  = t_pin49_14;
end
endmodule
