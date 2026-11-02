module top(input [7:0] x, output [63:0] o);
  wire [15:0]      _sm4ed_T_1 = {8'h0, x} ^ {x, 8'h0};
  wire [33:0]      _sm4ed_T_8 =
    {8'h0,
     {10'h0, _sm4ed_T_1[15:10], _sm4ed_T_1[9:0] ^ {x, 2'h0}}
       ^ {x, 18'h0}} ^ {2'h0, x[5:0], 26'h0};
  wire [33:0]      sm4ed =
    {_sm4ed_T_8[33:18], _sm4ed_T_8[17:0] ^ {x[7:6], 16'h0}};
assign o = {sm4ed[31:0],sm4ed[31:0]};
endmodule
