module Ops(
  input  [3:0]  sel,
  input  [7:0]  is,
  input  [7:0]  iu,
  output [13:0] os,
  output [12:0] ou,
  output        obool
);
  wire [1:0] _GEN_1 = sel == 4'h4 ? 2'h2 : 2'h0;
  wire [1:0] _GEN_2 = sel == 4'h5 ? 2'h2 : _GEN_1;
  wire [1:0] _GEN_3 = sel == 4'h4 ? 2'h0 : _GEN_2;
  wire [1:0] _GEN_4 = sel == 4'h5 ? 2'h2 : _GEN_3;
  wire [8:0] _GEN_5 = sel == 4'h4 ? 9'h0 : {{7'd0}, _GEN_4};
  wire [8:0] _GEN_6 = sel == 4'h5 ? is + iu : _GEN_5;
  wire [8:0] _GEN_7 = sel == 4'h4 ? is + iu : _GEN_6;
  wire [8:0] _GEN_9 = sel == 4'h3 ? 9'h0 : _GEN_7;
  wire [8:0] _GEN_10 = sel == 4'h2 ? $signed(9'sh1) : $signed(9'sh0);
  wire [8:0] _GEN_11 = sel == 4'h2 ? 9'h1 : _GEN_9;
  wire [8:0] _GEN_12 = sel == 4'h1 ? $signed(9'sh0) : $signed(_GEN_10);
  wire [8:0] _GEN_13 = sel == 4'h1 ? 9'h0 : _GEN_11;
  wire [8:0] _GEN_14 = sel == 4'h0 ? $signed($signed(is) + $signed(is)) : $signed(_GEN_12);
  wire [8:0] _GEN_15 = sel == 4'h0 ? iu + iu : _GEN_13;
  assign os = {{5{_GEN_14[8]}},_GEN_14};
  assign ou = {{4'd0}, _GEN_15};
  assign obool = 1'h0;
endmodule
