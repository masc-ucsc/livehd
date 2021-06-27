module Adder4(
   input signed reset
  ,input signed clock
  ,input signed [4:0] \io_A 
  ,input signed [4:0] \io_B 
  ,input signed [1:0] \io_Cin 
  ,output reg signed [1:0] \io_Cout 
  ,output reg signed [4:0] \io_Sum 
);
reg t_pin32_0;
reg t_pin39_0;
reg t_pin149_0;
reg t_pin25_6;
reg t_pin26_7;
reg signed [1:0] t_pin29_0;
reg signed [1:0] t_pin37_0;
reg t_pin54_0;
reg t_pin60_0;
reg t_pin151_0;
reg t_pin48_6;
reg t_pin49_7;
reg signed [1:0] t_pin52_0;
reg signed [1:0] t_pin58_0;
reg signed [1:0] t_pin63_0;
reg [1:0] ___unsign_t_pin63_0;
reg t_pin80_0;
reg t_pin87_0;
reg t_pin153_0;
reg t_pin73_6;
reg t_pin74_7;
reg signed [1:0] t_pin76_0;
reg signed [1:0] t_pin85_0;
reg signed [2:0] t_pin91_0;
reg [2:0] ___unsign_t_pin91_0;
reg t_pin109_0;
reg t_pin114_0;
reg t_pin156_0;
reg t_pin103_6;
reg t_pin104_7;
reg signed [1:0] t_pin106_0;
reg signed [1:0] t_pin112_0;
reg signed [3:0] t_pin117_0;
reg [3:0] ___unsign_t_pin117_0;
reg [3:0] \___unsign_io_A ;
reg [3:0] \___unsign_io_B ;
FullAdder i_nid97(
.io_a(t_pin109_0)
,.io_b(t_pin114_0)
,.io_cin(t_pin156_0)
,.io_cout(t_pin104_7)
,.io_sum(t_pin103_6)
);
FullAdder i_nid67(
.io_a(t_pin80_0)
,.io_b(t_pin87_0)
,.io_cin(t_pin153_0)
,.io_cout(t_pin74_7)
,.io_sum(t_pin73_6)
);
FullAdder i_nid19(
.io_a(t_pin32_0)
,.io_b(t_pin39_0)
,.io_cin(t_pin149_0)
,.io_cout(t_pin26_7)
,.io_sum(t_pin25_6)
);
FullAdder i_nid42(
.io_a(t_pin54_0)
,.io_b(t_pin60_0)
,.io_cin(t_pin151_0)
,.io_cout(t_pin49_7)
,.io_sum(t_pin48_6)
);
always_comb begin
  t_pin63_0 = ((t_pin48_6 << (2'sh1))) | t_pin25_6;
  ___unsign_t_pin63_0 = t_pin63_0;
  \___unsign_io_B  = \io_B [3:0];
  t_pin149_0 = \io_Cin [0:0];
  t_pin151_0 = t_pin26_7[0:0];
  t_pin153_0 = t_pin49_7[0:0];
  t_pin156_0 = t_pin74_7[0:0];
  \___unsign_io_A  = \io_A [3:0];
  t_pin29_0 = (\___unsign_io_A  >>> (1'sh0)) & (2'sh1);
  t_pin32_0 = t_pin29_0;
  t_pin37_0 = (\___unsign_io_B  >>> (1'sh0)) & (2'sh1);
  t_pin39_0 = t_pin37_0;
  t_pin52_0 = (\___unsign_io_A  >>> (2'sh1)) & (2'sh1);
  t_pin54_0 = t_pin52_0;
  t_pin58_0 = (\___unsign_io_B  >>> (2'sh1)) & (2'sh1);
  t_pin60_0 = t_pin58_0;
  t_pin76_0 = (\___unsign_io_A  >>> (3'sh2)) & (2'sh1);
  t_pin80_0 = t_pin76_0;
  t_pin85_0 = (\___unsign_io_B  >>> (3'sh2)) & (2'sh1);
  t_pin87_0 = t_pin85_0;
  t_pin91_0 = ((t_pin73_6 << (3'sh2))) | ___unsign_t_pin63_0;
  ___unsign_t_pin91_0 = t_pin91_0;
  t_pin106_0 = (\___unsign_io_A  >>> (3'sh3)) & (2'sh1);
  t_pin109_0 = t_pin106_0;
  t_pin112_0 = (\___unsign_io_B  >>> (3'sh3)) & (2'sh1);
  t_pin114_0 = t_pin112_0;
  t_pin117_0 = ((t_pin103_6 << (3'sh3))) | ___unsign_t_pin91_0;
  ___unsign_t_pin117_0 = t_pin117_0;
end
always_comb begin
  \io_Sum  = ___unsign_t_pin117_0;
  \io_Cout  = t_pin104_7;
end
endmodule
module FullAdder(
   input signed [1:0] reset
  ,input signed clock
  ,input signed [1:0] \io_a 
  ,input signed [1:0] \io_b 
  ,input signed [1:0] \io_cin 
  ,output reg signed [1:0] \io_cout 
  ,output reg signed [1:0] \io_sum 
);
reg signed t_pin31_0;
reg ___unsign_t_pin31_0;
reg signed t_pin36_0;
reg ___unsign_t_pin36_0;
reg signed [1:0] t_pin41_0;
reg ___unsign_t_pin41_0;
reg signed [1:0] t_pin44_0;
reg ___unsign_t_pin44_0;
reg signed [1:0] t_pin48_0;
reg ___unsign_t_pin48_0;
reg signed t_pin51_0;
reg ___unsign_t_pin51_0;
reg signed t_pin54_0;
reg ___unsign_t_pin54_0;
reg \___unsign_io_a ;
reg \___unsign_io_b ;
reg \___unsign_io_cin ;
always_comb begin
  \___unsign_io_a  = \io_a [0:0];
  \___unsign_io_b  = \io_b [0:0];
  \___unsign_io_cin  = \io_cin [0:0];
  t_pin31_0 = \___unsign_io_a  ^ \___unsign_io_b ;
  ___unsign_t_pin31_0 = t_pin31_0;
  t_pin36_0 = ___unsign_t_pin31_0 ^ \___unsign_io_cin ;
  ___unsign_t_pin36_0 = t_pin36_0;
  t_pin41_0 = \___unsign_io_a  & \___unsign_io_b ;
  ___unsign_t_pin41_0 = t_pin41_0;
  t_pin44_0 = \___unsign_io_b  & \___unsign_io_cin ;
  ___unsign_t_pin44_0 = t_pin44_0;
  t_pin48_0 = \___unsign_io_a  & \___unsign_io_cin ;
  ___unsign_t_pin48_0 = t_pin48_0;
  t_pin51_0 = ___unsign_t_pin41_0 | ___unsign_t_pin44_0;
  ___unsign_t_pin51_0 = t_pin51_0;
  t_pin54_0 = ___unsign_t_pin51_0 | ___unsign_t_pin48_0;
  ___unsign_t_pin54_0 = t_pin54_0;
end
always_comb begin
  \io_sum  = ___unsign_t_pin36_0;
  \io_cout  = ___unsign_t_pin54_0;
end
endmodule
